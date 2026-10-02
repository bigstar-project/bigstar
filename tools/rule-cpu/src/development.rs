//! Ordered development policy layers. Each layer consumes the preceding
//! proposed action; the ordering is part of the preserved policy contract.
mod boxes;
mod braking;
mod early;
mod encounters;
mod jumping;
mod landing;
mod pit;
mod recovery;
#[cfg(test)]
mod tests;
mod wall_flight;
use crate::{
    actors::{self, EnemyPoint},
    development_base::DevelopmentBase,
    observation::*,
    physics::Motion,
    planners::{self, PlannerState},
};
use serde_json::{json, Value};

pub struct DevelopmentRule {
    pub base: DevelopmentBase,
    pub first_equipment_seen: bool,
    pub emergence_plan: Option<Value>,
    pub launch_plan: Option<Value>,
    pub air_plan: Option<Value>,
    pub ceiling_npc_plan: Option<Value>,
    pub ceiling_navigator: crate::navigation::GrassNavigator,
    pub exit_fire_plan: Option<Value>,
    pub exit_fire_retry: i64,
    pub ground_away_plan: Option<Value>,
    pub ceiling_npc_brake: Option<Value>,
    pub last_brake_ceiling: i64,
    pub ground_cooldown_plan: Option<Value>,
    pub shaft_contact_plan: Option<Value>,
    pub shaft_contact_previous: Option<Value>,
    pub shaft_contact_navigator: crate::navigation::GrassNavigator,
    pub observed_ceiling_koopa_plan: Option<Value>,
    pub observed_ceiling_navigator: crate::navigation::GrassNavigator,
    pub imminent_wall_entry: Option<Value>,
    pub imminent_wall_navigator: crate::navigation::GrassNavigator,
    pub missed_takeoff_retry: Option<Value>,
    pub missed_takeoff_navigator: crate::navigation::GrassNavigator,
    pub upper_npc_release: Option<Value>,
    pub skid_jump_plan: Option<Value>,
    pub rising_release_plan: Option<Value>,
    pub contested_mushroom_hop: Option<Value>,
    pub forward_npc_plan: Option<Value>,
    pub descent_koopa_shot: Option<Value>,
    pub descent_koopa_retry: i64,
    pub overhead_pit_plan: Option<Value>,
    pub overhead_pit_retry: i64,
    pub overhead_pit_nav: crate::navigation::GrassNavigator,
    pub last_pit_recovery: i64,
    pub recovery_finish_plan: Option<Value>,
    pub recovery_finish_nav: crate::navigation::GrassNavigator,
    pub npc_landing_plan: Option<Value>,
    pub npc_landing_retry: i64,
    pub npc_landing_nav: crate::navigation::GrassNavigator,
    pub wait_wall_plan: Option<Value>,
    pub wait_wall_nav: crate::navigation::GrassNavigator,
    pub falling_goomba_plan: Option<Value>,
    pub falling_goomba_nav: crate::navigation::GrassNavigator,
    pub wall_short_nav: crate::navigation::GrassNavigator,
    pub shot_recovery_plan: Option<Value>,
    pub shot_recovery_nav: crate::navigation::GrassNavigator,
    pub spacing_box_plan: Option<Value>,
    pub spacing_box_retry: i64,
    pub spacing_box_nav: crate::navigation::GrassNavigator,
    pub departure_nav: crate::navigation::GrassNavigator,
}
pub fn motion(me: &Value, raw: &Value, grounded: bool) -> Motion {
    Motion {
        x: num(&me["pos"]["x"]) / 4096.0,
        depth: -num(&me["pos"]["y"]) / 4096.0,
        vx: num(&me["vel"]["x"]) / 4096.0,
        vy: num(&me["vel"]["y"]) / 4096.0,
        grounded,
        facing: int(&raw["facing"]),
        turn_remaining: 0,
        edge_remaining: 0,
    }
}
pub fn ordinary(raw: &Value, me: &Value, powers: &[i64], physics: &[i64]) -> bool {
    !flag(&me["dead"])
        && powers.contains(&int(&raw["currentPowerupRaw"]))
        && raw["damageStateRaw"].as_i64() == Some(0)
        && raw["updateLockedRaw"].as_i64() == Some(0)
        && physics.contains(&int(&raw["physicsFlagRaw"]))
        && matches!(int(&raw["actionFlagRaw"]), 0 | 0x100000)
}
pub fn near(a: &Value, b: &Value, x: f64, y: Option<f64>) -> bool {
    let (dx, dy) = delta(&a["pos"], &b["pos"]);
    dx.abs() < x && y.is_none_or(|limit| dy.abs() < limit)
}
pub fn alive(p: &Value) -> bool {
    flag(&p["found"]) && !flag(&p["dead"])
}
pub fn hazard(e: &Value) -> bool {
    matches!(
        text(&e["category"]),
        "enemy_goomba" | "enemy_koopa" | "player_fireball"
    )
}
pub fn pick(value: &Value, keys: &[&str]) -> Value {
    let mut result = json!({});
    for &key in keys {
        result[key] = value[key].clone();
    }
    result
}
pub fn differs(me: &Value, p: &Value, tolerance: f64) -> bool {
    wrap(num(&me["pos"]["x"]) / 4096.0, num(&p["x"])).abs() > tolerance
        || (-num(&me["pos"]["y"]) / 4096.0 - num(&p["depth"])).abs() > tolerance
}
impl DevelopmentRule {
    pub fn new(player: usize, period: i64) -> Result<Self, String> {
        Ok(Self {
            base: DevelopmentBase::new(player, period)?,
            first_equipment_seen: false,
            emergence_plan: None,
            launch_plan: None,
            air_plan: None,
            ceiling_npc_plan: None,
            ceiling_navigator: crate::navigation::GrassNavigator::default(),
            exit_fire_plan: None,
            exit_fire_retry: -1,
            ground_away_plan: None,
            ceiling_npc_brake: None,
            last_brake_ceiling: -1000000,
            ground_cooldown_plan: None,
            shaft_contact_plan: None,
            shaft_contact_previous: None,
            shaft_contact_navigator: crate::navigation::GrassNavigator::default(),
            observed_ceiling_koopa_plan: None,
            observed_ceiling_navigator: crate::navigation::GrassNavigator::default(),
            imminent_wall_entry: None,
            imminent_wall_navigator: crate::navigation::GrassNavigator::default(),
            missed_takeoff_retry: None,
            missed_takeoff_navigator: crate::navigation::GrassNavigator::default(),
            upper_npc_release: None,
            skid_jump_plan: None,
            rising_release_plan: None,
            contested_mushroom_hop: None,
            forward_npc_plan: None,
            descent_koopa_shot: None,
            descent_koopa_retry: -1,
            overhead_pit_plan: None,
            overhead_pit_retry: -1,
            overhead_pit_nav: crate::navigation::GrassNavigator::default(),
            last_pit_recovery: -1000,
            recovery_finish_plan: None,
            recovery_finish_nav: crate::navigation::GrassNavigator::default(),
            npc_landing_plan: None,
            npc_landing_retry: -1,
            npc_landing_nav: crate::navigation::GrassNavigator::default(),
            wait_wall_plan: None,
            wait_wall_nav: crate::navigation::GrassNavigator::default(),
            falling_goomba_plan: None,
            falling_goomba_nav: crate::navigation::GrassNavigator::default(),
            wall_short_nav: crate::navigation::GrassNavigator::default(),
            shot_recovery_plan: None,
            shot_recovery_nav: crate::navigation::GrassNavigator::default(),
            spacing_box_plan: None,
            spacing_box_retry: -1,
            spacing_box_nav: crate::navigation::GrassNavigator::default(),
            departure_nav: crate::navigation::GrassNavigator::default(),
        })
    }
    pub fn reset(&mut self) {
        *self = Self::new(self.base.player, self.base.period).expect("validated controller");
    }
    pub fn act_through(&mut self, d: &Value, frame: i64, previous: i64, layer: usize) -> i64 {
        let reset = frame <= self.base.previous_frame;
        if frame <= self.base.previous_frame {
            self.reset();
        }
        self.base.target_layer = layer;
        if layer >= 21 && encounters::opponent_unsafe(d, self.base.player) {
            self.forward_npc_plan = None;
        }
        self.base.target_previous = if reset { 0 } else { previous };
        let mut filtered = d.clone();
        let removed: Vec<Value> = if layer >= 8 {
            array(&d["observation"]["entities"])
                .iter()
                .filter(|e| {
                    text(&e["category"]) == "enemy_goomba"
                        && int(&e["entityUpdateStateFound"]) == 1
                        && int(&e["entityUpdateStateRaw"]) == 2
                })
                .cloned()
                .collect()
        } else {
            vec![]
        };
        if !removed.is_empty() {
            filtered["observation"]["entities"] = json!(array(&d["observation"]["entities"])
                .iter()
                .filter(|e| !removed.contains(e))
                .collect::<Vec<_>>());
        }
        let inner = if removed.is_empty() { d } else { &filtered };
        let mut held = self.base.act(inner, frame, previous);
        if layer >= 1 {
            held = self.emergence(inner, frame, previous, held);
        }
        if layer >= 2 {
            held = self.launch(inner, frame, previous, held);
        }
        if layer >= 3 {
            held = self.air(inner, frame, previous, held);
        }
        if layer >= 4 {
            held = self.ceiling_npc(inner, frame, previous, held);
        }
        if layer >= 5 {
            held = self.wall_exit_fire(inner, frame, previous, held);
        }
        if layer >= 6 {
            held = self.ground_away(inner, frame, previous, held);
        }
        if layer >= 7 {
            held = self.persistent_brake(inner, frame, previous, held);
        }
        if !removed.is_empty() {
            self.base.trace["ignored_defeated_goombas"] =
                json!(removed.iter().map(|e| &e["actorGuid"]).collect::<Vec<_>>());
        }
        if layer >= 9 {
            held = self.ground_cooldown(d, frame, previous, held);
        }
        if layer >= 10 {
            held = self.shaft_contact(d, frame, previous, held);
        }
        if layer >= 11 {
            held = self.observed_ceiling(d, frame, previous, held);
        }
        if layer >= 13 {
            held = self.imminent_wall(d, frame, previous, held);
        }
        if layer >= 14 {
            held = self.missed_takeoff(d, frame, previous, held);
        }
        if layer >= 15 {
            held = self.upper_release(d, frame, previous, held);
        }
        if layer >= 16 {
            held = self.skid_jump(d, frame, previous, held);
        }
        if layer >= 17 {
            held = self.predictive_rejump(d, frame, previous, held);
        }
        if layer >= 18 {
            held = self.rising_release(d, frame, previous, held);
        }
        if layer >= 19 {
            held = self.contested_hop(d, frame, previous, held);
        }
        if layer >= 20 {
            held = self.forward_npc(d, frame, previous, held);
        }
        if layer >= 22 {
            held = self.descent_shot(d, frame, previous, held);
        }
        if layer >= 23 && !self.base.falling_mushroom_excluded.is_empty() {
            self.base.trace["falling_mushroom_excluded"] =
                json!(self.base.falling_mushroom_excluded);
        }
        if layer >= 24 {
            held = self.overhead_pit(d, frame, previous, held);
        }
        if layer >= 25 {
            held = self.recovery_finish(d, frame, previous, held);
        }
        if layer >= 26 {
            held = self.npc_landing(d, frame, previous, held);
        }
        if layer >= 27 {
            held = self.wait_wall(d, frame, previous, held);
        }
        if layer >= 28 {
            held = self.falling_goomba(d, frame, previous, held);
        }
        if layer >= 29 {
            held = self.wall_short(d, frame, previous, held);
        }
        if layer >= 30 {
            held = self.shot_recovery(d, frame, previous, held);
        }
        if layer >= 31 {
            held = self.spacing_box(d, frame, previous, held);
        }
        if layer >= 32 {
            held = self.departure(d, frame, previous, held);
        }
        held
    }
    fn emergence(&mut self, d: &Value, frame: i64, previous: i64, mut held: i64) -> i64 {
        let player = self.base.player;
        let obs = &d["observation"];
        let me = &obs["players"][player];
        let raw = &d["runtimePlayers"][player];
        let entities = array(&obs["entities"]);
        self.first_equipment_seen |= int(&raw["currentPowerupRaw"]) != 0;
        let eligible = ordinary(raw, me, &[0], &[2, 130, 2050]);
        let mut active = self.emergence_plan.take();
        if active.is_some()
            || entities
                .iter()
                .any(|e| int(&e["itemBehaviorFunctionRaw"]) == 0x020d438c)
        {
            self.base.navigator.update(me);
        }
        let nav = &self.base.navigator;
        let signature = json!(nav.signature());
        let s = motion(me, raw, true);
        if let Some(plan) = &active {
            let elapsed = frame - int(&plan["start"]);
            let item = entities
                .iter()
                .find(|e| e["actorGuid"] == plan["item_guid"]);
            let mut reason = if !eligible {
                Some("player_state")
            } else if elapsed < 0 || elapsed > int(&plan["pickup_frame"]) + 6 {
                Some("expired")
            } else if signature != plan["terrain_signature"] {
                Some("terrain_changed")
            } else if item.is_none() {
                Some("item_missing")
            } else {
                None
            };
            if reason.is_none() && elapsed > 0 {
                for (name, observed, points) in [
                    ("player_deviation", me, array(&plan["points"])),
                    ("item_deviation", item.unwrap(), array(&plan["item_points"])),
                ] {
                    if differs(
                        observed,
                        &points[(elapsed as usize - 1).min(points.len() - 1)],
                        2.0,
                    ) {
                        reason = Some(name);
                        break;
                    }
                }
            }
            if reason.is_none() {
                reason = crate::opening::edge_world_reason(plan, d, player, frame, nav);
            }
            if let Some(reason) = reason {
                self.base.trace["emergence_plan_cancel"] = json!(reason);
                active = None;
            }
        }
        if active.is_none()
            && eligible
            && num(&me["vel"]["y"]) == -8192.0
            && [-5.0, 0.0, 4.0]
                .iter()
                .any(|foot| nav.occupied(s.x + foot, s.depth + 1.0))
        {
            let items: Vec<_> = entities
                .iter()
                .filter(|e| {
                    int(&e["itemBehaviorFunctionRaw"]) == 0x020d438c
                        && delta(&e["pos"], &me["pos"]).0.abs() < 144.0
                })
                .collect();
            let mut paths = vec![];
            let mut known = true;
            for e in entities {
                if !hazard(e) || delta(&e["pos"], &me["pos"]).0.abs() > 256.0 {
                    continue;
                }
                if !self.first_equipment_seen
                    && text(&e["category"]) == "enemy_goomba"
                    && int(&e["goombaBehaviorFunctionRaw"]) == 0x020e13d0
                {
                    continue;
                }
                if text(&e["category"]) != "enemy_goomba" || num(&e["vel"]["x"]).abs() != 2048.0 {
                    known = false;
                    break;
                }
                let (path, _) = actors::ground_goomba(
                    num(&e["pos"]["x"]) / 4096.0,
                    -num(&e["pos"]["y"]) / 4096.0,
                    num(&e["vel"]["x"]) / 4096.0,
                    &|x, y| nav.occupied(x, y),
                    160,
                    0,
                );
                if path.len() != 160 {
                    known = false;
                    break;
                }
                paths.push(path);
            }
            if known {
                for item in items {
                    let delay = d["time"]["inputDelay"].as_i64().unwrap_or(2);
                    let ground = planners::interception(
                        s,
                        previous,
                        delay,
                        item,
                        &|x, y| nav.occupied(x, y),
                        &paths,
                        false,
                    );
                    let ascent = planners::interception(
                        s,
                        previous,
                        delay,
                        item,
                        &|x, y| nav.occupied(x, y),
                        &paths,
                        true,
                    );
                    let edge = if !self.first_equipment_seen {
                        crate::opening::edge_interception(
                            s,
                            previous,
                            delay,
                            item,
                            nav,
                            &paths,
                            crate::opening::EdgeOptions {
                                horizon: 130,
                                only_side: None,
                            },
                        )
                    } else {
                        None
                    };
                    let mut plan = [ground.clone(), ascent.clone(), edge]
                        .into_iter()
                        .flatten()
                        .min_by_key(|p| int(&p["pickup_frame"]));
                    let other = &obs["players"][player ^ 1];
                    if plan.as_ref().is_some_and(|p| !p["edge_route"].is_null())
                        && (raw["subActionFlagRaw"].as_i64().unwrap_or(1) & 1 != 0
                            || (alive(other) && near(other, me, 128.0, None)))
                    {
                        plan = [ground, ascent]
                            .into_iter()
                            .flatten()
                            .min_by_key(|p| int(&p["pickup_frame"]));
                    }
                    if let Some(mut plan) = plan {
                        plan["start"] = json!(frame);
                        plan["terrain_signature"] = signature.clone();
                        active = Some(plan);
                        break;
                    }
                }
            }
        }
        if let Some(plan) = &active {
            let elapsed = (frame - int(&plan["start"])) as usize;
            held = array(&plan["input_sequence"]).get(elapsed).map_or(0, int);
            self.base.jump_until = frame;
            self.base.landing_support = None;
            self.base.trace["emergence_intercept"] = pick(
                plan,
                &[
                    "start",
                    "held",
                    "duration",
                    "pickup_frame",
                    "item_guid",
                    "item_x",
                    "item_depth",
                ],
            );
            if !plan["edge_route"].is_null() {
                self.base.trace["emergence_edge_route"] = plan["edge_route"].clone();
            }
        }
        self.emergence_plan = active;
        self.base.trace["held"] = json!(held);
        held
    }
    fn launch(&mut self, d: &Value, frame: i64, previous: i64, mut held: i64) -> i64 {
        let player = self.base.player;
        let obs = &d["observation"];
        let me = &obs["players"][player];
        let raw = &d["runtimePlayers"][player];
        let other = &obs["players"][player ^ 1];
        let entities = array(&obs["entities"]);
        let s = motion(me, raw, true);
        let eligible = ordinary(raw, me, &[0, 1, 2], &[2, 130, 2050]);
        let mut active = self.launch_plan.take();
        if let Some(plan) = &active {
            let elapsed = frame - int(&plan["start"]);
            self.base.navigator.update(me);
            let signature = json!(self.base.navigator.signature());
            let mut reason = if !eligible || self.emergence_plan.is_some() {
                Some("player_state")
            } else if elapsed < 0 || elapsed >= int(&plan["arrival"]) {
                Some("finished")
            } else if signature != plan["terrain_signature"] {
                Some("terrain_changed")
            } else if elapsed > 0 && differs(me, &plan["points"][elapsed as usize - 1], 2.0) {
                Some("player_deviation")
            } else {
                None
            };
            if reason.is_none() && elapsed > 0 {
                for h in array(&plan["hazards"]) {
                    let e = entities.iter().find(|e| e["actorGuid"] == h["guid"]);
                    if e.is_none() {
                        if flag(&h["lower_bound"]) {
                            continue;
                        }
                        reason = Some("enemy_missing");
                        break;
                    }
                    let e = e.unwrap();
                    let points = array(&h["points"]);
                    let expected = &points[(elapsed as usize - 1).min(points.len() - 1)];
                    if flag(&h["lower_bound"]) {
                        if -num(&e["pos"]["y"]) / 4096.0 < num(&expected["depth"]) - 2.0 {
                            reason = Some("enemy_vertical_bound");
                        }
                    } else if differs(e, expected, 2.0) {
                        reason = Some("enemy_deviation");
                    }
                    if reason.is_some() {
                        break;
                    }
                }
                if entities.iter().any(|e| {
                    hazard(e)
                        && !array(&plan["hazards"])
                            .iter()
                            .any(|h| h["guid"] == e["actorGuid"])
                        && near(e, me, 96.0, Some(64.0))
                }) {
                    reason = Some("new_hazard");
                }
                if alive(other) && near(other, me, 48.0, None) {
                    reason = Some("opponent_near");
                }
            }
            if let Some(reason) = reason {
                self.base.trace["launch_plan_cancel"] = json!(reason);
                active = None;
            }
        }
        if active.is_none()
            && eligible
            && held & 2 != 0
            && num(&me["vel"]["y"]) == -8192.0
            && self.emergence_plan.is_none()
        {
            self.base.navigator.update(me);
            let nav = &self.base.navigator;
            let occupied = |x, y| nav.occupied(x, y);
            let grounded = [-5.0, 0.0, 4.0]
                .iter()
                .any(|f| occupied(s.x + f, s.depth + 1.0));
            let goals: Vec<_> = array(&self.base.trace["waypoint"]["path"])
                .iter()
                .filter(|g| {
                    (24.0..=144.0).contains(&wrap(num(&g[0]), s.x).abs())
                        && (-64.0..=-8.0).contains(&(num(&g[1]) - s.depth))
                })
                .map(|g| (num(&g[0]), num(&g[1])))
                .collect();
            let mut paths = vec![];
            let mut hazards = vec![];
            let mut known = true;
            for e in entities {
                if !hazard(e) || delta(&e["pos"], &me["pos"]).0.abs() > 256.0 {
                    continue;
                }
                let lower = text(&e["category"]) == "enemy_goomba"
                    && matches!(
                        int(&e["goombaBehaviorFunctionRaw"]),
                        0x020e1478 | 0x020e1538
                    )
                    && num(&e["vel"]["y"]) <= -14336.0
                    && -num(&e["pos"]["y"]) / 4096.0 > s.depth + 48.0;
                let path = if lower {
                    vec![
                        EnemyPoint {
                            x: s.x,
                            depth: -num(&e["pos"]["y"]) / 4096.0,
                            vx: 0.0
                        };
                        96
                    ]
                } else if text(&e["category"]) == "enemy_koopa" {
                    actors::ground_koopa(e, &occupied, 96).0
                } else if text(&e["category"]) == "enemy_goomba"
                    && num(&e["vel"]["x"]).abs() == 2048.0
                {
                    actors::ground_goomba(
                        num(&e["pos"]["x"]) / 4096.0,
                        -num(&e["pos"]["y"]) / 4096.0,
                        num(&e["vel"]["x"]) / 4096.0,
                        &occupied,
                        96,
                        0,
                    )
                    .0
                } else {
                    vec![]
                };
                if path.is_empty() {
                    known = false;
                    break;
                }
                hazards.push(json!({"guid":e["actorGuid"],"points":path,"lower_bound":lower}));
                paths.push(path);
            }
            if alive(other) && near(other, me, 64.0, None) {
                known = false;
            }
            if known && grounded {
                for goal in goals {
                    if let Some(mut plan) = planners::launch_delay(
                        PlannerState {
                            motion: s,
                            height: if int(&raw["currentPowerupRaw"]) == 0 {
                                16.0
                            } else {
                                27.0
                            },
                        },
                        previous,
                        d["time"]["inputDelay"].as_i64().unwrap_or(2),
                        goal,
                        &occupied,
                        &paths,
                        96,
                    ) {
                        plan["start"] = json!(frame);
                        plan["goal"] = json!({"x":goal.0,"depth":goal.1});
                        plan["hazards"] = json!(hazards);
                        plan["terrain_signature"] = json!(nav.signature());
                        active = Some(plan);
                        break;
                    }
                }
            }
        }
        if let Some(plan) = &active {
            let seq = array(&plan["input_sequence"]);
            held = int(&seq[((frame - int(&plan["start"])) as usize).min(seq.len() - 1)]);
            self.base.jump_until = frame;
            self.base.trace["launch_plan"] =
                pick(plan, &["start", "wait", "hold", "arrival", "goal"]);
        }
        self.launch_plan = active;
        self.base.trace["held"] = json!(held);
        held
    }
    fn air(&mut self, d: &Value, frame: i64, previous: i64, mut held: i64) -> i64 {
        let player = self.base.player;
        let obs = &d["observation"];
        let me = &obs["players"][player];
        let raw = &d["runtimePlayers"][player];
        let other = &obs["players"][player ^ 1];
        let eligible = ordinary(raw, me, &[0, 1, 2], &[0, 2, 128, 130, 2050]);
        let grounded = int(&raw["collisionFlagRaw"]) & 0x8001 != 0;
        let s = motion(me, raw, false);
        let nearby = array(&obs["entities"])
            .iter()
            .any(|e| hazard(e) && near(e, me, 128.0, Some(96.0)))
            || (alive(other) && near(other, me, 64.0, Some(64.0)));
        let mut active = self.air_plan.take();
        if let Some(plan) = &active {
            let elapsed = frame - int(&plan["start"]);
            self.base.navigator.update(me);
            let signature = json!(self.base.navigator.signature());
            let mut reason = if !eligible || nearby {
                Some("state_or_hazard")
            } else if elapsed < 0
                || elapsed >= (int(&plan["settle"]) + 6).min(array(&plan["points"]).len() as i64)
            {
                Some("finished")
            } else if signature != plan["terrain_signature"] {
                Some("terrain_changed")
            } else {
                None
            };
            if reason.is_none() && elapsed > 0 {
                let i = (elapsed as usize - 1).min(array(&plan["points"]).len() - 1);
                let p = &plan["points"][i];
                let q = &plan["conservative_points"][i];
                let a = wrap(s.x, num(&p["x"]));
                let b = wrap(s.x, num(&q["x"]));
                if a.min(b) > 2.0
                    || a.max(b) < -2.0
                    || s.depth < num(&p["depth"]).min(num(&q["depth"])) - 2.0
                    || s.depth > num(&p["depth"]).max(num(&q["depth"])) + 2.0
                {
                    reason = Some("outside_prediction");
                }
            }
            if let Some(reason) = reason {
                self.base.trace["air_plan_cancel"] = json!(reason);
                active = None;
            }
        }
        let drop = matches!(
            text(&self.base.trace["target"]),
            "natural_star" | "dropped_star" | "powerup"
        ) && num(&self.base.trace["dy"]) < -24.0
            && self.base.trace["dx"].as_f64().unwrap_or(1024.0).abs() < 48.0;
        if active.is_none()
            && eligible
            && !nearby
            && !grounded
            && !drop
            && self.launch_plan.is_none()
            && self.emergence_plan.is_none()
            && matches!(int(&raw["behaviorFuncRaw"]), 0x021135b8 | 0x02110dc0)
        {
            self.base.navigator.update(me);
            if let Some(mut plan) = planners::air_landing(
                s,
                if int(&raw["currentPowerupRaw"]) == 0 {
                    16.0
                } else {
                    27.0
                },
                held,
                previous,
                d["time"]["inputDelay"].as_i64().unwrap_or(2),
                &|x, y| self.base.navigator.occupied(x, y),
                48,
            ) {
                plan["start"] = json!(frame);
                plan["terrain_signature"] = json!(self.base.navigator.signature());
                active = Some(plan);
            }
        }
        if let Some(plan) = &active {
            held = int(&plan["held"]);
            self.base.jump_until = frame;
            self.base.landing_support = None;
            self.base.trace["air_landing_plan"] =
                pick(plan, &["start", "held", "arrival", "settle"]);
        }
        self.air_plan = active;
        self.base.trace["held"] = json!(held);
        held
    }
}
