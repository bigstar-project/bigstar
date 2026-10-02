use super::*;
use crate::physics::{forecast_player, forecast_upper, ForecastOptions, Point};

fn guid(e: &Value) -> String {
    if let Some(s) = e["actorGuid"].as_str() {
        s.to_string()
    } else if e["actorGuid"].is_null() {
        "None".to_string()
    } else {
        e["actorGuid"].to_string()
    }
}
pub(super) fn defeated(e: &Value) -> bool {
    flag(&e["entityUpdateStateFound"]) && int(&e["entityUpdateStateRaw"]) == 2
}
pub(super) fn own_fire(e: &Value, player: usize) -> bool {
    text(&e["category"]) == "player_fireball"
        && flag(&e["ownerVerified"])
        && e["owner"].as_u64() == Some(player as u64)
}
fn error(me: &Value, p: &Value) -> f64 {
    wrap(num(&me["pos"]["x"]) / 4096.0, num(&p["x"]))
        .abs()
        .max((-num(&me["pos"]["y"]) / 4096.0 - num(&p["depth"])).abs())
        .max((num(&me["vel"]["x"]) / 4096.0 - num(&p["vx"])).abs())
        .max((num(&me["vel"]["y"]) / 4096.0 - num(&p["vy"])).abs())
}
fn koopa_bounds(
    e: &Value,
    occupied: &impl Fn(f64, f64) -> bool,
    horizon: usize,
) -> (Vec<Value>, &'static str) {
    let (path, status) = actors::ground_koopa(e, occupied, horizon);
    let mut points: Vec<Value> = path
        .iter()
        .map(|p| {
            let mut v = json!(p);
            v["radius"] = json!(0);
            v
        })
        .collect();
    if status != "walking_turn" || points.is_empty() {
        return (points, status);
    }
    let last = points.last().unwrap();
    let x = num(&last["x"]) + num(&e["vel"]["x"]) / 4096.0;
    let depth = num(&last["depth"]);
    let known = points.len();
    for i in known..horizon {
        let radius = 0.5 * (i as i64 - known as i64 - 9).max(0) as f64;
        let mut q = ((x - radius) / 16.0).floor() * 16.0;
        let end = ((x + radius) / 16.0).ceil() * 16.0;
        if !occupied(x - radius, depth + 1.0) || !occupied(x + radius, depth + 1.0) {
            return (points, "uncertain_support");
        }
        while q <= end {
            if !occupied(q, depth + 1.0) {
                return (points, "uncertain_support");
            }
            q += 16.0;
        }
        points.push(json!({"x":x,"depth":depth,"radius":radius}));
    }
    (points, "bounded_turn")
}
fn clearance(points: &[Point], hazards: &[(Vec<Value>, f64)], hb: &Value, mx: f64, my: f64) -> f64 {
    let (cx, cy, hw, hh) = (
        num(&hb["centerOffsetX"]) / 4096.0,
        num(&hb["centerOffsetY"]) / 4096.0,
        num(&hb["halfWidth"]) / 4096.0,
        num(&hb["halfHeight"]) / 4096.0,
    );
    points
        .iter()
        .enumerate()
        .flat_map(|(i, p)| {
            hazards.iter().map(move |(path, ew)| {
                let q = &path[i];
                ((wrap(p.state.x + cx, num(&q["x"])).abs() - num(&q["radius"])).max(0.0)
                    - hw
                    - ew
                    - mx)
                    .max(((p.state.depth - cy) - (num(&q["depth"]) - 8.0)).abs() - hh - 8.0 - my)
            })
        })
        .fold(f64::INFINITY, f64::min)
}
pub(super) fn opponent_unsafe(d: &Value, player: usize) -> bool {
    let other = &d["observation"]["players"][player ^ 1];
    let raw = &d["runtimePlayers"][player ^ 1];
    !flag(&other["found"])
        || raw.get("currentPowerupRaw").is_none()
        || raw.get("physicsFlagRaw").is_none()
        || int(&raw["currentPowerupRaw"]) == 3
        || int(&raw["physicsFlagRaw"]) & 32 != 0
        || flag(&other["visual"]["starInvincible"])
}
impl DevelopmentRule {
    fn hop_proposal(&mut self, d: &Value, previous: i64) -> Option<Value> {
        let waiting = self.emergence_plan.as_ref()?;
        if previous & 3 != 0 {
            return None;
        }
        let player = self.base.player;
        let obs = &d["observation"];
        let me = &obs["players"][player];
        let other = &obs["players"][player ^ 1];
        let raw = &d["runtimePlayers"][player];
        let other_raw = &d["runtimePlayers"][player ^ 1];
        if !ordinary(raw, me, &[0], &[2, 130, 2050])
            || int(&raw["behaviorFuncRaw"]) != 0x02115aac
            || !flag(&me["contact"]["tileGround"])
            || num(&me["vel"]["x"]).abs() / 4096.0 > 0.25
            || !alive(other)
            || int(&other_raw["currentPowerupRaw"]) == 3
            || int(&other_raw["physicsFlagRaw"]) & 32 != 0
        {
            return None;
        }
        let (ox, oy) = delta(&other["pos"], &me["pos"]);
        if !(12.0 < ox.abs() && ox.abs() < 64.0) || oy.abs() > 16.0 {
            return None;
        }
        let entities = array(&obs["entities"]);
        let item = entities
            .iter()
            .find(|e| e["actorGuid"] == waiting["item_guid"])?;
        if int(&item["objectId"]) != 31
            || item["itemKindRaw"].as_i64() != Some(0)
            || int(&item["itemBehaviorFunctionRaw"]) != 0x020d3a30
            || item["entityUpdateStateRaw"].as_i64() != Some(0)
            || num(&item["vel"]["x"]).abs() != 4096.0
            || !(-16384.0..=0.0).contains(&num(&item["vel"]["y"]))
        {
            return None;
        }
        self.base.navigator.update(me);
        let nav = &self.base.navigator;
        let occupied = |x, y| nav.occupied(x, y);
        let horizon = 42;
        let delay = d["time"]["inputDelay"].as_i64().unwrap_or(2);
        if !(0..=6).contains(&delay) {
            return None;
        }
        let item_points = actors::item(
            num(&item["pos"]["x"]) / 4096.0,
            -num(&item["pos"]["y"]) / 4096.0,
            num(&item["vel"]["x"]) / 4096.0,
            num(&item["vel"]["y"]) / 4096.0,
            &occupied,
            horizon,
        );
        let mut hazards: Vec<Vec<Value>> = vec![];
        for e in entities {
            if !hazard(e) || defeated(e) {
                continue;
            }
            let (dx, dy) = delta(&e["pos"], &me["pos"]);
            if dx.abs() > 160.0 || dy.abs() > 128.0 {
                continue;
            }
            let (path, status) = if text(&e["category"]) == "enemy_goomba" {
                let (p, s) = actors::short_goomba(e, &occupied, horizon);
                (p.iter().map(|v| json!(v)).collect::<Vec<_>>(), s)
            } else if text(&e["category"]) == "enemy_koopa"
                && e["koopaShellModeRaw"].as_i64() == Some(0)
                && int(&e["koopaBehaviorFunctionRaw"]) == 0x020dfc58
            {
                let (p, s) = actors::ground_koopa(e, &occupied, horizon);
                (p.iter().map(|v| json!(v)).collect(), s)
            } else {
                return None;
            };
            if path.len() != horizon || status != "horizon" {
                return None;
            }
            hazards.push(path);
        }
        let initial = motion(me, raw, true);
        let first_overlap = |points: &[Point]| {
            points
                .iter()
                .zip(&item_points)
                .position(|(p, q)| {
                    wrap(p.state.x, q.x).abs() < 6.0 && (p.state.depth - q.depth).abs() < 6.0
                })
                .map(|i| i + 1)
        };
        let (staying, _) = forecast_player(
            initial,
            previous,
            &planners::sequence(previous, delay as usize, 0, horizon),
            &occupied,
            ForecastOptions::default(),
        );
        let wait_pickup = first_overlap(&staying);
        let mut proposals = vec![];
        for hold in [6, 12, 18, 24, 30] {
            let mut inputs = vec![previous; delay as usize];
            inputs.extend(vec![2; hold]);
            inputs.resize(horizon, 0);
            let (points, _) = forecast_player(
                initial,
                previous,
                &inputs,
                &occupied,
                ForecastOptions::default(),
            );
            let Some(pickup) = first_overlap(&points) else {
                continue;
            };
            if wait_pickup.is_some_and(|w| pickup + 6 > w)
                || points.len() != horizon
                || points.iter().any(|p| p.state.depth > 288.0)
            {
                continue;
            }
            if hazards.iter().any(|path| {
                points.iter().zip(path).any(|(p, q)| {
                    (wrap(p.state.x, num(&q["x"])).abs() - 16.0)
                        .max(p.state.depth - 16.0 - num(&q["depth"]))
                        .max(num(&q["depth"]) - 20.0 - p.state.depth)
                        < 4.0
                })
            }) {
                continue;
            }
            proposals.push(json!({"hold":hold,"pickup_after":pickup,"wait_pickup_after":wait_pickup,"item_guid":item["actorGuid"],"points":points,"item_points":item_points}));
        }
        proposals
            .into_iter()
            .min_by_key(|p| (int(&p["pickup_after"]), int(&p["hold"])))
    }
    pub(super) fn contested_hop(
        &mut self,
        d: &Value,
        frame: i64,
        previous: i64,
        mut held: i64,
    ) -> i64 {
        let me = &d["observation"]["players"][self.base.player];
        let raw = &d["runtimePlayers"][self.base.player];
        let mut active = self.contested_mushroom_hop.take();
        if let Some(plan) = &active {
            let elapsed = frame - int(&plan["start"]);
            let entity = array(&d["observation"]["entities"])
                .iter()
                .find(|e| e["actorGuid"] == plan["item_guid"]);
            if elapsed < 0
                || elapsed as usize > array(&plan["points"]).len()
                || elapsed >= int(&plan["pickup_after"]) + 6
                || flag(&me["dead"])
                || int(&raw["currentPowerupRaw"]) != 0
                || flag(&raw["damageStateRaw"])
                || flag(&raw["updateLockedRaw"])
                || entity.is_none()
                || (elapsed > 0
                    && (differs(me, &plan["points"][elapsed as usize - 1], 2.0)
                        || differs(
                            entity.unwrap(),
                            &plan["item_points"][elapsed as usize - 1],
                            2.0,
                        )))
            {
                active = None;
            }
        }
        if active.is_none() && text(&self.base.trace["target"]) == "powerup" {
            active = self.hop_proposal(d, previous).map(|mut p| {
                p["start"] = json!(frame);
                p
            });
        }
        if let Some(plan) = &active {
            held = if frame - int(&plan["start"]) < int(&plan["hold"]) {
                2
            } else {
                0
            };
            let mut trace = plan.clone();
            trace.as_object_mut().unwrap().remove("points");
            trace.as_object_mut().unwrap().remove("item_points");
            self.base.trace["held"] = json!(held);
            self.base.trace["contested_mushroom_hop"] = trace;
        }
        self.contested_mushroom_hop = active;
        held
    }
    fn forward_proposal(&mut self, d: &Value, held: i64, previous: i64) -> Option<Value> {
        let player = self.base.player;
        let me = &d["observation"]["players"][player];
        let raw = &d["runtimePlayers"][player];
        if [
            "currentPowerupRaw",
            "behaviorFuncRaw",
            "physicsFlagRaw",
            "actionFlagRaw",
            "damageStateRaw",
            "damageCooldownRaw",
            "updateLockedRaw",
            "facing",
        ]
        .iter()
        .any(|k| raw.get(k).is_none())
        {
            return None;
        }
        let direction = if num(&me["vel"]["x"]) > 0.0 { 1 } else { -1 };
        let wanted = i64::from(held & 16 != 0) - i64::from(held & 32 != 0);
        if !ordinary(raw, me, &[1, 2], &[2, 130, 2050])
            || int(&raw["behaviorFuncRaw"]) != 0x02115aac
            || flag(&raw["damageCooldownRaw"])
            || flag(&me["visual"]["starInvincible"])
            || !flag(&me["contact"]["tileGround"])
            || !(1.0..=3.0).contains(&(num(&me["vel"]["x"]).abs() / 4096.0))
            || wanted != -direction
            || held & 3 == 0
            || previous & 3 != 0
            || int(&raw["facing"]) != direction
        {
            return None;
        }
        let hb = &me["hitbox"];
        if !flag(&hb["found"]) || int(&hb["fixedPointShift"]) != 12 {
            return None;
        }
        let delay = d["time"]["inputDelay"].as_i64().unwrap_or(-1);
        let horizon = 30;
        if !(0..=6).contains(&delay) {
            return None;
        }
        self.base.navigator.update(me);
        let occupied = |x, y| self.base.navigator.occupied(x, y);
        let mut tracks = json!({});
        let mut hazards = vec![];
        let mut falling = false;
        for e in array(&d["observation"]["entities"]) {
            if !hazard(e) || defeated(e) {
                continue;
            }
            let (dx, dy) = delta(&e["pos"], &me["pos"]);
            if text(&e["category"]) == "player_fireball" {
                if own_fire(e, player) {
                    continue;
                }
                if dx.abs() < 152.0 && dy.abs() < 128.0 {
                    return None;
                }
                continue;
            }
            if dx.abs() > 137.0 || dy.abs() > 128.0 {
                continue;
            }
            if text(&e["category"]) != "enemy_goomba" {
                return None;
            }
            let (path, status) = actors::short_goomba(e, &occupied, horizon);
            if status != "horizon" || path.len() != horizon || e["actorGuid"].is_null() {
                return None;
            }
            tracks[guid(e)] = json!(path);
            hazards.push((path.iter().map(|p| json!(p)).collect(), 7.0));
            falling |= 0.0 < direction as f64 * dx
                && direction as f64 * dx < 64.0
                && dy > 24.0
                && dy < 80.0
                && num(&e["vel"]["y"]) < 0.0;
        }
        if !falling {
            return None;
        }
        let initial = motion(me, raw, true);
        let options = ForecastOptions {
            height: 32.0,
            ..Default::default()
        };
        let (nominal, _) = forecast_player(
            initial,
            previous,
            &planners::sequence(previous, delay as usize, held, horizon),
            &occupied,
            options,
        );
        if nominal.len() < 18 || clearance(&nominal[..18], &hazards, hb, 0.0, 0.0) >= 0.0 {
            return None;
        }
        let forward = buttons(direction) | 2048;
        let (points, status) = forecast_player(
            initial,
            previous,
            &planners::sequence(previous, delay as usize, forward, horizon),
            &occupied,
            options,
        );
        if status != "horizon"
            || points.len() != horizon
            || points
                .iter()
                .any(|p| !p.state.grounded || p.contact.is_some() || p.state.depth != initial.depth)
        {
            return None;
        }
        let minimum = clearance(&points, &hazards, hb, 0.0, 0.0);
        if minimum < 0.5 {
            return None;
        }
        let other = &d["observation"]["players"][player ^ 1];
        if alive(other)
            && points.iter().enumerate().any(|(i, p)| {
                wrap(p.state.x, num(&other["pos"]["x"]) / 4096.0).abs()
                    < 24.0 + 4.0 * (i + 1) as f64
            })
        {
            return None;
        }
        let cx = num(&hb["centerOffsetX"]) / 4096.0;
        let hw = num(&hb["halfWidth"]) / 4096.0;
        let finish = (6..=horizon).step_by(6).find(|&i| {
            hazards.iter().all(|(q, _)| {
                direction as f64 * wrap(points[i - 1].state.x + cx, num(&q[i - 1]["x"])) > hw + 9.0
            })
        })?;
        if opponent_unsafe(d, player) {
            return None;
        }
        Some(
            json!({"held":forward,"points":points,"tracks":tracks,"duration":finish,"minimum_clearance":minimum,"form":raw["currentPowerupRaw"]}),
        )
    }
    pub(super) fn forward_npc(
        &mut self,
        d: &Value,
        frame: i64,
        previous: i64,
        mut held: i64,
    ) -> i64 {
        let player = self.base.player;
        let me = &d["observation"]["players"][player];
        let raw = &d["runtimePlayers"][player];
        if let Some(plan) = self.forward_npc_plan.take() {
            let elapsed = frame - int(&plan["start"]);
            let expected = if elapsed > 0 {
                array(&plan["points"]).get(elapsed as usize - 1)
            } else {
                None
            };
            let err = expected.map(|p| error(me, p));
            let mut valid = err.is_some_and(|e| e <= 0.25)
                && !flag(&me["dead"])
                && raw["currentPowerupRaw"] == plan["form"]
                && raw["damageStateRaw"].as_i64() == Some(0)
                && raw["damageCooldownRaw"].as_i64() == Some(0)
                && raw["updateLockedRaw"].as_i64() == Some(0)
                && int(&raw["behaviorFuncRaw"]) == 0x02115aac
                && flag(&me["contact"]["tileGround"]);
            for e in array(&d["observation"]["entities"]) {
                if !hazard(e) || defeated(e) || own_fire(e, player) {
                    continue;
                }
                let (dx, dy) = delta(&e["pos"], &me["pos"]);
                if dx.abs() > 128.0 || dy.abs() > 128.0 {
                    continue;
                }
                let q = if elapsed > 0 {
                    array(&plan["tracks"][guid(e)]).get(elapsed as usize - 1)
                } else {
                    None
                };
                if q.is_none_or(|q| differs(e, q, 0.25)) {
                    valid = false
                }
            }
            if valid && elapsed < int(&plan["duration"]) {
                held = int(&plan["held"]);
                self.base.jump_until = frame;
                self.base.trace["held"] = json!(held);
                self.base.trace["forward_npc_active"] = json!({"start":plan["start"],"elapsed":elapsed,"duration":plan["duration"],"error":err});
                self.forward_npc_plan = Some(plan);
                return held;
            }
            self.base.trace["forward_npc_end"] =
                json!({"start":plan["start"],"elapsed":elapsed,"valid":valid,"error":err});
        }
        if let Some(mut plan) = self.forward_proposal(d, held, previous) {
            plan["start"] = json!(frame);
            held = int(&plan["held"]);
            self.base.jump_until = frame;
            self.base.trace["held"] = json!(held);
            self.base.trace["forward_npc_start"] = pick(&plan, &["duration", "minimum_clearance"]);
            self.forward_npc_plan = Some(plan);
        }
        held
    }
    pub(super) fn rising_release(
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
        if let Some(plan) = self.rising_release_plan.take() {
            let elapsed = frame - int(&plan["start"]);
            let expected = if elapsed > 0 {
                array(&plan["points"]).get(elapsed as usize - 1)
            } else {
                None
            };
            let err = expected.map(|p| error(me, p));
            let clear_other = !(alive(other) && near(other, me, 128.0, Some(128.0)));
            let clear_fire = !array(&obs["entities"]).iter().any(|e| {
                text(&e["category"]) == "player_fireball"
                    && !own_fire(e, player)
                    && near(e, me, 256.0, Some(128.0))
            });
            let mut clear_npc = true;
            for e in array(&obs["entities"]) {
                if !matches!(text(&e["category"]), "enemy_goomba" | "enemy_koopa") || defeated(e) {
                    continue;
                }
                let (dx, dy) = delta(&e["pos"], &me["pos"]);
                if dx.abs() > 128.0 || dy.abs() > 128.0 {
                    continue;
                }
                let ep = if elapsed > 0 {
                    array(&plan["enemy_tracks"][guid(e)]).get(elapsed as usize - 1)
                } else {
                    None
                };
                if ep.is_none_or(|p| {
                    wrap(num(&e["pos"]["x"]) / 4096.0, num(&p["x"])).abs()
                        > num(&p["radius"]) + 0.25
                        || (-num(&e["pos"]["y"]) / 4096.0 - num(&p["depth"])).abs() > 0.25
                }) || (text(&e["category"]) == "enemy_koopa"
                    && e["koopaShellModeRaw"].as_i64() != Some(0))
                {
                    clear_npc = false;
                    break;
                }
            }
            let valid = err.is_some_and(|e| e <= 0.25)
                && !flag(&me["dead"])
                && raw["currentPowerupRaw"] == plan["form"]
                && raw["damageStateRaw"].as_i64() == Some(0)
                && raw["updateLockedRaw"].as_i64() == Some(0)
                && clear_other
                && clear_fire
                && clear_npc;
            if valid && elapsed < int(&plan["landing_after"]) {
                held = int(&plan["held"]);
                self.base.jump_until = frame;
                self.base.trace["held"] = json!(held);
                self.base.trace["rising_release_plan"] = json!({"start":plan["start"],"elapsed":elapsed,"landing_after":plan["landing_after"],"error":err});
                self.rising_release_plan = Some(plan);
                return held;
            }
            self.base.trace["rising_release_plan_end"] = json!({"start":plan["start"],"elapsed":elapsed,"expected_landing":plan["landing_after"],"valid":valid,"error":err,"clear_other":clear_other,"clear_fire":clear_fire,"clear_npc":clear_npc});
        }
        let delay = d["time"]["inputDelay"].as_i64().unwrap_or(-1);
        let direction = i64::from(held & 16 != 0) - i64::from(held & 32 != 0);
        if obs["stage"]["id"].as_i64() != Some(0)
            || int(&obs["stage"]["vsMode"]) != 1
            || !ordinary(raw, me, &[1, 2], &[2, 128])
            || flag(&me["visual"]["starInvincible"])
            || int(&raw["behaviorFuncRaw"]) != 0x021135b8
            || flag(&me["contact"]["tileGround"])
            || num(&me["vel"]["y"]) / 4096.0 <= 1.0
            || !(0..=6).contains(&delay)
            || held & 3 == 0
            || previous & 3 == 0
            || direction as f64 * num(&me["vel"]["x"]) <= 0.0
        {
            return held;
        }
        if alive(other) && near(other, me, 128.0, Some(128.0)) {
            return held;
        }
        let hb = &me["hitbox"];
        if !flag(&hb["found"]) || int(&hb["fixedPointShift"]) != 12 {
            return held;
        }
        self.base.navigator.update(me);
        let nav = &self.base.navigator;
        let occupied = |x, y| nav.occupied(x, y);
        let horizon = 48;
        let mut hazards = vec![];
        let mut tracks = json!({});
        let mut upper = false;
        for e in array(&obs["entities"]) {
            let cat = text(&e["category"]);
            if !hazard(e) || defeated(e) {
                continue;
            }
            let (dx, dy) = delta(&e["pos"], &me["pos"]);
            if dx.abs() > 256.0 || dy.abs() > 128.0 {
                continue;
            }
            if cat == "player_fireball" {
                if own_fire(e, player) {
                    continue;
                }
                return held;
            }
            if dx.abs() > 128.0 {
                continue;
            }
            let (points, status, ew) = if cat == "enemy_goomba" {
                let (p, s) = actors::short_goomba(e, &occupied, horizon);
                upper |=
                    dy > 20.0 && dy < 72.0 && dx.abs() < 64.0 && dx * num(&me["vel"]["x"]) > 0.0;
                (p.iter().map(|v| json!(v)).collect::<Vec<_>>(), s, 7.0)
            } else if e["koopaShellModeRaw"].as_i64() == Some(0)
                && int(&e["koopaBehaviorFunctionRaw"]) == 0x020dfc58
            {
                let (p, s) = koopa_bounds(e, &occupied, horizon);
                (p, s, 8.0)
            } else {
                return held;
            };
            if points.len() != horizon
                || !matches!(status, "horizon" | "bounded_turn")
                || e["actorGuid"].is_null()
            {
                return held;
            }
            tracks[guid(e)] = json!(points);
            hazards.push((points, ew));
        }
        if !upper {
            return held;
        }
        let initial = motion(me, raw, false);
        let options = ForecastOptions {
            height: 32.0,
            ..Default::default()
        };
        let (nominal, _) = forecast_upper(
            initial,
            previous,
            &planners::sequence(previous, delay as usize, held, horizon),
            &occupied,
            options,
        );
        let waypoint = &self.base.trace["waypoint"];
        if !waypoint.is_null() && nominal.len() >= 6 {
            let remaining = direction as f64 * wrap(num(&waypoint["x"]), initial.x);
            if remaining <= 0.0
                || nominal[..6]
                    .iter()
                    .any(|p| direction as f64 * wrap(p.state.x, initial.x) >= remaining)
            {
                return held;
            }
        }
        if nominal.len() < 24 || clearance(&nominal[..24], &hazards, hb, 0.0, 0.0) >= 0.0 {
            return held;
        }
        let (release, end) = forecast_upper(
            initial,
            previous,
            &planners::sequence(previous, delay as usize, held & !3, horizon),
            &occupied,
            options,
        );
        if end != "horizon"
            || release.len() != horizon
            || !release.last().unwrap().state.grounded
            || release
                .iter()
                .any(|p| p.state.depth > 288.0 || p.contact == Some("ceiling"))
            || clearance(&release, &hazards, hb, 0.25, 1.0) < 0.0
        {
            return held;
        }
        let Some(landing) = release
            .iter()
            .position(|p| p.contact == Some("floor"))
            .map(|i| i + 1)
        else {
            return held;
        };
        self.rising_release_plan = Some(
            json!({"start":frame,"held":held&!3,"points":release,"landing_after":landing,"form":raw["currentPowerupRaw"],"enemy_tracks":tracks}),
        );
        held &= !3;
        self.base.jump_until = frame;
        self.base.trace["held"] = json!(held);
        self.base.trace["rising_npc_release"] = json!({"frame":frame,"nominal_clearance":clearance(&nominal[..24],&hazards,hb,0.0,0.0),"released_clearance":clearance(&release,&hazards,hb,0.0,0.0),"landing_after":landing});
        held
    }
}
