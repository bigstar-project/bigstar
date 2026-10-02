use bigstar_rule_cpu::runtime::{Controller, Worker, PROFILES};
use flate2::{write::GzEncoder, Compression};
use serde_json::{json, Value};
use std::{
    env,
    fs::OpenOptions,
    io::{self, BufRead, Write},
    time::Instant,
};

fn main() {
    if let Err(e) = run() {
        eprintln!("CPU worker: {e}");
        std::process::exit(1)
    }
}
fn run() -> Result<(), Box<dyn std::error::Error>> {
    let mut args = env::args().skip(1);
    let mut check = false;
    let mut research = false;
    let mut player = 1;
    let mut period = None;
    let mut profile = env::var("MELONDS_NSML_RULE_PROFILE").unwrap_or_else(|_| "beginner".into());
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--check" => check = true,
            "--research" => research = true,
            "--profile" => profile = args.next().ok_or("Missing profile")?,
            "--player" => player = args.next().ok_or("Missing player")?.parse()?,
            "--period" => period = Some(args.next().ok_or("Missing period")?.parse::<i64>()?),
            "--help" => {
                println!("bigstar-rule-cpu [--check] [--research --player 0|1 --profile beginner|combat_v2|development --period N]");
                return Ok(());
            }
            _ => return Err(format!("Unknown argument: {arg}").into()),
        }
    }
    if check {
        for profile in PROFILES {
            Controller::new(profile, 1, if profile == "beginner" { 48 } else { 60 })?;
        }
        println!("READY");
        return Ok(());
    }
    if !research && (player != 1 || period.is_some()) {
        return Err("Player and period overrides require --research".into());
    }
    let period = period.unwrap_or(if profile == "beginner" { 48 } else { 60 });
    let controller = Controller::new(&profile, player, period)?;
    let mut worker = Worker::new(controller, player);
    let mut capture = if let Ok(path) = env::var("MELONDS_NSML_RULE_CAPTURE") {
        Some(GzEncoder::new(
            OpenOptions::new().write(true).create_new(true).open(path)?,
            Compression::fast(),
        ))
    } else {
        None
    };
    let stdin = io::stdin();
    let mut input = stdin.lock();
    let stdout = io::stdout();
    let mut output = io::BufWriter::new(stdout.lock());
    writeln!(output, "READY")?;
    output.flush()?;
    let mut line = String::new();
    loop {
        line.clear();
        if input.read_line(&mut line)? == 0 {
            break;
        }
        let request: Value = serde_json::from_str(&line)?;
        let started = Instant::now();
        if research {
            let reply = match request["op"].as_str().unwrap_or("act") {
                "reset" => {
                    worker.controller.reset();
                    json!({"ready":true})
                }
                "act" => {
                    let frame = request["frame"].as_i64().ok_or("Missing episode frame")?;
                    let previous = request["previousHeld"]
                        .as_i64()
                        .ok_or("Missing previousHeld")?;
                    if request["decision"]["time"]["rollbackEnabled"] == true {
                        return Err("The CPU worker does not support rollback".into());
                    }
                    let held = worker.controller.act(&request["decision"], frame, previous);
                    json!({"held":held,"trace":worker.controller.trace(),"decisionNanos":started.elapsed().as_nanos() as u64})
                }
                _ => return Err("Unknown research operation".into()),
            };
            serde_json::to_writer(&mut output, &reply)?;
            writeln!(output)?;
        } else {
            let (frame, held) = worker.step(&request)?;
            if let Some(capture) = &mut capture {
                serde_json::to_writer(
                    &mut *capture,
                    &json!({"decision":request["decision"],"previousHeld":request["previousHeld"],"held":held,"trace":worker.controller.trace()}),
                )?;
                writeln!(capture)?;
            }
            writeln!(output, "{frame} {held}")?;
        }
        output.flush()?;
    }
    if let Some(capture) = capture {
        capture.finish()?;
    }
    Ok(())
}
