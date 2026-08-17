use std::path::PathBuf;
use std::process::Child;
use std::sync::atomic::AtomicBool;
use std::sync::Mutex;

#[cfg(feature = "insiders-edition")]
use crate::models::MatchPlayerNames;
use crate::process_job::ChildProcessJob;

pub(crate) struct AppState {
    pub(crate) session: Mutex<Option<ManagedSession>>,
    pub(crate) launch_in_progress: AtomicBool,
}

impl Default for AppState {
    fn default() -> Self {
        Self {
            session: Mutex::new(None),
            launch_in_progress: AtomicBool::new(false),
        }
    }
}

pub(crate) struct ManagedSession {
    pub(crate) melon: Child,
    pub(crate) bridge: Child,
    pub(crate) last_melon_state: String,
    pub(crate) last_bridge_state: String,
    pub(crate) _process_job: ChildProcessJob,
    pub(crate) log_dir: PathBuf,
    #[cfg(feature = "insiders-edition")]
    pub(crate) player_names: Option<MatchPlayerNames>,
    #[cfg(feature = "insiders-edition")]
    pub(crate) crash_report_sent: bool,
}
