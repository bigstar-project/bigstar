use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::{SystemTime, UNIX_EPOCH};

#[cfg(windows)]
use std::os::windows::process::CommandExt;

use serde::{Deserialize, Serialize};
use specta::Type;

use crate::paths::{ensure_parent_dir, repo_root};

const MAX_TEXT_FILE_BYTES: u64 = 64 * 1024 * 1024;

#[derive(Debug, Serialize, Type)]
#[serde(rename_all = "snake_case")]
pub(crate) struct AiArtifact {
    pub(crate) path: String,
    pub(crate) kind: String,
    pub(crate) bytes: f64,
    pub(crate) modified_unix_secs: f64,
}

#[derive(Debug, Deserialize, Type)]
#[serde(rename_all = "snake_case")]
pub(crate) struct ReadAiTextFileRequest {
    pub(crate) path: String,
}

#[derive(Debug, Serialize, Type)]
#[serde(rename_all = "snake_case")]
pub(crate) struct ReadAiTextFileResponse {
    pub(crate) path: String,
    pub(crate) text: String,
}

#[derive(Debug, Deserialize, Type)]
#[serde(rename_all = "snake_case")]
pub(crate) struct RunAiToolRequest {
    pub(crate) task: String,
    pub(crate) input_path: Option<String>,
    pub(crate) output_path: Option<String>,
    pub(crate) session_path: Option<String>,
    pub(crate) dataset_path: Option<String>,
    pub(crate) model_path: Option<String>,
    pub(crate) runtime_model_path: Option<String>,
    pub(crate) log_dir: Option<String>,
    pub(crate) scenario: Option<String>,
    pub(crate) policy: Option<String>,
    pub(crate) seed: Option<String>,
    pub(crate) label_source: Option<String>,
    pub(crate) player: Option<u8>,
    pub(crate) frame: Option<i32>,
    pub(crate) frames: Option<u32>,
    pub(crate) epochs: Option<u32>,
    pub(crate) threshold: Option<f64>,
    pub(crate) max_objects: Option<u32>,
    pub(crate) dry_run: Option<bool>,
    pub(crate) split_by_recording: Option<bool>,
    pub(crate) allow_jit: Option<bool>,
    pub(crate) dual_window: Option<bool>,
    pub(crate) no_packet_capture: Option<bool>,
    pub(crate) scan_frames: Option<bool>,
}

#[derive(Debug, Serialize, Type)]
#[serde(rename_all = "snake_case")]
pub(crate) struct RunAiToolResponse {
    pub(crate) cwd: String,
    pub(crate) command_line: String,
    pub(crate) exit_code: Option<i32>,
    pub(crate) stdout: String,
    pub(crate) stderr: String,
    pub(crate) output_path: Option<String>,
}

#[tauri::command]
#[specta::specta]
pub(crate) fn list_ai_artifacts() -> Result<Vec<AiArtifact>, String> {
    let root = repo_root()?;
    let mut artifacts = Vec::new();
    for relative in ["logs", "datasets", "models"] {
        let dir = root.join(relative);
        if dir.exists() {
            collect_ai_artifacts(&dir, &mut artifacts)?;
        }
    }
    artifacts.sort_by(|a, b| {
        b.modified_unix_secs
            .partial_cmp(&a.modified_unix_secs)
            .unwrap_or(std::cmp::Ordering::Equal)
    });
    artifacts.truncate(300);
    Ok(artifacts)
}

#[tauri::command]
#[specta::specta]
pub(crate) fn read_ai_text_file(
    request: ReadAiTextFileRequest,
) -> Result<ReadAiTextFileResponse, String> {
    let path = resolve_user_path(&request.path)?;
    let metadata =
        fs::metadata(&path).map_err(|err| format!("ファイル情報を取得できません: {err}"))?;
    if metadata.len() > MAX_TEXT_FILE_BYTES {
        return Err(format!(
            "ファイルが大きすぎます: {} bytes（上限 {} bytes）",
            metadata.len(),
            MAX_TEXT_FILE_BYTES
        ));
    }
    let text = fs::read_to_string(&path)
        .map_err(|err| format!("テキストファイルを読み込めません: {err}"))?;
    Ok(ReadAiTextFileResponse {
        path: path.to_string_lossy().into_owned(),
        text,
    })
}

#[tauri::command]
#[specta::specta]
pub(crate) async fn run_ai_tool(request: RunAiToolRequest) -> Result<RunAiToolResponse, String> {
    tauri::async_runtime::spawn_blocking(move || run_ai_tool_inner(request))
        .await
        .map_err(|err| format!("AI tool worker が停止しました: {err}"))?
}

fn run_ai_tool_inner(request: RunAiToolRequest) -> Result<RunAiToolResponse, String> {
    let root = repo_root()?;
    let mut output_path = request
        .output_path
        .as_deref()
        .map(resolve_output_path)
        .transpose()?;
    let mut command = match request.task.as_str() {
        "inspect_playlog" => {
            let input = required_path(request.input_path.as_deref(), "playlog")?;
            let mut command = python_command(&root, "scripts/nsmb_mvl_ai_inspect_playlog.py");
            command.arg(resolve_user_path(&input)?);
            command.args(["--player", &player_string(&request)]);
            if let Some(frames) = request.frames {
                command.args(["--limit", &frames.to_string()]);
            }
            command
        }
        "render_svg" => {
            let input = required_path(request.input_path.as_deref(), "playlog")?;
            let output = output_path
                .clone()
                .unwrap_or_else(|| default_output_path("ai-workbench-frame.svg"));
            ensure_parent_dir(&output)?;
            output_path = Some(output.clone());
            let mut command = python_command(&root, "scripts/nsmb_mvl_ai_render_playlog_svg.py");
            command.arg(resolve_user_path(&input)?);
            command.arg(&output);
            command.args(["--player", &player_string(&request)]);
            if let Some(frame) = request.frame {
                if frame >= 0 {
                    command.args(["--frame", &frame.to_string()]);
                }
            }
            if let Some(max_objects) = request.max_objects {
                command.args(["--max-objects", &max_objects.to_string()]);
            }
            command
        }
        "human_recording" => {
            let mut command =
                powershell_script_command(&root, "scripts/run-nsmb-mvl-human-recording.ps1");
            command.args(["-Frames", &request.frames.unwrap_or(999_999).to_string()]);
            if let Some(value) = non_empty(&request.scenario) {
                command.args(["-Scenario", value]);
            }
            if let Some(value) = non_empty(&request.seed) {
                command.args(["-MvlMatchSeed", value]);
            }
            if let Some(value) = non_empty(&request.log_dir) {
                command.args(["-LogDir", value]);
            }
            if request.no_packet_capture.unwrap_or(true) {
                command.arg("-NoPacketCapture");
            }
            if request.dual_window.unwrap_or(false) {
                command.arg("-DualWindow");
            }
            if request.allow_jit.unwrap_or(true) {
                command.arg("-AllowJit");
            }
            command
        }
        "recording_postcommands" => {
            let session = required_path(request.session_path.as_deref(), "recording-session.json")?;
            let mut command =
                powershell_script_command(&root, "scripts/run-nsmb-mvl-recording-postcommands.ps1");
            command.args(["-Session", &resolve_user_path(&session)?.to_string_lossy()]);
            if request.dry_run.unwrap_or(false) {
                command.arg("-DryRun");
            }
            command
        }
        "build_dataset" => {
            let input = required_path(request.input_path.as_deref(), "recording/playlog")?;
            let output = output_path
                .clone()
                .unwrap_or_else(|| default_output_path("ai-dataset-player1.csv"));
            ensure_parent_dir(&output)?;
            let mut command = python_command(&root, "scripts/nsmb_mvl_ai_build_dataset.py");
            command.arg(resolve_user_path(&input)?);
            command.arg(&output);
            command.args(["--player", &player_string(&request)]);
            command.args([
                "--label-source",
                request.label_source.as_deref().unwrap_or("player"),
            ]);
            command.arg("--require-player-found");
            output_path = Some(output);
            command
        }
        "train_imitation" => {
            let dataset = required_path(
                request
                    .dataset_path
                    .as_deref()
                    .or(request.input_path.as_deref()),
                "dataset",
            )?;
            let model = output_path
                .clone()
                .or_else(|| {
                    request
                        .model_path
                        .as_deref()
                        .map(resolve_output_path)
                        .transpose()
                        .ok()
                        .flatten()
                })
                .unwrap_or_else(|| default_output_path("imitation-model.npz"));
            ensure_parent_dir(&model)?;
            let mut command = python_command(&root, "scripts/nsmb_mvl_ai_train_imitation.py");
            command.arg(resolve_user_path(&dataset)?);
            command.arg(&model);
            command.args(["--epochs", &request.epochs.unwrap_or(500).to_string()]);
            if request.split_by_recording.unwrap_or(true) {
                command.arg("--split-by-recording");
            }
            output_path = Some(model);
            command
        }
        "export_runtime_model" => {
            let model = required_path(
                request
                    .model_path
                    .as_deref()
                    .or(request.input_path.as_deref()),
                "model.npz",
            )?;
            let output = output_path
                .clone()
                .or_else(|| {
                    request
                        .runtime_model_path
                        .as_deref()
                        .map(resolve_output_path)
                        .transpose()
                        .ok()
                        .flatten()
                })
                .unwrap_or_else(|| default_output_path("runtime-model.json"));
            ensure_parent_dir(&output)?;
            let mut command = python_command(&root, "scripts/nsmb_mvl_ai_export_runtime_model.py");
            command.arg(resolve_user_path(&model)?);
            command.arg(&output);
            output_path = Some(output);
            command
        }
        "closed_loop_eval" => {
            let mut command =
                powershell_script_command(&root, "scripts/run-nsmb-mvl-ai-closed-loop-eval.ps1");
            command.args(["-Policy", request.policy.as_deref().unwrap_or("imitation")]);
            command.args(["-Frames", &request.frames.unwrap_or(2600).to_string()]);
            command.args(["-MvlStage", "0"]);
            if let Some(value) = non_empty(&request.seed) {
                command.args(["-MvlMatchSeed", value]);
            }
            if let Some(value) = non_empty(&request.log_dir) {
                command.args(["-LogDir", value]);
            }
            if let Some(value) = request
                .model_path
                .as_deref()
                .filter(|value| !value.trim().is_empty())
            {
                command.args(["-Model", &resolve_user_path(value)?.to_string_lossy()]);
            }
            if let Some(threshold) = request.threshold {
                command.args(["-Threshold", &threshold.to_string()]);
            }
            if request.allow_jit.unwrap_or(true) {
                command.arg("-AllowJit");
            }
            command
        }
        "recording_replay" => {
            let manifest = required_path(request.input_path.as_deref(), "recording.json")?;
            let mut command =
                powershell_script_command(&root, "scripts/run-nsmb-mvl-recording-replay.ps1");
            command.args([
                "-RecordingManifest",
                &resolve_user_path(&manifest)?.to_string_lossy(),
            ]);
            if request.dry_run.unwrap_or(false) {
                command.arg("-DryRun");
            }
            if request.scan_frames.unwrap_or(false) {
                command.arg("-ScanFrames");
            }
            if request.allow_jit.unwrap_or(true) {
                command.arg("-AllowJit");
            }
            command
        }
        other => return Err(format!("未対応のAIタスクです: {other}")),
    };

    command.current_dir(&root);
    command.stdin(Stdio::null());
    #[cfg(windows)]
    command.creation_flags(0x0800_0000);
    let command_line = command_line_text(&command);
    let output = command
        .output()
        .map_err(|err| format!("AIタスクを起動できません: {err}"))?;
    Ok(RunAiToolResponse {
        cwd: root.to_string_lossy().into_owned(),
        command_line,
        exit_code: output.status.code(),
        stdout: String::from_utf8_lossy(&output.stdout).into_owned(),
        stderr: String::from_utf8_lossy(&output.stderr).into_owned(),
        output_path: output_path.map(|path| path.to_string_lossy().into_owned()),
    })
}

fn collect_ai_artifacts(dir: &Path, artifacts: &mut Vec<AiArtifact>) -> Result<(), String> {
    for entry in
        fs::read_dir(dir).map_err(|err| format!("{} を読めません: {err}", dir.display()))?
    {
        let entry = entry.map_err(|err| format!("directory entry を読めません: {err}"))?;
        let path = entry.path();
        let file_name = path
            .file_name()
            .and_then(|name| name.to_str())
            .unwrap_or_default();
        if path.is_dir() {
            collect_ai_artifacts(&path, artifacts)?;
            continue;
        }
        let Some(kind) = artifact_kind(file_name) else {
            continue;
        };
        let metadata = entry
            .metadata()
            .map_err(|err| format!("{} の情報を取得できません: {err}", path.display()))?;
        let modified_unix_secs = metadata
            .modified()
            .unwrap_or(SystemTime::UNIX_EPOCH)
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs() as f64;
        artifacts.push(AiArtifact {
            path: path.to_string_lossy().into_owned(),
            kind: kind.to_owned(),
            bytes: metadata.len() as f64,
            modified_unix_secs,
        });
    }
    Ok(())
}

fn artifact_kind(file_name: &str) -> Option<&'static str> {
    if file_name == "ai-playlog.jsonl" || file_name.ends_with(".ai-playlog.jsonl") {
        Some("playlog")
    } else if file_name == "recording.json" {
        Some("recording")
    } else if file_name == "recording-session.json" {
        Some("session")
    } else if file_name == "recordings-index.json" {
        Some("index")
    } else if file_name.starts_with("ai-dataset") && file_name.ends_with(".csv") {
        Some("dataset")
    } else if file_name.ends_with(".npz") {
        Some("model")
    } else if file_name.ends_with("runtime-model.json") || file_name.ends_with("-runtime.json") {
        Some("runtime_model")
    } else if file_name == "closed-loop-eval.json" {
        Some("closed_loop_eval")
    } else if file_name.ends_with(".svg") {
        Some("svg")
    } else if file_name.ends_with("visual-state-audit.json") {
        Some("visual_audit")
    } else {
        None
    }
}

fn python_command(root: &Path, script: &str) -> Command {
    let mut command = Command::new("python");
    command.arg(root.join(script));
    command
}

fn powershell_script_command(root: &Path, script: &str) -> Command {
    let mut command = if cfg!(windows) {
        let mut command = Command::new("powershell");
        command.args(["-NoProfile", "-ExecutionPolicy", "Bypass", "-File"]);
        command
    } else {
        let mut command = Command::new("pwsh");
        command.args(["-NoProfile", "-File"]);
        command
    };
    command.arg(root.join(script));
    command
}

fn required_path(value: Option<&str>, label: &str) -> Result<String, String> {
    let value = value.unwrap_or_default().trim();
    if value.is_empty() {
        return Err(format!("{label} を指定してください"));
    }
    Ok(value.to_owned())
}

fn resolve_user_path(value: &str) -> Result<PathBuf, String> {
    let trimmed = value.trim();
    if trimmed.is_empty() {
        return Err("path が空です".to_owned());
    }
    let path = PathBuf::from(trimmed);
    let path = if path.is_absolute() {
        path
    } else {
        repo_root()?.join(path)
    };
    path.canonicalize()
        .map_err(|err| format!("path を解決できません: {}: {err}", path.display()))
}

fn resolve_output_path(value: &str) -> Result<PathBuf, String> {
    let trimmed = value.trim();
    if trimmed.is_empty() {
        return Err("output path が空です".to_owned());
    }
    let path = PathBuf::from(trimmed);
    Ok(if path.is_absolute() {
        path
    } else {
        repo_root()?.join(path)
    })
}

fn default_output_path(file_name: &str) -> PathBuf {
    repo_root()
        .unwrap_or_else(|_| PathBuf::from("."))
        .join("logs")
        .join("gui-ai-workbench")
        .join(file_name)
}

fn player_string(request: &RunAiToolRequest) -> String {
    if request.player == Some(0) {
        "0".to_owned()
    } else {
        "1".to_owned()
    }
}

fn non_empty(value: &Option<String>) -> Option<&str> {
    value
        .as_deref()
        .map(str::trim)
        .filter(|value| !value.is_empty())
}

fn command_line_text(command: &Command) -> String {
    let mut parts = vec![command.get_program().to_string_lossy().into_owned()];
    parts.extend(command.get_args().map(|arg| {
        let value = arg.to_string_lossy();
        if value.contains(' ') {
            format!("\"{value}\"")
        } else {
            value.into_owned()
        }
    }));
    parts.join(" ")
}
