use std::fs;
use std::io::{BufReader, Read};
use std::path::{Path, PathBuf};
use std::sync::{Mutex, OnceLock};
use tauri::AppHandle;

use crate::config::{app_version, REUSABLE_ROM_FORMAT, ROM_LOOP_ROLLBACK_CONTRACT};
use crate::models::{GenerateRomRequest, GenerateRomResponse, RomIdentity};
use crate::paths::{
    absolutize_existing, ensure_parent_dir, find_bridge_binary, find_symbols_file,
    fixed_generated_rom_paths,
};
use crate::save_bootstrap::{
    canonical_save_path, ensure_canonical_save, read_and_validate_canonical, validate_save_bytes,
    KNOWN_NSMB_US_ROM_SHA256,
};
use crate::settings::{course_mode_value, lives_value};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use uuid::Uuid;

const MANIFEST_VERSION: u8 = 4;
const ROM_GENERATOR_SOURCES: &[&str] = &[
    include_str!("../../../bigstar-rom/src/lib.rs"),
    include_str!("../../../bigstar-rom/src/binary.rs"),
    include_str!("config.rs"),
    include_str!("settings.rs"),
];

#[derive(Debug, Clone, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub(crate) struct RomManifestInputs {
    manifest_version: u8,
    rom_format: String,
    generator_id: String,
    source_rom_sha256: String,
    canonical_save_sha256: String,
    symbols_sha256: String,
    options: RomManifestOptions,
}

#[derive(Debug, Clone, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub(crate) struct RomManifestOptions {
    stage: u8,
    wins: u8,
    big_stars: u8,
    lives: String,
    course_mode: String,
    game_tick_probe: bool,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "snake_case")]
pub(crate) struct RomManifest {
    #[serde(flatten)]
    inputs: RomManifestInputs,
    identity: RomIdentity,
}

pub(crate) fn prepare_roms(
    app: &AppHandle,
    request: GenerateRomRequest,
    force: bool,
) -> Result<GenerateRomResponse, String> {
    let _prepare_guard = rom_prepare_lock()
        .lock()
        .map_err(|_| "ROM準備の排他状態が壊れています".to_owned())?;
    let (host_rom, client_rom) = fixed_generated_rom_paths(app)?;
    let source_rom = absolutize_existing(&request.source_rom)?;
    let source_rom_sha256 = sha256_file(&source_rom)?;
    let canonical_save = ensure_canonical_save(app, &source_rom, &source_rom_sha256)?;
    let canonical_save_sha256 = canonical_save.sha256.clone();
    let symbols = find_symbols_file(app)?;
    let bridge_sha256 = sha256_file(&find_bridge_binary(app)?)?;
    let inputs = RomManifestInputs {
        manifest_version: MANIFEST_VERSION,
        rom_format: REUSABLE_ROM_FORMAT.to_owned(),
        generator_id: rom_generator_id(),
        source_rom_sha256,
        canonical_save_sha256: canonical_save_sha256.clone(),
        symbols_sha256: sha256_file(&symbols)?,
        options: canonical_rom_options(),
    };

    ensure_parent_dir(&host_rom)?;
    ensure_parent_dir(&client_rom)?;

    if !force {
        if let Some(identity) = reusable_rom_identity(
            &host_rom,
            &client_rom,
            &inputs,
            &bridge_sha256,
            &canonical_save_sha256,
        )? {
            install_managed_save(&host_rom, &canonical_save.bytes)?;
            install_managed_save(&client_rom, &canonical_save.bytes)?;
            write_reusable_rom_manifest(
                &host_rom,
                &RomManifest {
                    inputs: inputs.clone(),
                    identity: identity.clone(),
                },
            )?;
            write_reusable_rom_manifest(
                &client_rom,
                &RomManifest {
                    inputs,
                    identity: identity.clone(),
                },
            )?;
            return Ok(GenerateRomResponse {
                host_rom: host_rom.to_string_lossy().into_owned(),
                client_rom: client_rom.to_string_lossy().into_owned(),
                generated: false,
                rom_identity: identity,
            });
        }
    }

    let options = bigstar_rom::StableRomOptions {
        source_rom,
        host_rom: host_rom.clone(),
        client_rom: client_rom.clone(),
        stage: inputs.options.stage,
        wins: inputs.options.wins,
        big_stars: inputs.options.big_stars,
        lives: inputs.options.lives.clone(),
        course_mode: inputs.options.course_mode.clone(),
        scene_settings: None,
        symbols,
        game_tick_probe: inputs.options.game_tick_probe,
    };

    bigstar_rom::generate_stable_roms(&options)
        .map_err(|err| format!("ROM生成に失敗しました: {err}"))?;
    install_managed_save(&host_rom, &canonical_save.bytes)?;
    install_managed_save(&client_rom, &canonical_save.bytes)?;
    let identity = rom_identity(
        &inputs.generator_id,
        &sha256_file(&host_rom)?,
        &sha256_file(&client_rom)?,
        &bridge_sha256,
        &canonical_save_sha256,
    );
    write_reusable_rom_manifest(
        &host_rom,
        &RomManifest {
            inputs: inputs.clone(),
            identity: identity.clone(),
        },
    )?;
    write_reusable_rom_manifest(
        &client_rom,
        &RomManifest {
            inputs,
            identity: identity.clone(),
        },
    )?;

    Ok(GenerateRomResponse {
        host_rom: host_rom.to_string_lossy().into_owned(),
        client_rom: client_rom.to_string_lossy().into_owned(),
        generated: true,
        rom_identity: identity,
    })
}

fn rom_prepare_lock() -> &'static Mutex<()> {
    static LOCK: OnceLock<Mutex<()>> = OnceLock::new();
    LOCK.get_or_init(|| Mutex::new(()))
}

pub(crate) fn prepared_roms_are_current(app: &AppHandle, source_rom: &str) -> bool {
    let Ok(source_rom) = absolutize_existing(source_rom) else {
        return false;
    };
    let Ok(source_rom_sha256) = sha256_file(&source_rom) else {
        return false;
    };
    let Ok(canonical_path) = canonical_save_path(app, &source_rom_sha256) else {
        return false;
    };
    let Ok(canonical) = read_and_validate_canonical(&canonical_path, &source_rom_sha256) else {
        return false;
    };
    let Ok(symbols) = find_symbols_file(app) else {
        return false;
    };
    let Ok(bridge) = find_bridge_binary(app) else {
        return false;
    };
    let Ok((host_rom, client_rom)) = fixed_generated_rom_paths(app) else {
        return false;
    };
    let inputs = RomManifestInputs {
        manifest_version: MANIFEST_VERSION,
        rom_format: REUSABLE_ROM_FORMAT.to_owned(),
        generator_id: rom_generator_id(),
        source_rom_sha256,
        canonical_save_sha256: canonical.sha256.clone(),
        symbols_sha256: match sha256_file(&symbols) {
            Ok(hash) => hash,
            Err(_) => return false,
        },
        options: canonical_rom_options(),
    };
    let bridge_sha256 = match sha256_file(&bridge) {
        Ok(hash) => hash,
        Err(_) => return false,
    };
    reusable_rom_identity(
        &host_rom,
        &client_rom,
        &inputs,
        &bridge_sha256,
        &canonical.sha256,
    )
    .is_ok_and(|identity| identity.is_some())
        && validate_rom_save(&host_rom).is_ok_and(|hash| hash == canonical.sha256)
        && validate_rom_save(&client_rom).is_ok_and(|hash| hash == canonical.sha256)
}

fn reusable_rom_identity(
    host_rom: &Path,
    client_rom: &Path,
    inputs: &RomManifestInputs,
    bridge_sha256: &str,
    save_sha256: &str,
) -> Result<Option<RomIdentity>, String> {
    if !host_rom.is_file() || !client_rom.is_file() {
        return Ok(None);
    }
    let Ok(host_manifest) = read_reusable_rom_manifest(host_rom) else {
        return Ok(None);
    };
    let Ok(client_manifest) = read_reusable_rom_manifest(client_rom) else {
        return Ok(None);
    };
    let host_rom_sha256 = sha256_file(host_rom)?;
    let client_rom_sha256 = sha256_file(client_rom)?;
    if host_manifest.inputs != *inputs
        || client_manifest.inputs != *inputs
        || host_manifest.identity.host_rom_sha256 != host_rom_sha256
        || host_manifest.identity.client_rom_sha256 != client_rom_sha256
        || host_manifest.identity.save_sha256 != save_sha256
        || client_manifest.identity.save_sha256 != save_sha256
    {
        return Ok(None);
    }
    Ok(Some(rom_identity(
        &inputs.generator_id,
        &host_rom_sha256,
        &client_rom_sha256,
        bridge_sha256,
        save_sha256,
    )))
}

fn read_reusable_rom_manifest(rom: &Path) -> Result<RomManifest, String> {
    let content = fs::read_to_string(reusable_rom_marker_path(rom))
        .map_err(|err| format!("ROM manifest を読み込めません: {err}"))?;
    serde_json::from_str(&content).map_err(|err| format!("ROM manifest の形式が不正です: {err}"))
}

fn canonical_rom_options() -> RomManifestOptions {
    RomManifestOptions {
        stage: 0,
        wins: 3,
        big_stars: 10,
        lives: lives_value(crate::models::Lives::Three).to_owned(),
        course_mode: course_mode_value(crate::models::CourseMode::Random).to_owned(),
        game_tick_probe: true,
    }
}

fn write_reusable_rom_manifest(rom: &Path, manifest: &RomManifest) -> Result<(), String> {
    let content = serde_json::to_string_pretty(manifest)
        .map_err(|err| format!("ROM manifest をJSON化できません: {err}"))?;
    fs::write(reusable_rom_marker_path(rom), format!("{content}\n"))
        .map_err(|err| format!("ROM manifest を保存できません: {err}"))
}

pub(crate) fn sha256_file(path: &Path) -> Result<String, String> {
    let file = fs::File::open(path)
        .map_err(|err| format!("{} を読み込めません: {err}", path.to_string_lossy()))?;
    let mut reader = BufReader::new(file);
    let mut hasher = Sha256::new();
    let mut buffer = [0_u8; 64 * 1024];
    loop {
        let read = reader
            .read(&mut buffer)
            .map_err(|err| format!("{} のhash計算に失敗しました: {err}", path.to_string_lossy()))?;
        if read == 0 {
            break;
        }
        hasher.update(&buffer[..read]);
    }
    Ok(format!("{:x}", hasher.finalize()))
}

pub(crate) fn sha256_bytes(bytes: &[u8]) -> String {
    let mut hasher = Sha256::new();
    hasher.update(bytes);
    format!("{:x}", hasher.finalize())
}

fn save_path_for_rom(rom: &Path) -> PathBuf {
    rom.with_extension("sav")
}

fn read_valid_managed_save(path: &Path) -> Result<Vec<u8>, String> {
    let bytes = fs::read(path).map_err(|err| {
        format!(
            "管理セーブ {} を読み込めません: {err}",
            path.to_string_lossy()
        )
    })?;
    validate_save_bytes(&bytes, KNOWN_NSMB_US_ROM_SHA256)
        .map_err(|err| format!("管理セーブ {} が不正です: {err}", path.display()))?;
    Ok(bytes)
}

fn install_managed_save(rom: &Path, bytes: &[u8]) -> Result<(), String> {
    let save = save_path_for_rom(rom);
    if fs::read(&save).is_ok_and(|current| current == bytes) {
        return validate_rom_save(rom).map(|_| ());
    }
    let temp = save.with_extension(format!("sav.tmp-{}", Uuid::new_v4()));
    let mut file = fs::File::create(&temp).map_err(|err| {
        format!(
            "管理セーブの一時ファイル {} を作成できません: {err}",
            temp.to_string_lossy()
        )
    })?;
    use std::io::Write;
    file.write_all(bytes)
        .and_then(|()| file.sync_all())
        .map_err(|err| format!("管理セーブの一時ファイルを書き込めません: {err}"))?;
    drop(file);
    if save.exists() {
        fs::remove_file(&save).map_err(|err| format!("古い管理セーブを置換できません: {err}"))?;
    }
    fs::rename(&temp, &save).map_err(|err| {
        format!(
            "管理セーブ {} を確定できません: {err}",
            save.to_string_lossy()
        )
    })?;
    validate_rom_save(rom).map(|_| ())
}

pub(crate) fn validate_rom_save(rom: &Path) -> Result<String, String> {
    read_valid_managed_save(&save_path_for_rom(rom)).map(|bytes| sha256_bytes(&bytes))
}

fn sha256_text(parts: &[&str]) -> String {
    let mut hasher = Sha256::new();
    for part in parts {
        hasher.update(part.as_bytes());
        hasher.update(b"\n");
    }
    format!("{:x}", hasher.finalize())
}

fn rom_generator_id() -> String {
    let mut parts = vec![
        app_version(),
        REUSABLE_ROM_FORMAT,
        ROM_LOOP_ROLLBACK_CONTRACT,
    ];
    parts.extend_from_slice(ROM_GENERATOR_SOURCES);
    sha256_text(&parts)
}

fn rom_identity(
    generator_id: &str,
    host_rom_sha256: &str,
    client_rom_sha256: &str,
    bridge_sha256: &str,
    save_sha256: &str,
) -> RomIdentity {
    RomIdentity {
        rom_pair_id: rom_pair_id(
            generator_id,
            host_rom_sha256,
            client_rom_sha256,
            bridge_sha256,
            save_sha256,
        ),
        generator_id: generator_id.to_owned(),
        host_rom_sha256: host_rom_sha256.to_owned(),
        client_rom_sha256: client_rom_sha256.to_owned(),
        bridge_sha256: bridge_sha256.to_owned(),
        save_sha256: save_sha256.to_owned(),
    }
}

fn rom_pair_id(
    generator_id: &str,
    host_rom_sha256: &str,
    client_rom_sha256: &str,
    bridge_sha256: &str,
    save_sha256: &str,
) -> String {
    sha256_text(&[
        generator_id,
        host_rom_sha256,
        client_rom_sha256,
        bridge_sha256,
        save_sha256,
    ])
}

pub(crate) fn reusable_rom_marker_path(rom: &Path) -> PathBuf {
    let mut marker = rom.as_os_str().to_owned();
    marker.push(".bigstar-version");
    PathBuf::from(marker)
}

#[cfg(test)]
pub(crate) fn reusable_rom_is_current(rom: &Path) -> bool {
    rom.is_file()
        && read_reusable_rom_manifest(rom).is_ok_and(|manifest| {
            manifest.inputs.manifest_version == MANIFEST_VERSION
                && manifest.inputs.rom_format == REUSABLE_ROM_FORMAT
        })
}

#[cfg(test)]
pub(crate) fn write_reusable_rom_marker(rom: &Path) -> Result<(), String> {
    let fake_sha = sha256_text(&[&rom.to_string_lossy()]);
    let manifest = RomManifest {
        inputs: RomManifestInputs {
            manifest_version: MANIFEST_VERSION,
            rom_format: REUSABLE_ROM_FORMAT.to_owned(),
            generator_id: rom_generator_id(),
            source_rom_sha256: fake_sha.clone(),
            canonical_save_sha256: fake_sha.clone(),
            symbols_sha256: fake_sha.clone(),
            options: RomManifestOptions {
                stage: 0,
                wins: 1,
                big_stars: 5,
                lives: "3".to_owned(),
                course_mode: "select".to_owned(),
                game_tick_probe: true,
            },
        },
        identity: RomIdentity {
            rom_pair_id: fake_sha.clone(),
            generator_id: rom_generator_id(),
            host_rom_sha256: fake_sha.clone(),
            client_rom_sha256: fake_sha.clone(),
            bridge_sha256: fake_sha,
            save_sha256: sha256_text(&["save"]),
        },
    };
    write_reusable_rom_manifest(rom, &manifest)
}
