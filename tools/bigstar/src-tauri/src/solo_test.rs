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

#[derive(Debug, Clone, Deserialize, Serialize, Type)]
pub(crate) struct SoloTestRequest {
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
        GameSettings {
            course_mode: CourseMode::Select,
            course_stages: vec![self.stage],
            wins: 1,
            big_stars: 10,
            lives: Lives::Endless,
            match_seed: self.match_seed.clone(),
            rng_seeds: vec![self.match_seed.clone()],
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
    )?;
    let mut session = state.solo_test.session.lock().map_err(|e| e.to_string())?;
    session.status.host_pid = Some(children.children[0].id());
    session.status.client_pid = Some(children.children[1].id());
    session.status.active = true;
    session.running = Some(children);
    Ok(session.status.clone())
}

fn launch_pair(
    melon: &Path,
    input: &Path,
    roms: [&Path; 2],
    log_dir: &Path,
    request: &SoloTestRequest,
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
            diagnostic_events_enabled: true,
            detailed_logs_enabled: true,
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
        command.env("MELONDS_NSML_SCREENSHOT_INTERVAL", "300");
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
    require_local_profile(option_env!("BIGSTAR_BUILD_PROFILE"))?;
    status_inner(&state)
}

#[tauri::command]
#[specta::specta]
pub(crate) fn stop_solo_test(state: State<'_, AppState>) -> Result<SoloTestStatus, String> {
    require_local_profile(option_env!("BIGSTAR_BUILD_PROFILE"))?;
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
        let pair = launch_pair(&melon, &input, [&host, &client], &log_dir, &request()).unwrap();
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
