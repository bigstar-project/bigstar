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

use preflight::cli_preflight_check;
#[cfg(any(debug_assertions, test))]
use specta_typescript::Typescript;
use state::AppState;
use tauri::{menu::MenuBuilder, tray::TrayIconBuilder, Manager, WebviewWindow, WindowEvent};
use tauri_plugin_autostart::MacosLauncher;
use tauri_specta::{collect_commands, Builder as SpectaBuilder};

const STARTUP_ARG: &str = "--startup";
const TRAY_SHOW_ID: &str = "show";
const TRAY_QUIT_ID: &str = "quit";

fn specta_builder() -> SpectaBuilder<tauri::Wry> {
    SpectaBuilder::<tauri::Wry>::new().commands(collect_commands![
        commands::get_defaults,
        commands::save_rom_paths,
        commands::save_diagnostic_events_enabled,
        commands::save_new_room_notifications_enabled,
        commands::save_player_name,
        commands::select_rom_file,
        preflight::preflight_check,
        commands::generate_roms,
        commands::ensure_roms,
        commands::start_match,
        commands::stop_match,
        commands::session_status,
        commands::load_match_history,
        commands::save_match_history,
        commands::open_log_dir,
        commands::open_melonds,
        commands::open_melonds_input_config,
        commands::get_startup_enabled,
        commands::set_startup_enabled
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
    let startup_launch = std::env::args().any(|arg| arg == STARTUP_ARG);

    tauri::Builder::default()
        .plugin(tauri_plugin_autostart::init(
            MacosLauncher::LaunchAgent,
            Some(vec![STARTUP_ARG]),
        ))
        .plugin(tauri_plugin_notification::init())
        .plugin(tauri_plugin_process::init())
        .plugin(tauri_plugin_updater::Builder::new().build())
        .manage(AppState::default())
        .invoke_handler(specta_builder.invoke_handler())
        .on_window_event(|window, event| {
            if window.label() != "main" {
                return;
            }
            if let WindowEvent::CloseRequested { api, .. } = event {
                api.prevent_close();
                let _ = window.hide();
            }
        })
        .setup(move |app| {
            specta_builder.mount_events(app);
            setup_tray(app)?;
            if startup_launch {
                if let Some(window) = app.get_webview_window("main") {
                    let _ = window.hide();
                }
            }
            Ok(())
        })
        .build(tauri::generate_context!())
        .expect("error while building tauri application")
        .run(|_, _| {});
}

fn setup_tray(app: &tauri::App) -> tauri::Result<()> {
    let menu = MenuBuilder::new(app)
        .text(TRAY_SHOW_ID, "開く")
        .separator()
        .text(TRAY_QUIT_ID, "終了")
        .build()?;

    let mut tray = TrayIconBuilder::new()
        .menu(&menu)
        .tooltip("NSMB Mario vs Luigi Online")
        .show_menu_on_left_click(false)
        .on_menu_event(|app, event| match event.id().as_ref() {
            TRAY_SHOW_ID => show_main_window(app.get_webview_window("main")),
            TRAY_QUIT_ID => app.exit(0),
            _ => {}
        });

    if let Some(icon) = app.default_window_icon() {
        tray = tray.icon(icon.clone());
    }

    tray.build(app)?;
    Ok(())
}

fn show_main_window(window: Option<WebviewWindow>) {
    if let Some(window) = window {
        let _ = window.show();
        let _ = window.set_focus();
    }
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
