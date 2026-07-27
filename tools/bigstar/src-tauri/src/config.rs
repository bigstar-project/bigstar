pub(crate) const DEFAULT_ROOM_CODE: &str = "test-room";
pub(crate) const DEFAULT_SIGNAL_URL: &str =
    "wss://bigstar-signaling-insiders-signaling-prod.uniunitaro.workers.dev/session";
pub(crate) fn default_signal_url() -> &'static str {
    match option_env!("BIGSTAR_DEFAULT_SIGNAL_URL") {
        Some(url) if !url.trim().is_empty() => url.trim(),
        _ => DEFAULT_SIGNAL_URL,
    }
}
pub(crate) fn app_display_name() -> &'static str {
    #[cfg(feature = "insiders-edition")]
    {
        "Bigstar Insiders"
    }
    #[cfg(not(feature = "insiders-edition"))]
    {
        "Bigstar"
    }
}
pub(crate) fn app_data_dir_name() -> &'static str {
    match option_env!("BIGSTAR_APP_DATA_DIR_NAME") {
        Some(name) if !name.trim().is_empty() => name.trim(),
        _ => {
            #[cfg(feature = "insiders-edition")]
            {
                "Bigstar Insiders"
            }
            #[cfg(not(feature = "insiders-edition"))]
            {
                "Bigstar"
            }
        }
    }
}
pub(crate) fn app_version() -> &'static str {
    match option_env!("BIGSTAR_APP_VERSION") {
        Some(version) if !version.trim().is_empty() => version.trim(),
        _ => env!("CARGO_PKG_VERSION"),
    }
}
pub(crate) const DEFAULT_PORT: u16 = 8165;
pub(crate) const DEFAULT_FRAMES: u32 = 999_999;
pub(crate) const NETPLAY_START_FRAME: u32 = 840;
pub(crate) const REUSABLE_ROM_FORMAT: &str = "bigstar-reusable-runtime-config-v4";
