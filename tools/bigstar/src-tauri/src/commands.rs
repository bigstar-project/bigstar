use std::fs;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::atomic::Ordering;
use tauri::{AppHandle, Manager, State};
use tauri_plugin_autostart::ManagerExt;

#[cfg(windows)]
use std::os::windows::process::CommandExt;

use crate::config::{
    app_edition, app_version, build_profile, default_signal_url, DEFAULT_PORT, DEFAULT_ROOM_CODE,
};
use crate::crash_report::{create_feedback_archive, FeedbackArchiveOptions};
use crate::diagnostics::{app_context_file, app_error_files};
use crate::history_store::{
    delete_match_history as delete_match_history_db,
    load_match_history_dashboard as load_match_history_dashboard_db,
    load_match_history_opponents as load_match_history_opponents_db,
    query_match_history as query_match_history_db, upsert_match_history as upsert_match_history_db,
};
use crate::models::{
    CleanupDetailedLogsResponse, Defaults, GenerateRomRequest, GenerateRomResponse, LaunchRequest,
    LaunchResponse, LogArchiveResponse, MatchHistoryDashboard, MatchHistoryFilter,
    MatchHistoryOpponent, MatchHistoryPage, MatchHistoryPageRequest, MatchHistoryRecord,
    SaveAiPlayLogRequest, SaveDetailedLogsRequest, SaveDiagnosticEventsRequest,
    SaveNewRoomNotificationsRequest, SavePerformanceLogsRequest, SavePlayerNameRequest,
    SaveRomPathsRequest, SessionStatus, ShowNewRoomNotificationRequest, UploadLogArchiveRequest,
    UploadLogArchiveResponse,
};
use crate::paths::{
    absolutize_existing, allowed_log_dir, app_data_dir, create_log_dir, find_bridge_binary,
    find_input_script, find_melonds_binary, fixed_generated_rom_paths, load_launcher_settings,
    open_allowed_log_dir, save_launcher_settings,
};
use crate::processes::{
    remove_inherited_melonds_env_keys, session_status_inner, start_match_resolved,
    stop_existing_with_unresolved_report, LaunchPaths,
};
use crate::roms::{prepare_roms, prepared_roms_are_current};
use crate::settings::validate_request;
use crate::state::AppState;
use crate::windowing::show_main_window;

#[cfg(feature = "insiders-edition")]
const MAX_LOG_ARCHIVE_BYTES: u64 = 50 * 1024 * 1024;
#[cfg(not(feature = "insiders-edition"))]
const MAX_LOG_ARCHIVE_BYTES: u64 = 10 * 1024 * 1024;
const LOG_ARCHIVE_UPLOAD_PART_BYTES: usize = 5 * 1024 * 1024;
const DETAILED_LOG_FILES: &[&str] = &[
    "bridge-events.jsonl",
    "bridge-events.jsonl.gz",
    "melonds-events.jsonl",
    "melonds-events.jsonl.gz",
    "melonds-game-state.csv",
    "melonds-game-state.csv.gz",
    "melonds-hang.dmp",
    "melonds-performance.jsonl",
    "melonds-phase-events.jsonl",
    "melonds-phase-events.jsonl.gz",
    "melonds.stdout.txt",
    "melonds-watchdog.jsonl",
    "melonds-watchdog.jsonl.gz",
];
const DETAILED_LOG_DIRS: &[&str] = &["screens"];

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
        std::env::var("BIGSTAR_SIGNAL_URL").unwrap_or_else(|_| default_signal_url().to_owned());

    Ok(Defaults {
        signal_url,
        room_code: DEFAULT_ROOM_CODE.to_owned(),
        host_rom_path: host_rom.to_string_lossy().into_owned(),
        client_rom_path: client_rom.to_string_lossy().into_owned(),
        base_rom_path: saved.base_rom_path.trim().to_owned(),
        player_name: saved.player_name.trim().to_owned(),
        player_profile_id: saved.player_profile_id.trim().to_owned(),
        roms_prepared_once: prepared_roms_are_current(&app, saved.base_rom_path.trim()),
        input_config_opened_once: saved.input_config_opened_once,
        port: DEFAULT_PORT,
        diagnostic_events_enabled: saved.diagnostic_events_enabled,
        detailed_logs_enabled: saved.detailed_logs_enabled,
        ai_play_log_enabled: saved.ai_play_log_enabled,
        performance_logs_enabled: saved.performance_logs_enabled || app_edition() == "public",
        new_room_notifications_enabled: saved.new_room_notifications_enabled,
    })
}

#[tauri::command]
#[specta::specta]
pub(crate) fn save_rom_paths(
    app: AppHandle,
    state: State<'_, AppState>,
    request: SaveRomPathsRequest,
) -> Result<(), String> {
    let mut settings = load_launcher_settings(&app)?;
    if settings.base_rom_path.trim() != request.base_rom_path.trim() {
        settings.roms_prepared_once = false;
    }
    settings.base_rom_path = request.base_rom_path.trim().to_owned();
    save_launcher_settings(&app, &settings)?;
    if !settings.base_rom_path.is_empty() && !session_is_active(state.inner())? {
        spawn_eager_rom_prepare(app, settings.base_rom_path);
    }
    Ok(())
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
pub(crate) fn save_detailed_logs_enabled(
    app: AppHandle,
    request: SaveDetailedLogsRequest,
) -> Result<(), String> {
    let mut settings = load_launcher_settings(&app)?;
    settings.detailed_logs_enabled = request.enabled;
    save_launcher_settings(&app, &settings)
}

#[tauri::command]
#[specta::specta]
pub(crate) fn save_ai_play_log_enabled(
    app: AppHandle,
    request: SaveAiPlayLogRequest,
) -> Result<(), String> {
    let mut settings = load_launcher_settings(&app)?;
    settings.ai_play_log_enabled = request.enabled;
    save_launcher_settings(&app, &settings)
}

#[tauri::command]
#[specta::specta]
pub(crate) fn save_performance_logs_enabled(
    app: AppHandle,
    request: SavePerformanceLogsRequest,
) -> Result<(), String> {
    let mut settings = load_launcher_settings(&app)?;
    settings.performance_logs_enabled = request.enabled;
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
pub(crate) fn create_log_archive(
    app: AppHandle,
    log_dir: String,
) -> Result<LogArchiveResponse, String> {
    let log_dir = resolve_allowed_log_dir(&app, &log_dir)?;
    let app_errors = app_error_files(&app)?;
    let app_context = app_context_file(&app)?;
    let archive = create_feedback_archive(
        &log_dir,
        &FeedbackArchiveOptions {
            category: "other",
            include_performance: true,
            include_detailed_diagnostics: cfg!(feature = "insiders-edition"),
            app_error_files: &app_errors,
            app_context_file: Some(&app_context),
        },
    )?;
    let size = archive_size(&archive.path)?;
    Ok(LogArchiveResponse {
        archive_path: archive.path.to_string_lossy().into_owned(),
        size: archive_size_for_gui(size)?,
        included_files: archive.included_files,
    })
}

#[tauri::command]
#[specta::specta]
pub(crate) fn cleanup_detailed_logs(
    app: AppHandle,
    state: State<'_, AppState>,
) -> Result<CleanupDetailedLogsResponse, String> {
    let logs_root = app_data_dir(&app)?.join("logs");
    let active_log_dir = state
        .session
        .lock()
        .map_err(|_| "session state を取得できません".to_owned())?
        .as_ref()
        .map(|session| session.log_dir.clone());
    cleanup_detailed_logs_in_root(&logs_root, active_log_dir.as_deref())
}

#[tauri::command]
#[specta::specta]
pub(crate) async fn upload_log_archive(
    app: AppHandle,
    request: UploadLogArchiveRequest,
) -> Result<UploadLogArchiveResponse, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let log_dir = resolve_allowed_log_dir(&app, &request.log_dir)?;
        validate_feedback(&request.category, &request.description)?;
        let app_errors = app_error_files(&app)?;
        let app_context = app_context_file(&app)?;
        let archive_path = create_feedback_archive(
            &log_dir,
            &FeedbackArchiveOptions {
                category: request.category.as_str(),
                include_performance: request.include_performance,
                include_detailed_diagnostics: cfg!(feature = "insiders-edition"),
                app_error_files: &app_errors,
                app_context_file: Some(&app_context),
            },
        )?
        .path;
        let result = upload_log_archive_inner(&archive_path, &request);
        let _ = fs::remove_file(&archive_path);
        result
    })
    .await
    .map_err(|err| format!("log archive upload worker が停止しました: {err}"))?
}

#[tauri::command]
#[specta::specta]
pub(crate) fn upsert_match_history(
    app: AppHandle,
    record: MatchHistoryRecord,
) -> Result<(), String> {
    upsert_match_history_db(&app, &record)
}

#[tauri::command]
#[specta::specta]
pub(crate) fn delete_match_history(app: AppHandle, match_id: String) -> Result<(), String> {
    delete_match_history_db(&app, &match_id)
}

#[tauri::command]
#[specta::specta]
pub(crate) fn query_match_history(
    app: AppHandle,
    request: MatchHistoryPageRequest,
) -> Result<MatchHistoryPage, String> {
    query_match_history_db(&app, &request)
}

#[tauri::command]
#[specta::specta]
pub(crate) fn load_match_history_dashboard(
    app: AppHandle,
    filter: MatchHistoryFilter,
) -> Result<MatchHistoryDashboard, String> {
    load_match_history_dashboard_db(&app, &filter)
}

#[tauri::command]
#[specta::specta]
pub(crate) fn load_match_history_opponents(
    app: AppHandle,
) -> Result<Vec<MatchHistoryOpponent>, String> {
    load_match_history_opponents_db(&app)
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
    if session_is_active(state.inner())? {
        return Err("対戦中のため新しい対戦準備を開始できません".to_owned());
    }
    if state
        .launch_in_progress
        .compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire)
        .is_err()
    {
        return Err("別の対戦準備が進行中です".to_owned());
    }
    let _launch_guard = LaunchPreparationGuard(&state.launch_in_progress);
    let settings = load_launcher_settings(&app)?;
    let source_rom = settings.base_rom_path.trim();
    if source_rom.is_empty() {
        return Err("ベースROMが設定されていません".to_owned());
    }
    let prepared = prepare_roms(
        &app,
        GenerateRomRequest {
            source_rom: source_rom.to_owned(),
        },
        false,
    )?;
    let expected_rom = match &request.role {
        crate::models::Role::Host => &prepared.host_rom,
        crate::models::Role::Client => &prepared.client_rom,
    };
    let requested_rom = absolutize_existing(&request.rom_path)?;
    let expected_rom = absolutize_existing(expected_rom)?;
    if requested_rom != expected_rom {
        return Err("対戦直前に準備したROMと起動対象ROMが一致しません".to_owned());
    }
    if request.rom_identity.as_ref() != Some(&prepared.rom_identity) {
        return Err("対戦直前のROM・セーブ同一性が部屋作成／参加時から変化しました".to_owned());
    }
    let log_dir = create_log_dir(&app)?;
    let bridge_path = find_bridge_binary(&app)?;
    let melon_path = find_melonds_binary(&app)?;
    let input_script = find_input_script(&app)?;
    let rom_path = requested_rom;

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

pub(crate) fn start_eager_saved_rom_prepare(app: AppHandle) {
    let settings = match load_launcher_settings(&app) {
        Ok(settings) => settings,
        Err(err) => {
            crate::diagnostics::record_backend_error(&app, "eager_rom_settings", &err);
            return;
        }
    };
    let source_rom = settings.base_rom_path.trim().to_owned();
    if source_rom.is_empty() {
        return;
    }
    let state = app.state::<AppState>();
    if session_is_active(state.inner()).unwrap_or(true) {
        return;
    }
    spawn_eager_rom_prepare(app, source_rom);
}

fn spawn_eager_rom_prepare(app: AppHandle, source_rom: String) {
    let _ = std::thread::Builder::new()
        .name("bigstar-eager-rom-prepare".to_owned())
        .spawn(move || {
            if let Err(err) = prepare_roms(
                &app,
                GenerateRomRequest {
                    source_rom: source_rom.clone(),
                },
                false,
            ) {
                crate::diagnostics::record_backend_error(&app, "eager_rom_prepare", &err);
                return;
            }
            if let Ok(mut settings) = load_launcher_settings(&app) {
                if settings.base_rom_path.trim() == source_rom {
                    settings.roms_prepared_once = true;
                    let _ = save_launcher_settings(&app, &settings);
                }
            }
        });
}

fn session_is_active(state: &AppState) -> Result<bool, String> {
    if state.launch_in_progress.load(Ordering::Acquire) {
        return Ok(true);
    }
    session_status_inner(state).map(|status| status.active)
}

struct LaunchPreparationGuard<'a>(&'a std::sync::atomic::AtomicBool);

impl Drop for LaunchPreparationGuard<'_> {
    fn drop(&mut self) {
        self.0.store(false, Ordering::Release);
    }
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

fn resolve_allowed_log_dir(app: &AppHandle, path: &str) -> Result<PathBuf, String> {
    let logs_root = app_data_dir(app)?.join("logs");
    fs::create_dir_all(&logs_root).map_err(|err| format!("ログ保存先を作成できません: {err}"))?;
    allowed_log_dir(&logs_root, &PathBuf::from(path.trim()))
}

pub(crate) fn cleanup_detailed_logs_in_root(
    logs_root: &Path,
    active_log_dir: Option<&Path>,
) -> Result<CleanupDetailedLogsResponse, String> {
    let mut response = CleanupDetailedLogsResponse {
        scanned_log_dirs: 0,
        skipped_active_log_dirs: 0,
        deleted_files: 0,
        deleted_dirs: 0,
        freed_bytes: 0,
    };

    if !logs_root.exists() {
        return Ok(response);
    }

    let active_log_dir = active_log_dir.and_then(|path| path.canonicalize().ok());
    for entry in
        fs::read_dir(logs_root).map_err(|err| format!("ログフォルダを読み込めません: {err}"))?
    {
        let entry = entry.map_err(|err| format!("ログフォルダの項目を読み込めません: {err}"))?;
        let metadata = entry
            .metadata()
            .map_err(|err| format!("ログフォルダの項目情報を取得できません: {err}"))?;
        if !metadata.is_dir() {
            continue;
        }

        let log_dir = entry.path();
        if active_log_dir
            .as_ref()
            .is_some_and(|active| log_dir.canonicalize().ok().as_ref() == Some(active))
        {
            response.skipped_active_log_dirs += 1;
            continue;
        }

        response.scanned_log_dirs += 1;
        for file_name in DETAILED_LOG_FILES {
            let path = log_dir.join(file_name);
            if !path.is_file() {
                continue;
            }
            response.freed_bytes = response.freed_bytes.saturating_add(
                path.metadata()
                    .map(|meta| bytes_for_gui(meta.len()))
                    .unwrap_or_default(),
            );
            fs::remove_file(&path)
                .map_err(|err| format!("詳細ログを削除できません {}: {err}", path.display()))?;
            response.deleted_files += 1;
        }
        for dir_name in DETAILED_LOG_DIRS {
            let path = log_dir.join(dir_name);
            if !path.is_dir() {
                continue;
            }
            let (files, bytes) = dir_stats(&path)?;
            fs::remove_dir_all(&path).map_err(|err| {
                format!("詳細ログフォルダを削除できません {}: {err}", path.display())
            })?;
            response.deleted_files += files;
            response.deleted_dirs += 1;
            response.freed_bytes = response.freed_bytes.saturating_add(bytes);
        }
    }

    Ok(response)
}

fn dir_stats(path: &Path) -> Result<(u32, u32), String> {
    let mut files = 0;
    let mut bytes: u32 = 0;
    for entry in fs::read_dir(path)
        .map_err(|err| format!("フォルダを読み込めません {}: {err}", path.display()))?
    {
        let entry = entry.map_err(|err| format!("フォルダ項目を読み込めません: {err}"))?;
        let metadata = entry
            .metadata()
            .map_err(|err| format!("フォルダ項目情報を取得できません: {err}"))?;
        if metadata.is_dir() {
            let (child_files, child_bytes) = dir_stats(&entry.path())?;
            files += child_files;
            bytes = bytes.saturating_add(child_bytes);
        } else if metadata.is_file() {
            files += 1;
            bytes = bytes.saturating_add(bytes_for_gui(metadata.len()));
        }
    }
    Ok((files, bytes))
}

fn bytes_for_gui(bytes: u64) -> u32 {
    bytes.min(u32::MAX as u64) as u32
}

fn archive_size(path: &Path) -> Result<u64, String> {
    fs::metadata(path)
        .map(|metadata| metadata.len())
        .map_err(|err| format!("log archive のサイズを取得できません: {err}"))
}

#[derive(serde::Serialize)]
struct CreateUploadRequest {
    file_name: String,
    size: u64,
    category: String,
    description: String,
    app_version: String,
    edition: String,
    schema_version: u8,
}

#[derive(serde::Deserialize)]
struct CreateUploadResponse {
    report_id: String,
    upload_id: String,
    upload_token: String,
    max_size: u64,
    max_part_size: usize,
}

#[derive(serde::Deserialize, serde::Serialize)]
struct UploadedPart {
    #[serde(rename = "partNumber")]
    part_number: u16,
    etag: String,
}

#[derive(serde::Serialize)]
struct CompleteUploadRequest {
    parts: Vec<UploadedPart>,
}

#[derive(serde::Deserialize)]
struct CompleteUploadResponse {
    report_id: String,
}

fn upload_log_archive_inner(
    archive_path: &Path,
    request: &UploadLogArchiveRequest,
) -> Result<UploadLogArchiveResponse, String> {
    let size = archive_size(archive_path)?;
    if size > MAX_LOG_ARCHIVE_BYTES {
        return Err(format!(
            "feedback archive が{}MiBを超えています: {} bytes",
            MAX_LOG_ARCHIVE_BYTES / (1024 * 1024),
            size,
        ));
    }
    let base_url = normalized_upload_url(&request.upload_url)?;
    let file_name = archive_path
        .file_name()
        .and_then(|name| name.to_str())
        .unwrap_or("bigstar-logs.zip")
        .to_owned();
    let client = reqwest::blocking::Client::builder()
        .timeout(std::time::Duration::from_secs(60))
        .build()
        .map_err(|err| format!("feedback upload client を作成できません: {err}"))?;
    let create_response: CreateUploadResponse = send_json(
        client
            .post(format!("{base_url}/uploads"))
            .json(&CreateUploadRequest {
                file_name,
                size,
                category: request.category.as_str().to_owned(),
                description: request.description.trim().to_owned(),
                app_version: app_version().to_owned(),
                edition: app_edition().to_owned(),
                schema_version: 1,
            }),
        "feedback upload の開始",
    )?;
    if create_response.max_size > 0 && size > create_response.max_size {
        return Err(format!(
            "log archive がサーバー上限を超えています: {} bytes",
            size
        ));
    }
    let part_size = create_response
        .max_part_size
        .clamp(1, LOG_ARCHIVE_UPLOAD_PART_BYTES);
    let upload_result = (|| {
        let mut file = fs::File::open(archive_path)
            .map_err(|err| format!("feedback archive を読めません: {err}"))?;
        let mut parts = Vec::new();
        let mut part_number: u16 = 1;
        loop {
            let mut buffer = vec![0_u8; part_size];
            let read = file
                .read(&mut buffer)
                .map_err(|err| format!("feedback archive の読み込みに失敗しました: {err}"))?;
            if read == 0 {
                break;
            }
            buffer.truncate(read);
            let part_url = reqwest::Url::parse(&format!(
                "{base_url}/uploads/{}/{}/parts/{part_number}",
                create_response.report_id, create_response.upload_id
            ))
            .map_err(|err| format!("feedback upload URL が不正です: {err}"))?;
            let part: UploadedPart = send_json(
                client
                    .put(part_url)
                    .header(
                        "x-bigstar-feedback-token",
                        create_response.upload_token.as_str(),
                    )
                    .body(buffer),
                "feedback archive part の送信",
            )?;
            parts.push(part);
            part_number = part_number
                .checked_add(1)
                .ok_or_else(|| "feedback archive part 数が多すぎます".to_owned())?;
        }
        send_json(
            client
                .post(format!(
                    "{base_url}/uploads/{}/{}/complete",
                    create_response.report_id, create_response.upload_id
                ))
                .header(
                    "x-bigstar-feedback-token",
                    create_response.upload_token.as_str(),
                )
                .json(&CompleteUploadRequest { parts }),
            "feedback upload の完了",
        )
    })();
    let complete: CompleteUploadResponse = match upload_result {
        Ok(complete) => complete,
        Err(err) => {
            let _ = client
                .delete(format!(
                    "{base_url}/uploads/{}/{}",
                    create_response.report_id, create_response.upload_id
                ))
                .header(
                    "x-bigstar-feedback-token",
                    create_response.upload_token.as_str(),
                )
                .send();
            return Err(err);
        }
    };
    Ok(UploadLogArchiveResponse {
        report_id: complete.report_id,
        size: archive_size_for_gui(size)?,
    })
}

fn archive_size_for_gui(size: u64) -> Result<u32, String> {
    if size > u32::MAX as u64 {
        return Err(format!("log archive が大きすぎます: {size} bytes"));
    }
    Ok(size as u32)
}

fn normalized_upload_url(value: &str) -> Result<String, String> {
    let mut url = reqwest::Url::parse(value.trim())
        .map_err(|err| format!("log archive upload URL が不正です: {err}"))?;
    match url.scheme() {
        "http" | "https" => {}
        _ => {
            return Err(
                "log archive upload URL は http:// または https:// を指定してください".to_owned(),
            );
        }
    }
    url.set_query(None);
    url.set_fragment(None);
    let normalized = url.as_str().trim_end_matches('/').to_owned();
    if build_profile() == "distribution" {
        let mut expected = reqwest::Url::parse(default_signal_url())
            .map_err(|err| format!("既定 signaling URL が不正です: {err}"))?;
        expected
            .set_scheme(if expected.scheme() == "wss" {
                "https"
            } else {
                "http"
            })
            .map_err(|_| "既定 signaling URL の scheme を変換できません".to_owned())?;
        expected.set_path("/feedback");
        expected.set_query(None);
        expected.set_fragment(None);
        if normalized != expected.as_str().trim_end_matches('/') {
            return Err("配布版の feedback upload URL が既定サーバーと一致しません".to_owned());
        }
    }
    Ok(normalized)
}

fn send_json<T: serde::de::DeserializeOwned>(
    request: reqwest::blocking::RequestBuilder,
    label: &str,
) -> Result<T, String> {
    let response = request
        .send()
        .map_err(|err| format!("{label}に失敗しました: {err}"))?;
    let status = response.status();
    if !status.is_success() {
        let body = response.text().unwrap_or_else(|_| String::new());
        return Err(format!("{label}に失敗しました status={status} {body}"));
    }
    response
        .json()
        .map_err(|err| format!("{label}のレスポンス形式が不正です: {err}"))
}

fn validate_feedback(
    _category: &crate::models::FeedbackCategory,
    description: &str,
) -> Result<(), String> {
    let count = description.trim().chars().count();
    if count == 0 {
        return Err("フィードバックの内容を入力してください".to_owned());
    }
    if count > 4000 {
        return Err("フィードバックの内容は4000文字以内で入力してください".to_owned());
    }
    Ok(())
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
            .join("bigstar")
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
