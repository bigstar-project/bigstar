use std::fs::{self, OpenOptions};
use std::io::Write;
use std::panic;
use std::path::{Path, PathBuf};
use std::sync::LazyLock;
use std::time::{SystemTime, UNIX_EPOCH};

use regex::Regex;
use tauri::AppHandle;

use crate::config::{app_edition, app_version, build_profile};
use crate::models::{RecordAppContextRequest, RecordAppErrorRequest};
use crate::paths::app_data_dir;

const APP_DIAGNOSTICS_DIR: &str = "diagnostics";
const APP_ERROR_FILE: &str = "app-errors.jsonl";
const APP_CONTEXT_FILE: &str = "app-context.json";
const APP_ERROR_MAX_BYTES: u64 = 1024 * 1024;
const APP_ERROR_GENERATIONS: usize = 3;
const SESSION_EVENT_FILE: &str = "session-events.jsonl";
const SESSION_EVENT_MAX_BYTES: usize = 256 * 1024;
const MAX_MESSAGE_BYTES: usize = 8 * 1024;
const MAX_STACK_BYTES: usize = 32 * 1024;

static URL_QUERY_RE: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r"(?i)\b((?:https?|wss?)://[^\s?#]+)(?:\?[^\s#]*)?(?:#[^\s]*)?")
        .expect("valid URL sanitizer")
});
static IPV4_RE: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r"\b(?:\d{1,3}\.){3}\d{1,3}(?::\d{1,5})?\b").expect("valid IPv4 sanitizer")
});
static IPV6_RE: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r"(?i)\b(?:[0-9a-f]{1,4}:){2,}[0-9a-f:]{1,4}(?:%\w+)?(?::\d{1,5})?\b")
        .expect("valid IPv6 sanitizer")
});
static SECRET_RE: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(
        r#"(?i)(["']?(?:token|authorization|bearer|secret|password|api[_-]?key|room[_-]?code|host[_-]?token|join[_-]?token|player[_-]?name)["']?(?:\s*[=:]\s*["']?|\s+))[^"'\s,;}\]]+"#,
    )
    .expect("valid secret sanitizer")
});

fn diagnostic_log_retention_limit<T>(public_limit: T) -> Option<T> {
    if cfg!(feature = "insiders-edition") {
        None
    } else {
        Some(public_limit)
    }
}

#[tauri::command]
#[specta::specta]
pub(crate) fn record_app_error(
    app: AppHandle,
    request: RecordAppErrorRequest,
) -> Result<(), String> {
    record_app_error_inner(&app, &request)
}

#[tauri::command]
#[specta::specta]
pub(crate) fn record_app_context(
    app: AppHandle,
    request: RecordAppContextRequest,
) -> Result<(), String> {
    let path = app_context_path(&app)?;
    let value = serde_json::json!({
        "schema_version": 1,
        "user_agent": sanitize_text(&request.user_agent, 2048),
        "language": sanitize_text(&request.language, 128),
        "hardware_concurrency": request.hardware_concurrency,
        "device_memory_gib": request.device_memory_gib,
        "screen_width": request.screen_width,
        "screen_height": request.screen_height,
        "device_pixel_ratio": request.device_pixel_ratio,
        "gpu_renderer": request
            .gpu_renderer
            .as_deref()
            .map(|value| sanitize_text(value, 2048)),
    });
    let bytes = serde_json::to_vec_pretty(&value)
        .map_err(|err| format!("app context を生成できません: {err}"))?;
    fs::write(path, bytes).map_err(|err| format!("app context を保存できません: {err}"))
}

pub(crate) fn record_backend_error(app: &AppHandle, operation: &str, message: &str) {
    let _ = record_app_error_inner(
        app,
        &RecordAppErrorRequest {
            source: "rust".to_owned(),
            operation: operation.to_owned(),
            message: message.to_owned(),
            stack: None,
        },
    );
}

pub(crate) fn install_panic_hook(app: &AppHandle) {
    let path = app_error_path(app).ok();
    let previous = panic::take_hook();
    panic::set_hook(Box::new(move |info| {
        if let Some(path) = path.as_deref() {
            let message = panic_message(info);
            let value = serde_json::json!({
                "schema_version": 1,
                "unix_ms": unix_ms(),
                "source": "rust_panic",
                "operation": "panic",
                "message": sanitize_text(&message, MAX_MESSAGE_BYTES),
                "app": {
                    "version": app_version(),
                    "edition": app_edition(),
                    "build_profile": build_profile(),
                },
            });
            let _ = append_rotating_json_line(path, &value);
        }
        previous(info);
    }));
}

pub(crate) fn app_error_files(app: &AppHandle) -> Result<Vec<PathBuf>, String> {
    let path = app_error_path(app)?;
    let mut paths = Vec::new();
    if path.is_file() {
        paths.push(path.clone());
    }
    for generation in 1..APP_ERROR_GENERATIONS {
        let candidate = rotated_path(&path, generation);
        if candidate.is_file() {
            paths.push(candidate);
        }
    }
    Ok(paths)
}

pub(crate) fn app_context_file(app: &AppHandle) -> Result<PathBuf, String> {
    app_context_path(app)
}

pub(crate) fn append_session_event(
    log_dir: &Path,
    component: &str,
    event: &str,
    detail: Option<&str>,
) {
    let value = serde_json::json!({
        "schema_version": 1,
        "unix_ms": unix_ms(),
        "component": sanitize_text(component, 128),
        "event": sanitize_text(event, 128),
        "detail": detail.map(|value| sanitize_text(value, MAX_MESSAGE_BYTES)),
    });
    if let Ok(line) = serde_json::to_vec(&value) {
        let _ = append_retained_line(
            &log_dir.join(SESSION_EVENT_FILE),
            &line,
            diagnostic_log_retention_limit(SESSION_EVENT_MAX_BYTES),
        );
    }
}

pub(crate) fn sanitize_text(value: &str, max_bytes: usize) -> String {
    let mut sanitized = value.to_owned();
    if let Ok(user_profile) = std::env::var("USERPROFILE") {
        if !user_profile.trim().is_empty() {
            sanitized = replace_case_insensitive(&sanitized, &user_profile, "<user-profile>");
        }
    }
    sanitized = URL_QUERY_RE
        .replace_all(&sanitized, "$1?[redacted]")
        .into_owned();
    sanitized = IPV4_RE.replace_all(&sanitized, "[ip]").into_owned();
    sanitized = IPV6_RE.replace_all(&sanitized, "[ip]").into_owned();
    sanitized = SECRET_RE
        .replace_all(&sanitized, "$1[redacted]")
        .into_owned();
    truncate_utf8(&sanitized, max_bytes)
}

fn record_app_error_inner(app: &AppHandle, request: &RecordAppErrorRequest) -> Result<(), String> {
    let value = serde_json::json!({
        "schema_version": 1,
        "unix_ms": unix_ms(),
        "source": sanitize_text(&request.source, 128),
        "operation": sanitize_text(&request.operation, 256),
        "message": sanitize_text(&request.message, MAX_MESSAGE_BYTES),
        "stack": request
            .stack
            .as_deref()
            .map(|stack| sanitize_text(stack, MAX_STACK_BYTES)),
        "app": {
            "version": app_version(),
            "edition": app_edition(),
            "build_profile": build_profile(),
        },
    });
    let path = app_error_path(app)?;
    append_rotating_json_line(&path, &value)
        .map_err(|err| format!("app error log を保存できません: {err}"))
}

fn app_error_path(app: &AppHandle) -> Result<PathBuf, String> {
    let dir = app_diagnostics_dir(app)?;
    Ok(dir.join(APP_ERROR_FILE))
}

fn app_context_path(app: &AppHandle) -> Result<PathBuf, String> {
    let dir = app_diagnostics_dir(app)?;
    Ok(dir.join(APP_CONTEXT_FILE))
}

fn app_diagnostics_dir(app: &AppHandle) -> Result<PathBuf, String> {
    let dir = app_data_dir(app)?.join(APP_DIAGNOSTICS_DIR);
    fs::create_dir_all(&dir)
        .map_err(|err| format!("app diagnostics directory を作成できません: {err}"))?;
    Ok(dir)
}

fn append_rotating_json_line(path: &Path, value: &serde_json::Value) -> std::io::Result<()> {
    let mut line = serde_json::to_vec(value).map_err(std::io::Error::other)?;
    line.push(b'\n');
    let current_size = path.metadata().map(|metadata| metadata.len()).unwrap_or(0);
    if diagnostic_log_retention_limit(APP_ERROR_MAX_BYTES)
        .is_some_and(|limit| current_size.saturating_add(line.len() as u64) > limit)
    {
        rotate_files(path)?;
    }
    let mut file = OpenOptions::new().create(true).append(true).open(path)?;
    file.write_all(&line)?;
    file.flush()
}

fn rotate_files(path: &Path) -> std::io::Result<()> {
    let oldest = rotated_path(path, APP_ERROR_GENERATIONS - 1);
    if oldest.exists() {
        fs::remove_file(oldest)?;
    }
    for generation in (1..APP_ERROR_GENERATIONS - 1).rev() {
        let source = rotated_path(path, generation);
        if source.exists() {
            fs::rename(source, rotated_path(path, generation + 1))?;
        }
    }
    if path.exists() {
        fs::rename(path, rotated_path(path, 1))?;
    }
    Ok(())
}

fn rotated_path(path: &Path, generation: usize) -> PathBuf {
    path.with_file_name(format!("{APP_ERROR_FILE}.{generation}"))
}

fn append_retained_line(path: &Path, line: &[u8], max_bytes: Option<usize>) -> std::io::Result<()> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }
    let Some(max_bytes) = max_bytes else {
        let mut file = OpenOptions::new().create(true).append(true).open(path)?;
        file.write_all(line)?;
        file.write_all(b"\n")?;
        return file.flush();
    };
    let mut content = fs::read(path).unwrap_or_default();
    if !content.is_empty() && !content.ends_with(b"\n") {
        content.push(b'\n');
    }
    content.extend_from_slice(line);
    content.push(b'\n');
    if content.len() > max_bytes {
        let start = content.len() - max_bytes;
        let boundary = content[start..]
            .iter()
            .position(|byte| *byte == b'\n')
            .map(|offset| start + offset + 1)
            .unwrap_or(start);
        content.drain(..boundary);
    }
    let temp = path.with_extension("jsonl.tmp");
    fs::write(&temp, content)?;
    if path.exists() {
        fs::remove_file(path)?;
    }
    fs::rename(temp, path)
}

fn panic_message(info: &panic::PanicHookInfo<'_>) -> String {
    let payload = if let Some(value) = info.payload().downcast_ref::<&str>() {
        (*value).to_owned()
    } else if let Some(value) = info.payload().downcast_ref::<String>() {
        value.clone()
    } else {
        "panic payload unavailable".to_owned()
    };
    match info.location() {
        Some(location) => format!(
            "{payload} at {}:{}:{}",
            location.file(),
            location.line(),
            location.column()
        ),
        None => payload,
    }
}

fn replace_case_insensitive(value: &str, needle: &str, replacement: &str) -> String {
    if needle.is_empty() {
        return value.to_owned();
    }
    let pattern = Regex::new(&format!("(?i){}", regex::escape(needle)));
    match pattern {
        Ok(pattern) => pattern.replace_all(value, replacement).into_owned(),
        Err(_) => value.to_owned(),
    }
}

fn truncate_utf8(value: &str, max_bytes: usize) -> String {
    if value.len() <= max_bytes {
        return value.to_owned();
    }
    let mut end = max_bytes;
    while !value.is_char_boundary(end) {
        end -= 1;
    }
    format!("{}…", &value[..end])
}

fn unix_ms() -> u128 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sanitize_text_removes_urls_ips_secrets_and_user_profile() {
        std::env::set_var("USERPROFILE", r"C:\Users\Sample");
        let value = sanitize_text(
            r"C:\Users\Sample\rom.nds wss://example.test/session?token=secret 192.0.2.12:1234 token=abc",
            4096,
        );
        assert!(!value.contains("Sample"));
        assert!(!value.contains("secret"));
        assert!(!value.contains("192.0.2.12"));
        assert!(value.contains("<user-profile>"));
        assert!(value.contains("[ip]"));
    }

    #[test]
    fn insiders_diagnostic_logs_have_no_retention_limit() {
        let session_limit = diagnostic_log_retention_limit(SESSION_EVENT_MAX_BYTES);
        assert_eq!(session_limit.is_none(), cfg!(feature = "insiders-edition"));
    }

    #[test]
    fn app_error_rotation_follows_edition_policy() {
        let dir = std::env::temp_dir().join(format!(
            "bigstar-app-error-rotation-{}-{}",
            std::process::id(),
            unix_ms()
        ));
        fs::create_dir_all(&dir).expect("create diagnostic test directory");
        let path = dir.join(APP_ERROR_FILE);
        let value = serde_json::json!({ "message": "x".repeat(600 * 1024) });

        append_rotating_json_line(&path, &value).expect("append first app error");
        append_rotating_json_line(&path, &value).expect("append second app error");

        if cfg!(feature = "insiders-edition") {
            assert!(path.metadata().expect("current app error log").len() > APP_ERROR_MAX_BYTES);
            assert!(!rotated_path(&path, 1).exists());
        } else {
            assert!(path.metadata().expect("current app error log").len() < APP_ERROR_MAX_BYTES);
            assert!(rotated_path(&path, 1).is_file());
        }
        fs::remove_dir_all(dir).expect("remove diagnostic test directory");
    }
}
