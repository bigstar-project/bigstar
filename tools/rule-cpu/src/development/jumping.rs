use super::*;
use crate::physics::{
    forecast_upper, horizontal_run_step, horizontal_walk_step, vertical_step, ForecastOptions,
};
impl DevelopmentRule {
    pub(super) fn skid_jump(&mut self, d: &Value, frame: i64, previous: i64, mut held: i64) -> i64 {
        let player = self.base.player;
        let obs = &d["observation"];
        let me = &obs["players"][player];
        let raw = &d["runtimePlayers"][player];
        let other = &obs["players"][player ^ 1];
        let s = motion(me, raw, true);
        let delay = d["time"]["inputDelay"].as_i64().unwrap_or(-1);
        let power = int(&raw["currentPowerupRaw"]);
        let mut allowed = !flag(&me["dead"])
            && (matches!(power, 0 | 1) || (power == 2 && held & 2048 != 0 && previous & 2048 != 0))
            && raw["damageStateRaw"].as_i64() == Some(0)
            && raw["updateLockedRaw"].as_i64() == Some(0)
            && matches!(int(&raw["actionFlagRaw"]), 0 | 0x100000)
            && (0..=6).contains(&delay)
            && me["contact"].get("tileGround").is_some();
        if alive(other) && near(other, me, 112.0, Some(96.0)) {
            allowed = false;
        }
        for e in array(&obs["entities"]) {
            if !hazard(e)
                || (flag(&e["entityUpdateStateFound"]) && int(&e["entityUpdateStateRaw"]) == 2)
            {
                continue;
            }
            if near(e, me, 112.0, Some(96.0)) {
                allowed = false;
            }
        }
        let mut active = self.skid_jump_plan.take();
        if let Some(plan) = &active {
            let elapsed = frame - int(&plan["start"]);
            if !allowed
                || elapsed < 0
                || elapsed as usize >= array(&plan["points"]).len()
                || (elapsed > delay + 2 && flag(&me["contact"]["tileGround"]))
                || (elapsed > 0 && differs(me, &plan["points"][elapsed as usize - 1], 2.0))
            {
                active = None;
            }
        }
        let direction = i64::from(held & 16 != 0) - i64::from(held & 32 != 0);
        let previous_direction = i64::from(previous & 16 != 0) - i64::from(previous & 32 != 0);
        if active.is_none()
            && allowed
            && int(&raw["behaviorFuncRaw"]) == 0x02114978
            && raw["behaviorStepRaw"].as_i64() == Some(2)
            && matches!(int(&raw["physicsFlagRaw"]), 0 | 130)
            && flag(&me["contact"]["tileGround"])
            && (s.depth - 272.0).abs() < 0.01
            && (held | previous) & 3 == 0
            && direction == previous_direction
            && direction as f64 * s.vx < -0.5
        {
            self.base.navigator.update(me);
            let nav = &self.base.navigator;
            let mut px = s.x;
            let mut pv = s.vx;
            let mut edge = None;
            let mut queued = vec![];
            for i in 0..32 {
                pv = (pv.abs() - 0.09375).max(0.0).copysign(pv);
                px += pv;
                if i < delay {
                    queued.push(json!({"x":px,"depth":s.depth}));
                }
                if ![-5.0, 0.0, 4.0]
                    .iter()
                    .any(|f| nav.occupied(px + f, s.depth + 1.0))
                {
                    edge = Some(i + 1);
                    break;
                }
                if pv == 0.0 {
                    break;
                }
            }
            if edge.is_some_and(|e| delay + 2 < e && e <= delay + 12)
                && queued.len() == delay as usize
            {
                px = queued.last().map_or(s.x, |p| num(&p["x"]));
                pv = (s.vx.abs() - delay as f64 * 0.09375)
                    .max(0.0)
                    .copysign(s.vx);
                let horizontal = buttons(direction) | 2048;
                let mut options = vec![];
                for hold in [12, 18, 24, 30] {
                    let mut commands = vec![horizontal | 2; hold];
                    commands.resize(72, horizontal);
                    let initial = Motion {
                        x: px,
                        vx: pv,
                        vy: -2.0,
                        ..s
                    };
                    let (points, _) = crate::physics::forecast_player(
                        initial,
                        previous,
                        &commands,
                        &|x, y| nav.occupied(x, y),
                        ForecastOptions {
                            height: if power == 0 { 16.0 } else { 32.0 },
                            ..Default::default()
                        },
                    );
                    for (i, p) in points.iter().enumerate() {
                        if p.contact == Some("floor") && p.state.depth <= 272.0 {
                            let stable = &points[i..points.len().min(i + 7)];
                            if stable.len() == 7
                                && stable
                                    .iter()
                                    .all(|q| q.state.grounded && q.state.depth == p.state.depth)
                            {
                                let mut full = queued.clone();
                                full.extend(array(&json!(points[..i + 7])).iter().cloned());
                                if !crate::opening::opponent_can_reach(
                                    other,
                                    &d["runtimePlayers"][player ^ 1],
                                    &full,
                                ) {
                                    options.push(json!({"start":frame,"hold":hold,"horizontal":horizontal,"edge_after":edge,"landing_after":delay+i as i64+1,"points":full}));
                                }
                            }
                            break;
                        }
                    }
                }
                active = options
                    .into_iter()
                    .min_by_key(|q| (int(&q["landing_after"]), int(&q["hold"])));
            }
        }
        if let Some(plan) = &active {
            let elapsed = frame - int(&plan["start"]);
            held = int(&plan["horizontal"]) | if elapsed < int(&plan["hold"]) { 2 } else { 0 };
            self.base.jump_until = frame;
            self.base.trace["held"] = json!(held);
            let mut info = plan.clone();
            info.as_object_mut().unwrap().remove("points");
            self.base.trace["skid_jump"] = info;
        }
        self.skid_jump_plan = active;
        held
    }
    pub(super) fn predictive_rejump(
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
        let delay = d["time"]["inputDelay"].as_i64().unwrap_or(-1);
        let direction = i64::from(held & 16 != 0) - i64::from(held & 32 != 0);
        let prev_direction = i64::from(previous & 16 != 0) - i64::from(previous & 32 != 0);
        if !(obs["stage"]["id"].as_i64() == Some(0)
            && obs["stage"]["vsMode"].as_i64() == Some(1)
            && ordinary(raw, me, &[1], &[0, 130])
            && flag(&me["contact"]["tileGround"])
            && (0..=6).contains(&delay)
            && frame < self.base.next_jump
            && (held | previous) & 3 == 0
            && direction != 0
            && direction == prev_direction
            && int(&raw["behaviorFuncRaw"]) == 0x02115aac
            && direction as f64 * num(&me["vel"]["x"]) > 0.0)
        {
            return held;
        }
        if alive(other) && near(other, me, 112.0, Some(96.0)) {
            return held;
        }
        let hb = &me["hitbox"];
        if !flag(&hb["found"]) || int(&hb["fixedPointShift"]) != 12 {
            return held;
        }
        self.base.navigator.update(me);
        let nav = &self.base.navigator;
        let mut hazards = vec![];
        for e in array(&obs["entities"]) {
            let (dx, dy) = delta(&e["pos"], &me["pos"]);
            let cat = text(&e["category"]);
            if !hazard(e) || dx.abs() >= 112.0 || dy.abs() >= 96.0 {
                continue;
            }
            if flag(&e["entityUpdateStateFound"]) && int(&e["entityUpdateStateRaw"]) == 2 {
                continue;
            }
            let (points, end, hw) =
                if cat == "enemy_goomba" && int(&e["goombaBehaviorFunctionRaw"]) == 0x020e1538 {
                    let (p, end) = actors::short_goomba(e, &|x, y| nav.occupied(x, y), 24);
                    (json!(p), end, 7.0)
                } else if cat == "enemy_koopa"
                    && int(&e["koopaBehaviorFunctionRaw"]) == 0x020dfc58
                    && e["koopaShellModeRaw"].as_i64() == Some(0)
                {
                    let (p, end) = actors::ground_koopa(e, &|x, y| nav.occupied(x, y), 24);
                    (json!(p), end, 8.0)
                } else {
                    return held;
                };
            if end != "horizon" || array(&points).len() != 24 {
                return held;
            }
            hazards.push((points, hw));
        }
        if hazards.is_empty() {
            return held;
        }
        let cx = num(&hb["centerOffsetX"]) / 4096.0;
        let cy = num(&hb["centerOffsetY"]) / 4096.0;
        let hw = num(&hb["halfWidth"]) / 4096.0;
        let hh = num(&hb["halfHeight"]) / 4096.0;
        let clearance = |points: &[crate::physics::Point], mx: f64, my: f64| {
            points
                .iter()
                .enumerate()
                .flat_map(|(i, p)| {
                    hazards.iter().map(move |(e, ew)| {
                        (wrap(p.state.x + cx, num(&e[i]["x"])).abs() - hw - ew - mx).max(
                            ((p.state.depth - cy) - (num(&e[i]["depth"]) - 8.0)).abs()
                                - hh
                                - 8.0
                                - my,
                        )
                    })
                })
                .fold(f64::INFINITY, f64::min)
        };
        let s = motion(me, raw, true);
        let options = ForecastOptions {
            height: 32.0,
            ..Default::default()
        };
        let (nominal, end) = forecast_upper(
            s,
            previous,
            &planners::sequence(previous, delay as usize, held, 24),
            &|x, y| nav.occupied(x, y),
            options,
        );
        if end != "horizon"
            || nominal.len() != 24
            || clearance(&nominal[..delay as usize + 10], 0.0, 0.0) >= 0.0
        {
            return held;
        }
        let mut inputs = vec![previous; delay as usize];
        inputs.extend(vec![held | 2; 6]);
        inputs.resize(24, held);
        let (jump, end) = forecast_upper(s, previous, &inputs, &|x, y| nav.occupied(x, y), options);
        if end != "horizon"
            || jump.len() != 24
            || !jump[delay as usize].launch
            || jump
                .iter()
                .any(|p| matches!(p.contact, Some("ceiling" | "edge")))
            || clearance(&jump, 0.25, 1.0) < 0.0
        {
            return held;
        }
        held |= 2;
        self.base.trace["held"] = json!(held);
        self.base.trace["predictive_rejump"] = json!({"frame":frame,"next_jump":self.base.next_jump,"nominal_clearance":clearance(&nominal[..delay as usize+10],0.0,0.0),"jump_clearance":clearance(&jump,0.0,0.0)});
        held
    }
    pub(super) fn upper_release(
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
        let eligible = ordinary(raw, me, &[0], &[2, 128, 2050])
            && !flag(&me["visual"]["starInvincible"])
            && me["contact"].get("tileGround").is_some()
            && !flag(&me["contact"]["tileGround"]);
        let mut active = self.upper_npc_release.take();
        if active
            .as_ref()
            .is_some_and(|p| !eligible || frame - int(&p["start"]) >= 6 || frame < int(&p["start"]))
        {
            active = None;
        }
        let delay = d["time"]["inputDelay"].as_i64().unwrap_or(-1);
        let nearby = alive(other) && near(other, me, 96.0, Some(96.0));
        if active.is_none()
            && eligible
            && !nearby
            && held & 3 != 0
            && previous & 3 != 0
            && s.vy > 1.0
            && (0..=6).contains(&delay)
            && matches!(int(&raw["physicsFlagRaw"]), 2 | 128)
        {
            self.base.navigator.update(me);
            let nav = &self.base.navigator;
            let mut paths = vec![];
            let mut known = true;
            let mut upper = false;
            for e in array(&obs["entities"]) {
                let (ex, ey) = delta(&e["pos"], &me["pos"]);
                let category = text(&e["category"]);
                if !hazard(e) || ex.abs() > 96.0 || ey.abs() > 96.0 {
                    continue;
                }
                if category == "enemy_goomba"
                    && int(&e["entityUpdateStateFound"]) == 1
                    && int(&e["entityUpdateStateRaw"]) == 2
                {
                    continue;
                }
                let (points, end) = if category == "enemy_goomba" {
                    let (p, e) = actors::short_goomba(e, &|x, y| nav.occupied(x, y), 24);
                    (json!(p), e)
                } else if category == "enemy_koopa"
                    && e["koopaShellModeRaw"].as_i64() == Some(0)
                    && int(&e["koopaBehaviorFunctionRaw"]) == 0x020dfc58
                {
                    let (p, e) = actors::ground_koopa(e, &|x, y| nav.occupied(x, y), 24);
                    (json!(p), e)
                } else {
                    known = false;
                    break;
                };
                if end != "horizon" || array(&points).len() != 24 {
                    known = false;
                    break;
                }
                paths.push(points);
                upper |= category == "enemy_goomba"
                    && 20.0 < ey
                    && ey < 72.0
                    && ex.abs() < 48.0
                    && ex * num(&me["vel"]["x"]) > 0.0;
            }
            if known && upper {
                let mut x = s.x;
                let mut depth = s.depth;
                let mut vx = s.vx;
                let mut vy = s.vy;
                let mut threat = None;
                for i in 0..24 {
                    let command = if i < delay as usize { previous } else { held };
                    let direction = i64::from(command & 16 != 0) - i64::from(command & 32 != 0);
                    vx = if direction != 0 && command & 2048 != 0 {
                        horizontal_run_step(vx, direction)
                    } else {
                        horizontal_walk_step(vx, direction)
                    };
                    x += vx;
                    vy = vertical_step(vy, true);
                    depth -= vy;
                    if threat.is_none()
                        && paths.iter().any(|path| {
                            wrap(x, num(&path[i]["x"])).abs() < 16.0
                                && num(&path[i]["depth"]) - 20.0 < depth
                                && depth < num(&path[i]["depth"]) + 16.0
                        })
                    {
                        threat = Some(i + 1);
                    }
                }
                let (short, end) = forecast_upper(
                    s,
                    previous,
                    &planners::sequence(previous, delay as usize, held & !3, 24),
                    &|x, y| nav.occupied(x, y),
                    ForecastOptions::default(),
                );
                let mut safe = end == "horizon"
                    && short.len() == 24
                    && short.last().is_some_and(|p| p.state.grounded)
                    && short.iter().all(|p| p.state.depth <= 288.0);
                let mut minimum = 1000.0_f64;
                if safe {
                    for (i, p) in short.iter().enumerate() {
                        for path in &paths {
                            let e = &path[i];
                            let gap = (wrap(p.state.x, num(&e["x"])).abs() - 16.0)
                                .max(p.state.depth - 16.0 - num(&e["depth"]))
                                .max(num(&e["depth"]) - 20.0 - p.state.depth);
                            minimum = minimum.min(gap);
                            if gap < if i < delay as usize { 0.0 } else { 4.0 } {
                                safe = false;
                            }
                        }
                    }
                }
                if safe && threat.is_some_and(|t| t <= 12) {
                    active = Some(
                        json!({"start":frame,"threat_after":threat,"minimum_predicted_clearance":minimum}),
                    );
                }
            }
        }
        if let Some(plan) = &active {
            held &= !3;
            self.base.jump_until = frame;
            self.base.trace["held"] = json!(held);
            self.base.trace["upper_npc_release"] = plan.clone();
        }
        self.upper_npc_release = active;
        held
    }
}
