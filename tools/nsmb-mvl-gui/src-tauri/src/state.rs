use std::path::PathBuf;
use std::process::Child;
use std::sync::Mutex;

use crate::models::MatchPlayerNames;
use crate::process_job::ChildProcessJob;

#[derive(Default)]
pub(crate) struct AppState {
    pub(crate) session: Mutex<Option<ManagedSession>>,
}

pub(crate) struct ManagedSession {
    pub(crate) melon: Child,
    pub(crate) bridge: Child,
    pub(crate) _process_job: ChildProcessJob,
    pub(crate) log_dir: PathBuf,
    pub(crate) player_names: Option<MatchPlayerNames>,
    pub(crate) crash_report_sent: bool,
}
