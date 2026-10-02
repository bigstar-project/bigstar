use super::encounters::defeated;
use super::*;
use crate::{
    navigation::GrassNavigator,
    physics::{forecast_player, vertical_step, ForecastOptions},
};

fn short_path(
    d: &Value,
    player: usize,
    commands: &[i64],
    nav: &GrassNavigator,
    counter: bool,
) -> Option<(Vec<Value>, &'static str)> {
    let me = &d["observation"]["players"][player];
    let raw = &d["runtimePlayers"][player];
    let height = if int(&raw["currentPowerupRaw"]) == 0 {
        16.0
    } else {
        27.0
    };
    let mut s = motion(me, raw, false);
    let mut left = flag(&me["contact"]["wallLeft"]);
    let mut right = flag(&me["contact"]["wallRight"]);
    let mut points = vec![];
    let mut released = false;
    for &held in commands {
        if s.vy < 0.75 {
            released = true;
            break;
        }
        if (s.vx > 0.0 && right) || (s.vx < 0.0 && left) {
            s.vx = 0.0
        }
        s.x += s.vx;
        s.vy = vertical_step(s.vy, held & 3 != 0);
        let depth = s.depth - s.vy;
        if [-5.0, 0.0, 4.0].iter().any(|o| {
            [1.0, 8.0, 16.0]
                .iter()
                .any(|v| nav.occupied(s.x + o, depth + v))
        }) || [-2.0, 0.0, 1.0]
            .iter()
            .any(|o| nav.occupied(s.x + o, depth - height))
        {
            return None;
        }
        s.depth = depth;
        left = [4.0, height - 8.0]
            .iter()
            .any(|h| nav.occupied(s.x - 9.0, depth - h));
        right = [4.0, height - 8.0]
            .iter()
            .any(|h| nav.occupied(s.x + 8.0, depth - h));
        if right {
            s.x =
                s.x.min(((s.x + 8.0) / 16.0).floor() * 16.0 - 7.0 - 1.0 / 4096.0)
        }
        if left {
            s.x = s.x.max(((s.x - 9.0) / 16.0).floor() * 16.0 + 24.0)
        }
        points.push(json!({"x":s.x,"depth":s.depth,"vx":s.vx,"vy":s.vy,"wall_left":left,"wall_right":right,"grounded":false,"contact":null}));
    }
    if !released || points.is_empty() {
        return None;
    }
    let n = points.len();
    let (tail, status) = forecast_player(
        s,
        commands[n - 1],
        &commands[n..],
        &|x, y| nav.occupied(x, y),
        ForecastOptions {
            height,
            counter_landing_turn: counter,
        },
    );
    points.extend(tail.iter().map(|p| json!(p)));
    Some((points, status))
}
fn short_hazards(d: &Value, player: usize, points: &[Value], nav: &GrassNavigator) -> bool {
    let obs = &d["observation"];
    let me = &obs["players"][player];
    let hb = &me["hitbox"];
    if !flag(&hb["found"]) || int(&hb["fixedPointShift"]) != 12 {
        return false;
    }
    let other = &obs["players"][player ^ 1];
    if crate::opening::opponent_can_reach(other, &d["runtimePlayers"][player ^ 1], points) {
        return false;
    }
    for e in array(&obs["entities"]) {
        if !matches!(text(&e["category"]), "enemy_goomba" | "enemy_koopa") || defeated(e) {
            continue;
        }
        let (dx, dy) = delta(&e["pos"], &me["pos"]);
        if dx.abs() > 240.0 || dy.abs() > 160.0 {
            continue;
        }
        let (track, status) = if text(&e["category"]) == "enemy_goomba" {
            let (p, s) = actors::short_goomba(e, &|x, y| nav.occupied(x, y), points.len());
            (p.iter().map(|p| json!(p)).collect::<Vec<_>>(), s)
        } else if int(&e["koopaShellModeRaw"]) == 1
            && int(&e["koopaBehaviorFunctionRaw"]) == 0x020df9f8
            && num(&e["vel"]["x"]) == 0.0
            && num(&e["vel"]["y"]) == 0.0
            && int(&e["koopaCollisionRaw"]) & 0x100 != 0
        {
            let ex = num(&e["pos"]["x"]) / 4096.0;
            let ed = -num(&e["pos"]["y"]) / 4096.0;
            let n = points.len() as i64;
            if alive(other)
                && wrap(num(&other["pos"]["x"]) / 4096.0, ex).abs() <= 24.0 + 4.0 * n as f64
            {
                return false;
            }
            if !(-n..=n).all(|o| nav.occupied(ex + o as f64, ed + 1.0))
                || points
                    .iter()
                    .enumerate()
                    .any(|(i, p)| wrap(num(&p["x"]), ex).abs() <= 24.0 + (i + 1) as f64)
            {
                return false;
            }
            continue;
        } else if e["koopaShellModeRaw"].as_i64() == Some(0)
            && int(&e["koopaBehaviorFunctionRaw"]) == 0x020dfc58
        {
            let (p, s) = actors::ground_koopa(e, &|x, y| nav.occupied(x, y), points.len());
            (p.iter().map(|p| json!(p)).collect(), s)
        } else {
            return false;
        };
        if status != "horizon" || track.len() != points.len() {
            return false;
        }
        for (p, q) in points.iter().zip(track) {
            let separation = (wrap(
                num(&p["x"]) + num(&hb["centerOffsetX"]) / 4096.0,
                num(&q["x"]),
            )
            .abs()
                - num(&hb["halfWidth"]) / 4096.0
                - 8.0)
                .max(
                    (num(&p["depth"])
                        - num(&hb["centerOffsetY"]) / 4096.0
                        - (num(&q["depth"]) - 8.0))
                        .abs()
                        - num(&hb["halfHeight"]) / 4096.0
                        - 8.0,
                );
            if separation < 4.0 {
                return false;
            }
        }
    }
    crate::opening::counterjump_clear(d, player, points, nav, true)
}
impl DevelopmentRule {
    pub(super) fn wall_short(
        &mut self,
        d: &Value,
        frame: i64,
        previous: i64,
        mut held: i64,
    ) -> i64 {
        let player = self.base.player;
        let obs = &d["observation"];
        let me = &obs["players"][player];
        let raw = &d["runtimePlayers"][player];
        let delay = d["time"]["inputDelay"].as_i64().unwrap_or(-1);
        let direction = i64::from(held & 16 != 0) - i64::from(held & 32 != 0);
        let s = motion(me, raw, false);
        if !flag(&me["found"])
            || !flag(&raw["found"])
            || flag(&me["dead"])
            || int(&obs["stage"]["group"]) != 9
            || obs["stage"]["id"].as_i64() != Some(0)
            || !matches!(int(&raw["currentPowerupRaw"]), 0..=2)
            || int(&raw["physicsFlagRaw"]) != 2
            || int(&raw["actionFlagRaw"]) != 0x100020
            || int(&raw["behaviorFuncRaw"]) != 0x021135b8
            || int(&raw["behaviorStepRaw"]) != 1
            || ["damageStateRaw", "damageCooldownRaw", "updateLockedRaw"]
                .iter()
                .any(|k| raw[k].as_i64() != Some(0))
            || !(0..=6).contains(&delay)
            || held & 2 == 0
            || previous & 2 == 0
            || held & !(2 | 48 | 2048) != 0
            || previous & !(2 | 48 | 2048) != 0
            || direction as f64 * s.vx >= 0.0
            || s.vx.abs() != 2.25
            || !(s.vy > 2.5 && s.vy <= 3.4375)
            || !(160.0..272.0).contains(&s.depth)
            || text(&self.base.trace["target"]) != "carrier"
            || (int(&raw["currentPowerupRaw"]) == 2 && (held & 2048 == 0 || previous & 2048 == 0))
        {
            return held;
        }
        let remaining = (((self.base.jump_until - frame).max(0) + 5) / 6 * 6).min(48);
        if remaining < 6 {
            return held;
        }
        self.wall_short_nav.update(me);
        let nav = &self.wall_short_nav;
        let mut commands = vec![previous; delay as usize];
        commands.extend(vec![held; remaining as usize]);
        commands.resize(72, held & !3);
        let Some((baseline, _)) = short_path(d, player, &commands, nav, false) else {
            return held;
        };
        if baseline.is_empty()
            || baseline.iter().any(|p| text(&p["contact"]) == "floor")
            || num(&baseline.last().unwrap()["depth"]) < 320.0
        {
            return held;
        }
        commands[delay as usize..delay as usize + 6].fill(held & !3);
        let mut landing = 0;
        for counter in [false, true] {
            let Some((points, _)) = short_path(d, player, &commands, nav, counter) else {
                return held;
            };
            let Some(n) = points
                .iter()
                .position(|p| text(&p["contact"]) == "floor")
                .map(|i| i + 1)
            else {
                return held;
            };
            landing = n;
            if n > 48
                || points.len() < n + 6
                || !points[n - 1..n + 6]
                    .iter()
                    .all(|p| flag(&p["grounded"]) && num(&p["depth"]) <= 272.0)
                || !short_hazards(d, player, &points[..n + 6], nav)
            {
                return held;
            }
        }
        held &= !3;
        self.base.trace["held"] = json!(held);
        self.base.trace["wall_short"] = json!({"start":frame,"landing_after":landing,"nominal_end_depth":baseline.last().unwrap()["depth"],"nominal_remaining_hold":remaining});
        held
    }
}
