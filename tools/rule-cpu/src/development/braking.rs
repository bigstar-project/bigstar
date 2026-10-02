use super::*;
use crate::{
    navigation::GrassNavigator,
    physics::{forecast_player, ForecastOptions},
    planners::sequence,
};

fn persistent_plan(
    d: &Value,
    player: usize,
    held: i64,
    previous: i64,
    nav: &GrassNavigator,
) -> Option<Value> {
    let obs = &d["observation"];
    let me = &obs["players"][player];
    let raw = &d["runtimePlayers"][player];
    let other = &obs["players"][player ^ 1];
    let s = motion(me, raw, false);
    if flag(&me["contact"]["tileGround"])
        || !(-4.0..=0.0).contains(&s.vy)
        || !(0.25..=3.0).contains(&s.vx.abs())
        || (alive(other) && near(other, me, 96.0, None))
    {
        return None;
    }
    let mut enemies = vec![];
    for e in array(&obs["entities"]) {
        if !hazard(e) {
            continue;
        }
        let (dx, dy) = delta(&e["pos"], &me["pos"]);
        if dx.abs() > 128.0 || dy.abs() > 96.0 {
            continue;
        }
        if text(&e["category"]) != "enemy_goomba"
            || int(&e["goombaBehaviorFunctionRaw"]) != 0x020e1538
            || num(&e["vel"]["x"]).abs() != 2048.0
            || num(&e["vel"]["y"]) != 0.0
        {
            return None;
        }
        let (points, end) = actors::ground_goomba(
            num(&e["pos"]["x"]) / 4096.0,
            -num(&e["pos"]["y"]) / 4096.0,
            num(&e["vel"]["x"]) / 4096.0,
            &|x, y| nav.occupied(x, y),
            30,
            0,
        );
        if end != "horizon" || points.len() != 30 {
            return None;
        }
        enemies.push(json!({"guid":e["actorGuid"],"dx":dx,"dy":dy,"points":points}));
    }
    let enemy = enemies
        .iter()
        .filter(|e| {
            12.0 < num(&e["dx"]).abs()
                && num(&e["dx"]).abs() < 64.0
                && (-48.0..=-4.0).contains(&num(&e["dy"]))
                && num(&e["dx"]) * s.vx > 0.0
        })
        .min_by(|a, b| num(&a["dx"]).abs().total_cmp(&num(&b["dx"]).abs()))?;
    let braking = if num(&enemy["dx"]) > 0.0 {
        32 | 2048
    } else {
        16 | 2048
    };
    let delay = d["time"]["inputDelay"].as_i64().unwrap_or(2);
    if !(0..=6).contains(&delay) {
        return None;
    }
    let near = |points: &[crate::physics::Point]| {
        points.iter().enumerate().any(|(i, p)| {
            enemies.iter().any(|e| {
                let q = &e["points"][i];
                wrap(p.state.x, num(&q["x"])).abs() < 16.0
                    && num(&q["depth"]) - 20.0 <= p.state.depth
                    && p.state.depth <= num(&q["depth"]) + 16.0
            })
        })
    };
    let mut nominal = false;
    let mut alternatives = vec![];
    for counter_landing_turn in [false, true] {
        let opts = ForecastOptions {
            height: 16.0,
            counter_landing_turn,
        };
        let (a, end) = forecast_player(
            s,
            previous,
            &sequence(previous, delay as usize, held, 30),
            &|x, y| nav.occupied(x, y),
            opts,
        );
        nominal |= end == "horizon" && a.len() == 30 && near(&a);
        let (b, end) = forecast_player(
            s,
            previous,
            &sequence(previous, delay as usize, braking, 30),
            &|x, y| nav.occupied(x, y),
            opts,
        );
        if end != "horizon"
            || b.len() != 30
            || near(&b)
            || !b.last()?.state.grounded
            || b.iter().any(|p| p.state.depth > 288.0)
        {
            return None;
        }
        alternatives.push(b);
    }
    nominal.then(|| json!({"held":braking,"paths":alternatives,"enemies":enemies}))
}
impl DevelopmentRule {
    pub(super) fn observed_ceiling(
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
        let other_raw = &d["runtimePlayers"][player ^ 1];
        let entities = array(&obs["entities"]);
        let mut active = self.observed_ceiling_koopa_plan.take();
        let mut eligible = ordinary(raw, me, &[0], &[0, 2, 128, 130, 2050]) && held & 1024 == 0;
        if !eligible || (active.is_none() && !flag(&me["contact"]["ceiling"])) {
            return held;
        }
        self.observed_ceiling_navigator.update(me);
        let nav = &self.observed_ceiling_navigator;
        let mut enemies = vec![];
        let mut tracks = vec![];
        for e in entities {
            let (dx, dy) = delta(&e["pos"], &me["pos"]);
            if !hazard(e) || dx.abs() > 128.0 || dy.abs() > 96.0 {
                continue;
            }
            if text(&e["category"]) == "enemy_goomba"
                && int(&e["entityUpdateStateFound"]) == 1
                && int(&e["entityUpdateStateRaw"]) == 2
            {
                continue;
            }
            if text(&e["category"]) != "enemy_koopa"
                || e["koopaShellModeRaw"].as_i64() != Some(0)
                || e["entityUpdateStateFound"].as_i64() != Some(1)
                || e["entityUpdateStateRaw"].as_i64() != Some(0)
            {
                eligible = false;
                break;
            }
            let (points, end) = actors::ground_koopa(e, &|x, y| nav.occupied(x, y), 36);
            if end != "horizon" {
                eligible = false;
                break;
            }
            enemies.push(json!({"guid":e["actorGuid"],"points":points}));
            tracks.push(points);
        }
        let opponent_clear = |paths: &Value, elapsed: usize| {
            if !alive(other) {
                return true;
            }
            if !matches!(other_raw["currentPowerupRaw"].as_i64(), Some(0..=2))
                || int(&other_raw["physicsFlagRaw"]) & 32 != 0
            {
                return false;
            }
            let ox = num(&other["pos"]["x"]) / 4096.0;
            let ovx = num(&other["vel"]["x"]) / 4096.0;
            if ovx.abs() > 3.0 {
                return false;
            }
            array(paths).iter().all(|path| {
                array(path)
                    .iter()
                    .skip(elapsed)
                    .enumerate()
                    .all(|(i, p)| wrap(num(&p["x"]), ox + ovx * (i + 1) as f64).abs() >= 32.0)
            })
        };
        if let Some(plan) = &active {
            let elapsed = frame - int(&plan["start"]);
            let mut valid = eligible && 0 < elapsed && elapsed < int(&plan["duration"]);
            if valid {
                valid = array(&plan["paths"])
                    .iter()
                    .any(|path| !differs(me, &path[elapsed as usize - 1], 2.0));
                let old = array(&plan["enemies"]);
                if !enemies
                    .iter()
                    .all(|e| old.iter().any(|h| e["guid"] == h["guid"]))
                    || !old
                        .iter()
                        .all(|e| enemies.iter().any(|h| e["guid"] == h["guid"]))
                {
                    valid = false;
                }
                for e in old {
                    if entities
                        .iter()
                        .rev()
                        .find(|a| {
                            text(&a["category"]) == "enemy_koopa" && a["actorGuid"] == e["guid"]
                        })
                        .is_none_or(|a| differs(a, &e["points"][elapsed as usize - 1], 2.0))
                    {
                        valid = false;
                    }
                }
                valid = valid && opponent_clear(&plan["paths"], elapsed as usize);
            }
            if !valid {
                active = None;
            }
        }
        if active.is_none()
            && eligible
            && !enemies.is_empty()
            && flag(&me["contact"]["ceiling"])
            && !flag(&me["contact"]["tileGround"])
        {
            if let Some(plan) = planners::observed_ceiling(
                PlannerState {
                    motion: motion(me, raw, false),
                    height: 16.0,
                },
                held,
                previous,
                d["time"]["inputDelay"].as_i64().unwrap_or(2),
                &|x, y| nav.occupied(x, y),
                &tracks,
                36,
            ) {
                let paths = json!([plan["points"], plan["conservative_points"]]);
                if opponent_clear(&paths, 0) {
                    active = Some(
                        json!({"start":frame,"held":plan["held"],"duration":plan["duration"],"paths":paths,"enemies":enemies}),
                    );
                }
            }
        }
        if let Some(plan) = &active {
            held = int(&plan["held"]);
            self.base.trace["held"] = json!(held);
            self.base.trace["observed_ceiling_koopa_plan"] =
                pick(plan, &["start", "held", "duration"]);
        }
        self.observed_ceiling_koopa_plan = active;
        held
    }
    pub(super) fn persistent_brake(
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
        let entities = array(&obs["entities"]);
        if flag(&me["contact"]["ceiling"]) {
            self.last_brake_ceiling = frame;
        }
        let eligible = ordinary(raw, me, &[0], &[0, 2, 128, 130, 2050]);
        let mut active = self.ceiling_npc_brake.take();
        if let Some(plan) = &active {
            let elapsed = frame - int(&plan["start"]);
            let mut valid = eligible && 0 < elapsed && elapsed < 30;
            if valid {
                valid = array(&plan["paths"])
                    .iter()
                    .any(|p| !differs(me, &p[elapsed as usize - 1], 4.0));
                for enemy in array(&plan["enemies"]) {
                    let matched: Vec<_> = entities
                        .iter()
                        .filter(|e| {
                            e["actorGuid"] == enemy["guid"]
                                && text(&e["category"]) == "enemy_goomba"
                        })
                        .collect();
                    if matched.len() != 1
                        || int(&matched[0]["goombaBehaviorFunctionRaw"]) != 0x020e1538
                        || differs(matched[0], &enemy["points"][elapsed as usize - 1], 2.0)
                    {
                        valid = false;
                        break;
                    }
                }
                if entities.iter().any(|e| {
                    hazard(e)
                        && !array(&plan["enemies"])
                            .iter()
                            .any(|p| p["guid"] == e["actorGuid"])
                        && near(e, me, 96.0, Some(64.0))
                }) {
                    valid = false;
                }
            }
            if !valid {
                active = None;
            }
        }
        if active.is_none() && eligible && (0..=18).contains(&(frame - self.last_brake_ceiling)) {
            if let Some(mut plan) = persistent_plan(d, player, held, previous, &self.base.navigator)
            {
                plan["start"] = json!(frame);
                active = Some(plan);
            }
        }
        if let Some(plan) = &active {
            held = int(&plan["held"]);
            self.base.trace["held"] = json!(held);
            self.base.trace["ceiling_npc_brake"] = json!({"start":plan["start"],"held":held});
        }
        self.ceiling_npc_brake = active;
        held
    }
    pub(super) fn ground_cooldown(
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
        let s = motion(me, raw, true);
        let mut eligible =
            ordinary(raw, me, &[0], &[0, 2, 128, 130, 2050]) && flag(&me["contact"]["tileGround"]);
        if alive(other) && near(other, me, 96.0, None) {
            eligible = false;
        }
        let nav = &self.base.navigator;
        let mut hazards = vec![];
        for e in entities {
            let (dx, dy) = delta(&e["pos"], &me["pos"]);
            if !hazard(e) || dx.abs() > 128.0 || dy.abs() > 96.0 {
                continue;
            }
            if text(&e["category"]) == "enemy_goomba"
                && int(&e["entityUpdateStateFound"]) == 1
                && int(&e["entityUpdateStateRaw"]) == 2
            {
                continue;
            }
            if text(&e["category"]) != "enemy_koopa" {
                eligible = false;
                break;
            }
            let (points, end) = actors::ground_koopa(e, &|x, y| nav.occupied(x, y), 24);
            if end != "horizon" {
                eligible = false;
                break;
            }
            hazards.push(json!({"guid":e["actorGuid"],"points":points}));
        }
        let mut active = self.ground_cooldown_plan.take();
        if let Some(plan) = &active {
            let elapsed = frame - int(&plan["start"]);
            let mut valid = eligible && 0 < elapsed && elapsed < 24;
            if valid {
                valid = !differs(me, &plan["points"][elapsed as usize - 1], 2.0);
                let old = array(&plan["hazards"]);
                if !hazards
                    .iter()
                    .all(|h| old.iter().any(|p| h["guid"] == p["guid"]))
                    || !old
                        .iter()
                        .all(|h| hazards.iter().any(|p| h["guid"] == p["guid"]))
                {
                    valid = false;
                }
                for h in old {
                    if entities
                        .iter()
                        .find(|e| e["actorGuid"] == h["guid"])
                        .is_none_or(|e| differs(e, &h["points"][elapsed as usize - 1], 2.0))
                    {
                        valid = false;
                    }
                }
            }
            if !valid {
                active = None;
            }
        }
        if active.is_none()
            && eligible
            && !hazards.is_empty()
            && frame < self.base.next_jump
            && held & 3 == 0
            && previous & 3 == 0
        {
            let delay = d["time"]["inputDelay"].as_i64().unwrap_or(2);
            if (0..=6).contains(&delay) {
                let predict = |button| {
                    forecast_player(
                        s,
                        previous,
                        &sequence(previous, delay as usize, button, 24),
                        &|x, y| nav.occupied(x, y),
                        ForecastOptions::default(),
                    )
                };
                let near = |points: &[crate::physics::Point]| {
                    points.iter().enumerate().any(|(i, p)| {
                        hazards.iter().any(|h| {
                            wrap(p.state.x, num(&h["points"][i]["x"])).abs() < 16.0
                                && (p.state.depth - num(&h["points"][i]["depth"])).abs() < 20.0
                        })
                    })
                };
                let (nominal, _) = predict(held);
                if near(&nominal[..nominal.len().min(18)]) {
                    for button in [2064, 2080] {
                        let (points, end) = predict(button);
                        if end == "horizon"
                            && points.len() == 24
                            && !near(&points)
                            && points.iter().all(|p| {
                                p.state.grounded && nav.occupied(p.state.x, p.state.depth + 1.0)
                            })
                        {
                            active = Some(
                                json!({"start":frame,"held":button,"points":points,"hazards":hazards}),
                            );
                            break;
                        }
                    }
                }
            }
        }
        if let Some(plan) = &active {
            held = int(&plan["held"]);
            self.base.trace["held"] = json!(held);
            self.base.trace["ground_cooldown_brake"] = json!({"start":plan["start"],"held":held});
        }
        self.ground_cooldown_plan = active;
        held
    }
}
