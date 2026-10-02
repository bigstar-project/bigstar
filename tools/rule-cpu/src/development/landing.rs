use super::*;
use super::{
    encounters::{defeated, own_fire},
    recovery::pos_error,
};
use crate::physics::{forecast_player, ForecastOptions, Point};
fn direction(held: i64) -> i64 {
    i64::from(held & 16 != 0) - i64::from(held & 32 != 0)
}
fn trace_plan(plan: &Value) -> Value {
    let mut p = plan.clone();
    for key in ["points", "track"] {
        p.as_object_mut().unwrap().remove(key);
    }
    p
}
fn close(p: &Point, e: &EnemyPoint, hb: &Value, margin: f64) -> bool {
    (wrap(p.state.x + num(&hb["centerOffsetX"]) / 4096.0, e.x).abs()
        < num(&hb["halfWidth"]) / 4096.0 + 8.0 + margin)
        && ((p.state.depth - num(&hb["centerOffsetY"]) / 4096.0 - (e.depth - 8.0)).abs()
            < num(&hb["halfHeight"]) / 4096.0 + 8.0 + margin)
}
fn wait_eligible(d: &Value, player: usize) -> bool {
    let obs = &d["observation"];
    let me = &obs["players"][player];
    let raw = &d["runtimePlayers"][player];
    let delay = d["time"]["inputDelay"].as_i64().unwrap_or(-1);
    if obs["stage"]["id"].as_i64() != Some(0)
        || me["contact"].get("tileGround").is_none_or(flag)
        || !ordinary(raw, me, &[0, 1, 2], &[0, 2, 128, 130, 2050])
        || raw["damageCooldownRaw"].as_i64() != Some(0)
        || !(0..=6).contains(&delay)
    {
        return false;
    }
    let other = &obs["players"][player ^ 1];
    if alive(other) && near(other, me, 48.0, Some(64.0)) {
        return false;
    }
    !array(&obs["entities"])
        .iter()
        .any(|e| hazard(e) && !own_fire(e, player) && !defeated(e) && near(e, me, 48.0, Some(64.0)))
}
impl DevelopmentRule {
    fn npc_proposal(&mut self, d: &Value, held: i64, previous: i64) -> Option<Value> {
        let player = self.base.player;
        let obs = &d["observation"];
        let me = &obs["players"][player];
        let raw = &d["runtimePlayers"][player];
        let initial = motion(me, raw, false);
        let dir = if initial.vx > 0.0 { 1 } else { -1 };
        let delay = d["time"]["inputDelay"].as_i64().unwrap_or(-1);
        if me["contact"].get("tileGround").is_none_or(flag)
            || !ordinary(raw, me, &[0], &[2])
            || !matches!(int(&raw["behaviorFuncRaw"]), 0x021135b8 | 0x02115aac)
            || raw["damageCooldownRaw"].as_i64() != Some(0)
            || held & (3 | 64 | 128) != 0
            || !(0.5..=3.0).contains(&initial.vx.abs())
            || !(-3.5..=1.0).contains(&initial.vy)
            || direction(held) != dir
            || !(0..=6).contains(&delay)
        {
            return None;
        }
        let other = &obs["players"][player ^ 1];
        if alive(other) && near(other, me, 192.0, None) {
            return None;
        }
        let mut enemies = vec![];
        let mut other_hazards = vec![];
        for e in array(&obs["entities"]) {
            if !hazard(e) || defeated(e) {
                continue;
            }
            let (dx, dy) = delta(&e["pos"], &me["pos"]);
            if text(&e["category"]) == "player_fireball" && dx.abs() < 320.0 {
                return None;
            }
            if dx.abs() > 144.0 || dy.abs() > 128.0 {
                continue;
            }
            if text(&e["category"]) == "enemy_koopa"
                && e["koopaShellModeRaw"].as_i64() == Some(0)
                && int(&e["koopaBehaviorFunctionRaw"]) == 0x020dfc58
            {
                enemies.push(e)
            } else {
                other_hazards.push(e)
            }
        }
        if enemies.len() != 1 {
            return None;
        }
        let enemy = enemies[0];
        let (dx, dy) = delta(&enemy["pos"], &me["pos"]);
        if !(16.0 < dir as f64 * dx && dir as f64 * dx < 112.0) || !(-96.0 < dy && dy < -16.0) {
            return None;
        }
        self.npc_landing_nav.update(me);
        let occupied = |x, y| self.npc_landing_nav.occupied(x, y);
        let (track, status) = actors::ground_koopa(enemy, &occupied, 48);
        if status != "horizon" || track.len() != 48 {
            return None;
        }
        let hb = &me["hitbox"];
        if !flag(&hb["found"]) || int(&hb["fixedPointShift"]) != 12 {
            return None;
        }
        let (nominal, _) = forecast_player(
            initial,
            previous,
            &planners::sequence(previous, delay as usize, held, 48),
            &occupied,
            ForecastOptions::default(),
        );
        let land = nominal.iter().position(|p| p.contact == Some("floor"))?;
        if land > 30 {
            return None;
        }
        let contact = nominal
            .iter()
            .zip(&track)
            .position(|(p, e)| close(p, e, hb, 0.0))?;
        if contact > land + 2 {
            return None;
        }
        let head = nominal
            .iter()
            .zip(&track)
            .position(|(p, e)| p.state.depth >= e.depth - 16.0)?;
        let hw = num(&hb["halfWidth"]) / 4096.0;
        if wrap(nominal[head].state.x, track[head].x).abs() <= hw + 10.0 {
            return None;
        }
        let brake = buttons(-dir) | 2048;
        let (points, status) = forecast_player(
            initial,
            previous,
            &planners::sequence(previous, delay as usize, brake, 48),
            &occupied,
            ForecastOptions::default(),
        );
        let landing = points
            .iter()
            .position(|p| p.contact == Some("floor"))
            .map(|i| i + 1)?;
        if status != "horizon" || landing > 30 {
            return None;
        }
        let support = &points[landing - 1];
        if ![-5.0, 0.0, 5.0]
            .iter()
            .all(|o| occupied(support.state.x + o, support.state.depth + 1.0))
        {
            return None;
        }
        if points
            .iter()
            .take(landing + 8)
            .zip(&track)
            .any(|(p, e)| close(p, e, hb, 4.0) || p.state.depth > 288.0)
        {
            return None;
        }
        for e in other_hazards {
            if text(&e["category"]) != "enemy_goomba"
                || int(&e["goombaBehaviorFunctionRaw"]) != 0x020e1538
                || num(&e["vel"]["y"]) != 0.0
                || num(&e["vel"]["x"]).abs() > 2048.0
                || points.iter().take(landing + 8).enumerate().any(|(i, p)| {
                    wrap(p.state.x, num(&e["pos"]["x"]) / 4096.0).abs()
                        <= hw + 12.0 + 0.5 * (i + 1) as f64
                })
            {
                return None;
            }
        }
        Some(
            json!({"held":brake,"points":points,"track":track,"landing":landing,"guid":enemy["actorGuid"],"contact_after":contact+1}),
        )
    }
    fn npc_valid(&self, p: &Value, d: &Value, elapsed: i64) -> bool {
        let player = self.base.player;
        let obs = &d["observation"];
        let me = &obs["players"][player];
        let raw = &d["runtimePlayers"][player];
        if elapsed <= 0
            || elapsed >= int(&p["landing"]) + 6
            || elapsed as usize > array(&p["points"]).len()
            || flag(&me["dead"])
            || flag(&me["contact"]["tileGround"])
            || raw["currentPowerupRaw"].as_i64() != Some(0)
            || raw["damageStateRaw"].as_i64() != Some(0)
            || raw["updateLockedRaw"].as_i64() != Some(0)
        {
            return false;
        }
        let Some(e) = array(&obs["entities"])
            .iter()
            .find(|e| e["actorGuid"] == p["guid"])
        else {
            return false;
        };
        if int(&e["koopaBehaviorFunctionRaw"]) != 0x020dfc58
            || e["koopaShellModeRaw"].as_i64() != Some(0)
            || differs(me, &p["points"][elapsed as usize - 1], 0.25)
            || differs(e, &p["track"][elapsed as usize - 1], 0.25)
        {
            return false;
        }
        let other = &obs["players"][player ^ 1];
        if alive(other) && near(other, me, 96.0, None) {
            return false;
        }
        !array(&obs["entities"]).iter().any(|e| {
            e["actorGuid"] != p["guid"]
                && hazard(e)
                && !defeated(e)
                && near(e, me, 64.0, Some(64.0))
        })
    }
    pub(super) fn npc_landing(
        &mut self,
        d: &Value,
        frame: i64,
        previous: i64,
        mut held: i64,
    ) -> i64 {
        let mut plan = self.npc_landing_plan.take();
        if let Some(p) = &plan {
            if !self.npc_valid(p, d, frame - int(&p["start"])) {
                self.base.trace["npc_landing_end"] =
                    json!({"start":p["start"],"elapsed":frame-int(&p["start"])});
                plan = None;
            }
        }
        if plan.is_none() && frame >= self.npc_landing_retry {
            plan = self.npc_proposal(d, held, previous).map(|mut p| {
                p["start"] = json!(frame);
                self.npc_landing_retry = frame + 48;
                p
            });
        }
        if let Some(p) = &plan {
            held = int(&p["held"]);
            self.base.trace["held"] = json!(held);
            self.base.trace["npc_landing_brake"] = trace_plan(p);
        }
        self.npc_landing_plan = plan;
        held
    }
    fn wait_proposal(&mut self, d: &Value, held: i64, previous: i64) -> Option<Value> {
        let player = self.base.player;
        if !wait_eligible(d, player) {
            return None;
        }
        let me = &d["observation"]["players"][player];
        let raw = &d["runtimePlayers"][player];
        let initial = motion(me, raw, false);
        if !(224.0..=296.0).contains(&initial.depth)
            || initial.vy >= 0.0
            || (held | previous) & 3 != 0
        {
            return None;
        }
        self.wait_wall_nav.update(me);
        let nav = &self.wait_wall_nav;
        let occupied = |x, y| nav.occupied(x, y);
        let delay = int(&d["time"]["inputDelay"]);
        let height = if int(&raw["currentPowerupRaw"]) == 0 {
            16.0
        } else {
            27.0
        };
        let options = ForecastOptions {
            height,
            ..Default::default()
        };
        let (_, end) = forecast_player(
            initial,
            previous,
            &planners::sequence(previous, delay as usize, held, 36),
            &occupied,
            options,
        );
        if end != "deep" {
            return None;
        }
        let mut choices = vec![];
        for button in [16, 32] {
            let (points, reason) = forecast_player(
                initial,
                previous,
                &planners::sequence(previous, delay as usize, button, 36),
                &occupied,
                options,
            );
            let bound = super::pit::late_wall_depth(&points, button, previous, delay, nav, height);
            if reason == "wall" {
                if let Some(bound) = bound {
                    if num(&bound["peak_depth"]) < 332.0 {
                        choices.push(json!({"held":button,"points":points,"bound":bound,"form":raw["currentPowerupRaw"]}));
                    }
                }
            }
        }
        choices.into_iter().min_by(|a, b| {
            num(&a["bound"]["peak_depth"]).total_cmp(&num(&b["bound"]["peak_depth"]))
        })
    }
    pub(super) fn wait_wall(&mut self, d: &Value, frame: i64, previous: i64, mut held: i64) -> i64 {
        let player = self.base.player;
        let me = &d["observation"]["players"][player];
        let raw = &d["runtimePlayers"][player];
        let mut plan = self.wait_wall_plan.take();
        if let Some(p) = &plan {
            let elapsed = frame - int(&p["start"]);
            if !wait_eligible(d, player)
                || elapsed <= 0
                || elapsed as usize > array(&p["points"]).len()
                || elapsed >= 36
                || num(&me["vel"]["y"]) > 0.0
                || raw["currentPowerupRaw"] != p["form"]
                || differs(me, &p["points"][elapsed as usize - 1], 0.5)
            {
                plan = None;
            }
        }
        if plan.is_none()
            && text(&self.base.trace["target"]) == "wait"
            && frame >= self.base.wall_depart_until
        {
            plan = self.wait_proposal(d, held, previous).map(|mut p| {
                p["start"] = json!(frame);
                p
            });
        }
        if let Some(p) = &plan {
            let contact = flag(
                &me["contact"][if int(&p["held"]) & 16 != 0 {
                    "wallRight"
                } else {
                    "wallLeft"
                }],
            );
            held = int(&p["held"]) | if contact && previous & 2 == 0 { 2 } else { 0 };
            self.base.trace["held"] = json!(held);
            self.base.trace["wait_wall"] = trace_plan(p);
        }
        self.wait_wall_plan = plan;
        held
    }
    pub(super) fn falling_goomba(
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
        let mut plan = self.falling_goomba_plan.take();
        let mut eligible = flag(&me["found"])
            && flag(&raw["found"])
            && int(&obs["stage"]["group"]) == 9
            && obs["stage"]["id"].as_i64() == Some(0)
            && ordinary(raw, me, &[0], &[0, 130])
            && flag(&me["contact"]["tileGround"])
            && raw["damageCooldownRaw"].as_i64() == Some(0)
            && !(alive(other) && near(other, me, 128.0, None));
        let entities: Vec<_> = array(&obs["entities"])
            .iter()
            .filter(|e| hazard(e) && !defeated(e) && near(e, me, 160.0, Some(128.0)))
            .collect();
        eligible &= entities.len() == 1 && text(&entities[0]["category"]) == "enemy_goomba";
        if let Some(p) = &plan {
            let elapsed = frame - int(&p["start"]);
            if !eligible || elapsed <= 0 || elapsed >= 48 || entities[0]["actorGuid"] != p["guid"] {
                self.base.trace["falling_goomba_end"] =
                    json!({"start":p["start"],"frame":frame,"reason":"state_or_hazards"});
                plan = None;
            } else {
                let error = pos_error(me, &p["points"][elapsed as usize - 1])
                    .max(pos_error(entities[0], &p["track"][elapsed as usize - 1]));
                if error > 0.25 || int(&entities[0]["goombaBehaviorFunctionRaw"]) != 0x020e1538 {
                    self.base.trace["falling_goomba_end"] = json!({"start":p["start"],"frame":frame,"reason":"forecast_or_enemy_state","error":error});
                    plan = None;
                }
            }
        }
        let delay = d["time"]["inputDelay"].as_i64().unwrap_or(-1);
        let vx = num(&me["vel"]["x"]) / 4096.0;
        let dir = direction(held);
        if plan.is_none()
            && eligible
            && (0..=6).contains(&delay)
            && previous & 3 == 0
            && held & (64 | 128 | 1024) == 0
            && dir as f64 * vx > 0.1
            && vx.abs() < 2.25
            && flag(&raw["facingKnown"])
            && int(&raw["behaviorFuncRaw"]) == 0x02115aac
        {
            let enemy = entities[0];
            let (dx, dy) = delta(&enemy["pos"], &me["pos"]);
            let hb = &me["hitbox"];
            if dir as f64 * dx > 12.0
                && dir as f64 * dx < 80.0
                && dy > 24.0
                && dy < 96.0
                && flag(&hb["found"])
                && int(&hb["fixedPointShift"]) == 12
            {
                self.falling_goomba_nav.update(me);
                let occupied = |x, y| self.falling_goomba_nav.occupied(x, y);
                let (track, status) = actors::short_goomba(enemy, &occupied, 48);
                if status == "horizon"
                    && track.len() == 48
                    && track[47].depth > track[0].depth + 8.0
                {
                    let (cx, cy, hw, hh) = (
                        num(&hb["centerOffsetX"]) / 4096.0,
                        num(&hb["centerOffsetY"]) / 4096.0,
                        num(&hb["halfWidth"]) / 4096.0,
                        num(&hb["halfHeight"]) / 4096.0,
                    );
                    let clearance = |points: &[Point], margin: f64| {
                        points
                            .iter()
                            .zip(&track)
                            .map(|(p, e)| {
                                (wrap(p.state.x + cx, e.x).abs() - hw - 7.0 - margin).max(
                                    (p.state.depth - cy - (e.depth - 8.0)).abs()
                                        - hh
                                        - 8.0
                                        - margin,
                                )
                            })
                            .fold(f64::INFINITY, f64::min)
                    };
                    let initial = motion(me, raw, true);
                    let (nominal, _) = forecast_player(
                        initial,
                        previous,
                        &planners::sequence(previous, delay as usize, held, 48),
                        &occupied,
                        ForecastOptions::default(),
                    );
                    if nominal.len() >= 30 && clearance(&nominal[..30], 0.0) < 0.0 {
                        let brake = buttons(-dir) | 2048;
                        for duration in [6, 12, 18, 24] {
                            let mut commands = vec![previous; delay as usize];
                            commands.extend(vec![brake; duration]);
                            commands.resize(48, 0);
                            let (points, end) = forecast_player(
                                initial,
                                previous,
                                &commands,
                                &occupied,
                                ForecastOptions::default(),
                            );
                            if end != "horizon"
                                || points.len() != 48
                                || clearance(&points, 2.0) < 0.0
                                || points.iter().any(|p| {
                                    !p.state.grounded
                                        || p.contact.is_some()
                                        || ![-hw, hw]
                                            .iter()
                                            .all(|o| occupied(p.state.x + o, p.state.depth + 1.0))
                                })
                            {
                                continue;
                            }
                            plan = Some(
                                json!({"start":frame,"held":brake,"duration":duration,"delay":delay,"points":points,"track":track,"guid":enemy["actorGuid"],"clearance":clearance(&points,2.0)}),
                            );
                            break;
                        }
                    }
                }
            }
        }
        if let Some(p) = &plan {
            held = if frame - int(&p["start"]) < int(&p["duration"]) {
                int(&p["held"])
            } else {
                0
            };
            self.base.trace["held"] = json!(held);
            self.base.trace["falling_goomba_brake"] = trace_plan(p);
        }
        self.falling_goomba_plan = plan;
        held
    }
}
