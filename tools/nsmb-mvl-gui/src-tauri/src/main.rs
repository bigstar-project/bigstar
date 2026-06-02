#![cfg_attr(all(not(debug_assertions), windows), windows_subsystem = "windows")]

use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::ffi::OsStr;
use std::fs::{self, File};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::Mutex;
use tauri::{AppHandle, Manager, State};

#[cfg(windows)]
use std::os::windows::process::CommandExt;

#[cfg(windows)]
const CREATE_NO_WINDOW: u32 = 0x0800_0000;

const DEFAULT_ROOM_CODE: &str = "test-room";
const DEFAULT_SIGNAL_URL: &str =
    "wss://nsmb-mvl-signaling-signaling-prod.uniunitaro.workers.dev/session";
const DEFAULT_PORT: u16 = 8165;
const DEFAULT_FRAMES: u32 = 999_999;
const DEFAULT_INPUT_DELAY_FRAMES: u8 = 4;
const DEFAULT_INPUT_MAX_FRAME_LEAD: u8 = 4;
const NETPLAY_START_FRAME: u32 = 840;
const REUSABLE_ROM_FORMAT: &str = "nsmb-mvl-reusable-runtime-config-v3";

#[derive(Default)]
struct AppState {
    session: Mutex<Option<ManagedSession>>,
}

struct ManagedSession {
    melon: Child,
    bridge: Child,
    log_dir: PathBuf,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "snake_case")]
struct LaunchRequest {
    role: Role,
    signal_url: String,
    room_code: String,
    port: u16,
    rom_path: String,
    settings: GameSettings,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "snake_case")]
struct GenerateRomRequest {
    source_rom: String,
    host_rom: String,
    client_rom: String,
    stage: u8,
    settings: GameSettings,
}

#[derive(Debug, Deserialize, Clone)]
#[serde(rename_all = "snake_case")]
enum Role {
    Host,
    Client,
}

#[derive(Debug, Deserialize, Clone)]
#[serde(rename_all = "snake_case")]
struct GameSettings {
    course_mode: CourseMode,
    wins: u8,
    big_stars: u8,
    lives: Lives,
    match_seed: String,
}

#[derive(Debug, Deserialize, Clone, Copy)]
#[serde(rename_all = "snake_case")]
enum CourseMode {
    Random,
    Select,
}

#[derive(Debug, Deserialize, Clone, Copy)]
#[serde(rename_all = "snake_case")]
enum Lives {
    #[serde(rename = "3")]
    Three,
    #[serde(rename = "5")]
    Five,
    Endless,
}

#[derive(Serialize)]
struct Defaults {
    signal_url: String,
    room_code: String,
    host_rom_path: String,
    client_rom_path: String,
    base_rom_path: String,
    port: u16,
}

#[derive(Debug, Default, Deserialize, Serialize)]
#[serde(default)]
struct LauncherSettings {
    host_rom_path: String,
    client_rom_path: String,
    base_rom_path: String,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "snake_case")]
struct SaveRomPathsRequest {
    host_rom_path: String,
    client_rom_path: String,
    base_rom_path: String,
}

#[derive(Debug, Serialize)]
struct LaunchResponse {
    log_dir: String,
    melon_pid: u32,
    bridge_pid: u32,
}

#[derive(Serialize)]
struct GenerateRomResponse {
    host_rom: String,
    client_rom: String,
    generated: bool,
}

#[derive(Serialize)]
struct SessionStatus {
    active: bool,
    log_dir: Option<String>,
    melon: Option<String>,
    bridge: Option<String>,
}

#[derive(Debug, Serialize)]
struct PreflightResponse {
    melonds_path: String,
    bridge_path: String,
    input_script: String,
    symbols_file: String,
    bridge_smoke: String,
}

#[tauri::command]
fn get_defaults(app: AppHandle) -> Result<Defaults, String> {
    let app_dir = app_data_dir(&app)?;
    let rom_dir = app_dir.join("roms");
    fs::create_dir_all(&rom_dir).map_err(|err| format!("ROM保存先を作成できません: {err}"))?;
    let saved = load_launcher_settings(&app)?;
    let signal_url =
        std::env::var("NSMB_MVL_SIGNAL_URL").unwrap_or_else(|_| DEFAULT_SIGNAL_URL.to_owned());
    let dev_base_rom = repo_root()
        .ok()
        .map(|root| root.join("roms").join("nsmb-us.nds"))
        .filter(|path| path.exists());

    Ok(Defaults {
        signal_url,
        room_code: DEFAULT_ROOM_CODE.to_owned(),
        host_rom_path: saved_path_or_default(
            &saved.host_rom_path,
            rom_dir.join("nsmb-mvl-host.nds"),
        ),
        client_rom_path: saved_path_or_default(
            &saved.client_rom_path,
            rom_dir.join("nsmb-mvl-client.nds"),
        ),
        base_rom_path: saved_path_or_default(
            &saved.base_rom_path,
            dev_base_rom.unwrap_or_else(|| rom_dir.join("nsmb-us.nds")),
        ),
        port: DEFAULT_PORT,
    })
}

#[tauri::command]
fn save_rom_paths(app: AppHandle, request: SaveRomPathsRequest) -> Result<(), String> {
    let settings = LauncherSettings {
        host_rom_path: request.host_rom_path,
        client_rom_path: request.client_rom_path,
        base_rom_path: request.base_rom_path,
    };
    save_launcher_settings(&app, &settings)
}

#[tauri::command]
fn select_rom_file(current_path: String) -> Result<Option<String>, String> {
    let mut dialog = rfd::FileDialog::new()
        .add_filter("Nintendo DS ROM", &["nds", "srl"])
        .add_filter("All files", &["*"]);
    let current = PathBuf::from(current_path.trim());
    if current.is_file() {
        if let Some(parent) = current.parent() {
            dialog = dialog.set_directory(parent);
        }
        if let Some(name) = current.file_name() {
            dialog = dialog.set_file_name(name.to_string_lossy().into_owned());
        }
    } else if current.is_dir() {
        dialog = dialog.set_directory(current);
    }
    Ok(dialog
        .pick_file()
        .map(|path| path.to_string_lossy().into_owned()))
}

#[tauri::command]
fn preflight_check(app: AppHandle) -> Result<PreflightResponse, String> {
    let melon_path = find_melonds_binary(&app)?;
    let bridge_path = find_bridge_binary(&app)?;
    let input_script = find_input_script(&app)?;
    let symbols_file = find_symbols_file(&app)?;
    let bridge_smoke = run_bridge_signaling_smoke(&bridge_path)?;

    Ok(PreflightResponse {
        melonds_path: melon_path.to_string_lossy().into_owned(),
        bridge_path: bridge_path.to_string_lossy().into_owned(),
        input_script: input_script.to_string_lossy().into_owned(),
        symbols_file: symbols_file.to_string_lossy().into_owned(),
        bridge_smoke,
    })
}

fn cli_preflight_check() -> Result<PreflightResponse, String> {
    let melon_path = find_melonds_binary_without_app()?;
    let bridge_path = find_bridge_binary_without_app()?;
    let input_script = find_input_script_without_app()?;
    let symbols_file = find_symbols_file_without_app()?;
    let bridge_smoke = run_bridge_signaling_smoke(&bridge_path)?;

    Ok(PreflightResponse {
        melonds_path: melon_path.to_string_lossy().into_owned(),
        bridge_path: bridge_path.to_string_lossy().into_owned(),
        input_script: input_script.to_string_lossy().into_owned(),
        symbols_file: symbols_file.to_string_lossy().into_owned(),
        bridge_smoke,
    })
}

#[tauri::command]
fn start_match(
    app: AppHandle,
    state: State<'_, AppState>,
    request: LaunchRequest,
) -> Result<LaunchResponse, String> {
    validate_request(&request)?;
    let log_dir = create_log_dir(&app)?;
    let bridge_path = find_bridge_binary(&app)?;
    let melon_path = find_melonds_binary(&app)?;
    let input_script = find_input_script(&app)?;
    let rom_path = absolutize_existing(&request.rom_path)?;

    start_match_resolved(
        state.inner(),
        request,
        LaunchPaths {
            log_dir,
            bridge_path,
            melon_path,
            input_script,
            rom_path,
        },
    )
}

struct LaunchPaths {
    log_dir: PathBuf,
    bridge_path: PathBuf,
    melon_path: PathBuf,
    input_script: PathBuf,
    rom_path: PathBuf,
}

fn start_match_resolved(
    state: &AppState,
    request: LaunchRequest,
    paths: LaunchPaths,
) -> Result<LaunchResponse, String> {
    stop_existing(state)?;

    let mut bridge = build_bridge_command(&paths.bridge_path, &request, &paths.log_dir)?;
    let mut bridge_child = bridge
        .spawn()
        .map_err(|err| format!("bridge の起動に失敗しました: {err}"))?;

    let mut melon = build_melon_command(
        &paths.melon_path,
        &paths.rom_path,
        &paths.input_script,
        &request,
        &paths.log_dir,
    )?;
    let melon_child = match melon.spawn() {
        Ok(child) => child,
        Err(err) => {
            terminate_child(&mut bridge_child);
            return Err(format!("melonDS の起動に失敗しました: {err}"));
        }
    };

    let response = LaunchResponse {
        log_dir: paths.log_dir.to_string_lossy().into_owned(),
        melon_pid: melon_child.id(),
        bridge_pid: bridge_child.id(),
    };

    let mut guard = state
        .session
        .lock()
        .map_err(|_| "session state lock failed".to_owned())?;
    *guard = Some(ManagedSession {
        melon: melon_child,
        bridge: bridge_child,
        log_dir: paths.log_dir,
    });

    Ok(response)
}

#[tauri::command]
fn generate_roms(
    app: AppHandle,
    request: GenerateRomRequest,
) -> Result<GenerateRomResponse, String> {
    prepare_roms(&app, request, true)
}

#[tauri::command]
fn ensure_roms(app: AppHandle, request: GenerateRomRequest) -> Result<GenerateRomResponse, String> {
    prepare_roms(&app, request, false)
}

fn prepare_roms(
    app: &AppHandle,
    request: GenerateRomRequest,
    force: bool,
) -> Result<GenerateRomResponse, String> {
    validate_settings(&request.settings)?;
    let stage = selected_stage(&request.settings, request.stage)?;
    let host_rom = absolutize_target(&app, &request.host_rom)?;
    let client_rom = absolutize_target(&app, &request.client_rom)?;
    if !force && reusable_rom_is_current(&host_rom) && reusable_rom_is_current(&client_rom) {
        return Ok(GenerateRomResponse {
            host_rom: host_rom.to_string_lossy().into_owned(),
            client_rom: client_rom.to_string_lossy().into_owned(),
            generated: false,
        });
    }

    let source_rom = absolutize_existing(&request.source_rom)?;
    ensure_parent_dir(&host_rom)?;
    ensure_parent_dir(&client_rom)?;
    let options = nsmb_mvl_rom::StableRomOptions {
        source_rom,
        host_rom: host_rom.clone(),
        client_rom: client_rom.clone(),
        stage,
        wins: request.settings.wins,
        big_stars: request.settings.big_stars,
        lives: lives_value(request.settings.lives).to_owned(),
        course_mode: course_mode_value(request.settings.course_mode).to_owned(),
        scene_settings: None,
        symbols: find_symbols_file(&app)?,
    };

    nsmb_mvl_rom::generate_stable_roms(&options)
        .map_err(|err| format!("ROM生成に失敗しました: {err}"))?;
    write_reusable_rom_marker(&host_rom)?;
    write_reusable_rom_marker(&client_rom)?;

    Ok(GenerateRomResponse {
        host_rom: host_rom.to_string_lossy().into_owned(),
        client_rom: client_rom.to_string_lossy().into_owned(),
        generated: true,
    })
}

fn reusable_rom_marker_path(rom: &Path) -> PathBuf {
    let mut marker = rom.as_os_str().to_owned();
    marker.push(".nsmb-mvl-version");
    PathBuf::from(marker)
}

fn reusable_rom_is_current(rom: &Path) -> bool {
    rom.is_file()
        && fs::read_to_string(reusable_rom_marker_path(rom))
            .is_ok_and(|version| version.trim() == REUSABLE_ROM_FORMAT)
}

fn write_reusable_rom_marker(rom: &Path) -> Result<(), String> {
    fs::write(
        reusable_rom_marker_path(rom),
        format!("{REUSABLE_ROM_FORMAT}\n"),
    )
    .map_err(|err| format!("ROM形式 marker を保存できません: {err}"))
}

#[tauri::command]
fn stop_match(state: State<'_, AppState>) -> Result<(), String> {
    stop_existing(state.inner())
}

#[tauri::command]
fn session_status(state: State<'_, AppState>) -> Result<SessionStatus, String> {
    session_status_inner(state.inner())
}

fn session_status_inner(state: &AppState) -> Result<SessionStatus, String> {
    let mut guard = state
        .session
        .lock()
        .map_err(|_| "session state lock failed".to_owned())?;

    let Some(session) = guard.as_mut() else {
        return Ok(SessionStatus {
            active: false,
            log_dir: None,
            melon: None,
            bridge: None,
        });
    };

    let melon = process_state(&mut session.melon)?;
    let bridge = process_state(&mut session.bridge)?;
    let active = melon == "running" || bridge == "running";

    Ok(SessionStatus {
        active,
        log_dir: Some(session.log_dir.to_string_lossy().into_owned()),
        melon: Some(melon),
        bridge: Some(bridge),
    })
}

fn build_bridge_command(
    bridge_path: &Path,
    request: &LaunchRequest,
    log_dir: &Path,
) -> Result<Command, String> {
    let mut command = Command::new(bridge_path);
    match request.role {
        Role::Host => {
            command.args([
                "webrtc-offer",
                "--local-bind",
                "127.0.0.1:0",
                "--local-target",
                &format!("127.0.0.1:{}", request.port),
            ]);
        }
        Role::Client => {
            command.args([
                "webrtc-answer",
                "--local-bind",
                &format!("127.0.0.1:{}", request.port),
            ]);
        }
    }
    command.args([
        "--signal",
        request.signal_url.trim(),
        "--session",
        request.room_code.trim(),
    ]);
    with_stdio(command, log_dir, "bridge")
}

fn build_melon_command(
    melon_path: &Path,
    rom_path: &Path,
    input_script: &Path,
    request: &LaunchRequest,
    log_dir: &Path,
) -> Result<Command, String> {
    let mut command = Command::new(melon_path);
    command.arg(rom_path);
    command.current_dir(log_dir);

    remove_inherited_melonds_env(&mut command);
    for (key, value) in melon_env(request, input_script, log_dir) {
        command.env(key, value);
    }

    with_stdio(command, log_dir, "melonds")
}

fn remove_inherited_melonds_env(command: &mut Command) {
    remove_inherited_melonds_env_keys(command, std::env::vars_os().map(|(key, _)| key));
}

fn remove_inherited_melonds_env_keys<I, K>(command: &mut Command, keys: I)
where
    I: IntoIterator<Item = K>,
    K: AsRef<OsStr>,
{
    for key in keys {
        let key = key.as_ref();
        if key.to_string_lossy().starts_with("MELONDS_NSML_") {
            command.env_remove(key);
        }
    }
}

fn melon_env(
    request: &LaunchRequest,
    input_script: &Path,
    log_dir: &Path,
) -> BTreeMap<String, String> {
    let mut env = BTreeMap::new();
    let stage = selected_stage(&request.settings, 0).unwrap_or(0);
    let role = match request.role {
        Role::Host => "host",
        Role::Client => "client",
    };
    let local_instance = match request.role {
        Role::Host => "0",
        Role::Client => "1",
    };

    env.insert("MELONDS_NSML_TEST".into(), "1".into());
    env.insert("MELONDS_NSML_TEST_INSTANCES".into(), "1".into());
    env.insert(
        "MELONDS_NSML_TEST_FRAMES".into(),
        DEFAULT_FRAMES.to_string(),
    );
    env.insert("MELONDS_NSML_POC".into(), "1".into());
    env.insert("MELONDS_NSML_ROLE".into(), role.into());
    env.insert("MELONDS_NSML_PORT".into(), request.port.to_string());
    env.insert("MELONDS_NSML_LOCAL_INSTANCE".into(), local_instance.into());
    env.insert(
        "MELONDS_NSML_INPUT_SCRIPT".into(),
        input_script.to_string_lossy().into_owned(),
    );
    env.insert("MELONDS_NSML_DISABLE_HASH".into(), "1".into());
    env.insert(
        "MELONDS_NSML_SCREENSHOT_DIR".into(),
        log_dir.join("screens").to_string_lossy().into_owned(),
    );
    env.insert("MELONDS_NSML_SCREENSHOT_INTERVAL".into(), "0".into());
    env.insert(
        "MELONDS_NSML_FIXED_RTC".into(),
        "2020-01-01T00:00:00".into(),
    );
    env.insert("MELONDS_NSML_ALLOW_JIT".into(), "1".into());
    env.insert("MELONDS_NSML_INPUT_NETPLAY_ONLY".into(), "1".into());
    env.insert("MELONDS_NSML_REMOTE_INPUT_TIMEOUT_FATAL".into(), "1".into());
    env.insert(
        "MELONDS_NSML_DELAY".into(),
        DEFAULT_INPUT_DELAY_FRAMES.to_string(),
    );
    env.insert(
        "MELONDS_NSML_INPUT_MAX_FRAME_LEAD".into(),
        DEFAULT_INPUT_MAX_FRAME_LEAD.to_string(),
    );
    env.insert("MELONDS_NSML_INPUT_UNRELIABLE".into(), "1".into());
    env.insert("MELONDS_NSML_INPUT_BUNDLE_HISTORY".into(), "8".into());
    env.insert("MELONDS_NSML_WAIT_FOR_PEER".into(), "1".into());
    env.insert(
        "MELONDS_NSML_WAIT_FOR_PEER_AT_NETPLAY_START".into(),
        "1".into(),
    );
    env.insert("MELONDS_NSML_DEFER_NETWORK_UNTIL_START".into(), "1".into());
    env.insert(
        "MELONDS_NSML_NETPLAY_START_FRAME".into(),
        NETPLAY_START_FRAME.to_string(),
    );
    env.insert(
        "MELONDS_NSML_PACKET_BRIDGE_JIT_HELPER_PATCH".into(),
        "1".into(),
    );
    env.insert(
        "MELONDS_NSML_PACKET_BRIDGE_JIT_HELPER_PATCH_FRAME".into(),
        NETPLAY_START_FRAME.to_string(),
    );
    env.insert("MELONDS_NSML_CLEAR_MVL_CAMERA_INIT_HOLD".into(), "1".into());
    env.insert(
        "MELONDS_NSML_CLEAR_MVL_CAMERA_INIT_HOLD_START_FRAME".into(),
        NETPLAY_START_FRAME.to_string(),
    );
    env.insert("MELONDS_NSML_MVL_STAGE".into(), stage.to_string());
    env.insert(
        "MELONDS_NSML_DIRECT_MVL_BOOT_STAGE".into(),
        stage.to_string(),
    );

    if matches!(request.role, Role::Client) {
        env.insert("MELONDS_NSML_PEER".into(), "127.0.0.1".into());
    }

    env.insert(
        "MELONDS_NSML_MVL_COURSE_MODE".into(),
        match request.settings.course_mode {
            CourseMode::Random => "random",
            CourseMode::Select => "select",
        }
        .into(),
    );
    env.insert(
        "MELONDS_NSML_MVL_WINS".into(),
        request.settings.wins.to_string(),
    );
    env.insert(
        "MELONDS_NSML_MVL_BIG_STARS".into(),
        request.settings.big_stars.to_string(),
    );
    env.insert(
        "MELONDS_NSML_MVL_LIVES".into(),
        match request.settings.lives {
            Lives::Three => "3",
            Lives::Five => "5",
            Lives::Endless => "endless",
        }
        .into(),
    );
    if request.settings.wins > 1 {
        env.insert(
            "MELONDS_NSML_MVL_AUTO_RESTART_AFTER_RESULT".into(),
            "1".into(),
        );
        env.insert(
            "MELONDS_NSML_MVL_AUTO_RESTART_DELAY_FRAMES".into(),
            "120".into(),
        );
    }
    if !request.settings.match_seed.trim().is_empty() {
        env.insert(
            "MELONDS_NSML_MATCH_SEED".into(),
            request.settings.match_seed.trim().to_owned(),
        );
    }

    env
}

fn with_stdio(mut command: Command, log_dir: &Path, name: &str) -> Result<Command, String> {
    let stdout = File::create(log_dir.join(format!("{name}.stdout.txt")))
        .map_err(|err| format!("{name} stdout log を作成できません: {err}"))?;
    let stderr = File::create(log_dir.join(format!("{name}.stderr.txt")))
        .map_err(|err| format!("{name} stderr log を作成できません: {err}"))?;
    command.stdout(Stdio::from(stdout));
    command.stderr(Stdio::from(stderr));
    hide_child_console_window(&mut command);
    Ok(command)
}

#[cfg(windows)]
fn hide_child_console_window(command: &mut Command) {
    command.creation_flags(CREATE_NO_WINDOW);
}

#[cfg(not(windows))]
fn hide_child_console_window(_command: &mut Command) {}

fn validate_request(request: &LaunchRequest) -> Result<(), String> {
    validate_settings(&request.settings)?;
    let room = request.room_code.trim();
    if room.is_empty()
        || room.len() > 64
        || !room
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || b == b'_' || b == b'-')
    {
        return Err("部屋コードは ^[A-Za-z0-9_-]{1,64}$ にしてください".into());
    }
    let signal = request.signal_url.trim();
    if !(signal.starts_with("ws://") || signal.starts_with("wss://")) {
        return Err("シグナリングサーバーは ws:// または wss:// で始めてください".into());
    }
    Ok(())
}

fn validate_settings(settings: &GameSettings) -> Result<(), String> {
    if settings.wins < 1 || settings.wins > 3 {
        return Err("勝利数は 1-3 にしてください".into());
    }
    if !matches!(settings.big_stars, 3 | 5 | 10) {
        return Err("ビッグスターは 3/5/10 のいずれかにしてください".into());
    }
    if matches!(settings.course_mode, CourseMode::Random) {
        parse_match_seed(settings.match_seed.trim())?;
    }
    Ok(())
}

fn selected_stage(settings: &GameSettings, fallback_stage: u8) -> Result<u8, String> {
    match settings.course_mode {
        CourseMode::Random => {
            let seed = parse_match_seed(settings.match_seed.trim())?;
            Ok((seed % 5) as u8)
        }
        CourseMode::Select => Ok(fallback_stage.min(4)),
    }
}

fn parse_match_seed(value: &str) -> Result<u32, String> {
    if value.is_empty() {
        return Err("ランダムコースではマッチシードが必要です".into());
    }
    let parsed = if let Some(hex) = value
        .strip_prefix("0x")
        .or_else(|| value.strip_prefix("0X"))
    {
        u32::from_str_radix(hex, 16)
    } else {
        value.parse::<u32>()
    };
    parsed.map_err(|_| "マッチシードは10進数か0x始まりの16進数で指定してください".to_owned())
}

fn course_mode_value(course_mode: CourseMode) -> &'static str {
    match course_mode {
        CourseMode::Random => "random",
        CourseMode::Select => "select",
    }
}

fn lives_value(lives: Lives) -> &'static str {
    match lives {
        Lives::Three => "3",
        Lives::Five => "5",
        Lives::Endless => "endless",
    }
}

fn stop_existing(state: &AppState) -> Result<(), String> {
    let mut guard = state
        .session
        .lock()
        .map_err(|_| "session state lock failed".to_owned())?;
    if let Some(mut session) = guard.take() {
        terminate_child(&mut session.melon);
        terminate_child(&mut session.bridge);
    }
    Ok(())
}

fn terminate_child(child: &mut Child) {
    if matches!(child.try_wait(), Ok(None)) {
        let _ = child.kill();
    }
    let _ = child.wait();
}

fn process_state(child: &mut Child) -> Result<String, String> {
    match child.try_wait() {
        Ok(Some(status)) => Ok(format!("exited({})", status.code().unwrap_or(-1))),
        Ok(None) => Ok("running".into()),
        Err(err) => Err(format!("process status failed: {err}")),
    }
}

fn run_bridge_signaling_smoke(bridge_path: &Path) -> Result<String, String> {
    let output = Command::new(bridge_path)
        .arg("webrtc-signaling-udp-pair-smoke")
        .output()
        .map_err(|err| format!("bridge signaling smoke の起動に失敗しました: {err}"))?;
    let stdout = String::from_utf8_lossy(&output.stdout);
    let stderr = String::from_utf8_lossy(&output.stderr);
    if !output.status.success() {
        let details = if stderr.trim().is_empty() {
            stdout.trim()
        } else {
            stderr.trim()
        };
        return Err(format!(
            "bridge signaling smoke が失敗しました status={} details={}",
            output.status.code().unwrap_or(-1),
            shorten(details, 1200)
        ));
    }
    if !stdout.contains("signaling udp pair smoke passed") {
        return Err(format!(
            "bridge signaling smoke の成功ログを確認できません: {}",
            shorten(stdout.trim(), 1200)
        ));
    }
    Ok(shorten(stdout.trim(), 1200))
}

fn shorten(value: &str, max_chars: usize) -> String {
    let mut result = String::new();
    for ch in value.chars().take(max_chars) {
        result.push(ch);
    }
    if value.chars().count() > max_chars {
        result.push_str("...");
    }
    result
}

fn create_log_dir(app: &AppHandle) -> Result<PathBuf, String> {
    let root = app_data_dir(app)?;
    let stamp = chrono_like_stamp();
    let log_dir = root.join("logs").join(format!("nsmb-mvl-gui-{stamp}"));
    fs::create_dir_all(&log_dir).map_err(|err| format!("log dir を作成できません: {err}"))?;
    Ok(log_dir)
}

fn chrono_like_stamp() -> String {
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();
    now.to_string()
}

fn absolutize_existing(value: &str) -> Result<PathBuf, String> {
    let path = PathBuf::from(value.trim());
    if path.exists() {
        return path
            .canonicalize()
            .map_err(|err| format!("path を解決できません: {err}"));
    }
    Err(format!("ファイルが見つかりません: {}", path.display()))
}

fn absolutize_target(app: &AppHandle, value: &str) -> Result<PathBuf, String> {
    let path = PathBuf::from(value.trim());
    if path.as_os_str().is_empty() {
        return Err("出力ROMパスを指定してください".into());
    }
    if path.is_absolute() {
        Ok(path)
    } else {
        Ok(app_data_dir(app)?.join(path))
    }
}

fn ensure_parent_dir(path: &Path) -> Result<(), String> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)
            .map_err(|err| format!("出力先ディレクトリを作成できません: {err}"))?;
    }
    Ok(())
}

fn find_melonds_binary(app: &AppHandle) -> Result<PathBuf, String> {
    find_binary(
        app,
        &[
            PathBuf::from("build/release-windows-x86_64/melonDS.exe"),
            PathBuf::from("build/release-windows-x86_64/melonDS"),
        ],
        "melonDS",
    )
}

fn find_bridge_binary(app: &AppHandle) -> Result<PathBuf, String> {
    find_binary(
        app,
        &[
            PathBuf::from("tools/nsmb-net-bridge/target/release/nsmb-net-bridge.exe"),
            PathBuf::from("tools/nsmb-net-bridge/target/release/nsmb-net-bridge"),
            PathBuf::from("tools/nsmb-net-bridge/target/debug/nsmb-net-bridge.exe"),
            PathBuf::from("tools/nsmb-net-bridge/target/debug/nsmb-net-bridge"),
        ],
        "nsmb-net-bridge",
    )
}

fn find_binary(app: &AppHandle, dev_candidates: &[PathBuf], stem: &str) -> Result<PathBuf, String> {
    let mut bundled = Vec::new();
    if let Ok(resource_dir) = app.path().resource_dir() {
        bundled.push(resource_dir.join("binaries"));
    }
    if let Ok(exe) = std::env::current_exe() {
        if let Some(parent) = exe.parent() {
            bundled.push(parent.to_path_buf());
            bundled.push(parent.join("binaries"));
        }
    }

    let names = bundled_binary_names(stem);
    for dir in bundled {
        for name in &names {
            let path = dir.join(name);
            if path.exists() {
                return path
                    .canonicalize()
                    .map_err(|err| format!("bundled binary path を解決できません: {err}"));
            }
        }
    }

    if let Ok(root) = repo_root() {
        for candidate in dev_candidates {
            let path = root.join(candidate);
            if path.exists() {
                return path
                    .canonicalize()
                    .map_err(|err| format!("binary path を解決できません: {err}"));
            }
        }
    }

    Err(format!("{stem} の実行ファイルが見つかりません"))
}

fn find_binary_without_app(dev_candidates: &[PathBuf], stem: &str) -> Result<PathBuf, String> {
    find_binary_in_dirs(&standalone_resource_dirs(), dev_candidates, stem)
}

fn find_binary_in_dirs(
    resource_dirs: &[PathBuf],
    dev_candidates: &[PathBuf],
    stem: &str,
) -> Result<PathBuf, String> {
    let names = bundled_binary_names(stem);
    for dir in resource_dirs {
        for name in &names {
            let path = dir.join(name);
            if path.exists() {
                return path
                    .canonicalize()
                    .map_err(|err| format!("bundled binary path を解決できません: {err}"));
            }
        }
    }

    if let Ok(root) = repo_root() {
        for candidate in dev_candidates {
            let path = root.join(candidate);
            if path.exists() {
                return path
                    .canonicalize()
                    .map_err(|err| format!("binary path を解決できません: {err}"));
            }
        }
    }

    Err(format!("{stem} の実行ファイルが見つかりません"))
}

fn bundled_binary_names(stem: &str) -> Vec<String> {
    let mut names = vec![stem.to_owned()];
    if cfg!(windows) {
        names.push(format!("{stem}.exe"));
        names.push(format!("{stem}-x86_64-pc-windows-msvc.exe"));
    } else if cfg!(target_os = "macos") {
        names.push(format!("{stem}-x86_64-apple-darwin"));
        names.push(format!("{stem}-aarch64-apple-darwin"));
    } else {
        names.push(format!("{stem}-x86_64-unknown-linux-gnu"));
    }
    names
}

fn standalone_resource_dirs() -> Vec<PathBuf> {
    let mut dirs = Vec::new();
    if let Ok(exe) = std::env::current_exe() {
        if let Some(parent) = exe.parent() {
            dirs.push(parent.to_path_buf());
            dirs.push(parent.join("binaries"));
        }
    }
    dirs
}

fn find_melonds_binary_without_app() -> Result<PathBuf, String> {
    find_binary_without_app(
        &[
            PathBuf::from("build/release-windows-x86_64/melonDS.exe"),
            PathBuf::from("build/release-windows-x86_64/melonDS"),
        ],
        "melonDS",
    )
}

fn find_bridge_binary_without_app() -> Result<PathBuf, String> {
    find_binary_without_app(
        &[
            PathBuf::from("tools/nsmb-net-bridge/target/release/nsmb-net-bridge.exe"),
            PathBuf::from("tools/nsmb-net-bridge/target/release/nsmb-net-bridge"),
            PathBuf::from("tools/nsmb-net-bridge/target/debug/nsmb-net-bridge.exe"),
            PathBuf::from("tools/nsmb-net-bridge/target/debug/nsmb-net-bridge"),
        ],
        "nsmb-net-bridge",
    )
}

fn find_input_script(app: &AppHandle) -> Result<PathBuf, String> {
    let root = repo_root()?;
    let dev = root
        .join("tests")
        .join("nsmb_us_direct_mvl_minimal_bootstrap.inputs");
    if dev.exists() {
        return dev
            .canonicalize()
            .map_err(|err| format!("input script path を解決できません: {err}"));
    }

    if let Ok(resource_dir) = app.path().resource_dir() {
        let bundled = resource_dir
            .join("resources")
            .join("nsmb_us_direct_mvl_minimal_bootstrap.inputs");
        if bundled.exists() {
            return bundled
                .canonicalize()
                .map_err(|err| format!("bundled input script path を解決できません: {err}"));
        }
    }

    Err("input script が見つかりません".into())
}

fn find_input_script_without_app() -> Result<PathBuf, String> {
    for dir in standalone_resource_dirs() {
        let bundled = dir
            .join("resources")
            .join("nsmb_us_direct_mvl_minimal_bootstrap.inputs");
        if bundled.exists() {
            return bundled
                .canonicalize()
                .map_err(|err| format!("bundled input script path を解決できません: {err}"));
        }
    }

    let root = repo_root()?;
    let dev = root
        .join("tests")
        .join("nsmb_us_direct_mvl_minimal_bootstrap.inputs");
    if dev.exists() {
        return dev
            .canonicalize()
            .map_err(|err| format!("input script path を解決できません: {err}"));
    }

    Err("input script が見つかりません".into())
}

fn find_symbols_file(app: &AppHandle) -> Result<PathBuf, String> {
    if let Ok(root) = repo_root() {
        for relative in [
            Path::new("tools")
                .join("nsmb-mvl-rom")
                .join("resources")
                .join("symbols9.x"),
            Path::new("external")
                .join("NSMB-Code-Reference")
                .join("symbols9.x"),
        ] {
            let dev = root.join(relative);
            if dev.exists() {
                return dev
                    .canonicalize()
                    .map_err(|err| format!("symbols path を解決できません: {err}"));
            }
        }
    }

    if let Ok(resource_dir) = app.path().resource_dir() {
        let bundled = resource_dir.join("resources").join("symbols9.x");
        if bundled.exists() {
            return bundled
                .canonicalize()
                .map_err(|err| format!("bundled symbols path を解決できません: {err}"));
        }
    }

    Err("symbols9.x が見つかりません".into())
}

fn find_symbols_file_without_app() -> Result<PathBuf, String> {
    for dir in standalone_resource_dirs() {
        let bundled = dir.join("resources").join("symbols9.x");
        if bundled.exists() {
            return bundled
                .canonicalize()
                .map_err(|err| format!("bundled symbols path を解決できません: {err}"));
        }
    }

    if let Ok(root) = repo_root() {
        for relative in [
            Path::new("tools")
                .join("nsmb-mvl-rom")
                .join("resources")
                .join("symbols9.x"),
            Path::new("external")
                .join("NSMB-Code-Reference")
                .join("symbols9.x"),
        ] {
            let dev = root.join(relative);
            if dev.exists() {
                return dev
                    .canonicalize()
                    .map_err(|err| format!("symbols path を解決できません: {err}"));
            }
        }
    }

    Err("symbols9.x が見つかりません".into())
}

fn saved_path_or_default(value: &str, fallback: PathBuf) -> String {
    let value = value.trim();
    if value.is_empty() {
        fallback.to_string_lossy().into_owned()
    } else {
        value.to_owned()
    }
}

fn launcher_settings_path(app: &AppHandle) -> Result<PathBuf, String> {
    Ok(app_data_dir(app)?.join("launcher-settings.json"))
}

fn load_launcher_settings(app: &AppHandle) -> Result<LauncherSettings, String> {
    let path = launcher_settings_path(app)?;
    if !path.exists() {
        return Ok(LauncherSettings::default());
    }
    let content = fs::read_to_string(&path)
        .map_err(|err| format!("launcher settings を読み込めません: {err}"))?;
    serde_json::from_str(&content)
        .map_err(|err| format!("launcher settings の形式が不正です: {err}"))
}

fn save_launcher_settings(app: &AppHandle, settings: &LauncherSettings) -> Result<(), String> {
    let path = launcher_settings_path(app)?;
    let content = serde_json::to_string_pretty(settings)
        .map_err(|err| format!("launcher settings をJSON化できません: {err}"))?;
    fs::write(&path, format!("{content}\n"))
        .map_err(|err| format!("launcher settings を保存できません: {err}"))
}

fn app_data_dir(app: &AppHandle) -> Result<PathBuf, String> {
    let path = app
        .path()
        .app_data_dir()
        .map_err(|err| format!("アプリデータディレクトリを解決できません: {err}"))?;
    fs::create_dir_all(&path)
        .map_err(|err| format!("アプリデータディレクトリを作成できません: {err}"))?;
    Ok(path)
}

fn repo_root() -> Result<PathBuf, String> {
    let manifest = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    manifest
        .parent()
        .and_then(Path::parent)
        .and_then(Path::parent)
        .map(Path::to_path_buf)
        .ok_or_else(|| "repo root を解決できません".to_owned())
}

fn main() {
    if std::env::args().any(|arg| arg == "--preflight") {
        match cli_preflight_check() {
            Ok(response) => {
                println!(
                    "{}",
                    serde_json::to_string_pretty(&response)
                        .unwrap_or_else(|_| format!("{response:?}"))
                );
            }
            Err(err) => {
                eprintln!("{err}");
                std::process::exit(1);
            }
        }
        return;
    }

    tauri::Builder::default()
        .manage(AppState::default())
        .invoke_handler(tauri::generate_handler![
            get_defaults,
            save_rom_paths,
            select_rom_file,
            preflight_check,
            generate_roms,
            ensure_roms,
            start_match,
            stop_match,
            session_status
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::{OsStr, OsString};

    fn request(role: Role) -> LaunchRequest {
        LaunchRequest {
            role,
            signal_url: "ws://127.0.0.1:8787/session".to_owned(),
            room_code: "room_01-test".to_owned(),
            port: 8165,
            rom_path: "unused.nds".to_owned(),
            settings: GameSettings {
                course_mode: CourseMode::Random,
                wins: 3,
                big_stars: 10,
                lives: Lives::Five,
                match_seed: "7".to_owned(),
            },
        }
    }

    fn temp_log_dir(name: &str) -> PathBuf {
        let path =
            std::env::temp_dir().join(format!("nsmb-mvl-gui-test-{name}-{}", std::process::id()));
        let _ = fs::remove_dir_all(&path);
        fs::create_dir_all(&path).expect("create temp log dir");
        path
    }

    fn fake_executable(dir: &Path, name: &str, should_sleep: bool) -> PathBuf {
        #[cfg(windows)]
        {
            let path = dir.join(format!("{name}.cmd"));
            let body = if should_sleep {
                format!(
                    "@echo off\r\necho {name} args:%*\r\necho {name} role:%MELONDS_NSML_ROLE%\r\necho {name} stage:%MELONDS_NSML_MVL_STAGE%\r\nping -n 60 127.0.0.1 > nul\r\n"
                )
            } else {
                "@echo off\r\nexit /b 42\r\n".to_owned()
            };
            fs::write(&path, body).expect("write fake cmd");
            path
        }

        #[cfg(not(windows))]
        {
            use std::os::unix::fs::PermissionsExt;

            let path = dir.join(name);
            let body = if should_sleep {
                format!(
                    "#!/bin/sh\necho \"{name} args:$*\"\necho \"{name} role:$MELONDS_NSML_ROLE\"\necho \"{name} stage:$MELONDS_NSML_MVL_STAGE\"\nsleep 60\n"
                )
            } else {
                "#!/bin/sh\nexit 42\n".to_owned()
            };
            fs::write(&path, body).expect("write fake sh");
            let mut permissions = fs::metadata(&path).expect("fake sh metadata").permissions();
            permissions.set_mode(0o755);
            fs::set_permissions(&path, permissions).expect("chmod fake sh");
            path
        }
    }

    fn fake_bridge_smoke_executable(dir: &Path, name: &str, supports_smoke: bool) -> PathBuf {
        #[cfg(windows)]
        {
            let path = dir.join(format!("{name}.cmd"));
            let body = if supports_smoke {
                "@echo off\r\nif \"%1\"==\"webrtc-signaling-udp-pair-smoke\" (\r\n  echo nsmb-net-bridge webrtc: signaling udp pair smoke passed\r\n  exit /b 0\r\n)\r\necho usage\r\nexit /b 0\r\n"
            } else {
                "@echo off\r\necho unknown command \"%1\" 1>&2\r\nexit /b 1\r\n"
            };
            fs::write(&path, body).expect("write fake bridge smoke cmd");
            path
        }

        #[cfg(not(windows))]
        {
            use std::os::unix::fs::PermissionsExt;

            let path = dir.join(name);
            let body = if supports_smoke {
                "#!/bin/sh\nif [ \"$1\" = \"webrtc-signaling-udp-pair-smoke\" ]; then\n  echo \"nsmb-net-bridge webrtc: signaling udp pair smoke passed\"\n  exit 0\nfi\necho usage\nexit 0\n"
            } else {
                "#!/bin/sh\necho \"unknown command \\\"$1\\\"\" >&2\nexit 1\n"
            };
            fs::write(&path, body).expect("write fake bridge smoke sh");
            let mut permissions = fs::metadata(&path)
                .expect("fake bridge smoke sh metadata")
                .permissions();
            permissions.set_mode(0o755);
            fs::set_permissions(&path, permissions).expect("chmod fake bridge smoke sh");
            path
        }
    }

    fn launch_paths(dir: &Path, bridge_path: PathBuf, melon_path: PathBuf) -> LaunchPaths {
        let input_script = dir.join("bootstrap.inputs");
        let rom_path = dir.join("host.nds");
        fs::write(&input_script, b"").expect("write input script");
        fs::write(&rom_path, b"fake rom").expect("write fake rom");
        LaunchPaths {
            log_dir: dir.join("logs"),
            bridge_path,
            melon_path,
            input_script,
            rom_path,
        }
    }

    fn wait_for_file_contains(path: &Path, needle: &str) -> String {
        for _ in 0..50 {
            let content = fs::read_to_string(path).unwrap_or_default();
            if content.contains(needle) {
                return content;
            }
            std::thread::sleep(std::time::Duration::from_millis(50));
        }
        fs::read_to_string(path).unwrap_or_default()
    }

    fn arg_strings(command: &Command) -> Vec<String> {
        command
            .get_args()
            .map(|arg| arg.to_string_lossy().into_owned())
            .collect()
    }

    fn env_value(command: &Command, key: &str) -> Option<String> {
        command.get_envs().find_map(|(name, value)| {
            if name == OsStr::new(key) {
                value.map(|value| value.to_string_lossy().into_owned())
            } else {
                None
            }
        })
    }

    #[test]
    fn bridge_command_uses_signaling_offer_for_host() {
        let log_dir = temp_log_dir("bridge-host");
        let command = build_bridge_command(Path::new("bridge.exe"), &request(Role::Host), &log_dir)
            .expect("build bridge command");
        let args = arg_strings(&command);
        assert_eq!(args[0], "webrtc-offer");
        assert!(args
            .windows(2)
            .any(|pair| pair == ["--local-target", "127.0.0.1:8165"]));
        assert!(args
            .windows(2)
            .any(|pair| pair == ["--signal", "ws://127.0.0.1:8787/session"]));
        assert!(args
            .windows(2)
            .any(|pair| pair == ["--session", "room_01-test"]));
        let _ = fs::remove_dir_all(log_dir);
    }

    #[test]
    fn bridge_command_uses_signaling_answer_for_client() {
        let log_dir = temp_log_dir("bridge-client");
        let command =
            build_bridge_command(Path::new("bridge.exe"), &request(Role::Client), &log_dir)
                .expect("build bridge command");
        let args = arg_strings(&command);
        assert_eq!(args[0], "webrtc-answer");
        assert!(args
            .windows(2)
            .any(|pair| pair == ["--local-bind", "127.0.0.1:8165"]));
        assert!(!args.iter().any(|arg| arg == "--local-target"));
        let _ = fs::remove_dir_all(log_dir);
    }

    #[test]
    fn melon_env_carries_game_settings_and_netplay_start() {
        let request = request(Role::Client);
        let env = melon_env(
            &request,
            Path::new("bootstrap.inputs"),
            Path::new("logs/nsmb-mvl-gui-test"),
        );
        assert_eq!(env["MELONDS_NSML_ROLE"], "client");
        assert_eq!(env["MELONDS_NSML_PEER"], "127.0.0.1");
        assert_eq!(env["MELONDS_NSML_MVL_STAGE"], "2");
        assert_eq!(env["MELONDS_NSML_DIRECT_MVL_BOOT_STAGE"], "2");
        assert_eq!(env["MELONDS_NSML_MVL_COURSE_MODE"], "random");
        assert_eq!(env["MELONDS_NSML_MVL_WINS"], "3");
        assert_eq!(env["MELONDS_NSML_MVL_BIG_STARS"], "10");
        assert_eq!(env["MELONDS_NSML_MVL_LIVES"], "5");
        assert_eq!(env["MELONDS_NSML_MVL_AUTO_RESTART_AFTER_RESULT"], "1");
        assert_eq!(env["MELONDS_NSML_NETPLAY_START_FRAME"], "840");
        assert_eq!(env["MELONDS_NSML_MATCH_SEED"], "7");
    }

    #[test]
    fn melon_command_sets_rom_arg_and_environment() {
        let log_dir = temp_log_dir("melon-command");
        let command = build_melon_command(
            Path::new("melonDS.exe"),
            Path::new("host.nds"),
            Path::new("bootstrap.inputs"),
            &request(Role::Host),
            &log_dir,
        )
        .expect("build melon command");
        assert_eq!(arg_strings(&command), vec!["host.nds"]);
        assert_eq!(
            env_value(&command, "MELONDS_NSML_MVL_BIG_STARS").as_deref(),
            Some("10")
        );
        assert_eq!(
            env_value(&command, "MELONDS_NSML_MVL_STAGE").as_deref(),
            Some("2")
        );
        let _ = fs::remove_dir_all(log_dir);
    }

    #[test]
    fn melon_command_sanitizes_inherited_melonds_environment() {
        let mut command = Command::new("melonDS.exe");
        command.env("MELONDS_NSML_NORMALIZE_MVL_ENTRANCE_SPAWN_WRITES", "1");
        command.env("MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTERS", "1");
        command.env("NSMB_MVL_SIGNAL_URL", "ws://127.0.0.1:8787/session");

        remove_inherited_melonds_env_keys(
            &mut command,
            [
                OsString::from("MELONDS_NSML_NORMALIZE_MVL_ENTRANCE_SPAWN_WRITES"),
                OsString::from("MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTERS"),
                OsString::from("NSMB_MVL_SIGNAL_URL"),
            ],
        );

        assert_eq!(
            env_value(&command, "MELONDS_NSML_NORMALIZE_MVL_ENTRANCE_SPAWN_WRITES"),
            None
        );
        assert_eq!(
            env_value(&command, "MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTERS"),
            None
        );
        assert_eq!(
            env_value(&command, "NSMB_MVL_SIGNAL_URL").as_deref(),
            Some("ws://127.0.0.1:8787/session")
        );
    }

    #[test]
    fn validation_rejects_bad_signal_url_and_room_code() {
        let mut bad_signal = request(Role::Host);
        bad_signal.signal_url = "https://example.invalid/session".to_owned();
        assert!(validate_request(&bad_signal).is_err());

        let mut bad_room = request(Role::Host);
        bad_room.room_code = "bad room".to_owned();
        assert!(validate_request(&bad_room).is_err());
    }

    #[test]
    fn selected_stage_uses_match_seed_for_random_course() {
        let mut request = request(Role::Host);
        request.settings.match_seed = "0x0E".to_owned();
        assert_eq!(selected_stage(&request.settings, 4).expect("stage"), 4);

        request.settings.course_mode = CourseMode::Select;
        assert_eq!(selected_stage(&request.settings, 9).expect("stage"), 4);
    }

    #[test]
    fn reusable_rom_marker_requires_current_format_and_rom_file() {
        let dir = temp_log_dir("reusable-rom-marker");
        let rom = dir.join("host.nds");
        fs::write(&rom, b"fake rom").expect("write fake rom");
        assert!(!reusable_rom_is_current(&rom));

        write_reusable_rom_marker(&rom).expect("write marker");
        assert!(reusable_rom_is_current(&rom));

        fs::write(reusable_rom_marker_path(&rom), "old-format\n").expect("write stale marker");
        assert!(!reusable_rom_is_current(&rom));

        fs::remove_file(&rom).expect("remove fake rom");
        write_reusable_rom_marker(&rom).expect("write marker without rom");
        assert!(!reusable_rom_is_current(&rom));
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn bridge_signaling_smoke_requires_success_log() {
        let dir = temp_log_dir("bridge-smoke-ok");
        let bridge = fake_bridge_smoke_executable(&dir, "fake-bridge-smoke", true);
        let output = run_bridge_signaling_smoke(&bridge).expect("bridge smoke");
        assert!(output.contains("signaling udp pair smoke passed"));
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn bridge_signaling_smoke_rejects_old_bridge() {
        let dir = temp_log_dir("bridge-smoke-old");
        let bridge = fake_bridge_smoke_executable(&dir, "fake-bridge-old", false);
        let err = run_bridge_signaling_smoke(&bridge).expect_err("old bridge rejected");
        assert!(err.contains("bridge signaling smoke が失敗しました"));
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn start_match_resolved_launches_processes_and_stop_clears_session() {
        let dir = temp_log_dir("start-match");
        let bridge = fake_executable(&dir, "fake-bridge", true);
        let melon = fake_executable(&dir, "fake-melon", true);
        let paths = launch_paths(&dir, bridge, melon);
        fs::create_dir_all(&paths.log_dir).expect("create logs");

        let state = AppState::default();
        let response =
            start_match_resolved(&state, request(Role::Host), paths).expect("start fake match");
        assert!(response.bridge_pid > 0);
        assert!(response.melon_pid > 0);

        let status = session_status_inner(&state).expect("status");
        assert!(status.active);
        assert_eq!(status.bridge.as_deref(), Some("running"));
        assert_eq!(status.melon.as_deref(), Some("running"));

        let bridge_stdout =
            wait_for_file_contains(&dir.join("logs").join("bridge.stdout.txt"), "fake-bridge");
        assert!(bridge_stdout.contains("webrtc-offer"));
        assert!(bridge_stdout.contains("room_01-test"));

        let melon_stdout =
            wait_for_file_contains(&dir.join("logs").join("melonds.stdout.txt"), "fake-melon");
        assert!(melon_stdout.contains("role:host"));
        assert!(melon_stdout.contains("stage:2"));

        stop_existing(&state).expect("stop fake match");
        let status = session_status_inner(&state).expect("status after stop");
        assert!(!status.active);

        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn start_match_resolved_does_not_store_session_when_melon_spawn_fails() {
        let dir = temp_log_dir("start-match-fail");
        let bridge = fake_executable(&dir, "fake-bridge", true);
        let missing_melon = dir.join("missing-melon.exe");
        let paths = launch_paths(&dir, bridge, missing_melon);
        fs::create_dir_all(&paths.log_dir).expect("create logs");

        let state = AppState::default();
        let err = start_match_resolved(&state, request(Role::Host), paths)
            .expect_err("melon spawn fails");
        assert!(err.contains("melonDS の起動に失敗しました"));

        let status = session_status_inner(&state).expect("status after failed start");
        assert!(!status.active);

        let _ = fs::remove_dir_all(dir);
    }
}
