use super::*;
use crate::{
    actors,
    development::{alive, hazard, motion, near},
    physics::{forecast_player, ForecastOptions},
    planners::sequence,
};
impl DevelopmentBase {
    pub(super) fn filter_pit_targets(&mut self, d: &Value, choices: &mut Vec<Target>) {
        let obs = &d["observation"];
        let me = &obs["players"][self.player];
        let raw = &d["runtimePlayers"][self.player];
        let depth = -num(&me["pos"]["y"]) / 4096.0;
        let allowed = obs["stage"]["id"].as_i64() == Some(0)
            && obs["stage"]["vsMode"].as_i64() == Some(1)
            && !flag(&me["dead"])
            && depth > 272.0
            && !me["contact"]["tileGround"].as_bool().unwrap_or(true)
            && matches!(int(&raw["currentPowerupRaw"]), 0..=2)
            && raw["damageStateRaw"].as_i64() == Some(0)
            && raw["updateLockedRaw"].as_i64() == Some(0);
        if !allowed {
            self.unreachable_pit_active = false;
            return;
        }
        let excluded: Vec<_> = array(&obs["entities"])
            .iter()
            .filter(|e| {
                text(&e["category"]) == "dropped_star_item"
                    && num(&e["pos"]["y"]) < -360.0 * 4096.0
                    && num(&e["vel"]["y"]) < 0.0
                    && delta(&e["pos"], &me["pos"]).0.abs() < 48.0
            })
            .collect();
        if excluded.is_empty() {
            self.unreachable_pit_active = false;
            return;
        }
        if !self.unreachable_pit_active {
            if num(&me["vel"]["y"]) >= 0.0
                || int(&raw["actionFlagRaw"]) != 0x100000
                || int(&raw["behaviorFuncRaw"]) != 0x021135b8
                || !matches!(int(&raw["physicsFlagRaw"]), 0 | 2 | 128 | 130 | 2050)
            {
                return;
            }
            let other = &obs["players"][self.player ^ 1];
            if alive(other) && near(other, me, 48.0, Some(64.0)) {
                return;
            }
            for e in array(&obs["entities"]) {
                if !hazard(e) {
                    continue;
                }
                if text(&e["category"]) == "player_fireball"
                    && flag(&e["ownerVerified"])
                    && e["owner"].as_i64() == Some(self.player as i64)
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
                    return;
                }
            }
            self.unreachable_pit_navigator.update(me);
            let nav = &self.unreachable_pit_navigator;
            let delay = d["time"]["inputDelay"].as_i64().unwrap_or(2);
            if !(0..=6).contains(&delay) {
                return;
            }
            let mut trials = vec![];
            for held in [16, 32] {
                let (points, reason) = forecast_player(
                    motion(me, raw, false),
                    self.target_previous,
                    &sequence(self.target_previous, delay as usize, held, 36),
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
                if reason == "wall"
                    && points
                        .last()
                        .is_some_and(|p| p.state.depth + 4.0 * ((delay + 2) as f64) < 336.0)
                {
                    trials.push(json!({"held":held,"frames":points.len(),"depth":points.last().unwrap().state.depth}));
                }
            }
            if trials.is_empty() {
                return;
            }
            self.unreachable_pit_active = true;
            self.trace["unreachable_pit_wall_candidates"] = json!(trials);
        }
        self.trace["unreachable_pit_recovery"] =
            json!(excluded.iter().map(|e| &e["actorGuid"]).collect::<Vec<_>>());
        choices.retain(|c| {
            c.kind != "dropped_star"
                || !excluded
                    .iter()
                    .any(|e| e["pos"]["x"] == c.pos["x"] && e["pos"]["y"] == c.pos["y"])
        });
    }
    pub(super) fn filter_mushroom_targets(&mut self, d: &Value, choices: &mut Vec<Target>) {
        self.falling_mushroom_excluded.clear();
        let obs = &d["observation"];
        let me = &obs["players"][self.player];
        let entities: Vec<_> = array(&obs["entities"])
            .iter()
            .filter(|e| {
                self.is_equipment_item(e)
                    && e["vel"]["y"].as_i64() == Some(-16384)
                    && -num(&e["pos"]["y"]) / 4096.0 > 288.0
            })
            .collect();
        if entities.is_empty() {
            return;
        }
        self.falling_mushroom_navigator.update(me);
        let mut excluded = vec![];
        for e in entities {
            if let Some(reason) =
                falling_beyond_reach(d, self.player, e, &self.falling_mushroom_navigator)
            {
                excluded.push(e);
                self.falling_mushroom_excluded.push(reason);
            }
        }
        choices.retain(|c| {
            c.kind != "powerup"
                || !excluded
                    .iter()
                    .any(|e| e["pos"]["x"] == c.pos["x"] && e["pos"]["y"] == c.pos["y"])
        });
    }
}
fn falling_beyond_reach(
    d: &Value,
    player: usize,
    e: &Value,
    nav: &GrassNavigator,
) -> Option<Value> {
    let obs = &d["observation"];
    let me = &obs["players"][player];
    let raw = &d["runtimePlayers"][player];
    let depth = -num(&me["pos"]["y"]) / 4096.0;
    if obs["stage"]["id"].as_i64() != Some(0)
        || obs["stage"]["vsMode"].as_i64() != Some(1)
        || flag(&me["dead"])
        || raw["currentPowerupRaw"].as_i64() != Some(0)
        || !(0.0..=288.0).contains(&depth)
        || raw["damageStateRaw"].as_i64() != Some(0)
        || raw["updateLockedRaw"].as_i64() != Some(0)
        || !matches!(
            int(&raw["behaviorFuncRaw"]),
            0x02115aac | 0x021135b8 | 0x02114978
        )
        || int(&e["objectId"]) != 31
        || e["itemKindRaw"].as_i64() != Some(0)
        || int(&e["itemBehaviorFunctionRaw"]) != 0x020d3a30
        || e["itemRollingSuppressedRaw"].as_i64() != Some(0)
        || e["vel"]["y"].as_i64() != Some(-16384)
    {
        return None;
    }
    let item_depth = -num(&e["pos"]["y"]) / 4096.0;
    if item_depth <= 288.0 || delta(&e["pos"], &me["pos"]).0.abs() > 96.0 {
        return None;
    }
    let other = &obs["players"][player ^ 1];
    if alive(other) && near(other, e, 160.0, None) {
        return None;
    }
    let predicted = actors::item(
        num(&e["pos"]["x"]) / 4096.0,
        item_depth,
        num(&e["vel"]["x"]) / 4096.0,
        -4.0,
        &|x, y| nav.occupied(x, y),
        48,
    );
    let mut minimum = f64::INFINITY;
    for (frame, pdepth, vy) in std::iter::once((0, item_depth, -4.0))
        .chain(predicted.iter().map(|p| (p.frame, p.depth, p.vy)))
    {
        if vy != -4.0 {
            return None;
        }
        let best_foot = (depth + 6.0 * frame as f64).min(364.0);
        let gap = pdepth - 20.0 - best_foot;
        minimum = minimum.min(gap);
        if gap <= 0.0 {
            return None;
        }
        if pdepth > 384.0 {
            return Some(
                json!({"item_guid":e["actorGuid"],"item_depth":item_depth,"player_depth":depth,"frames":frame,"minimum_gap":minimum}),
            );
        }
    }
    None
}
