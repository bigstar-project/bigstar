//! Shared native controller for GUI matches and research replay.
use crate::{development::DevelopmentRule, frozen::FrozenRule, observation::*};
use serde_json::Value;
pub const PROFILES: [&str; 3] = ["beginner", "combat_v2", "development"];
pub enum Controller {
    Frozen(Box<FrozenRule>),
    Development(Box<DevelopmentRule>),
}
impl Controller {
    pub fn new(profile: &str, player: usize, period: i64) -> Result<Self, String> {
        if player > 1 || period <= 0 {
            return Err("Player must be 0 or 1 and period must be positive".into());
        }
        match profile {
            "beginner" | "combat_v2" => Ok(Self::Frozen(Box::new(FrozenRule::new(
                player,
                period,
                profile == "combat_v2",
            )?))),
            "development" => Ok(Self::Development(Box::new(DevelopmentRule::new(
                player, period,
            )?))),
            _ => Err(format!("Unknown CPU profile: {profile}")),
        }
    }
    pub fn reset(&mut self) {
        match self {
            Self::Frozen(r) => r.reset(),
            Self::Development(r) => r.reset(),
        }
    }
    pub fn act(&mut self, decision: &Value, frame: i64, previous: i64) -> i64 {
        match self {
            Self::Frozen(r) => r.act(decision, frame, previous),
            Self::Development(r) => r.act_through(decision, frame, previous, 32),
        }
    }
    pub fn trace(&self) -> &Value {
        match self {
            Self::Frozen(r) => &r.trace,
            Self::Development(r) => &r.base.trace,
        }
    }
}
pub struct Worker {
    pub controller: Controller,
    generation: Value,
    origin: i64,
    last: i64,
    next_decision: i64,
    held: i64,
    player: usize,
}
impl Worker {
    pub fn new(controller: Controller, player: usize) -> Self {
        Self {
            controller,
            generation: Value::Null,
            origin: 0,
            last: -1,
            next_decision: 0,
            held: 0,
            player,
        }
    }
    pub fn step(&mut self, request: &Value) -> Result<(i64, i64), String> {
        let d = &request["decision"];
        let time = &d["time"];
        let frame = time["rawFrame"].as_i64().ok_or("Missing rawFrame")?;
        if flag(&time["rollbackEnabled"]) {
            return Err("The CPU worker does not support rollback".into());
        }
        let previous = request["previousHeld"]
            .as_i64()
            .ok_or("Missing previousHeld")?;
        if self.generation != time["generation"] || frame <= self.last {
            self.controller.reset();
            self.generation = time["generation"].clone();
            self.origin = frame;
            self.next_decision = frame;
            self.held = 0;
        }
        self.last = frame;
        let obs = &d["observation"];
        if int(&obs["stage"]["group"]) != 9
            || obs["stage"]["id"].as_i64() != Some(0)
            || !flag(&obs["players"][self.player]["found"])
        {
            self.held = 0;
            self.next_decision = frame;
        } else if frame >= self.next_decision {
            self.held = self.controller.act(d, frame - self.origin, previous);
            self.next_decision = frame + 6;
        }
        Ok((frame, self.held))
    }
}
