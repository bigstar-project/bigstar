use std::fs;
use std::path::PathBuf;
use std::process::{Command, Stdio};
use tauri::{AppHandle, State};

#[cfg(windows)]
use std::os::windows::process::CommandExt;

use crate::config::{DEFAULT_PORT, DEFAULT_ROOM_CODE, DEFAULT_SIGNAL_URL};
use crate::models::{
    Defaults, GenerateRomRequest, GenerateRomResponse, LaunchRequest, LaunchResponse,
    LauncherSettings, SaveRomPathsRequest, SessionStatus,
};
use crate::paths::{
    absolutize_existing, app_data_dir, create_log_dir, find_bridge_binary, find_input_script,
    find_melonds_binary, fixed_generated_rom_paths, load_launcher_settings, open_allowed_log_dir,
    repo_root, save_launcher_settings, saved_path_or_default,
};
use crate::processes::{session_status_inner, start_match_resolved, stop_existing, LaunchPaths};
use crate::roms::prepare_roms;
use crate::settings::validate_request;
use crate::state::AppState;

#[tauri::command]
#[specta::specta]
pub(crate) fn get_defaults(app: AppHandle) -> Result<Defaults, String> {
    let app_dir = app_data_dir(&app)?;
    fs::create_dir_all(&app_dir)
        .map_err(|err| format!("アプリデータディレクトリを作成できません: {err}"))?;
    let (host_rom, client_rom) = fixed_generated_rom_paths(&app)?;
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
        host_rom_path: host_rom.to_string_lossy().into_owned(),
        client_rom_path: client_rom.to_string_lossy().into_owned(),
        base_rom_path: saved_path_or_default(
            &saved.base_rom_path,
            dev_base_rom.unwrap_or_else(|| app_dir.join("roms").join("nsmb-us.nds")),
        ),
        port: DEFAULT_PORT,
    })
}

#[tauri::command]
#[specta::specta]
pub(crate) fn save_rom_paths(app: AppHandle, request: SaveRomPathsRequest) -> Result<(), String> {
    let settings = LauncherSettings {
        base_rom_path: request.base_rom_path,
    };
    save_launcher_settings(&app, &settings)
}

#[tauri::command]
#[specta::specta]
pub(crate) fn select_rom_file(current_path: String) -> Result<Option<String>, String> {
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
#[specta::specta]
pub(crate) fn open_log_dir(app: AppHandle, path: String) -> Result<(), String> {
    open_allowed_log_dir(app, path)
}

#[tauri::command]
#[specta::specta]
pub(crate) fn open_melonds(app: AppHandle) -> Result<u32, String> {
    launch_melonds(&app, &[])
}

#[tauri::command]
#[specta::specta]
pub(crate) fn open_melonds_input_config(app: AppHandle) -> Result<u32, String> {
    launch_melonds(&app, &["--open-input-config"])
}

#[tauri::command]
#[specta::specta]
pub(crate) fn start_match(
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

#[tauri::command]
#[specta::specta]
pub(crate) async fn generate_roms(
    app: AppHandle,
    request: GenerateRomRequest,
) -> Result<GenerateRomResponse, String> {
    prepare_roms_on_blocking_thread(app, request, true).await
}

#[tauri::command]
#[specta::specta]
pub(crate) async fn ensure_roms(
    app: AppHandle,
    request: GenerateRomRequest,
) -> Result<GenerateRomResponse, String> {
    prepare_roms_on_blocking_thread(app, request, false).await
}

#[tauri::command]
#[specta::specta]
pub(crate) fn stop_match(state: State<'_, AppState>) -> Result<(), String> {
    stop_existing(state.inner())
}

#[tauri::command]
#[specta::specta]
pub(crate) fn session_status(state: State<'_, AppState>) -> Result<SessionStatus, String> {
    session_status_inner(state.inner())
}

fn launch_melonds(app: &AppHandle, args: &[&str]) -> Result<u32, String> {
    let melon_path = find_melonds_binary(app)?;
    let mut command = Command::new(&melon_path);
    command.args(args);
    if let Some(parent) = melon_path.parent() {
        command.current_dir(parent);
    }
    command.stdin(Stdio::null());
    command.stdout(Stdio::null());
    command.stderr(Stdio::null());
    hide_child_console_window(&mut command);
    command
        .spawn()
        .map(|child| child.id())
        .map_err(|err| format!("melonDS の起動に失敗しました: {err}"))
}

async fn prepare_roms_on_blocking_thread(
    app: AppHandle,
    request: GenerateRomRequest,
    force: bool,
) -> Result<GenerateRomResponse, String> {
    tauri::async_runtime::spawn_blocking(move || prepare_roms(&app, request, force))
        .await
        .map_err(|err| format!("ROM準備 worker が停止しました: {err}"))?
}

#[cfg(windows)]
fn hide_child_console_window(command: &mut Command) {
    command.creation_flags(0x0800_0000);
}

#[cfg(not(windows))]
fn hide_child_console_window(_command: &mut Command) {}
