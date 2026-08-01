use anyhow::{anyhow, bail, Result};

pub(crate) fn read_banner(data: &[u8], off: usize) -> Result<Vec<u8>> {
    let version = read_u16(data, off)? as usize;
    let size = match version {
        0x0001 => 0x840,
        0x0002 => 0x940,
        0x0003 => 0x1240,
        _ => 0x840,
    };
    Ok(slice(data, off, size, "banner")?.to_vec())
}

pub(crate) fn slice<'a>(data: &'a [u8], off: usize, len: usize, label: &str) -> Result<&'a [u8]> {
    data.get(off..off + len)
        .ok_or_else(|| anyhow!("{label} slice out of range: off=0x{off:x} len=0x{len:x}"))
}

pub(crate) fn read_u16(data: &[u8], off: usize) -> Result<u16> {
    Ok(u16::from_le_bytes(slice(data, off, 2, "u16")?.try_into()?))
}

pub(crate) fn read_u32(data: &[u8], off: usize) -> Result<u32> {
    Ok(u32::from_le_bytes(slice(data, off, 4, "u32")?.try_into()?))
}

pub(crate) fn write_u16(data: &mut [u8], off: usize, value: u16) -> Result<()> {
    data.get_mut(off..off + 2)
        .ok_or_else(|| anyhow!("u16 write out of range: 0x{off:x}"))?
        .copy_from_slice(&value.to_le_bytes());
    Ok(())
}

pub(crate) fn write_u32(data: &mut [u8], off: usize, value: u32) -> Result<()> {
    data.get_mut(off..off + 4)
        .ok_or_else(|| anyhow!("u32 write out of range: 0x{off:x}"))?
        .copy_from_slice(&value.to_le_bytes());
    Ok(())
}

pub(crate) fn append_u32(out: &mut Vec<u8>, value: u32) {
    out.extend_from_slice(&value.to_le_bytes());
}

pub(crate) fn align_vec(out: &mut Vec<u8>, alignment: usize, value: u8) -> usize {
    let padding = (!out.len() + 1) & (alignment - 1);
    out.resize(out.len() + padding, value);
    out.len()
}

pub(crate) fn encode_mov_imm(rd: u8, imm: u32) -> Result<u32> {
    Ok(0xE3A0_0000 | ((rd as u32) << 12) | encode_arm_imm12(imm)?)
}

pub(crate) fn encode_load_imm(rd: u8, imm: u32) -> Result<u32> {
    encode_mov_imm(rd, imm).or_else(|_| {
        let inverse = !imm;
        Ok(0xE3E0_0000 | ((rd as u32) << 12) | encode_arm_imm12(inverse)?)
    })
}

pub(crate) fn encode_arm_imm12(imm: u32) -> Result<u32> {
    for rot in 0..16 {
        let left = if rot == 0 {
            imm
        } else {
            imm.rotate_left(rot * 2)
        };
        if left <= 0xff {
            return Ok((rot << 8) | left);
        }
    }
    bail!("immediate 0x{imm:x} is not encodable as ARM immediate")
}

pub(crate) fn encode_add_sp_imm(imm: u32) -> Result<u32> {
    Ok(0xE28D_D000 | encode_arm_imm12(imm)?)
}

pub(crate) fn encode_add_imm(rd: u8, rn: u8, imm: u32) -> Result<u32> {
    Ok(0xE280_0000 | ((rn as u32) << 16) | ((rd as u32) << 12) | encode_arm_imm12(imm)?)
}

pub(crate) fn encode_sub_imm(rd: u8, rn: u8, imm: u32) -> Result<u32> {
    Ok(0xE240_0000 | ((rn as u32) << 16) | ((rd as u32) << 12) | encode_arm_imm12(imm)?)
}

pub(crate) fn encode_sub_sp_imm(imm: u32) -> Result<u32> {
    Ok(0xE24D_D000 | encode_arm_imm12(imm)?)
}

pub(crate) fn encode_str_imm(rd: u8, rn: u8, off: u32) -> Result<u32> {
    if off > 0xfff {
        bail!("STR offset out of range: 0x{off:x}");
    }
    Ok(0xE580_0000 | ((rn as u32) << 16) | ((rd as u32) << 12) | off)
}

pub(crate) fn encode_strb_imm(rd: u8, rn: u8, off: u32) -> Result<u32> {
    if off > 0xfff {
        bail!("STRB offset out of range: 0x{off:x}");
    }
    Ok(0xE5C0_0000 | ((rn as u32) << 16) | ((rd as u32) << 12) | off)
}

pub(crate) fn encode_strh_imm(rd: u8, rn: u8, off: u32) -> Result<u32> {
    if off > 0xff {
        bail!("STRH offset out of range: 0x{off:x}");
    }
    Ok(
        0xE1C0_00B0
            | ((rn as u32) << 16)
            | ((rd as u32) << 12)
            | ((off & 0xf0) << 4)
            | (off & 0x0f),
    )
}

pub(crate) fn encode_ldr_imm(rd: u8, rn: u8, off: u32) -> Result<u32> {
    if off > 0xfff {
        bail!("LDR offset out of range: 0x{off:x}");
    }
    Ok(0xE590_0000 | ((rn as u32) << 16) | ((rd as u32) << 12) | off)
}

pub(crate) fn encode_ldrb_imm(rd: u8, rn: u8, off: u32) -> Result<u32> {
    if off > 0xfff {
        bail!("LDRB offset out of range: 0x{off:x}");
    }
    Ok(0xE5D0_0000 | ((rn as u32) << 16) | ((rd as u32) << 12) | off)
}

pub(crate) fn encode_ldrh_imm(rd: u8, rn: u8, off: u32) -> Result<u32> {
    if off > 0xff {
        bail!("LDRH offset out of range: 0x{off:x}");
    }
    Ok(
        0xE1D0_00B0
            | ((rn as u32) << 16)
            | ((rd as u32) << 12)
            | ((off & 0xf0) << 4)
            | (off & 0x0f),
    )
}

pub(crate) fn encode_ldr_reg_lsl(rd: u8, rn: u8, rm: u8, shift: u8) -> Result<u32> {
    if shift > 31 {
        bail!("LDR shift out of range: {shift}");
    }
    Ok(0xE790_0000 | ((rn as u32) << 16) | ((rd as u32) << 12) | ((shift as u32) << 7) | rm as u32)
}

pub(crate) fn encode_cmp_imm(rn: u8, imm: u32) -> Result<u32> {
    Ok(0xE350_0000 | ((rn as u32) << 16) | encode_arm_imm12(imm)?)
}

pub(crate) fn encode_cmp_reg(rn: u8, rm: u8) -> u32 {
    0xE150_0000 | ((rn as u32) << 16) | rm as u32
}

pub(crate) fn encode_mov_reg(rd: u8, rm: u8) -> u32 {
    0xE1A0_0000 | ((rd as u32) << 12) | rm as u32
}

pub(crate) fn encode_add_reg(rd: u8, rn: u8, rm: u8) -> u32 {
    0xE080_0000 | ((rn as u32) << 16) | ((rd as u32) << 12) | rm as u32
}

pub(crate) fn encode_add_reg_lsl(rd: u8, rn: u8, rm: u8, shift: u8) -> Result<u32> {
    if shift > 31 {
        bail!("ADD shift out of range: {shift}");
    }
    Ok(0xE080_0000 | ((rn as u32) << 16) | ((rd as u32) << 12) | ((shift as u32) << 7) | rm as u32)
}

pub(crate) fn encode_rsb_imm(rd: u8, rn: u8, imm: u32) -> Result<u32> {
    Ok(0xE260_0000 | ((rn as u32) << 16) | ((rd as u32) << 12) | encode_arm_imm12(imm)?)
}

pub(crate) fn with_cond(word: u32, cond: u8) -> u32 {
    (word & 0x0fff_ffff) | ((cond as u32 & 0xf) << 28)
}

pub(crate) fn encode_push(regmask: u32) -> u32 {
    0xE92D_0000 | regmask
}

pub(crate) fn encode_bl(src_addr: u32, dst_addr: u32) -> Result<u32> {
    encode_branch(0xEB00_0000, src_addr, dst_addr)
}

pub(crate) fn encode_b(src_addr: u32, dst_addr: u32) -> Result<u32> {
    encode_branch(0xEA00_0000, src_addr, dst_addr)
}

pub(crate) fn encode_branch(opcode: u32, src_addr: u32, dst_addr: u32) -> Result<u32> {
    let diff = dst_addr as i64 - (src_addr as i64 + 8);
    if diff % 4 != 0 {
        bail!("unaligned branch target 0x{dst_addr:08x} from 0x{src_addr:08x}");
    }
    let off = diff / 4;
    if !(-(1 << 23)..(1 << 23)).contains(&off) {
        bail!("branch target out of range 0x{dst_addr:08x} from 0x{src_addr:08x}");
    }
    Ok(opcode | (off as u32 & 0x00ff_ffff))
}

pub(crate) fn encode_ldr_pc_literal(
    rd: u8,
    instruction_addr: u32,
    literal_addr: u32,
    cond: u8,
) -> Result<u32> {
    let pc = instruction_addr + 8;
    let off = literal_addr
        .checked_sub(pc)
        .ok_or_else(|| anyhow!("literal before instruction"))?;
    if off > 0xfff || off % 4 != 0 {
        bail!("LDR literal target 0x{literal_addr:08x} out of range from 0x{instruction_addr:08x}");
    }
    Ok(((cond as u32) << 28) | 0x059F_0000 | ((rd as u32) << 12) | off)
}
