use std::ffi::{OsStr, OsString};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

use crate::models::{
    CourseMode, GameSettings, GameStateMismatch, LaunchRequest, Lives, Role, RomIdentity,
};
use crate::paths::allowed_log_dir;
use crate::paths::load_match_history_document_content;
use crate::processes::{
    build_bridge_command, build_melon_command, melon_env, read_bridge_diagnostics,
    read_melon_diagnostics, read_mvl_results, remove_inherited_melonds_env_keys,
    run_bridge_signaling_smoke, session_status_inner, should_show_game_state_mismatch_in_gui,
    start_match_resolved, stop_existing, LaunchPaths,
};
use crate::roms::{reusable_rom_is_current, reusable_rom_marker_path, write_reusable_rom_marker};
use crate::settings::{selected_stage, validate_request};
use crate::state::AppState;

fn request(role: Role) -> LaunchRequest {
    LaunchRequest {
        role,
        signal_url: "ws://127.0.0.1:8787/session".to_owned(),
        room_code: "room_01-test".to_owned(),
        port: 8165,
        diagnostic_events_enabled: false,
        rom_identity: None,
        rom_path: "unused.nds".to_owned(),
        settings: GameSettings {
            course_mode: CourseMode::Random,
            course_stages: vec![2, 3, 4, 0, 1],
            wins: 3,
            big_stars: 10,
            lives: Lives::Five,
            match_seed: "7".to_owned(),
            rng_seeds: vec![
                "7".to_owned(),
                "8".to_owned(),
                "9".to_owned(),
                "10".to_owned(),
                "11".to_owned(),
            ],
            input_delay_frames: 4,
            input_max_frame_lead: 4,
            rollback_enabled: false,
        },
    }
}

fn temp_log_dir(name: &str) -> PathBuf {
    let path =
        std::env::temp_dir().join(format!("nsmb-mvl-gui-test-{name}-{}", std::process::id()));
    let _ = fs::remove_dir_all(&path);
    fs::create_dir_all(&path).expect("create temp log dir");
    path
}

fn fake_executable(dir: &Path, name: &str, should_sleep: bool) -> PathBuf {
    #[cfg(windows)]
    {
        let path = dir.join(format!("{name}.cmd"));
        let body = if should_sleep {
            format!(
                "@echo off\r\necho {name} args:%*\r\necho {name} role:%MELONDS_NSML_ROLE%\r\necho {name} stage:%MELONDS_NSML_MVL_STAGE%\r\n> bridge-status.json echo ^{{\"phase\":\"connected\"^}}\r\nping -n 60 127.0.0.1 > nul\r\n"
            )
        } else {
            "@echo off\r\nexit /b 42\r\n".to_owned()
        };
        fs::write(&path, body).expect("write fake cmd");
        path
    }

    #[cfg(not(windows))]
    {
        use std::os::unix::fs::PermissionsExt;

        let path = dir.join(name);
        let body = if should_sleep {
            format!(
                "#!/bin/sh\necho \"{name} args:$*\"\necho \"{name} role:$MELONDS_NSML_ROLE\"\necho \"{name} stage:$MELONDS_NSML_MVL_STAGE\"\nprintf '%s\\n' '{{\"phase\":\"connected\"}}' > bridge-status.json\nsleep 60\n"
            )
        } else {
            "#!/bin/sh\nexit 42\n".to_owned()
        };
        fs::write(&path, body).expect("write fake sh");
        let mut permissions = fs::metadata(&path).expect("fake sh metadata").permissions();
        permissions.set_mode(0o755);
        fs::set_permissions(&path, permissions).expect("chmod fake sh");
        path
    }
}

fn fake_bridge_smoke_executable(dir: &Path, name: &str, supports_smoke: bool) -> PathBuf {
    #[cfg(windows)]
    {
        let path = dir.join(format!("{name}.cmd"));
        let body = if supports_smoke {
            "@echo off\r\nif \"%1\"==\"webrtc-signaling-udp-pair-smoke\" (\r\n  echo nsmb-net-bridge webrtc: signaling udp pair smoke passed\r\n  exit /b 0\r\n)\r\necho usage\r\nexit /b 0\r\n"
        } else {
            "@echo off\r\necho unknown command \"%1\" 1>&2\r\nexit /b 1\r\n"
        };
        fs::write(&path, body).expect("write fake bridge smoke cmd");
        path
    }

    #[cfg(not(windows))]
    {
        use std::os::unix::fs::PermissionsExt;

        let path = dir.join(name);
        let body = if supports_smoke {
            "#!/bin/sh\nif [ \"$1\" = \"webrtc-signaling-udp-pair-smoke\" ]; then\n  echo \"nsmb-net-bridge webrtc: signaling udp pair smoke passed\"\n  exit 0\nfi\necho usage\nexit 0\n"
        } else {
            "#!/bin/sh\necho \"unknown command \\\"$1\\\"\" >&2\nexit 1\n"
        };
        fs::write(&path, body).expect("write fake bridge smoke sh");
        let mut permissions = fs::metadata(&path)
            .expect("fake bridge smoke sh metadata")
            .permissions();
        permissions.set_mode(0o755);
        fs::set_permissions(&path, permissions).expect("chmod fake bridge smoke sh");
        path
    }
}

fn launch_paths(dir: &Path, bridge_path: PathBuf, melon_path: PathBuf) -> LaunchPaths {
    let input_script = dir.join("bootstrap.inputs");
    let rom_path = dir.join("host.nds");
    fs::write(&input_script, b"").expect("write input script");
    fs::write(&rom_path, b"fake rom").expect("write fake rom");
    LaunchPaths {
        log_dir: dir.join("logs"),
        bridge_path,
        melon_path,
        input_script,
        rom_path,
    }
}

fn wait_for_file_contains(path: &Path, needle: &str) -> String {
    for _ in 0..50 {
        let content = fs::read_to_string(path).unwrap_or_default();
        if content.contains(needle) {
            return content;
        }
        std::thread::sleep(std::time::Duration::from_millis(50));
    }
    fs::read_to_string(path).unwrap_or_default()
}

fn arg_strings(command: &Command) -> Vec<String> {
    command
        .get_args()
        .map(|arg| arg.to_string_lossy().into_owned())
        .collect()
}

fn env_value(command: &Command, key: &str) -> Option<String> {
    command.get_envs().find_map(|(name, value)| {
        if name == OsStr::new(key) {
            value.map(|value| value.to_string_lossy().into_owned())
        } else {
            None
        }
    })
}

#[test]
fn bridge_command_uses_signaling_offer_for_host() {
    let log_dir = temp_log_dir("bridge-host");
    let command = build_bridge_command(Path::new("bridge.exe"), &request(Role::Host), &log_dir)
        .expect("build bridge command");
    let args = arg_strings(&command);
    assert_eq!(args[0], "webrtc-offer");
    assert!(args
        .windows(2)
        .any(|pair| pair == ["--local-target", "127.0.0.1:8165"]));
    assert!(args
        .windows(2)
        .any(|pair| pair == ["--signal", "ws://127.0.0.1:8787/session"]));
    assert!(args
        .windows(2)
        .any(|pair| pair == ["--session", "room_01-test"]));
    assert!(args
        .windows(2)
        .any(|pair| pair[0] == "--status-file" && pair[1].ends_with("bridge-status.json")));
    let _ = fs::remove_dir_all(log_dir);
}

#[test]
fn bridge_command_uses_signaling_answer_for_client() {
    let log_dir = temp_log_dir("bridge-client");
    let command = build_bridge_command(Path::new("bridge.exe"), &request(Role::Client), &log_dir)
        .expect("build bridge command");
    let args = arg_strings(&command);
    assert_eq!(args[0], "webrtc-answer");
    assert!(args
        .windows(2)
        .any(|pair| pair == ["--local-bind", "127.0.0.1:8165"]));
    assert!(!args.iter().any(|arg| arg == "--local-target"));
    let _ = fs::remove_dir_all(log_dir);
}

#[test]
fn melon_env_carries_game_settings_and_netplay_start() {
    let request = request(Role::Client);
    let env = melon_env(
        &request,
        Path::new("bootstrap.inputs"),
        Path::new("logs/nsmb-mvl-gui-test"),
    );
    assert_eq!(env["MELONDS_NSML_ROLE"], "client");
    assert_eq!(env["MELONDS_NSML_PEER"], "127.0.0.1");
    assert_eq!(env["MELONDS_NSML_MVL_STAGE"], "2");
    assert_eq!(env["MELONDS_NSML_DIRECT_MVL_BOOT_STAGE"], "2");
    assert_eq!(env["MELONDS_NSML_MVL_STAGE_SEQUENCE"], "2,3,4,0,1");
    assert_eq!(env["MELONDS_NSML_MVL_COURSE_MODE"], "random");
    assert_eq!(env["MELONDS_NSML_MVL_WINS"], "3");
    assert_eq!(env["MELONDS_NSML_MVL_BIG_STARS"], "10");
    assert_eq!(env["MELONDS_NSML_MVL_LIVES"], "5");
    assert_eq!(env["MELONDS_NSML_MVL_AUTO_RESTART_AFTER_RESULT"], "1");
    assert_eq!(env["MELONDS_NSML_NETPLAY_START_FRAME"], "840");
    assert_eq!(env["MELONDS_NSML_MATCH_SEED"], "7");
    assert_eq!(env["MELONDS_NSML_MATCH_SEED_SEQUENCE"], "7,8,9,10,11");
    assert_eq!(env["MELONDS_NSML_DELAY"], "4");
    assert_eq!(env["MELONDS_NSML_INPUT_MAX_FRAME_LEAD"], "4");
    assert_eq!(env["MELONDS_NSML_SEED_WAIT_TIMEOUT_MS"], "60000");
    assert_eq!(env["MELONDS_NSML_INPUT_HEALTH_TRACE"], "1");
    assert_eq!(env["MELONDS_NSML_INPUT_HEALTH_TRACE_INTERVAL"], "120");
    assert_eq!(
        env["MELONDS_NSML_INPUT_HEALTH_TRACE_WAIT_THRESHOLD_MS"],
        "16"
    );
    assert_eq!(env["MELONDS_NSML_STATE_SYNC"], "1");
    assert_eq!(env["MELONDS_NSML_STATE_SYNC_INTERVAL"], "60");
    assert_eq!(env["MELONDS_NSML_STATE_SYNC_EXTENDED"], "1");
    assert!(env["MELONDS_NSML_DIAGNOSTICS_FILE"].ends_with("melonds-diagnostics.json"));
    assert!(!env.contains_key("MELONDS_NSML_DIAGNOSTIC_EVENTS"));
    assert!(!env.contains_key("MELONDS_NSML_DIAGNOSTIC_EVENTS_FILE"));
    assert!(!env.contains_key("MELONDS_NSML_ROLLBACK"));
}

#[test]
fn melon_env_carries_diagnostic_event_settings_when_enabled() {
    let mut request = request(Role::Host);
    request.diagnostic_events_enabled = true;
    let env = melon_env(
        &request,
        Path::new("bootstrap.inputs"),
        Path::new("logs/nsmb-mvl-gui-test"),
    );

    assert_eq!(env["MELONDS_NSML_DIAGNOSTIC_EVENTS"], "1");
    assert!(env["MELONDS_NSML_DIAGNOSTIC_EVENTS_FILE"].ends_with("melonds-events.jsonl"));
}

#[test]
fn melon_env_carries_rollback_settings_when_enabled() {
    let mut request = request(Role::Host);
    request.settings.rollback_enabled = true;
    request.settings.input_delay_frames = 2;
    request.settings.input_max_frame_lead = 2;

    let env = melon_env(
        &request,
        Path::new("bootstrap.inputs"),
        Path::new("logs/nsmb-mvl-gui-test"),
    );

    assert_eq!(env["MELONDS_NSML_DELAY"], "2");
    assert_eq!(env["MELONDS_NSML_INPUT_MAX_FRAME_LEAD"], "2");
    assert_eq!(env["MELONDS_NSML_ROLLBACK"], "1");
    assert_eq!(env["MELONDS_NSML_ROLLBACK_BACKEND"], "coredelta");
    assert_eq!(env["MELONDS_NSML_ROLLBACK_RESIMULATE"], "1");
}

#[test]
fn melon_command_sets_rom_arg_and_environment() {
    let log_dir = temp_log_dir("melon-command");
    let command = build_melon_command(
        Path::new("melonDS.exe"),
        Path::new("host.nds"),
        Path::new("bootstrap.inputs"),
        &request(Role::Host),
        &log_dir,
    )
    .expect("build melon command");
    assert_eq!(arg_strings(&command), vec!["host.nds"]);
    assert_eq!(
        env_value(&command, "MELONDS_NSML_MVL_BIG_STARS").as_deref(),
        Some("10")
    );
    assert_eq!(
        env_value(&command, "MELONDS_NSML_MVL_STAGE").as_deref(),
        Some("2")
    );
    let _ = fs::remove_dir_all(log_dir);
}

#[test]
fn melonds_cli_open_input_config_option_is_registered() {
    let cli_cpp = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("..")
        .join("..")
        .join("..")
        .join("src")
        .join("frontend")
        .join("qt_sdl")
        .join("CLI.cpp");
    let source = fs::read_to_string(cli_cpp).expect("read CLI.cpp");
    assert!(source.contains("open-input-config"));
}

#[test]
fn melon_command_sanitizes_inherited_melonds_environment() {
    let mut command = Command::new("melonDS.exe");
    command.env("MELONDS_NSML_NORMALIZE_MVL_ENTRANCE_SPAWN_WRITES", "1");
    command.env("MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTERS", "1");
    command.env("NSMB_MVL_SIGNAL_URL", "ws://127.0.0.1:8787/session");

    remove_inherited_melonds_env_keys(
        &mut command,
        [
            OsString::from("MELONDS_NSML_NORMALIZE_MVL_ENTRANCE_SPAWN_WRITES"),
            OsString::from("MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTERS"),
            OsString::from("NSMB_MVL_SIGNAL_URL"),
        ],
    );

    assert_eq!(
        env_value(&command, "MELONDS_NSML_NORMALIZE_MVL_ENTRANCE_SPAWN_WRITES"),
        None
    );
    assert_eq!(
        env_value(&command, "MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTERS"),
        None
    );
    assert_eq!(
        env_value(&command, "NSMB_MVL_SIGNAL_URL").as_deref(),
        Some("ws://127.0.0.1:8787/session")
    );
}

#[test]
fn validation_rejects_bad_signal_url_and_room_code() {
    let mut bad_signal = request(Role::Host);
    bad_signal.signal_url = "https://example.invalid/session".to_owned();
    assert!(validate_request(&bad_signal).is_err());

    let mut bad_room = request(Role::Host);
    bad_room.room_code = "bad room".to_owned();
    assert!(validate_request(&bad_room).is_err());
}

#[test]
fn selected_stage_uses_match_seed_for_random_course() {
    let mut request = request(Role::Host);
    request.settings.match_seed = "0x0E".to_owned();
    request.settings.rng_seeds[0] = "0x0E".to_owned();
    request.settings.course_stages = vec![4, 3, 2, 1, 0];
    assert_eq!(selected_stage(&request.settings, 2).expect("stage"), 4);

    request.settings.course_mode = CourseMode::Select;
    request.settings.course_stages = vec![1, 2, 3, 4, 0];
    assert_eq!(selected_stage(&request.settings, 9).expect("stage"), 1);
}

#[test]
fn validation_rejects_duplicate_random_course_stages() {
    let mut request = request(Role::Host);
    request.settings.course_mode = CourseMode::Random;
    request.settings.course_stages = vec![1, 2, 3, 2, 4];
    assert!(validate_request(&request)
        .expect_err("duplicate random stages rejected")
        .contains("同じコース"));

    request.settings.course_mode = CourseMode::Select;
    validate_request(&request).expect("manual stage selection may repeat stages");
}

#[test]
fn reusable_rom_marker_requires_current_format_and_rom_file() {
    let dir = temp_log_dir("reusable-rom-marker");
    let rom = dir.join("host.nds");
    fs::write(&rom, b"fake rom").expect("write fake rom");
    assert!(!reusable_rom_is_current(&rom));

    write_reusable_rom_marker(&rom).expect("write marker");
    assert!(reusable_rom_is_current(&rom));

    fs::write(reusable_rom_marker_path(&rom), "old-format\n").expect("write stale marker");
    assert!(!reusable_rom_is_current(&rom));

    fs::remove_file(&rom).expect("remove fake rom");
    write_reusable_rom_marker(&rom).expect("write marker without rom");
    assert!(!reusable_rom_is_current(&rom));
    let _ = fs::remove_dir_all(dir);
}

#[test]
fn bridge_signaling_smoke_requires_success_log() {
    let dir = temp_log_dir("bridge-smoke-ok");
    let bridge = fake_bridge_smoke_executable(&dir, "fake-bridge-smoke", true);
    let output = run_bridge_signaling_smoke(&bridge).expect("bridge smoke");
    assert!(output.contains("signaling udp pair smoke passed"));
    let _ = fs::remove_dir_all(dir);
}

#[test]
fn bridge_signaling_smoke_rejects_old_bridge() {
    let dir = temp_log_dir("bridge-smoke-old");
    let bridge = fake_bridge_smoke_executable(&dir, "fake-bridge-old", false);
    let err = run_bridge_signaling_smoke(&bridge).expect_err("old bridge rejected");
    assert!(err.contains("bridge signaling smoke が失敗しました"));
    let _ = fs::remove_dir_all(dir);
}

#[test]
fn bridge_diagnostics_reads_status_json() {
    let dir = temp_log_dir("bridge-status");
    fs::write(
        dir.join("bridge-status.json"),
        br#"{"phase":"connected","selected_candidate_pair":{"route":"stun"}}"#,
    )
    .expect("write status");
    let (value, error) = read_bridge_diagnostics(&dir);
    assert!(error.is_none());
    assert_eq!(
        value
            .expect("status value")
            .selected_candidate_pair
            .and_then(|pair| pair.route)
            .as_deref(),
        Some("stun")
    );
    let _ = fs::remove_dir_all(dir);
}

#[test]
fn melon_diagnostics_reads_game_state_mismatch_json() {
    let dir = temp_log_dir("game-state-mismatch");
    fs::write(
        dir.join("melonds-diagnostics.json"),
        br#"{
          "game_state_mismatch": {
            "instance": 0,
            "frame": 180,
            "local_hash": "0000000000000003",
            "remote_hash": "0000000000000004",
            "basic_matches": false,
            "player_global_matches": false,
            "wifi_candidate_matches": true,
            "render_candidate_matches": true,
            "line": "NSMB PoC: game state mismatch inst=0 frame=180 local=0000000000000003 remote=0000000000000004 basic=0 playerGlobal=0 wifiCandidate=1 renderCandidate=1"
          }
        }"#,
    )
    .expect("write melonds diagnostics");

    let diagnostics = read_melon_diagnostics(&dir).expect("diagnostics");
    let mismatch = diagnostics.game_state_mismatch.expect("mismatch");

    assert_eq!(mismatch.instance, Some(0));
    assert_eq!(mismatch.frame, Some(180));
    assert_eq!(mismatch.local_hash.as_deref(), Some("0000000000000003"));
    assert_eq!(mismatch.remote_hash.as_deref(), Some("0000000000000004"));
    assert_eq!(mismatch.basic_matches, Some(false));
    assert_eq!(mismatch.player_global_matches, Some(false));
    assert_eq!(mismatch.wifi_candidate_matches, Some(true));
    assert_eq!(mismatch.render_candidate_matches, Some(true));
    assert!(mismatch.line.contains("frame=180"));
    let _ = fs::remove_dir_all(dir);
}

#[test]
fn mvl_results_parse_melon_stdout_and_launcher_stage_sequence() {
    let dir = temp_log_dir("mvl-results");
    fs::write(
        dir.join("launcher.json"),
        br#"{
          "request": {
            "settings": {
              "course_stages": [2, 3, 4, 0, 1]
            }
          }
        }"#,
    )
    .expect("write launcher");
    fs::write(
        dir.join("melonds.stdout.txt"),
        "other line\n\
NSMB MvL auto restart: result inst=0 frame=4320 winner=0 stars=5/0 displayed=5/0 collected=5/0 lives=3/2 deaths=0/1 dead=0/0 matchWins=1/0 target=2\n\
NSMB MvL auto restart: result inst=0 frame=8200 winner=1 stars=0/5 displayed=0/5 collected=0/5 lives=1/3 deaths=2/0 dead=1/0 matchWins=1/1 target=2\n",
    )
    .expect("write stdout");

    let results = read_mvl_results(&dir);

    assert_eq!(results.len(), 2);
    assert_eq!(results[0].game_index, 1);
    assert_eq!(results[0].stage, Some(2));
    assert_eq!(results[0].winner, Some(0));
    assert_eq!(results[0].mario.stars, 5);
    assert_eq!(results[0].luigi.lives, 2);
    assert_eq!(results[0].mario_match_wins, 1);
    assert_eq!(results[0].target_wins, 2);
    assert!(results[0].resolved);
    assert_eq!(results[1].game_index, 2);
    assert_eq!(results[1].stage, Some(3));
    assert_eq!(results[1].winner, Some(1));
    assert!(results[1].mario.dead);
    assert_eq!(results[1].mario.lives, 0);
    let _ = fs::remove_dir_all(dir);
}

#[test]
fn mvl_results_prefer_death_loss_over_logged_star_winner() {
    let dir = temp_log_dir("mvl-results-death-winner");
    fs::write(
        dir.join("melonds.stdout.txt"),
        "NSMB MvL auto restart: result inst=0 frame=4320 winner=0 stars=5/3 displayed=5/3 collected=5/3 lives=1/2 deaths=2/1 dead=1/0 matchWins=1/0 target=2\n\
NSMB MvL auto restart: result inst=0 frame=8200 winner=0 stars=5/0 displayed=5/0 collected=5/0 lives=2/1 deaths=1/2 dead=0/1 matchWins=2/0 target=2\n",
    )
    .expect("write stdout");

    let results = read_mvl_results(&dir);

    assert_eq!(results.len(), 2);
    assert_eq!(results[0].winner, Some(1));
    assert_eq!(results[0].mario.lives, 0);
    assert_eq!(results[0].mario_match_wins, 0);
    assert_eq!(results[0].luigi_match_wins, 1);
    assert_eq!(results[1].winner, Some(0));
    assert_eq!(results[1].mario_match_wins, 1);
    assert_eq!(results[1].luigi_match_wins, 1);
    let _ = fs::remove_dir_all(dir);
}

#[test]
fn match_history_v1_migration_recomputes_death_decided_winners() {
    let document = serde_json::json!({
        "schema_version": 1,
        "matches": [
            {
                "id": "match-1",
                "logDir": "C:\\logs\\match-1",
                "playerIds": {
                    "mario": "host-id",
                    "luigi": "client-id"
                },
                "playerNames": {
                    "mario": "Host",
                    "luigi": "Client"
                },
                "role": "host",
                "roomCode": "room-1",
                "settings": {
                    "course_mode": "random",
                    "course_stages": [0, 1, 2, 3, 4],
                    "wins": 3,
                    "big_stars": 5,
                    "lives": "3",
                    "match_seed": "1",
                    "rng_seeds": ["1", "2", "3", "4", "5"],
                    "input_delay_frames": 4,
                    "input_max_frame_lead": 4,
                    "rollback_enabled": false
                },
                "stages": [
                    {
                        "game_index": 1,
                        "stage": 0,
                        "frame": 4320,
                        "winner": 0,
                        "mario": {
                            "stars": 5,
                            "displayed_stars": 5,
                            "collected_stars": 5,
                            "lives": 0,
                            "deaths": 3,
                            "dead": true
                        },
                        "luigi": {
                            "stars": 3,
                            "displayed_stars": 3,
                            "collected_stars": 3,
                            "lives": 1,
                            "deaths": 2,
                            "dead": false
                        },
                        "mario_match_wins": 1,
                        "luigi_match_wins": 0,
                        "target_wins": 3,
                        "resolved": true,
                        "line": "old winner favoured stars"
                    },
                    {
                        "game_index": 2,
                        "stage": 1,
                        "frame": 8200,
                        "winner": 1,
                        "mario": {
                            "stars": 2,
                            "displayed_stars": 2,
                            "collected_stars": 2,
                            "lives": 2,
                            "deaths": 1,
                            "dead": false
                        },
                        "luigi": {
                            "stars": 5,
                            "displayed_stars": 5,
                            "collected_stars": 5,
                            "lives": 0,
                            "deaths": 3,
                            "dead": true
                        },
                        "mario_match_wins": 1,
                        "luigi_match_wins": 1,
                        "target_wins": 3,
                        "resolved": true,
                        "line": "old winner favoured stars"
                    }
                ],
                "startedAt": "2026-06-24T00:00:00.000Z",
                "status": "completed"
            }
        ]
    });

    let (matches, migrated) =
        load_match_history_document_content(&document.to_string()).expect("migrate history");

    assert!(migrated);
    assert_eq!(matches.len(), 1);
    assert_eq!(matches[0].stages[0].winner, Some(1));
    assert_eq!(matches[0].stages[0].mario_match_wins, 0);
    assert_eq!(matches[0].stages[0].luigi_match_wins, 1);
    assert_eq!(matches[0].stages[1].winner, Some(0));
    assert_eq!(matches[0].stages[1].mario_match_wins, 1);
    assert_eq!(matches[0].stages[1].luigi_match_wins, 1);
}

#[test]
fn mvl_results_keep_unresolved_stage_without_winner() {
    let dir = temp_log_dir("mvl-results-unresolved");
    fs::write(
        dir.join("melonds.stdout.txt"),
        "NSMB MvL auto restart: result unresolved inst=0 frame=2400 stars=0/0 displayed=0/0 collected=0/0 lives=3/3 deaths=0/0 dead=0/0 matchWins=0/0 target=2\n",
    )
    .expect("write stdout");

    let results = read_mvl_results(&dir);

    assert_eq!(results.len(), 1);
    assert_eq!(results[0].winner, None);
    assert!(!results[0].resolved);
    assert_eq!(results[0].mario.lives, 3);
    assert_eq!(results[0].luigi.lives, 3);
    let _ = fs::remove_dir_all(dir);
}

#[test]
fn game_state_mismatch_gui_visibility_ignores_basic_only_mismatch() {
    assert!(!should_show_game_state_mismatch_in_gui(
        &game_state_mismatch(Some(false), Some(true), Some(true), Some(true))
    ));
    assert!(should_show_game_state_mismatch_in_gui(
        &game_state_mismatch(Some(false), Some(false), Some(true), Some(true))
    ));
    assert!(should_show_game_state_mismatch_in_gui(
        &game_state_mismatch(Some(false), Some(true), Some(false), Some(true))
    ));
    assert!(should_show_game_state_mismatch_in_gui(
        &game_state_mismatch(Some(false), Some(true), Some(true), Some(false))
    ));
}

fn game_state_mismatch(
    basic_matches: Option<bool>,
    player_global_matches: Option<bool>,
    wifi_candidate_matches: Option<bool>,
    render_candidate_matches: Option<bool>,
) -> GameStateMismatch {
    GameStateMismatch {
        instance: Some(0),
        frame: Some(180),
        local_hash: Some("0000000000000003".to_owned()),
        remote_hash: Some("0000000000000004".to_owned()),
        basic_matches,
        player_global_matches,
        wifi_candidate_matches,
        render_candidate_matches,
        line: "NSMB PoC: game state mismatch".to_owned(),
    }
}

#[test]
fn allowed_log_dir_rejects_path_outside_logs_root() {
    let dir = temp_log_dir("allowed-log-dir");
    let logs = dir.join("logs");
    let run = logs.join("run");
    let outside = dir.join("outside");
    fs::create_dir_all(&run).expect("create run");
    fs::create_dir_all(&outside).expect("create outside");
    assert_eq!(
        allowed_log_dir(&logs, &run).expect("allowed run"),
        run.canonicalize().expect("canonical run")
    );
    assert!(allowed_log_dir(&logs, &outside).is_err());
    let _ = fs::remove_dir_all(dir);
}

#[test]
fn start_match_resolved_launches_processes_and_stop_clears_session() {
    let dir = temp_log_dir("start-match");
    let bridge = fake_executable(&dir, "fake-bridge", true);
    let melon = fake_executable(&dir, "fake-melon", true);
    let paths = launch_paths(&dir, bridge, melon);
    fs::create_dir_all(&paths.log_dir).expect("create logs");

    let state = AppState::default();
    let mut launch_request = request(Role::Host);
    launch_request.rom_identity = Some(RomIdentity {
        rom_pair_id: "pair_hash".to_owned(),
        generator_id: "generator_hash".to_owned(),
        host_rom_sha256: "host_hash".to_owned(),
        client_rom_sha256: "client_hash".to_owned(),
    });
    let response = start_match_resolved(&state, launch_request, paths).expect("start fake match");
    assert!(response.bridge_pid > 0);
    assert!(response.melon_pid > 0);

    let status = session_status_inner(&state).expect("status");
    assert!(status.active);
    assert_eq!(status.bridge.as_deref(), Some("running"));
    assert_eq!(status.melon.as_deref(), Some("running"));
    assert!(status.game_state_mismatch.is_none());
    assert!(status.mvl_results.is_empty());

    let bridge_stdout =
        wait_for_file_contains(&dir.join("logs").join("bridge.stdout.txt"), "fake-bridge");
    assert!(bridge_stdout.contains("webrtc-offer"));
    assert!(bridge_stdout.contains("room_01-test"));

    let melon_stdout =
        wait_for_file_contains(&dir.join("logs").join("melonds.stdout.txt"), "fake-melon");
    assert!(melon_stdout.contains("role:host"));
    assert!(melon_stdout.contains("stage:2"));

    let launcher_json =
        fs::read_to_string(dir.join("logs").join("launcher.json")).expect("launcher manifest");
    let launcher: serde_json::Value =
        serde_json::from_str(&launcher_json).expect("parse launcher manifest");
    assert_eq!(launcher["gui"]["version"], env!("CARGO_PKG_VERSION"));
    assert_eq!(launcher["rom_identity"]["host_rom_sha256"], "host_hash");
    assert_eq!(
        launcher["request"]["rom_identity"]["client_rom_sha256"],
        "client_hash"
    );

    stop_existing(&state).expect("stop fake match");
    let status = session_status_inner(&state).expect("status after stop");
    assert!(!status.active);

    let _ = fs::remove_dir_all(dir);
}

#[test]
fn start_match_resolved_does_not_store_session_when_melon_spawn_fails() {
    let dir = temp_log_dir("start-match-fail");
    let bridge = fake_executable(&dir, "fake-bridge", true);
    let missing_melon = dir.join("missing-melon.exe");
    let paths = launch_paths(&dir, bridge, missing_melon);
    fs::create_dir_all(&paths.log_dir).expect("create logs");

    let state = AppState::default();
    let err =
        start_match_resolved(&state, request(Role::Host), paths).expect_err("melon spawn fails");
    assert!(err.contains("melonDS の起動に失敗しました"));

    let status = session_status_inner(&state).expect("status after failed start");
    assert!(!status.active);

    let _ = fs::remove_dir_all(dir);
}
