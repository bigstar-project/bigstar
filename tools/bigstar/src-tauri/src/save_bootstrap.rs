use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::{Arc, Condvar, Mutex, OnceLock};
use std::thread;
use std::time::{Duration, Instant};

use serde::Deserialize;
use tauri::AppHandle;
use uuid::Uuid;

use crate::paths::{app_data_dir, find_melonds_binary};
use crate::roms::{sha256_bytes, sha256_file};

pub(crate) const KNOWN_NSMB_US_ROM_SHA256: &str =
    "9f67fef1b4c73e966767f6153431ada3751dc1b0da2c70f386c14a5e3017f354";
pub(crate) const CANONICAL_NSMB_US_SAVE_SHA256: &str =
    "42ffb80e234c01d5784bdc291fee41c26e59f66295d7f105c798ba8dde11b2ee";
pub(crate) const NSMB_SAVE_SIZE: usize = 8_192;

const MELONDS_TIMEOUT: Duration = Duration::from_secs(30);
const OUTER_TIMEOUT: Duration = Duration::from_secs(35);
const GRACEFUL_CANCEL_TIMEOUT: Duration = Duration::from_secs(5);

#[derive(Debug, Deserialize)]
struct BootstrapResult {
    success: bool,
    rom_sha256: String,
    save_path: String,
    save_size: u64,
    save_sha256: String,
    error: String,
}

#[derive(Clone)]
pub(crate) struct CanonicalSave {
    pub(crate) bytes: Vec<u8>,
    pub(crate) sha256: String,
}

pub(crate) fn ensure_canonical_save(
    app: &AppHandle,
    source_rom: &Path,
    source_rom_sha256: &str,
) -> Result<CanonicalSave, String> {
    validate_supported_rom(source_rom, source_rom_sha256)?;
    let (flight, leader) = {
        let mut flights = canonical_flights()
            .lock()
            .map_err(|_| "基準セーブ初期化の排他状態が壊れています".to_owned())?;
        if let Some(flight) = flights.get(source_rom_sha256) {
            (Arc::clone(flight), false)
        } else {
            let flight = Arc::new(CanonicalFlight::default());
            flights.insert(source_rom_sha256.to_owned(), Arc::clone(&flight));
            (flight, true)
        }
    };
    if !leader {
        let mut result = flight
            .result
            .lock()
            .map_err(|_| "基準セーブ初期化の待機状態が壊れています".to_owned())?;
        while result.is_none() {
            result = flight
                .ready
                .wait(result)
                .map_err(|_| "基準セーブ初期化の待機に失敗しました".to_owned())?;
        }
        return result.clone().expect("single-flight result must be set");
    }

    let result = ensure_canonical_save_inner(app, source_rom, source_rom_sha256);
    if let Ok(mut shared) = flight.result.lock() {
        *shared = Some(result.clone());
        flight.ready.notify_all();
    }
    if let Ok(mut flights) = canonical_flights().lock() {
        if flights
            .get(source_rom_sha256)
            .is_some_and(|current| Arc::ptr_eq(current, &flight))
        {
            flights.remove(source_rom_sha256);
        }
    }
    result
}

#[derive(Default)]
struct CanonicalFlight {
    result: Mutex<Option<Result<CanonicalSave, String>>>,
    ready: Condvar,
}

fn canonical_flights() -> &'static Mutex<HashMap<String, Arc<CanonicalFlight>>> {
    static FLIGHTS: OnceLock<Mutex<HashMap<String, Arc<CanonicalFlight>>>> = OnceLock::new();
    FLIGHTS.get_or_init(|| Mutex::new(HashMap::new()))
}

fn ensure_canonical_save_inner(
    app: &AppHandle,
    source_rom: &Path,
    source_rom_sha256: &str,
) -> Result<CanonicalSave, String> {
    let canonical = canonical_save_path(app, source_rom_sha256)?;
    let root = canonical
        .parent()
        .ok_or_else(|| "基準セーブ保存先を解決できません".to_owned())?
        .to_path_buf();
    fs::create_dir_all(&root).map_err(|err| format!("基準セーブ保存先を作成できません: {err}"))?;

    if canonical.is_file() {
        match read_and_validate_canonical(&canonical, source_rom_sha256) {
            Ok(save) => return Ok(save),
            Err(_) => {
                let invalid =
                    root.join(format!(".{}.invalid-{}", source_rom_sha256, Uuid::new_v4()));
                fs::rename(&canonical, &invalid).map_err(|err| {
                    format!(
                        "不正な基準セーブを隔離できません {}: {err}",
                        canonical.display()
                    )
                })?;
                let _ = fs::remove_file(invalid);
            }
        }
    }

    let work_root = app_data_dir(app)?.join("save-bootstrap");
    fs::create_dir_all(&work_root)
        .map_err(|err| format!("セーブ初期化用ディレクトリを作成できません: {err}"))?;
    let work = work_root.join(Uuid::new_v4().to_string());
    fs::create_dir(&work)
        .map_err(|err| format!("セーブ初期化用の隔離領域を作成できません: {err}"))?;

    let result = generate_canonical_save(app, source_rom, source_rom_sha256, &work)
        .and_then(|generated| promote_canonical(&generated, &canonical, source_rom_sha256));
    match result {
        Ok(save) => {
            let _ = fs::remove_dir_all(&work);
            Ok(save)
        }
        Err(err) => {
            for file in ["bootstrap.nds", "bootstrap.sav", "cancel.request"] {
                let _ = fs::remove_file(work.join(file));
            }
            let _ = fs::remove_dir_all(work.join("config"));
            Err(format!("{err} (診断ログ: {})", work.display()))
        }
    }
}

pub(crate) fn canonical_save_path(
    app: &AppHandle,
    source_rom_sha256: &str,
) -> Result<PathBuf, String> {
    Ok(app_data_dir(app)?
        .join("canonical-saves")
        .join(format!("{source_rom_sha256}.sav")))
}

fn validate_supported_rom(source_rom: &Path, source_rom_sha256: &str) -> Result<(), String> {
    if source_rom_sha256 != KNOWN_NSMB_US_ROM_SHA256 {
        return Err(format!(
            "対応していないROMです: {} (SHA-256 {source_rom_sha256})",
            source_rom.display()
        ));
    }
    Ok(())
}

fn generate_canonical_save(
    app: &AppHandle,
    source_rom: &Path,
    source_rom_sha256: &str,
    work: &Path,
) -> Result<PathBuf, String> {
    let bootstrap_rom = work.join("bootstrap.nds");
    let bootstrap_save = work.join("bootstrap.sav");
    let result_path = work.join("result.json");
    let cancel_path = work.join("cancel.request");
    let config_dir = work.join("config");
    let stdout_path = work.join("melonDS.stdout.log");
    let stderr_path = work.join("melonDS.stderr.log");
    fs::create_dir(&config_dir)
        .map_err(|err| format!("melonDSの隔離設定領域を作成できません: {err}"))?;
    fs::copy(source_rom, &bootstrap_rom)
        .map_err(|err| format!("実測用ROMコピーを作成できません: {err}"))?;
    if sha256_file(&bootstrap_rom)? != source_rom_sha256 {
        return Err("実測用ROMコピーのhashが元ROMと一致しません".to_owned());
    }

    let melon = find_melonds_binary(app)?;
    let stdout = fs::File::create(&stdout_path)
        .map_err(|err| format!("bootstrap標準出力ログを作成できません: {err}"))?;
    let stderr = fs::File::create(&stderr_path)
        .map_err(|err| format!("bootstrap標準エラーログを作成できません: {err}"))?;
    let mut command = Command::new(&melon);
    command
        .arg(&bootstrap_rom)
        .current_dir(melon.parent().unwrap_or(work))
        .env("MELONDS_SAVE_BOOTSTRAP_RESULT", &result_path)
        .env("MELONDS_SAVE_BOOTSTRAP_CANCEL", &cancel_path)
        .env("MELONDS_SAVE_BOOTSTRAP_CONFIG_DIR", &config_dir)
        .env("MELONDS_SAVE_BOOTSTRAP_ROM_SHA256", source_rom_sha256)
        .env(
            "MELONDS_SAVE_BOOTSTRAP_TIMEOUT_MS",
            MELONDS_TIMEOUT.as_millis().to_string(),
        )
        .stdin(Stdio::null())
        .stdout(Stdio::from(stdout))
        .stderr(Stdio::from(stderr));
    crate::processes::remove_inherited_melonds_env_keys(
        &mut command,
        std::env::vars_os().map(|(key, _)| key),
    );
    hide_child_console_window(&mut command);
    let mut child = command
        .spawn()
        .map_err(|err| format!("基準セーブ生成用melonDSを起動できません: {err}"))?;

    let started = Instant::now();
    let status = 'wait_for_process: loop {
        if let Some(status) = child
            .try_wait()
            .map_err(|err| format!("基準セーブ生成用melonDSの状態を確認できません: {err}"))?
        {
            break status;
        }
        if started.elapsed() >= OUTER_TIMEOUT {
            fs::write(&cancel_path, b"cancel\n")
                .map_err(|err| format!("melonDSへ正常終了を要求できません: {err}"))?;
            let cancel_started = Instant::now();
            loop {
                if let Some(status) = child
                    .try_wait()
                    .map_err(|err| format!("終了要求後のmelonDS状態を確認できません: {err}"))?
                {
                    break 'wait_for_process status;
                }
                if cancel_started.elapsed() >= GRACEFUL_CANCEL_TIMEOUT {
                    child
                        .kill()
                        .map_err(|err| format!("応答しないmelonDSを終了できません: {err}"))?;
                    let _ = child.wait();
                    return Err("基準セーブの初期化がタイムアウトしました".to_owned());
                }
                thread::sleep(Duration::from_millis(50));
            }
        } else {
            thread::sleep(Duration::from_millis(50));
        }
    };

    if !status.success() {
        return Err(format!(
            "基準セーブ生成用melonDSが異常終了しました: {status}"
        ));
    }
    let result_bytes = fs::read(&result_path)
        .map_err(|err| format!("melonDSのbootstrap結果を読み込めません: {err}"))?;
    let result: BootstrapResult = serde_json::from_slice(&result_bytes)
        .map_err(|err| format!("melonDSのbootstrap結果が不正です: {err}"))?;
    if !result.success {
        return Err(format!("基準セーブを初期化できません: {}", result.error));
    }
    if result.rom_sha256 != source_rom_sha256 {
        return Err("melonDSが報告したROM hashが一致しません".to_owned());
    }
    let reported_save = PathBuf::from(&result.save_path)
        .canonicalize()
        .map_err(|err| format!("melonDSが報告したsave pathを解決できません: {err}"))?;
    let expected_save = bootstrap_save
        .canonicalize()
        .map_err(|err| format!("生成saveを解決できません: {err}"))?;
    if reported_save != expected_save {
        return Err("melonDSが隔離領域外のsaveを報告しました".to_owned());
    }
    let verified = read_and_validate_canonical(&expected_save, source_rom_sha256)?;
    if result.save_size != verified.bytes.len() as u64 || result.save_sha256 != verified.sha256 {
        return Err("melonDSのbootstrap結果とBigstarの再検証結果が一致しません".to_owned());
    }
    Ok(expected_save)
}

fn promote_canonical(
    generated: &Path,
    canonical: &Path,
    source_rom_sha256: &str,
) -> Result<CanonicalSave, String> {
    if canonical.exists() {
        return read_and_validate_canonical(canonical, source_rom_sha256);
    }
    fs::rename(generated, canonical)
        .map_err(|err| format!("生成saveを基準セーブへ昇格できません: {err}"))?;
    read_and_validate_canonical(canonical, source_rom_sha256)
}

pub(crate) fn read_and_validate_canonical(
    path: &Path,
    source_rom_sha256: &str,
) -> Result<CanonicalSave, String> {
    let bytes = fs::read(path)
        .map_err(|err| format!("基準セーブ {} を読み込めません: {err}", path.display()))?;
    validate_save_bytes(&bytes, source_rom_sha256)?;
    Ok(CanonicalSave {
        sha256: sha256_bytes(&bytes),
        bytes,
    })
}

pub(crate) fn validate_save_bytes(bytes: &[u8], source_rom_sha256: &str) -> Result<(), String> {
    if source_rom_sha256 != KNOWN_NSMB_US_ROM_SHA256 {
        return Err("対応していないROMのセーブは検証できません".to_owned());
    }
    if bytes.len() != NSMB_SAVE_SIZE {
        return Err(format!(
            "基準セーブのサイズが不正です: {} bytes (期待値 {NSMB_SAVE_SIZE} bytes)",
            bytes.len()
        ));
    }
    if bytes[..0x1000] != bytes[0x1000..] {
        return Err("基準セーブの4KiB複製領域が一致しません".to_owned());
    }
    for offset in [0x2, 0x102, 0x382, 0x602, 0x882] {
        if bytes.get(offset..offset + 8) != Some(b"Mario2d\0") {
            return Err(format!("基準セーブのMario2d識別子が不正です: 0x{offset:x}"));
        }
    }
    if bytes
        .get(0x89e..0x1000)
        .is_none_or(|tail| tail.iter().any(|byte| *byte != 0xff))
    {
        return Err("基準セーブの未使用領域が不正です".to_owned());
    }
    let hash = sha256_bytes(bytes);
    if hash != CANONICAL_NSMB_US_SAVE_SHA256 {
        return Err(format!("基準セーブのSHA-256が不正です: {hash}"));
    }
    Ok(())
}

#[cfg(windows)]
fn hide_child_console_window(command: &mut Command) {
    use std::os::windows::process::CommandExt;
    command.creation_flags(0x0800_0000);
}

#[cfg(not(windows))]
fn hide_child_console_window(_command: &mut Command) {}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn known_save_rejects_wrong_hash_after_structural_validation() {
        let mut bytes = vec![0xff; NSMB_SAVE_SIZE];
        for half in [0, 0x1000] {
            for offset in [0x2, 0x102, 0x382, 0x602, 0x882] {
                bytes[half + offset..half + offset + 8].copy_from_slice(b"Mario2d\0");
            }
        }
        assert!(validate_save_bytes(&bytes, KNOWN_NSMB_US_ROM_SHA256)
            .unwrap_err()
            .contains("SHA-256"));
    }

    #[test]
    fn known_save_rejects_nonduplicated_halves() {
        let mut bytes = vec![0xff; NSMB_SAVE_SIZE];
        bytes[0] = 0;
        assert!(validate_save_bytes(&bytes, KNOWN_NSMB_US_ROM_SHA256)
            .unwrap_err()
            .contains("4KiB"));
    }
}
