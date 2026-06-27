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
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub(crate) player_names: Option<MatchPlayerNames>,
    #[serde(default)]
    pub(crate) diagnostic_events_enabled: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub(crate) rom_identity: Option<RomIdentity>,
}

#[derive(Debug, Deserialize, Type)]
#[serde(rename_all = "snake_case")]
pub(crate) struct GenerateRomRequest {
    pub(crate) source_rom: String,
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
    pub(crate) course_stages: Vec<u8>,
    pub(crate) wins: u8,
    pub(crate) big_stars: u8,
    pub(crate) lives: Lives,
    pub(crate) match_seed: String,
    pub(crate) rng_seeds: Vec<String>,
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
    pub(crate) player_name: String,
    pub(crate) player_profile_id: String,
    pub(crate) roms_prepared_once: bool,
    pub(crate) input_config_opened_once: bool,
    pub(crate) port: u16,
    pub(crate) diagnostic_events_enabled: bool,
    pub(crate) new_room_notifications_enabled: bool,
}

#[derive(Debug, Deserialize, Serialize, Type)]
#[serde(default)]
pub(crate) struct LauncherSettings {
    pub(crate) base_rom_path: String,
    pub(crate) player_name: String,
    pub(crate) player_profile_id: String,
    pub(crate) roms_prepared_once: bool,
    pub(crate) input_config_opened_once: bool,
    pub(crate) diagnostic_events_enabled: bool,
    pub(crate) startup_configured: bool,
    pub(crate) startup_default_off_migration_applied: bool,
    #[serde(default = "default_new_room_notifications_enabled")]
    pub(crate) new_room_notifications_enabled: bool,
}

fn default_new_room_notifications_enabled() -> bool {
    true
}

impl Default for LauncherSettings {
    fn default() -> Self {
        Self {
            base_rom_path: String::new(),
            player_name: String::new(),
            player_profile_id: String::new(),
            roms_prepared_once: false,
            input_config_opened_once: false,
            diagnostic_events_enabled: false,
            startup_configured: false,
            startup_default_off_migration_applied: false,
            new_room_notifications_enabled: default_new_room_notifications_enabled(),
        }
    }
}

#[derive(Debug, Deserialize, Type)]
#[serde(rename_all = "snake_case")]
pub(crate) struct SaveRomPathsRequest {
    pub(crate) base_rom_path: String,
}

#[derive(Debug, Deserialize, Type)]
#[serde(rename_all = "snake_case")]
pub(crate) struct SaveDiagnosticEventsRequest {
    pub(crate) enabled: bool,
}

#[derive(Debug, Deserialize, Type)]
#[serde(rename_all = "snake_case")]
pub(crate) struct SaveNewRoomNotificationsRequest {
    pub(crate) enabled: bool,
}

#[derive(Debug, Deserialize, Type)]
#[serde(rename_all = "snake_case")]
pub(crate) struct ShowNewRoomNotificationRequest {
    pub(crate) title: String,
    pub(crate) body: String,
}

#[derive(Debug, Deserialize, Type)]
#[serde(rename_all = "snake_case")]
pub(crate) struct SavePlayerNameRequest {
    pub(crate) player_name: String,
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
    pub(crate) rom_identity: RomIdentity,
}

#[derive(Debug, Clone, Deserialize, Serialize, Type)]
#[serde(rename_all = "snake_case")]
pub(crate) struct RomIdentity {
    pub(crate) rom_pair_id: String,
    pub(crate) generator_id: String,
    pub(crate) host_rom_sha256: String,
    pub(crate) client_rom_sha256: String,
}

#[derive(Serialize, Type)]
pub(crate) struct SessionStatus {
    pub(crate) active: bool,
    pub(crate) log_dir: Option<String>,
    pub(crate) melon: Option<String>,
    pub(crate) bridge: Option<String>,
    pub(crate) webrtc: Option<BridgeDiagnostics>,
    pub(crate) diagnostics_error: Option<String>,
    pub(crate) game_state_mismatch: Option<GameStateMismatch>,
    pub(crate) mvl_results: Vec<MvlStageResult>,
}

#[derive(Debug, Deserialize, Serialize, Type)]
pub(crate) struct GameStateMismatch {
    pub(crate) instance: Option<i32>,
    pub(crate) frame: Option<u32>,
    pub(crate) local_hash: Option<String>,
    pub(crate) remote_hash: Option<String>,
    pub(crate) basic_matches: Option<bool>,
    pub(crate) player_global_matches: Option<bool>,
    pub(crate) wifi_candidate_matches: Option<bool>,
    pub(crate) render_candidate_matches: Option<bool>,
    pub(crate) line: String,
}

#[derive(Clone, Debug, Deserialize, Serialize, Type)]
pub(crate) struct MvlPlayerResult {
    pub(crate) stars: u32,
    pub(crate) displayed_stars: u32,
    pub(crate) collected_stars: u32,
    pub(crate) lives: u32,
    pub(crate) deaths: u32,
    pub(crate) dead: bool,
}

#[derive(Clone, Debug, Deserialize, Serialize, Type)]
pub(crate) struct MvlStageResult {
    pub(crate) game_index: u32,
    pub(crate) stage: Option<u8>,
    pub(crate) frame: u32,
    pub(crate) winner: Option<u8>,
    pub(crate) mario: MvlPlayerResult,
    pub(crate) luigi: MvlPlayerResult,
    pub(crate) mario_match_wins: u32,
    pub(crate) luigi_match_wins: u32,
    pub(crate) target_wins: u32,
    pub(crate) resolved: bool,
    pub(crate) line: String,
}

#[derive(Clone, Debug, Deserialize, Serialize, Type)]
#[serde(rename_all = "camelCase")]
pub(crate) struct MatchPlayerNames {
    pub(crate) mario: String,
    pub(crate) luigi: String,
}

#[derive(Clone, Debug, Deserialize, Serialize, Type)]
#[serde(rename_all = "camelCase")]
pub(crate) struct MatchPlayerIds {
    pub(crate) mario: String,
    pub(crate) luigi: String,
}

#[derive(Clone, Debug, Deserialize, Serialize, Type)]
#[serde(rename_all = "camelCase")]
pub(crate) enum MatchHistoryStatus {
    Running,
    Completed,
    Stopped,
}

#[derive(Clone, Debug, Deserialize, Serialize, Type)]
#[serde(rename_all = "camelCase")]
pub(crate) struct MatchHistoryRecord {
    pub(crate) id: String,
    pub(crate) log_dir: String,
    pub(crate) player_ids: MatchPlayerIds,
    pub(crate) player_names: MatchPlayerNames,
    pub(crate) role: Role,
    pub(crate) room_code: String,
    pub(crate) settings: GameSettings,
    pub(crate) stages: Vec<MvlStageResult>,
    pub(crate) started_at: String,
    pub(crate) status: MatchHistoryStatus,
}

#[derive(Debug, Deserialize)]
pub(crate) struct MelonDiagnostics {
    pub(crate) game_state_mismatch: Option<GameStateMismatch>,
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
