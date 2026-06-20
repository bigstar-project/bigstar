use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicU32, Ordering};
use tauri::{AppHandle, Manager};

use crate::models::LauncherSettings;
use crate::processes::hide_child_console_window;

pub(crate) fn create_log_dir(app: &AppHandle) -> Result<PathBuf, String> {
    let root = app_data_dir(app)?;
    let logs_root = root.join("logs");
    fs::create_dir_all(&logs_root).map_err(|err| format!("log dir を作成できません: {err}"))?;

    for _ in 0..16 {
        let stamp = chrono_like_stamp();
        let counter = LOG_DIR_COUNTER.fetch_add(1, Ordering::Relaxed);
        let log_dir = logs_root.join(format!(
            "nsmb-mvl-gui-{stamp}-{}-{counter}",
            std::process::id()
        ));
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
            PathBuf::from("tools/nsmb-net-bridge/target/release/nsmb-net-bridge.exe"),
            PathBuf::from("tools/nsmb-net-bridge/target/release/nsmb-net-bridge"),
            PathBuf::from("tools/nsmb-net-bridge/target/debug/nsmb-net-bridge.exe"),
            PathBuf::from("tools/nsmb-net-bridge/target/debug/nsmb-net-bridge"),
        ],
        "nsmb-net-bridge",
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

pub(crate) fn fixed_generated_rom_paths(app: &AppHandle) -> Result<(PathBuf, PathBuf), String> {
    let rom_dir = app_data_dir(app)?.join("roms");
    fs::create_dir_all(&rom_dir).map_err(|err| format!("ROM保存先を作成できません: {err}"))?;
    Ok((
        rom_dir.join("nsmb-mvl-host.nds"),
        rom_dir.join("nsmb-mvl-client.nds"),
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

pub(crate) fn app_data_dir(app: &AppHandle) -> Result<PathBuf, String> {
    let path = app
        .path()
        .app_data_dir()
        .map_err(|err| format!("アプリデータディレクトリを解決できません: {err}"))?;
    fs::create_dir_all(&path)
        .map_err(|err| format!("アプリデータディレクトリを作成できません: {err}"))?;
    Ok(path)
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
