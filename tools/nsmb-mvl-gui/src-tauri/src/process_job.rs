use std::process::Child;

#[cfg(windows)]
use std::os::windows::io::AsRawHandle;

pub(crate) struct ChildProcessJob {
    #[cfg(windows)]
    job: win32job::Job,
}

impl ChildProcessJob {
    pub(crate) fn create() -> Result<Self, String> {
        create_child_process_job()
    }

    pub(crate) fn assign_child(&self, child: &Child, label: &str) -> Result<(), String> {
        assign_child_to_job(self, child, label)
    }
}

#[cfg(windows)]
fn create_child_process_job() -> Result<ChildProcessJob, String> {
    let mut info = win32job::ExtendedLimitInfo::new();
    info.limit_kill_on_job_close();

    let job = win32job::Job::create_with_limit_info(&info)
        .map_err(|err| format!("process job を作成できません: {err}"))?;
    Ok(ChildProcessJob { job })
}

#[cfg(not(windows))]
fn create_child_process_job() -> Result<ChildProcessJob, String> {
    Ok(ChildProcessJob {})
}

#[cfg(windows)]
fn assign_child_to_job(job: &ChildProcessJob, child: &Child, label: &str) -> Result<(), String> {
    let handle = child.as_raw_handle() as isize;
    job.job
        .assign_process(handle)
        .map_err(|err| format!("{label} を process job に割り当てられません: {err}"))
}

#[cfg(not(windows))]
fn assign_child_to_job(_job: &ChildProcessJob, _child: &Child, _label: &str) -> Result<(), String> {
    Ok(())
}
