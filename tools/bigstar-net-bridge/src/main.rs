use std::env;
use std::io;
use std::net::{SocketAddr, UdpSocket};
use std::path::PathBuf;
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};

const MAX_DATAGRAM_SIZE: usize = 2048;

#[derive(Clone, Debug)]
struct UdpTunnelConfig {
    local_bind: SocketAddr,
    local_target: Option<SocketAddr>,
    bridge_bind: SocketAddr,
    bridge_peer: SocketAddr,
}

struct LocalWebRtcConfig {
    local_bind: SocketAddr,
    local_target: Option<SocketAddr>,
    stun_servers: Vec<String>,
    signal_session: Option<(String, String)>,
    status_file: Option<PathBuf>,
}

#[derive(Default)]
struct Stats {
    app_to_bridge_packets: u64,
    app_to_bridge_bytes: u64,
    bridge_to_app_packets: u64,
    bridge_to_app_bytes: u64,
    dropped_no_local_target: u64,
}

fn usage() -> &'static str {
    "usage:
  bigstar-net-bridge udp --local-bind ADDR --bridge-bind ADDR --bridge-peer ADDR [--local-target ADDR]
  bigstar-net-bridge webrtc-offer  --local-bind ADDR [--local-target ADDR] [--stun URI] [--signal URL --session ID] [--status-file PATH]
  bigstar-net-bridge webrtc-answer --local-bind ADDR [--local-target ADDR] [--stun URI] [--signal URL --session ID] [--status-file PATH]
  bigstar-net-bridge webrtc-loopback-smoke [--stun URI]
  bigstar-net-bridge webrtc-signaling-loopback-smoke [--stun URI]
  bigstar-net-bridge webrtc-signaling-udp-pair-smoke [--stun URI]

examples:
  host bridge:
    bigstar-net-bridge udp --local-bind 127.0.0.1:0 --local-target 127.0.0.1:8165 --bridge-bind 127.0.0.1:9001 --bridge-peer 127.0.0.1:9002

  client bridge:
    bigstar-net-bridge udp --local-bind 127.0.0.1:8265 --bridge-bind 127.0.0.1:9002 --bridge-peer 127.0.0.1:9001

  manual WebRTC offer side:
    bigstar-net-bridge webrtc-offer --local-bind 127.0.0.1:0 --local-target 127.0.0.1:8165

  manual WebRTC answer side:
    bigstar-net-bridge webrtc-answer --local-bind 127.0.0.1:8265

  signaling WebRTC offer side:
    bigstar-net-bridge webrtc-offer --local-bind 127.0.0.1:0 --local-target 127.0.0.1:8165 --signal wss://example.workers.dev/session --session test-room

  signaling WebRTC answer side:
    bigstar-net-bridge webrtc-answer --local-bind 127.0.0.1:8265 --signal wss://example.workers.dev/session --session test-room
"
}

fn parse_socket_addr(value: &str, name: &str) -> Result<SocketAddr, String> {
    value
        .parse::<SocketAddr>()
        .map_err(|err| format!("invalid {name} address {value:?}: {err}"))
}

fn take_arg(args: &[String], name: &str) -> Option<String> {
    args.windows(2)
        .find_map(|w| (w[0] == name).then(|| w[1].clone()))
}

fn parse_udp_config(args: &[String]) -> Result<UdpTunnelConfig, String> {
    let local_bind = take_arg(args, "--local-bind")
        .ok_or_else(|| "missing --local-bind".to_owned())
        .and_then(|v| parse_socket_addr(&v, "--local-bind"))?;
    let local_target = take_arg(args, "--local-target")
        .map(|v| parse_socket_addr(&v, "--local-target"))
        .transpose()?;
    let bridge_bind = take_arg(args, "--bridge-bind")
        .ok_or_else(|| "missing --bridge-bind".to_owned())
        .and_then(|v| parse_socket_addr(&v, "--bridge-bind"))?;
    let bridge_peer = take_arg(args, "--bridge-peer")
        .ok_or_else(|| "missing --bridge-peer".to_owned())
        .and_then(|v| parse_socket_addr(&v, "--bridge-peer"))?;

    Ok(UdpTunnelConfig {
        local_bind,
        local_target,
        bridge_bind,
        bridge_peer,
    })
}

fn parse_local_config(args: &[String]) -> Result<LocalWebRtcConfig, String> {
    let local_bind = take_arg(args, "--local-bind")
        .ok_or_else(|| "missing --local-bind".to_owned())
        .and_then(|v| parse_socket_addr(&v, "--local-bind"))?;
    let local_target = take_arg(args, "--local-target")
        .map(|v| parse_socket_addr(&v, "--local-target"))
        .transpose()?;
    let stun_servers = args
        .windows(2)
        .filter(|w| w[0] == "--stun")
        .map(|w| w[1].clone())
        .collect::<Vec<_>>();
    let signal = take_arg(args, "--signal");
    let session = take_arg(args, "--session");
    let signal_session = match (signal, session) {
        (Some(signal), Some(session)) => {
            validate_signal_session(&session)?;
            Some((signal, session))
        }
        (None, None) => None,
        _ => {
            return Err("--signal and --session must be specified together".to_owned());
        }
    };
    let stun_servers = if stun_servers.is_empty() && signal_session.is_none() {
        vec!["stun:stun.l.google.com:19302".to_owned()]
    } else {
        stun_servers
    };
    let status_file = take_arg(args, "--status-file").map(PathBuf::from);

    Ok(LocalWebRtcConfig {
        local_bind,
        local_target,
        stun_servers,
        signal_session,
        status_file,
    })
}

fn validate_signal_session(session: &str) -> Result<(), String> {
    let valid = !session.is_empty()
        && session.len() <= 64
        && session
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || b == b'_' || b == b'-');
    if valid {
        Ok(())
    } else {
        Err("session must match ^[A-Za-z0-9_-]{1,64}$".to_owned())
    }
}

fn run_udp_tunnel(config: UdpTunnelConfig) -> io::Result<()> {
    let local_socket = Arc::new(UdpSocket::bind(config.local_bind)?);
    let bridge_socket = Arc::new(UdpSocket::bind(config.bridge_bind)?);
    local_socket.set_nonblocking(false)?;
    bridge_socket.set_nonblocking(false)?;

    let local_addr = local_socket.local_addr()?;
    let bridge_addr = bridge_socket.local_addr()?;
    let local_target = Arc::new(Mutex::new(config.local_target));
    let stats = Arc::new(Mutex::new(Stats::default()));

    println!(
        "bigstar-net-bridge udp: local={} localTarget={} bridge={} bridgePeer={}",
        local_addr,
        config
            .local_target
            .map(|addr| addr.to_string())
            .unwrap_or_else(|| "learn".to_owned()),
        bridge_addr,
        config.bridge_peer
    );

    {
        let local_socket = Arc::clone(&local_socket);
        let bridge_socket = Arc::clone(&bridge_socket);
        let local_target = Arc::clone(&local_target);
        let stats = Arc::clone(&stats);
        let bridge_peer = config.bridge_peer;
        thread::spawn(move || {
            let mut buf = [0u8; MAX_DATAGRAM_SIZE];
            loop {
                match local_socket.recv_from(&mut buf) {
                    Ok((len, from)) => {
                        {
                            let mut target = local_target.lock().expect("local target lock");
                            if target.is_none() {
                                *target = Some(from);
                                println!("bigstar-net-bridge udp: learned local target {}", from);
                            }
                        }
                        if let Err(err) = bridge_socket.send_to(&buf[..len], bridge_peer) {
                            eprintln!("bigstar-net-bridge udp: send bridge failed: {err}");
                            continue;
                        }
                        let mut stats = stats.lock().expect("stats lock");
                        stats.app_to_bridge_packets += 1;
                        stats.app_to_bridge_bytes += len as u64;
                    }
                    Err(err) => {
                        eprintln!("bigstar-net-bridge udp: local recv failed: {err}");
                        thread::sleep(Duration::from_millis(10));
                    }
                }
            }
        });
    }

    {
        let local_socket = Arc::clone(&local_socket);
        let bridge_socket = Arc::clone(&bridge_socket);
        let local_target = Arc::clone(&local_target);
        let stats = Arc::clone(&stats);
        thread::spawn(move || {
            let mut buf = [0u8; MAX_DATAGRAM_SIZE];
            loop {
                match bridge_socket.recv_from(&mut buf) {
                    Ok((len, from)) => {
                        if from != config.bridge_peer {
                            eprintln!(
                                "bigstar-net-bridge udp: ignored datagram from unexpected bridge peer {}",
                                from
                            );
                            continue;
                        }
                        let target = *local_target.lock().expect("local target lock");
                        let Some(target) = target else {
                            let mut stats = stats.lock().expect("stats lock");
                            stats.dropped_no_local_target += 1;
                            continue;
                        };
                        if let Err(err) = local_socket.send_to(&buf[..len], target) {
                            eprintln!("bigstar-net-bridge udp: send local failed: {err}");
                            continue;
                        }
                        let mut stats = stats.lock().expect("stats lock");
                        stats.bridge_to_app_packets += 1;
                        stats.bridge_to_app_bytes += len as u64;
                    }
                    Err(err) => {
                        eprintln!("bigstar-net-bridge udp: bridge recv failed: {err}");
                        thread::sleep(Duration::from_millis(10));
                    }
                }
            }
        });
    }

    let start = Instant::now();
    loop {
        thread::sleep(Duration::from_secs(2));
        let stats = stats.lock().expect("stats lock");
        println!(
            "bigstar-net-bridge udp: t={:.1}s app->bridge={}pkts/{}B bridge->app={}pkts/{}B droppedNoLocalTarget={}",
            start.elapsed().as_secs_f32(),
            stats.app_to_bridge_packets,
            stats.app_to_bridge_bytes,
            stats.bridge_to_app_packets,
            stats.bridge_to_app_bytes,
            stats.dropped_no_local_target
        );
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args = env::args().skip(1).collect::<Vec<_>>();
    if args.is_empty() || args.iter().any(|arg| arg == "-h" || arg == "--help") {
        print!("{}", usage());
        return Ok(());
    }

    match args[0].as_str() {
        "udp" => {
            let config = parse_udp_config(&args[1..]).map_err(|err| {
                io::Error::new(io::ErrorKind::InvalidInput, format!("{err}\n{}", usage()))
            })?;
            run_udp_tunnel(config)?;
        }
        "webrtc-offer" | "webrtc-answer" => {
            let config = parse_local_config(&args[1..]).map_err(|err| {
                io::Error::new(io::ErrorKind::InvalidInput, format!("{err}\n{}", usage()))
            })?;
            match config.signal_session {
                Some((signal_url, session)) => run_signaling_webrtc(
                    args[0].as_str(),
                    config.local_bind,
                    config.local_target,
                    config.stun_servers,
                    signal_url,
                    session,
                    config.status_file,
                )?,
                None => run_manual_webrtc(
                    args[0].as_str(),
                    config.local_bind,
                    config.local_target,
                    config.stun_servers,
                    config.status_file,
                )?,
            }
        }
        "webrtc-loopback-smoke" => {
            let stun_servers = args[1..]
                .windows(2)
                .filter(|w| w[0] == "--stun")
                .map(|w| w[1].clone())
                .collect::<Vec<_>>();
            run_webrtc_loopback_smoke(stun_servers)?;
        }
        "webrtc-signaling-loopback-smoke" => {
            let stun_servers = args[1..]
                .windows(2)
                .filter(|w| w[0] == "--stun")
                .map(|w| w[1].clone())
                .collect::<Vec<_>>();
            run_webrtc_signaling_loopback_smoke(stun_servers)?;
        }
        "webrtc-signaling-udp-pair-smoke" => {
            let stun_servers = args[1..]
                .windows(2)
                .filter(|w| w[0] == "--stun")
                .map(|w| w[1].clone())
                .collect::<Vec<_>>();
            run_webrtc_signaling_udp_pair_smoke(stun_servers)?;
        }
        other => {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("unknown command {other:?}\n{}", usage()),
            )
            .into());
        }
    }

    Ok(())
}

#[cfg(not(feature = "webrtc"))]
fn run_webrtc_loopback_smoke(_stun_servers: Vec<String>) -> Result<(), Box<dyn std::error::Error>> {
    Err(io::Error::new(
        io::ErrorKind::Unsupported,
        "WebRTC loopback smoke requires building with --features webrtc",
    )
    .into())
}

#[cfg(not(feature = "webrtc"))]
fn run_webrtc_signaling_loopback_smoke(
    _stun_servers: Vec<String>,
) -> Result<(), Box<dyn std::error::Error>> {
    Err(io::Error::new(
        io::ErrorKind::Unsupported,
        "WebRTC signaling loopback smoke requires building with --features webrtc",
    )
    .into())
}

#[cfg(not(feature = "webrtc"))]
fn run_webrtc_signaling_udp_pair_smoke(
    _stun_servers: Vec<String>,
) -> Result<(), Box<dyn std::error::Error>> {
    Err(io::Error::new(
        io::ErrorKind::Unsupported,
        "WebRTC signaling UDP pair smoke requires building with --features webrtc",
    )
    .into())
}

#[cfg(feature = "webrtc")]
fn run_webrtc_loopback_smoke(stun_servers: Vec<String>) -> Result<(), Box<dyn std::error::Error>> {
    let runtime = tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()?;
    runtime.block_on(webrtc::run_loopback_smoke(stun_servers))
}

#[cfg(feature = "webrtc")]
fn run_webrtc_signaling_loopback_smoke(
    stun_servers: Vec<String>,
) -> Result<(), Box<dyn std::error::Error>> {
    let runtime = tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()?;
    runtime.block_on(webrtc::run_signaling_loopback_smoke(stun_servers))
}

#[cfg(feature = "webrtc")]
fn run_webrtc_signaling_udp_pair_smoke(
    stun_servers: Vec<String>,
) -> Result<(), Box<dyn std::error::Error>> {
    let runtime = tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()?;
    runtime.block_on(webrtc::run_signaling_udp_pair_smoke(stun_servers))
}

#[cfg(not(feature = "webrtc"))]
fn run_manual_webrtc(
    _side: &str,
    _local_bind: SocketAddr,
    _local_target: Option<SocketAddr>,
    _stun_servers: Vec<String>,
    _status_file: Option<PathBuf>,
) -> Result<(), Box<dyn std::error::Error>> {
    Err(io::Error::new(
        io::ErrorKind::Unsupported,
        "manual WebRTC mode requires building with --features webrtc",
    )
    .into())
}

#[cfg(not(feature = "webrtc"))]
fn run_signaling_webrtc(
    _side: &str,
    _local_bind: SocketAddr,
    _local_target: Option<SocketAddr>,
    _stun_servers: Vec<String>,
    _signal_url: String,
    _session: String,
    _status_file: Option<PathBuf>,
) -> Result<(), Box<dyn std::error::Error>> {
    Err(io::Error::new(
        io::ErrorKind::Unsupported,
        "signaling WebRTC mode requires building with --features webrtc",
    )
    .into())
}

#[cfg(feature = "webrtc")]
fn run_manual_webrtc(
    side: &str,
    local_bind: SocketAddr,
    local_target: Option<SocketAddr>,
    stun_servers: Vec<String>,
    status_file: Option<PathBuf>,
) -> Result<(), Box<dyn std::error::Error>> {
    let runtime = tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()?;
    runtime.block_on(webrtc::run_manual_webrtc(
        side.to_owned(),
        local_bind,
        local_target,
        stun_servers,
        status_file,
    ))
}

#[cfg(feature = "webrtc")]
fn run_signaling_webrtc(
    side: &str,
    local_bind: SocketAddr,
    local_target: Option<SocketAddr>,
    stun_servers: Vec<String>,
    signal_url: String,
    session: String,
    status_file: Option<PathBuf>,
) -> Result<(), Box<dyn std::error::Error>> {
    let runtime = tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()?;
    runtime.block_on(webrtc::run_signaling_webrtc(
        webrtc::SignalingWebRtcConfig {
            side: side.to_owned(),
            local_bind,
            local_target,
            stun_servers,
            signal_url,
            session,
            status_file,
            fallback_to_default_stun: true,
        },
    ))
}

#[cfg(feature = "webrtc")]
mod webrtc;
