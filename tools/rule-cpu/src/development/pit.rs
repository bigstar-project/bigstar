use super::*;
use crate::{
    navigation::GrassNavigator,
    physics::{forecast_player, ForecastOptions, Point},
    planners::sequence,
};

pub fn late_wall_depth(
    points: &[Point],
    button: i64,
    previous: i64,
    delay: i64,
    nav: &GrassNavigator,
    height: f64,
) -> Option<Value> {
    if points.is_empty() || previous & 3 != 0 || button & 3 != 0 || !(0..=6).contains(&delay) {
        return None;
    }
    let side = i64::from(button & 16 != 0) - i64::from(button & 32 != 0);
    if side == 0 {
        return None;
    }
    let sensor = if side < 0 { -9.0 } else { 8.0 };
    for (i, point) in points.iter().enumerate() {
        let index = i as i64 + 1;
        let p = &point.state;
        if p.grounded || p.vy >= 0.0 {
            return None;
        }
        if ![4.0, height - 8.0]
            .iter()
            .any(|h| nav.occupied(p.x + sensor, p.depth - h))
        {
            continue;
        }
        if p.vx * side as f64 <= 0.0703125 {
            return None;
        }
        let next_decision = (index + 6) / 6 * 6;
        let prepare = next_decision + delay + 1;
        let mut depth = p.depth;
        let mut velocity = p.vy;
        for _ in index + 1..prepare {
            velocity = if velocity < -2.5 {
                (velocity + 0.34375).min(-2.5)
            } else {
                (velocity - 0.34375).max(-2.5)
            };
            depth -= velocity;
            if ![4.0, height - 8.0]
                .iter()
                .any(|h| nav.occupied(p.x + sensor, depth - h))
            {
                return None;
            }
        }
        return Some(
            json!({"contact_after":index,"next_decision_after":next_decision,"prepare_after":prepare,"peak_depth":depth}),
        );
    }
    None
}
impl DevelopmentRule {
    pub(super) fn imminent_wall(
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
        let other = &obs["players"][player ^ 1];
        let s = motion(me, raw, false);
        let allowed = obs["stage"]["id"].as_i64() == Some(0)
            && obs["stage"]["vsMode"].as_i64() == Some(1)
            && !flag(&me["dead"])
            && matches!(int(&raw["currentPowerupRaw"]), 0..=2)
            && raw["damageStateRaw"].as_i64() == Some(0)
            && raw["updateLockedRaw"].as_i64() == Some(0)
            && !me["contact"]["tileGround"].as_bool().unwrap_or(true);
        let mut active = self.imminent_wall_entry.take();
        if active.as_ref().is_some_and(|p| {
            !allowed
                || int(&raw["actionFlagRaw"]) & 32 != 0
                || frame - int(&p["start"]) >= 24
                || s.vy > 0.0
        }) {
            self.base.trace["imminent_wall_entry_end"] = json!(frame);
            active = None;
        }
        if active.is_none() && allowed && s.vy < 0.0 {
            let direction = i64::from(held & 16 != 0) - i64::from(held & 32 != 0);
            let mut eligible = direction != 0
                && int(&raw["actionFlagRaw"]) == 0x100000
                && int(&raw["behaviorFuncRaw"]) == 0x021135b8
                && matches!(int(&raw["physicsFlagRaw"]), 0 | 2 | 128 | 130 | 2050)
                && matches!(
                    text(&self.base.trace["target"]),
                    "carrier" | "wait" | "patrol"
                )
                && frame >= self.base.wall_depart_until;
            if alive(other) && near(other, me, 48.0, Some(64.0)) {
                eligible = false;
            }
            for e in array(&obs["entities"]) {
                if !hazard(e) {
                    continue;
                }
                if text(&e["category"]) == "player_fireball"
                    && flag(&e["ownerVerified"])
                    && e["owner"].as_i64() == Some(player as i64)
                {
                    continue;
                }
                if text(&e["category"]) == "enemy_goomba"
                    && flag(&e["entityUpdateStateFound"])
                    && int(&e["entityUpdateStateRaw"]) == 2
                {
                    continue;
                }
                if near(e, me, 48.0, Some(64.0)) {
                    eligible = false;
                }
            }
            if eligible {
                self.imminent_wall_navigator.update(me);
                let nav = &self.imminent_wall_navigator;
                let delay = d["time"]["inputDelay"].as_i64().unwrap_or(2);
                if (0..=6).contains(&delay) {
                    let (points, reason) = forecast_player(
                        s,
                        previous,
                        &sequence(previous, delay as usize, held, delay as usize + 6),
                        &|x, y| nav.occupied(x, y),
                        ForecastOptions {
                            height: if int(&raw["currentPowerupRaw"]) == 0 {
                                16.0
                            } else {
                                27.0
                            },
                            ..Default::default()
                        },
                    );
                    if reason == "wall" && !points.is_empty() && points.len() <= 6 {
                        let end = points.last().unwrap().state;
                        let side = if end.vx > 0.0 { 1 } else { -1 };
                        let left = (8..65)
                            .step_by(8)
                            .find(|&n| nav.occupied(end.x - n as f64, end.depth - 4.0));
                        let right = (8..65)
                            .step_by(8)
                            .find(|&n| nav.occupied(end.x + n as f64, end.depth - 4.0));
                        let shaft = left.zip(right).is_some_and(|(l, r)| l + r <= 48);
                        let no_floor = ![-5.0, 0.0, 4.0].iter().any(|dx| {
                            [1.0, 8.0, 16.0]
                                .iter()
                                .any(|dy| nav.occupied(end.x + dx, end.depth + dy))
                        });
                        if end.vx.abs() > 0.0703125
                            && side != direction
                            && shaft
                            && no_floor
                            && end.depth + 4.0 * ((delay + 2) as f64) < 336.0
                        {
                            active = Some(json!({"start":frame,"side":side}));
                            self.base.trace["imminent_wall_entry_prediction"] = json!({"nominal_held":held,"side":side,"frames":points.len(),"x":end.x,"depth":end.depth});
                        }
                    }
                }
            }
        }
        if let Some(plan) = &active {
            let side = int(&plan["side"]);
            let contact = flag(&me["contact"][if side > 0 { "wallRight" } else { "wallLeft" }]);
            held = buttons(side) | if contact && previous & 2 == 0 { 2 } else { 0 };
            let mut info = plan.clone();
            info["contact"] = json!(contact);
            self.base.trace["held"] = json!(held);
            self.base.trace["imminent_wall_entry"] = info;
        }
        self.imminent_wall_entry = active;
        held
    }
    pub(super) fn missed_takeoff(
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
        let other = &obs["players"][player ^ 1];
        let entities = array(&obs["entities"]);
        let grounded = flag(&me["contact"]["tileGround"]);
        let delay = d["time"]["inputDelay"].as_i64().unwrap_or(-1);
        let s = motion(me, raw, grounded);
        let eligible = obs["stage"]["id"].as_i64() == Some(0)
            && obs["stage"]["vsMode"].as_i64() == Some(1)
            && ordinary(raw, me, &[0], &[0, 2, 128, 130, 2050])
            && (0..=6).contains(&delay)
            && !(alive(other) && near(other, me, 96.0, Some(64.0)));
        let mut active = self.missed_takeoff_retry.take();
        let attempt = grounded && held & 2 != 0 && previous & 2 == 0;
        if !eligible
            || active
                .as_ref()
                .is_some_and(|p| frame - int(&p["start"]) >= 36 || s.vy > 0.0)
        {
            return held;
        }
        if active.is_none() && !attempt {
            return held;
        }
        self.missed_takeoff_navigator.update(me);
        let nav = &self.missed_takeoff_navigator;
        let mut hazards = vec![];
        for e in entities {
            let (dx, dy) = delta(&e["pos"], &me["pos"]);
            if !hazard(e) || dx.abs() >= 128.0 || dy.abs() >= 96.0 {
                continue;
            }
            if text(&e["category"]) == "enemy_goomba"
                && flag(&e["entityUpdateStateFound"])
                && int(&e["entityUpdateStateRaw"]) == 2
            {
                continue;
            }
            if text(&e["category"]) != "enemy_koopa"
                || int(&e["koopaBehaviorFunctionRaw"]) != 0x020dfc58
                || e["koopaShellModeRaw"].as_i64() != Some(0)
            {
                return held;
            }
            let (points, end) = actors::ground_koopa(e, &|x, y| nav.occupied(x, y), 24);
            if end != "horizon" || points.len() != 24 {
                return held;
            }
            hazards.push(json!({"guid":e["actorGuid"],"points":points}));
        }
        if hazards.is_empty() {
            return held;
        }
        let hitbox = &me["hitbox"];
        if !flag(&hitbox["found"]) || int(&hitbox["fixedPointShift"]) != 12 {
            return held;
        }
        let cx = num(&hitbox["centerOffsetX"]) / 4096.0;
        let cy = num(&hitbox["centerOffsetY"]) / 4096.0;
        let hw = num(&hitbox["halfWidth"]) / 4096.0;
        let hh = num(&hitbox["halfHeight"]) / 4096.0;
        let predict = |button| {
            forecast_player(
                s,
                previous,
                &sequence(previous, delay as usize, button, 24),
                &|x, y| nav.occupied(x, y),
                ForecastOptions::default(),
            )
        };
        let close = |points: &[Point]| {
            points.iter().enumerate().any(|(i, p)| {
                hazards.iter().any(|h| {
                    wrap(p.state.x + cx, num(&h["points"][i]["x"])).abs() < hw + 8.0 + 0.25
                        && ((p.state.depth - cy) - (num(&h["points"][i]["depth"]) - 8.0)).abs()
                            < hh + 8.0 + 1.0
                })
            })
        };
        if let Some(plan) = &active {
            let elapsed = frame - int(&plan["verified_at"]);
            if elapsed <= 0 || elapsed as usize > array(&plan["points"]).len() {
                return held;
            }
            let expected = &plan["points"][elapsed as usize - 1];
            let mut valid = wrap(s.x, num(&expected["x"])).abs() <= 0.25
                && (s.depth - num(&expected["depth"])).abs() <= 0.75;
            for h in array(&plan["hazards"]) {
                if entities
                    .iter()
                    .find(|e| e["actorGuid"] == h["guid"])
                    .is_none_or(|a| differs(a, &h["points"][elapsed as usize - 1], 0.25))
                {
                    valid = false;
                }
            }
            if !valid {
                return held;
            }
        }
        if active.is_none() {
            let (points, end) = predict(held);
            let prefix = &points[..points.len().min(delay as usize + 1)];
            let lost = prefix.iter().any(|p| p.contact == Some("edge"));
            let missed = points.len() > delay as usize && !prefix.iter().any(|p| p.launch);
            if !(end == "horizon" && lost && missed && close(&points)) {
                return held;
            }
            self.base.trace["missed_takeoff_prediction"] = json!({"delay":delay,"initial_held":held,"edge_after":points.iter().position(|p|p.contact==Some("edge")).unwrap()+1});
            active =
                Some(json!({"start":frame,"verified_at":frame,"points":points,"hazards":hazards}));
        }
        held &= !2;
        self.base.jump_until = frame;
        self.base.next_jump = frame;
        if previous & 2 == 0 {
            let (jump_points, end) = predict(held | 2);
            let (nominal, _) = predict(held);
            let timely = jump_points.get(delay as usize).is_some_and(|p| p.launch);
            let safe = end == "horizon"
                && jump_points.len() == 24
                && !jump_points.iter().any(|p| p.contact == Some("ceiling"))
                && !close(&jump_points);
            if timely && safe && close(&nominal[..nominal.len().min(18)]) {
                held |= 2;
                self.base.jump_until = frame + 30;
                self.base.next_jump = frame + 42;
                self.base.trace["missed_takeoff_retry_launch"] =
                    json!({"start":active.as_ref().unwrap()["start"],"frame":frame});
                active = None;
            }
        }
        if let Some(plan) = active.as_mut() {
            let (points, _) = predict(held);
            plan["verified_at"] = json!(frame);
            plan["points"] = json!(points);
            plan["hazards"] = json!(hazards);
        }
        self.base.trace["held"] = json!(held);
        self.base.trace["missed_takeoff_retry"] = active
            .as_ref()
            .map_or(Value::Null, |p| json!({"start":p["start"]}));
        self.missed_takeoff_retry = active;
        held
    }
    pub(super) fn shaft_contact(
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
        let other = &obs["players"][player ^ 1];
        let s = motion(me, raw, false);
        let height = if int(&raw["currentPowerupRaw"]) == 0 {
            16.0
        } else {
            27.0
        };
        let mut eligible = ordinary(raw, me, &[0, 1, 2], &[0, 2, 128, 130, 2050])
            && flag(&self.base.trace["recovery"])
            && flag(&self.base.trace["in_shaft"])
            && !me["contact"]["tileGround"].as_bool().unwrap_or(true)
            && num(&me["vel"]["y"]) < 0.0
            && int(&raw["behaviorFuncRaw"]) == 0x021135b8
            && int(&raw["actionFlagRaw"]) == 0x100000
            && frame >= self.base.wall_depart_until;
        if alive(other) && near(other, me, 48.0, Some(64.0)) {
            eligible = false;
        }
        for e in array(&obs["entities"]) {
            if !hazard(e) {
                continue;
            }
            if text(&e["category"]) == "player_fireball"
                && flag(&e["ownerVerified"])
                && e["owner"].as_i64() == Some(player as i64)
            {
                continue;
            }
            if text(&e["category"]) == "enemy_goomba"
                && flag(&e["entityUpdateStateFound"])
                && int(&e["entityUpdateStateRaw"]) == 2
            {
                continue;
            }
            if near(e, me, 48.0, Some(64.0)) {
                eligible = false;
            }
        }
        self.shaft_contact_navigator.update(me);
        let nav = &self.shaft_contact_navigator;
        let signature = json!(nav.signature());
        if eligible {
            let mut tops = vec![];
            for side in [-1, 1] {
                let wall_x = (8..65)
                    .step_by(8)
                    .map(|offset| s.x + (side * offset) as f64)
                    .find(|&x| nav.occupied(x, s.depth - 4.0));
                let Some(wall_x) = wall_x else {
                    eligible = false;
                    break;
                };
                let mut top = ((s.depth - 4.0) / 16.0).floor() * 16.0;
                while top > 0.0 && nav.occupied(wall_x, top - 1.0) {
                    top -= 16.0;
                }
                tops.push(top);
            }
            if tops.is_empty()
                || s.depth - height < tops.into_iter().max_by(f64::total_cmp).unwrap()
            {
                eligible = false;
            }
        }
        let mut active = self.shaft_contact_plan.take();
        if let Some(plan) = &active {
            let elapsed = frame - int(&plan["start"]);
            let reason = if !eligible {
                Some("state_or_contact")
            } else if elapsed >= int(&plan["duration"]) {
                Some("contact_deadline")
            } else if signature != plan["terrain_signature"] {
                Some("terrain_changed")
            } else if elapsed > 0 && differs(me, &plan["points"][elapsed as usize - 1], 1.0) {
                Some("motion_deviation")
            } else {
                None
            };
            if let Some(reason) = reason {
                self.base.trace["shaft_contact_cancel"] = json!(reason);
                active = None;
            }
        }
        let direction = i64::from(held & 16 != 0) - i64::from(held & 32 != 0);
        if active.is_none() && eligible && direction != 0 && held & 3 == 0 {
            let delay = d["time"]["inputDelay"].as_i64().unwrap_or(2);
            if (0..=6).contains(&delay) {
                let predict = |button| {
                    let (points, end) = forecast_player(
                        s,
                        previous,
                        &sequence(previous, delay as usize, button, 36),
                        &|x, y| nav.occupied(x, y),
                        ForecastOptions {
                            height,
                            ..Default::default()
                        },
                    );
                    let margin = end == "wall"
                        && points
                            .last()
                            .is_some_and(|p| p.state.depth + 4.0 * ((delay + 2) as f64) < 336.0);
                    (points, end, margin)
                };
                let (nominal, nominal_reason, nominal_margin) = predict(held);
                if !nominal_margin {
                    let mut choices = vec![];
                    for button in [16, 32] {
                        let (points, _, margin) = predict(button);
                        if margin {
                            choices.push(
                                json!({"held":button,"duration":points.len(),"points":points}),
                            );
                        }
                    }
                    if choices.is_empty() {
                        let late = |points: &[Point], reason: &str, button| {
                            reason == "wall"
                                && late_wall_depth(points, button, previous, delay, nav, height)
                                    .is_some_and(|p| num(&p["peak_depth"]) < 352.0)
                        };
                        if !late(&nominal, nominal_reason, held) {
                            for button in [16, 32] {
                                let (points, reason, _) = predict(button);
                                if late(&points, reason, button) {
                                    choices.push(json!({"held":button,"points":points,"duration":points.len()}));
                                }
                            }
                        }
                    }
                    if let Some(mut best) = choices
                        .into_iter()
                        .min_by_key(|p| (int(&p["duration"]), int(&p["held"]) != held & 48))
                    {
                        best["start"] = json!(frame);
                        best["terrain_signature"] = signature;
                        active = Some(best);
                        self.base.trace["shaft_missed_contact_nominal"] =
                            json!({"held":held,"reason":nominal_reason,"frames":nominal.len()});
                    }
                }
            }
        }
        self.shaft_contact_previous = Some(json!({"frame":frame,"eligible":eligible}));
        if let Some(plan) = &active {
            held = (held & !(48 | 2048)) | int(&plan["held"]);
            self.base.trace["held"] = json!(held);
            self.base.trace["shaft_contact_plan"] = pick(plan, &["start", "held", "duration"]);
        }
        self.shaft_contact_plan = active;
        held
    }
}
