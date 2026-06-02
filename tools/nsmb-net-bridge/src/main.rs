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
  nsmb-net-bridge udp --local-bind ADDR --bridge-bind ADDR --bridge-peer ADDR [--local-target ADDR]
  nsmb-net-bridge webrtc-offer  --local-bind ADDR [--local-target ADDR] [--stun URI] [--signal URL --session ID] [--status-file PATH]
  nsmb-net-bridge webrtc-answer --local-bind ADDR [--local-target ADDR] [--stun URI] [--signal URL --session ID] [--status-file PATH]
  nsmb-net-bridge webrtc-loopback-smoke [--stun URI]
  nsmb-net-bridge webrtc-signaling-loopback-smoke [--stun URI]
  nsmb-net-bridge webrtc-signaling-udp-pair-smoke [--stun URI]

examples:
  host bridge:
    nsmb-net-bridge udp --local-bind 127.0.0.1:0 --local-target 127.0.0.1:8165 --bridge-bind 127.0.0.1:9001 --bridge-peer 127.0.0.1:9002

  client bridge:
    nsmb-net-bridge udp --local-bind 127.0.0.1:8265 --bridge-bind 127.0.0.1:9002 --bridge-peer 127.0.0.1:9001

  manual WebRTC offer side:
    nsmb-net-bridge webrtc-offer --local-bind 127.0.0.1:0 --local-target 127.0.0.1:8165

  manual WebRTC answer side:
    nsmb-net-bridge webrtc-answer --local-bind 127.0.0.1:8265

  signaling WebRTC offer side:
    nsmb-net-bridge webrtc-offer --local-bind 127.0.0.1:0 --local-target 127.0.0.1:8165 --signal wss://example.workers.dev/session --session test-room

  signaling WebRTC answer side:
    nsmb-net-bridge webrtc-answer --local-bind 127.0.0.1:8265 --signal wss://example.workers.dev/session --session test-room
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

fn parse_local_config(
    args: &[String],
) -> Result<
    (
        SocketAddr,
        Option<SocketAddr>,
        Vec<String>,
        Option<(String, String)>,
        Option<PathBuf>,
    ),
    String,
> {
    let local_bind = take_arg(args, "--local-bind")
        .ok_or_else(|| "missing --local-bind".to_owned())
        .and_then(|v| parse_socket_addr(&v, "--local-bind"))?;
    let local_target = take_arg(args, "--local-target")
        .map(|v| parse_socket_addr(&v, "--local-target"))
        .transpose()?;
    let stun_servers = args
        .windows(2)
        .filter_map(|w| (w[0] == "--stun").then(|| w[1].clone()))
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

    Ok((
        local_bind,
        local_target,
        stun_servers,
        signal_session,
        status_file,
    ))
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
        "nsmb-net-bridge udp: local={} localTarget={} bridge={} bridgePeer={}",
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
                                println!("nsmb-net-bridge udp: learned local target {}", from);
                            }
                        }
                        if let Err(err) = bridge_socket.send_to(&buf[..len], bridge_peer) {
                            eprintln!("nsmb-net-bridge udp: send bridge failed: {err}");
                            continue;
                        }
                        let mut stats = stats.lock().expect("stats lock");
                        stats.app_to_bridge_packets += 1;
                        stats.app_to_bridge_bytes += len as u64;
                    }
                    Err(err) => {
                        eprintln!("nsmb-net-bridge udp: local recv failed: {err}");
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
                                "nsmb-net-bridge udp: ignored datagram from unexpected bridge peer {}",
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
                            eprintln!("nsmb-net-bridge udp: send local failed: {err}");
                            continue;
                        }
                        let mut stats = stats.lock().expect("stats lock");
                        stats.bridge_to_app_packets += 1;
                        stats.bridge_to_app_bytes += len as u64;
                    }
                    Err(err) => {
                        eprintln!("nsmb-net-bridge udp: bridge recv failed: {err}");
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
            "nsmb-net-bridge udp: t={:.1}s app->bridge={}pkts/{}B bridge->app={}pkts/{}B droppedNoLocalTarget={}",
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
            let (local_bind, local_target, stun_servers, signal_session, status_file) =
                parse_local_config(&args[1..]).map_err(|err| {
                    io::Error::new(io::ErrorKind::InvalidInput, format!("{err}\n{}", usage()))
                })?;
            match signal_session {
                Some((signal_url, session)) => run_signaling_webrtc(
                    args[0].as_str(),
                    local_bind,
                    local_target,
                    stun_servers,
                    signal_url,
                    session,
                    status_file,
                )?,
                None => run_manual_webrtc(
                    args[0].as_str(),
                    local_bind,
                    local_target,
                    stun_servers,
                    status_file,
                )?,
            }
        }
        "webrtc-loopback-smoke" => {
            let stun_servers = args[1..]
                .windows(2)
                .filter_map(|w| (w[0] == "--stun").then(|| w[1].clone()))
                .collect::<Vec<_>>();
            run_webrtc_loopback_smoke(stun_servers)?;
        }
        "webrtc-signaling-loopback-smoke" => {
            let stun_servers = args[1..]
                .windows(2)
                .filter_map(|w| (w[0] == "--stun").then(|| w[1].clone()))
                .collect::<Vec<_>>();
            run_webrtc_signaling_loopback_smoke(stun_servers)?;
        }
        "webrtc-signaling-udp-pair-smoke" => {
            let stun_servers = args[1..]
                .windows(2)
                .filter_map(|w| (w[0] == "--stun").then(|| w[1].clone()))
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
        side.to_owned(),
        local_bind,
        local_target,
        stun_servers,
        signal_url,
        session,
        status_file,
        true,
    ))
}

#[cfg(feature = "webrtc")]
mod webrtc {
    use super::{PathBuf, SocketAddr, MAX_DATAGRAM_SIZE};
    use base64::prelude::*;
    use env_logger::{Builder as LogBuilder, Env as LogEnv, Target as LogTarget};
    use futures_util::{SinkExt, StreamExt};
    use serde_json::json;
    use std::fs;
    use std::io::{self, Write};
    use std::net::UdpSocket;
    use std::sync::Arc;
    use std::time::{Duration, Instant};
    use tokio::net::{TcpListener, TcpStream, UdpSocket as TokioUdpSocket};
    use tokio::sync::Mutex as TokioMutex;
    use tokio_tungstenite::tungstenite::Message as WebSocketMessage;
    use tokio_tungstenite::{accept_async, connect_async};

    const DEFAULT_STUN_SERVER: &str = "stun:stun.l.google.com:19302";

    struct WebRtcEndpoint {
        data_channel: datachannel_wrapper::DataChannel,
        peer_connection: datachannel_wrapper::PeerConnection,
        event_rx: tokio::sync::mpsc::Receiver<datachannel_wrapper::PeerConnectionEvent>,
    }

    #[derive(Default)]
    struct PacketStats {
        app_to_webrtc_packets: u64,
        app_to_webrtc_bytes: u64,
        webrtc_to_app_packets: u64,
        webrtc_to_app_bytes: u64,
        dropped_no_local_target: u64,
    }

    struct DiagnosticsReporter {
        status_file: Option<PathBuf>,
        role: String,
        phase: String,
        signal_url: Option<String>,
        session: Option<String>,
        ice_servers: Vec<String>,
        connection_state: Option<String>,
        gathering_state: Option<String>,
        ice_state: Option<String>,
        local_candidates: Vec<String>,
        selected_local_candidate: Option<String>,
        selected_remote_candidate: Option<String>,
        selected_route: Option<String>,
        local_address: Option<String>,
        remote_address: Option<String>,
        last_error: Option<String>,
        started_at: Instant,
        stats: PacketStats,
    }

    impl DiagnosticsReporter {
        fn new(status_file: Option<PathBuf>, role: impl Into<String>) -> Self {
            let reporter = Self {
                status_file,
                role: role.into(),
                phase: "starting".to_owned(),
                signal_url: None,
                session: None,
                ice_servers: Vec::new(),
                connection_state: None,
                gathering_state: None,
                ice_state: None,
                local_candidates: Vec::new(),
                selected_local_candidate: None,
                selected_remote_candidate: None,
                selected_route: None,
                local_address: None,
                remote_address: None,
                last_error: None,
                started_at: Instant::now(),
                stats: PacketStats::default(),
            };
            reporter.persist();
            reporter
        }

        fn set_phase(&mut self, phase: &str) {
            self.phase = phase.to_owned();
            println!("nsmb-net-bridge diagnostics: phase={phase}");
            self.persist();
        }

        fn set_signaling(&mut self, url: &str, session: &str) {
            self.signal_url = Some(url.to_owned());
            self.session = Some(session.to_owned());
            println!(
                "nsmb-net-bridge diagnostics: signalingUrl={} session={} role={}",
                url, session, self.role
            );
            self.persist();
        }

        fn set_ice_servers(&mut self, servers: Vec<String>, source: &str) {
            self.ice_servers = servers;
            println!(
                "nsmb-net-bridge diagnostics: iceServers source={} values={:?}",
                source, self.ice_servers
            );
            self.persist();
        }

        fn observe_event(&mut self, event: &datachannel_wrapper::PeerConnectionEvent) {
            use datachannel_wrapper::PeerConnectionEvent;
            println!("nsmb-net-bridge webrtc: event {:?}", event);
            match event {
                PeerConnectionEvent::IceCandidate(candidate) => {
                    println!(
                        "nsmb-net-bridge webrtc: local candidate type={} mid={} value={}",
                        candidate_type(&candidate.candidate),
                        candidate.mid,
                        candidate.candidate
                    );
                    self.local_candidates.push(candidate.candidate.clone());
                }
                PeerConnectionEvent::ConnectionStateChange(state) => {
                    self.connection_state = Some(format!("{state:?}").to_lowercase());
                }
                PeerConnectionEvent::GatheringStateChange(state) => {
                    self.gathering_state = Some(format!("{state:?}").to_lowercase());
                }
                PeerConnectionEvent::IceStateChange(state) => {
                    self.ice_state = Some(format!("{state:?}").to_lowercase());
                }
                PeerConnectionEvent::SessionDescription(_)
                | PeerConnectionEvent::SignalingStateChange(_) => {}
            }
            self.persist();
        }

        fn observe_selected_pair(&mut self, endpoint: &WebRtcEndpoint) {
            self.local_address = endpoint.peer_connection.local_address();
            self.remote_address = endpoint.peer_connection.remote_address();
            if let Some(pair) = endpoint.peer_connection.selected_candidate_pair() {
                let route = selected_route(&pair.local, &pair.remote);
                if self.selected_local_candidate.as_deref() != Some(&pair.local)
                    || self.selected_remote_candidate.as_deref() != Some(&pair.remote)
                {
                    println!(
                        "nsmb-net-bridge webrtc: selected candidate pair route={} local={} remote={}",
                        route, pair.local, pair.remote
                    );
                }
                self.selected_local_candidate = Some(pair.local);
                self.selected_remote_candidate = Some(pair.remote);
                self.selected_route = Some(route.to_owned());
            }
            self.persist();
        }

        fn fail(&mut self, error: &dyn std::fmt::Display) {
            self.phase = "failed".to_owned();
            self.last_error = Some(error.to_string());
            eprintln!("nsmb-net-bridge diagnostics: failed: {error}");
            self.persist();
        }

        fn persist(&self) {
            let Some(path) = &self.status_file else {
                return;
            };
            let updated_at_unix_ms = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap_or_default()
                .as_millis();
            let value = json!({
                "version": 1,
                "updated_at_unix_ms": updated_at_unix_ms,
                "elapsed_seconds": self.started_at.elapsed().as_secs_f32(),
                "role": self.role,
                "phase": self.phase,
                "signal_url": self.signal_url,
                "session": self.session,
                "ice_servers": self.ice_servers,
                "connection_state": self.connection_state,
                "gathering_state": self.gathering_state,
                "ice_state": self.ice_state,
                "local_candidates": self.local_candidates,
                "selected_candidate_pair": self.selected_local_candidate.as_ref().zip(
                    self.selected_remote_candidate.as_ref()
                ).map(|(local, remote)| json!({
                    "route": self.selected_route,
                    "local_type": candidate_type(local),
                    "remote_type": candidate_type(remote),
                    "local": local,
                    "remote": remote,
                    "local_address": self.local_address,
                    "remote_address": self.remote_address,
                })),
                "stats": {
                    "app_to_webrtc_packets": self.stats.app_to_webrtc_packets,
                    "app_to_webrtc_bytes": self.stats.app_to_webrtc_bytes,
                    "webrtc_to_app_packets": self.stats.webrtc_to_app_packets,
                    "webrtc_to_app_bytes": self.stats.webrtc_to_app_bytes,
                    "dropped_no_local_target": self.stats.dropped_no_local_target,
                },
                "last_error": self.last_error,
            });
            if let Some(parent) = path.parent() {
                let _ = fs::create_dir_all(parent);
            }
            let temp = path.with_extension("json.tmp");
            let write_result = serde_json::to_vec_pretty(&value)
                .map_err(io::Error::other)
                .and_then(|bytes| fs::write(&temp, bytes));
            if let Err(error) = write_result {
                eprintln!(
                    "nsmb-net-bridge diagnostics: status write failed path={} error={error}",
                    path.display()
                );
                return;
            }
            if path.exists() {
                let _ = fs::remove_file(path);
            }
            if let Err(error) = fs::rename(&temp, path) {
                eprintln!(
                    "nsmb-net-bridge diagnostics: status rename failed path={} error={error}",
                    path.display()
                );
            }
        }
    }

    fn init_native_logging() {
        let _ = LogBuilder::from_env(LogEnv::default().default_filter_or("debug"))
            .format_timestamp_millis()
            .target(LogTarget::Stderr)
            .try_init();
    }

    fn candidate_type(candidate: &str) -> &str {
        let mut parts = candidate.split_whitespace();
        while let Some(part) = parts.next() {
            if part == "typ" {
                return parts.next().unwrap_or("unknown");
            }
        }
        "unknown"
    }

    fn candidate_address(candidate: &str) -> Option<&str> {
        candidate.split_whitespace().nth(4)
    }

    fn is_local_candidate_address(candidate: &str) -> bool {
        candidate_address(candidate)
            .and_then(|address| address.parse::<std::net::IpAddr>().ok())
            .is_some_and(|address| match address {
                std::net::IpAddr::V4(address) => {
                    address.is_private() || address.is_loopback() || address.is_link_local()
                }
                std::net::IpAddr::V6(address) => {
                    address.is_loopback()
                        || address.is_unicast_link_local()
                        || (address.segments()[0] & 0xfe00) == 0xfc00
                }
            })
    }

    fn selected_route(local: &str, remote: &str) -> &'static str {
        let local_type = candidate_type(local);
        let remote_type = candidate_type(remote);
        if local_type == "relay" || remote_type == "relay" {
            "turn-relay"
        } else if matches!(local_type, "srflx" | "prflx")
            || matches!(remote_type, "srflx" | "prflx")
        {
            "stun"
        } else if local_type == "host"
            && remote_type == "host"
            && is_local_candidate_address(local)
            && is_local_candidate_address(remote)
        {
            "local"
        } else if local_type == "host" && remote_type == "host" {
            "direct"
        } else {
            "unknown"
        }
    }

    async fn create_endpoint(
        stun_servers: Vec<String>,
        reporter: &mut DiagnosticsReporter,
    ) -> Result<WebRtcEndpoint, Box<dyn std::error::Error>> {
        init_native_logging();
        let rtc_config = datachannel_wrapper::RtcConfig::new(&stun_servers);
        let (mut peer_connection, mut event_rx) =
            datachannel_wrapper::PeerConnection::new(rtc_config)?;
        let data_channel = peer_connection.create_data_channel(
            "nsmb",
            datachannel_wrapper::DataChannelInit::default()
                .reliability(datachannel_wrapper::Reliability {
                    unordered: true,
                    unreliable: true,
                    max_packet_life_time: 0,
                    max_retransmits: 0,
                })
                .negotiated()
                .manual_stream()
                .stream(0),
        )?;

        reporter.set_phase("ice-gathering");
        tokio::time::timeout(Duration::from_secs(30), async {
            while let Some(event) = event_rx.recv().await {
                reporter.observe_event(&event);
                if matches!(
                    event,
                    datachannel_wrapper::PeerConnectionEvent::GatheringStateChange(
                        datachannel_wrapper::GatheringState::Complete
                    )
                ) {
                    return Ok::<(), io::Error>(());
                }
            }
            Err(io::Error::new(
                io::ErrorKind::UnexpectedEof,
                "WebRTC event stream closed during ICE gathering",
            ))
        })
        .await
        .map_err(|_| io::Error::new(io::ErrorKind::TimedOut, "ICE gathering timed out"))??;

        Ok(WebRtcEndpoint {
            data_channel,
            peer_connection,
            event_rx,
        })
    }

    fn encode_sdp(sdp: &str) -> String {
        BASE64_STANDARD.encode(sdp.as_bytes())
    }

    fn decode_sdp(encoded: &str) -> Result<String, Box<dyn std::error::Error>> {
        let bytes = BASE64_STANDARD.decode(encoded.trim())?;
        Ok(String::from_utf8(bytes)?)
    }

    fn read_pasted_sdp(prompt: &str) -> Result<String, Box<dyn std::error::Error>> {
        println!("{prompt}");
        print!("> ");
        io::stdout().flush()?;
        let mut line = String::new();
        io::stdin().read_line(&mut line)?;
        decode_sdp(&line)
    }

    fn print_sdp(label: &str, sdp: &str) {
        println!("==== {label} SDP base64 begin ====");
        println!("{}", encode_sdp(sdp));
        println!("==== {label} SDP base64 end ====");
        println!("==== {label} SDP text begin ====");
        println!("{sdp}");
        println!("==== {label} SDP text end ====");
    }

    fn role_from_side(side: &str) -> &'static str {
        match side {
            "webrtc-offer" => "offer",
            "webrtc-answer" => "answer",
            _ => unreachable!("validated by caller"),
        }
    }

    fn sdp_type_from_str(value: &str) -> Result<datachannel_wrapper::SdpType, io::Error> {
        match value {
            "offer" => Ok(datachannel_wrapper::SdpType::Offer),
            "answer" => Ok(datachannel_wrapper::SdpType::Answer),
            other => Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("unsupported SDP type {other:?}"),
            )),
        }
    }

    fn build_signal_url(base: &str, session: &str, role: &str) -> String {
        let separator = if base.contains('?') { '&' } else { '?' };
        format!("{base}{separator}session={session}&role={role}")
    }

    fn parse_server_ice_servers(value: &serde_json::Value) -> Vec<String> {
        value
            .get("iceServers")
            .and_then(|servers| servers.as_array())
            .map(|servers| {
                servers
                    .iter()
                    .filter_map(|server| server.as_str().map(ToOwned::to_owned))
                    .collect::<Vec<_>>()
            })
            .unwrap_or_default()
    }

    async fn wait_signal_hello(
        ws: &mut tokio_tungstenite::WebSocketStream<
            tokio_tungstenite::MaybeTlsStream<tokio::net::TcpStream>,
        >,
    ) -> Result<serde_json::Value, Box<dyn std::error::Error>> {
        loop {
            let Some(message) = ws.next().await else {
                return Err(io::Error::new(
                    io::ErrorKind::UnexpectedEof,
                    "signaling socket closed",
                )
                .into());
            };
            let message = message?;
            let WebSocketMessage::Text(text) = message else {
                continue;
            };
            let value = serde_json::from_str::<serde_json::Value>(&text)?;
            match value.get("type").and_then(|v| v.as_str()) {
                Some("hello") => {
                    println!("nsmb-net-bridge signaling: {}", value);
                    return Ok(value);
                }
                Some("peer-joined") | Some("pong") => {
                    println!("nsmb-net-bridge signaling: {}", value);
                }
                Some("error") => {
                    return Err(io::Error::new(
                        io::ErrorKind::ConnectionAborted,
                        format!("signaling server error: {value}"),
                    )
                    .into());
                }
                other => {
                    println!(
                        "nsmb-net-bridge signaling: ignored message type {:?}",
                        other
                    );
                }
            }
        }
    }

    async fn wait_signal_sdp(
        ws: &mut tokio_tungstenite::WebSocketStream<
            tokio_tungstenite::MaybeTlsStream<tokio::net::TcpStream>,
        >,
        expected_sdp_type: &str,
    ) -> Result<serde_json::Value, Box<dyn std::error::Error>> {
        loop {
            let Some(message) = ws.next().await else {
                return Err(io::Error::new(
                    io::ErrorKind::UnexpectedEof,
                    "signaling socket closed",
                )
                .into());
            };
            let message = message?;
            let WebSocketMessage::Text(text) = message else {
                continue;
            };
            let value = serde_json::from_str::<serde_json::Value>(&text)?;
            match value.get("type").and_then(|v| v.as_str()) {
                Some("hello") | Some("peer-joined") | Some("pong") => {
                    println!("nsmb-net-bridge signaling: {}", value);
                }
                Some("error") => {
                    return Err(io::Error::new(
                        io::ErrorKind::ConnectionAborted,
                        format!("signaling server error: {value}"),
                    )
                    .into());
                }
                Some("sdp") => {
                    if value.get("sdpType").and_then(|v| v.as_str()) == Some(expected_sdp_type) {
                        return Ok(value);
                    }
                }
                other => {
                    println!(
                        "nsmb-net-bridge signaling: ignored message type {:?}",
                        other
                    );
                }
            }
        }
    }

    async fn wait_connected(
        endpoint: &mut WebRtcEndpoint,
        reporter: &mut DiagnosticsReporter,
    ) -> Result<(), Box<dyn std::error::Error>> {
        reporter.set_phase("webrtc-connecting");
        tokio::time::timeout(Duration::from_secs(30), async {
            loop {
                let Some(event) = endpoint.event_rx.recv().await else {
                    return Err(io::Error::new(
                        io::ErrorKind::UnexpectedEof,
                        "WebRTC event stream closed",
                    ));
                };
                reporter.observe_event(&event);
                if let datachannel_wrapper::PeerConnectionEvent::ConnectionStateChange(state) =
                    event
                {
                    match state {
                        datachannel_wrapper::ConnectionState::Connected => {
                            reporter.set_phase("connected");
                            reporter.observe_selected_pair(endpoint);
                            return Ok(());
                        }
                        datachannel_wrapper::ConnectionState::Disconnected => {
                            return Err(io::Error::new(
                                io::ErrorKind::ConnectionAborted,
                                "WebRTC disconnected",
                            ))
                        }
                        datachannel_wrapper::ConnectionState::Failed => {
                            return Err(io::Error::new(
                                io::ErrorKind::ConnectionAborted,
                                "WebRTC failed",
                            ))
                        }
                        datachannel_wrapper::ConnectionState::Closed => {
                            return Err(io::Error::new(
                                io::ErrorKind::ConnectionAborted,
                                "WebRTC closed",
                            ))
                        }
                        _ => {}
                    }
                }
            }
        })
        .await
        .map_err(|_| io::Error::new(io::ErrorKind::TimedOut, "WebRTC connect timed out"))??;
        Ok(())
    }

    async fn connect_offer(
        stun_servers: Vec<String>,
        reporter: &mut DiagnosticsReporter,
    ) -> Result<WebRtcEndpoint, Box<dyn std::error::Error>> {
        reporter.set_ice_servers(stun_servers.clone(), "cli-or-manual-default");
        let mut endpoint = create_endpoint(stun_servers, reporter).await?;
        let offer = endpoint
            .peer_connection
            .local_description()
            .ok_or_else(|| io::Error::new(io::ErrorKind::Other, "missing local offer SDP"))?;
        print_sdp("offer", &offer.sdp.to_string());
        let answer_sdp = read_pasted_sdp("Paste answer SDP base64 from the answer side.")?;
        endpoint.peer_connection.set_remote_description(
            datachannel_wrapper::SessionDescription {
                sdp_type: datachannel_wrapper::SdpType::Answer,
                sdp: datachannel_wrapper::sdp::parse_sdp(&answer_sdp, false)?,
            },
        )?;
        print_sdp("remote answer", &answer_sdp);
        wait_connected(&mut endpoint, reporter).await?;
        Ok(endpoint)
    }

    async fn connect_answer(
        stun_servers: Vec<String>,
        reporter: &mut DiagnosticsReporter,
    ) -> Result<WebRtcEndpoint, Box<dyn std::error::Error>> {
        let offer_sdp = read_pasted_sdp("Paste offer SDP base64 from the offer side.")?;
        print_sdp("remote offer", &offer_sdp);
        reporter.set_ice_servers(stun_servers.clone(), "cli-or-manual-default");
        let mut endpoint = create_endpoint(stun_servers, reporter).await?;
        endpoint
            .peer_connection
            .set_local_description(datachannel_wrapper::SdpType::Rollback)?;
        endpoint.peer_connection.set_remote_description(
            datachannel_wrapper::SessionDescription {
                sdp_type: datachannel_wrapper::SdpType::Offer,
                sdp: datachannel_wrapper::sdp::parse_sdp(&offer_sdp, false)?,
            },
        )?;
        let answer = endpoint
            .peer_connection
            .local_description()
            .ok_or_else(|| io::Error::new(io::ErrorKind::Other, "missing local answer SDP"))?;
        print_sdp("answer", &answer.sdp.to_string());
        wait_connected(&mut endpoint, reporter).await?;
        Ok(endpoint)
    }

    async fn connect_signal_offer(
        signal_url: String,
        session: String,
        stun_servers: Vec<String>,
        reporter: &mut DiagnosticsReporter,
        fallback_to_default_stun: bool,
    ) -> Result<WebRtcEndpoint, Box<dyn std::error::Error>> {
        let url = build_signal_url(&signal_url, &session, "offer");
        reporter.set_signaling(&signal_url, &session);
        reporter.set_phase("signaling-connecting");
        let (mut ws, _) = connect_async(&url).await?;
        println!("nsmb-net-bridge signaling: connected {}", url);
        reporter.set_phase("signaling-connected");

        let hello = wait_signal_hello(&mut ws).await?;
        let server_ice_servers = parse_server_ice_servers(&hello);
        let (stun_servers, source) = if !stun_servers.is_empty() {
            (stun_servers, "cli")
        } else if !server_ice_servers.is_empty() {
            (server_ice_servers, "signaling-server")
        } else if fallback_to_default_stun {
            (vec![DEFAULT_STUN_SERVER.to_owned()], "bridge-default")
        } else {
            (Vec::new(), "disabled-for-smoke")
        };
        reporter.set_ice_servers(stun_servers.clone(), source);

        let mut endpoint = create_endpoint(stun_servers, reporter).await?;
        let offer = endpoint
            .peer_connection
            .local_description()
            .ok_or_else(|| io::Error::new(io::ErrorKind::Other, "missing local offer SDP"))?;
        ws.send(WebSocketMessage::Text(
            serde_json::json!({
                "type": "sdp",
                "sdpType": "offer",
                "sdp": offer.sdp.to_string(),
            })
            .to_string()
            .into(),
        ))
        .await?;
        println!("nsmb-net-bridge signaling: sent offer SDP");
        print_sdp("local offer", &offer.sdp.to_string());

        reporter.set_phase("waiting-answer-sdp");
        let answer = wait_signal_sdp(&mut ws, "answer").await?;
        let answer_sdp = answer
            .get("sdp")
            .and_then(|v| v.as_str())
            .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidData, "missing answer SDP"))?;
        endpoint.peer_connection.set_remote_description(
            datachannel_wrapper::SessionDescription {
                sdp_type: sdp_type_from_str(
                    answer
                        .get("sdpType")
                        .and_then(|v| v.as_str())
                        .unwrap_or("answer"),
                )?,
                sdp: datachannel_wrapper::sdp::parse_sdp(answer_sdp, false)?,
            },
        )?;
        print_sdp("remote answer", answer_sdp);
        wait_connected(&mut endpoint, reporter).await?;
        Ok(endpoint)
    }

    async fn connect_signal_answer(
        signal_url: String,
        session: String,
        stun_servers: Vec<String>,
        reporter: &mut DiagnosticsReporter,
        fallback_to_default_stun: bool,
    ) -> Result<WebRtcEndpoint, Box<dyn std::error::Error>> {
        let url = build_signal_url(&signal_url, &session, "answer");
        reporter.set_signaling(&signal_url, &session);
        reporter.set_phase("signaling-connecting");
        let (mut ws, _) = connect_async(&url).await?;
        println!("nsmb-net-bridge signaling: connected {}", url);
        reporter.set_phase("signaling-connected");

        let hello = wait_signal_hello(&mut ws).await?;
        let server_ice_servers = parse_server_ice_servers(&hello);
        let (stun_servers, source) = if !stun_servers.is_empty() {
            (stun_servers, "cli")
        } else if !server_ice_servers.is_empty() {
            (server_ice_servers, "signaling-server")
        } else if fallback_to_default_stun {
            (vec![DEFAULT_STUN_SERVER.to_owned()], "bridge-default")
        } else {
            (Vec::new(), "disabled-for-smoke")
        };
        reporter.set_ice_servers(stun_servers.clone(), source);

        reporter.set_phase("waiting-offer-sdp");
        let offer = wait_signal_sdp(&mut ws, "offer").await?;
        let offer_sdp = offer
            .get("sdp")
            .and_then(|v| v.as_str())
            .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidData, "missing offer SDP"))?;

        print_sdp("remote offer", offer_sdp);
        let mut endpoint = create_endpoint(stun_servers, reporter).await?;
        endpoint
            .peer_connection
            .set_local_description(datachannel_wrapper::SdpType::Rollback)?;
        endpoint.peer_connection.set_remote_description(
            datachannel_wrapper::SessionDescription {
                sdp_type: sdp_type_from_str(
                    offer
                        .get("sdpType")
                        .and_then(|v| v.as_str())
                        .unwrap_or("offer"),
                )?,
                sdp: datachannel_wrapper::sdp::parse_sdp(offer_sdp, false)?,
            },
        )?;
        let answer = endpoint
            .peer_connection
            .local_description()
            .ok_or_else(|| io::Error::new(io::ErrorKind::Other, "missing local answer SDP"))?;
        ws.send(WebSocketMessage::Text(
            serde_json::json!({
                "type": "sdp",
                "sdpType": "answer",
                "sdp": answer.sdp.to_string(),
            })
            .to_string()
            .into(),
        ))
        .await?;
        println!("nsmb-net-bridge signaling: sent answer SDP");
        print_sdp("local answer", &answer.sdp.to_string());

        wait_connected(&mut endpoint, reporter).await?;
        Ok(endpoint)
    }

    pub async fn run_manual_webrtc(
        side: String,
        local_bind: SocketAddr,
        local_target: Option<SocketAddr>,
        stun_servers: Vec<String>,
        status_file: Option<PathBuf>,
    ) -> Result<(), Box<dyn std::error::Error>> {
        let role = role_from_side(&side);
        let mut reporter = DiagnosticsReporter::new(status_file, role);
        println!(
            "nsmb-net-bridge webrtc: start mode=manual role={} localBind={} localTarget={}",
            role,
            local_bind,
            local_target
                .map(|value| value.to_string())
                .unwrap_or_else(|| "learn".to_owned())
        );
        let endpoint = match side.as_str() {
            "webrtc-offer" => connect_offer(stun_servers, &mut reporter).await,
            "webrtc-answer" => connect_answer(stun_servers, &mut reporter).await,
            _ => unreachable!("validated by caller"),
        };
        let endpoint = match endpoint {
            Ok(endpoint) => endpoint,
            Err(error) => {
                reporter.fail(error.as_ref());
                return Err(error);
            }
        };
        let result = run_webrtc_udp_tunnel(endpoint, local_bind, local_target, &mut reporter).await;
        if let Err(error) = &result {
            reporter.fail(error.as_ref());
        }
        result
    }

    pub async fn run_signaling_webrtc(
        side: String,
        local_bind: SocketAddr,
        local_target: Option<SocketAddr>,
        stun_servers: Vec<String>,
        signal_url: String,
        session: String,
        status_file: Option<PathBuf>,
        fallback_to_default_stun: bool,
    ) -> Result<(), Box<dyn std::error::Error>> {
        let role = role_from_side(&side);
        let mut reporter = DiagnosticsReporter::new(status_file, role);
        println!(
            "nsmb-net-bridge webrtc: start mode=signaling role={} localBind={} localTarget={} signalUrl={} session={}",
            role,
            local_bind,
            local_target
                .map(|value| value.to_string())
                .unwrap_or_else(|| "learn".to_owned()),
            signal_url,
            session
        );
        let endpoint = {
            let endpoint_result = match role_from_side(&side) {
                "offer" => {
                    connect_signal_offer(
                        signal_url,
                        session,
                        stun_servers,
                        &mut reporter,
                        fallback_to_default_stun,
                    )
                    .await
                }
                "answer" => {
                    connect_signal_answer(
                        signal_url,
                        session,
                        stun_servers,
                        &mut reporter,
                        fallback_to_default_stun,
                    )
                    .await
                }
                _ => unreachable!("validated by caller"),
            };
            match endpoint_result {
                Ok(endpoint) => endpoint,
                Err(error) => {
                    reporter.fail(error.as_ref());
                    return Err(error);
                }
            }
        };
        let result = run_webrtc_udp_tunnel(endpoint, local_bind, local_target, &mut reporter).await;
        if let Err(error) = &result {
            reporter.fail(error.as_ref());
        }
        result
    }

    pub async fn run_loopback_smoke(
        stun_servers: Vec<String>,
    ) -> Result<(), Box<dyn std::error::Error>> {
        let stun_servers = if stun_servers.is_empty() {
            Vec::new()
        } else {
            stun_servers
        };
        let mut offer_reporter = DiagnosticsReporter::new(None, "smoke-offer");
        let mut answer_reporter = DiagnosticsReporter::new(None, "smoke-answer");
        let mut offer = create_endpoint(stun_servers.clone(), &mut offer_reporter).await?;
        let mut answer = create_endpoint(stun_servers, &mut answer_reporter).await?;

        let offer_sdp = offer
            .peer_connection
            .local_description()
            .ok_or_else(|| io::Error::new(io::ErrorKind::Other, "missing loopback offer SDP"))?
            .sdp
            .to_string();
        answer
            .peer_connection
            .set_local_description(datachannel_wrapper::SdpType::Rollback)?;
        answer
            .peer_connection
            .set_remote_description(datachannel_wrapper::SessionDescription {
                sdp_type: datachannel_wrapper::SdpType::Offer,
                sdp: datachannel_wrapper::sdp::parse_sdp(&offer_sdp, false)?,
            })?;

        let answer_sdp = answer
            .peer_connection
            .local_description()
            .ok_or_else(|| io::Error::new(io::ErrorKind::Other, "missing loopback answer SDP"))?
            .sdp
            .to_string();
        offer
            .peer_connection
            .set_remote_description(datachannel_wrapper::SessionDescription {
                sdp_type: datachannel_wrapper::SdpType::Answer,
                sdp: datachannel_wrapper::sdp::parse_sdp(&answer_sdp, false)?,
            })?;

        tokio::try_join!(
            wait_connected(&mut offer, &mut offer_reporter),
            wait_connected(&mut answer, &mut answer_reporter)
        )?;

        let (mut offer_tx, _offer_rx) = offer.data_channel.split();
        let (_answer_tx, mut answer_rx) = answer.data_channel.split();
        offer_tx.send(b"nsmb-net-bridge-smoke").await?;
        let Some(received) = answer_rx.receive().await else {
            return Err(io::Error::new(
                io::ErrorKind::UnexpectedEof,
                "loopback data channel closed",
            )
            .into());
        };
        if received != b"nsmb-net-bridge-smoke" {
            return Err(
                io::Error::new(io::ErrorKind::InvalidData, "loopback payload mismatch").into(),
            );
        }

        println!("nsmb-net-bridge webrtc: loopback smoke passed");
        Ok(())
    }

    async fn run_local_signaling_server(
        listener: TcpListener,
    ) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
        let (first, _) = listener.accept().await?;
        let (second, _) = listener.accept().await?;
        let first = accept_async(first).await?;
        let second = accept_async(second).await?;
        let (mut first_tx, first_rx) = first.split();
        let (mut second_tx, second_rx) = second.split();

        let hello = serde_json::json!({
            "type": "hello",
            "role": "loopback",
            "session": "loopback",
            "peerCount": 2,
            "iceServers": [],
        })
        .to_string();
        first_tx
            .send(WebSocketMessage::Text(hello.clone().into()))
            .await?;
        second_tx.send(WebSocketMessage::Text(hello.into())).await?;

        let first_to_second = relay_signaling_messages(first_rx, second_tx);
        let second_to_first = relay_signaling_messages(second_rx, first_tx);
        let _ = tokio::join!(first_to_second, second_to_first);
        Ok(())
    }

    async fn relay_signaling_messages(
        mut rx: futures_util::stream::SplitStream<tokio_tungstenite::WebSocketStream<TcpStream>>,
        mut tx: futures_util::stream::SplitSink<
            tokio_tungstenite::WebSocketStream<TcpStream>,
            WebSocketMessage,
        >,
    ) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
        while let Some(message) = rx.next().await {
            tx.send(message?).await?;
        }
        Ok(())
    }

    pub async fn run_signaling_loopback_smoke(
        stun_servers: Vec<String>,
    ) -> Result<(), Box<dyn std::error::Error>> {
        let listener = TcpListener::bind("127.0.0.1:0").await?;
        let signal_url = format!("ws://{}/session", listener.local_addr()?);
        let server = tokio::spawn(run_local_signaling_server(listener));
        let mut offer_reporter = DiagnosticsReporter::new(None, "smoke-offer");
        let mut answer_reporter = DiagnosticsReporter::new(None, "smoke-answer");
        let offer = connect_signal_offer(
            signal_url.clone(),
            "loopback".to_owned(),
            stun_servers.clone(),
            &mut offer_reporter,
            false,
        );
        let answer = connect_signal_answer(
            signal_url,
            "loopback".to_owned(),
            stun_servers,
            &mut answer_reporter,
            false,
        );

        let (offer, answer) = tokio::time::timeout(Duration::from_secs(30), async {
            tokio::try_join!(offer, answer)
        })
        .await
        .map_err(|_| io::Error::new(io::ErrorKind::TimedOut, "signaling loopback timed out"))??;

        let (mut offer_tx, _offer_rx) = offer.data_channel.split();
        let (_answer_tx, mut answer_rx) = answer.data_channel.split();
        offer_tx.send(b"nsmb-net-bridge-signaling-smoke").await?;
        let Some(received) = answer_rx.receive().await else {
            return Err(io::Error::new(
                io::ErrorKind::UnexpectedEof,
                "signaling loopback data channel closed",
            )
            .into());
        };
        if received != b"nsmb-net-bridge-signaling-smoke" {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "signaling loopback payload mismatch",
            )
            .into());
        }

        server.abort();
        println!("nsmb-net-bridge webrtc: signaling loopback smoke passed");
        Ok(())
    }

    fn reserve_loopback_udp_addr() -> io::Result<SocketAddr> {
        UdpSocket::bind("127.0.0.1:0")?.local_addr()
    }

    async fn wait_for_udp_payload(
        sender: &TokioUdpSocket,
        bridge_addr: SocketAddr,
        receiver: &TokioUdpSocket,
        payload: &[u8],
        label: &str,
    ) -> Result<(), Box<dyn std::error::Error>> {
        let mut tick = tokio::time::interval(Duration::from_millis(100));
        let mut buf = [0u8; MAX_DATAGRAM_SIZE];
        tokio::time::timeout(Duration::from_secs(15), async {
            loop {
                tokio::select! {
                    _ = tick.tick() => {
                        sender.send_to(payload, bridge_addr).await?;
                    }
                    recv = receiver.recv_from(&mut buf) => {
                        match recv {
                            Ok((len, _)) if &buf[..len] == payload => {
                                return Ok::<(), io::Error>(());
                            }
                            Ok(_) => {}
                            Err(err) if err.kind() == io::ErrorKind::ConnectionReset => {}
                            Err(err) => return Err(err),
                        }
                    }
                }
            }
        })
        .await
        .map_err(|_| {
            io::Error::new(
                io::ErrorKind::TimedOut,
                format!("{label} UDP payload timed out"),
            )
        })??;
        Ok(())
    }

    pub async fn run_signaling_udp_pair_smoke(
        stun_servers: Vec<String>,
    ) -> Result<(), Box<dyn std::error::Error>> {
        let listener = TcpListener::bind("127.0.0.1:0").await?;
        let signal_url = format!("ws://{}/session", listener.local_addr()?);
        let server = tokio::spawn(run_local_signaling_server(listener));
        let offer_bridge_addr = reserve_loopback_udp_addr()?;
        let answer_bridge_addr = reserve_loopback_udp_addr()?;
        let host_app = TokioUdpSocket::bind("127.0.0.1:0").await?;
        let client_app = TokioUdpSocket::bind("127.0.0.1:0").await?;
        let host_app_addr = host_app.local_addr()?;
        let session = "udp-pair-smoke".to_owned();

        let offer_stun = stun_servers.clone();
        let offer_signal = signal_url.clone();
        let offer_session = session.clone();
        let offer_task = tokio::spawn(async move {
            if let Err(err) = run_signaling_webrtc(
                "webrtc-offer".to_owned(),
                offer_bridge_addr,
                Some(host_app_addr),
                offer_stun,
                offer_signal,
                offer_session,
                None,
                false,
            )
            .await
            {
                eprintln!("nsmb-net-bridge webrtc: offer smoke task failed: {err}");
            }
        });

        let answer_task = tokio::spawn(async move {
            if let Err(err) = run_signaling_webrtc(
                "webrtc-answer".to_owned(),
                answer_bridge_addr,
                None,
                stun_servers,
                signal_url,
                session,
                None,
                false,
            )
            .await
            {
                eprintln!("nsmb-net-bridge webrtc: answer smoke task failed: {err}");
            }
        });

        wait_for_udp_payload(
            &client_app,
            answer_bridge_addr,
            &host_app,
            b"nsmb-net-bridge-client-to-host",
            "client-to-host",
        )
        .await?;
        wait_for_udp_payload(
            &host_app,
            offer_bridge_addr,
            &client_app,
            b"nsmb-net-bridge-host-to-client",
            "host-to-client",
        )
        .await?;

        offer_task.abort();
        answer_task.abort();
        server.abort();
        println!("nsmb-net-bridge webrtc: signaling udp pair smoke passed");
        Ok(())
    }

    async fn run_webrtc_udp_tunnel(
        endpoint: WebRtcEndpoint,
        local_bind: SocketAddr,
        local_target: Option<SocketAddr>,
        reporter: &mut DiagnosticsReporter,
    ) -> Result<(), Box<dyn std::error::Error>> {
        let local_socket = TokioUdpSocket::bind(local_bind).await?;
        let local_addr = local_socket.local_addr()?;
        let local_target = Arc::new(TokioMutex::new(local_target));
        let WebRtcEndpoint {
            data_channel,
            peer_connection,
            mut event_rx,
        } = endpoint;
        let (mut dc_tx, mut dc_rx) = data_channel.split();
        let mut app_buf = [0u8; MAX_DATAGRAM_SIZE];
        let start = Instant::now();
        let mut stats_tick = tokio::time::interval(Duration::from_secs(2));

        let initial_target = {
            let target = local_target.lock().await;
            target
                .map(|addr| addr.to_string())
                .unwrap_or_else(|| "learn".to_owned())
        };
        println!(
            "nsmb-net-bridge webrtc: connected local={} localTarget={}",
            local_addr, initial_target
        );

        loop {
            tokio::select! {
                recv = local_socket.recv_from(&mut app_buf) => {
                    let (len, from) = recv?;
                    {
                        let mut target = local_target.lock().await;
                        if target.is_none() {
                            *target = Some(from);
                            println!("nsmb-net-bridge webrtc: learned local target {}", from);
                        }
                    }
                    dc_tx.send(&app_buf[..len]).await?;
                    reporter.stats.app_to_webrtc_packets += 1;
                    reporter.stats.app_to_webrtc_bytes += len as u64;
                }
                msg = dc_rx.receive() => {
                    let Some(msg) = msg else {
                        return Err(io::Error::new(io::ErrorKind::UnexpectedEof, "WebRTC data channel closed").into());
                    };
                    let target = *local_target.lock().await;
                    let Some(target) = target else {
                        reporter.stats.dropped_no_local_target += 1;
                        continue;
                    };
                    local_socket.send_to(&msg, target).await?;
                    reporter.stats.webrtc_to_app_packets += 1;
                    reporter.stats.webrtc_to_app_bytes += msg.len() as u64;
                }
                event = event_rx.recv() => {
                    let Some(event) = event else {
                        return Err(io::Error::new(io::ErrorKind::UnexpectedEof, "WebRTC event stream closed").into());
                    };
                    reporter.observe_event(&event);
                    if let datachannel_wrapper::PeerConnectionEvent::ConnectionStateChange(state) = event {
                        if matches!(
                            state,
                            datachannel_wrapper::ConnectionState::Disconnected
                                | datachannel_wrapper::ConnectionState::Failed
                                | datachannel_wrapper::ConnectionState::Closed
                        ) {
                            return Err(io::Error::new(
                                io::ErrorKind::ConnectionAborted,
                                format!("WebRTC connection ended: {state:?}"),
                            ).into());
                        }
                    }
                }
                _ = stats_tick.tick() => {
                    println!(
                        "nsmb-net-bridge webrtc: t={:.1}s app->rtc={}pkts/{}B rtc->app={}pkts/{}B droppedNoLocalTarget={}",
                        start.elapsed().as_secs_f32(),
                        reporter.stats.app_to_webrtc_packets,
                        reporter.stats.app_to_webrtc_bytes,
                        reporter.stats.webrtc_to_app_packets,
                        reporter.stats.webrtc_to_app_bytes,
                        reporter.stats.dropped_no_local_target
                    );
                    reporter.local_address = peer_connection.local_address();
                    reporter.remote_address = peer_connection.remote_address();
                    if let Some(pair) = peer_connection.selected_candidate_pair() {
                        reporter.selected_route = Some(selected_route(&pair.local, &pair.remote).to_owned());
                        reporter.selected_local_candidate = Some(pair.local);
                        reporter.selected_remote_candidate = Some(pair.remote);
                    }
                    reporter.persist();
                }
            }
        }
    }

    #[cfg(test)]
    mod tests {
        use super::{candidate_type, selected_route};

        #[test]
        fn classifies_candidate_types_and_routes() {
            let private_host = "a=candidate:1 1 UDP 1 192.168.0.10 5000 typ host";
            let private_peer = "a=candidate:2 1 UDP 1 192.168.0.11 5001 typ host";
            let public_host = "a=candidate:3 1 UDP 1 2001:db8::10 5000 typ host";
            let public_peer = "a=candidate:4 1 UDP 1 2001:db8::11 5001 typ host";
            let srflx = "a=candidate:5 1 UDP 1 203.0.113.10 5002 typ srflx";
            let relay = "a=candidate:6 1 UDP 1 203.0.113.20 5003 typ relay";

            assert_eq!(candidate_type(srflx), "srflx");
            assert_eq!(selected_route(private_host, private_peer), "local");
            assert_eq!(selected_route(public_host, public_peer), "direct");
            assert_eq!(selected_route(private_host, srflx), "stun");
            assert_eq!(selected_route(private_host, relay), "turn-relay");
        }
    }
}
