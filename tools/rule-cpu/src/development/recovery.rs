use super::encounters::{defeated, own_fire};
use super::*;
use crate::physics::{forecast_player, forecast_upper, ForecastOptions};
fn dir(held: i64) -> i64 {
    i64::from(held & 16 != 0) - i64::from(held & 32 != 0)
}
fn ordinary_other(raw: &Value) -> bool {
    raw["currentPowerupRaw"]
        .as_i64()
        .is_some_and(|v| (0..=2).contains(&v))
        && raw["physicsFlagRaw"].as_i64().is_some_and(|v| v & 32 == 0)
}
fn undamaged(raw: &Value) -> bool {
    ["damageStateRaw", "damageCooldownRaw", "updateLockedRaw"]
        .iter()
        .all(|k| raw[k].as_i64() == Some(0))
}
fn normal_behavior(raw: &Value) -> bool {
    matches!(
        int(&raw["behaviorFuncRaw"]),
        0x02115aac | 0x021135b8 | 0x02110dc0
    )
}
fn without_points(plan: &Value) -> Value {
    let mut v = plan.clone();
    v.as_object_mut().unwrap().remove("points");
    v
}
fn shot_clear(
    d: &Value,
    player: usize,
    points: &[Value],
    nav: &crate::navigation::GrassNavigator,
) -> bool {
    let obs = &d["observation"];
    let hb = &obs["players"][player]["hitbox"];
    if !flag(&hb["found"]) || int(&hb["fixedPointShift"]) != 12 {
        return false;
    }
    let mut filtered = d.clone();
    filtered["observation"]["entities"] = json!(array(&obs["entities"])
        .iter()
        .filter(|e| !(text(&e["category"]) == "enemy_goomba"
            && int(&e["goombaBehaviorFunctionRaw"]) == 0x020e13d0))
        .collect::<Vec<_>>());
    crate::opening::counterjump_clear(&filtered, player, points, nav, false)
        && !crate::opening::opponent_can_reach(
            &obs["players"][player ^ 1],
            &d["runtimePlayers"][player ^ 1],
            points,
        )
}
impl DevelopmentRule {
    pub(super) fn shot_recovery(
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
        self.shot_recovery_nav.update(me);
        let mut plan = self.shot_recovery_plan.take();
        if let Some(p) = &plan {
            let elapsed = frame - int(&p["start"]);
            let points = array(&p["points"]);
            let valid = elapsed > 0
                && elapsed <= int(&p["landing_after"]) + 6
                && elapsed as usize <= points.len()
                && !flag(&me["dead"])
                && me["contact"].get("tileGround").is_some_and(|v| !flag(v))
                && raw["currentPowerupRaw"] == p["form"]
                && raw["damageStateRaw"].as_i64() == Some(0)
                && raw["updateLockedRaw"].as_i64() == Some(0)
                && !differs(me, &points[elapsed as usize - 1], 0.5);
            if !valid
                || !shot_clear(
                    d,
                    player,
                    &points[(elapsed.max(0) as usize).min(points.len())
                        ..(int(&p["landing_after"]) as usize + 6).min(points.len())],
                    &self.shot_recovery_nav,
                )
            {
                plan = None;
            }
        }
        let flat_near = array(&obs["entities"]).iter().any(|e| {
            text(&e["category"]) == "enemy_goomba"
                && int(&e["goombaBehaviorFunctionRaw"]) == 0x020e13d0
                && near(e, me, 32.0, Some(48.0))
        });
        if plan.is_none()
            && flag(&me["found"])
            && flag(&raw["found"])
            && int(&obs["stage"]["group"]) == 9
            && obs["stage"]["id"].as_i64() == Some(0)
            && int(&raw["currentPowerupRaw"]) == 2
            && int(&raw["physicsFlagRaw"]) == 128
            && int(&raw["behaviorFuncRaw"]) == 0x02110dc0
            && int(&raw["behaviorStepRaw"]) == 1
            && text(&self.base.trace["target"]) == "carrier"
            && num(&self.base.trace["dx"]).abs() >= 128.0
            && (0..=18).contains(&(frame - self.last_pit_recovery))
            && flat_near
            && held & 2048 != 0
            && previous & 2048 != 0
        {
            if let Some(mut p) = self.recovery_proposal(d, held, previous, true) {
                let landing = int(&p["landing_after"]) as usize;
                let points = array(&p["points"]);
                if points.len() >= landing + 6
                    && points[landing - 1..landing + 6]
                        .iter()
                        .all(|p| flag(&p["grounded"]) && num(&p["depth"]) <= 272.0)
                    && shot_clear(d, player, &points[..landing + 6], &self.shot_recovery_nav)
                {
                    p["start"] = json!(frame);
                    p["form"] = json!(2);
                    plan = Some(p);
                }
            }
        }
        if let Some(p) = &plan {
            held = int(&p["held"]);
            self.base.trace["held"] = json!(held);
            self.base.trace["shot_recovery"] = without_points(p);
        }
        self.shot_recovery_plan = plan;
        held
    }
    fn descent_proposal(&mut self, d: &Value, held: i64, previous: i64) -> Option<Value> {
        let player = self.base.player;
        let obs = &d["observation"];
        let me = &obs["players"][player];
        let raw = &d["runtimePlayers"][player];
        if [
            "currentPowerupRaw",
            "behaviorFuncRaw",
            "damageStateRaw",
            "damageCooldownRaw",
            "updateLockedRaw",
            "physicsFlagRaw",
            "actionFlagRaw",
            "facing",
        ]
        .iter()
        .any(|k| raw.get(k).is_none())
        {
            return None;
        }
        let direction = dir(held);
        let delay = d["time"]["inputDelay"].as_i64().unwrap_or(-1);
        if !ordinary(raw, me, &[2], &[0, 2, 128, 130, 2050])
            || int(&raw["behaviorFuncRaw"]) != 0x021135b8
            || flag(&raw["damageCooldownRaw"])
            || !(0..=6).contains(&delay)
            || me["contact"].get("tileGround").is_none_or(flag)
            || num(&me["vel"]["y"]) >= 0.0
            || !(1.0..=3.0).contains(&(num(&me["vel"]["x"]).abs() / 4096.0))
            || direction as f64 * num(&me["vel"]["x"]) <= 0.0
            || direction != dir(previous)
            || int(&raw["facing"]) != direction
            || (held | previous) & 3 != 0
            || held & 2048 == 0
            || previous & 2048 == 0
        {
            return None;
        }
        let other = &obs["players"][player ^ 1];
        if alive(other)
            && (!ordinary_other(&d["runtimePlayers"][player ^ 1]) || near(other, me, 128.0, None))
        {
            return None;
        }
        let mut own_balls = 0;
        let mut enemies = vec![];
        for e in array(&obs["entities"]) {
            let cat = text(&e["category"]);
            let (ex, ey) = delta(&e["pos"], &me["pos"]);
            if cat == "player_fireball" {
                if own_fire(e, player) {
                    own_balls += 1
                } else if ex.abs() < 128.0 && ey.abs() < 96.0 {
                    return None;
                }
            }
            if !matches!(cat, "enemy_koopa" | "enemy_goomba")
                || defeated(e)
                || ex.abs() >= 128.0
                || ey.abs() >= 96.0
            {
                continue;
            }
            if cat != "enemy_koopa"
                || int(&e["koopaBehaviorFunctionRaw"]) != 0x020dfc58
                || e["koopaShellModeRaw"].as_i64() != Some(0)
                || num(&e["vel"]["x"]).abs() != 2048.0
                || num(&e["vel"]["y"]) != 0.0
                || !(24.0 < direction as f64 * ex && direction as f64 * ex < 96.0)
                || !(ey > -80.0 && ey < -16.0)
            {
                return None;
            }
            enemies.push(e);
        }
        if own_balls >= 2 || enemies.len() != 1 {
            return None;
        }
        let enemy = enemies[0];
        self.base.navigator.update(me);
        let occupied = |x, y| self.base.navigator.occupied(x, y);
        let horizon = 30;
        let (ep, status) = actors::ground_koopa(enemy, &occupied, horizon);
        if status != "horizon" || ep.len() != horizon {
            return None;
        }
        let hb = &me["hitbox"];
        if !flag(&hb["found"]) || int(&hb["fixedPointShift"]) != 12 {
            return None;
        }
        let initial = motion(me, raw, false);
        let mut contact = vec![];
        for counter in [false, true] {
            let options = ForecastOptions {
                height: 32.0,
                counter_landing_turn: counter,
            };
            let (nominal, end) = forecast_upper(
                initial,
                previous,
                &planners::sequence(previous, delay as usize, held, horizon),
                &occupied,
                options,
            );
            if end != "horizon"
                || nominal.len() != horizon
                || nominal[..delay as usize + 12]
                    .iter()
                    .any(|p| p.state.grounded)
            {
                return None;
            }
            for (i, (p, q)) in nominal.iter().zip(&ep).enumerate() {
                let separation = (wrap(p.state.x + num(&hb["centerOffsetX"]) / 4096.0, q.x).abs()
                    - num(&hb["halfWidth"]) / 4096.0
                    - 8.0)
                    .max(
                        (p.state.depth - num(&hb["centerOffsetY"]) / 4096.0 - (q.depth - 12.0))
                            .abs()
                            - num(&hb["halfHeight"]) / 4096.0
                            - 12.0,
                    );
                if separation < 4.0 {
                    contact.push(i + 1);
                    break;
                }
            }
            let mut commands = vec![previous; delay as usize];
            commands.extend([held & !2048; 6]);
            commands.resize(horizon, held);
            let (released, end) = forecast_upper(initial, previous, &commands, &occupied, options);
            if end != "horizon"
                || released.len() != horizon
                || released.iter().any(|p| p.state.depth > 288.0)
            {
                return None;
            }
        }
        let contact = contact.into_iter().min()?;
        if contact < delay as usize + 12 || contact > 30 {
            return None;
        }
        Some(
            json!({"guid":enemy["actorGuid"],"direction":direction,"contact_after":contact,"release":6,"duration":12}),
        )
    }
    pub(super) fn descent_shot(
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
        let mut plan = self.descent_koopa_shot.take();
        if let Some(p) = &plan {
            let enemy = array(&obs["entities"])
                .iter()
                .find(|e| e["actorGuid"] == p["guid"]);
            let other = &obs["players"][player ^ 1];
            let valid = !flag(&me["dead"])
                && raw["currentPowerupRaw"].as_i64() == Some(2)
                && undamaged(raw)
                && matches!(int(&raw["physicsFlagRaw"]), 0 | 2 | 128 | 130 | 2050)
                && dir(held) == int(&p["direction"])
                && held & 3 == 0
                && normal_behavior(raw)
                && enemy.is_some_and(|e| {
                    e["entityUpdateStateRaw"].as_i64() == Some(0)
                        && e["koopaShellModeRaw"].as_i64() == Some(0)
                })
                && (!alive(other)
                    || (ordinary_other(&d["runtimePlayers"][player ^ 1])
                        && !near(other, me, 96.0, None)))
                && !array(&obs["entities"]).iter().any(|e| {
                    text(&e["category"]) == "player_fireball"
                        && !own_fire(e, player)
                        && near(e, me, 96.0, Some(96.0))
                });
            if frame - int(&p["start"]) >= int(&p["duration"]) || !valid {
                self.base.trace["descent_koopa_shot_end"] =
                    json!({"start":p["start"],"elapsed":frame-int(&p["start"]),"valid":valid});
                plan = None;
            }
        }
        if plan.is_none()
            && frame >= self.descent_koopa_retry
            && text(&self.base.trace["target"]) == "carrier"
        {
            plan = self.descent_proposal(d, held, previous).map(|mut p| {
                p["start"] = json!(frame);
                self.descent_koopa_retry = frame + 60;
                p
            });
        }
        if let Some(p) = &plan {
            held = if frame - int(&p["start"]) < int(&p["release"]) {
                held & !2048
            } else {
                held | 2048
            };
            self.base.trace["held"] = json!(held);
            self.base.trace["descent_koopa_shot"] = p.clone();
        }
        self.descent_koopa_shot = plan;
        held
    }
    fn overhead_proposal(&mut self, d: &Value, held: i64, previous: i64) -> Option<Value> {
        let player = self.base.player;
        let me = &d["observation"]["players"][player];
        let raw = &d["runtimePlayers"][player];
        let other = &d["observation"]["players"][player ^ 1];
        let ort = &d["runtimePlayers"][player ^ 1];
        let trace = &self.base.trace;
        let direction = dir(held);
        if !flag(&trace["overhead_evade"])
            || !flag(&trace["grounded"])
            || direction != int(&trace["body_avoid"])
            || direction == 0
            || held & !(48 | 2048) != 0
            || held & 2048 == 0
            || previous & 3 != 0
            || !ordinary(raw, me, &[0, 1, 2], &[0, 2, 128, 130, 2050])
            || !flag(&me["contact"]["tileGround"])
            || !normal_behavior(raw)
            || !undamaged(raw)
            || flag(&me["visual"]["starInvincible"])
            || !ordinary_other(ort)
            || flag(&other["visual"]["starInvincible"])
        {
            return None;
        }
        let delay = d["time"]["inputDelay"].as_i64().unwrap_or(-1);
        let initial = motion(me, raw, true);
        if !(0..=6).contains(&delay)
            || !(0.0..=272.0).contains(&initial.depth)
            || initial.vx.abs() > 3.0
        {
            return None;
        }
        self.overhead_pit_nav.update(me);
        let nav = &self.overhead_pit_nav;
        let occupied = |x, y| nav.occupied(x, y);
        if [8, 16, 24, 32, 40, 48]
            .iter()
            .all(|n| occupied(initial.x + (direction * n) as f64, initial.depth + 8.0))
        {
            return None;
        }
        let options = ForecastOptions {
            height: if int(&raw["currentPowerupRaw"]) == 0 {
                16.0
            } else {
                27.0
            },
            ..Default::default()
        };
        let (nominal, _) = forecast_player(
            initial,
            previous,
            &planners::sequence(previous, delay as usize, held, 60),
            &occupied,
            options,
        );
        let edge = nominal
            .iter()
            .position(|p| p.contact == Some("edge"))
            .map(|i| i + 1)?;
        if edge > 36 {
            return None;
        }
        let button = held | 2;
        let mut commands = planners::sequence(previous, delay as usize, button, 24);
        commands.extend([held; 36]);
        let (points, status) = forecast_player(initial, previous, &commands, &occupied, options);
        if status != "horizon"
            || points.len() != 60
            || !points.last()?.state.grounded
            || points.iter().any(|p| {
                p.state.depth > initial.depth + 1.0
                    || matches!(p.contact, Some("ceiling" | "ground_wall"))
            })
            || !crate::opening::counterjump_clear(
                d,
                player,
                &points[..24].iter().map(|p| json!(p)).collect::<Vec<_>>(),
                nav,
                false,
            )
        {
            return None;
        }
        let landing = points
            .iter()
            .enumerate()
            .find(|(i, p)| *i > delay as usize && p.contact == Some("floor"))
            .map(|(i, _)| i + 1)?;
        Some(
            json!({"held":button,"points":points,"form":raw["currentPowerupRaw"],"duration":24,"nominal_edge_after":edge,"predicted_landing_after":landing}),
        )
    }
    pub(super) fn overhead_pit(
        &mut self,
        d: &Value,
        frame: i64,
        previous: i64,
        mut held: i64,
    ) -> i64 {
        let player = self.base.player;
        let me = &d["observation"]["players"][player];
        let raw = &d["runtimePlayers"][player];
        let mut plan = self.overhead_pit_plan.take();
        if let Some(p) = &plan {
            let elapsed = frame - int(&p["start"]);
            let point = if elapsed > 0 && elapsed < int(&p["duration"]) {
                array(&p["points"]).get(elapsed as usize - 1)
            } else {
                None
            };
            let error = point.map(|p| pos_error(me, p));
            let mut valid = error.is_some_and(|e| e <= 0.5)
                && !flag(&me["dead"])
                && raw["currentPowerupRaw"] == p["form"]
                && undamaged(raw)
                && normal_behavior(raw)
                && matches!(int(&raw["actionFlagRaw"]), 0 | 0x100000);
            if valid {
                self.overhead_pit_nav.update(me);
                let points = array(&p["points"]);
                valid = crate::opening::counterjump_clear(
                    d,
                    player,
                    &points[elapsed as usize..(elapsed as usize + 6).min(points.len())],
                    &self.overhead_pit_nav,
                    false,
                );
            }
            if !valid {
                self.base.trace["overhead_pit_end"] =
                    json!({"start":p["start"],"elapsed":elapsed,"error":error});
                plan = None;
            }
        }
        if plan.is_none() && frame >= self.overhead_pit_retry {
            plan = self.overhead_proposal(d, held, previous).map(|mut p| {
                p["start"] = json!(frame);
                self.overhead_pit_retry = frame + 60;
                p
            });
        }
        if let Some(p) = &plan {
            held = int(&p["held"]);
            self.base.trace["held"] = json!(held);
            self.base.trace["overhead_pit_jump"] = without_points(p);
        }
        self.overhead_pit_plan = plan;
        held
    }
    fn recovery_proposal(
        &mut self,
        d: &Value,
        held: i64,
        previous: i64,
        shot: bool,
    ) -> Option<Value> {
        let me = &d["observation"]["players"][self.base.player];
        let raw = &d["runtimePlayers"][self.base.player];
        let initial = motion(me, raw, false);
        let direction = if initial.vx > 0.0 { 1 } else { -1 };
        let delay = d["time"]["inputDelay"].as_i64().unwrap_or(-1);
        if flag(&me["dead"])
            || me["contact"].get("tileGround").is_none_or(flag)
            || !(0.1..=3.0).contains(&initial.vx.abs())
            || !(-4.0..=if shot { 3.5 } else { 3.0 }).contains(&initial.vy)
            || dir(held) != -direction
            || held & (3 | 64 | 128) != 0
            || !matches!(int(&raw["currentPowerupRaw"]), 0..=2)
            || !matches!(int(&raw["physicsFlagRaw"]), 2 | 128)
            || !normal_behavior(raw)
            || !undamaged(raw)
            || !(0..=6).contains(&delay)
        {
            return None;
        }
        let nav = if shot {
            &mut self.shot_recovery_nav
        } else {
            &mut self.recovery_finish_nav
        };
        nav.update(me);
        let occupied = |x, y| nav.occupied(x, y);
        let options = ForecastOptions {
            height: if int(&raw["currentPowerupRaw"]) == 0 {
                16.0
            } else {
                27.0
            },
            ..Default::default()
        };
        let (nominal, status) = forecast_player(
            initial,
            previous,
            &planners::sequence(previous, delay as usize, held, 36),
            &occupied,
            options,
        );
        if !matches!(status, "deep" | "wall")
            || nominal.last()?.state.depth < if shot { 280.0 } else { initial.depth + 64.0 }
            || nominal.last()?.state.vy > -2.0
            || nominal.iter().any(|p| p.contact.is_some())
        {
            return None;
        }
        let forward = buttons(direction) | 2048;
        let (points, _) = forecast_player(
            initial,
            previous,
            &planners::sequence(previous, delay as usize, forward, 36),
            &occupied,
            options,
        );
        let landing = points
            .iter()
            .position(|p| p.contact == Some("floor"))
            .map(|i| i + 1)?;
        if landing > if shot { 30 } else { 24 }
            || points[..landing]
                .iter()
                .any(|p| !matches!(p.contact, None | Some("floor")))
        {
            return None;
        }
        Some(
            json!({"held":forward,"points":points,"landing_after":landing,"nominal_end_depth":nominal.last()?.state.depth}),
        )
    }
    pub(super) fn recovery_finish(
        &mut self,
        d: &Value,
        frame: i64,
        previous: i64,
        mut held: i64,
    ) -> i64 {
        if flag(&self.base.trace["recovery"]) && flag(&self.base.trace["in_shaft"]) {
            self.last_pit_recovery = frame;
        }
        let player = self.base.player;
        let me = &d["observation"]["players"][player];
        let raw = &d["runtimePlayers"][player];
        let other = &d["observation"]["players"][player ^ 1];
        let ort = &d["runtimePlayers"][player ^ 1];
        let mut plan = self.recovery_finish_plan.take();
        let mut clear =
            !alive(other) || (ordinary_other(ort) && !near(other, me, 32.0, Some(64.0)));
        if let Some(p) = &plan {
            self.recovery_finish_nav.update(me);
            let elapsed = frame - int(&p["start"]);
            let points = array(&p["points"]);
            let start = (elapsed.max(0) as usize).min(points.len());
            clear = clear
                && crate::opening::counterjump_clear(
                    d,
                    player,
                    &points[start..(start + 6).min(points.len())],
                    &self.recovery_finish_nav,
                    false,
                );
            let valid = elapsed > 0
                && elapsed <= int(&p["landing_after"]) + 6
                && elapsed as usize <= points.len()
                && !flag(&me["dead"])
                && me["contact"].get("tileGround").is_some_and(|v| !flag(v))
                && raw["currentPowerupRaw"] == p["form"]
                && raw["damageStateRaw"].as_i64() == Some(0)
                && raw["updateLockedRaw"].as_i64() == Some(0)
                && !differs(me, &points[elapsed as usize - 1], 0.5);
            if !clear || !valid {
                self.base.trace["recovery_finish_end"] = json!(frame);
                plan = None;
            }
        }
        if plan.is_none()
            && clear
            && (0..=18).contains(&(frame - self.last_pit_recovery))
            && text(&self.base.trace["target"]) == "protect_lead"
        {
            if let Some(mut p) = self.recovery_proposal(d, held, previous, false) {
                if crate::opening::counterjump_clear(
                    d,
                    player,
                    &array(&p["points"])[..int(&p["landing_after"]) as usize],
                    &self.recovery_finish_nav,
                    false,
                ) {
                    p["start"] = json!(frame);
                    p["form"] = raw["currentPowerupRaw"].clone();
                    plan = Some(p);
                }
            }
        }
        if let Some(p) = &plan {
            held = int(&p["held"]);
            self.base.trace["held"] = json!(held);
            self.base.trace["recovery_finish"] = without_points(p);
        }
        self.recovery_finish_plan = plan;
        held
    }
}
pub(super) fn pos_error(me: &Value, p: &Value) -> f64 {
    wrap(num(&me["pos"]["x"]) / 4096.0, num(&p["x"]))
        .abs()
        .max((-num(&me["pos"]["y"]) / 4096.0 - num(&p["depth"])).abs())
}
