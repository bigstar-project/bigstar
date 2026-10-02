//! First-equipment routes and bounded checks against current observations.
use crate::{
    actors::{self, EnemyPoint},
    development::{alive, near},
    navigation::GrassNavigator,
    observation::*,
    physics::*,
};
use serde_json::{json, Value};

pub struct EdgeOptions {
    pub horizon: usize,
    pub only_side: Option<i64>,
}
pub fn opponent_can_reach(other: &Value, raw: &Value, points: &[Value]) -> bool {
    if !alive(other) {
        return false;
    }
    if !matches!(raw["currentPowerupRaw"].as_i64(), Some(0..=2)) {
        return true;
    }
    let x = num(&other["pos"]["x"]) / 4096.0;
    let mut speed = 4.0_f64.max((num(&other["vel"]["x"]) / 4096.0).abs());
    if int(&raw["currentPowerupRaw"]) == 2 {
        speed += 3.625;
    }
    points
        .iter()
        .enumerate()
        .any(|(i, p)| wrap(num(&p["x"]), x).abs() <= 16.0 + speed * (i + 1) as f64)
}
pub fn edge_interception(
    s: Motion,
    previous: i64,
    delay: i64,
    entity: &Value,
    nav: &GrassNavigator,
    paths: &[Vec<EnemyPoint>],
    options: EdgeOptions,
) -> Option<Value> {
    if !s.grounded || !(0..=6).contains(&delay) || s.vx.abs() >= 2.25 {
        return None;
    }
    let horizon = options.horizon;
    let occupied = |x, y| nav.occupied(x, y);
    let items = actors::emerging(entity, &occupied, horizon)?;
    let raw = int(&entity["itemEmergenceTargetYRaw"]);
    let surface = -(if raw >= 1 << 31 { raw - (1 << 32) } else { raw }) as f64 / 4096.0;
    let item_x = s.x + wrap(num(&entity["pos"]["x"]) / 4096.0, s.x);
    if !(40.0..=80.0).contains(&(s.depth - surface)) || !occupied(item_x, surface + 1.0) {
        return None;
    }
    let mut left = (item_x / 16.0).floor() * 16.0;
    let mut right = left + 16.0;
    let mut found = false;
    for _ in 0..8 {
        if !occupied(left - 1.0, surface + 1.0) {
            found = true;
            break;
        }
        left -= 16.0;
    }
    if !found {
        return None;
    }
    found = false;
    for _ in 0..8 {
        if !occupied(right, surface + 1.0) {
            found = true;
            break;
        }
        right += 16.0;
    }
    if !found || s.x < left - 24.0 || s.x > right + 24.0 {
        return None;
    }
    struct Candidate {
        side: i64,
        back: usize,
        brake: usize,
        lift: usize,
        jump: usize,
        held: i64,
        inputs: Vec<i64>,
    }
    let mut candidates = vec![];
    for (side, away) in [(-1, 32), (1, 16)] {
        if options.only_side.is_some_and(|wanted| wanted != side) {
            continue;
        }
        let toward = 48 - away;
        for back in (6..67).step_by(6) {
            for brake in [0, 6, 12, 18] {
                let mut ground = vec![previous; delay as usize];
                ground.extend(vec![away | 2048; back]);
                ground.extend(vec![toward | 2048; brake]);
                let (points, _) =
                    forecast_player(s, previous, &ground, &occupied, ForecastOptions::default());
                if points.len() != ground.len() || !points.iter().all(|p| p.state.grounded) {
                    continue;
                }
                let takeoff = points.last()?.state;
                let clearance = if side < 0 {
                    left - takeoff.x
                } else {
                    takeoff.x - right
                };
                if !(8.0..=32.0).contains(&clearance)
                    || [-8.0, 0.0, 8.0].iter().any(|x| {
                        (surface as i64..s.depth as i64 - 15)
                            .step_by(8)
                            .any(|y| occupied(takeoff.x + x, y as f64))
                    })
                {
                    continue;
                }
                for lift in [0, 6, 12] {
                    for jump in [18, 24, 30] {
                        let mut inputs = ground.clone();
                        inputs.extend(vec![2; lift]);
                        inputs.extend(vec![toward | 2050; jump]);
                        if inputs.len() < horizon {
                            inputs.resize(horizon, toward | 2048);
                        }
                        candidates.push(Candidate {
                            side,
                            back,
                            brake,
                            lift,
                            jump,
                            held: toward | 2048,
                            inputs,
                        });
                    }
                }
            }
        }
    }
    let mut best: Option<(usize, usize)> = None;
    for (index, trial) in candidates.iter().enumerate() {
        let (points, _) = forecast_player(
            s,
            previous,
            &trial.inputs,
            &occupied,
            ForecastOptions::default(),
        );
        for (i, (p, item)) in points.iter().zip(&items).enumerate() {
            let p = &p.state;
            if p.depth > 288.0
                || paths.iter().any(|path| {
                    path.get(i).is_none_or(|e| {
                        wrap(p.x, e.x).abs() < 16.0 && (p.depth - e.depth).abs() < 24.0
                    })
                })
            {
                break;
            }
            if item.collectable
                && wrap(p.x, item.x).abs() < 10.0
                && (p.depth - item.depth).abs() < 10.0
            {
                if best.is_none_or(|(_, pickup)| i + 1 < pickup) {
                    best = Some((index, i + 1));
                }
                break;
            }
            if best.is_some_and(|(_, pickup)| i + 1 >= pickup) {
                break;
            }
        }
    }
    let (index, pickup) = best?;
    let trial = &candidates[index];
    let (points, _) = forecast_player(
        s,
        previous,
        &trial.inputs,
        &occupied,
        ForecastOptions::default(),
    );
    if points.len() < pickup
        || points[..pickup].iter().any(|p| {
            !p.state.grounded
                && [-8.0, 8.0].iter().any(|x| {
                    [4.0, 11.0]
                        .iter()
                        .any(|h| occupied(p.state.x + x, p.state.depth - h))
                })
        })
    {
        return None;
    }
    Some(
        json!({"pickup_frame":pickup,"held":trial.held,"duration":horizon-delay as usize,"input_sequence":trial.inputs[delay as usize..],"points":points,"item_points":items,"item_guid":entity["actorGuid"],"item_x":items[pickup-1].x,"item_depth":items[pickup-1].depth,"edge_route":{"side":trial.side,"back":trial.back,"brake":trial.brake,"lift":trial.lift,"jump":trial.jump},"candidate_count":candidates.len()}),
    )
}
pub fn counterjump_clear(
    d: &Value,
    player: usize,
    points: &[Value],
    nav: &GrassNavigator,
    fire_only: bool,
) -> bool {
    let me = &d["observation"]["players"][player];
    let hb = &me["hitbox"];
    for e in array(&d["observation"]["entities"]) {
        let cat = text(&e["category"]);
        let (dx, dy) = delta(&e["pos"], &me["pos"]);
        if !fire_only && matches!(cat, "enemy_goomba" | "enemy_koopa") {
            if flag(&e["entityUpdateStateFound"]) && int(&e["entityUpdateStateRaw"]) == 2 {
                continue;
            }
            if dx.abs() < 160.0 && dy.abs() < 128.0 {
                return false;
            }
        }
        if cat != "player_fireball"
            || (flag(&e["ownerVerified"]) && e["owner"].as_u64() == Some(player as u64))
            || dx.abs() > 256.0
        {
            continue;
        }
        let mut x = num(&e["pos"]["x"]) / 4096.0;
        let mut depth = -num(&e["pos"]["y"]) / 4096.0;
        let vx = num(&e["vel"]["x"]) / 4096.0;
        let mut vy = num(&e["vel"]["y"]) / 4096.0;
        if vx.abs() != 3.625 || !(-4.0..=4.0).contains(&vy) {
            return false;
        }
        for p in points {
            x += vx;
            depth -= vy;
            let mut next_vy = (vy - 0.4375).max(-4.0);
            let floor = (depth / 16.0).floor() * 16.0;
            if nav.occupied(x, depth) && !nav.occupied(x, floor - 0.01) {
                depth = floor;
                if vy < 0.0 {
                    next_vy = 4.0;
                }
            }
            vy = next_vy;
            let separation = (wrap(num(&p["x"]) + num(&hb["centerOffsetX"]) / 4096.0, x).abs()
                - num(&hb["halfWidth"]) / 4096.0
                - 4.0)
                .max(
                    (num(&p["depth"]) - num(&hb["centerOffsetY"]) / 4096.0 - depth).abs()
                        - num(&hb["halfHeight"]) / 4096.0
                        - 4.0,
                );
            if separation < 4.0 {
                return false;
            }
        }
    }
    true
}
pub fn edge_world_reason(
    plan: &Value,
    d: &Value,
    player: usize,
    frame: i64,
    nav: &GrassNavigator,
) -> Option<&'static str> {
    if plan["edge_route"].is_null() {
        return None;
    }
    if d["runtimePlayers"][player]["subActionFlagRaw"]
        .as_i64()
        .unwrap_or(1)
        & 1
        != 0
    {
        return Some("edge_carrying_or_unknown");
    }
    let obs = &d["observation"];
    let me = &obs["players"][player];
    let other = &obs["players"][player ^ 1];
    if alive(other) && near(other, me, 128.0, None) {
        return Some("edge_opponent_near");
    }
    let elapsed = (frame - int(&plan["start"])).max(0) as usize;
    let points = array(&plan["points"]);
    let future = &points[elapsed.min(points.len())..(elapsed + 12).min(points.len())];
    if !counterjump_clear(d, player, future, nav, true) {
        return Some("edge_projectile_approaching");
    }
    for e in array(&obs["entities"]) {
        if !matches!(text(&e["category"]), "enemy_goomba" | "enemy_koopa")
            || delta(&e["pos"], &me["pos"]).0.abs() > 256.0
        {
            continue;
        }
        if text(&e["category"]) == "enemy_goomba"
            && int(&e["goombaBehaviorFunctionRaw"]) == 0x020e13d0
        {
            continue;
        }
        let x = num(&e["pos"]["x"]) / 4096.0;
        let depth = -num(&e["pos"]["y"]) / 4096.0;
        if !future.is_empty()
            && text(&e["category"]) == "enemy_goomba"
            && int(&e["goombaBehaviorFunctionRaw"]) == 0x020e1478
            && num(&e["vel"]["x"]) == 0.0
            && num(&e["vel"]["y"]) == 0.0
            && nav.occupied(x, depth + 1.0)
            && future
                .iter()
                .enumerate()
                .all(|(i, p)| wrap(num(&p["x"]), x).abs() > 24.0 + 0.5 * (i + 1) as f64)
        {
            continue;
        }
        if text(&e["category"]) != "enemy_goomba"
            || int(&e["goombaBehaviorFunctionRaw"]) != 0x020e1538
            || num(&e["vel"]["x"]).abs() != 2048.0
            || num(&e["vel"]["y"]) != 0.0
        {
            return Some("edge_unknown_hazard");
        }
        let (path, _) = actors::ground_goomba(
            x,
            depth,
            num(&e["vel"]["x"]) / 4096.0,
            &|x, y| nav.occupied(x, y),
            future.len(),
            0,
        );
        if path.len() < future.len() {
            return Some("edge_hazard_forecast_incomplete");
        }
        if future.iter().zip(&path).any(|(p, e)| {
            wrap(num(&p["x"]), e.x).abs() < 16.0 && (num(&p["depth"]) - e.depth).abs() < 24.0
        }) {
            return Some("edge_hazard_approaching");
        }
    }
    None
}
