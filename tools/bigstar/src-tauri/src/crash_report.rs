use std::fs::{self, File};
use std::io::{BufRead, BufReader, Read, Write};
use std::path::{Path, PathBuf};

use regex::Regex;
use sha2::{Digest, Sha256};

#[cfg(feature = "insiders-edition")]
use crate::models::MatchPlayerNames;
#[cfg(any(feature = "insiders-edition", test))]
use crate::models::MvlStageResult;
use crate::{
    config::{app_edition, app_version, build_profile},
    diagnostics::sanitize_text,
};

#[cfg(feature = "insiders-edition")]
const INSIDERS_REPORT_URL: Option<&str> = option_env!("BIGSTAR_INSIDERS_REPORT_URL");
#[cfg(feature = "insiders-edition")]
const REPORT_STATUS_FILE: &str = "insiders-session-report.txt";
const USER_LOG_ARCHIVE_PREFIX: &str = "bigstar-feedback";
const PROCESS_TAIL_BYTES: usize = 512 * 1024;
const APP_ERROR_TAIL_BYTES: usize = 1024 * 1024;
const PERFORMANCE_MAX_BYTES: usize = 8 * 1024 * 1024;
const INSIDERS_SCREENSHOT_MAX_BYTES: u64 = 8 * 1024 * 1024;
const INSIDERS_SCREENSHOT_MAX_FILES: usize = 16;
const INSIDERS_DETAILED_TEXT_FILES: &[(&str, usize)] = &[
    ("melonds-events.jsonl", 12 * 1024 * 1024),
    ("bridge-events.jsonl", 8 * 1024 * 1024),
    ("melonds-game-state.csv", 4 * 1024 * 1024),
    ("melonds-phase-events.jsonl", 4 * 1024 * 1024),
    ("melonds-watchdog.jsonl", 2 * 1024 * 1024),
];

pub(crate) struct FeedbackArchive {
    pub(crate) path: PathBuf,
    pub(crate) included_files: Vec<String>,
}

pub(crate) struct FeedbackArchiveOptions<'a> {
    pub(crate) category: &'a str,
    pub(crate) include_performance: bool,
    pub(crate) include_detailed_diagnostics: bool,
    pub(crate) app_error_files: &'a [PathBuf],
    pub(crate) app_context_file: Option<&'a Path>,
}

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
    _player_names: Option<MatchPlayerNames>,
    reason: &'static str,
) {
    std::thread::spawn(move || {
        let result = send_crash_report(&log_dir, &melon_state, &bridge_state, reason);
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
    reason: &str,
) -> Result<(), String> {
    let report_url = INSIDERS_REPORT_URL
        .filter(|url| !url.trim().is_empty())
        .ok_or_else(|| "Insiders report endpoint が設定されていません".to_owned())?;
    let archive_path = create_log_archive(log_dir)?;
    let archive_name = archive_path
        .file_name()
        .and_then(|name| name.to_str())
        .unwrap_or("bigstar-feedback.zip")
        .to_owned();
    let archive =
        fs::read(&archive_path).map_err(|err| format!("crash log archive を読めません: {err}"))?;

    let payload = serde_json::json!({
        "content": format!(
            "Bigstar unresolved session report: reason={} melonDS={} bridge={}",
            sanitize_text(reason, 256),
            sanitize_text(melon_state, 256),
            sanitize_text(bridge_state, 256),
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
        "bigstar-feedback-{}-{}.zip",
        std::process::id(),
        sanitize_file_name(
            log_dir
                .file_name()
                .and_then(|name| name.to_str())
                .unwrap_or("logs")
        )
    ));
    create_feedback_archive_at(
        log_dir,
        &archive_path,
        &FeedbackArchiveOptions {
            category: "crash",
            include_performance: true,
            include_detailed_diagnostics: false,
            app_error_files: &[],
            app_context_file: None,
        },
    )
    .map(|archive| archive.path)
}

#[cfg(test)]
pub(crate) fn create_user_log_archive(log_dir: &Path) -> Result<PathBuf, String> {
    create_feedback_archive(
        log_dir,
        &FeedbackArchiveOptions {
            category: "other",
            include_performance: true,
            include_detailed_diagnostics: false,
            app_error_files: &[],
            app_context_file: None,
        },
    )
    .map(|archive| archive.path)
}

#[cfg(test)]
pub(crate) fn create_user_log_archive_with_diagnostics(log_dir: &Path) -> Result<PathBuf, String> {
    create_feedback_archive(
        log_dir,
        &FeedbackArchiveOptions {
            category: "other",
            include_performance: false,
            include_detailed_diagnostics: true,
            app_error_files: &[],
            app_context_file: None,
        },
    )
    .map(|archive| archive.path)
}

pub(crate) fn create_feedback_archive(
    log_dir: &Path,
    options: &FeedbackArchiveOptions<'_>,
) -> Result<FeedbackArchive, String> {
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
    create_feedback_archive_at(log_dir, &archive_path, options)
}

fn create_feedback_archive_at(
    log_dir: &Path,
    archive_path: &Path,
    options: &FeedbackArchiveOptions<'_>,
) -> Result<FeedbackArchive, String> {
    let file = File::create(archive_path)
        .map_err(|err| format!("feedback archive を作成できません: {err}"))?;
    let mut zip = zip::ZipWriter::new(file);
    let zip_options = zip::write::SimpleFileOptions::default()
        .compression_method(zip::CompressionMethod::Deflated);
    let mut included_files = Vec::new();
    let sensitive_values = feedback_sensitive_values(log_dir);

    let summary = feedback_summary(
        log_dir,
        options.category,
        options.app_context_file,
        &sensitive_values,
    );
    let summary_bytes = serde_json::to_vec_pretty(&summary)
        .map_err(|err| format!("feedback summary を生成できません: {err}"))?;
    add_bytes(
        &mut zip,
        zip_options,
        "feedback-summary.json",
        &summary_bytes,
        &mut included_files,
    )?;

    add_sanitized_text_file(
        &mut zip,
        zip_options,
        &log_dir.join("session-events.jsonl"),
        "session-events.jsonl",
        256 * 1024,
        &sensitive_values,
        &mut included_files,
    )?;

    if options.include_performance {
        add_performance_log(
            &mut zip,
            zip_options,
            &log_dir.join("melonds-performance.jsonl"),
            &mut included_files,
        )?;
    }

    if options.include_detailed_diagnostics {
        add_detailed_diagnostics(
            &mut zip,
            zip_options,
            log_dir,
            &sensitive_values,
            &mut included_files,
        )?;
    }

    for process in ["bridge", "melonds"] {
        for stream in ["stdout", "stderr"] {
            let source = log_dir.join(format!("{process}.{stream}.txt"));
            let archive_name = format!("{process}.{stream}.tail.txt");
            add_process_tail(
                &mut zip,
                zip_options,
                &source,
                &archive_name,
                &sensitive_values,
                &mut included_files,
            )?;
        }
    }

    add_app_errors(
        &mut zip,
        zip_options,
        options.app_error_files,
        &sensitive_values,
        &mut included_files,
    )?;

    zip.finish()
        .map_err(|err| format!("feedback archive を完了できません: {err}"))?;
    Ok(FeedbackArchive {
        path: archive_path.to_path_buf(),
        included_files,
    })
}

fn feedback_summary(
    log_dir: &Path,
    category: &str,
    app_context_file: Option<&Path>,
    sensitive_values: &[String],
) -> serde_json::Value {
    let launcher = read_json(log_dir.join("launcher.json"));
    let bridge = read_json(log_dir.join("bridge-status.json"));
    let melon = read_json(log_dir.join("melonds-diagnostics.json"));
    let runtime_identity = read_json(log_dir.join("melonds-runtime-identity.json"));
    let app_context = app_context_file
        .map(|path| read_json(path.to_path_buf()))
        .unwrap_or_default();
    let request = launcher.get("request").cloned().unwrap_or_default();
    let settings = request.get("settings").cloned().unwrap_or_default();
    let rom_identity = request.get("rom_identity").cloned().unwrap_or_default();
    let selected_pair = bridge
        .get("selected_candidate_pair")
        .cloned()
        .unwrap_or_default();
    let mismatch = melon
        .get("game_state_mismatch")
        .cloned()
        .unwrap_or_default();
    let performance = performance_summary(&log_dir.join("melonds-performance.jsonl"));
    let lifecycle = session_event_summary(&log_dir.join("session-events.jsonl"));
    let artifacts = launcher.get("artifacts").cloned().unwrap_or_default();
    let melon_path = launcher
        .pointer("/paths/melonds")
        .and_then(serde_json::Value::as_str)
        .map(PathBuf::from);
    let rom_path = launcher
        .pointer("/paths/rom")
        .and_then(serde_json::Value::as_str)
        .map(PathBuf::from);
    let config_identity = melon_path
        .as_deref()
        .and_then(Path::parent)
        .map(|parent| file_identity(&parent.join("melonDS.toml")))
        .unwrap_or(serde_json::Value::Null);
    let save_identity = rom_path
        .map(|mut path| {
            path.set_extension("sav");
            file_identity(&path)
        })
        .unwrap_or(serde_json::Value::Null);

    serde_json::json!({
        "schema_version": 2,
        "category": category,
        "app": {
            "version": app_version(),
            "edition": app_edition(),
            "build_profile": build_profile(),
        },
        "runtime": {
            "os": std::env::consts::OS,
            "arch": std::env::consts::ARCH,
            "family": std::env::consts::FAMILY,
            "client": app_context,
        },
        "session": {
            "role": request.get("role"),
            "lifecycle": lifecycle,
            "settings": {
                "course_mode": settings.get("course_mode"),
                "course_stages": settings.get("course_stages"),
                "wins": settings.get("wins"),
                "big_stars": settings.get("big_stars"),
                "lives": settings.get("lives"),
                "input_delay_frames": settings.get("input_delay_frames"),
                "input_max_frame_lead": settings.get("input_max_frame_lead"),
                "rollback_enabled": settings.get("rollback_enabled"),
            },
            "rom": {
                "rom_pair_id": rom_identity.get("rom_pair_id"),
                "generator_id": rom_identity.get("generator_id"),
                "host_rom_sha256": rom_identity.get("host_rom_sha256"),
                "client_rom_sha256": rom_identity.get("client_rom_sha256"),
                "bridge_sha256": rom_identity.get("bridge_sha256"),
                "save_sha256": rom_identity.get("save_sha256"),
            },
        },
        "network": {
            "phase": bridge.get("phase"),
            "elapsed_seconds": bridge.get("elapsed_seconds"),
            "connection_state": bridge.get("connection_state"),
            "gathering_state": bridge.get("gathering_state"),
            "ice_state": bridge.get("ice_state"),
            "route": selected_pair.get("route"),
            "local_type": selected_pair.get("local_type"),
            "remote_type": selected_pair.get("remote_type"),
            "stats": bridge.get("stats"),
            "last_error": bridge
                .get("last_error")
                .and_then(serde_json::Value::as_str)
                .map(|value| sanitize_feedback_text(value, 4096, sensitive_values)),
        },
        "emulator": {
            "runtime_identity": runtime_identity,
            "artifacts": {
                "melonds": feedback_artifact_identity(artifacts.get("melonds")),
                "bridge": feedback_artifact_identity(artifacts.get("bridge")),
                "rom": feedback_artifact_identity(artifacts.get("rom")),
                "input_script": feedback_artifact_identity(artifacts.get("input_script")),
            },
            "config": config_identity,
            "save": save_identity,
            "game_state_mismatch": {
                "frame": mismatch.get("frame"),
                "basic_matches": mismatch.get("basic_matches"),
                "player_global_matches": mismatch.get("player_global_matches"),
                "wifi_candidate_matches": mismatch.get("wifi_candidate_matches"),
                "render_candidate_matches": mismatch.get("render_candidate_matches"),
            },
            "performance": performance,
        },
    })
}

fn session_event_summary(path: &Path) -> serde_json::Value {
    let Ok(file) = File::open(path) else {
        return serde_json::Value::Null;
    };
    let mut first_unix_ms = None;
    let mut last_unix_ms = None;
    let mut last_event = None;
    let mut melonds_exit = None;
    let mut bridge_exit = None;
    let mut event_count = 0_u64;
    for line in BufReader::new(file).lines().map_while(Result::ok) {
        let Ok(value) = serde_json::from_str::<serde_json::Value>(&line) else {
            continue;
        };
        let timestamp = value.get("unix_ms").and_then(serde_json::Value::as_u64);
        if first_unix_ms.is_none() {
            first_unix_ms = timestamp;
        }
        if timestamp.is_some() {
            last_unix_ms = timestamp;
        }
        last_event = value
            .get("event")
            .and_then(serde_json::Value::as_str)
            .map(str::to_owned);
        if value.get("event").and_then(serde_json::Value::as_str) == Some("process_exited") {
            let detail = value
                .get("detail")
                .and_then(serde_json::Value::as_str)
                .map(|detail| sanitize_text(detail, 256));
            match value.get("component").and_then(serde_json::Value::as_str) {
                Some("melonds") => melonds_exit = detail,
                Some("bridge") => bridge_exit = detail,
                _ => {}
            }
        }
        event_count += 1;
    }
    let duration_ms = first_unix_ms
        .zip(last_unix_ms)
        .map(|(first, last)| last.saturating_sub(first));
    serde_json::json!({
        "event_count": event_count,
        "duration_ms": duration_ms,
        "last_event": last_event,
        "process_exit": {
            "melonds": melonds_exit,
            "bridge": bridge_exit,
        },
    })
}

fn performance_summary(path: &Path) -> serde_json::Value {
    let Ok(file) = File::open(path) else {
        return serde_json::Value::Null;
    };
    let mut summary_records = 0_u64;
    let mut slow_frame_records = 0_u64;
    let mut worst_total_ms = 0_f64;
    let mut latest_summary = serde_json::Value::Null;
    for line in BufReader::new(file).lines().map_while(Result::ok) {
        let Ok(value) = serde_json::from_str::<serde_json::Value>(&line) else {
            continue;
        };
        match value.get("type").and_then(serde_json::Value::as_str) {
            Some("summary") => {
                summary_records += 1;
                worst_total_ms = worst_total_ms.max(
                    value
                        .pointer("/total_ms/max")
                        .and_then(serde_json::Value::as_f64)
                        .unwrap_or_default(),
                );
                latest_summary = value;
            }
            Some("slow_frame") => {
                slow_frame_records += 1;
                worst_total_ms = worst_total_ms.max(
                    value
                        .get("total_ms")
                        .and_then(serde_json::Value::as_f64)
                        .unwrap_or_default(),
                );
            }
            _ => {}
        }
    }
    serde_json::json!({
        "summary_records": summary_records,
        "slow_frame_records": slow_frame_records,
        "worst_total_ms": worst_total_ms,
        "latest_summary": latest_summary,
    })
}

fn add_performance_log<W: Write + std::io::Seek>(
    zip: &mut zip::ZipWriter<W>,
    options: zip::write::SimpleFileOptions,
    path: &Path,
    included_files: &mut Vec<String>,
) -> Result<(), String> {
    if !path.is_file() {
        return Ok(());
    }
    let file = File::open(path)
        .map_err(|err| format!("performance log を開けません {}: {err}", path.display()))?;
    let mut output = Vec::new();
    for line in BufReader::new(file).lines() {
        let line = line.map_err(|err| format!("performance log を読めません: {err}"))?;
        let mut value: serde_json::Value = match serde_json::from_str(&line) {
            Ok(value) => value,
            Err(_) => continue,
        };
        if let Some(object) = value.as_object_mut() {
            object.remove("audio_device");
        }
        let encoded = serde_json::to_vec(&value)
            .map_err(|err| format!("performance log を変換できません: {err}"))?;
        if output.len().saturating_add(encoded.len() + 1) > PERFORMANCE_MAX_BYTES {
            break;
        }
        output.extend_from_slice(&encoded);
        output.push(b'\n');
    }
    if !output.is_empty() {
        add_bytes(
            zip,
            options,
            "melonds-performance.jsonl",
            &output,
            included_files,
        )?;
    }
    Ok(())
}

fn add_process_tail<W: Write + std::io::Seek>(
    zip: &mut zip::ZipWriter<W>,
    options: zip::write::SimpleFileOptions,
    path: &Path,
    archive_name: &str,
    sensitive_values: &[String],
    included_files: &mut Vec<String>,
) -> Result<(), String> {
    let rotated = path.with_extension("txt.1");
    let mut content = fs::read(rotated).unwrap_or_default();
    content.extend_from_slice(&fs::read(path).unwrap_or_default());
    if content.is_empty() {
        return Ok(());
    }
    let start = content.len().saturating_sub(PROCESS_TAIL_BYTES);
    let text = String::from_utf8_lossy(&content[start..]);
    let sanitized = sanitize_feedback_text(&text, PROCESS_TAIL_BYTES, sensitive_values);
    add_bytes(
        zip,
        options,
        archive_name,
        sanitized.as_bytes(),
        included_files,
    )
}

fn add_app_errors<W: Write + std::io::Seek>(
    zip: &mut zip::ZipWriter<W>,
    options: zip::write::SimpleFileOptions,
    paths: &[PathBuf],
    sensitive_values: &[String],
    included_files: &mut Vec<String>,
) -> Result<(), String> {
    let mut content = Vec::new();
    for path in paths.iter().rev() {
        content.extend_from_slice(&fs::read(path).unwrap_or_default());
    }
    if content.is_empty() {
        return Ok(());
    }
    let start = content.len().saturating_sub(APP_ERROR_TAIL_BYTES);
    let text = String::from_utf8_lossy(&content[start..]);
    let sanitized = sanitize_feedback_text(&text, APP_ERROR_TAIL_BYTES, sensitive_values);
    add_bytes(
        zip,
        options,
        "app-errors.jsonl",
        sanitized.as_bytes(),
        included_files,
    )
}

fn add_sanitized_text_file<W: Write + std::io::Seek>(
    zip: &mut zip::ZipWriter<W>,
    options: zip::write::SimpleFileOptions,
    source: &Path,
    archive_name: &str,
    max_bytes: usize,
    sensitive_values: &[String],
    included_files: &mut Vec<String>,
) -> Result<(), String> {
    let content = fs::read(source).unwrap_or_default();
    if content.is_empty() {
        return Ok(());
    }
    let start = content.len().saturating_sub(max_bytes);
    let text = String::from_utf8_lossy(&content[start..]);
    let sanitized = sanitize_feedback_text(&text, max_bytes, sensitive_values);
    add_bytes(
        zip,
        options,
        archive_name,
        sanitized.as_bytes(),
        included_files,
    )
}

fn add_detailed_diagnostics<W: Write + std::io::Seek>(
    zip: &mut zip::ZipWriter<W>,
    options: zip::write::SimpleFileOptions,
    log_dir: &Path,
    sensitive_values: &[String],
    included_files: &mut Vec<String>,
) -> Result<(), String> {
    for (file_name, max_bytes) in INSIDERS_DETAILED_TEXT_FILES {
        add_sanitized_text_file(
            zip,
            options,
            &log_dir.join(file_name),
            file_name,
            *max_bytes,
            sensitive_values,
            included_files,
        )?;
    }
    add_recent_screenshots(zip, options, log_dir, included_files)
}

fn add_recent_screenshots<W: Write + std::io::Seek>(
    zip: &mut zip::ZipWriter<W>,
    options: zip::write::SimpleFileOptions,
    log_dir: &Path,
    included_files: &mut Vec<String>,
) -> Result<(), String> {
    let Ok(entries) = fs::read_dir(log_dir.join("screens")) else {
        return Ok(());
    };
    let mut screenshots = entries
        .filter_map(Result::ok)
        .filter_map(|entry| {
            let file_type = entry.file_type().ok()?;
            if !file_type.is_file() {
                return None;
            }
            let path = entry.path();
            if !path
                .extension()
                .and_then(|extension| extension.to_str())
                .is_some_and(|extension| extension.eq_ignore_ascii_case("png"))
            {
                return None;
            }
            let metadata = entry.metadata().ok()?;
            let modified = metadata.modified().unwrap_or(std::time::UNIX_EPOCH);
            Some((modified, metadata.len(), path))
        })
        .collect::<Vec<_>>();
    screenshots.sort_by_key(|entry| std::cmp::Reverse(entry.0));

    let mut included_bytes = 0_u64;
    let mut included_count = 0_usize;
    for (_, size, path) in screenshots {
        if included_count >= INSIDERS_SCREENSHOT_MAX_FILES {
            break;
        }
        if size > INSIDERS_SCREENSHOT_MAX_BYTES.saturating_sub(included_bytes) {
            continue;
        }
        let Ok(content) = fs::read(&path) else {
            continue;
        };
        let Some(file_stem) = path.file_stem().and_then(|name| name.to_str()) else {
            continue;
        };
        let archive_name = format!("screens/{}.png", sanitize_file_name(file_stem));
        add_bytes(zip, options, &archive_name, &content, included_files)?;
        included_bytes = included_bytes.saturating_add(size);
        included_count += 1;
    }
    Ok(())
}

fn add_bytes<W: Write + std::io::Seek>(
    zip: &mut zip::ZipWriter<W>,
    options: zip::write::SimpleFileOptions,
    name: &str,
    bytes: &[u8],
    included_files: &mut Vec<String>,
) -> Result<(), String> {
    zip.start_file(name, options)
        .map_err(|err| format!("feedback archive entry を開始できません {name}: {err}"))?;
    zip.write_all(bytes)
        .map_err(|err| format!("feedback archive entry を保存できません {name}: {err}"))?;
    included_files.push(name.to_owned());
    Ok(())
}

fn read_json(path: PathBuf) -> serde_json::Value {
    fs::read(path)
        .ok()
        .and_then(|bytes| serde_json::from_slice(&bytes).ok())
        .unwrap_or_default()
}

fn feedback_artifact_identity(value: Option<&serde_json::Value>) -> serde_json::Value {
    let Some(value) = value else {
        return serde_json::Value::Null;
    };
    serde_json::json!({
        "bytes": value.get("bytes"),
        "modified_unix_ms": value.get("modified_unix_ms"),
        "sha256": value.get("sha256"),
        "error": value
            .get("error")
            .and_then(serde_json::Value::as_str)
            .map(|error| sanitize_text(error, 512)),
    })
}

fn file_identity(path: &Path) -> serde_json::Value {
    if !path.is_file() {
        return serde_json::json!({ "present": false });
    }
    let metadata = match fs::metadata(path) {
        Ok(metadata) => metadata,
        Err(err) => {
            return serde_json::json!({
                "present": true,
                "error": sanitize_text(&err.to_string(), 512),
            });
        }
    };
    let modified_unix_ms = metadata
        .modified()
        .ok()
        .and_then(|modified| {
            modified
                .duration_since(std::time::SystemTime::UNIX_EPOCH)
                .ok()
        })
        .map(|duration| duration.as_millis());
    match sha256_file(path) {
        Ok(sha256) => serde_json::json!({
            "present": true,
            "bytes": metadata.len(),
            "modified_unix_ms": modified_unix_ms,
            "sha256": sha256,
        }),
        Err(err) => serde_json::json!({
            "present": true,
            "bytes": metadata.len(),
            "modified_unix_ms": modified_unix_ms,
            "error": sanitize_text(&err, 512),
        }),
    }
}

fn sha256_file(path: &Path) -> Result<String, String> {
    let mut file = File::open(path).map_err(|err| format!("file open failed: {err}"))?;
    let mut hasher = Sha256::new();
    let mut buffer = [0_u8; 64 * 1024];
    loop {
        let read = file
            .read(&mut buffer)
            .map_err(|err| format!("file read failed: {err}"))?;
        if read == 0 {
            break;
        }
        hasher.update(&buffer[..read]);
    }
    Ok(format!("{:x}", hasher.finalize()))
}

fn feedback_sensitive_values(log_dir: &Path) -> Vec<String> {
    let launcher = read_json(log_dir.join("launcher.json"));
    [
        "/request/room_code",
        "/request/player_names/mario",
        "/request/player_names/luigi",
    ]
    .into_iter()
    .filter_map(|pointer| launcher.pointer(pointer))
    .filter_map(serde_json::Value::as_str)
    .map(str::trim)
    .filter(|value| !value.is_empty())
    .map(str::to_owned)
    .collect()
}

fn sanitize_feedback_text(value: &str, max_bytes: usize, sensitive_values: &[String]) -> String {
    let mut sanitized = sanitize_text(value, max_bytes);
    for sensitive in sensitive_values {
        sanitized = replace_feedback_value(&sanitized, sensitive);
    }
    sanitized
}

fn replace_feedback_value(value: &str, sensitive: &str) -> String {
    let pattern = Regex::new(&format!("(?i){}", regex::escape(sensitive)));
    match pattern {
        Ok(pattern) => pattern.replace_all(value, "[redacted]").into_owned(),
        Err(_) => value.to_owned(),
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
