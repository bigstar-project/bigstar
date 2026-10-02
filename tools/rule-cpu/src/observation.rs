use serde_json::Value;

pub fn num(v: &Value) -> f64 {
    v.as_f64().unwrap_or(0.0)
}
pub fn int(v: &Value) -> i64 {
    if let Some(s) = v.as_str() {
        if let Some(hex) = s.strip_prefix("0x") {
            return i64::from_str_radix(hex, 16).unwrap_or(0);
        }
        return s.parse().unwrap_or(0);
    }
    v.as_i64().unwrap_or_else(|| num(v) as i64)
}
pub fn flag(v: &Value) -> bool {
    v.as_bool().unwrap_or_else(|| num(v) != 0.0)
}
pub fn array(v: &Value) -> &[Value] {
    v.as_array().map_or(&[], Vec::as_slice)
}
pub fn text(v: &Value) -> &str {
    v.as_str().unwrap_or("")
}
pub fn wrap(a: f64, b: f64) -> f64 {
    (a - b + 512.0).rem_euclid(1024.0) - 512.0
}
pub fn delta(target: &Value, position: &Value) -> (f64, f64) {
    (
        wrap(num(&target["x"]) / 4096.0, num(&position["x"]) / 4096.0),
        (num(&target["y"]) - num(&position["y"])) / 4096.0,
    )
}
pub fn solid(mask: Option<i64>) -> bool {
    mask.is_some_and(|m| m & 32769 != 0 && m & 10240 == 0)
}
pub fn terrain_cell(player: &Value, offset_x: f64, offset_depth: f64) -> Option<i64> {
    let grid = &player["terrain"];
    let x = num(&player["pos"]["x"]) / 4096.0;
    let depth = -num(&player["pos"]["y"]) / 4096.0;
    let rx = ((x + offset_x) / 16.0).floor() as i64 - (x / 16.0).floor() as i64;
    let ry = ((depth + offset_depth) / 16.0).floor() as i64 - (depth / 16.0).floor() as i64;
    if !(int(&grid["minRelTileX"])..int(&grid["minRelTileX"]) + int(&grid["width"])).contains(&rx)
        || !(int(&grid["minRelTileY"])..int(&grid["minRelTileY"]) + int(&grid["height"]))
            .contains(&ry)
    {
        return None;
    }
    match array(&grid["cells"])
        .iter()
        .find(|c| int(&c["rx"]) == rx && int(&c["ry"]) == ry)
    {
        Some(c) => flag(&c["found"]).then(|| int(&c["mask"])),
        None => flag(&grid["omittedCellFound"]).then_some(0),
    }
}
pub fn direction(dx: f64, tolerance: f64) -> i64 {
    if dx > tolerance {
        1
    } else if dx < -tolerance {
        -1
    } else {
        0
    }
}
pub fn buttons(direction: i64) -> i64 {
    if direction > 0 {
        16
    } else if direction < 0 {
        32
    } else {
        0
    }
}
