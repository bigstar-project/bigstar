use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use tauri::{AppHandle, Manager, State};
use tauri_plugin_autostart::ManagerExt;

#[cfg(windows)]
use std::os::windows::process::CommandExt;

use crate::config::{default_signal_url, DEFAULT_PORT, DEFAULT_ROOM_CODE};
use crate::models::{
    Defaults, GenerateRomRequest, GenerateRomResponse, LaunchRequest, LaunchResponse,
    MatchHistoryRecord, SaveDiagnosticEventsRequest, SaveNewRoomNotificationsRequest,
    SavePlayerNameRequest, SaveRomPathsRequest, SessionStatus, ShowNewRoomNotificationRequest,
};
use crate::paths::{
    absolutize_existing, app_data_dir, create_log_dir, find_bridge_binary, find_input_script,
    find_melonds_binary, fixed_generated_rom_paths, load_launcher_settings,
    load_match_history as load_match_history_file, open_allowed_log_dir, save_launcher_settings,
    save_match_history as save_match_history_file,
};
use crate::processes::{
    remove_inherited_melonds_env_keys, session_status_inner, start_match_resolved,
    stop_existing_with_unresolved_report, LaunchPaths,
};
use crate::roms::prepare_roms;
use crate::settings::validate_request;
use crate::state::AppState;
use crate::windowing::show_main_window;

#[tauri::command]
#[specta::specta]
pub(crate) fn get_defaults(app: AppHandle) -> Result<Defaults, String> {
    let app_dir = app_data_dir(&app)?;
    fs::create_dir_all(&app_dir)
        .map_err(|err| format!("アプリデータディレクトリを作成できません: {err}"))?;
    let (host_rom, client_rom) = fixed_generated_rom_paths(&app)?;
    let mut saved = load_launcher_settings(&app)?;
    if saved.player_profile_id.trim().is_empty() {
        saved.player_profile_id = uuid::Uuid::new_v4().to_string();
        save_launcher_settings(&app, &saved)?;
    }
    let signal_url =
        std::env::var("NSMB_MVL_SIGNAL_URL").unwrap_or_else(|_| default_signal_url().to_owned());

    Ok(Defaults {
        signal_url,
        room_code: DEFAULT_ROOM_CODE.to_owned(),
        host_rom_path: host_rom.to_string_lossy().into_owned(),
        client_rom_path: client_rom.to_string_lossy().into_owned(),
        base_rom_path: saved.base_rom_path.trim().to_owned(),
        player_name: saved.player_name.trim().to_owned(),
        player_profile_id: saved.player_profile_id.trim().to_owned(),
        roms_prepared_once: saved.roms_prepared_once,
        input_config_opened_once: saved.input_config_opened_once,
        port: DEFAULT_PORT,
        diagnostic_events_enabled: saved.diagnostic_events_enabled,
        new_room_notifications_enabled: saved.new_room_notifications_enabled,
    })
}

#[tauri::command]
#[specta::specta]
pub(crate) fn save_rom_paths(app: AppHandle, request: SaveRomPathsRequest) -> Result<(), String> {
    let mut settings = load_launcher_settings(&app)?;
    settings.base_rom_path = request.base_rom_path;
    save_launcher_settings(&app, &settings)
}

#[tauri::command]
#[specta::specta]
pub(crate) fn save_diagnostic_events_enabled(
    app: AppHandle,
    request: SaveDiagnosticEventsRequest,
) -> Result<(), String> {
    let mut settings = load_launcher_settings(&app)?;
    settings.diagnostic_events_enabled = request.enabled;
    save_launcher_settings(&app, &settings)
}

#[tauri::command]
#[specta::specta]
pub(crate) fn save_new_room_notifications_enabled(
    app: AppHandle,
    request: SaveNewRoomNotificationsRequest,
) -> Result<(), String> {
    let mut settings = load_launcher_settings(&app)?;
    settings.new_room_notifications_enabled = request.enabled;
    save_launcher_settings(&app, &settings)
}

#[tauri::command]
#[specta::specta]
pub(crate) fn show_new_room_notification(
    app: AppHandle,
    request: ShowNewRoomNotificationRequest,
) -> Result<bool, String> {
    show_new_room_notification_inner(app, request)
}

#[tauri::command]
#[specta::specta]
pub(crate) fn save_player_name(
    app: AppHandle,
    request: SavePlayerNameRequest,
) -> Result<(), String> {
    let player_name = request.player_name.trim();
    if player_name.is_empty() {
        return Err("プレイヤーネームを入力してください".to_owned());
    }
    if player_name.chars().count() > 32 {
        return Err("プレイヤーネームは32文字以内で入力してください".to_owned());
    }

    let mut settings = load_launcher_settings(&app)?;
    settings.player_name = player_name.to_owned();
    save_launcher_settings(&app, &settings)
}

#[tauri::command]
#[specta::specta]
pub(crate) fn get_startup_enabled(app: AppHandle) -> Result<bool, String> {
    let autolaunch = app.autolaunch();
    let enabled = autolaunch
        .is_enabled()
        .map_err(|err| format!("スタートアップ設定を取得できません: {err}"))?;
    let mut settings = load_launcher_settings(&app)?;

    if settings.startup_configured {
        return Ok(enabled);
    }

    settings.startup_configured = true;
    save_launcher_settings(&app, &settings)?;
    Ok(enabled)
}

#[tauri::command]
#[specta::specta]
pub(crate) fn set_startup_enabled(app: AppHandle, enabled: bool) -> Result<(), String> {
    let autolaunch = app.autolaunch();
    if enabled {
        autolaunch
            .enable()
            .map_err(|err| format!("スタートアップ登録に失敗しました: {err}"))
    } else {
        autolaunch
            .disable()
            .map_err(|err| format!("スタートアップ解除に失敗しました: {err}"))
    }?;
    let mut settings = load_launcher_settings(&app)?;
    settings.startup_configured = true;
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
pub(crate) fn load_match_history(app: AppHandle) -> Result<Vec<MatchHistoryRecord>, String> {
    load_match_history_file(&app)
}

#[tauri::command]
#[specta::specta]
pub(crate) fn save_match_history(
    app: AppHandle,
    matches: Vec<MatchHistoryRecord>,
) -> Result<(), String> {
    save_match_history_file(&app, &matches)
}

#[tauri::command]
#[specta::specta]
pub(crate) fn open_melonds(app: AppHandle) -> Result<u32, String> {
    launch_melonds(&app, &[])
}

#[tauri::command]
#[specta::specta]
pub(crate) fn open_melonds_input_config(app: AppHandle) -> Result<u32, String> {
    let pid = launch_melonds(&app, &["--open-input-config"])?;
    let mut settings = load_launcher_settings(&app)?;
    settings.input_config_opened_once = true;
    save_launcher_settings(&app, &settings)?;
    Ok(pid)
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
    stop_existing_with_unresolved_report(state.inner())
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
    remove_inherited_melonds_env_keys(&mut command, std::env::vars_os().map(|(key, _)| key));
    command.env("MELONDS_NSML_ALLOW_JIT", "1");
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
    tauri::async_runtime::spawn_blocking(move || {
        let response = prepare_roms(&app, request, force)?;
        let mut settings = load_launcher_settings(&app)?;
        settings.roms_prepared_once = true;
        save_launcher_settings(&app, &settings)?;
        Ok::<_, String>(response)
    })
    .await
    .map_err(|err| format!("ROM準備 worker が停止しました: {err}"))?
}

#[cfg(windows)]
fn hide_child_console_window(command: &mut Command) {
    command.creation_flags(0x0800_0000);
}

#[cfg(not(windows))]
fn hide_child_console_window(_command: &mut Command) {}

#[cfg(windows)]
fn show_new_room_notification_inner(
    app: AppHandle,
    request: ShowNewRoomNotificationRequest,
) -> Result<bool, String> {
    let title = request.title.trim();
    let body = request.body.trim();
    if title.is_empty() {
        return Err("通知タイトルが空です".to_owned());
    }

    let exe = tauri::utils::platform::current_exe()
        .map_err(|err| format!("実行ファイルパスを取得できません: {err}"))?;
    let app_id = if let Some(exe_dir) = exe.parent() {
        let curr_dir = exe_dir.display().to_string();
        if !(curr_dir.ends_with("\\target\\debug") || curr_dir.ends_with("\\target\\release")) {
            app.config().identifier.clone()
        } else {
            tauri_winrt_notification::Toast::POWERSHELL_APP_ID.to_owned()
        }
    } else {
        tauri_winrt_notification::Toast::POWERSHELL_APP_ID.to_owned()
    };

    let activation_app = app.clone();
    tauri_winrt_notification::Toast::new(&app_id)
        .title(title)
        .text1(body)
        .duration(tauri_winrt_notification::Duration::Short)
        .sound(None)
        .on_activated(move |_action| {
            let app = activation_app.clone();
            let _ = app.clone().run_on_main_thread(move || {
                show_main_window(app.get_webview_window("main"));
            });
            Ok(())
        })
        .show()
        .map_err(|err| format!("通知の表示に失敗しました: {err}"))?;
    play_new_room_notification_sound(&app);

    Ok(true)
}

#[cfg(windows)]
fn play_new_room_notification_sound(app: &AppHandle) {
    let Ok(sound_path) = find_new_room_notification_sound(app) else {
        return;
    };
    std::thread::spawn(move || {
        use rodio::source::Source;

        let Ok(stream) = rodio::DeviceSinkBuilder::open_default_sink() else {
            return;
        };
        let Ok(file) = std::fs::File::open(sound_path) else {
            return;
        };
        let Ok(source) = rodio::Decoder::try_from(file) else {
            return;
        };
        let player = rodio::Player::connect_new(stream.mixer());
        player.append(source.amplify(0.8));
        player.sleep_until_end();
    });
}

#[cfg(windows)]
fn find_new_room_notification_sound(app: &AppHandle) -> Result<PathBuf, String> {
    if let Ok(root) = crate::paths::repo_root() {
        let dev = root
            .join("tools")
            .join("nsmb-mvl-gui")
            .join("src-tauri")
            .join("resources")
            .join("notification_sound.wav");
        if dev.exists() {
            return canonicalize_sound_path(&dev);
        }
    }

    if let Ok(resource_dir) = app.path().resource_dir() {
        let bundled = resource_dir
            .join("resources")
            .join("notification_sound.wav");
        if bundled.exists() {
            return canonicalize_sound_path(&bundled);
        }
    }

    if let Ok(exe) = std::env::current_exe() {
        if let Some(parent) = exe.parent() {
            let bundled = parent.join("resources").join("notification_sound.wav");
            if bundled.exists() {
                return canonicalize_sound_path(&bundled);
            }
        }
    }

    Err("通知音ファイルが見つかりません".to_owned())
}

#[cfg(windows)]
fn canonicalize_sound_path(path: &Path) -> Result<PathBuf, String> {
    path.canonicalize()
        .map_err(|err| format!("通知音ファイルのパスを解決できません: {err}"))
}

#[cfg(not(windows))]
fn show_new_room_notification_inner(
    _app: AppHandle,
    _request: ShowNewRoomNotificationRequest,
) -> Result<bool, String> {
    Ok(false)
}
