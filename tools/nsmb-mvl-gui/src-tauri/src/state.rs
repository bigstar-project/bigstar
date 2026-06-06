use std::path::PathBuf;
use std::process::Child;
use std::sync::Mutex;

#[derive(Default)]
pub(crate) struct AppState {
    pub(crate) session: Mutex<Option<ManagedSession>>,
}

pub(crate) struct ManagedSession {
    pub(crate) melon: Child,
    pub(crate) bridge: Child,
    pub(crate) log_dir: PathBuf,
}
