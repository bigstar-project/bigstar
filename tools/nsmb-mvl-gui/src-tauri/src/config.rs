pub(crate) const DEFAULT_ROOM_CODE: &str = "test-room";
pub(crate) const DEFAULT_SIGNAL_URL: &str =
    "wss://nsmb-mvl-signaling-signaling-prod.uniunitaro.workers.dev/session";
pub(crate) fn default_signal_url() -> &'static str {
    match option_env!("NSMB_MVL_DEFAULT_SIGNAL_URL") {
        Some(url) if !url.trim().is_empty() => url.trim(),
        _ => DEFAULT_SIGNAL_URL,
    }
}
pub(crate) const DEFAULT_PORT: u16 = 8165;
pub(crate) const DEFAULT_FRAMES: u32 = 999_999;
pub(crate) const NETPLAY_START_FRAME: u32 = 840;
pub(crate) const REUSABLE_ROM_FORMAT: &str = "nsmb-mvl-reusable-runtime-config-v3";
