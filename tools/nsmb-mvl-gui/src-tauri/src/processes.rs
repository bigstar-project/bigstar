use std::collections::BTreeMap;
use std::ffi::OsStr;
use std::fs::{self, File};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};

#[cfg(windows)]
use std::os::windows::process::CommandExt;

use crate::config::{DEFAULT_FRAMES, NETPLAY_START_FRAME};
use crate::models::{
    BridgeDiagnostics, CourseMode, LaunchRequest, LaunchResponse, Role, SessionStatus,
};
use crate::settings::selected_stage;
use crate::state::{AppState, ManagedSession};

#[cfg(windows)]
const CREATE_NO_WINDOW: u32 = 0x0800_0000;

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

pub(crate) fn stop_existing(state: &AppState) -> Result<(), String> {
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
        });
    };

    let melon = process_state(&mut session.melon)?;
    let bridge = process_state(&mut session.bridge)?;
    let active = melon == "running" || bridge == "running";
    let (webrtc, diagnostics_error) = read_bridge_diagnostics(&session.log_dir);

    Ok(SessionStatus {
        active,
        log_dir: Some(session.log_dir.to_string_lossy().into_owned()),
        melon: Some(melon),
        bridge: Some(bridge),
        webrtc,
        diagnostics_error,
    })
}

pub(crate) fn build_bridge_command(
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
    let status_file = log_dir
        .join("bridge-status.json")
        .to_string_lossy()
        .into_owned();
    command.args([
        "--signal",
        request.signal_url.trim(),
        "--session",
        request.room_code.trim(),
        "--status-file",
        &status_file,
    ]);
    with_stdio(command, log_dir, "bridge")
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
        request.settings.input_delay_frames.to_string(),
    );
    env.insert(
        "MELONDS_NSML_INPUT_MAX_FRAME_LEAD".into(),
        request.settings.input_max_frame_lead.to_string(),
    );
    env.insert("MELONDS_NSML_INPUT_UNRELIABLE".into(), "1".into());
    env.insert("MELONDS_NSML_INPUT_BUNDLE_HISTORY".into(), "8".into());
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
    let value = serde_json::json!({
        "request": request,
        "paths": {
            "log_dir": paths.log_dir,
            "bridge": paths.bridge_path,
            "melonds": paths.melon_path,
            "input_script": paths.input_script,
            "rom": paths.rom_path,
        }
    });
    let json = serde_json::to_vec_pretty(&value)
        .map_err(|err| format!("launcher manifest を生成できません: {err}"))?;
    fs::write(paths.log_dir.join("launcher.json"), json)
        .map_err(|err| format!("launcher manifest を保存できません: {err}"))
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
