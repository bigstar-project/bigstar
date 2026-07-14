#![cfg_attr(all(not(debug_assertions), windows), windows_subsystem = "windows")]

mod ai_workbench;
mod commands;
mod config;
mod crash_report;
mod history_store;
mod models;
mod paths;
mod preflight;
mod process_job;
mod processes;
mod roms;
mod settings;
mod state;
mod windowing;

#[cfg(test)]
mod tests;

use preflight::cli_preflight_check;
#[cfg(any(debug_assertions, test))]
use specta_typescript::Typescript;
use state::AppState;
use tauri::{
    menu::MenuBuilder,
    tray::{MouseButton, MouseButtonState, TrayIconBuilder, TrayIconEvent},
    Manager, WindowEvent,
};
use tauri_plugin_autostart::MacosLauncher;
use tauri_plugin_autostart::ManagerExt;
use tauri_plugin_window_state::{StateFlags, WindowExt};
use tauri_specta::{collect_commands, Builder as SpectaBuilder};
use windowing::show_main_window;

const STARTUP_ARG: &str = "--startup";
const TRAY_SHOW_ID: &str = "show";
const TRAY_QUIT_ID: &str = "quit";

fn specta_builder() -> SpectaBuilder<tauri::Wry> {
    SpectaBuilder::<tauri::Wry>::new().commands(collect_commands![
        ai_workbench::list_ai_artifacts,
        ai_workbench::open_ai_replay_log,
        ai_workbench::read_ai_replay_frame,
        ai_workbench::read_ai_text_file,
        ai_workbench::run_ai_tool,
        ai_workbench::select_ai_log_file,
        commands::get_defaults,
        commands::save_rom_paths,
        commands::save_diagnostic_events_enabled,
        commands::save_detailed_logs_enabled,
        commands::save_ai_play_log_enabled,
        commands::save_performance_logs_enabled,
        commands::save_new_room_notifications_enabled,
        commands::show_new_room_notification,
        commands::save_player_name,
        commands::select_rom_file,
        preflight::preflight_check,
        commands::generate_roms,
        commands::ensure_roms,
        commands::start_match,
        commands::stop_match,
        commands::session_status,
        commands::upsert_match_history,
        commands::delete_match_history,
        commands::query_match_history,
        commands::load_match_history_dashboard,
        commands::load_match_history_opponents,
        commands::open_log_dir,
        commands::create_log_archive,
        commands::cleanup_detailed_logs,
        commands::upload_log_archive,
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

    let builder = tauri::Builder::default();
    #[cfg(feature = "single-instance")]
    let builder = builder.plugin(tauri_plugin_single_instance::init(|app, _args, _cwd| {
        show_main_window(app.get_webview_window("main"));
    }));

    builder
        .plugin(tauri_plugin_autostart::init(
            MacosLauncher::LaunchAgent,
            Some(vec![STARTUP_ARG]),
        ))
        .plugin(tauri_plugin_notification::init())
        .plugin(tauri_plugin_process::init())
        .plugin(tauri_plugin_updater::Builder::new().build())
        .plugin(
            tauri_plugin_window_state::Builder::default()
                .with_state_flags(window_state_flags())
                .skip_initial_state("main")
                .build(),
        )
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
            start_session_supervisor(app.handle().clone());
            if let Err(err) = apply_startup_default_off_migration(app.handle()) {
                eprintln!("{err}");
            }
            if let Some(window) = app.get_webview_window("main") {
                let _ = window.restore_state(window_state_flags());
                if !startup_launch {
                    show_main_window(Some(window));
                }
            }
            Ok(())
        })
        .build(tauri::generate_context!())
        .expect("error while building tauri application")
        .run(|_, _| {});
}

fn window_state_flags() -> StateFlags {
    StateFlags::SIZE | StateFlags::POSITION | StateFlags::MAXIMIZED | StateFlags::FULLSCREEN
}

fn start_session_supervisor(app: tauri::AppHandle) {
    let _ = std::thread::Builder::new()
        .name("nsmb-mvl-session-supervisor".to_owned())
        .spawn(move || loop {
            std::thread::sleep(std::time::Duration::from_secs(1));
            let state = app.state::<AppState>();
            if let Err(err) = processes::supervise_session_inner(state.inner()) {
                eprintln!("session supervisor failed: {err}");
            }
        });
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
        .on_tray_icon_event(|tray, event| {
            let should_show = matches!(
                event,
                TrayIconEvent::Click {
                    button: MouseButton::Left,
                    button_state: MouseButtonState::Up,
                    ..
                } | TrayIconEvent::DoubleClick {
                    button: MouseButton::Left,
                    ..
                }
            );
            if should_show {
                let app = tray.app_handle();
                show_main_window(app.get_webview_window("main"));
            }
        })
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

fn apply_startup_default_off_migration(app: &tauri::AppHandle) -> Result<(), String> {
    let mut settings = paths::load_launcher_settings(app)?;
    if settings.startup_default_off_migration_applied {
        return Ok(());
    }

    let autolaunch = app.autolaunch();
    if autolaunch
        .is_enabled()
        .map_err(|err| format!("スタートアップ設定を取得できません: {err}"))?
    {
        autolaunch
            .disable()
            .map_err(|err| format!("スタートアップ解除に失敗しました: {err}"))?;
    }

    settings.startup_configured = true;
    settings.startup_default_off_migration_applied = true;
    paths::save_launcher_settings(app, &settings)
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
