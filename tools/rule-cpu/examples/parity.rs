//! Development-only JSON-lines adapter for comparison with the Python oracle.
use bigstar_rule_cpu::development_base::DevelopmentBase;
use bigstar_rule_cpu::{
    actors,
    frozen::FrozenRule,
    navigation::GrassNavigator,
    observation::*,
    physics::*,
    planners::{self, PlannerState},
};
use serde_json::{json, Value};
use std::io::{self, BufRead, Write};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut rule = FrozenRule::new(1, 60, false)?;
    let mut development_base = DevelopmentBase::new(1, 60)?;
    let mut use_development_base = false;
    let mut out = io::BufWriter::new(io::stdout().lock());
    for line in io::stdin().lock().lines() {
        let request: Value = serde_json::from_str(&line?)?;
        let response = match text(&request["op"]) {
            "reset" => {
                use_development_base = flag(&request["development_base"]);
                development_base = DevelopmentBase::new(
                    int(&request["player"]) as usize,
                    int(&request["period"]),
                )?;
                rule = FrozenRule::new(
                    int(&request["player"]) as usize,
                    int(&request["period"]),
                    flag(&request["combat"]),
                )?;
                json!(true)
            }
            "act" => {
                if use_development_base {
                    let held = development_base.act(
                        &request["decision"],
                        int(&request["frame"]),
                        int(&request["previousHeld"]),
                    );
                    serde_json::to_writer(
                        &mut out,
                        &json!({"held":held,"trace":development_base.trace}),
                    )?;
                    writeln!(&mut out)?;
                    out.flush()?;
                    continue;
                }
                let held = rule.act(
                    &request["decision"],
                    int(&request["frame"]),
                    int(&request["previousHeld"]),
                );
                json!({"held":held,"trace":rule.trace})
            }
            "forecast" => {
                let mut nav = GrassNavigator::default();
                if !request["player"].is_null() {
                    nav.update(&request["player"]);
                }
                let s: Motion = serde_json::from_value(request["initial"].clone())?;
                let inputs: Vec<i64> = serde_json::from_value(request["inputs"].clone())?;
                let options = ForecastOptions {
                    height: request["height"].as_f64().unwrap_or(16.0),
                    counter_landing_turn: flag(&request["counter_landing_turn"]),
                };
                let (points, end) = forecast_player(
                    s,
                    int(&request["previous"]),
                    &inputs,
                    &|x, y| nav.occupied(x, y),
                    options,
                );
                json!([points, end])
            }
            "plan" => {
                let mut nav = GrassNavigator::default();
                nav.update(&request["player"]);
                let occupied = |x, y| nav.occupied(x, y);
                let motion: Motion = serde_json::from_value(request["initial"].clone())?;
                let height = num(&request["height"]);
                let initial = PlannerState { motion, height };
                let proposed = int(&request["proposed"]);
                let previous = int(&request["previous"]);
                let delay = int(&request["delay"]);
                match text(&request["name"]) {
                    "landing_support" => planners::landing_support(
                        initial,
                        proposed,
                        previous,
                        delay,
                        &occupied,
                        &[],
                        24,
                    ),
                    "air_landing" => planners::air_landing(
                        motion, height, proposed, previous, delay, &occupied, 48,
                    ),
                    "launch_delay" => planners::launch_delay(
                        initial,
                        previous,
                        delay,
                        (num(&request["goal"]["x"]), num(&request["goal"]["depth"])),
                        &occupied,
                        &[],
                        96,
                    ),
                    "ground_interception" | "ascent_interception" => planners::interception(
                        motion,
                        previous,
                        delay,
                        &request["entity"],
                        &occupied,
                        &[],
                        text(&request["name"]) == "ascent_interception",
                    ),
                    _ => return Err("Unknown planner".into()),
                }
                .unwrap_or(Value::Null)
            }
            "actor" => {
                let mut nav = GrassNavigator::default();
                nav.update(&request["player"]);
                let occupied = |x, y| nav.occupied(x, y);
                let e = &request["entity"];
                match text(&request["name"]) {
                    "goomba" => {
                        let (p, end) = actors::ground_goomba(
                            num(&e["pos"]["x"]) / 4096.0,
                            -num(&e["pos"]["y"]) / 4096.0,
                            num(&e["vel"]["x"]) / 4096.0,
                            &occupied,
                            96,
                            int(&request["pause"]),
                        );
                        json!([p, end])
                    }
                    "koopa" => {
                        let (p, end) = actors::ground_koopa(e, &occupied, 96);
                        json!([p, end])
                    }
                    "emerging" => json!(actors::emerging(e, &occupied, 160)),
                    _ => return Err("Unknown actor forecast".into()),
                }
            }
            _ => return Err("Unknown parity operation".into()),
        };
        serde_json::to_writer(&mut out, &response)?;
        writeln!(&mut out)?;
        out.flush()?;
    }
    Ok(())
}
