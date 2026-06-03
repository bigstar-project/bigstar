use std::fs;
use std::path::{Path, PathBuf};
use tauri::AppHandle;

use crate::config::REUSABLE_ROM_FORMAT;
use crate::models::{GenerateRomRequest, GenerateRomResponse};
use crate::paths::{absolutize_existing, absolutize_target, ensure_parent_dir, find_symbols_file};
use crate::settings::{course_mode_value, lives_value, selected_stage, validate_settings};

pub(crate) fn prepare_roms(
    app: &AppHandle,
    request: GenerateRomRequest,
    force: bool,
) -> Result<GenerateRomResponse, String> {
    validate_settings(&request.settings)?;
    let stage = selected_stage(&request.settings, request.stage)?;
    let host_rom = absolutize_target(app, &request.host_rom)?;
    let client_rom = absolutize_target(app, &request.client_rom)?;
    if !force && reusable_rom_is_current(&host_rom) && reusable_rom_is_current(&client_rom) {
        return Ok(GenerateRomResponse {
            host_rom: host_rom.to_string_lossy().into_owned(),
            client_rom: client_rom.to_string_lossy().into_owned(),
            generated: false,
        });
    }

    let source_rom = absolutize_existing(&request.source_rom)?;
    ensure_parent_dir(&host_rom)?;
    ensure_parent_dir(&client_rom)?;
    let options = nsmb_mvl_rom::StableRomOptions {
        source_rom,
        host_rom: host_rom.clone(),
        client_rom: client_rom.clone(),
        stage,
        wins: request.settings.wins,
        big_stars: request.settings.big_stars,
        lives: lives_value(request.settings.lives).to_owned(),
        course_mode: course_mode_value(request.settings.course_mode).to_owned(),
        scene_settings: None,
        symbols: find_symbols_file(app)?,
    };

    nsmb_mvl_rom::generate_stable_roms(&options)
        .map_err(|err| format!("ROM生成に失敗しました: {err}"))?;
    write_reusable_rom_marker(&host_rom)?;
    write_reusable_rom_marker(&client_rom)?;

    Ok(GenerateRomResponse {
        host_rom: host_rom.to_string_lossy().into_owned(),
        client_rom: client_rom.to_string_lossy().into_owned(),
        generated: true,
    })
}

pub(crate) fn reusable_rom_marker_path(rom: &Path) -> PathBuf {
    let mut marker = rom.as_os_str().to_owned();
    marker.push(".nsmb-mvl-version");
    PathBuf::from(marker)
}

pub(crate) fn reusable_rom_is_current(rom: &Path) -> bool {
    rom.is_file()
        && fs::read_to_string(reusable_rom_marker_path(rom))
            .is_ok_and(|version| version.trim() == REUSABLE_ROM_FORMAT)
}

pub(crate) fn write_reusable_rom_marker(rom: &Path) -> Result<(), String> {
    fs::write(
        reusable_rom_marker_path(rom),
        format!("{REUSABLE_ROM_FORMAT}\n"),
    )
    .map_err(|err| format!("ROM形式 marker を保存できません: {err}"))
}
