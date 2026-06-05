use tauri::AppHandle;

use crate::models::PreflightResponse;
use crate::paths::{
    find_bridge_binary, find_bridge_binary_without_app, find_input_script,
    find_input_script_without_app, find_melonds_binary, find_melonds_binary_without_app,
    find_symbols_file, find_symbols_file_without_app,
};
use crate::processes::run_bridge_signaling_smoke;

#[tauri::command]
#[specta::specta]
pub(crate) fn preflight_check(app: AppHandle) -> Result<PreflightResponse, String> {
    let melon_path = find_melonds_binary(&app)?;
    let bridge_path = find_bridge_binary(&app)?;
    let input_script = find_input_script(&app)?;
    let symbols_file = find_symbols_file(&app)?;
    let bridge_smoke = run_bridge_signaling_smoke(&bridge_path)?;

    Ok(PreflightResponse {
        melonds_path: melon_path.to_string_lossy().into_owned(),
        bridge_path: bridge_path.to_string_lossy().into_owned(),
        input_script: input_script.to_string_lossy().into_owned(),
        symbols_file: symbols_file.to_string_lossy().into_owned(),
        bridge_smoke,
    })
}

pub(crate) fn cli_preflight_check() -> Result<PreflightResponse, String> {
    let melon_path = find_melonds_binary_without_app()?;
    let bridge_path = find_bridge_binary_without_app()?;
    let input_script = find_input_script_without_app()?;
    let symbols_file = find_symbols_file_without_app()?;
    let bridge_smoke = run_bridge_signaling_smoke(&bridge_path)?;

    Ok(PreflightResponse {
        melonds_path: melon_path.to_string_lossy().into_owned(),
        bridge_path: bridge_path.to_string_lossy().into_owned(),
        input_script: input_script.to_string_lossy().into_owned(),
        symbols_file: symbols_file.to_string_lossy().into_owned(),
        bridge_smoke,
    })
}
