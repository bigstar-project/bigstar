//! Ordinary motion forecast. These equations deliberately retain the reference
//! operation order and its limited validity (not a replacement game simulator).
use serde::{Deserialize, Serialize};

pub fn ground_velocity(v: f64, direction: i64, run: bool) -> f64 {
    if direction == 0 {
        return (v.abs() - 0.03515625).max(0.0).copysign(v);
    }
    let d = direction as f64;
    let a = v * d;
    if a < 0.0 {
        return v + d * 0.078125;
    }
    if !run && a > 1.5 {
        return d * (a - if a < 2.25 { 0.03125 } else { 0.0234375 }).max(1.5);
    }
    let acceleration = if a < 0.5 {
        0.0703125
    } else if a < 1.5 {
        if run {
            0.04296875
        } else {
            0.03515625
        }
    } else if a < 2.25 {
        0.03125
    } else {
        0.0234375
    };
    d * (a + acceleration).min(if run { 3.0 } else { 1.5 })
}
pub fn horizontal_run_step(v: f64, direction: i64) -> f64 {
    let a = v * direction as f64;
    let acceleration = if a < 0.5 {
        0.0703125
    } else if a < 1.5 {
        0.04296875
    } else if a < 2.25 {
        0.03125
    } else {
        0.0234375
    };
    direction as f64 * (a + acceleration).min(3.0)
}
pub fn horizontal_walk_step(v: f64, direction: i64) -> f64 {
    let a = v * direction as f64;
    if direction == 0 || a >= 1.5 {
        return v;
    }
    direction as f64 * (a + if a < 0.5 { 0.0703125 } else { 0.03515625 }).min(1.5)
}
pub fn vertical_step(v: f64, jump: bool) -> f64 {
    let a = if jump && v > 2.5 {
        -0.0625
    } else if (jump && v > 1.5) || (-2.0 < v && v < 0.0) {
        -0.25
    } else {
        -0.34375
    };
    if v < -4.0 {
        (v - a).min(-4.0)
    } else {
        (v + a).max(-4.0)
    }
}
pub fn ground_turn_step(
    v: f64,
    direction: i64,
    run: bool,
    facing: i64,
    turn: i64,
) -> (f64, i64, i64) {
    if direction != 0 && direction != facing {
        return ((v.abs() - 0.03515625).max(0.0).copysign(v), direction, 2);
    }
    if turn != 0 {
        return ((v.abs() - 0.078125).max(0.0).copysign(v), facing, turn - 1);
    }
    if direction != 0 && v * (direction as f64) < 0.0 {
        return (v + direction as f64 * 0.1875, facing, 0);
    }
    (ground_velocity(v, direction, run), facing, 0)
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize)]
pub struct Motion {
    pub x: f64,
    pub depth: f64,
    pub vx: f64,
    pub vy: f64,
    pub grounded: bool,
    #[serde(default = "default_facing")]
    pub facing: i64,
    #[serde(default)]
    pub turn_remaining: i64,
    #[serde(default)]
    pub edge_remaining: i64,
}
fn default_facing() -> i64 {
    1
}
#[derive(Debug, Clone, Serialize)]
pub struct Point {
    #[serde(flatten)]
    pub state: Motion,
    pub contact: Option<&'static str>,
    pub launch: bool,
    pub was_grounded: bool,
}
#[derive(Debug, Clone, Copy)]
pub struct ForecastOptions {
    pub height: f64,
    pub counter_landing_turn: bool,
}
impl Default for ForecastOptions {
    fn default() -> Self {
        Self {
            height: 16.0,
            counter_landing_turn: false,
        }
    }
}

pub fn forecast_player(
    s: Motion,
    previous: i64,
    inputs: &[i64],
    occupied: &impl Fn(f64, f64) -> bool,
    options: ForecastOptions,
) -> (Vec<Point>, &'static str) {
    forecast(s, previous, inputs, occupied, options, false)
}
pub fn forecast_upper(
    s: Motion,
    previous: i64,
    inputs: &[i64],
    occupied: &impl Fn(f64, f64) -> bool,
    options: ForecastOptions,
) -> (Vec<Point>, &'static str) {
    forecast(s, previous, inputs, occupied, options, true)
}
fn forecast(
    mut s: Motion,
    mut previous: i64,
    inputs: &[i64],
    occupied: &impl Fn(f64, f64) -> bool,
    options: ForecastOptions,
    ceiling_corner: bool,
) -> (Vec<Point>, &'static str) {
    let mut points = Vec::with_capacity(inputs.len());
    for &held in inputs {
        let direction = i64::from(held & 16 != 0) - i64::from(held & 32 != 0);
        let jump = held & 3 != 0;
        let was_grounded = s.grounded;
        let launch = s.grounded && jump && previous & 3 == 0;
        if launch {
            s.vy = if s.vx.abs() < 1.0 {
                3.65625
            } else if s.vx.abs() < 1.5 {
                3.78125
            } else {
                3.90625
            };
            s.grounded = false;
        }
        if s.grounded {
            (s.vx, s.facing, s.turn_remaining) = ground_turn_step(
                s.vx,
                direction,
                held & 2048 != 0,
                s.facing,
                s.turn_remaining,
            );
        } else {
            s.vx = if direction != 0 && held & 2048 != 0 {
                horizontal_run_step(s.vx, direction)
            } else {
                horizontal_walk_step(s.vx, direction)
            };
            if direction != 0 {
                s.facing = direction;
                s.turn_remaining = 0;
            }
        }
        let mut nx = s.x + s.vx;
        let side = if s.vx > 0.0 { 1.0 } else { -1.0 };
        let mut contact = None;
        if s.vx != 0.0
            && [4.0, if options.height == 16.0 { 11.0 } else { 19.0 }]
                .iter()
                .any(|h| occupied(nx + side * 8.0, s.depth - h))
        {
            if !s.grounded {
                return (points, "wall");
            }
            let boundary = ((nx + side * 8.0) / 16.0).floor() * 16.0;
            nx = if side > 0.0 {
                boundary - 8.0
            } else {
                boundary + 24.0
            };
            s.vx = 0.0;
            contact = Some("ground_wall");
        }
        s.x = nx;
        if s.grounded {
            if [-5.0, 0.0, 4.0]
                .iter()
                .any(|f| occupied(s.x + f, s.depth + 1.0))
            {
                s.vy = -2.0;
            } else {
                s.grounded = false;
                s.depth += 2.34375;
                s.vy = 0.0;
                s.edge_remaining = 4;
                contact = Some("edge");
            }
        } else {
            let mut nd;
            if s.edge_remaining != 0 {
                nd = s.depth + 0.34375;
                s.vy = if s.edge_remaining >= 3 { 0.0 } else { -0.34375 };
                s.edge_remaining -= 1;
            } else {
                s.vy = vertical_step(s.vy, jump);
                nd = s.depth - s.vy;
            }
            if s.vy > 0.0 {
                let head = nd - options.height;
                if ceiling_corner && [-2.0, 0.0, 1.0].iter().any(|f| occupied(s.x + f, head)) {
                    nd = ((head / 16.0).floor() + 1.0) * 16.0 + options.height;
                    s.vy = 0.0;
                    contact = Some("ceiling");
                }
                let mut y = ((s.depth - options.height) / 16.0).floor() * 16.0;
                let end = ((nd - options.height) / 16.0).ceil() * 16.0;
                while contact != Some("ceiling") && y >= end {
                    if [-2.0, 0.0, 1.0].iter().any(|f| occupied(s.x + f, y - 0.01)) {
                        nd = y + options.height;
                        s.vy = 0.0;
                        contact = Some("ceiling");
                        break;
                    }
                    y -= 16.0;
                }
            } else {
                let mut y = (s.depth / 16.0).ceil() * 16.0;
                let end = (nd / 16.0).floor() * 16.0;
                while y <= end {
                    if [-5.0, 0.0, 4.0].iter().any(|f| occupied(s.x + f, y + 0.01)) {
                        nd = y;
                        s.vy = -2.0;
                        s.grounded = true;
                        contact = Some("floor");
                        if options.counter_landing_turn
                            && direction != 0
                            && s.vx * (direction as f64) < 0.0
                        {
                            s.facing = -direction;
                            s.turn_remaining = 0;
                        }
                        break;
                    }
                    y += 16.0;
                }
            }
            s.depth = nd;
        }
        points.push(Point {
            state: s,
            contact,
            launch,
            was_grounded,
        });
        previous = held;
        if s.depth > 352.0 {
            return (points, "deep");
        }
    }
    (points, "horizon")
}
