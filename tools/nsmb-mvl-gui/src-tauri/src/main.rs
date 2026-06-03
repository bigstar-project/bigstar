#![cfg_attr(all(not(debug_assertions), windows), windows_subsystem = "windows")]

mod commands;
mod config;
mod models;
mod paths;
mod preflight;
mod processes;
mod roms;
mod settings;
mod state;

#[cfg(test)]
mod tests;

use commands::{
    ensure_roms, generate_roms, get_defaults, open_log_dir, save_rom_paths, select_rom_file,
    session_status, start_match, stop_match,
};
use preflight::{cli_preflight_check, preflight_check};
use state::AppState;

fn main() {
    if std::env::args().any(|arg| arg == "--preflight") {
        match cli_preflight_check() {
            Ok(response) => {
                println!(
                    "{}",
                    serde_json::to_string_pretty(&response)
                        .unwrap_or_else(|_| format!("{response:?}"))
                );
            }
            Err(err) => {
                eprintln!("{err}");
                std::process::exit(1);
            }
        }
        return;
    }

    tauri::Builder::default()
        .plugin(tauri_plugin_process::init())
        .plugin(tauri_plugin_updater::Builder::new().build())
        .manage(AppState::default())
        .invoke_handler(tauri::generate_handler![
            get_defaults,
            save_rom_paths,
            select_rom_file,
            preflight_check,
            generate_roms,
            ensure_roms,
            start_match,
            stop_match,
            session_status,
            open_log_dir
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
