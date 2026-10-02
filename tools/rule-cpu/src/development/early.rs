use super::*;
use crate::physics::vertical_step;
impl DevelopmentRule {
    pub(super) fn ceiling_npc(
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
        let eligible = ordinary(raw, me, &[0], &[0, 2, 130, 2050]);
        let opponent_near = alive(other) && near(other, me, 64.0, Some(64.0));
        self.ceiling_navigator.update(me);
        let nav = &self.ceiling_navigator;
        let signature = json!(nav.signature());
        let s = motion(me, raw, false);
        let mut active = self.ceiling_npc_plan.take();
        if let Some(plan) = &active {
            let elapsed = frame - int(&plan["start"]);
            let mut reason = if !eligible || opponent_near {
                Some("state_or_opponent")
            } else if elapsed < 0 || elapsed >= int(&plan["duration"]) {
                Some("finished")
            } else if signature != plan["terrain_signature"] {
                Some("terrain_changed")
            } else {
                None
            };
            if reason.is_none() && elapsed > 0 {
                if ["points", "conservative_points"]
                    .iter()
                    .all(|&k| differs(me, &plan[k][elapsed as usize - 1], 2.0))
                {
                    reason = Some("player_deviation");
                }
                for h in array(&plan["hazards"]) {
                    let e = entities.iter().find(|e| e["actorGuid"] == h["guid"]);
                    if e.is_none_or(|e| differs(e, &h["points"][elapsed as usize - 1], 2.0)) {
                        reason = Some("enemy_deviation");
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
            }
            if let Some(reason) = reason {
                self.base.trace["ceiling_npc_cancel"] = json!(reason);
                active = None;
            }
        }
        if active.is_none()
            && eligible
            && !opponent_near
            && num(&me["vel"]["y"]) <= 0.0
            && num(&me["vel"]["y"]) != -8192.0
            && self.emergence_plan.is_none()
            && self.launch_plan.is_none()
            && self.air_plan.is_none()
            && [-8.0, 0.0, 8.0]
                .iter()
                .any(|dx| nav.occupied(s.x + dx, s.depth - 20.0))
        {
            let mut paths = vec![];
            let mut hazards = vec![];
            let mut known = true;
            for e in entities {
                let (dx, dy) = delta(&e["pos"], &me["pos"]);
                if !hazard(e) || dx.abs() > 128.0 || dy.abs() > 96.0 {
                    continue;
                }
                if int(&e["goombaBehaviorFunctionRaw"]) == 0x020e13d0 {
                    continue;
                }
                if text(&e["category"]) != "enemy_koopa" {
                    known = false;
                    break;
                }
                let (path, _) = actors::ground_koopa(e, &|x, y| nav.occupied(x, y), 36);
                if path.len() != 36 {
                    known = false;
                    break;
                }
                hazards.push(json!({"guid":e["actorGuid"],"points":path}));
                paths.push(path);
            }
            if known && !paths.is_empty() {
                if let Some(mut plan) = planners::ceiling_npc(
                    PlannerState {
                        motion: s,
                        height: 16.0,
                    },
                    held,
                    previous,
                    d["time"]["inputDelay"].as_i64().unwrap_or(2),
                    &|x, y| nav.occupied(x, y),
                    &paths,
                    36,
                ) {
                    plan["start"] = json!(frame);
                    plan["hazards"] = json!(hazards);
                    plan["terrain_signature"] = signature;
                    active = Some(plan);
                }
            }
        }
        if let Some(plan) = &active {
            held = int(&plan["held"]);
            self.base.jump_until = frame;
            self.base.landing_support = None;
            self.base.trace["ceiling_npc_plan"] = pick(plan, &["start", "held", "duration"]);
        }
        self.ceiling_npc_plan = active;
        self.base.trace["held"] = json!(held);
        held
    }
    pub(super) fn wall_exit_fire(
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
        let s = motion(me, raw, false);
        let grounded = flag(&self.base.trace["grounded"]);
        if flag(&me["dead"]) || int(&raw["currentPowerupRaw"]) != 2 {
            self.exit_fire_plan = None;
            return held;
        }
        let mut active = self.exit_fire_plan.take();
        if let Some(plan) = active.as_mut() {
            if grounded && plan["landed"].is_null() {
                plan["landed"] = json!(frame);
            }
        }
        if active.as_ref().is_some_and(|p| {
            frame >= int(&p["end"]) || (!p["landed"].is_null() && frame >= int(&p["landed"]) + 6)
        }) {
            active = None;
        }
        if active.is_none()
            && frame >= self.exit_fire_retry
            && !grounded
            && d["time"]["inputDelay"].as_i64().unwrap_or(2) == 2
            && int(&raw["actionFlagRaw"]) & 32 != 0
            && previous & 2 != 0
            && previous & 2048 == 0
            && s.vx.abs() < 0.1
            && 1.5 < s.vy
            && s.vy < 3.3
            && flag(&raw["facingKnown"])
            && !flag(&raw["damageStateRaw"])
        {
            let own_balls = array(&obs["entities"])
                .iter()
                .filter(|e| {
                    text(&e["category"]) == "player_fireball"
                        && flag(&e["ownerVerified"])
                        && int(&e["owner"]) == player as i64
                })
                .count();
            if own_balls < 2 {
                let direction = int(&raw["facing"]);
                let wall_x = s.x + direction as f64 * 10.0;
                let nav = &self.base.navigator;
                if nav.occupied(wall_x, s.depth - 4.0) {
                    let mut surfaces = vec![];
                    let mut y = (s.depth / 16.0).floor() * 16.0;
                    let end = ((s.depth - 64.0) / 16.0).floor() * 16.0;
                    while y > end {
                        if nav.occupied(wall_x, y + 1.0)
                            && !nav.occupied(wall_x, y - 1.0)
                            && !nav.occupied(wall_x, y - 24.0)
                        {
                            surfaces.push(y);
                        }
                        y -= 16.0;
                    }
                    let mut velocity = s.vy;
                    let mut rise = 0.0;
                    let mut maximum = 0.0_f64;
                    for _ in 0..32 {
                        velocity = vertical_step(velocity, true);
                        rise += velocity;
                        maximum = maximum.max(rise);
                    }
                    if let Some(surface) = surfaces
                        .into_iter()
                        .filter(|&y| 8.0 < s.depth - y && s.depth - y <= maximum)
                        .max_by(f64::total_cmp)
                    {
                        active = Some(
                            json!({"start":frame,"end":frame+42,"direction":direction,"surface":surface,"predicted_rise":maximum,"landed":null}),
                        );
                        self.exit_fire_retry = frame + 90;
                    }
                }
            }
        }
        if let Some(plan) = &active {
            held = buttons(int(&plan["direction"]));
            if !grounded && frame < int(&plan["start"]) + 18 {
                held |= 2050;
            }
            self.base.trace["held"] = json!(held);
            self.base.trace["wall_exit_fire"] = plan.clone();
        }
        self.exit_fire_plan = active;
        held
    }
    pub(super) fn ground_away(
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
        let entities = array(&obs["entities"]);
        let eligible = !flag(&me["dead"])
            && int(&raw["currentPowerupRaw"]) == 0
            && raw["damageStateRaw"].as_i64() == Some(0)
            && raw["updateLockedRaw"].as_i64() == Some(0)
            && !(alive(other) && near(other, me, 64.0, Some(64.0)));
        let mut active = self.ground_away_plan.take();
        if let Some(plan) = &active {
            let enemy = entities.iter().find(|e| e["actorGuid"] == plan["guid"]);
            if !eligible
                || frame >= int(&plan["end"])
                || enemy.is_none_or(|e| {
                    let (dx, dy) = delta(&e["pos"], &me["pos"]);
                    dx.abs() >= 28.0 || dy.abs() > 24.0
                })
            {
                active = None;
            }
        }
        if active.is_none()
            && eligible
            && !flag(&self.base.trace["grounded"])
            && -4.0 <= s.vy
            && s.vy < 0.0
            && (0.25..=3.0).contains(&s.vx.abs())
            && held & 2 == 0
            && int(&raw["actionFlagRaw"]) == 0x100000
        {
            let proposed = i64::from(held & 16 != 0) - i64::from(held & 32 != 0);
            let old = i64::from(previous & 16 != 0) - i64::from(previous & 32 != 0);
            if proposed != 0 && old == -proposed {
                for e in entities {
                    if text(&e["category"]) != "enemy_goomba"
                        || int(&e["goombaBehaviorFunctionRaw"]) != 0x020e1538
                        || num(&e["vel"]["x"]).abs() != 2048.0
                        || num(&e["vel"]["y"]) != 0.0
                    {
                        continue;
                    }
                    let (dx, dy) = delta(&e["pos"], &me["pos"]);
                    if !(8.0 < dx.abs()
                        && dx.abs() < 24.0
                        && (-12.0..=0.0).contains(&dy)
                        && dx * s.vx < 0.0
                        && dx * proposed as f64 > 0.0)
                    {
                        continue;
                    }
                    let depth = -num(&e["pos"]["y"]) / 4096.0;
                    let nav = &self.base.navigator;
                    if ![-8.0, 0.0, 8.0]
                        .iter()
                        .all(|offset| nav.occupied(s.x + offset, depth + 1.0))
                    {
                        continue;
                    }
                    let direction = if dx > 0.0 { -1 } else { 1 };
                    if !(0..97)
                        .step_by(4)
                        .all(|dist| nav.occupied(s.x + (direction * dist) as f64, depth + 1.0))
                    {
                        continue;
                    }
                    active = Some(
                        json!({"start":frame,"end":frame+30,"guid":e["actorGuid"],"direction":direction}),
                    );
                    break;
                }
            }
        }
        if let Some(plan) = &active {
            held = buttons(int(&plan["direction"])) | 2048;
            self.base.trace["held"] = json!(held);
            self.base.trace["ground_away_plan"] = plan.clone();
        }
        self.ground_away_plan = active;
        held
    }
}
