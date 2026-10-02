use super::*;
use std::io::Read;
fn same(a: &Value, b: &Value, path: &str) {
    match (a, b) {
        (Value::Number(a), Value::Number(b)) => assert_eq!(a.as_f64(), b.as_f64(), "{path}"),
        (Value::Array(a), Value::Array(b)) => {
            assert_eq!(a.len(), b.len(), "{path}");
            for (i, (a, b)) in a.iter().zip(b).enumerate() {
                same(a, b, &format!("{path}/{i}"));
            }
        }
        (Value::Object(a), Value::Object(b)) => {
            assert_eq!(a.len(), b.len(), "{path}: {a:?} vs {b:?}");
            for (k, a) in a {
                same(a, b.get(k).unwrap_or(&Value::Null), &format!("{path}/{k}"));
            }
        }
        _ => assert_eq!(a, b, "{path}"),
    }
}
#[test]
fn native_regression_layers_match_preserved_oracle() {
    let mut decoded = String::new();
    flate2::read::GzDecoder::new(
        &include_bytes!("../../tests/fixtures/rust-layer-parity.json.gz")[..],
    )
    .read_to_string(&mut decoded)
    .unwrap();
    let cases: Value = serde_json::from_str(&decoded).unwrap();
    for case in array(&cases) {
        let mut r = DevelopmentRule::new(int(&case["player"]) as usize, 60).unwrap();
        r.base.trace = case["trace"].clone();
        if let Some(n) = case["state"]["jump_until"].as_i64() {
            r.base.jump_until = n
        }
        if let Some(n) = case["state"]["last_pit_recovery"].as_i64() {
            r.last_pit_recovery = n
        }
        let (d, f, p, h) = (
            &case["decision"],
            int(&case["frame"]),
            int(&case["previous"]),
            int(&case["held"]),
        );
        let (held, plan) = match int(&case["layer"]) {
            16 => (r.skid_jump(d, f, p, h), r.skid_jump_plan.clone()),
            25 => (
                r.recovery_finish(d, f, p, h),
                r.recovery_finish_plan.clone(),
            ),
            26 => (r.npc_landing(d, f, p, h), r.npc_landing_plan.clone()),
            27 => (r.wait_wall(d, f, p, h), r.wait_wall_plan.clone()),
            28 => (r.falling_goomba(d, f, p, h), r.falling_goomba_plan.clone()),
            29 => (r.wall_short(d, f, p, h), None),
            30 => (r.shot_recovery(d, f, p, h), r.shot_recovery_plan.clone()),
            _ => panic!("unknown layer"),
        };
        same(
            &case["expected"],
            &json!({"held":held,"trace":r.base.trace,"plan":plan}),
            text(&case["name"]),
        );
    }
}
