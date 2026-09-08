use std::path::PathBuf;
use std::process::Child;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Mutex;

#[cfg(feature = "insiders-edition")]
use crate::models::MatchPlayerNames;
use crate::process_job::ChildProcessJob;

pub(crate) struct AppState {
    pub(crate) session: Mutex<Option<ManagedSession>>,
    pub(crate) launch_in_progress: AtomicBool,
    pub(crate) solo_test: crate::solo_test::SoloTestState,
}

impl Default for AppState {
    fn default() -> Self {
        Self {
            session: Mutex::new(None),
            launch_in_progress: AtomicBool::new(false),
            solo_test: crate::solo_test::SoloTestState::default(),
        }
    }
}

impl AppState {
    pub(crate) fn begin_launch(&self) -> Result<LaunchPreparationGuard<'_>, String> {
        self.launch_in_progress
            .compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire)
            .map_err(|_| "別の対戦準備が進行中です".to_owned())?;
        Ok(LaunchPreparationGuard(&self.launch_in_progress))
    }
}

pub(crate) struct LaunchPreparationGuard<'a>(pub(crate) &'a AtomicBool);

impl Drop for LaunchPreparationGuard<'_> {
    fn drop(&mut self) {
        self.0.store(false, Ordering::Release);
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
