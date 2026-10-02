use flate2::read::GzDecoder;
use serde_json::{json, Value};
use std::{
    io::{BufRead, BufReader, Read, Write},
    process::{Command, Stdio},
};
fn worker() -> Command {
    let mut c = Command::new(env!("CARGO_BIN_EXE_bigstar-rule-cpu"));
    c.env_remove("MELONDS_NSML_RULE_CAPTURE")
        .env("MELONDS_NSML_RULE_PROFILE", "development");
    c
}
fn fixture() -> Vec<Value> {
    let mut s = String::new();
    GzDecoder::new(&include_bytes!("fixtures/opening-box-watch.json.gz")[..])
        .read_to_string(&mut s)
        .unwrap();
    serde_json::from_str(&s).unwrap()
}
#[test]
fn packaged_protocol_replays_native_opening_and_holds_between_decisions() {
    let mut child = worker()
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .spawn()
        .unwrap();
    let mut input = child.stdin.take().unwrap();
    let mut output = BufReader::new(child.stdout.take().unwrap());
    let mut line = String::new();
    output.read_line(&mut line).unwrap();
    assert_eq!(line.trim(), "READY");
    for row in fixture() {
        let d = &row["decision"];
        let request = json!({"decision":d,"previousHeld":row["previousHeld"]});
        writeln!(input, "{request}").unwrap();
        input.flush().unwrap();
        line.clear();
        output.read_line(&mut line).unwrap();
        assert_eq!(
            line.trim(),
            format!("{} {}", d["time"]["rawFrame"], row["held"])
        );
        let mut intervening = d.clone();
        intervening["time"]["rawFrame"] = json!(d["time"]["rawFrame"].as_i64().unwrap() + 1);
        writeln!(
            input,
            "{}",
            json!({"decision":intervening,"previousHeld":row["held"]})
        )
        .unwrap();
        input.flush().unwrap();
        line.clear();
        output.read_line(&mut line).unwrap();
        assert_eq!(
            line.trim(),
            format!("{} {}", intervening["time"]["rawFrame"], row["held"])
        );
    }
    drop(input);
    assert!(child.wait().unwrap().success());
}
#[test]
fn worker_validates_profile_and_refuses_rollback() {
    assert!(worker().arg("--check").output().unwrap().status.success());
    assert!(!worker()
        .args(["--profile", "unknown"])
        .output()
        .unwrap()
        .status
        .success());
    let mut child = worker()
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .unwrap();
    let mut row = fixture().remove(0);
    row["decision"]["time"]["rollbackEnabled"] = json!(true);
    writeln!(child.stdin.take().unwrap(), "{row}").unwrap();
    let output = child.wait_with_output().unwrap();
    assert!(!output.status.success());
    assert_eq!(String::from_utf8(output.stdout).unwrap().trim(), "READY");
    assert!(String::from_utf8(output.stderr)
        .unwrap()
        .contains("does not support rollback"));
}
