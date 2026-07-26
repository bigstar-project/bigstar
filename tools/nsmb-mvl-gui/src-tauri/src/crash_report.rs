use std::fs::{self, File};
use std::io::{self, Read, Write};
use std::path::{Path, PathBuf};

#[cfg(feature = "insiders-edition")]
use crate::models::MatchPlayerNames;
#[cfg(any(feature = "insiders-edition", test))]
use crate::models::MvlStageResult;

#[cfg(any(feature = "insiders-edition", test))]
const DIAGNOSTIC_EVENT_LOG: &str = "melonds-events.jsonl";
#[cfg(feature = "insiders-edition")]
const INSIDERS_REPORT_URL: Option<&str> = option_env!("NSMB_MVL_INSIDERS_REPORT_URL");
#[cfg(feature = "insiders-edition")]
const REPORT_STATUS_FILE: &str = "insiders-session-report.txt";
const USER_LOG_ARCHIVE_PREFIX: &str = "nsmb-mvl-logs";

#[cfg(any(feature = "insiders-edition", test))]
pub(crate) fn match_result_decided(results: &[MvlStageResult]) -> bool {
    results.iter().any(|result| {
        result.resolved
            && result.target_wins > 0
            && (result.mario_match_wins >= result.target_wins
                || result.luigi_match_wins >= result.target_wins)
    })
}

#[cfg(feature = "insiders-edition")]
pub(crate) fn send_crash_report_async(
    log_dir: PathBuf,
    melon_state: String,
    bridge_state: String,
    player_names: Option<MatchPlayerNames>,
    reason: &'static str,
) {
    std::thread::spawn(move || {
        let result = send_crash_report(
            &log_dir,
            &melon_state,
            &bridge_state,
            player_names.as_ref(),
            reason,
        );
        let message = match result {
            Ok(()) => "Insiders unresolved session report sent".to_owned(),
            Err(err) => format!("Insiders unresolved session report failed: {err}"),
        };
        let _ = fs::write(log_dir.join(REPORT_STATUS_FILE), message);
    });
}

#[cfg(feature = "insiders-edition")]
fn send_crash_report(
    log_dir: &Path,
    melon_state: &str,
    bridge_state: &str,
    player_names: Option<&MatchPlayerNames>,
    reason: &str,
) -> Result<(), String> {
    let report_url = INSIDERS_REPORT_URL
        .filter(|url| !url.trim().is_empty())
        .ok_or_else(|| "Insiders report endpoint が設定されていません".to_owned())?;
    let archive_path = create_log_archive(log_dir)?;
    let archive_name = archive_path
        .file_name()
        .and_then(|name| name.to_str())
        .unwrap_or("nsmb-mvl-crash-logs.zip")
        .to_owned();
    let archive =
        fs::read(&archive_path).map_err(|err| format!("crash log archive を読めません: {err}"))?;

    let payload = serde_json::json!({
        "content": format!(
            "NSMB MvL unresolved session report: reason={reason} melonDS={melon_state} bridge={bridge_state}\nplayers: Mario={} Luigi={}\nlog_dir={}",
            player_names.map(|names| names.mario.as_str()).unwrap_or("-"),
            player_names.map(|names| names.luigi.as_str()).unwrap_or("-"),
            log_dir.display()
        )
    });
    let file_part = reqwest::blocking::multipart::Part::bytes(archive)
        .file_name(archive_name)
        .mime_str("application/zip")
        .map_err(|err| format!("Discord 添付の MIME 設定に失敗しました: {err}"))?;
    let form = reqwest::blocking::multipart::Form::new()
        .text("payload_json", payload.to_string())
        .part("files[0]", file_part);
    let response = reqwest::blocking::Client::new()
        .post(report_url)
        .multipart(form)
        .send()
        .map_err(|err| format!("Insiders report 送信に失敗しました: {err}"))?;
    if !response.status().is_success() {
        return Err(format!(
            "Insiders report が失敗しました status={}",
            response.status()
        ));
    }

    let _ = fs::remove_file(archive_path);
    Ok(())
}

#[cfg(any(feature = "insiders-edition", test))]
pub(crate) fn create_log_archive(log_dir: &Path) -> Result<PathBuf, String> {
    let archive_path = std::env::temp_dir().join(format!(
        "nsmb-mvl-crash-logs-{}-{}.zip",
        std::process::id(),
        sanitize_file_name(
            log_dir
                .file_name()
                .and_then(|name| name.to_str())
                .unwrap_or("logs")
        )
    ));
    create_log_archive_at(log_dir, &archive_path, ArchiveMode::Crash)
}

pub(crate) fn create_user_log_archive(log_dir: &Path) -> Result<PathBuf, String> {
    let archive_path = log_dir.join(format!(
        "{USER_LOG_ARCHIVE_PREFIX}-{}-{}.zip",
        unix_timestamp_seconds(),
        sanitize_file_name(
            log_dir
                .file_name()
                .and_then(|name| name.to_str())
                .unwrap_or("logs")
        )
    ));
    create_log_archive_at(log_dir, &archive_path, ArchiveMode::User)
}

fn create_log_archive_at(
    log_dir: &Path,
    archive_path: &Path,
    mode: ArchiveMode,
) -> Result<PathBuf, String> {
    let file =
        File::create(archive_path).map_err(|err| format!("log archive を作成できません: {err}"))?;
    let mut zip = zip::ZipWriter::new(file);
    add_dir_to_zip(&mut zip, log_dir, log_dir, archive_path, mode)
        .map_err(|err| format!("log archive にログを追加できません: {err}"))?;
    zip.finish()
        .map_err(|err| format!("log archive を完了できません: {err}"))?;
    Ok(archive_path.to_path_buf())
}

#[derive(Clone, Copy)]
enum ArchiveMode {
    #[cfg(any(feature = "insiders-edition", test))]
    Crash,
    User,
}

fn add_dir_to_zip<W: Write + io::Seek>(
    zip: &mut zip::ZipWriter<W>,
    root: &Path,
    dir: &Path,
    archive_path: &Path,
    mode: ArchiveMode,
) -> io::Result<()> {
    let options = zip::write::SimpleFileOptions::default()
        .compression_method(zip::CompressionMethod::Deflated);
    for entry in fs::read_dir(dir)? {
        let entry = entry?;
        let path = entry.path();
        let metadata = entry.metadata()?;
        if metadata.is_dir() {
            add_dir_to_zip(zip, root, &path, archive_path, mode)?;
            continue;
        }
        if !metadata.is_file() || should_exclude_log_file(&path, archive_path, mode) {
            continue;
        }
        let relative = path.strip_prefix(root).unwrap_or(&path);
        let archive_name = relative.to_string_lossy().replace('\\', "/");
        zip.start_file(archive_name, options)?;
        let mut file = File::open(&path)?;
        let mut buffer = [0_u8; 16 * 1024];
        loop {
            let read = file.read(&mut buffer)?;
            if read == 0 {
                break;
            }
            zip.write_all(&buffer[..read])?;
        }
    }
    Ok(())
}

fn should_exclude_log_file(path: &Path, archive_path: &Path, _mode: ArchiveMode) -> bool {
    if same_path(path, archive_path) {
        return true;
    }
    let file_name = path.file_name().and_then(|name| name.to_str());
    #[cfg(any(feature = "insiders-edition", test))]
    {
        if matches!(_mode, ArchiveMode::Crash)
            && file_name.is_some_and(|name| name.eq_ignore_ascii_case(DIAGNOSTIC_EVENT_LOG))
        {
            return true;
        }
    }
    file_name.is_some_and(|name| {
        name.starts_with(USER_LOG_ARCHIVE_PREFIX) && name.to_ascii_lowercase().ends_with(".zip")
    })
}

fn same_path(left: &Path, right: &Path) -> bool {
    match (left.canonicalize(), right.canonicalize()) {
        (Ok(left), Ok(right)) => left == right,
        _ => left == right,
    }
}

fn unix_timestamp_seconds() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs()
}

fn sanitize_file_name(value: &str) -> String {
    value
        .chars()
        .map(|ch| {
            if ch.is_ascii_alphanumeric() || matches!(ch, '-' | '_') {
                ch
            } else {
                '_'
            }
        })
        .collect()
}
