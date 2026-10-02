//! Base routing/combat for the development profile. The frozen opponents retain
//! their own implementation so fixes here cannot silently change difficulty.
use crate::navigation::{GrassNavigator, Waypoint};
use crate::observation::*;
use serde_json::{json, Value};
use std::collections::HashMap;

#[derive(Clone)]
pub struct Target {
    pub score: f64,
    pub kind: String,
    pub pos: Value,
}
#[derive(Clone)]
struct Runup {
    back: f64,
    takeoff: f64,
    toward: i64,
    go: bool,
    expires: i64,
    kind: String,
}
pub struct DevelopmentBase {
    pub player: usize,
    pub period: i64,

    pub previous_frame: i64,
    pub jump_until: i64,
    pub next_jump: i64,
    pub box_retry: HashMap<(i64, i64), i64>,
    pub trace: Value,
    pub navigator: GrassNavigator,
    pub waypoint: Option<Waypoint>,
    pub last_route: i64,
    previous_goal: Option<(String, i64, i64)>,
    previous_x: Option<f64>,
    pub stuck_frames: i64,
    escape_until: i64,
    wall_depart_until: i64,
    wall_depart_direction: i64,
    runup: Option<Runup>,
    ascent_attempts: HashMap<Option<(i64, i64)>, (i64, i64)>,
    target_intent: Option<(String, Value)>,
    item_escape_direction: i64,
    item_escape_until: i64,
    ledge_return: i64,
    pub landing_support: Option<Value>,
}
impl DevelopmentBase {
    pub fn new(player: usize, period: i64) -> Result<Self, String> {
        if player > 1 || period < 24 {
            return Err("Invalid player or fire period".into());
        }
        Ok(Self {
            player,
            period,
            previous_frame: -1,
            jump_until: -1,
            next_jump: 0,
            box_retry: HashMap::new(),
            trace: json!({}),
            navigator: GrassNavigator::default(),
            waypoint: None,
            last_route: -100,
            previous_goal: None,
            previous_x: None,
            stuck_frames: 0,
            escape_until: -1,
            wall_depart_until: -1,
            wall_depart_direction: 0,
            runup: None,
            ascent_attempts: HashMap::new(),
            target_intent: None,
            item_escape_direction: 0,
            item_escape_until: -1,
            ledge_return: 0,
            landing_support: None,
        })
    }
    pub fn reset(&mut self) {
        *self = Self::new(self.player, self.period).expect("validated controller");
    }
    pub fn item_kind(&self, e: &Value) -> i64 {
        let kinds = array(&e["kindByPlayer"]);
        if kinds.len() > self.player && int(&kinds[self.player]["confidence"]) >= 2 {
            return int(&kinds[self.player]["kind"]);
        }
        if int(&e["objectId"]) == 31 && int(&e["settings"]) == 0x108b {
            return 4;
        }
        if int(&e["objectId"]) == 31 && int(&e["settings"]) == 0x1099 {
            3
        } else {
            0
        }
    }
    pub fn is_equipment_item(&self, e: &Value) -> bool {
        int(&e["objectId"]) == 31
            && (matches!(int(&e["settings"]), 0x80000 | 0x90000)
                || matches!(self.item_kind(e), 1 | 2))
    }
    pub fn needs_equipment(raw: &Value) -> bool {
        matches!(int(&raw["currentPowerupRaw"]), 0 | 1 | 4)
    }
    pub fn targets(&mut self, d: &Value) -> Vec<Target> {
        let obs = &d["observation"];
        let me = &obs["players"][self.player];
        let other = &obs["players"][self.player ^ 1];
        let raw = &d["runtimePlayers"][self.player];
        let position = &me["pos"];
        let mut choices = vec![];
        let add = |choices: &mut Vec<Target>, kind: &str, pos: &Value, bonus: f64| {
            let (dx, dy) = delta(pos, position);
            choices.push(Target {
                score: dx.abs() + dy.abs() * 1.5 - bonus,
                kind: kind.into(),
                pos: pos.clone(),
            });
        };
        let natural = &obs["targets"]["bigStarActor"];
        if flag(&natural["found"]) {
            add(&mut choices, "natural_star", &natural["pos"], 0.0);
        }
        for e in array(&obs["entities"]) {
            if text(&e["category"]) == "dropped_star_item" {
                add(&mut choices, "dropped_star", &e["pos"], 80.0);
            }
            if Self::needs_equipment(raw) && self.is_equipment_item(e) {
                add(&mut choices, "powerup", &e["pos"], 120.0);
            }
        }
        if Self::needs_equipment(raw) {
            for c in array(&me["terrain"]["cells"]) {
                if !flag(&c["found"]) || int(&c["status"]) != 0 || int(&c["behavior"]) != 0x50000 {
                    continue;
                }
                let bx = ((num(&position["x"]) / 65536.0).floor() as i64 + int(&c["rx"])) * 65536
                    + 32768;
                let by = -((-num(&position["y"]) / 65536.0).floor() as i64 + int(&c["ry"])) * 65536
                    - 32768;
                let pos = json!({"x":bx,"y":by});
                let key = (bx.rem_euclid(1024 * 4096), by);
                for e in array(&obs["entities"]) {
                    if self.is_equipment_item(e) {
                        let (ex, ey) = delta(&e["pos"], &pos);
                        if ex.abs() < 20.0 && (-8.0..=48.0).contains(&ey) {
                            self.box_retry.insert(key, self.previous_frame + 300);
                        }
                    }
                }
                if self.previous_frame >= *self.box_retry.get(&key).unwrap_or(&-1) {
                    add(&mut choices, "powerup_box", &pos, 80.0);
                }
            }
        }
        if flag(&other["found"]) && !flag(&other["dead"]) && int(&other["battleStars"]) != 0 {
            add(
                &mut choices,
                "carrier",
                &other["pos"],
                if int(&me["battleStars"]) < int(&other["battleStars"]) {
                    20.0
                } else {
                    -100.0
                },
            );
        }
        for e in array(&obs["entities"]) {
            if text(&e["category"]) != "dropped_star_item"
                && int(&e["objectId"]) == 34
                && matches!(int(&e["settings"]) & 0x7fffffff, 0x2102 | 0x3102 | 3)
            {
                add(&mut choices, "dropped_star", &e["pos"], 80.0);
            }
        }
        choices.sort_by(|a, b| a.score.total_cmp(&b.score));
        choices
    }
    pub fn act(&mut self, d: &Value, frame: i64, previous: i64) -> i64 {
        if frame <= self.previous_frame {
            self.reset();
        }
        let elapsed = (frame - self.previous_frame).max(1);
        self.previous_frame = frame;
        let obs = &d["observation"];
        let me = &obs["players"][self.player];
        let other = &obs["players"][self.player ^ 1];
        let raw = &d["runtimePlayers"][self.player];
        self.trace = json!({"frame":frame,"player":self.player,"target":null,"held":0});
        if !flag(&me["found"]) || flag(&me["dead"]) || !flag(&raw["found"]) {
            self.waypoint = None;
            self.previous_x = None;
            self.target_intent = None;
            return 0;
        }
        let x = num(&me["pos"]["x"]) / 4096.0;
        let depth = -num(&me["pos"]["y"]) / 4096.0;
        let vx = num(&me["vel"]["x"]) / 4096.0;
        let power = int(&raw["currentPowerupRaw"]);
        let flags = int(&raw["physicsFlagRaw"]);
        let collision = int(&raw["collisionFlagRaw"]);
        let action = int(&raw["actionFlagRaw"]);
        let grounded = collision & 32769 != 0 && action & 4 == 0;
        if frame - self.last_route >= 24 {
            self.navigator.update(me);
        }
        if flags & 1 != 0 {
            self.stuck_frames = 0;
            self.escape_until = -1;
        } else if self.previous_x.is_some_and(|old| wrap(x, old).abs() < 0.5) && previous & 48 != 0
        {
            self.stuck_frames += elapsed;
        } else {
            self.stuck_frames = 0;
        }
        self.previous_x = Some(x);
        let mut choices = self.targets(d);
        if Self::needs_equipment(raw) {
            for &(bx, by) in &self.navigator.boxes {
                if !self.navigator.box_active[&(bx, by)] {
                    continue;
                }
                let pos = json!({"x":(bx*16+8)*4096,"y":-(by*16+8)*4096});
                let key = (int(&pos["x"]).rem_euclid(1024 * 4096), int(&pos["y"]));
                if frame >= *self.box_retry.get(&key).unwrap_or(&-1) {
                    let (tx, ty) = delta(&pos, &me["pos"]);
                    choices.push(Target {
                        score: tx.abs() + ty.abs() * 1.5 - 60.0,
                        kind: "powerup_box".into(),
                        pos,
                    });
                }
            }
        }
        let stars = int(&me["battleStars"]);
        let other_stars = int(&other["battleStars"]);
        if stars > other_stars
            && matches!(power, 0 | 1 | 4)
            && flags & 32 == 0
            && choices.iter().any(|c| c.kind != "carrier")
        {
            choices.retain(|c| c.kind != "carrier");
        }
        let (mut kind, mut pos) =
            if let Some(best) = choices.iter().min_by(|a, b| a.score.total_cmp(&b.score)) {
                let mut selected = best;
                {
                    if let Some((old_kind, old_pos)) = &self.target_intent {
                        if let Some(old) = choices
                            .iter()
                            .filter(|c| {
                                let (dx, dy) = delta(&c.pos, old_pos);
                                c.kind == *old_kind && dx.abs() < 8.0 && dy.abs() < 8.0
                            })
                            .min_by(|a, b| a.score.total_cmp(&b.score))
                        {
                            if old.score <= selected.score + 64.0 {
                                selected = old;
                            }
                        }
                    }
                    self.target_intent = Some((selected.kind.clone(), selected.pos.clone()));
                }
                (selected.kind.clone(), selected.pos.clone())
            } else {
                self.target_intent = None;
                if power != 2 {
                    (
                        "patrol".into(),
                        json!({"x":(x+160.0).rem_euclid(1024.0)*4096.0,"y":-272*4096}),
                    )
                } else {
                    ("wait".into(), me["pos"].clone())
                }
            };
        let (mut tx, mut ty) = delta(&pos, &me["pos"]);
        let (ex, ey) = if flag(&other["found"]) {
            delta(&other["pos"], &me["pos"])
        } else {
            (1024.0, 0.0)
        };
        let enemy_raw = &d["runtimePlayers"][self.player ^ 1];
        let dangerous = flag(&other["found"])
            && !flag(&other["dead"])
            && (int(&enemy_raw["currentPowerupRaw"]) == 3
                || int(&enemy_raw["physicsFlagRaw"]) & 32 != 0);
        let contact_advantage = (power == 3 || flags & 32 != 0) && !dangerous;
        if dangerous && ex.abs() < 160.0 && flags & 32 == 0 && power != 3 {
            kind = "escape_invincible".into();
            pos = json!({"x":num(&me["pos"]["x"])+if ex>0.0 {-144.0*4096.0}else{144.0*4096.0},"y":me["pos"]["y"]});
            (tx, ty) = delta(&pos, &me["pos"]);
        }
        if kind == "carrier"
            && !contact_advantage
            && stars > other_stars
            && ex.abs() < 144.0
            && ey.abs() < 48.0
        {
            kind = "protect_lead".into();
            pos = json!({"x":num(&me["pos"]["x"])+if ex>0.0 {-112.0*4096.0}else{112.0*4096.0},"y":me["pos"]["y"]});
            (tx, ty) = delta(&pos, &me["pos"]);
        }
        let goal_x = num(&pos["x"]) / 4096.0;
        let goal_depth = -num(&pos["y"]) / 4096.0 + if kind == "powerup_box" { 56.0 } else { 0.0 };
        let goal = (
            kind.clone(),
            (goal_x / 24.0).round_ties_even() as i64,
            (goal_depth / 16.0).round_ties_even() as i64,
        );
        if !grounded && self.previous_goal.as_ref() != Some(&goal) {
            self.waypoint =
                self.navigator
                    .route(x, depth, goal_x, goal_depth, kind == "powerup_box");
            self.previous_goal = Some(goal.clone());
        }
        if grounded && (frame - self.last_route >= 24 || self.previous_goal.as_ref() != Some(&goal))
        {
            self.navigator.update(me);
            self.waypoint =
                self.navigator
                    .route(x, depth, goal_x, goal_depth, kind == "powerup_box");
            self.last_route = frame;
            self.previous_goal = Some(goal);
        }
        let nav = self.waypoint.as_ref();
        let mut move_dx = nav.map_or(tx, |n| wrap(n.x, x));
        if tx.abs() < 36.0 && kind != "powerup_box" && (nav.is_none() || ty.abs() < 24.0) {
            move_dx = tx;
        }
        if kind == "powerup_box" && tx.abs() < 36.0 && ty > 30.0 {
            move_dx = tx;
        }
        let changing_surface = nav.is_some_and(|n| (n.depth - depth).abs() > 8.0);
        let mut direction = direction(move_dx, if changing_surface { 1.0 } else { 5.0 });
        if !changing_surface && move_dx.abs() < 9.0_f64.max(vx.abs() * 9.0) && vx.abs() > 0.65 {
            direction = if vx > 0.0 { -1 } else { 1 };
        }
        let landing_brake = !grounded
            && nav.is_some_and(|n| depth < n.depth && self.navigator.occupied(n.x, n.depth + 8.0))
            && move_dx * vx > 0.0
            && vx.abs() > 0.65
            && move_dx.abs() < vx * vx / 0.14 + 3.0 * vx.abs();
        if landing_brake {
            direction = if vx > 0.0 { -1 } else { 1 };
        }
        if kind == "carrier" && power == 2 && 56.0 < ex.abs() && ex.abs() < 104.0 && ey.abs() < 32.0
        {
            direction = 0;
        }
        let route_jump =
            nav.is_some_and(|n| n.jump && (move_dx.abs() > 12.0 || depth - n.depth > 8.0));
        let obstacle = direction != 0
            && [16.0, 32.0]
                .iter()
                .any(|dist| solid(terrain_cell(me, direction as f64 * dist, -16.0)));
        let gap = direction != 0
            && [8.0, 24.0, 40.0].iter().all(|dep| {
                let c = terrain_cell(me, direction as f64 * 32.0, *dep);
                c.is_some() && !solid(c)
            });
        let mut jump = route_jump || obstacle || gap || (ty > 20.0 && tx.abs() < 40.0);
        if nav.is_some_and(|n| !obstacle && self.navigator.can_walk_down(x, depth, n.x, n.depth)) {
            jump = false;
            if grounded {
                self.jump_until = frame;
            }
        }
        if kind == "powerup_box" && tx.abs() < 40.0 {
            jump = tx.abs() < 10.0 && ty > 24.0;
        }
        let stomp_attempt = kind == "carrier"
            && !dangerous
            && !matches!(power, 2 | 3)
            && 24.0 < ex.abs()
            && ex.abs() < 80.0
            && -12.0 < ey
            && ey < 24.0
            && direction as f64 * ex > 0.0;
        let close_stomp = kind == "carrier"
            && !dangerous
            && grounded
            && !matches!(power, 2 | 3)
            && ex.abs() <= 24.0
            && ey.abs() < 12.0;
        if close_stomp {
            direction = 0;
            jump = true;
        }
        if stomp_attempt {
            jump = true;
        }
        let high_ascent = nav.is_some_and(|n| {
            n.jump && n.path.first().is_some_and(|p| p.1 as f64 - n.depth >= 56.0)
        });
        let ascent_key = if high_ascent {
            nav.map(|n| (n.x.rem_euclid(1024.0) as i64, n.depth as i64))
        } else {
            None
        };
        let (mut attempts, last_attempt) =
            *self.ascent_attempts.get(&ascent_key).unwrap_or(&(0, -1000));
        if frame - last_attempt > 360 {
            attempts = 0;
        }
        if self
            .runup
            .as_ref()
            .is_some_and(|r| !grounded || frame > r.expires || kind != r.kind)
        {
            self.runup = None;
        }
        if self.runup.is_none()
            && grounded
            && high_ascent
            && attempts >= 2
            && vx.abs() < 1.25
            && 24.0 < move_dx.abs()
            && move_dx.abs() < 64.0
            && kind != "powerup_box"
        {
            let toward = if move_dx > 0.0 { 1 } else { -1 };
            let back = x - toward as f64 * 24.0;
            if [-8.0, 0.0, 8.0]
                .iter()
                .all(|s| self.navigator.occupied(back + s, depth + 8.0))
            {
                self.runup = Some(Runup {
                    back,
                    takeoff: x,
                    toward,
                    go: false,
                    expires: frame + 180,
                    kind: kind.clone(),
                });
                self.ascent_attempts.insert(ascent_key, (0, frame));
            }
        }
        let runup_active = self.runup.is_some();
        if let Some(r) = self.runup.as_mut() {
            if !r.go {
                let offset = wrap(r.back, x);
                direction = crate::observation::direction(offset, 4.0);
                if offset.abs() < 8.0_f64.max(vx.abs() * 9.0) && vx.abs() > 0.35 {
                    direction = if vx > 0.0 { -1 } else { 1 };
                }
                if offset.abs() < 8.0 && vx.abs() < 0.4 {
                    r.go = true;
                }
                jump = false;
            }
            if r.go {
                direction = r.toward;
                jump =
                    wrap(r.takeoff, x) * (direction as f64) < 4.0 && vx * direction as f64 > 1.25;
            }
        }
        let intentional_drop = matches!(kind.as_str(), "natural_star" | "dropped_star" | "powerup")
            && ty < -24.0
            && tx.abs() < 48.0;
        if intentional_drop {
            jump = false;
        }
        let mut danger = false;
        let mut landing_avoid = 0;
        let mut landing_avoid_causes = vec![];
        let mut item_avoid = 0;
        let mut body_avoid = 0;
        let body_jump = flag(&other["found"])
            && !flag(&other["dead"])
            && stars > 0
            && flags & 32 == 0
            && power != 3
            && ey.abs() < 24.0
            && ex.abs() < 64.0
            && direction as f64 * ex > 0.0;
        if body_jump && grounded {
            if ex.abs() > 24.0 {
                jump = true;
            } else {
                body_avoid = if ex >= 0.0 { -1 } else { 1 };
            }
        }
        let mut body_evade = false;
        if flag(&other["found"])
            && !flag(&other["dead"])
            && stars > 0
            && flags & 32 == 0
            && power != 3
            && ex.abs() < 48.0
            && ey.abs() < 32.0
        {
            let rv = num(&other["vel"]["x"]) / 4096.0 - vx;
            body_evade = ex * rv < 0.0 && (ex + rv * 8.0).abs() < 24.0;
            if body_evade {
                body_avoid = if ex >= 0.0 { -1 } else { 1 };
            }
        }
        let overhead_evade = grounded
            && !contact_advantage
            && stars > 0
            && flag(&other["found"])
            && !flag(&other["dead"])
            && 24.0 < ey
            && ey < 72.0
            && ex.abs() < 32.0
            && num(&other["vel"]["y"]) < 0.0;
        if overhead_evade {
            let fall = (-num(&other["vel"]["y"]) / 4096.0).max(0.0);
            let time = ((fall * fall + 2.0 * 0.3125 * (ey - 24.0).max(0.0)).sqrt() - fall) / 0.3125;
            let dx = ex + (num(&other["vel"]["x"]) / 4096.0 - vx) * time.min(24.0);
            body_avoid = if dx >= 0.0 { -1 } else { 1 };
            body_evade = true;
        }
        for e in array(&obs["entities"]) {
            let category = text(&e["category"]);
            let (hx, hy) = delta(&e["pos"], &me["pos"]);
            if power == 2
                && matches!(self.item_kind(e), 3 | 4)
                && hx.abs() < 48.0
                && -8.0 < hy
                && hy < 80.0
            {
                if hy < 8.0 && direction as f64 * hx > 0.0 {
                    jump = true;
                } else {
                    if frame >= self.item_escape_until {
                        self.item_escape_direction = if hx >= 0.0 { -1 } else { 1 };
                        self.item_escape_until = frame + 60;
                    }
                    item_avoid = self.item_escape_direction;
                }
            }
            if power == 3 || flags & 32 != 0 {
                continue;
            }
            if !matches!(category, "enemy_goomba" | "enemy_koopa" | "player_fireball") {
                continue;
            }
            if category == "player_fireball"
                && (int(&e["owner"]) == self.player as i64 || !flag(&e["ownerVerified"]))
            {
                continue;
            }
            if hx.abs() < 48.0
                && -8.0 < hy
                && hy < 32.0
                && (hx * direction as f64 > 0.0 || category == "player_fireball")
            {
                jump = true;
                danger = true;
            }
            if matches!(category, "enemy_goomba" | "enemy_koopa")
                && !grounded
                && num(&me["vel"]["y"]) < 0.0
                && -96.0 < hy
                && hy < -12.0
            {
                let time = -hy / 1.0_f64.max(-num(&me["vel"]["y"]) / 4096.0);
                let future = hx + (num(&e["vel"]["x"]) / 4096.0 - vx) * time;
                if time < 24.0 && future.abs() < 28.0 {
                    landing_avoid = if hx > 0.0 { -1 } else { 1 };
                    landing_avoid_causes.push(json!({"guid":e["actorGuid"],"category":category,"dx":hx,"dy":hy,"vx":num(&e["vel"]["x"])/4096.0,"vy":num(&e["vel"]["y"])/4096.0,"shell_mode":e["koopaShellModeRaw"],"time_to_level":time,"projected_dx":future,"direction":landing_avoid}));
                }
            }
        }
        if self.stuck_frames > 90 {
            self.escape_until = frame + 30;
            self.stuck_frames = 0;
        }
        if frame < self.escape_until {
            direction = if tx > 0.0 { -1 } else { 1 };
            jump = true;
        }
        let wall_depth = -4.0;
        let left = (8..65)
            .step_by(8)
            .find(|&n| solid(terrain_cell(me, -(n as f64), wall_depth)));
        let right = (8..65)
            .step_by(8)
            .find(|&n| solid(terrain_cell(me, n as f64, wall_depth)));
        let in_shaft = left.zip(right).is_some_and(|(l, r)| l + r <= 48)
            && !self.navigator.occupied(x, depth + 8.0);
        let recovery = !grounded && (depth > 272.0 || in_shaft) && !intentional_drop;
        if recovery {
            direction = if left.is_some_and(|l| right.is_none_or(|r| l <= r)) {
                -1
            } else {
                1
            };
            if frame < self.wall_depart_until {
                direction = self.wall_depart_direction;
            }
        }
        if action & 4 != 0 && !intentional_drop {
            if previous & 2 == 0 {
                jump = true;
                direction = if collision & 0x408 != 0 { -1 } else { 1 };
                self.wall_depart_direction = -direction;
                self.wall_depart_until = frame + 12;
                self.jump_until = frame + 18;
                self.next_jump = frame + 12;
            } else {
                self.jump_until = frame;
            }
        }
        if self.runup.is_some() && !jump {
            self.jump_until = frame;
        }
        if jump && frame >= self.next_jump && (grounded || action & 4 != 0) {
            if grounded && high_ascent && self.runup.is_none() {
                self.ascent_attempts
                    .insert(ascent_key, (attempts + 1, frame));
            }
            self.jump_until = frame + 30;
            self.next_jump = frame + 42;
        }
        let mut ledge_reset = 0;
        let unsupported_ledge =
            grounded && route_jump && !intentional_drop && !self.navigator.occupied(x, depth + 8.0);
        if !unsupported_ledge {
            self.ledge_return = 0;
        }
        if unsupported_ledge && (self.ledge_return != 0 || (vx.abs() < 0.5 && previous & 2 != 0)) {
            let supports: Vec<_> = [8, 16, 24]
                .into_iter()
                .flat_map(|offset| [-1, 1].into_iter().map(move |sign| (offset, sign)))
                .filter(|&(offset, sign)| {
                    self.navigator
                        .occupied(x + (sign * offset) as f64, depth + 8.0)
                })
                .collect();
            if let Some(&(_, sign)) = supports.iter().min() {
                ledge_reset = if self.ledge_return != 0 {
                    self.ledge_return
                } else {
                    sign
                };
                self.ledge_return = ledge_reset;
                direction = ledge_reset;
                self.jump_until = frame;
                self.next_jump = frame + 6;
            }
        }
        let mut held = buttons(direction) | if frame < self.jump_until { 2 } else { 0 };
        if landing_avoid != 0 && !recovery {
            held = (held & !(48 | 2)) | buttons(landing_avoid);
            self.jump_until = frame;
        }
        if item_avoid != 0 && !recovery {
            held = (held & !(48 | 2)) | buttons(item_avoid);
            self.jump_until = frame;
        }
        if body_avoid != 0 && !recovery {
            held = (held & !48) | buttons(body_avoid);
        }
        let ascent_run = high_ascent && !recovery;
        if direction != 0
            && (move_dx.abs() > 48.0 || ascent_run)
            && kind != "powerup_box"
            && frame % self.period >= 6
        {
            held |= 2048;
        }
        if let Some(r) = &self.runup {
            held = (held & !2048) | if r.go { 2048 } else { 0 };
        }
        if body_evade && !recovery {
            held |= 2048;
        }
        let can_shoot = power == 2
            && flag(&other["found"])
            && !flag(&other["dead"])
            && ex.abs() < 240.0
            && -96.0 < ey
            && ey < 24.0;
        if can_shoot && frame % self.period < 12 && !gap && !runup_active && body_avoid == 0 {
            if frame % self.period < 6 {
                held = (held & !(48 | 2048)) | if ex < 0.0 { 32 } else { 16 };
            } else if flag(&raw["facingKnown"])
                && int(&raw["facing"]) == if ex < 0.0 { -1 } else { 1 }
            {
                held |= 2048;
            }
        }
        if ledge_reset != 0 {
            held = (held & !(48 | 2 | 2048)) | buttons(ledge_reset);
        }
        if matches!(int(&raw["inventoryPowerupRaw"]), 1 | 2)
            && matches!(power, 0 | 1 | 4)
            && frame % 120 < 6
        {
            held |= 1024;
        }
        self.trace = json!({"frame":frame,"player":self.player,"target":kind,"dx":tx,"dy":ty,"nav_dx":move_dx,"waypoint":nav,"gap":gap,"danger":danger,"grounded":grounded,"held":held,"stuck":self.stuck_frames,"recovery":recovery,"landing_avoid":landing_avoid,"item_avoid":item_avoid,"stomp_attempt":stomp_attempt});
        {
            let extras = json!({"in_shaft":in_shaft,"close_stomp":close_stomp,"ascent_run":ascent_run,"landing_brake":landing_brake,"overhead_evade":overhead_evade,"body_jump":body_jump,"body_avoid":body_avoid,"body_evade":body_evade,"ascent_retries":attempts,"runup":self.runup.as_ref().map(|r|json!({"back":r.back,"takeoff":r.takeoff,"toward":r.toward,"phase":if r.go {"go"}else{"back"},"expires":r.expires,"kind":r.kind}))});
            self.trace
                .as_object_mut()
                .unwrap()
                .extend(extras.as_object().unwrap().clone());
        }
        self.trace["ledge_reset"] = json!(ledge_reset);
        self.trace["contact_advantage"] = json!(contact_advantage);
        if !landing_avoid_causes.is_empty() {
            self.trace["landing_avoid_causes"] = json!(landing_avoid_causes);
        }
        let precision = recovery
            || ledge_reset != 0
            || landing_brake
            || runup_active
            || close_stomp
            || (kind == "powerup_box" && tx.abs() < 16.0);
        let shoot_release = can_shoot && frame % self.period < 6;
        let run_default = held & 48 != 0
            && held & 2048 == 0
            && matches!(power, 0..=2)
            && !precision
            && !shoot_release;
        if run_default {
            held |= 2048;
        }
        self.trace["run_default"] = json!(run_default);
        self.trace["run_precision"] = json!(precision);
        self.trace["held"] = json!(held);
        let eligible = matches!(power, 0..=2)
            && !flag(&me["dead"])
            && raw["damageStateRaw"].as_i64() == Some(0)
            && raw["updateLockedRaw"].as_i64() == Some(0)
            && matches!(flags, 0 | 2 | 130 | 2050)
            && matches!(action, 0 | 0x100000)
            && !intentional_drop
            && !recovery;
        let nearby = array(&obs["entities"]).iter().any(|e| {
            matches!(
                text(&e["category"]),
                "enemy_goomba" | "enemy_koopa" | "player_fireball"
            ) && delta(&e["pos"], &me["pos"]).0.abs() < 96.0
        });
        let opponent_near =
            flag(&other["found"]) && !flag(&other["dead"]) && ex.abs() < 48.0 && ey.abs() < 64.0;
        if !eligible
            || nearby
            || opponent_near
            || self
                .landing_support
                .as_ref()
                .is_some_and(|p| frame >= int(&p["until"]))
        {
            self.landing_support = None;
        }
        if eligible && !nearby && self.landing_support.is_none() && !opponent_near {
            let s = crate::physics::Motion {
                x,
                depth,
                vx,
                vy: num(&me["vel"]["y"]) / 4096.0,
                grounded,
                facing: int(&raw["facing"]),
                turn_remaining: 0,
                edge_remaining: 0,
            };
            if let Some(plan) = crate::planners::landing_support(
                crate::planners::PlannerState {
                    motion: s,
                    height: if power == 0 { 16.0 } else { 27.0 },
                },
                held,
                previous,
                d["time"]["inputDelay"].as_i64().unwrap_or(2),
                &|x, y| self.navigator.occupied(x, y),
                &[],
                24,
            ) {
                self.landing_support =
                    Some(json!({"held":plan["held"],"until":frame+24,"forecast":plan}));
            }
        }
        if let Some(plan) = &self.landing_support {
            held = int(&plan["held"]);
            self.jump_until = frame;
        }
        self.trace["landing_support"] = json!(self.landing_support);
        self.trace["held"] = json!(held);
        held
    }
}
