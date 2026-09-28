use std::fs;
use std::net::UdpSocket;
use std::path::{Path, PathBuf};
use std::process::Child;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Mutex;

use serde::{Deserialize, Serialize};
use specta::Type;
use tauri::{AppHandle, Manager, State};

use crate::models::{CourseMode, GameSettings, GenerateRomRequest, LaunchRequest, Lives, Role};
use crate::paths::{
    create_log_dir, find_input_script, find_melonds_binary, load_launcher_settings,
};
use crate::process_job::ChildProcessJob;
use crate::processes::{build_melon_command, capture_child_stdio, process_state, terminate_child};
use crate::state::{AppState, LaunchPreparationGuard};

#[derive(Debug, Clone, Deserialize, Serialize, Type)]
pub(crate) struct SoloNetwork {
    pub(crate) delay_frames: u8,
    pub(crate) jitter_frames: u8,
    pub(crate) drop_every: u16,
}

#[derive(Debug, Clone, Copy, Deserialize, Serialize, Type)]
#[serde(rename_all = "snake_case")]
pub(crate) enum SoloControl {
    Mario,
    Luigi,
    Both,
}

#[derive(Debug, Clone, Copy, Deserialize, Serialize, Type)]
#[serde(rename_all = "snake_case")]
pub(crate) enum CpuOpponent {
    Beginner,
    CombatV2,
    Development,
}

impl CpuOpponent {
    fn profile(self) -> &'static str {
        match self {
            Self::Beginner => "beginner",
            Self::CombatV2 => "combat_v2",
            Self::Development => "development",
        }
    }
}

#[derive(Debug, Clone, Deserialize, Serialize, Type)]
pub(crate) struct CpuMatchRules {
    pub(crate) wins: u8,
    pub(crate) big_stars: u8,
    pub(crate) lives: Lives,
}

impl Default for CpuMatchRules {
    fn default() -> Self {
        Self {
            wins: 3,
            big_stars: 10,
            lives: Lives::Endless,
        }
    }
}

#[derive(Debug, Clone, Deserialize, Serialize, Type)]
pub(crate) struct SoloTestRequest {
    #[serde(default)]
    pub(crate) cpu_rules: Option<CpuMatchRules>,
    #[serde(default)]
    pub(crate) cpu_opponent: Option<CpuOpponent>,
    pub(crate) stage: u8,
    pub(crate) controlled_player: SoloControl,
    pub(crate) rollback_enabled: bool,
    pub(crate) input_delay_frames: u8,
    pub(crate) match_seed: String,
    pub(crate) host: SoloNetwork,
    pub(crate) client: SoloNetwork,
}

impl SoloTestRequest {
    fn settings(&self) -> GameSettings {
        let rules = if self.cpu_opponent.is_some() {
            self.cpu_rules.clone().unwrap_or_default()
        } else {
            CpuMatchRules {
                wins: 1,
                big_stars: 10,
                lives: Lives::Endless,
            }
        };
        let games = usize::from(rules.wins.saturating_mul(2).saturating_sub(1));
        GameSettings {
            course_mode: CourseMode::Select,
            course_stages: vec![self.stage; games],
            wins: rules.wins,
            big_stars: rules.big_stars,
            lives: rules.lives,
            match_seed: self.match_seed.clone(),
            rng_seeds: vec![self.match_seed.clone(); games],
            input_delay_frames: self.input_delay_frames,
            input_max_frame_lead: 2,
            rollback_enabled: self.rollback_enabled,
        }
    }

    fn validate(&self) -> Result<(), String> {
        crate::settings::validate_settings(&self.settings())?;
        for network in [&self.host, &self.client] {
            if network.delay_frames > 60 || network.jitter_frames > 60 || network.drop_every == 1 {
                return Err(
                    "疑似WANの遅延・揺らぎは0〜60、間引きは0または2以上にしてください".into(),
                );
            }
        }
        Ok(())
    }
}

#[derive(Default, Clone, Serialize, Type)]
pub(crate) struct SoloTestStatus {
    pub(crate) active: bool,
    pub(crate) preparing: bool,
    pub(crate) log_dir: Option<String>,
    pub(crate) host_pid: Option<u32>,
    pub(crate) client_pid: Option<u32>,
    pub(crate) error: Option<String>,
    pub(crate) config: Option<SoloTestRequest>,
}

#[derive(Default)]
pub(crate) struct SoloTestState {
    preparing: AtomicBool,
    session: Mutex<SoloSessionState>,
}

#[derive(Default)]
struct SoloSessionState {
    running: Option<SoloChildren>,
    status: SoloTestStatus,
}

struct SoloChildren {
    children: Vec<Child>,
    _job: ChildProcessJob,
}

impl Drop for SoloChildren {
    fn drop(&mut self) {
        for child in &mut self.children {
            terminate_child(child);
        }
    }
}

fn require_local_profile(profile: Option<&str>) -> Result<(), String> {
    if profile == Some("local") {
        Ok(())
    } else {
        Err("ひとり検証はローカルビルド専用です".into())
    }
}

#[tauri::command]
#[specta::specta]
pub(crate) async fn start_solo_test(
    app: AppHandle,
    request: SoloTestRequest,
) -> Result<SoloTestStatus, String> {
    require_local_profile(option_env!("BIGSTAR_BUILD_PROFILE"))?;
    if request.cpu_opponent.is_some() {
        return Err("CPU対戦は専用の開始操作を使用してください".into());
    }
    start_session(app, request).await
}

#[tauri::command]
#[specta::specta]
pub(crate) async fn start_cpu_match(
    app: AppHandle,
    opponent: CpuOpponent,
    rules: CpuMatchRules,
) -> Result<SoloTestStatus, String> {
    let request = cpu_request(opponent, rules);
    start_session(app, request).await
}

fn cpu_request(opponent: CpuOpponent, rules: CpuMatchRules) -> SoloTestRequest {
    let network = SoloNetwork {
        delay_frames: 0,
        jitter_frames: 0,
        drop_every: 0,
    };
    SoloTestRequest {
        cpu_rules: Some(rules),
        cpu_opponent: Some(opponent),
        stage: 0,
        controlled_player: SoloControl::Mario,
        rollback_enabled: false,
        input_delay_frames: 2,
        match_seed: (std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_nanos() as u32)
            .to_string(),
        host: network.clone(),
        client: network,
    }
}

async fn start_session(app: AppHandle, request: SoloTestRequest) -> Result<SoloTestStatus, String> {
    request.validate()?;
    tauri::async_runtime::spawn_blocking(move || {
        let state = app.state::<AppState>();
        let _launch = state.begin_launch()?;
        if crate::processes::session_status_inner(&state)?.active || status_inner(&state)?.active {
            return Err("起動中の対戦を終了してください".into());
        }
        state.solo_test.preparing.store(true, Ordering::Release);
        let _preparing = LaunchPreparationGuard(&state.solo_test.preparing);
        let result = start_inner(&app, &state, request);
        if let Err(error) = &result {
            if let Ok(mut session) = state.solo_test.session.lock() {
                session.status.error = Some(error.clone());
            }
        }
        result
    })
    .await
    .map_err(|error| format!("検証の準備に失敗しました: {error}"))?
}

fn start_inner(
    app: &AppHandle,
    state: &AppState,
    request: SoloTestRequest,
) -> Result<SoloTestStatus, String> {
    let melon = find_melonds_binary(app)?;
    let input = find_input_script(app)?;
    let cpu_worker = if request.cpu_opponent.is_some() {
        let worker = crate::paths::find_rule_cpu_binary(app)?;
        check_cpu_worker(&worker)?;
        Some(worker)
    } else {
        None
    };
    let source_rom = load_launcher_settings(app)?.base_rom_path;
    let roms = crate::roms::prepare_roms(app, GenerateRomRequest { source_rom }, false)?;
    let log_dir = create_log_dir(app)?;
    {
        let mut session = state.solo_test.session.lock().map_err(|e| e.to_string())?;
        session.status = SoloTestStatus {
            log_dir: Some(log_dir.to_string_lossy().into_owned()),
            config: Some(request.clone()),
            ..Default::default()
        };
    }
    let children = launch_pair(
        &melon,
        &input,
        [
            &PathBuf::from(roms.host_rom),
            &PathBuf::from(roms.client_rom),
        ],
        &log_dir,
        &request,
        cpu_worker.as_deref(),
    )?;
    let mut session = state.solo_test.session.lock().map_err(|e| e.to_string())?;
    session.status.host_pid = Some(children.children[0].id());
    session.status.client_pid = Some(children.children[1].id());
    session.status.active = true;
    session.running = Some(children);
    Ok(session.status.clone())
}

fn check_cpu_worker(worker: &Path) -> Result<(), String> {
    use std::process::{Command, Stdio};
    use std::time::{Duration, Instant};
    let mut command = Command::new(worker);
    command
        .arg("--check")
        .stdout(Stdio::null())
        .stderr(Stdio::null());
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        command.creation_flags(0x08000000);
    }
    let mut child = command
        .spawn()
        .map_err(|e| format!("CPUを準備できません: {e}"))?;
    let deadline = Instant::now() + Duration::from_secs(30);
    loop {
        if let Some(status) = child.try_wait().map_err(|e| e.to_string())? {
            return if status.success() {
                Ok(())
            } else {
                Err("CPUの動作確認に失敗しました。対戦用ファイルを再ビルドまたは再インストールしてください".into())
            };
        }
        if Instant::now() >= deadline {
            terminate_child(&mut child);
            return Err("CPUの準備が時間切れになりました".into());
        }
        std::thread::sleep(Duration::from_millis(25));
    }
}

fn launch_pair(
    melon: &Path,
    input: &Path,
    roms: [&Path; 2],
    log_dir: &Path,
    request: &SoloTestRequest,
    cpu_worker: Option<&Path>,
) -> Result<SoloChildren, String> {
    let socket = UdpSocket::bind("127.0.0.1:0").map_err(|e| e.to_string())?;
    let port = socket.local_addr().map_err(|e| e.to_string())?.port();
    let mut pair = SoloChildren {
        children: Vec::new(),
        _job: ChildProcessJob::create()?,
    };
    fs::write(
        log_dir.join("solo-test.json"),
        serde_json::to_vec_pretty(&serde_json::json!({"config":request,"port":port}))
            .map_err(|e| e.to_string())?,
    )
    .map_err(|e| e.to_string())?;
    drop(socket);
    for (index, source) in roms.into_iter().enumerate() {
        let peer_dir = log_dir.join(if index == 0 { "host" } else { "client" });
        fs::create_dir_all(&peer_dir).map_err(|e| e.to_string())?;
        let rom = peer_dir.join("game.nds");
        crate::roms::validate_rom_save(source)?;
        fs::copy(source, &rom).map_err(|e| e.to_string())?;
        fs::copy(source.with_extension("sav"), rom.with_extension("sav"))
            .map_err(|e| e.to_string())?;
        let config_dir = peer_dir.join("config");
        write_config(melon, &config_dir)?;
        let launch = LaunchRequest {
            role: if index == 0 { Role::Host } else { Role::Client },
            port,
            signal_url: String::new(),
            room_code: "solo-test".into(),
            rom_path: rom.to_string_lossy().into_owned(),
            settings: request.settings(),
            player_names: None,
            diagnostic_events_enabled: request.cpu_opponent.is_none(),
            detailed_logs_enabled: request.cpu_opponent.is_none(),
            ai_play_log_enabled: false,
            performance_logs_enabled: true,
            rom_identity: None,
        };
        let mut command = build_melon_command(melon, &rom, input, &launch, &peer_dir)?;
        for (key, _) in std::env::vars_os() {
            if key.to_string_lossy().starts_with("MELONDS_SAVE_BOOTSTRAP") {
                command.env_remove(key);
            }
        }
        command.env("MELONDS_SAVE_BOOTSTRAP_CONFIG_DIR", config_dir);
        let network = if index == 0 {
            &request.host
        } else {
            &request.client
        };
        command.env(
            "MELONDS_NSML_INPUT_SEND_DELAY_FRAMES",
            network.delay_frames.to_string(),
        );
        command.env(
            "MELONDS_NSML_INPUT_SEND_JITTER_FRAMES",
            network.jitter_frames.to_string(),
        );
        command.env(
            "MELONDS_NSML_INPUT_DROP_MODULO",
            network.drop_every.to_string(),
        );
        command.env("MELONDS_NSML_TARGET_FPS", "60");
        if let Some(opponent) = request.cpu_opponent {
            command.env("MELONDS_NSML_FIXED_FRAME_SLEEP", "1");
            if index == 1 {
                command.env(
                    "MELONDS_NSML_RULE_WORKER",
                    cpu_worker.ok_or("CPU実行ファイルがありません")?,
                );
                command.env("MELONDS_NSML_RULE_PROFILE", opponent.profile());
            }
        }
        if request.cpu_opponent.is_none() {
            command.env("MELONDS_NSML_SCREENSHOT_INTERVAL", "300");
        }
        command.env(
            "MELONDS_NSML_INPUT_RECORD_FILE",
            peer_dir.join("recorded.inputs"),
        );
        let neutral = matches!(
            (request.controlled_player, index),
            (SoloControl::Mario, 1) | (SoloControl::Luigi, 0)
        );
        command.env(
            "MELONDS_NSML_NEUTRALIZE_POLLED_INPUT",
            if neutral { "1" } else { "0" },
        );
        let child = command
            .spawn()
            .map_err(|e| format!("検証用melonDSを起動できません: {e}"))?;
        pair.children.push(child);
        let child = pair.children.last_mut().expect("just pushed");
        pair._job.assign_child(child, "solo melonDS")?;
        capture_child_stdio(child, &peer_dir, "melonds")?;
    }
    Ok(pair)
}

fn set_value(table: &mut toml::Table, path: &[&str], value: toml::Value) {
    if path.len() == 1 {
        table.insert(path[0].to_owned(), value);
        return;
    }
    let entry = table
        .entry(path[0])
        .or_insert_with(|| toml::Value::Table(toml::Table::new()));
    if !entry.is_table() {
        *entry = toml::Value::Table(toml::Table::new());
    }
    set_value(entry.as_table_mut().expect("table"), &path[1..], value);
}

fn write_config(melon: &Path, directory: &Path) -> Result<(), String> {
    let parent = melon.parent().ok_or("melonDSのディレクトリがありません")?;
    let source = if parent.join("portable").is_dir() {
        parent.join("portable/melonDS.toml")
    } else {
        parent.join("melonDS.toml")
    };
    let mut table: toml::Table = if source.exists() {
        fs::read_to_string(source)
            .map_err(|e| e.to_string())?
            .parse()
            .map_err(|e| format!("入力設定を読み込めません: {e}"))?
    } else {
        toml::Table::new()
    };
    for (path, value) in [
        (vec!["PauseLostFocus"], false.into()),
        (vec!["LimitFPS"], true.into()),
        (vec!["AudioSync"], false.into()),
        (vec!["JIT", "Enable"], true.into()),
        (vec!["Screen", "UseGL"], false.into()),
        (vec!["3D", "Renderer"], 0.into()),
    ] {
        set_value(&mut table, &path, value);
    }
    for instance in ["Instance0", "Instance1"] {
        set_value(&mut table, &[instance, "SaveFilePath"], "".into());
        for (key, value) in [
            ("A", 88),
            ("B", 90),
            ("X", 83),
            ("Y", 65),
            ("Start", 16777220),
            ("Left", 16777234),
            ("Up", 16777235),
            ("Right", 16777236),
            ("Down", 16777237),
        ] {
            let current = table
                .get(instance)
                .and_then(|v| v.get("Keyboard"))
                .and_then(|v| v.get(key))
                .and_then(toml::Value::as_integer);
            if current.is_none_or(|v| v < 0) {
                set_value(&mut table, &[instance, "Keyboard", key], value.into());
            }
        }
        set_value(&mut table, &[instance, "Window0", "Width"], 384.into());
        set_value(&mut table, &[instance, "Window0", "Height"], 576.into());
    }
    fs::create_dir_all(directory).map_err(|e| e.to_string())?;
    fs::write(
        directory.join("melonDS.toml"),
        toml::to_string_pretty(&table).map_err(|e| e.to_string())?,
    )
    .map_err(|e| e.to_string())
}

pub(crate) fn status_inner(state: &AppState) -> Result<SoloTestStatus, String> {
    let mut session = state.solo_test.session.lock().map_err(|e| e.to_string())?;
    let mut ended = None;
    if let Some(pair) = &mut session.running {
        for (index, child) in pair.children.iter_mut().enumerate() {
            let status = process_state(child)?;
            if status != "running" {
                ended = Some(format!(
                    "{}側が終了しました（{status}）。両側を停止しました。",
                    if index == 0 {
                        "マリオ"
                    } else {
                        "ルイージ"
                    }
                ));
                break;
            }
        }
    }
    if let Some(reason) = ended {
        session.running = None;
        session.status.active = false;
        session.status.error = Some(reason);
    }
    let mut status = session.status.clone();
    status.preparing = state.solo_test.preparing.load(Ordering::Acquire);
    Ok(status)
}

#[tauri::command]
#[specta::specta]
pub(crate) fn get_solo_test_status(state: State<'_, AppState>) -> Result<SoloTestStatus, String> {
    status_inner(&state)
}

#[tauri::command]
#[specta::specta]
pub(crate) fn stop_solo_test(state: State<'_, AppState>) -> Result<SoloTestStatus, String> {
    let _launch = state.begin_launch()?;
    let mut session = state.solo_test.session.lock().map_err(|e| e.to_string())?;
    session.running = None;
    session.status.active = false;
    Ok(session.status.clone())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn request() -> SoloTestRequest {
        SoloTestRequest {
            cpu_opponent: None,
            cpu_rules: None,
            stage: 3,
            controlled_player: SoloControl::Mario,
            rollback_enabled: true,
            input_delay_frames: 2,
            match_seed: "0x12345678".into(),
            host: SoloNetwork {
                delay_frames: 2,
                jitter_frames: 1,
                drop_every: 0,
            },
            client: SoloNetwork {
                delay_frames: 2,
                jitter_frames: 1,
                drop_every: 0,
            },
        }
    }

    #[test]
    fn validates_network_and_game_settings() {
        let mut config = request();
        assert!(config.validate().is_ok());
        config.client.drop_every = 1;
        assert!(config.validate().is_err());
        config.client.drop_every = 0;
        config.stage = 5;
        assert!(config.validate().is_err());
        config.stage = 3;
        config.match_seed = "invalid".into();
        assert!(config.validate().is_err());
    }

    #[test]
    fn isolates_config_and_preserves_input_mapping() {
        let dir = tempfile::tempdir().unwrap();
        let source = dir.path().join("melonDS.toml");
        let original = "PauseLostFocus = true\n[Instance0]\nSaveFilePath = 'shared'\n[Instance0.Keyboard]\nA = 75\nB = -1\n[JIT]\nEnable = false\n";
        fs::write(&source, original).unwrap();
        let target = dir.path().join("isolated");
        write_config(&dir.path().join("melonDS.exe"), &target).unwrap();
        assert_eq!(fs::read_to_string(source).unwrap(), original);
        let config: toml::Table = fs::read_to_string(target.join("melonDS.toml"))
            .unwrap()
            .parse()
            .unwrap();
        assert_eq!(config["Instance0"]["Keyboard"]["A"].as_integer(), Some(75));
        assert_eq!(config["Instance0"]["Keyboard"]["B"].as_integer(), Some(90));
        assert_eq!(config["Instance0"]["SaveFilePath"].as_str(), Some(""));
        assert_eq!(config["PauseLostFocus"].as_bool(), Some(false));
        assert_eq!(config["JIT"]["Enable"].as_bool(), Some(true));
    }

    #[test]
    #[ignore = "Requires melonDS and prepared host/client ROM paths in BIGSTAR_SOLO_TEST_* environment variables"]
    fn real_pair_smoke_and_peer_cleanup() {
        let host = PathBuf::from(std::env::var("BIGSTAR_SOLO_TEST_HOST_ROM").unwrap());
        let client = PathBuf::from(std::env::var("BIGSTAR_SOLO_TEST_CLIENT_ROM").unwrap());
        let melon = crate::paths::find_melonds_binary_without_app().unwrap();
        let input = crate::paths::find_input_script_without_app().unwrap();
        let log_dir = PathBuf::from(std::env::var("BIGSTAR_SOLO_TEST_LOG_DIR").unwrap());
        fs::create_dir_all(&log_dir).unwrap();
        let pair =
            launch_pair(&melon, &input, [&host, &client], &log_dir, &request(), None).unwrap();
        let state = AppState::default();
        {
            let mut session = state.solo_test.session.lock().unwrap();
            session.status.active = true;
            session.running = Some(pair);
        }
        std::thread::sleep(std::time::Duration::from_secs(30));
        assert!(
            status_inner(&state).unwrap().active,
            "{:?}",
            status_inner(&state).unwrap().error
        );
        for role in ["host", "client"] {
            let output = fs::read_to_string(log_dir.join(role).join("melonds.stdout.txt")).unwrap();
            let stderr = fs::read_to_string(log_dir.join(role).join("melonds.stderr.txt")).unwrap();
            let combined = format!("{output}\n{stderr}");
            assert!(
                combined.contains("inputSendDelay=2 inputSendJitter=1"),
                "network settings missing: {role}"
            );
            let trace =
                fs::read_to_string(log_dir.join(role).join("melonds-game-state.csv")).unwrap();
            assert!(trace.lines().count() > 30, "gameplay trace missing: {role}");
        }
        {
            let mut session = state.solo_test.session.lock().unwrap();
            terminate_child(&mut session.running.as_mut().unwrap().children[0]);
        }
        assert!(!status_inner(&state).unwrap().active);
        assert!(state.solo_test.session.lock().unwrap().running.is_none());
    }

    #[test]
    fn cpu_profiles_use_supported_local_match_settings() {
        for profile in [
            CpuOpponent::Beginner,
            CpuOpponent::CombatV2,
            CpuOpponent::Development,
        ] {
            let request = cpu_request(profile, CpuMatchRules::default());
            request.validate().unwrap();
            assert_eq!(request.settings().course_stages, vec![0; 5]);
            assert_eq!(request.settings().big_stars, 10);
            assert_eq!(request.settings().wins, 3);
            assert!(!request.rollback_enabled);
            assert!(matches!(request.controlled_player, SoloControl::Mario));
        }
    }

    #[test]
    #[ignore = "Requires prepared ROMs and the packaged CPU runtime; runs real emulator pairs"]
    fn real_cpu_profiles_and_peer_cleanup() {
        let host = PathBuf::from(std::env::var("BIGSTAR_SOLO_TEST_HOST_ROM").unwrap());
        let client = PathBuf::from(std::env::var("BIGSTAR_SOLO_TEST_CLIENT_ROM").unwrap());
        let melon = crate::paths::find_melonds_binary_without_app().unwrap();
        let input = crate::paths::find_input_script_without_app().unwrap();
        let worker = PathBuf::from(std::env::var("BIGSTAR_CPU_WORKER").unwrap());
        let root = PathBuf::from(std::env::var("BIGSTAR_SOLO_TEST_LOG_DIR").unwrap());
        check_cpu_worker(&worker).unwrap();
        for profile in [
            CpuOpponent::Beginner,
            CpuOpponent::CombatV2,
            CpuOpponent::Development,
        ] {
            let log_dir = root.join(profile.profile());
            fs::create_dir_all(&log_dir).unwrap();
            let mut request = cpu_request(profile, CpuMatchRules::default());
            request.match_seed = "45".into();
            let pair = launch_pair(
                &melon,
                &input,
                [&host, &client],
                &log_dir,
                &request,
                Some(&worker),
            )
            .unwrap();
            let state = AppState::default();
            {
                let mut session = state.solo_test.session.lock().unwrap();
                session.status.active = true;
                session.running = Some(pair);
            }
            for _ in 0..40 {
                std::thread::sleep(std::time::Duration::from_secs(1));
                assert!(
                    status_inner(&state).unwrap().active,
                    "{}: {:?}",
                    profile.profile(),
                    status_inner(&state).unwrap().error
                );
            }
            {
                let mut session = state.solo_test.session.lock().unwrap();
                terminate_child(&mut session.running.as_mut().unwrap().children[0]);
            }
            assert!(!status_inner(&state).unwrap().active);
            assert!(state.solo_test.session.lock().unwrap().running.is_none());
            let output = fs::read_to_string(log_dir.join("client/melonds.stdout.txt")).unwrap();
            assert!(
                output.contains("ready player=1 ruleCPU=1"),
                "{}: no CPU ready",
                profile.profile()
            );
            assert!(output.contains("held=0x"), "no gameplay decisions");
            assert!(!output.contains("FAILED"), "CPU failed");
            println!(
                "CPU {}: gameplay and peer cleanup verified",
                profile.profile()
            );
        }
    }

    #[test]
    fn cpu_rules_are_forwarded_and_invalid_values_rejected() {
        let request = cpu_request(
            CpuOpponent::Development,
            CpuMatchRules {
                wins: 2,
                big_stars: 3,
                lives: Lives::Five,
            },
        );
        request.validate().unwrap();
        let settings = request.settings();
        assert_eq!(settings.wins, 2);
        assert_eq!(settings.big_stars, 3);
        assert!(matches!(settings.lives, Lives::Five));
        assert_eq!(settings.course_stages, vec![0; 3]);
        assert_eq!(settings.rng_seeds.len(), 3);
        for (wins, big_stars) in [(0, 10), (4, 10), (3, 4)] {
            let invalid = cpu_request(
                CpuOpponent::Beginner,
                CpuMatchRules {
                    wins,
                    big_stars,
                    lives: Lives::Endless,
                },
            );
            assert!(invalid.validate().is_err());
        }
    }

    #[test]
    fn only_explicit_local_builds_can_launch() {
        assert!(require_local_profile(Some("local")).is_ok());
        for profile in [None, Some("distribution"), Some(""), Some("unknown")] {
            assert!(require_local_profile(profile).is_err());
        }
    }

    #[test]
    fn launch_lock_is_shared_and_released_on_failure() {
        let state = AppState::default();
        let guard = state.begin_launch().unwrap();
        assert!(state.begin_launch().is_err());
        drop(guard);
        assert!(state.begin_launch().is_ok());
    }
}
