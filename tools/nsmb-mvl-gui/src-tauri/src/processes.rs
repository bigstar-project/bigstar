use std::collections::BTreeMap;
use std::ffi::OsStr;
use std::fs::{self, File};
use std::io::{BufReader, Read};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::time::{Duration, Instant, UNIX_EPOCH};

#[cfg(windows)]
use std::os::windows::process::CommandExt;

use crate::config::{DEFAULT_FRAMES, NETPLAY_START_FRAME};
use crate::crash_report::{match_result_decided, send_crash_report_async};
use crate::models::{
    BridgeDiagnostics, CourseMode, GameStateMismatch, LaunchRequest, LaunchResponse,
    MelonDiagnostics, MvlPlayerResult, MvlStageResult, Role, SessionStatus,
};
use crate::settings::selected_stage;
use crate::state::{AppState, ManagedSession};
use sha2::{Digest, Sha256};

#[cfg(windows)]
const CREATE_NO_WINDOW: u32 = 0x0800_0000;

const BRIDGE_CONNECT_TIMEOUT: Duration = Duration::from_secs(45);
const BRIDGE_CONNECT_POLL_INTERVAL: Duration = Duration::from_millis(100);

pub(crate) struct LaunchPaths {
    pub(crate) log_dir: PathBuf,
    pub(crate) bridge_path: PathBuf,
    pub(crate) melon_path: PathBuf,
    pub(crate) input_script: PathBuf,
    pub(crate) rom_path: PathBuf,
}

pub(crate) fn start_match_resolved(
    state: &AppState,
    request: LaunchRequest,
    paths: LaunchPaths,
) -> Result<LaunchResponse, String> {
    stop_existing(state)?;
    write_launch_manifest(&paths, &request)?;

    let mut bridge = build_bridge_command(&paths.bridge_path, &request, &paths.log_dir)?;
    let mut bridge_child = bridge
        .spawn()
        .map_err(|err| format!("bridge の起動に失敗しました: {err}"))?;

    if let Err(err) = wait_for_bridge_connected(&mut bridge_child, &paths.log_dir) {
        terminate_child(&mut bridge_child);
        return Err(err);
    }

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
        player_names: request.player_names,
        crash_report_sent: false,
    });

    Ok(response)
}

fn wait_for_bridge_connected(bridge: &mut Child, log_dir: &Path) -> Result<(), String> {
    let started = Instant::now();
    let mut last_diagnostics_error = None;
    loop {
        let bridge_state = process_state(bridge)?;
        if bridge_state != "running" {
            let (_, diagnostics_error) = read_bridge_diagnostics(log_dir);
            let detail = diagnostics_error
                .or(last_diagnostics_error)
                .unwrap_or_else(|| "診断なし".to_owned());
            return Err(format!(
                "bridge が接続前に終了しました state={bridge_state} detail={detail}"
            ));
        }

        let (diagnostics, diagnostics_error) = read_bridge_diagnostics(log_dir);
        if let Some(diagnostics) = diagnostics {
            if diagnostics.phase.as_deref() == Some("connected") {
                return Ok(());
            }
            if let Some(error) = diagnostics.last_error {
                return Err(format!("bridge 接続に失敗しました: {error}"));
            }
        } else if let Some(error) = diagnostics_error {
            last_diagnostics_error = Some(error);
        }

        if started.elapsed() >= BRIDGE_CONNECT_TIMEOUT {
            let detail = last_diagnostics_error
                .map(|error| format!(" detail={error}"))
                .unwrap_or_default();
            return Err(format!(
                "bridge の WebRTC 接続がタイムアウトしました waitedMs={}{}",
                BRIDGE_CONNECT_TIMEOUT.as_millis(),
                detail
            ));
        }
        std::thread::sleep(BRIDGE_CONNECT_POLL_INTERVAL);
    }
}

pub(crate) fn stop_existing(state: &AppState) -> Result<(), String> {
    stop_existing_inner(state, false)
}

pub(crate) fn stop_existing_with_unresolved_report(state: &AppState) -> Result<(), String> {
    stop_existing_inner(state, true)
}

fn stop_existing_inner(state: &AppState, report_unresolved: bool) -> Result<(), String> {
    let mut guard = state
        .session
        .lock()
        .map_err(|_| "session state lock failed".to_owned())?;
    if let Some(mut session) = guard.take() {
        terminate_child(&mut session.melon);
        terminate_child(&mut session.bridge);
        let mvl_results = read_mvl_results(&session.log_dir);
        if report_unresolved && !session.crash_report_sent && !match_result_decided(&mvl_results) {
            session.crash_report_sent = true;
            send_crash_report_async(
                session.log_dir,
                "stopped_by_user".to_owned(),
                "stopped_by_user".to_owned(),
                session.player_names,
                "user_stop",
            );
        }
    }
    Ok(())
}

pub(crate) fn session_status_inner(state: &AppState) -> Result<SessionStatus, String> {
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
            webrtc: None,
            diagnostics_error: None,
            game_state_mismatch: None,
            mvl_results: Vec::new(),
        });
    };

    let melon = process_state(&mut session.melon)?;
    let bridge = process_state(&mut session.bridge)?;
    let active = melon == "running" || bridge == "running";
    let (webrtc, diagnostics_error) = read_bridge_diagnostics(&session.log_dir);
    let game_state_mismatch = read_melon_diagnostics(&session.log_dir)
        .and_then(|diagnostics| diagnostics.game_state_mismatch)
        .filter(should_show_game_state_mismatch_in_gui);
    let mvl_results = read_mvl_results(&session.log_dir);
    if !session.crash_report_sent
        && (melon != "running" || bridge != "running")
        && !match_result_decided(&mvl_results)
    {
        session.crash_report_sent = true;
        send_crash_report_async(
            session.log_dir.clone(),
            melon.clone(),
            bridge.clone(),
            session.player_names.clone(),
            "process_exit",
        );
    }

    Ok(SessionStatus {
        active,
        log_dir: Some(session.log_dir.to_string_lossy().into_owned()),
        melon: Some(melon),
        bridge: Some(bridge),
        webrtc,
        diagnostics_error,
        game_state_mismatch,
        mvl_results,
    })
}

pub(crate) fn should_show_game_state_mismatch_in_gui(mismatch: &GameStateMismatch) -> bool {
    matches!(mismatch.player_global_matches, Some(false))
        || matches!(mismatch.wifi_candidate_matches, Some(false))
        || matches!(mismatch.render_candidate_matches, Some(false))
}

pub(crate) fn build_bridge_command(
    bridge_path: &Path,
    request: &LaunchRequest,
    log_dir: &Path,
) -> Result<Command, String> {
    let mut command = Command::new(bridge_path);
    command.args(bridge_args(request, log_dir));
    command.envs(bridge_env(request));
    command.current_dir(log_dir);
    with_stdio(command, log_dir, "bridge")
}

fn bridge_env(request: &LaunchRequest) -> BTreeMap<String, String> {
    let mut env = BTreeMap::new();
    if request.detailed_logs_enabled {
        env.insert("NSMB_MVL_DETAILED_LOGS".to_owned(), "1".to_owned());
    }
    env
}

fn bridge_args(request: &LaunchRequest, log_dir: &Path) -> Vec<String> {
    let mut args = Vec::new();
    match request.role {
        Role::Host => {
            args.push("webrtc-offer".to_owned());
            args.push("--local-bind".to_owned());
            args.push("127.0.0.1:0".to_owned());
            args.push("--local-target".to_owned());
            args.push(format!("127.0.0.1:{}", request.port));
        }
        Role::Client => {
            args.push("webrtc-answer".to_owned());
            args.push("--local-bind".to_owned());
            args.push(format!("127.0.0.1:{}", request.port));
        }
    }
    let status_file = log_dir
        .join("bridge-status.json")
        .to_string_lossy()
        .into_owned();
    args.push("--signal".to_owned());
    args.push(request.signal_url.trim().to_owned());
    args.push("--session".to_owned());
    args.push(request.room_code.trim().to_owned());
    args.push("--status-file".to_owned());
    args.push(status_file);
    args
}

pub(crate) fn build_melon_command(
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

pub(crate) fn remove_inherited_melonds_env_keys<I, K>(command: &mut Command, keys: I)
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

pub(crate) fn melon_env(
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
    env.insert(
        "MELONDS_NSML_SCREENSHOT_INTERVAL".into(),
        if request.detailed_logs_enabled {
            "60"
        } else {
            "0"
        }
        .into(),
    );
    env.insert(
        "MELONDS_NSML_FIXED_RTC".into(),
        "2020-01-01T00:00:00".into(),
    );
    env.insert("MELONDS_NSML_ALLOW_JIT".into(), "1".into());
    env.insert("MELONDS_NSML_INPUT_NETPLAY_ONLY".into(), "1".into());
    env.insert("MELONDS_NSML_REMOTE_INPUT_TIMEOUT_FATAL".into(), "1".into());
    env.insert("MELONDS_NSML_WAIT_TIMEOUT_MS".into(), "60000".into());
    env.insert("MELONDS_NSML_SEED_WAIT_TIMEOUT_MS".into(), "60000".into());
    env.insert(
        "MELONDS_NSML_DELAY".into(),
        request.settings.input_delay_frames.to_string(),
    );
    env.insert(
        "MELONDS_NSML_INPUT_MAX_FRAME_LEAD".into(),
        request.settings.input_max_frame_lead.to_string(),
    );
    env.insert("MELONDS_NSML_INPUT_UNRELIABLE".into(), "1".into());
    env.insert("MELONDS_NSML_INPUT_BUNDLE_HISTORY".into(), "8".into());
    if request.detailed_logs_enabled {
        env.insert("MELONDS_NSML_INPUT_TRACE".into(), "1".into());
        env.insert("MELONDS_NSML_INPUT_TRACE_INTERVAL".into(), "1".into());
        env.insert("MELONDS_NSML_INPUT_NETPLAY_TRACE".into(), "1".into());
    }
    env.insert("MELONDS_NSML_INPUT_HEALTH_TRACE".into(), "1".into());
    env.insert(
        "MELONDS_NSML_INPUT_HEALTH_TRACE_INTERVAL".into(),
        if request.detailed_logs_enabled {
            "30"
        } else {
            "120"
        }
        .into(),
    );
    env.insert(
        "MELONDS_NSML_INPUT_HEALTH_TRACE_WAIT_THRESHOLD_MS".into(),
        if request.detailed_logs_enabled {
            "1"
        } else {
            "16"
        }
        .into(),
    );
    if request.detailed_logs_enabled {
        env.insert(
            "MELONDS_NSML_GAME_STATE_TRACE".into(),
            log_dir
                .join("melonds-game-state.csv")
                .to_string_lossy()
                .into_owned(),
        );
        env.insert("MELONDS_NSML_GAME_STATE_TRACE_INTERVAL".into(), "30".into());
        env.insert("MELONDS_NSML_GAME_STATE_TRACE_EXTENDED".into(), "1".into());
    }
    env.insert("MELONDS_NSML_STATE_SYNC".into(), "1".into());
    env.insert(
        "MELONDS_NSML_STATE_SYNC_INTERVAL".into(),
        if request.detailed_logs_enabled {
            "30"
        } else {
            "60"
        }
        .into(),
    );
    env.insert("MELONDS_NSML_STATE_SYNC_EXTENDED".into(), "1".into());
    env.insert(
        "MELONDS_NSML_DIAGNOSTICS_FILE".into(),
        log_dir
            .join("melonds-diagnostics.json")
            .to_string_lossy()
            .into_owned(),
    );
    if request.diagnostic_events_enabled || request.detailed_logs_enabled {
        env.insert("MELONDS_NSML_DIAGNOSTIC_EVENTS".into(), "1".into());
        env.insert(
            "MELONDS_NSML_DIAGNOSTIC_EVENTS_FILE".into(),
            log_dir
                .join("melonds-events.jsonl")
                .to_string_lossy()
                .into_owned(),
        );
    }
    if request.settings.rollback_enabled {
        env.insert("MELONDS_NSML_ROLLBACK".into(), "1".into());
        env.insert("MELONDS_NSML_ROLLBACK_BACKEND".into(), "coredelta".into());
        env.insert("MELONDS_NSML_ROLLBACK_WINDOW".into(), "64".into());
        env.insert(
            "MELONDS_NSML_ROLLBACK_CHECKPOINT_INTERVAL".into(),
            "8".into(),
        );
        env.insert("MELONDS_NSML_ROLLBACK_RESIMULATE".into(), "1".into());
        env.insert(
            "MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL".into(),
            "30".into(),
        );
        env.insert(
            "MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE".into(),
            "256".into(),
        );
    }
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
    env.insert(
        "MELONDS_NSML_MVL_STAGE_SEQUENCE".into(),
        request
            .settings
            .course_stages
            .iter()
            .map(u8::to_string)
            .collect::<Vec<_>>()
            .join(","),
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
            crate::models::Lives::Three => "3",
            crate::models::Lives::Five => "5",
            crate::models::Lives::Endless => "endless",
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
    if !request.settings.rng_seeds.is_empty() {
        env.insert(
            "MELONDS_NSML_MATCH_SEED_SEQUENCE".into(),
            request
                .settings
                .rng_seeds
                .iter()
                .map(|seed| seed.trim().to_owned())
                .collect::<Vec<_>>()
                .join(","),
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

fn write_launch_manifest(paths: &LaunchPaths, request: &LaunchRequest) -> Result<(), String> {
    let current_exe = std::env::current_exe().ok();
    let value = serde_json::json!({
        "gui": {
            "version": env!("CARGO_PKG_VERSION"),
            "package": env!("CARGO_PKG_NAME"),
            "exe": current_exe
                .as_deref()
                .map(file_fingerprint)
                .unwrap_or_else(|| serde_json::json!({ "error": "current_exe unavailable" })),
        },
        "rom_identity": request.rom_identity,
        "request": request,
        "paths": {
            "log_dir": paths.log_dir,
            "bridge": paths.bridge_path,
            "melonds": paths.melon_path,
            "input_script": paths.input_script,
            "rom": paths.rom_path,
        },
        "artifacts": {
            "bridge": file_fingerprint(&paths.bridge_path),
            "melonds": file_fingerprint(&paths.melon_path),
            "input_script": file_fingerprint(&paths.input_script),
            "rom": file_fingerprint(&paths.rom_path),
        },
        "launch": {
            "bridge": {
                "cwd": paths.log_dir,
                "args": bridge_args(request, &paths.log_dir),
                "env": bridge_env(request),
            },
            "melonds": {
                "cwd": paths.log_dir,
                "args": [paths.rom_path.to_string_lossy().into_owned()],
                "env": melon_env(request, &paths.input_script, &paths.log_dir),
            },
        },
        "runtime": {
            "os": std::env::consts::OS,
            "arch": std::env::consts::ARCH,
            "family": std::env::consts::FAMILY,
            "current_dir": std::env::current_dir()
                .map(|path| path.to_string_lossy().into_owned())
                .unwrap_or_else(|err| format!("error: {err}")),
        },
    });
    let json = serde_json::to_vec_pretty(&value)
        .map_err(|err| format!("launcher manifest を生成できません: {err}"))?;
    fs::write(paths.log_dir.join("launcher.json"), json)
        .map_err(|err| format!("launcher manifest を保存できません: {err}"))
}

fn file_fingerprint(path: &Path) -> serde_json::Value {
    match file_fingerprint_result(path) {
        Ok(value) => value,
        Err(err) => serde_json::json!({
            "path": path,
            "error": err,
        }),
    }
}

fn file_fingerprint_result(path: &Path) -> Result<serde_json::Value, String> {
    let metadata = fs::metadata(path).map_err(|err| format!("metadata を読み込めません: {err}"))?;
    let modified_unix_ms = metadata
        .modified()
        .ok()
        .and_then(|modified| modified.duration_since(UNIX_EPOCH).ok())
        .map(|duration| duration.as_millis());
    Ok(serde_json::json!({
        "path": path,
        "bytes": metadata.len(),
        "modified_unix_ms": modified_unix_ms,
        "sha256": sha256_file(path)?,
    }))
}

fn sha256_file(path: &Path) -> Result<String, String> {
    let file =
        File::open(path).map_err(|err| format!("{} を読み込めません: {err}", path.display()))?;
    let mut reader = BufReader::new(file);
    let mut hasher = Sha256::new();
    let mut buffer = [0_u8; 64 * 1024];
    loop {
        let read = reader
            .read(&mut buffer)
            .map_err(|err| format!("{} のhash計算に失敗しました: {err}", path.display()))?;
        if read == 0 {
            break;
        }
        hasher.update(&buffer[..read]);
    }
    Ok(format!("{:x}", hasher.finalize()))
}

pub(crate) fn read_bridge_diagnostics(
    log_dir: &Path,
) -> (Option<BridgeDiagnostics>, Option<String>) {
    let path = log_dir.join("bridge-status.json");
    let json = match fs::read(&path) {
        Ok(json) => json,
        Err(err) if err.kind() == std::io::ErrorKind::NotFound => return (None, None),
        Err(err) => return (None, Some(format!("bridge 診断を読めません: {err}"))),
    };
    match serde_json::from_slice(&json) {
        Ok(value) => (Some(value), None),
        Err(err) => (None, Some(format!("bridge 診断 JSON を読めません: {err}"))),
    }
}

pub(crate) fn read_melon_diagnostics(log_dir: &Path) -> Option<MelonDiagnostics> {
    let json = fs::read(log_dir.join("melonds-diagnostics.json")).ok()?;
    serde_json::from_slice(&json).ok()
}

pub(crate) fn read_mvl_results(log_dir: &Path) -> Vec<MvlStageResult> {
    let stdout = match fs::read_to_string(log_dir.join("melonds.stdout.txt")) {
        Ok(stdout) => stdout,
        Err(_) => return Vec::new(),
    };
    let stages = read_launcher_course_stages(log_dir);
    let mut results: Vec<MvlStageResult> = stdout
        .lines()
        .filter_map(parse_mvl_result_line)
        .enumerate()
        .map(|(index, mut result)| {
            result.game_index = (index + 1) as u32;
            result.stage = stages.get(index).copied();
            result
        })
        .collect();
    apply_corrected_match_wins(&mut results);
    results
}

fn read_launcher_course_stages(log_dir: &Path) -> Vec<u8> {
    let json = match fs::read(log_dir.join("launcher.json")) {
        Ok(json) => json,
        Err(_) => return Vec::new(),
    };
    let value: serde_json::Value = match serde_json::from_slice(&json) {
        Ok(value) => value,
        Err(_) => return Vec::new(),
    };
    value
        .get("request")
        .and_then(|request| request.get("settings"))
        .and_then(|settings| settings.get("course_stages"))
        .and_then(serde_json::Value::as_array)
        .map(|stages| {
            stages
                .iter()
                .filter_map(serde_json::Value::as_u64)
                .filter_map(|stage| u8::try_from(stage).ok())
                .collect()
        })
        .unwrap_or_default()
}

fn parse_mvl_result_line(line: &str) -> Option<MvlStageResult> {
    const PREFIX: &str = "NSMB MvL auto restart: result";
    if !line.starts_with(PREFIX) {
        return None;
    }

    let resolved = !line.starts_with("NSMB MvL auto restart: result unresolved");
    let frame = parse_value(line, "frame")?;
    let (stars_mario, stars_luigi) = parse_pair(line, "stars")?;
    let (displayed_mario, displayed_luigi) = parse_pair(line, "displayed")?;
    let (collected_mario, collected_luigi) = parse_pair(line, "collected")?;
    let (lives_mario, lives_luigi) = parse_pair(line, "lives")?;
    let (deaths_mario, deaths_luigi) = parse_pair(line, "deaths")?;
    let (dead_mario, dead_luigi) = parse_pair(line, "dead")?;
    let (mario_match_wins, luigi_match_wins) = parse_pair(line, "matchWins")?;
    let target_wins = parse_value(line, "target")?;
    let logged_winner = if resolved {
        parse_value(line, "winner").and_then(|winner| u8::try_from(winner).ok())
    } else {
        None
    };
    let winner = corrected_stage_winner(logged_winner, dead_mario, dead_luigi);

    Some(MvlStageResult {
        game_index: 0,
        stage: None,
        frame,
        winner,
        mario: MvlPlayerResult {
            stars: stars_mario,
            displayed_stars: displayed_mario,
            collected_stars: collected_mario,
            lives: result_lives(lives_mario, dead_mario),
            deaths: deaths_mario,
            dead: dead_mario != 0,
        },
        luigi: MvlPlayerResult {
            stars: stars_luigi,
            displayed_stars: displayed_luigi,
            collected_stars: collected_luigi,
            lives: result_lives(lives_luigi, dead_luigi),
            deaths: deaths_luigi,
            dead: dead_luigi != 0,
        },
        mario_match_wins,
        luigi_match_wins,
        target_wins,
        resolved,
        line: line.to_owned(),
    })
}

fn corrected_stage_winner(
    logged_winner: Option<u8>,
    dead_mario: u32,
    dead_luigi: u32,
) -> Option<u8> {
    match (dead_mario != 0, dead_luigi != 0) {
        (true, false) => Some(1),
        (false, true) => Some(0),
        _ => logged_winner,
    }
}

fn apply_corrected_match_wins(results: &mut [MvlStageResult]) {
    let mut mario_wins = 0;
    let mut luigi_wins = 0;
    for result in results {
        match result.winner {
            Some(0) if result.resolved => mario_wins += 1,
            Some(1) if result.resolved => luigi_wins += 1,
            _ => {}
        }
        result.mario_match_wins = mario_wins;
        result.luigi_match_wins = luigi_wins;
    }
}

fn result_lives(lives: u32, dead: u32) -> u32 {
    if dead == 0 {
        lives
    } else {
        0
    }
}

fn parse_value(line: &str, key: &str) -> Option<u32> {
    line.split_whitespace()
        .find_map(|part| part.strip_prefix(&format!("{key}=")))
        .and_then(|value| value.parse().ok())
}

fn parse_pair(line: &str, key: &str) -> Option<(u32, u32)> {
    let value = line
        .split_whitespace()
        .find_map(|part| part.strip_prefix(&format!("{key}=")))?;
    let (left, right) = value.split_once('/')?;
    Some((left.parse().ok()?, right.parse().ok()?))
}

#[cfg(windows)]
pub(crate) fn hide_child_console_window(command: &mut Command) {
    command.creation_flags(CREATE_NO_WINDOW);
}

#[cfg(not(windows))]
pub(crate) fn hide_child_console_window(_command: &mut Command) {}

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

pub(crate) fn run_bridge_signaling_smoke(bridge_path: &Path) -> Result<String, String> {
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
