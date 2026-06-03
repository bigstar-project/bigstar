use serde::{Deserialize, Serialize};

#[derive(Debug, Deserialize, Serialize)]
#[serde(rename_all = "snake_case")]
pub(crate) struct LaunchRequest {
    pub(crate) role: Role,
    pub(crate) signal_url: String,
    pub(crate) room_code: String,
    pub(crate) port: u16,
    pub(crate) rom_path: String,
    pub(crate) settings: GameSettings,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "snake_case")]
pub(crate) struct GenerateRomRequest {
    pub(crate) source_rom: String,
    pub(crate) host_rom: String,
    pub(crate) client_rom: String,
    pub(crate) stage: u8,
    pub(crate) settings: GameSettings,
}

#[derive(Debug, Deserialize, Serialize, Clone)]
#[serde(rename_all = "snake_case")]
pub(crate) enum Role {
    Host,
    Client,
}

#[derive(Debug, Deserialize, Serialize, Clone)]
#[serde(rename_all = "snake_case")]
pub(crate) struct GameSettings {
    pub(crate) course_mode: CourseMode,
    pub(crate) wins: u8,
    pub(crate) big_stars: u8,
    pub(crate) lives: Lives,
    pub(crate) match_seed: String,
}

#[derive(Debug, Deserialize, Serialize, Clone, Copy)]
#[serde(rename_all = "snake_case")]
pub(crate) enum CourseMode {
    Random,
    Select,
}

#[derive(Debug, Deserialize, Serialize, Clone, Copy)]
#[serde(rename_all = "snake_case")]
pub(crate) enum Lives {
    #[serde(rename = "3")]
    Three,
    #[serde(rename = "5")]
    Five,
    Endless,
}

#[derive(Serialize)]
pub(crate) struct Defaults {
    pub(crate) signal_url: String,
    pub(crate) room_code: String,
    pub(crate) host_rom_path: String,
    pub(crate) client_rom_path: String,
    pub(crate) base_rom_path: String,
    pub(crate) port: u16,
}

#[derive(Debug, Default, Deserialize, Serialize)]
#[serde(default)]
pub(crate) struct LauncherSettings {
    pub(crate) host_rom_path: String,
    pub(crate) client_rom_path: String,
    pub(crate) base_rom_path: String,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "snake_case")]
pub(crate) struct SaveRomPathsRequest {
    pub(crate) host_rom_path: String,
    pub(crate) client_rom_path: String,
    pub(crate) base_rom_path: String,
}

#[derive(Debug, Serialize)]
pub(crate) struct LaunchResponse {
    pub(crate) log_dir: String,
    pub(crate) melon_pid: u32,
    pub(crate) bridge_pid: u32,
}

#[derive(Serialize)]
pub(crate) struct GenerateRomResponse {
    pub(crate) host_rom: String,
    pub(crate) client_rom: String,
    pub(crate) generated: bool,
}

#[derive(Serialize)]
pub(crate) struct SessionStatus {
    pub(crate) active: bool,
    pub(crate) log_dir: Option<String>,
    pub(crate) melon: Option<String>,
    pub(crate) bridge: Option<String>,
    pub(crate) webrtc: Option<serde_json::Value>,
    pub(crate) diagnostics_error: Option<String>,
}

#[derive(Debug, Serialize)]
pub(crate) struct PreflightResponse {
    pub(crate) melonds_path: String,
    pub(crate) bridge_path: String,
    pub(crate) input_script: String,
    pub(crate) symbols_file: String,
    pub(crate) bridge_smoke: String,
}
