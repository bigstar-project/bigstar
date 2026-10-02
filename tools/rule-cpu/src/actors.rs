//! Bounded item/NPC forecasts. Unknown actor states remain unsupported.
use crate::observation::*;
use serde::Serialize;
use serde_json::Value;

#[derive(Clone, Debug, Serialize)]
pub struct EnemyPoint {
    pub x: f64,
    pub depth: f64,
    pub vx: f64,
}
#[derive(Clone, Debug, Serialize)]
pub struct FallingEnemyPoint {
    pub x: f64,
    pub depth: f64,
    pub vx: f64,
    pub vy: f64,
}
pub fn short_goomba(
    e: &Value,
    occupied: &impl Fn(f64, f64) -> bool,
    frames: usize,
) -> (Vec<FallingEnemyPoint>, &'static str) {
    if int(&e["objectId"]) != 83
        || int(&e["goombaBehaviorFunctionRaw"]) != 0x020e1538
        || e["entityUpdateStateFound"].as_i64() != Some(1)
        || e["entityUpdateStateRaw"].as_i64() != Some(0)
    {
        return (vec![], "unsupported_state");
    }
    let mut x = num(&e["pos"]["x"]) / 4096.0;
    let mut depth = -num(&e["pos"]["y"]) / 4096.0;
    let vx = num(&e["vel"]["x"]) / 4096.0;
    let mut vy = num(&e["vel"]["y"]) / 4096.0;
    if vx.abs() != 0.5 || !(-4.0..=0.0).contains(&vy) {
        return (vec![], "unsupported_velocity");
    }
    let mut points = vec![];
    for _ in 0..frames {
        let nx = x + vx;
        if occupied(nx + if vx > 0.0 { 7.0 } else { -8.0 }, depth - 8.0) {
            return (points, "side_contact");
        }
        let supported = vy == 0.0 && depth.rem_euclid(16.0) == 0.0 && occupied(nx, depth + 0.01);
        let mut ny = depth;
        if !supported {
            vy = (vy - 0.1875).max(-4.0);
            ny = depth - vy;
            let mut floor = (depth / 16.0).ceil() * 16.0;
            let end = (ny / 16.0).floor() * 16.0;
            while floor <= end {
                if occupied(nx, floor + 0.01) {
                    ny = floor;
                    vy = 0.0;
                    break;
                }
                floor += 16.0;
            }
        }
        x = nx;
        depth = ny;
        points.push(FallingEnemyPoint { x, depth, vx, vy });
    }
    (points, "horizon")
}
#[derive(Clone, Debug, Serialize)]
pub struct ItemPoint {
    pub frame: usize,
    pub x: f64,
    pub depth: f64,
    pub vx: f64,
    pub vy: f64,
    pub collectable: bool,
    pub phase: &'static str,
}
pub fn ground_goomba(
    mut x: f64,
    depth: f64,
    mut vx: f64,
    occupied: &impl Fn(f64, f64) -> bool,
    frames: usize,
    mut pause: i64,
) -> (Vec<EnemyPoint>, &'static str) {
    if ![-0.5, 0.0, 0.5].contains(&vx) {
        return (vec![], "unknown_speed");
    }
    let mut direction = if vx > 0.0 { 1.0 } else { -1.0 };
    if vx == 0.0 {
        let right = occupied(x + 7.01, depth - 8.0);
        let left = occupied(x - 8.01, depth - 8.0);
        if right == left {
            return (vec![], "unknown_still_state");
        }
        direction = if right { -1.0 } else { 1.0 };
    }
    let mut points = vec![];
    for _ in 0..frames {
        if vx == 0.0 {
            if pause > 0 {
                pause -= 1;
            }
            if pause == 0 {
                vx = direction * 0.5;
            }
            points.push(EnemyPoint { x, depth, vx });
            continue;
        }
        let nx = x + vx;
        if !occupied(nx, depth + 1.0) {
            return (points, "unsupported");
        }
        if occupied(nx + if direction > 0.0 { 7.0 } else { -8.0 }, depth - 8.0) {
            x = nx;
            direction = -direction;
            vx = 0.0;
            pause = 5;
        } else {
            x = nx;
        }
        points.push(EnemyPoint { x, depth, vx });
    }
    (points, "horizon")
}
pub fn ground_koopa(
    e: &Value,
    occupied: &impl Fn(f64, f64) -> bool,
    frames: usize,
) -> (Vec<EnemyPoint>, &'static str) {
    if int(&e["objectId"]) != 94 {
        return (vec![], "wrong_actor");
    }
    let mut x = num(&e["pos"]["x"]) / 4096.0;
    let depth = -num(&e["pos"]["y"]) / 4096.0;
    let mut vx = num(&e["vel"]["x"]) / 4096.0;
    let shell = int(&e["koopaBehaviorFunctionRaw"]) == 0x020df9f8
        && int(&e["koopaShellModeRaw"]) == 1
        && (2.0..=3.0).contains(&vx.abs());
    let walking = int(&e["koopaBehaviorFunctionRaw"]) == 0x020dfc58
        && e["koopaShellModeRaw"].as_i64() == Some(0)
        && vx.abs() == 0.5;
    if !(shell || walking) || int(&e["koopaCollisionRaw"]) & 0x100 == 0 {
        return (vec![], "unsupported_state");
    }
    let mut points = vec![];
    for _ in 0..frames {
        let nx = x + vx;
        if !occupied(nx, depth + 1.0) {
            return (points, "support_edge");
        }
        let direction = if vx > 0.0 { 1.0 } else { -1.0 };
        let wall = [6.0, 9.0]
            .iter()
            .any(|h| occupied(nx + direction * 6.0, depth - h));
        if wall && !shell {
            return (points, "walking_turn");
        }
        x = nx;
        if wall {
            vx = -vx;
        }
        points.push(EnemyPoint {
            x: x.rem_euclid(1024.0),
            depth,
            vx,
        });
    }
    (points, "horizon")
}
pub fn item(
    mut x: f64,
    mut depth: f64,
    mut vx: f64,
    mut vy: f64,
    occupied: &impl Fn(f64, f64) -> bool,
    frames: usize,
) -> Vec<ItemPoint> {
    let mut points = vec![];
    for frame in 1..=frames {
        let direction = if vx > 0.0 { 1.0 } else { -1.0 };
        let nx = x + vx;
        if vx != 0.0 && occupied(nx + direction * 8.0, depth - 8.0) {
            let wall = ((nx + direction * 8.0) / 16.0).floor() * 16.0;
            x = if direction > 0.0 {
                wall - 8.0
            } else {
                wall + 24.0
            };
            vx = -vx;
        } else {
            x = nx;
        }
        if vy <= 0.0 && occupied(x, depth + 0.01) {
            vy = 0.0;
        } else {
            vy = (vy - 0.1875).max(-4.0);
            let nd = depth - vy;
            let mut landed = false;
            if vy <= 0.0 {
                let mut y = (depth / 16.0).ceil() * 16.0;
                let end = (nd / 16.0).floor() * 16.0;
                while y <= end {
                    if occupied(x, y + 0.01) {
                        depth = y;
                        vy = 0.0;
                        landed = true;
                        break;
                    }
                    y += 16.0;
                }
            }
            if !landed {
                depth = nd;
            }
        }
        points.push(ItemPoint {
            frame,
            x: x.rem_euclid(1024.0),
            depth,
            vx,
            vy,
            collectable: true,
            phase: "rolling",
        });
    }
    points
}
pub fn emerging(
    e: &Value,
    occupied: &impl Fn(f64, f64) -> bool,
    frames: usize,
) -> Option<Vec<ItemPoint>> {
    if int(&e["objectId"]) != 31
        || int(&e["itemBehaviorFunctionRaw"]) != 0x020d438c
        || e["itemKindRaw"].as_i64() != Some(0)
        || e["itemRollingSuppressedRaw"].as_i64() != Some(0)
        || !matches!(e["itemDirectionRaw"].as_i64(), Some(0 | 1))
        || num(&e["vel"]["x"]) != 0.0
        || num(&e["vel"]["y"]) != 1280.0
    {
        return None;
    }
    let mut target = e["itemEmergenceTargetYRaw"].as_i64()?;
    if target >= 1 << 31 {
        target -= 1 << 32;
    }
    let target_depth = -(target as f64) / 4096.0;
    let x = num(&e["pos"]["x"]) / 4096.0;
    let mut depth = -num(&e["pos"]["y"]) / 4096.0;
    if !(0.0..=16.0).contains(&(depth - target_depth)) {
        return None;
    }
    let vx = if int(&e["itemDirectionRaw"]) == 0 {
        1.0
    } else {
        -1.0
    };
    let mut points = vec![];
    while points.len() < frames {
        depth = (depth - 0.3125).max(target_depth);
        let done = depth == target_depth;
        points.push(ItemPoint {
            frame: points.len() + 1,
            x,
            depth,
            vx: if done { vx } else { 0.0 },
            vy: if done { 0.0 } else { 0.3125 },
            collectable: depth <= target_depth + 8.0,
            phase: if done { "rolling" } else { "emerging" },
        });
        if done {
            break;
        }
    }
    let offset = points.len();
    if depth == target_depth && offset < frames {
        for mut point in item(x, depth, vx, 0.0, occupied, frames - offset) {
            point.frame += offset;
            points.push(point);
        }
    }
    Some(points)
}
