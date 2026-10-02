#[derive(Clone, Copy)]
pub struct PlannerState {
    pub motion: Motion,
    pub height: f64,
}
use crate::{
    actors::{self, EnemyPoint},
    observation::*,
    physics::*,
};
use serde_json::{json, Value};

pub fn sequence(previous: i64, delay: usize, action: i64, horizon: usize) -> Vec<i64> {
    let mut v = vec![action; horizon];
    v[..delay].fill(previous);
    v
}
fn danger(
    points: &[Point],
    paths: &[Vec<EnemyPoint>],
    horizontal: f64,
    above: f64,
    height: f64,
) -> bool {
    points.iter().enumerate().any(|(i, p)| {
        paths.iter().any(|path| {
            path.get(i).is_none_or(|e| {
                wrap(p.state.x, e.x).abs() < horizontal
                    && e.depth - above < p.state.depth
                    && p.state.depth < e.depth + height
            })
        })
    })
}
pub fn landing_support(
    initial: PlannerState,
    proposed: i64,
    previous: i64,
    delay: i64,
    occupied: &impl Fn(f64, f64) -> bool,
    paths: &[Vec<EnemyPoint>],
    horizon: usize,
) -> Option<Value> {
    let PlannerState { motion: s, height } = initial;
    if s.grounded
        || !(232.0..=272.0).contains(&s.depth)
        || s.vy > -2.0
        || !(0..=6).contains(&delay)
        || occupied(s.x, 280.0)
    {
        return None;
    }
    let sides: Vec<_> = [-1, 1]
        .into_iter()
        .filter(|&side| {
            [8.0, 16.0]
                .iter()
                .any(|dist| occupied(s.x + side as f64 * dist, 280.0))
        })
        .collect();
    if sides.is_empty() || paths.iter().any(|p| p.len() < horizon) {
        return None;
    }
    let options = ForecastOptions {
        height,
        ..Default::default()
    };
    let simulate = |action| {
        forecast_player(
            s,
            previous,
            &sequence(previous, delay as usize, action, horizon),
            occupied,
            options,
        )
    };
    if !simulate(proposed).0.iter().any(|p| p.state.depth > 288.0) {
        return None;
    }
    let mut best: Option<(f64, i64, Value)> = None;
    for side in sides {
        for run in [0, 2048] {
            let held = buttons(side) | run;
            let (points, end) = simulate(held);
            if end != "horizon"
                || points.len() != horizon
                || !points[horizon - 6..]
                    .iter()
                    .all(|p| p.state.grounded && occupied(p.state.x, p.state.depth + 8.0))
                || points.iter().any(|p| p.state.depth > 288.0)
                || danger(&points, paths, 18.0, 14.0, height)
            {
                continue;
            }
            let last = &points.last()?.state;
            let travel = (last.x - s.x).abs();
            let key = (travel, held);
            if best.as_ref().is_none_or(|b| key < (b.0, b.1)) {
                best = Some((
                    travel,
                    held,
                    json!({"held":held,"expected_landing":points.iter().position(|p|p.state.grounded)?+1,"horizon":horizon,"final_x":last.x,"final_depth":last.depth}),
                ));
            }
        }
    }
    best.map(|p| p.2)
}
pub fn air_landing(
    s: Motion,
    height: f64,
    proposed: i64,
    previous: i64,
    delay: i64,
    occupied: &impl Fn(f64, f64) -> bool,
    horizon: usize,
) -> Option<Value> {
    if s.grounded || s.vy >= 0.0 || s.depth > 272.0 || !(0..=6).contains(&delay) {
        return None;
    }
    let simulate = |action, counter_landing_turn| {
        forecast_player(
            s,
            previous,
            &sequence(previous, delay as usize, action, horizon),
            occupied,
            ForecastOptions {
                height,
                counter_landing_turn,
            },
        )
    };
    let (nominal, nominal_end) = simulate(proposed, false);
    if nominal.iter().any(|p| p.state.grounded) {
        return None;
    }
    let mut best: Option<((usize, u32, i64), Value)> = None;
    for held in [0, 16, 32, 2064, 2080] {
        let (points, end) = simulate(held, false);
        let (conservative, ce) = simulate(held, true);
        if [&points, &conservative]
            .iter()
            .zip([end, ce])
            .any(|(p, e)| {
                e != "horizon"
                    || p.len() != horizon
                    || p.iter().any(|p| p.state.depth > 288.0)
                    || !p[horizon - 6..].iter().all(|p| p.state.grounded)
            })
        {
            continue;
        }
        let arrival = points.iter().position(|p| p.state.grounded)? + 1;
        let mut stable = 0;
        let mut settle = None;
        for (i, (p, q)) in points.iter().zip(&conservative).enumerate() {
            let supported = [p, q].iter().all(|p| {
                p.state.grounded
                    && [-8.0, 0.0, 8.0]
                        .iter()
                        .all(|offset| occupied(p.state.x + offset, p.state.depth + 1.0))
            });
            stable = if supported { stable + 1 } else { 0 };
            if stable >= 6 {
                settle = Some(i + 1);
                break;
            }
        }
        let Some(settle) = settle else { continue };
        let key = (arrival, (held ^ proposed).count_ones(), held);
        if best.as_ref().is_none_or(|b| key < b.0) {
            best = Some((
                key,
                json!({"held":held,"arrival":arrival,"settle":settle,"points":points,"conservative_points":conservative,"nominal_end":nominal_end}),
            ));
        }
    }
    best.map(|p| p.1)
}
pub fn launch_delay(
    initial: PlannerState,
    previous: i64,
    delay: i64,
    goal: (f64, f64),
    occupied: &impl Fn(f64, f64) -> bool,
    paths: &[Vec<EnemyPoint>],
    horizon: usize,
) -> Option<Value> {
    let PlannerState { motion: s, height } = initial;
    let (goal_x, goal_depth) = goal;
    let dx = wrap(goal_x, s.x);
    if !s.grounded
        || !(24.0..=144.0).contains(&dx.abs())
        || (goal_depth - s.depth).abs() > 64.0
        || !(0..=6).contains(&delay)
    {
        return None;
    }
    let d = if dx > 0.0 { 16 } else { 32 };
    let mut best: Option<((usize, f64), Value)> = None;
    for wait in [0, 6, 12, 18, 24] {
        for hold in [18, 24, 30, 36] {
            let mut inputs = vec![previous; delay as usize];
            inputs.extend(vec![d | 2048; wait]);
            inputs.extend(vec![d | 2050; hold]);
            inputs.resize(horizon, d | 2048);
            inputs.truncate(horizon);
            let (points, _) = forecast_player(
                s,
                previous,
                &inputs,
                occupied,
                ForecastOptions {
                    height,
                    ..Default::default()
                },
            );
            let mut airborne = false;
            for (i, point) in points.iter().enumerate() {
                let p = &point.state;
                airborne |= !p.grounded;
                if p.depth > s.depth.max(goal_depth) + 32.0
                    || paths.iter().any(|path| {
                        path.get(i).is_none_or(|e| {
                            wrap(p.x, e.x).abs() < 18.0
                                && e.depth - 14.0 < p.depth
                                && p.depth < e.depth + height
                        })
                    })
                {
                    break;
                }
                if airborne
                    && p.grounded
                    && (p.depth - goal_depth).abs() < 1.0
                    && wrap(p.x, goal_x).abs() < 24.0
                {
                    if wait == 0 {
                        return None;
                    }
                    let key = (i + 1, wrap(p.x, goal_x).abs());
                    if best.as_ref().is_none_or(|b| key < b.0) {
                        best = Some((
                            key,
                            json!({"wait":wait,"hold":hold,"arrival":i+1,"points":points[..i+1],"input_sequence":inputs[(delay as usize).min(i+1)..i+1],"final_x":p.x,"final_depth":p.depth}),
                        ));
                    }
                    break;
                }
            }
        }
    }
    best.map(|p| p.1)
}
pub fn ceiling_npc(
    initial: PlannerState,
    proposed: i64,
    previous: i64,
    delay: i64,
    occupied: &impl Fn(f64, f64) -> bool,
    paths: &[Vec<EnemyPoint>],
    horizon: usize,
) -> Option<Value> {
    let PlannerState { motion: s, height } = initial;
    if s.grounded
        || s.vy > 0.0
        || paths.is_empty()
        || !(0..=6).contains(&delay)
        || ![-8.0, 0.0, 8.0]
            .iter()
            .any(|dx| occupied(s.x + dx, s.depth - height - 4.0))
    {
        return None;
    }
    observed_ceiling(initial, proposed, previous, delay, occupied, paths, horizon)
}

pub fn observed_ceiling(
    initial: PlannerState,
    proposed: i64,
    previous: i64,
    delay: i64,
    occupied: &impl Fn(f64, f64) -> bool,
    paths: &[Vec<EnemyPoint>],
    horizon: usize,
) -> Option<Value> {
    let PlannerState { motion: s, height } = initial;
    if s.grounded || s.vy > 0.0 || paths.is_empty() || !(0..=6).contains(&delay) {
        return None;
    }
    let predict = |held, counter_landing_turn| {
        forecast_player(
            s,
            previous,
            &sequence(previous, delay as usize, held, horizon),
            occupied,
            ForecastOptions {
                height,
                counter_landing_turn,
            },
        )
        .0
    };
    let nominal = predict(proposed, false);
    if !danger(&nominal[..nominal.len().min(24)], paths, 16.0, 20.0, height) {
        return None;
    }
    let mut best: Option<((bool, u32), Value)> = None;
    for held in [2064, 2080, 16, 32, 0] {
        let points = predict(held, false);
        let conservative = predict(held, true);
        if [&points, &conservative].iter().any(|p| {
            p.len() != horizon
                || danger(p, paths, 16.0, 20.0, height)
                || p.iter().any(|p| p.state.depth > 288.0)
                || !p[horizon - 6..]
                    .iter()
                    .all(|p| p.state.grounded && occupied(p.state.x, p.state.depth + 1.0))
        }) {
            continue;
        }
        let key = (held & 2048 == 0, (held ^ proposed).count_ones());
        if best.as_ref().is_none_or(|b| key < b.0) {
            best = Some((
                key,
                json!({"held":held,"duration":(horizon-delay as usize)/6*6,"points":points,"conservative_points":conservative}),
            ));
        }
    }
    best.map(|p| p.1)
}

pub fn interception(
    s: Motion,
    previous: i64,
    delay: i64,
    entity: &Value,
    occupied: &impl Fn(f64, f64) -> bool,
    paths: &[Vec<EnemyPoint>],
    ascent: bool,
) -> Option<Value> {
    let horizon = if ascent { 130 } else { 160 };
    let items = actors::emerging(entity, occupied, horizon)?;
    if !s.grounded || !(0..=6).contains(&delay) {
        return None;
    }
    struct Trial {
        held: i64,
        duration: usize,
        coast: usize,
        neutral: usize,
        jump: usize,
        inputs: Vec<i64>,
    }
    let mut trials = vec![];
    if ascent {
        let toward = if wrap(num(&entity["pos"]["x"]) / 4096.0, s.x) >= 0.0 {
            16
        } else {
            32
        };
        for coast in [0, 6, 12, 18] {
            for neutral in [0, 6, 12] {
                for jump in [12, 18, 24, 30] {
                    for held in [toward, toward | 2048] {
                        let mut inputs = vec![previous; delay as usize];
                        inputs.extend(vec![0; coast]);
                        inputs.extend(vec![2; neutral]);
                        inputs.extend(vec![toward | 2; jump]);
                        inputs.resize(horizon, held);
                        trials.push(Trial {
                            held,
                            duration: inputs.len() - delay as usize,
                            coast,
                            neutral,
                            jump,
                            inputs,
                        });
                    }
                }
            }
        }
    } else {
        for held in [2064, 2080, 16, 32] {
            for duration in (6..97).step_by(6) {
                let mut inputs = vec![previous; delay as usize];
                inputs.extend(vec![held; duration]);
                inputs.resize(horizon, 0);
                trials.push(Trial {
                    held,
                    duration,
                    coast: 0,
                    neutral: 0,
                    jump: 0,
                    inputs,
                });
            }
        }
    }
    let mut best: Option<((usize, bool, usize), Value)> = None;
    for trial in trials {
        let (points, _) = forecast_player(
            s,
            previous,
            &trial.inputs,
            occupied,
            ForecastOptions::default(),
        );
        for (i, (point, item)) in points.iter().zip(&items).enumerate() {
            let p = &point.state;
            if p.depth > 288.0
                || (!ascent && !p.grounded)
                || paths.iter().any(|path| {
                    path.get(i).is_none_or(|e| {
                        wrap(p.x, e.x).abs() < 16.0 && (p.depth - e.depth).abs() < 24.0
                    })
                })
            {
                break;
            }
            if item.collectable
                && wrap(p.x, item.x).abs() < 10.0
                && (p.depth - item.depth).abs() < 10.0
            {
                let key = (
                    i + 1,
                    if ascent {
                        false
                    } else {
                        trial.held & 2048 == 0
                    },
                    if ascent { 0 } else { trial.duration },
                );
                if best.as_ref().is_none_or(|b| key < b.0) {
                    let end = if ascent { points.len() } else { i + 1 };
                    let input_end = if ascent { trial.inputs.len() } else { i + 1 };
                    let item_end = if ascent { items.len() } else { i + 1 };
                    let mut plan = json!({"pickup_frame":i+1,"held":trial.held,"duration":trial.duration,"input_sequence":trial.inputs[(delay as usize).min(input_end)..input_end],"points":points[..end],"item_points":items[..item_end],"item_guid":entity["actorGuid"],"item_x":item.x,"item_depth":item.depth});
                    if ascent {
                        plan["coast"] = json!(trial.coast);
                        plan["neutral_jump"] = json!(trial.neutral);
                        plan["jump"] = json!(trial.jump);
                    }
                    best = Some((key, plan));
                }
                break;
            }
            if best.as_ref().is_some_and(|b| i + 1 >= b.0 .0) {
                break;
            }
        }
    }
    best.map(|p| p.1)
}
