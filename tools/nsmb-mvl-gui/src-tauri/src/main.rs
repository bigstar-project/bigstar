#![cfg_attr(all(not(debug_assertions), windows), windows_subsystem = "windows")]

mod ai_workbench;
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

use preflight::cli_preflight_check;
#[cfg(any(debug_assertions, test))]
use specta_typescript::Typescript;
use state::AppState;
use tauri_specta::{collect_commands, Builder as SpectaBuilder};

fn specta_builder() -> SpectaBuilder<tauri::Wry> {
    SpectaBuilder::<tauri::Wry>::new().commands(collect_commands![
        ai_workbench::list_ai_artifacts,
        ai_workbench::read_ai_text_file,
        ai_workbench::run_ai_tool,
        ai_workbench::select_ai_log_file,
        commands::get_defaults,
        commands::save_rom_paths,
        commands::select_rom_file,
        preflight::preflight_check,
        commands::generate_roms,
        commands::ensure_roms,
        commands::start_match,
        commands::stop_match,
        commands::session_status,
        commands::open_log_dir,
        commands::open_melonds,
        commands::open_melonds_input_config
    ])
}

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

    let specta_builder = specta_builder();
    #[cfg(debug_assertions)]
    specta_builder
        .export(Typescript::default(), "../src/bindings.ts")
        .expect("failed to export TypeScript bindings");

    tauri::Builder::default()
        .plugin(tauri_plugin_process::init())
        .plugin(tauri_plugin_updater::Builder::new().build())
        .manage(AppState::default())
        .invoke_handler(specta_builder.invoke_handler())
        .setup(move |app| {
            specta_builder.mount_events(app);
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}

#[cfg(test)]
mod specta_tests {
    use super::*;

    #[test]
    fn export_bindings() {
        let builder = specta_builder();
        builder
            .export(Typescript::default(), "../src/bindings.ts")
            .expect("failed to export TypeScript bindings");
    }
}
