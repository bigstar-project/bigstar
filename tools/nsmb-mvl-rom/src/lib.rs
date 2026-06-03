use anyhow::{anyhow, bail, Context, Result};
use ds_rom::{compress::lz77::Lz77, crc::CRC_16_MODBUS};
use std::{
    collections::{BTreeMap, BTreeSet},
    fs,
    path::{Path, PathBuf},
};

const BX_LR: u32 = 0xE12F_FF1E;
const POP_PC: u32 = 0xE8BD_8000;
const NOP: u32 = 0xE1A0_0000;
const ARM9_COMPRESSION_START: usize = 0x4000;
const ARM9_FOOTER_SIZE: usize = 12;
const HEADER_SIZE: usize = 0x4000;
const MVL_RUNTIME_CONFIG_ADDR: u32 = 0x020C_5360;
const MVL_RUNTIME_CONFIG_MAGIC: u32 = 0x434C_564D; // "MVLC", little endian
const MVL_RUNTIME_CONFIG_STAGE_OFFSET: u32 = 0x04;
const MVL_RUNTIME_CONFIG_SCENE_SETTINGS_OFFSET: u32 = 0x08;
const MVL_RUNTIME_CONFIG_INITIAL_LIVES_OFFSET: u32 = 0x0C;
const MVL_RUNTIME_CONFIG_LIFE_MODE_SELECTOR_OFFSET: u32 = 0x10;
const MVL_RUNTIME_CONFIG_BIG_STAR_SELECTOR_OFFSET: u32 = 0x14;

#[derive(Debug, Clone)]
pub struct StableRomOptions {
    pub source_rom: PathBuf,
    pub host_rom: PathBuf,
    pub client_rom: PathBuf,
    pub stage: u8,
    pub wins: u8,
    pub big_stars: u8,
    pub lives: String,
    pub course_mode: String,
    pub scene_settings: Option<String>,
    pub symbols: PathBuf,
}

#[derive(Debug)]
pub struct StableRomResult {
    pub host_rom: PathBuf,
    pub client_rom: PathBuf,
}

#[derive(Clone)]
struct RomImage {
    header: Vec<u8>,
    arm9_ram: u32,
    arm9_entry: u32,
    arm9_autoload_callback: u32,
    arm9_code_settings_pointer: u32,
    arm9_footer: [u8; ARM9_FOOTER_SIZE],
    arm9: Vec<u8>,
    arm9_sections: Vec<Arm9Section>,
    arm7: Vec<u8>,
    arm7_ram: u32,
    arm7_entry: u32,
    arm7_autoload_callback: u32,
    fnt: Vec<u8>,
    files: Vec<Vec<u8>>,
    overlays: Vec<Overlay>,
    arm7_overlay_table: Vec<u8>,
    banner: Vec<u8>,
    original_len: usize,
}

#[derive(Clone)]
struct Arm9Section {
    ram_addr: u32,
    file_off: usize,
    size: usize,
}

#[derive(Clone)]
struct Overlay {
    id: u32,
    base_addr: u32,
    code_size: u32,
    bss_size: u32,
    ctor_start: u32,
    ctor_end: u32,
    file_id: u32,
    flags: u32,
    data: Vec<u8>,
}

pub fn generate_stable_roms(options: &StableRomOptions) -> Result<StableRomResult> {
    if !options.source_rom.exists() {
        bail!("source ROM not found: {}", options.source_rom.display());
    }
    validate_game_settings(
        options.wins,
        options.big_stars,
        &options.lives,
        &options.course_mode,
    )?;
    let scene_settings = match &options.scene_settings {
        Some(value) if !value.trim().is_empty() => parse_u32(value)?,
        _ => stage_scene_settings(options.stage)?,
    };
    let initial_lives = initial_lives(&options.lives)?;
    let life_mode_selector = life_mode_selector(&options.lives)?;
    let big_star_selector = big_star_selector(options.big_stars)?;
    let symbols = load_symbols(&options.symbols)?;
    let base = RomImage::load(&options.source_rom)?;

    let mut host = base.clone();
    patch_direct_mvl_entry(
        &mut host,
        &symbols,
        options.stage,
        0,
        scene_settings,
        initial_lives,
        life_mode_selector,
        big_star_selector,
    )?;
    patch_wifi_communicating_consoles(&mut host, 2)?;
    host.save(&options.host_rom)?;

    let mut client = base;
    patch_direct_mvl_entry(
        &mut client,
        &symbols,
        options.stage,
        1,
        scene_settings,
        initial_lives,
        life_mode_selector,
        big_star_selector,
    )?;
    patch_wifi_communicating_consoles(&mut client, 2)?;
    client.save(&options.client_rom)?;

    Ok(StableRomResult {
        host_rom: options.host_rom.clone(),
        client_rom: options.client_rom.clone(),
    })
}

fn validate_game_settings(wins: u8, big_stars: u8, lives: &str, course_mode: &str) -> Result<()> {
    if !(1..=3).contains(&wins) {
        bail!("wins must be 1, 2, or 3: {wins}");
    }
    match big_stars {
        3 | 5 | 10 => {}
        _ => bail!("big stars must be 3, 5, or 10: {big_stars}"),
    }
    match lives.to_ascii_lowercase().as_str() {
        "3" | "5" | "endless" => {}
        _ => bail!("lives must be 3, 5, or endless: {lives}"),
    }
    match course_mode.to_ascii_lowercase().as_str() {
        "random" | "select" => {}
        _ => bail!("course mode must be random or select: {course_mode}"),
    }
    Ok(())
}

pub fn stage_scene_settings(stage: u8) -> Result<u32> {
    if stage > 4 {
        bail!("stage must be between 0 and 4: {stage}");
    }
    Ok(((0xb4u32 + stage as u32) << 16) | 0xff00)
}

fn initial_lives(lives: &str) -> Result<u32> {
    match lives.to_ascii_lowercase().as_str() {
        "3" => Ok(3),
        "5" => Ok(5),
        "endless" => Ok(3),
        _ => bail!("lives must be 3, 5, or endless: {lives}"),
    }
}

fn life_mode_selector(lives: &str) -> Result<u32> {
    match lives.to_ascii_lowercase().as_str() {
        "3" | "5" => Ok(0),
        "endless" => Ok(2),
        _ => bail!("lives must be 3, 5, or endless: {lives}"),
    }
}

fn big_star_selector(big_stars: u8) -> Result<u32> {
    match big_stars {
        3 => Ok(0),
        5 => Ok(1),
        10 => Ok(2),
        _ => bail!("big stars must be 3, 5, or 10: {big_stars}"),
    }
}

impl RomImage {
    fn load(path: &Path) -> Result<Self> {
        let data = fs::read(path).with_context(|| format!("read ROM {}", path.display()))?;
        if data.len() < HEADER_SIZE {
            bail!("ROM is too small: {}", path.display());
        }

        let arm9_off = read_u32(&data, 0x20)? as usize;
        let arm9_entry = read_u32(&data, 0x24)?;
        let arm9_ram = read_u32(&data, 0x28)?;
        let arm9_size = read_u32(&data, 0x2c)? as usize;
        let arm7_off = read_u32(&data, 0x30)? as usize;
        let arm7_entry = read_u32(&data, 0x34)?;
        let arm7_ram = read_u32(&data, 0x38)?;
        let arm7_size = read_u32(&data, 0x3c)? as usize;
        let fnt_off = read_u32(&data, 0x40)? as usize;
        let fnt_size = read_u32(&data, 0x44)? as usize;
        let fat_off = read_u32(&data, 0x48)? as usize;
        let fat_size = read_u32(&data, 0x4c)? as usize;
        let arm9_ovt_off = read_u32(&data, 0x50)? as usize;
        let arm9_ovt_size = read_u32(&data, 0x54)? as usize;
        let arm7_ovt_off = read_u32(&data, 0x58)? as usize;
        let arm7_ovt_size = read_u32(&data, 0x5c)? as usize;
        let arm9_autoload_callback = read_u32(&data, 0x60)?;
        let arm9_code_settings_pointer = read_u32(&data, 0x70)?;
        let arm7_autoload_callback = read_u32(&data, 0x64)?;
        let banner_off = read_u32(&data, 0x68)? as usize;

        let arm9_raw = slice(&data, arm9_off, arm9_size, "ARM9")?;
        let arm9_footer_slice =
            slice(&data, arm9_off + arm9_size, ARM9_FOOTER_SIZE, "ARM9 footer")?;
        let mut arm9_footer = [0u8; ARM9_FOOTER_SIZE];
        arm9_footer.copy_from_slice(arm9_footer_slice);
        let arm9 = Lz77 {}
            .decompress(arm9_raw)
            .map(|v| v.into_vec())
            .context("decompress ARM9")?;
        let arm9_sections = parse_arm9_sections(&arm9, arm9_ram, arm9_code_settings_pointer)?;

        let file_count = fat_size / 8;
        let mut files = Vec::with_capacity(file_count);
        for file_id in 0..file_count {
            let entry = fat_off + file_id * 8;
            let start = read_u32(&data, entry)? as usize;
            let end = read_u32(&data, entry + 4)? as usize;
            files.push(slice(&data, start, end.saturating_sub(start), "file")?.to_vec());
        }

        let mut overlays = Vec::new();
        for off in (arm9_ovt_off..arm9_ovt_off + arm9_ovt_size).step_by(0x20) {
            let file_id = read_u32(&data, off + 0x18)?;
            let flags = read_u32(&data, off + 0x1c)?;
            let raw = files
                .get(file_id as usize)
                .ok_or_else(|| anyhow!("overlay file id out of range: {file_id}"))?;
            let compressed = (flags & (1 << 24)) != 0;
            let overlay_id = read_u32(&data, off)?;
            let overlay_data = if compressed {
                Lz77 {}
                    .decompress(raw)
                    .map(|v| v.into_vec())
                    .with_context(|| format!("decompress overlay {overlay_id}"))?
            } else {
                raw.clone()
            };
            overlays.push(Overlay {
                id: overlay_id,
                base_addr: read_u32(&data, off + 0x04)?,
                code_size: read_u32(&data, off + 0x08)?,
                bss_size: read_u32(&data, off + 0x0c)?,
                ctor_start: read_u32(&data, off + 0x10)?,
                ctor_end: read_u32(&data, off + 0x14)?,
                file_id,
                flags,
                data: overlay_data,
            });
        }

        Ok(Self {
            header: data[..HEADER_SIZE].to_vec(),
            arm9_ram,
            arm9_entry,
            arm9_autoload_callback,
            arm9_code_settings_pointer,
            arm9_footer,
            arm9,
            arm9_sections,
            arm7: slice(&data, arm7_off, arm7_size, "ARM7")?.to_vec(),
            arm7_ram,
            arm7_entry,
            arm7_autoload_callback,
            fnt: slice(&data, fnt_off, fnt_size, "FNT")?.to_vec(),
            files,
            overlays,
            arm7_overlay_table: if arm7_ovt_size == 0 {
                Vec::new()
            } else {
                slice(&data, arm7_ovt_off, arm7_ovt_size, "ARM7 overlay table")?.to_vec()
            },
            banner: read_banner(&data, banner_off)?,
            original_len: data.len(),
        })
    }

    fn save(&self, path: &Path) -> Result<()> {
        let mut output = vec![0u8; HEADER_SIZE];
        let overlay_file_ids: BTreeSet<u32> = self
            .overlays
            .iter()
            .map(|overlay| overlay.file_id)
            .collect();
        let mut fat = vec![(0u32, 0u32); self.files.len()];
        let mut arm9 = self.arm9.clone();
        let arm9_build_info = self.arm9_build_info_offset()?;

        let arm9_off = align_vec(&mut output, 0x200, 0);
        let compressed_arm9 =
            compress_arm9_with_build_info(&mut arm9, arm9_build_info, self.arm9_ram)?;
        output.extend_from_slice(&compressed_arm9);
        output.extend_from_slice(&self.arm9_footer);
        let arm9_size = compressed_arm9.len() as u32;

        let arm9_ovt_off = align_vec(&mut output, 0x200, 0);
        let mut overlay_payloads = Vec::with_capacity(self.overlays.len());
        for overlay in &self.overlays {
            overlay_payloads.push(if overlay.compressed() {
                Lz77 {}
                    .compress(&overlay.data, 0)
                    .with_context(|| format!("compress overlay {}", overlay.id))?
                    .into_vec()
            } else {
                overlay.data.clone()
            });
        }
        let mut overlay_table = Vec::with_capacity(self.overlays.len() * 0x20);
        for (overlay, data) in self.overlays.iter().zip(&overlay_payloads) {
            append_overlay_entry(&mut overlay_table, overlay, data.len() as u32);
        }
        output.extend_from_slice(&overlay_table);

        for (overlay, data) in self.overlays.iter().zip(&overlay_payloads) {
            let file_id = overlay.file_id as usize;
            let start = align_vec(&mut output, 0x200, 0);
            output.extend_from_slice(&data);
            fat[file_id] = (start as u32, output.len() as u32);
        }

        let arm7_off = align_vec(&mut output, 0x200, 0);
        output.extend_from_slice(&self.arm7);

        let arm7_ovt_off = if self.arm7_overlay_table.is_empty() {
            0
        } else {
            let off = align_vec(&mut output, 0x200, 0);
            output.extend_from_slice(&self.arm7_overlay_table);
            off
        };

        let fnt_off = align_vec(&mut output, 0x200, 0);
        output.extend_from_slice(&self.fnt);

        let fat_off = align_vec(&mut output, 0x200, 0);
        let fat_size = (fat.len() * 8) as u32;
        output.resize(output.len() + fat_size as usize, 0);

        let banner_off = align_vec(&mut output, 0x200, 0);
        output.extend_from_slice(&self.banner);

        for file_id in 0..self.files.len() {
            if overlay_file_ids.contains(&(file_id as u32)) {
                continue;
            }
            let start = align_vec(&mut output, 0x200, 0xff);
            output.extend_from_slice(&self.files[file_id]);
            fat[file_id] = (start as u32, output.len() as u32);
        }

        let rom_size = output.len() as u32;
        let padded_len = output
            .len()
            .next_power_of_two()
            .max(self.original_len.next_power_of_two());
        output.resize(padded_len, 0xff);

        for (i, (start, end)) in fat.iter().enumerate() {
            write_u32(&mut output, fat_off + i * 8, *start)?;
            write_u32(&mut output, fat_off + i * 8 + 4, *end)?;
        }

        output[..HEADER_SIZE].copy_from_slice(&self.header);
        write_u32(&mut output, 0x20, arm9_off as u32)?;
        write_u32(&mut output, 0x24, self.arm9_entry)?;
        write_u32(&mut output, 0x28, self.arm9_ram)?;
        write_u32(&mut output, 0x2c, arm9_size)?;
        write_u32(&mut output, 0x30, arm7_off as u32)?;
        write_u32(&mut output, 0x34, self.arm7_entry)?;
        write_u32(&mut output, 0x38, self.arm7_ram)?;
        write_u32(&mut output, 0x3c, self.arm7.len() as u32)?;
        write_u32(&mut output, 0x40, fnt_off as u32)?;
        write_u32(&mut output, 0x44, self.fnt.len() as u32)?;
        write_u32(&mut output, 0x48, fat_off as u32)?;
        write_u32(&mut output, 0x4c, fat_size)?;
        write_u32(&mut output, 0x50, arm9_ovt_off as u32)?;
        write_u32(&mut output, 0x54, overlay_table.len() as u32)?;
        write_u32(&mut output, 0x58, arm7_ovt_off as u32)?;
        write_u32(&mut output, 0x5c, self.arm7_overlay_table.len() as u32)?;
        write_u32(&mut output, 0x60, self.arm9_autoload_callback)?;
        write_u32(&mut output, 0x64, self.arm7_autoload_callback)?;
        write_u32(&mut output, 0x68, banner_off as u32)?;
        write_u32(&mut output, 0x70, self.arm9_code_settings_pointer)?;
        write_u32(&mut output, 0x80, rom_size)?;
        let header_crc = CRC_16_MODBUS.checksum(&output[..0x15e]);
        write_u16(&mut output, 0x15e, header_crc)?;

        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent).with_context(|| format!("create {}", parent.display()))?;
        }
        fs::write(path, output).with_context(|| format!("write ROM {}", path.display()))
    }

    fn arm9_build_info_offset(&self) -> Result<usize> {
        let offset = read_u32(&self.arm9_footer, 4)? as usize;
        if offset + 0x24 > self.arm9.len() {
            bail!("ARM9 build info offset out of range: 0x{offset:x}");
        }
        Ok(offset)
    }
}

fn parse_arm9_sections(
    data: &[u8],
    ram_addr: u32,
    code_settings_pointer_addr: u32,
) -> Result<Vec<Arm9Section>> {
    let code_settings_pointer_off = code_settings_pointer_addr
        .checked_sub(ram_addr + 4)
        .ok_or_else(|| {
            anyhow!("ARM9 code settings pointer before base: 0x{code_settings_pointer_addr:08x}")
        })? as usize;
    let Ok(code_settings_addr) = read_u32(data, code_settings_pointer_off) else {
        return Ok(vec![Arm9Section {
            ram_addr,
            file_off: 0,
            size: data.len(),
        }]);
    };
    let code_settings_off = match code_settings_addr.checked_sub(ram_addr) {
        Some(off) if off as usize + 12 <= data.len() => off as usize,
        _ => {
            return Ok(vec![Arm9Section {
                ram_addr,
                file_off: 0,
                size: data.len(),
            }])
        }
    };

    let copy_table_begin = read_u32(data, code_settings_off)?.saturating_sub(ram_addr) as usize;
    let copy_table_end = read_u32(data, code_settings_off + 4)?.saturating_sub(ram_addr) as usize;
    let mut data_begin = read_u32(data, code_settings_off + 8)?.saturating_sub(ram_addr) as usize;
    if copy_table_begin > copy_table_end || copy_table_end > data.len() || data_begin > data.len() {
        return Ok(vec![Arm9Section {
            ram_addr,
            file_off: 0,
            size: data.len(),
        }]);
    }

    let mut sections = vec![Arm9Section {
        ram_addr,
        file_off: 0,
        size: data_begin,
    }];
    for entry in (copy_table_begin..copy_table_end).step_by(12) {
        if entry + 12 > data.len() {
            bail!("ARM9 copy table entry out of range: 0x{entry:x}");
        }
        let section_ram = read_u32(data, entry)?;
        let section_size = read_u32(data, entry + 4)? as usize;
        if data_begin + section_size > data.len() {
            bail!("ARM9 section out of range: off=0x{data_begin:x} size=0x{section_size:x}");
        }
        sections.push(Arm9Section {
            ram_addr: section_ram,
            file_off: data_begin,
            size: section_size,
        });
        data_begin += section_size;
    }
    Ok(sections)
}

fn compress_arm9_with_build_info(
    arm9: &mut [u8],
    build_info: usize,
    ram_base: u32,
) -> Result<Vec<u8>> {
    let mut compressed = Lz77 {}
        .compress(arm9, ARM9_COMPRESSION_START)
        .context("compress ARM9")?
        .into_vec();
    for _ in 0..3 {
        let end = ram_base + compressed.len() as u32;
        if read_u32(arm9, build_info + 0x14)? == end {
            return Ok(compressed);
        }
        write_u32(arm9, build_info + 0x14, end)?;
        compressed = Lz77 {}
            .compress(arm9, ARM9_COMPRESSION_START)
            .context("compress ARM9 after build-info update")?
            .into_vec();
    }
    Ok(compressed)
}

fn patch_direct_mvl_entry(
    rom: &mut RomImage,
    symbols: &BTreeMap<String, u32>,
    stage: u8,
    player_id: u8,
    scene_settings: u32,
    initial_lives: u32,
    life_mode_selector: u32,
    big_star_selector: u32,
) -> Result<()> {
    patch_arm9_words(rom, 0x0201_3428, &[encode_mov_imm(12, 6)?])?;

    patch_overlay_words(
        rom,
        0x0215_9348,
        &[symbol(symbols, "_ZN14VSConnectScene10loadGameSME")?],
    )?;

    let update_addr = symbol(symbols, "_ZN14VSConnectScene16updateLoadGameSMEv")?;
    let stub = build_direct_loadlevel_stub(
        update_addr,
        symbol(symbols, "_ZN4Game9loadLevelEtmhhhhhhhhhhhhhhm")?,
        symbol(symbols, "_ZN14VSConnectScene19loadMvsLFilesThreadEv")?,
        stage,
        player_id,
        scene_settings,
        initial_lives,
        life_mode_selector,
        big_star_selector,
    )?;
    patch_overlay_words(rom, update_addr, &stub)?;

    patch_overlay_words(rom, 0x0215_2888, &[NOP])?;
    patch_arm9_words(
        rom,
        symbol(symbols, "_ZN3Net4Core14transferPacketENS_12PacketActionE")?,
        &[encode_mov_imm(0, 8)?, BX_LR],
    )?;
    patch_overlay_words(rom, 0x0215_2E64, &[encode_mov_imm(1, 0)?])?;
    patch_mvl_load_thread_entrance_ids(rom)?;
    patch_is_out_of_view_vertical_camera_fallback(rom)?;
    patch_camera_focus_loop_count(rom, 2)?;
    patch_player_stage_lock_vsmode_noop(rom)?;
    Ok(())
}

fn build_direct_loadlevel_stub(
    start_addr: u32,
    load_level_addr: u32,
    load_mvl_files_after_addr: u32,
    stage: u8,
    player_id: u8,
    force_scene_settings: u32,
    initial_lives: u32,
    life_mode_selector: u32,
    big_star_selector: u32,
) -> Result<Vec<u32>> {
    let stack_values = [
        0,                // act
        player_id as u32, // playerID
        3,                // playerMask
        0,                // character1
        1,                // character2
        0,                // powerup
        0xff,             // entrance
        1,                // flag
        0,                // unused1
        0,                // controlOptions
        0,                // unused2
        0,                // challengeMode
        0xffff_ffff,      // rngSeed: use network/random state
    ];

    let mut words = vec![
        encode_push((1 << 4) | (1 << 14)),
        encode_mov_reg(4, 0),
        encode_ldr_imm(12, 4, 0x16c)?,
        encode_cmp_imm(12, 0x77)?,
        with_cond(encode_mov_imm(0, 1)?, 0),
        with_cond(POP_PC | (1 << 4), 0),
        encode_mov_imm(12, 0x77)?,
        encode_str_imm(12, 4, 0x16c)?,
        encode_sub_sp_imm(0x38)?,
    ];

    let mut literals: Vec<u32> = Vec::new();
    let mut literal_refs: Vec<(usize, u8, usize, u8)> = Vec::new();
    let mut emit_ldr_literal = |words: &mut Vec<u32>, rd: u8, value: u32, cond: u8| {
        let literal_index = literals.len();
        literals.push(value);
        let word_index = words.len();
        words.push(0);
        literal_refs.push((word_index, rd, literal_index, cond));
    };

    emit_ldr_literal(&mut words, 4, MVL_RUNTIME_CONFIG_ADDR, 0xE);
    words.push(encode_ldr_imm(12, 4, 0)?);
    emit_ldr_literal(&mut words, 0, MVL_RUNTIME_CONFIG_MAGIC, 0xE);
    words.push(encode_cmp_reg(12, 0));
    words.push(encode_mov_imm(0, 0x0f)?);
    words.push(encode_mov_imm(1, 1)?);
    words.push(encode_mov_imm(2, 9)?);
    words.push(encode_mov_imm(3, stage as u32)?);
    words.push(with_cond(
        encode_ldr_imm(3, 4, MVL_RUNTIME_CONFIG_STAGE_OFFSET)?,
        0,
    ));

    let mut current_ip_value = None;
    for (i, value) in stack_values.iter().enumerate() {
        if current_ip_value != Some(*value) {
            words.push(encode_load_imm(12, *value)?);
            current_ip_value = Some(*value);
        }
        words.push(encode_str_imm(12, 13, (i * 4) as u32)?);
    }

    let bl_addr = start_addr + words.len() as u32 * 4;
    words.push(encode_bl(bl_addr, load_level_addr)?);
    let bl_load_files_addr = start_addr + words.len() as u32 * 4;
    words.push(encode_bl(bl_load_files_addr, load_mvl_files_after_addr)?);

    words.push(encode_ldr_imm(12, 4, 0)?);
    emit_ldr_literal(&mut words, 2, MVL_RUNTIME_CONFIG_MAGIC, 0xE);
    words.push(encode_cmp_reg(12, 2));

    emit_ldr_literal(&mut words, 0, 0x0208_8F38, 0xE);
    emit_ldr_literal(&mut words, 1, force_scene_settings, 0xE);
    words.push(with_cond(
        encode_ldr_imm(1, 4, MVL_RUNTIME_CONFIG_SCENE_SETTINGS_OFFSET)?,
        0,
    ));
    words.push(encode_str_imm(1, 0, 0)?);

    emit_ldr_literal(&mut words, 0, 0x0208_B364, 0xE);
    words.push(encode_load_imm(1, initial_lives)?);
    words.push(with_cond(
        encode_ldr_imm(1, 4, MVL_RUNTIME_CONFIG_INITIAL_LIVES_OFFSET)?,
        0,
    ));
    words.push(encode_str_imm(1, 0, 0)?);
    words.push(encode_str_imm(1, 0, 4)?);

    emit_ldr_literal(&mut words, 0, 0x0215_C89C, 0xE);
    words.push(encode_mov_imm(1, life_mode_selector)?);
    words.push(with_cond(
        encode_ldr_imm(1, 4, MVL_RUNTIME_CONFIG_LIFE_MODE_SELECTOR_OFFSET)?,
        0,
    ));
    words.push(encode_strb_imm(1, 0, 0)?);

    emit_ldr_literal(&mut words, 0, 0x0215_C88C, 0xE);
    words.push(encode_mov_imm(1, big_star_selector)?);
    words.push(with_cond(
        encode_ldr_imm(1, 4, MVL_RUNTIME_CONFIG_BIG_STAR_SELECTOR_OFFSET)?,
        0,
    ));
    words.push(encode_strb_imm(1, 0, 0)?);

    emit_ldr_literal(&mut words, 0, 0x0208_B094, 0xE);
    words.push(encode_mov_imm(1, 0)?);
    words.push(encode_strb_imm(1, 0, 0)?);
    words.push(encode_mov_imm(1, 1)?);
    words.push(encode_strb_imm(1, 0, 1)?);
    emit_ldr_literal(&mut words, 0, 0x0208_B098, 0xE);
    words.push(encode_mov_imm(1, 0)?);
    words.push(encode_strb_imm(1, 0, 0)?);
    words.push(encode_strb_imm(1, 0, 1)?);
    emit_ldr_literal(&mut words, 0, 0x0208_B0A0, 0xE);
    words.push(encode_ldr_imm(1, 0, 0)?);
    words.push(encode_add_imm(2, 1, 0x14)?);
    words.push(encode_str_imm(2, 0, 4)?);
    emit_ldr_literal(&mut words, 0, 0x0208_87F0, 0xE);
    words.push(encode_mov_imm(1, (player_id & 3) as u32)?);
    words.push(encode_str_imm(1, 0, 0)?);
    words.push(encode_add_sp_imm(0x38)?);
    words.push(encode_mov_imm(0, 1)?);
    words.push(POP_PC | (1 << 4));

    let literal_start_addr = start_addr + words.len() as u32 * 4;
    for (word_index, rd, literal_index, cond) in literal_refs {
        let instruction_addr = start_addr + word_index as u32 * 4;
        let literal_addr = literal_start_addr + literal_index as u32 * 4;
        words[word_index] = encode_ldr_pc_literal(rd, instruction_addr, literal_addr, cond)?;
    }
    words.extend(literals);
    Ok(words)
}

fn patch_wifi_communicating_consoles(rom: &mut RomImage, count: u8) -> Result<()> {
    if !(1..=4).contains(&count) {
        bail!("communicating console count must be 1..4, got {count}");
    }
    patch_arm9_words(rom, 0x0204_6C34, &[encode_mov_imm(0, count as u32)?, BX_LR])?;
    patch_arm9_words(
        rom,
        0x0204_6C44,
        &[
            encode_cmp_imm(0, count as u32)?,
            with_cond(encode_mov_imm(0, 1)?, 3),
            with_cond(encode_mov_imm(0, 0)?, 2),
            BX_LR,
        ],
    )?;
    Ok(())
}

fn patch_mvl_load_thread_entrance_ids(rom: &mut RomImage) -> Result<()> {
    for (addr, word) in [
        (0x0215_2D64, encode_mov_imm(0, 1)?),
        (0x0215_2D68, encode_strb_imm(0, 13, 0x1d)?),
        (0x0215_2D74, encode_strb_imm(0, 13, 0x1c)?),
        (0x0215_2DC0, encode_mov_imm(0, 0)?),
        (0x0215_2DC8, encode_mov_imm(0, 1)?),
        (0x0215_2E00, encode_mov_imm(0, 0)?),
        (0x0215_2E0C, encode_mov_imm(0, 1)?),
    ] {
        patch_overlay_words(rom, addr, &[word])?;
    }
    Ok(())
}

fn patch_is_out_of_view_vertical_camera_fallback(rom: &mut RomImage) -> Result<()> {
    let stub_addr = 0x020C_5298;
    let stub = build_is_out_of_view_vertical_camera_fallback_stub(stub_addr)?;
    ensure_zero_overlay_words(rom, 0, stub_addr, stub.len())?;
    patch_overlay_words_by_id(rom, 0, stub_addr, &stub)?;
    patch_overlay_words_by_id(rom, 0, 0x020A_06DC, &[encode_b(0x020A_06DC, stub_addr)?])?;
    Ok(())
}

fn build_is_out_of_view_vertical_camera_fallback_stub(start_addr: u32) -> Result<Vec<u32>> {
    let mut words = Vec::new();
    let mut literals: Vec<u32> = Vec::new();
    let mut refs: Vec<(usize, u8, usize, u8)> = Vec::new();
    let mut emit = |words: &mut Vec<u32>, rd: u8, value: u32, cond: u8| {
        let literal_index = literals.len();
        literals.push(value);
        let word_index = words.len();
        words.push(0);
        refs.push((word_index, rd, literal_index, cond));
    };

    words.push(encode_push((1 << 4) | (1 << 14)));
    words.push(encode_cmp_imm(2, 1)?);
    words.push(with_cond(encode_mov_imm(2, 0)?, 0));
    emit(&mut words, 3, 0x020C_AD8C, 0xE);
    words.push(encode_ldr_reg_lsl(12, 3, 2, 2)?);
    words.push(encode_cmp_imm(12, 0)?);
    words.push(with_cond(encode_mov_imm(2, 0)?, 0));
    emit(&mut words, 12, 0x020C_AD94, 0xE);
    words.push(encode_ldr_imm(14, 1, 4)?);
    emit(&mut words, 3, 0x020C_AD8C, 0xE);
    words.push(encode_ldr_imm(4, 0, 0x64)?);
    words.push(encode_ldr_reg_lsl(12, 12, 2, 2)?);
    words.push(encode_ldr_reg_lsl(0, 3, 2, 2)?);
    words.push(encode_add_reg(2, 4, 14));
    words.push(encode_add_reg(0, 12, 0));
    words.push(encode_ldr_imm(1, 1, 0x0c)?);
    words.push(encode_add_imm(2, 2, 0x18000)?);
    words.push(encode_add_reg(1, 2, 1));
    words.push(encode_rsb_imm(0, 0, 0)?);
    words.push(0xE151_0000);
    words.push(with_cond(encode_mov_imm(0, 1)?, 0xB));
    words.push(with_cond(encode_mov_imm(0, 0)?, 0xA));
    words.push(POP_PC | (1 << 4));

    let literal_start_addr = start_addr + words.len() as u32 * 4;
    for (word_index, rd, literal_index, cond) in refs {
        let instruction_addr = start_addr + word_index as u32 * 4;
        let literal_addr = literal_start_addr + literal_index as u32 * 4;
        words[word_index] = encode_ldr_pc_literal(rd, instruction_addr, literal_addr, cond)?;
    }
    words.extend(literals);
    Ok(words)
}

fn patch_camera_focus_loop_count(rom: &mut RomImage, count: u8) -> Result<()> {
    let word = encode_mov_imm(0, count as u32)?;
    patch_overlay_words_by_id(rom, 0, 0x020B_AAE4, &[word])?;
    patch_overlay_words_by_id(rom, 0, 0x020B_AC18, &[word])?;
    Ok(())
}

fn patch_player_stage_lock_vsmode_noop(rom: &mut RomImage) -> Result<()> {
    for (func_addr, stub_addr, original_word) in [
        (0x0212_C130, 0x020C_5390, 0xE92D_4000),
        (0x0212_C1B8, 0x020C_53B0, 0xE92D_4010),
    ] {
        let stub = [
            0xE59F_C010,
            0xE5DC_C000,
            0xE35C_0000,
            0x112F_FF1E,
            original_word,
            encode_b(stub_addr + 0x14, func_addr + 0x04)?,
            0x0208_5A84,
        ];
        ensure_zero_overlay_words(rom, 0, stub_addr, stub.len())?;
        patch_overlay_words_by_id(rom, 0, stub_addr, &stub)?;
        let old = patch_overlay_words(rom, func_addr, &[encode_b(func_addr, stub_addr)?])?;
        let old_word = u32::from_le_bytes(old[..4].try_into()?);
        if old_word != original_word {
            bail!("stage lock hook 0x{func_addr:08x} expected 0x{original_word:08x}, got 0x{old_word:08x}");
        }
    }
    Ok(())
}

fn patch_arm9_words(rom: &mut RomImage, addr: u32, words: &[u32]) -> Result<Vec<u8>> {
    let section = rom
        .arm9_sections
        .iter()
        .find(|section| addr >= section.ram_addr && addr < section.ram_addr + section.size as u32)
        .ok_or_else(|| anyhow!("address 0x{addr:08x} is not in an ARM9 data section"))?;
    let off = section.file_off + (addr - section.ram_addr) as usize;
    patch_words(&mut rom.arm9, off, words)
}

fn patch_overlay_words(rom: &mut RomImage, addr: u32, words: &[u32]) -> Result<Vec<u8>> {
    let index = overlay_index_for_addr(rom, addr)?;
    let off = (addr - rom.overlays[index].base_addr) as usize;
    patch_words(&mut rom.overlays[index].data, off, words)
}

fn patch_overlay_words_by_id(
    rom: &mut RomImage,
    overlay_id: u32,
    addr: u32,
    words: &[u32],
) -> Result<Vec<u8>> {
    let index = rom
        .overlays
        .iter()
        .position(|overlay| overlay.id == overlay_id)
        .ok_or_else(|| anyhow!("overlay {overlay_id} not found"))?;
    let overlay = &rom.overlays[index];
    if addr < overlay.base_addr || addr >= overlay.base_addr + overlay.data.len() as u32 {
        bail!("address 0x{addr:08x} is not in overlay {overlay_id}");
    }
    let off = (addr - overlay.base_addr) as usize;
    patch_words(&mut rom.overlays[index].data, off, words)
}

fn ensure_zero_overlay_words(
    rom: &RomImage,
    overlay_id: u32,
    addr: u32,
    word_count: usize,
) -> Result<()> {
    let overlay = rom
        .overlays
        .iter()
        .find(|overlay| overlay.id == overlay_id)
        .ok_or_else(|| anyhow!("overlay {overlay_id} not found"))?;
    let off = (addr - overlay.base_addr) as usize;
    let len = word_count * 4;
    if off + len > overlay.data.len() {
        bail!("overlay cave out of range at 0x{addr:08x}");
    }
    if overlay.data[off..off + len].iter().any(|byte| *byte != 0) {
        bail!("overlay cave at 0x{addr:08x} is not empty");
    }
    Ok(())
}

fn overlay_index_for_addr(rom: &RomImage, addr: u32) -> Result<usize> {
    rom.overlays
        .iter()
        .enumerate()
        .filter(|(_, overlay)| {
            addr >= overlay.base_addr && addr < overlay.base_addr + overlay.data.len() as u32
        })
        .min_by_key(|(_, overlay)| overlay.data.len())
        .map(|(index, _)| index)
        .ok_or_else(|| anyhow!("address 0x{addr:08x} is not in an ARM9 overlay"))
}

fn patch_words(data: &mut [u8], off: usize, words: &[u32]) -> Result<Vec<u8>> {
    let len = words.len() * 4;
    if off + len > data.len() {
        bail!("patch out of range: offset=0x{off:x} len=0x{len:x}");
    }
    let old = data[off..off + len].to_vec();
    for (i, word) in words.iter().enumerate() {
        data[off + i * 4..off + i * 4 + 4].copy_from_slice(&word.to_le_bytes());
    }
    Ok(old)
}

fn append_overlay_entry(out: &mut Vec<u8>, overlay: &Overlay, compressed_size: u32) {
    append_u32(out, overlay.id);
    append_u32(out, overlay.base_addr);
    append_u32(out, overlay.code_size);
    append_u32(out, overlay.bss_size);
    append_u32(out, overlay.ctor_start);
    append_u32(out, overlay.ctor_end);
    append_u32(out, overlay.file_id);
    let mut flags = overlay.flags & !0x00ff_ffff;
    if overlay.compressed() {
        flags |= compressed_size & 0x00ff_ffff;
    }
    append_u32(out, flags);
}

impl Overlay {
    fn compressed(&self) -> bool {
        (self.flags & (1 << 24)) != 0
    }
}

fn load_symbols(path: &Path) -> Result<BTreeMap<String, u32>> {
    let text =
        fs::read_to_string(path).with_context(|| format!("read symbols {}", path.display()))?;
    let mut symbols = BTreeMap::new();
    for line in text.lines() {
        let Some((name, rest)) = line.split_once('=') else {
            continue;
        };
        let Some((value, _)) = rest.split_once(';') else {
            continue;
        };
        let name = name.trim();
        if name.is_empty()
            || !name
                .bytes()
                .all(|b| b.is_ascii_alphanumeric() || b == b'_' || b == b'.' || b == b'$')
        {
            continue;
        }
        if let Ok(parsed) = parse_u32(value.trim()) {
            symbols.insert(name.to_owned(), parsed);
        }
    }
    Ok(symbols)
}

fn symbol(symbols: &BTreeMap<String, u32>, name: &str) -> Result<u32> {
    symbols
        .get(name)
        .copied()
        .ok_or_else(|| anyhow!("symbol not found: {name}"))
}

fn parse_u32(value: &str) -> Result<u32> {
    let trimmed = value.trim();
    if let Some(hex) = trimmed
        .strip_prefix("0x")
        .or_else(|| trimmed.strip_prefix("0X"))
    {
        Ok(u32::from_str_radix(hex, 16)?)
    } else {
        Ok(u32::from_str_radix(trimmed, 16).or_else(|_| trimmed.parse())?)
    }
}

mod binary;
#[cfg(test)]
mod tests;

use binary::*;
