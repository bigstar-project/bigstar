use serde::{Deserialize, Serialize};
use specta::Type;

#[derive(Debug, Deserialize, Serialize, Type)]
#[serde(rename_all = "snake_case")]
pub(crate) struct LaunchRequest {
    pub(crate) role: Role,
    pub(crate) signal_url: String,
    pub(crate) room_code: String,
    pub(crate) port: u16,
    pub(crate) rom_path: String,
    pub(crate) settings: GameSettings,
}

#[derive(Debug, Deserialize, Type)]
#[serde(rename_all = "snake_case")]
pub(crate) struct GenerateRomRequest {
    pub(crate) source_rom: String,
    pub(crate) stage: u8,
    pub(crate) settings: GameSettings,
}

#[derive(Debug, Deserialize, Serialize, Clone, Type)]
#[serde(rename_all = "snake_case")]
pub(crate) enum Role {
    Host,
    Client,
}

#[derive(Debug, Deserialize, Serialize, Clone, Type)]
#[serde(rename_all = "snake_case")]
pub(crate) struct GameSettings {
    pub(crate) course_mode: CourseMode,
    pub(crate) wins: u8,
    pub(crate) big_stars: u8,
    pub(crate) lives: Lives,
    pub(crate) match_seed: String,
    pub(crate) input_delay_frames: u8,
    pub(crate) input_max_frame_lead: u8,
    pub(crate) rollback_enabled: bool,
}

#[derive(Debug, Deserialize, Serialize, Clone, Copy, Type)]
#[serde(rename_all = "snake_case")]
pub(crate) enum CourseMode {
    Random,
    Select,
}

#[derive(Debug, Deserialize, Serialize, Clone, Copy, Type)]
#[serde(rename_all = "snake_case")]
pub(crate) enum Lives {
    #[serde(rename = "3")]
    Three,
    #[serde(rename = "5")]
    Five,
    Endless,
}

#[derive(Serialize, Type)]
pub(crate) struct Defaults {
    pub(crate) signal_url: String,
    pub(crate) room_code: String,
    pub(crate) host_rom_path: String,
    pub(crate) client_rom_path: String,
    pub(crate) base_rom_path: String,
    pub(crate) roms_prepared_once: bool,
    pub(crate) input_config_opened_once: bool,
    pub(crate) port: u16,
}

#[derive(Debug, Default, Deserialize, Serialize, Type)]
#[serde(default)]
pub(crate) struct LauncherSettings {
    pub(crate) base_rom_path: String,
    pub(crate) roms_prepared_once: bool,
    pub(crate) input_config_opened_once: bool,
}

#[derive(Debug, Deserialize, Type)]
#[serde(rename_all = "snake_case")]
pub(crate) struct SaveRomPathsRequest {
    pub(crate) base_rom_path: String,
}

#[derive(Debug, Serialize, Type)]
pub(crate) struct LaunchResponse {
    pub(crate) log_dir: String,
    pub(crate) melon_pid: u32,
    pub(crate) bridge_pid: u32,
}

#[derive(Serialize, Type)]
pub(crate) struct GenerateRomResponse {
    pub(crate) host_rom: String,
    pub(crate) client_rom: String,
    pub(crate) generated: bool,
}

#[derive(Serialize, Type)]
pub(crate) struct SessionStatus {
    pub(crate) active: bool,
    pub(crate) log_dir: Option<String>,
    pub(crate) melon: Option<String>,
    pub(crate) bridge: Option<String>,
    pub(crate) webrtc: Option<BridgeDiagnostics>,
    pub(crate) diagnostics_error: Option<String>,
}

#[derive(Debug, Deserialize, Serialize, Type)]
pub(crate) struct BridgeDiagnostics {
    pub(crate) role: Option<String>,
    pub(crate) phase: Option<String>,
    pub(crate) signal_url: Option<String>,
    pub(crate) session: Option<String>,
    pub(crate) ice_servers: Option<Vec<String>>,
    pub(crate) connection_state: Option<String>,
    pub(crate) gathering_state: Option<String>,
    pub(crate) ice_state: Option<String>,
    pub(crate) selected_candidate_pair: Option<SelectedCandidatePair>,
    pub(crate) stats: Option<BridgeStats>,
    pub(crate) last_error: Option<String>,
}

#[derive(Debug, Deserialize, Serialize, Type)]
pub(crate) struct SelectedCandidatePair {
    pub(crate) route: Option<String>,
    pub(crate) local_type: Option<String>,
    pub(crate) remote_type: Option<String>,
    pub(crate) local: Option<String>,
    pub(crate) remote: Option<String>,
    pub(crate) local_address: Option<String>,
    pub(crate) remote_address: Option<String>,
}

#[derive(Debug, Deserialize, Serialize, Type)]
pub(crate) struct BridgeStats {
    pub(crate) app_to_webrtc_packets: Option<u32>,
    pub(crate) app_to_webrtc_bytes: Option<u32>,
    pub(crate) webrtc_to_app_packets: Option<u32>,
    pub(crate) webrtc_to_app_bytes: Option<u32>,
    pub(crate) dropped_no_local_target: Option<u32>,
}

#[derive(Debug, Serialize, Type)]
pub(crate) struct PreflightResponse {
    pub(crate) melonds_path: String,
    pub(crate) bridge_path: String,
    pub(crate) input_script: String,
    pub(crate) symbols_file: String,
    pub(crate) bridge_smoke: String,
}
