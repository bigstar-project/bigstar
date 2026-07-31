use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicU32, Ordering};
use std::time::{Duration, SystemTime};
use tauri::{AppHandle, Manager};

use crate::models::{LauncherSettings, MatchHistoryRecord, MvlStageResult};
use crate::processes::hide_child_console_window;

#[cfg(feature = "insiders-edition")]
const LEGACY_INSIDERS_APP_DATA_DIR_NAME: &str = "dev.melonds.nsmb-mvl";
#[cfg(feature = "insiders-edition")]
const LEGACY_INSIDERS_MIGRATION_MARKER: &str = ".legacy-dev.melonds.nsmb-mvl-imported";

pub(crate) fn create_log_dir(app: &AppHandle) -> Result<PathBuf, String> {
    let root = app_data_dir(app)?;
    let logs_root = root.join("logs");
    fs::create_dir_all(&logs_root).map_err(|err| format!("log dir を作成できません: {err}"))?;

    for _ in 0..16 {
        let stamp = chrono_like_stamp();
        let counter = LOG_DIR_COUNTER.fetch_add(1, Ordering::Relaxed);
        let log_dir = logs_root.join(format!("bigstar-{stamp}-{}-{counter}", std::process::id()));
        match fs::create_dir(&log_dir) {
            Ok(()) => return Ok(log_dir),
            Err(err) if err.kind() == std::io::ErrorKind::AlreadyExists => continue,
            Err(err) => return Err(format!("log dir を作成できません: {err}")),
        }
    }

    Err("log dir を一意に作成できません".to_owned())
}

fn chrono_like_stamp() -> String {
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis();
    now.to_string()
}

static LOG_DIR_COUNTER: AtomicU32 = AtomicU32::new(0);

pub(crate) fn cleanup_log_retention(app: &AppHandle) -> Result<(), String> {
    const MAX_SESSIONS: usize = 50;
    const MAX_TOTAL_BYTES: u64 = 500 * 1024 * 1024;
    const MAX_AGE: Duration = Duration::from_secs(30 * 24 * 60 * 60);

    let logs_root = app_data_dir(app)?.join("logs");
    if !logs_root.is_dir() {
        return Ok(());
    }
    let mut entries = fs::read_dir(&logs_root)
        .map_err(|err| format!("log retention directory を読めません: {err}"))?
        .filter_map(Result::ok)
        .filter_map(|entry| {
            let metadata = entry.metadata().ok()?;
            metadata.is_dir().then_some((
                entry.path(),
                metadata.modified().unwrap_or(SystemTime::UNIX_EPOCH),
            ))
        })
        .collect::<Vec<_>>();
    entries.sort_by_key(|(_, modified)| std::cmp::Reverse(*modified));

    let now = SystemTime::now();
    let mut retained_bytes = 0_u64;
    for (index, (path, modified)) in entries.into_iter().enumerate() {
        let bytes = directory_size(&path);
        let expired = now
            .duration_since(modified)
            .map(|age| age > MAX_AGE)
            .unwrap_or(false);
        let exceeds_count = index >= MAX_SESSIONS;
        let exceeds_bytes = retained_bytes.saturating_add(bytes) > MAX_TOTAL_BYTES;
        if expired || exceeds_count || exceeds_bytes {
            fs::remove_dir_all(&path)
                .map_err(|err| format!("期限切れログを削除できません {}: {err}", path.display()))?;
        } else {
            retained_bytes = retained_bytes.saturating_add(bytes);
        }
    }
    Ok(())
}

fn directory_size(path: &Path) -> u64 {
    fs::read_dir(path)
        .ok()
        .into_iter()
        .flatten()
        .filter_map(Result::ok)
        .map(|entry| {
            entry
                .metadata()
                .ok()
                .map(|metadata| {
                    if metadata.is_dir() {
                        directory_size(&entry.path())
                    } else if metadata.is_file() {
                        metadata.len()
                    } else {
                        0
                    }
                })
                .unwrap_or(0)
        })
        .sum()
}

pub(crate) fn absolutize_existing(value: &str) -> Result<PathBuf, String> {
    let path = PathBuf::from(value.trim());
    if path.exists() {
        return path
            .canonicalize()
            .map_err(|err| format!("path を解決できません: {err}"));
    }
    Err(format!("ファイルが見つかりません: {}", path.display()))
}

pub(crate) fn ensure_parent_dir(path: &Path) -> Result<(), String> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)
            .map_err(|err| format!("出力先ディレクトリを作成できません: {err}"))?;
    }
    Ok(())
}

pub(crate) fn find_melonds_binary(app: &AppHandle) -> Result<PathBuf, String> {
    find_binary(
        app,
        &[
            PathBuf::from("build/release-windows-x86_64/melonDS.exe"),
            PathBuf::from("build/release-windows-x86_64/melonDS"),
        ],
        "melonDS",
    )
}

pub(crate) fn find_bridge_binary(app: &AppHandle) -> Result<PathBuf, String> {
    find_binary(
        app,
        &[
            PathBuf::from("tools/bigstar-net-bridge/target/release/bigstar-net-bridge.exe"),
            PathBuf::from("tools/bigstar-net-bridge/target/release/bigstar-net-bridge"),
            PathBuf::from("tools/bigstar-net-bridge/target/debug/bigstar-net-bridge.exe"),
            PathBuf::from("tools/bigstar-net-bridge/target/debug/bigstar-net-bridge"),
        ],
        "bigstar-net-bridge",
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

    find_binary_in_dirs(&bundled, dev_candidates, stem)
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

pub(crate) fn find_melonds_binary_without_app() -> Result<PathBuf, String> {
    find_binary_without_app(
        &[
            PathBuf::from("build/release-windows-x86_64/melonDS.exe"),
            PathBuf::from("build/release-windows-x86_64/melonDS"),
        ],
        "melonDS",
    )
}

pub(crate) fn find_bridge_binary_without_app() -> Result<PathBuf, String> {
    find_binary_without_app(
        &[
            PathBuf::from("tools/bigstar-net-bridge/target/release/bigstar-net-bridge.exe"),
            PathBuf::from("tools/bigstar-net-bridge/target/release/bigstar-net-bridge"),
            PathBuf::from("tools/bigstar-net-bridge/target/debug/bigstar-net-bridge.exe"),
            PathBuf::from("tools/bigstar-net-bridge/target/debug/bigstar-net-bridge"),
        ],
        "bigstar-net-bridge",
    )
}

pub(crate) fn find_input_script(app: &AppHandle) -> Result<PathBuf, String> {
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

pub(crate) fn find_input_script_without_app() -> Result<PathBuf, String> {
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

pub(crate) fn find_symbols_file(app: &AppHandle) -> Result<PathBuf, String> {
    if let Ok(root) = repo_root() {
        for relative in [
            Path::new("tools")
                .join("bigstar-rom")
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

pub(crate) fn find_symbols_file_without_app() -> Result<PathBuf, String> {
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
                .join("bigstar-rom")
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

pub(crate) fn fixed_generated_rom_paths(app: &AppHandle) -> Result<(PathBuf, PathBuf), String> {
    let rom_dir = app_data_dir(app)?.join("roms");
    fs::create_dir_all(&rom_dir).map_err(|err| format!("ROM保存先を作成できません: {err}"))?;
    Ok((
        rom_dir.join("bigstar-host.nds"),
        rom_dir.join("bigstar-client.nds"),
    ))
}

fn launcher_settings_path(app: &AppHandle) -> Result<PathBuf, String> {
    Ok(app_data_dir(app)?.join("launcher-settings.json"))
}

pub(crate) fn load_launcher_settings(app: &AppHandle) -> Result<LauncherSettings, String> {
    let path = launcher_settings_path(app)?;
    if !path.exists() {
        return Ok(LauncherSettings::default());
    }
    let content = fs::read_to_string(&path)
        .map_err(|err| format!("launcher settings を読み込めません: {err}"))?;
    serde_json::from_str(&content)
        .map_err(|err| format!("launcher settings の形式が不正です: {err}"))
}

pub(crate) fn save_launcher_settings(
    app: &AppHandle,
    settings: &LauncherSettings,
) -> Result<(), String> {
    let path = launcher_settings_path(app)?;
    let content = serde_json::to_string_pretty(settings)
        .map_err(|err| format!("launcher settings をJSON化できません: {err}"))?;
    fs::write(&path, format!("{content}\n"))
        .map_err(|err| format!("launcher settings を保存できません: {err}"))
}

const CURRENT_MATCH_HISTORY_SCHEMA_VERSION: u32 = 2;

#[derive(serde::Deserialize)]
struct MatchHistoryDocumentHeader {
    schema_version: u32,
}

#[derive(serde::Deserialize)]
struct MatchHistoryDocumentV1 {
    matches: Vec<MatchHistoryRecord>,
}

#[derive(serde::Deserialize, serde::Serialize)]
struct MatchHistoryDocumentV2 {
    schema_version: u32,
    matches: Vec<MatchHistoryRecord>,
}

pub(crate) fn load_match_history_document_content(
    content: &str,
) -> Result<(Vec<MatchHistoryRecord>, bool), String> {
    let header: MatchHistoryDocumentHeader = serde_json::from_str(content)
        .map_err(|err| format!("match history の形式が不正です: {err}"))?;
    match header.schema_version {
        1 => {
            let document: MatchHistoryDocumentV1 = serde_json::from_str(content)
                .map_err(|err| format!("match history v1 の形式が不正です: {err}"))?;
            let mut matches = document.matches;
            migrate_match_history_v1_to_v2(&mut matches);
            Ok((matches, true))
        }
        CURRENT_MATCH_HISTORY_SCHEMA_VERSION => {
            let document: MatchHistoryDocumentV2 = serde_json::from_str(content)
                .map_err(|err| format!("match history v2 の形式が不正です: {err}"))?;
            Ok((document.matches, false))
        }
        version => Err(format!(
            "未対応のmatch history schema_versionです: {version}"
        )),
    }
}

fn migrate_match_history_v1_to_v2(matches: &mut [MatchHistoryRecord]) {
    for record in matches {
        normalize_stage_winners(&mut record.stages);
    }
}

fn normalize_stage_winners(stages: &mut [MvlStageResult]) {
    let mut mario_wins = 0;
    let mut luigi_wins = 0;
    for stage in stages {
        stage.winner = corrected_stage_winner(stage.winner, stage.mario.dead, stage.luigi.dead);
        match stage.winner {
            Some(0) if stage.resolved => mario_wins += 1,
            Some(1) if stage.resolved => luigi_wins += 1,
            _ => {}
        }
        stage.mario_match_wins = mario_wins;
        stage.luigi_match_wins = luigi_wins;
    }
}

fn corrected_stage_winner(
    logged_winner: Option<u8>,
    mario_dead: bool,
    luigi_dead: bool,
) -> Option<u8> {
    match (mario_dead, luigi_dead) {
        (true, false) => Some(1),
        (false, true) => Some(0),
        _ => logged_winner,
    }
}

pub(crate) fn app_data_dir(app: &AppHandle) -> Result<PathBuf, String> {
    let path = app
        .path()
        .data_dir()
        .map_err(|err| format!("アプリデータディレクトリを解決できません: {err}"))?;
    let path = path.join(crate::config::app_data_dir_name());
    fs::create_dir_all(&path)
        .map_err(|err| format!("アプリデータディレクトリを作成できません: {err}"))?;
    Ok(path)
}

pub(crate) fn migrate_legacy_insiders_app_data(app: &AppHandle) -> Result<(), String> {
    #[cfg(feature = "insiders-edition")]
    {
        let base = app
            .path()
            .data_dir()
            .map_err(|err| format!("アプリデータディレクトリを解決できません: {err}"))?;
        let current = base.join(crate::config::app_data_dir_name());
        migrate_legacy_app_data(
            &base.join(LEGACY_INSIDERS_APP_DATA_DIR_NAME),
            &current,
            LEGACY_INSIDERS_MIGRATION_MARKER,
        )
    }

    #[cfg(not(feature = "insiders-edition"))]
    {
        let _ = app;
        Ok(())
    }
}

#[cfg(any(feature = "insiders-edition", test))]
pub(crate) fn migrate_legacy_app_data(
    legacy: &Path,
    current: &Path,
    marker_name: &str,
) -> Result<(), String> {
    if !legacy.is_dir() || current.join(marker_name).is_file() {
        return Ok(());
    }
    if legacy == current {
        return Err("旧アプリデータと新アプリデータの保存先が同一です".to_owned());
    }

    fs::create_dir_all(current)
        .map_err(|err| format!("Bigstar Insiders の保存先を作成できません: {err}"))?;
    copy_legacy_entries(legacy, current)?;
    fs::write(
        current.join(marker_name),
        "Legacy Insiders data was imported. The source directory is kept as a backup.\n",
    )
    .map_err(|err| format!("旧アプリデータの移行完了を記録できません: {err}"))?;
    Ok(())
}

#[cfg(any(feature = "insiders-edition", test))]
fn copy_legacy_entries(source: &Path, destination: &Path) -> Result<(), String> {
    for entry in
        fs::read_dir(source).map_err(|err| format!("旧アプリデータを読み取れません: {err}"))?
    {
        let entry = entry.map_err(|err| format!("旧アプリデータを読み取れません: {err}"))?;
        let source_path = entry.path();
        let destination_path = destination.join(entry.file_name());
        let file_type = entry
            .file_type()
            .map_err(|err| format!("旧アプリデータの種類を判定できません: {err}"))?;

        if file_type.is_symlink() {
            return Err(format!(
                "旧アプリデータ内のシンボリックリンクは移行できません: {}",
                source_path.display()
            ));
        }
        if file_type.is_dir() {
            fs::create_dir_all(&destination_path).map_err(|err| {
                format!(
                    "移行先ディレクトリを作成できません ({}): {err}",
                    destination_path.display()
                )
            })?;
            copy_legacy_entries(&source_path, &destination_path)?;
            continue;
        }
        if file_type.is_file() && !destination_path.exists() {
            fs::copy(&source_path, &destination_path).map_err(|err| {
                format!(
                    "旧アプリデータをコピーできません ({}): {err}",
                    source_path.display()
                )
            })?;
        }
    }
    Ok(())
}

pub(crate) fn repo_root() -> Result<PathBuf, String> {
    let manifest = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    manifest
        .parent()
        .and_then(Path::parent)
        .and_then(Path::parent)
        .map(Path::to_path_buf)
        .ok_or_else(|| "repo root を解決できません".to_owned())
}

pub(crate) fn allowed_log_dir(logs_root: &Path, selected: &Path) -> Result<PathBuf, String> {
    let root = logs_root
        .canonicalize()
        .map_err(|err| format!("ログ保存先を解決できません: {err}"))?;
    let selected = selected
        .canonicalize()
        .map_err(|err| format!("ログフォルダを解決できません: {err}"))?;
    if !selected.is_dir() || !selected.starts_with(&root) {
        return Err("アプリのログフォルダだけを開けます".to_owned());
    }
    Ok(selected)
}

pub(crate) fn open_allowed_log_dir(app: AppHandle, path: String) -> Result<(), String> {
    let logs_root = app_data_dir(&app)?.join("logs");
    fs::create_dir_all(&logs_root).map_err(|err| format!("ログ保存先を作成できません: {err}"))?;
    let selected = PathBuf::from(path.trim());
    let selected = allowed_log_dir(&logs_root, &selected)?;

    #[cfg(windows)]
    let mut command = Command::new("explorer.exe");
    #[cfg(target_os = "macos")]
    let mut command = Command::new("open");
    #[cfg(all(unix, not(target_os = "macos")))]
    let mut command = Command::new("xdg-open");

    command.arg(selected);
    hide_child_console_window(&mut command);
    command
        .spawn()
        .map_err(|err| format!("ログフォルダを開けません: {err}"))?;
    Ok(())
}
