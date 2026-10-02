use super::*;
use crate::{
    navigation::GrassNavigator,
    physics::{forecast_player, ForecastOptions, Point},
};
fn enemy_collision(p: &Point, q: &EnemyPoint, hb: &Value, e: &Value, margin: f64) -> bool {
    wrap(p.state.x, q.x).abs()
        < (num(&hb["halfWidth"]) + num(&e["npcColliderHalfWidthRaw"])) / 4096.0 + margin
        && (p.state.depth - num(&hb["centerOffsetY"]) / 4096.0 - q.depth
            + num(&e["npcColliderCenterYRaw"]) / 4096.0)
            .abs()
            < (num(&hb["halfHeight"]) + num(&e["npcColliderHalfHeightRaw"])) / 4096.0 + margin
}
fn walking_goomba(e: &Value) -> bool {
    text(&e["category"]) == "enemy_goomba"
        && int(&e["goombaBehaviorFunctionRaw"]) == 0x020e1538
        && num(&e["vel"]["x"]).abs() == 2048.0
        && num(&e["vel"]["y"]) == 0.0
        && flag(&e["npcContactFound"])
}
fn abort_jump(
    d: &Value,
    player: usize,
    previous: i64,
    nav: &GrassNavigator,
    fallback: i64,
) -> Option<i64> {
    let obs = &d["observation"];
    let me = &obs["players"][player];
    let raw = &d["runtimePlayers"][player];
    let delay = d["time"]["inputDelay"].as_i64().unwrap_or(-1);
    if previous & !(48 | 2048) != 0
        || fallback & !(48 | 2048) != 0
        || !(0..=6).contains(&delay)
        || raw["subActionFlagRaw"].as_i64().unwrap_or(1) & 1 != 0
        || !flag(&me["contact"]["tileGround"])
        || num(&me["vel"]["x"]).abs() / 4096.0 >= 2.25
        || int(&raw["behaviorFuncRaw"]) != 0x02115aac
    {
        return None;
    }
    let held = fallback | 2050;
    let initial = motion(me, raw, true);
    let occupied = |x, y| nav.occupied(x, y);
    let (points, _) = forecast_player(
        initial,
        previous,
        &planners::sequence(previous, delay as usize, held, 18),
        &occupied,
        ForecastOptions::default(),
    );
    if points.len() != 18 || points.iter().any(|p| p.state.depth > initial.depth + 1.0) {
        return None;
    }
    let hb = &me["hitbox"];
    let mut threat = false;
    for e in array(&obs["entities"]) {
        if !hazard(e) || !near(e, me, 128.0000001, None) {
            continue;
        }
        if !walking_goomba(e) {
            return None;
        }
        let (path, _) = actors::ground_goomba(
            num(&e["pos"]["x"]) / 4096.0,
            -num(&e["pos"]["y"]) / 4096.0,
            num(&e["vel"]["x"]) / 4096.0,
            &occupied,
            18,
            0,
        );
        if path.len() != 18 {
            return None;
        }
        threat |= near(e, me, 32.0, None);
        if points
            .iter()
            .zip(path)
            .any(|(p, q)| enemy_collision(p, &q, hb, e, 1.0))
        {
            return None;
        }
    }
    threat.then_some(held)
}
fn box_choices(
    d: &Value,
    player: usize,
    previous: i64,
    nav: &GrassNavigator,
    box_x: f64,
    box_bottom: f64,
) -> Vec<Value> {
    let obs = &d["observation"];
    let me = &obs["players"][player];
    let other = &obs["players"][player ^ 1];
    let raw = &d["runtimePlayers"][player];
    let delay = d["time"]["inputDelay"].as_i64().unwrap_or(-1);
    let stage = &obs["stage"];
    let hb = &me["hitbox"];
    if stage["id"].as_i64() != Some(0)
        || int(&stage["group"]) != 9
        || !flag(&me["found"])
        || !flag(&hb["found"])
        || int(&hb["fixedPointShift"]) != 12
        || !(0..=6).contains(&delay)
        || !flag(&me["contact"]["tileGround"])
        || !ordinary(raw, me, &[0], &[0, 130])
        || int(&raw["behaviorFuncRaw"]) != 0x02115aac
        || raw["subActionFlagRaw"].as_i64().unwrap_or(1) & 1 != 0
        || previous & !(48 | 2048) != 0
    {
        return vec![];
    }
    let initial = motion(me, raw, true);
    let dx = wrap(box_x, initial.x);
    if dx.abs() >= 112.0
        || !(32.0..=64.0).contains(&(initial.depth - box_bottom))
        || (alive(other) && near(other, me, 64.0, None))
    {
        return vec![];
    }
    let toward = if dx > 0.0 { 16 } else { 32 };
    let away = 48 - toward;
    let mut enemies = vec![];
    for e in array(&obs["entities"]) {
        if !hazard(e) || wrap(num(&e["pos"]["x"]) / 4096.0, initial.x).abs() > 160.0 {
            continue;
        }
        if !walking_goomba(e) {
            return vec![];
        }
        enemies.push(e);
    }
    if enemies.is_empty() {
        return vec![];
    }
    let mut choices = vec![];
    let occupied = |x, y| nav.occupied(x, y);
    for coast in (0..=60).step_by(6) {
        for lead in [0, 6, 12, 18] {
            for brake in [6, 12, 18] {
                let mut sequence = vec![0; coast];
                sequence.extend(vec![toward | 2048; lead]);
                sequence.extend(vec![toward | 2050; brake]);
                sequence.extend([away | 2050; 18]);
                sequence.extend([away | 2048; 18]);
                let mut inputs = vec![previous; delay as usize];
                inputs.extend(&sequence);
                let (path, _) = forecast_player(
                    initial,
                    previous,
                    &inputs,
                    &occupied,
                    ForecastOptions::default(),
                );
                let mut points = vec![];
                let mut hit = None;
                let mut safe = true;
                for (i, p) in path.into_iter().enumerate() {
                    points.push(p);
                    let p = points.last().unwrap();
                    if p.state.depth > initial.depth + 1.0 {
                        safe = false;
                        break;
                    }
                    safe = !enemies.iter().any(|e| {
                        let q = EnemyPoint {
                            x: num(&e["pos"]["x"]) / 4096.0
                                + num(&e["vel"]["x"]) / 4096.0 * (i + 1) as f64,
                            depth: -num(&e["pos"]["y"]) / 4096.0,
                            vx: 0.0,
                        };
                        enemy_collision(p, &q, hb, e, 1.0)
                    });
                    if !safe {
                        break;
                    }
                    if p.contact == Some("ceiling") {
                        if wrap(p.state.x, box_x).abs() <= 4.0
                            && (p.state.depth - (box_bottom + 16.0)).abs() < 0.01
                        {
                            hit = Some(i + 1)
                        }
                        break;
                    }
                }
                if safe {
                    if let Some(hit) = hit {
                        choices.push(json!({"coast":coast,"lead":lead,"brake":brake,"hit":hit,"sequence":sequence,"points":points,"box_x":box_x,"box_bottom":box_bottom}));
                    }
                }
            }
        }
    }
    choices.sort_by(|a, b| {
        int(&a["hit"]).cmp(&int(&b["hit"])).then_with(|| {
            (num(&a["points"][int(&a["hit"]) as usize - 1]["x"]) - box_x)
                .abs()
                .total_cmp(&(num(&b["points"][int(&b["hit"]) as usize - 1]["x"]) - box_x).abs())
        })
    });
    choices
}
impl DevelopmentRule {
    pub(super) fn spacing_box(
        &mut self,
        d: &Value,
        frame: i64,
        previous: i64,
        mut held: i64,
    ) -> i64 {
        let player = self.base.player;
        let obs = &d["observation"];
        let me = &obs["players"][player];
        let other = &obs["players"][player ^ 1];
        let raw = &d["runtimePlayers"][player];
        let valid = obs["stage"]["id"].as_i64() == Some(0)
            && int(&obs["stage"]["group"]) == 9
            && flag(&me["found"])
            && ordinary(raw, me, &[0], &[0, 2, 130, 2050])
            && matches!(int(&raw["behaviorFuncRaw"]), 0x02115aac | 0x021135b8)
            && raw["subActionFlagRaw"].as_i64().unwrap_or(1) & 1 == 0;
        let mut active = self.spacing_box_plan.take();
        if let Some(p) = &active {
            let elapsed = frame - int(&p["start"]);
            let mut reason = if !valid || elapsed < 0 || elapsed >= int(&p["hit"]) {
                Some("finished_or_state_changed")
            } else if elapsed > 0 && differs(me, &p["points"][elapsed as usize - 1], 0.5) {
                Some("motion_deviation")
            } else {
                None
            };
            if reason.is_none() {
                let points = array(&p["points"]);
                if crate::opening::opponent_can_reach(
                    other,
                    &d["runtimePlayers"][player ^ 1],
                    &points[elapsed as usize..(elapsed as usize + 12).min(points.len())],
                ) {
                    reason = Some("opponent_can_reach")
                }
            }
            if reason.is_none() {
                for enemy in array(&obs["entities"]) {
                    if !hazard(enemy) || delta(&enemy["pos"], &me["pos"]).0.abs() > 96.0 {
                        continue;
                    }
                    let original = array(&p["enemies"])
                        .iter()
                        .find(|e| e["actorGuid"] == enemy["actorGuid"]);
                    let Some(original) = original else {
                        reason = Some("new_hazard");
                        break;
                    };
                    let ex = num(&original["pos"]["x"]) / 4096.0
                        + num(&original["vel"]["x"]) / 4096.0 * elapsed as f64;
                    if int(&enemy["goombaBehaviorFunctionRaw"]) != 0x020e1538
                        || wrap(num(&enemy["pos"]["x"]) / 4096.0, ex).abs() > 0.5
                        || enemy["pos"]["y"] != original["pos"]["y"]
                    {
                        reason = Some("enemy_changed");
                        break;
                    }
                }
            }
            if let Some(reason) = reason {
                self.base.trace["spacing_box_cancel"] = json!(reason);
                if reason == "opponent_can_reach" && valid {
                    if let Some(escape) =
                        abort_jump(d, player, previous, &self.spacing_box_nav, held)
                    {
                        held = escape;
                        self.base.jump_until = frame + 12;
                        self.base.trace["spacing_box_abort_jump"] = json!(true);
                        self.base.trace["held"] = json!(held);
                    }
                }
                active = None;
            }
        }
        if active.is_none()
            && valid
            && frame >= self.spacing_box_retry
            && text(&self.base.trace["target"]) == "powerup_box"
            && !self.first_equipment_seen
            && self.base.trace["dx"].as_f64().unwrap_or(999.0).abs() < 32.0
            && previous & 3 == 0
        {
            self.spacing_box_nav.update(me);
            let box_x = num(&me["pos"]["x"]) / 4096.0 + num(&self.base.trace["dx"]);
            let bottom = -num(&me["pos"]["y"]) / 4096.0 - num(&self.base.trace["dy"]) + 8.0;
            let choices = box_choices(d, player, previous, &self.spacing_box_nav, box_x, bottom);
            self.spacing_box_retry = frame + 12;
            if let Some(mut p) = choices.into_iter().next() {
                p["start"] = json!(frame);
                p["enemies"] = json!(array(&obs["entities"])
                    .iter()
                    .filter(|e| text(&e["category"]) == "enemy_goomba")
                    .collect::<Vec<_>>());
                active = Some(p);
            }
        }
        if let Some(p) = &active {
            held = int(&p["sequence"][(frame - int(&p["start"])) as usize]);
            self.base.jump_until = frame;
            self.base.landing_support = None;
            self.base.trace["held"] = json!(held);
            self.base.trace["spacing_box"] = pick(
                p,
                &[
                    "start",
                    "coast",
                    "lead",
                    "brake",
                    "hit",
                    "box_x",
                    "box_bottom",
                ],
            );
        }
        self.spacing_box_plan = active;
        held
    }
    pub(super) fn departure(&mut self, d: &Value, frame: i64, previous: i64, mut held: i64) -> i64 {
        let player = self.base.player;
        let obs = &d["observation"];
        let me = &obs["players"][player];
        let raw = &d["runtimePlayers"][player];
        let direction = i64::from(held & 16 != 0) - i64::from(held & 32 != 0);
        let delay = d["time"]["inputDelay"].as_i64().unwrap_or(-1);
        if int(&obs["stage"]["group"]) != 9
            || obs["stage"]["id"].as_i64() != Some(0)
            || text(&self.base.trace["target"]) != "natural_star"
            || held & 2 == 0
            || previous & 3 != 0
            || held & !(48 | 2050) != 0
            || held & 2048 == 0
            || direction == 0
            || num(&me["vel"]["x"]).abs() / 4096.0 >= 2.25
            || !flag(&me["contact"]["tileGround"])
            || !ordinary(raw, me, &[1, 2], &[0, 130])
            || int(&raw["behaviorFuncRaw"]) != 0x02115aac
            || raw["subActionFlagRaw"].as_i64().unwrap_or(1) & 1 != 0
            || !(0..=6).contains(&delay)
        {
            return held;
        }
        self.departure_nav.update(me);
        let nav = &self.departure_nav;
        let occupied = |x, y| nav.occupied(x, y);
        let initial = motion(me, raw, true);
        let options = ForecastOptions {
            height: 27.0,
            ..Default::default()
        };
        let (nominal, _) = forecast_player(
            initial,
            previous,
            &planners::sequence(previous, delay as usize, held, delay as usize + 18),
            &occupied,
            options,
        );
        let Some(ceiling) = nominal
            .iter()
            .position(|p| p.contact == Some("ceiling"))
            .map(|i| i + 1)
        else {
            return held;
        };
        let alternative = held & !3;
        let (points, _) = forecast_player(
            initial,
            previous,
            &planners::sequence(previous, delay as usize, alternative, delay as usize + 12),
            &occupied,
            options,
        );
        let values: Vec<Value> = points.iter().map(|p| json!(p)).collect();
        if points.len() != delay as usize + 12
            || points
                .iter()
                .any(|p| !p.state.grounded || p.state.depth != initial.depth || p.contact.is_some())
            || crate::opening::opponent_can_reach(
                &obs["players"][player ^ 1],
                &d["runtimePlayers"][player ^ 1],
                &values,
            )
            || !crate::opening::counterjump_clear(d, player, &values, nav, true)
        {
            return held;
        }
        for e in array(&obs["entities"]) {
            if !matches!(text(&e["category"]), "enemy_goomba" | "enemy_koopa")
                || delta(&e["pos"], &me["pos"]).0.abs() > 256.0
                || int(&e["goombaBehaviorFunctionRaw"]) == 0x020e13d0
            {
                continue;
            }
            if !walking_goomba(e) {
                return held;
            }
            let (enemies, _) = actors::ground_goomba(
                num(&e["pos"]["x"]) / 4096.0,
                -num(&e["pos"]["y"]) / 4096.0,
                num(&e["vel"]["x"]) / 4096.0,
                &occupied,
                points.len(),
                0,
            );
            if enemies.len() != points.len()
                || points
                    .iter()
                    .zip(enemies)
                    .any(|(p, q)| enemy_collision(p, &q, &me["hitbox"], e, 2.0))
            {
                return held;
            }
        }
        held = alternative;
        self.base.jump_until = frame;
        self.base.next_jump = frame + 6;
        self.base.trace["held"] = json!(held);
        self.base.trace["departure_brake"] = json!({"predicted_ceiling":ceiling});
        held
    }
}
