use std::env;
use std::io;
use std::net::{SocketAddr, UdpSocket};
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
  nsmb-net-bridge webrtc-offer  --local-bind ADDR [--local-target ADDR] [--stun URI] [--signal URL --session ID]
  nsmb-net-bridge webrtc-answer --local-bind ADDR [--local-target ADDR] [--stun URI] [--signal URL --session ID]
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

    Ok((local_bind, local_target, stun_servers, signal_session))
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
            let (local_bind, local_target, stun_servers, signal_session) =
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
                )?,
                None => {
                    run_manual_webrtc(args[0].as_str(), local_bind, local_target, stun_servers)?
                }
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
) -> Result<(), Box<dyn std::error::Error>> {
    let runtime = tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()?;
    runtime.block_on(webrtc::run_manual_webrtc(
        side.to_owned(),
        local_bind,
        local_target,
        stun_servers,
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
    ))
}

#[cfg(feature = "webrtc")]
mod webrtc {
    use super::{SocketAddr, MAX_DATAGRAM_SIZE};
    use base64::prelude::*;
    use futures_util::{SinkExt, StreamExt};
    use std::io::{self, Write};
    use std::net::UdpSocket;
    use std::sync::Arc;
    use std::time::{Duration, Instant};
    use tokio::net::{TcpListener, TcpStream, UdpSocket as TokioUdpSocket};
    use tokio::sync::Mutex as TokioMutex;
    use tokio_tungstenite::tungstenite::Message as WebSocketMessage;
    use tokio_tungstenite::{accept_async, connect_async};

    struct WebRtcEndpoint {
        data_channel: datachannel_wrapper::DataChannel,
        peer_connection: datachannel_wrapper::PeerConnection,
    }

    async fn create_endpoint(
        stun_servers: Vec<String>,
    ) -> Result<
        (
            WebRtcEndpoint,
            tokio::sync::mpsc::Receiver<datachannel_wrapper::PeerConnectionEvent>,
        ),
        Box<dyn std::error::Error>,
    > {
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

        while let Some(event) = event_rx.recv().await {
            if matches!(
                event,
                datachannel_wrapper::PeerConnectionEvent::GatheringStateChange(
                    datachannel_wrapper::GatheringState::Complete
                )
            ) {
                break;
            }
        }

        Ok((
            WebRtcEndpoint {
                data_channel,
                peer_connection,
            },
            event_rx,
        ))
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
        event_rx: &mut tokio::sync::mpsc::Receiver<datachannel_wrapper::PeerConnectionEvent>,
    ) -> Result<(), Box<dyn std::error::Error>> {
        loop {
            let Some(event) = event_rx.recv().await else {
                return Err(io::Error::new(
                    io::ErrorKind::UnexpectedEof,
                    "WebRTC event stream closed",
                )
                .into());
            };
            if let datachannel_wrapper::PeerConnectionEvent::ConnectionStateChange(state) = event {
                println!("nsmb-net-bridge webrtc: connection state {:?}", state);
                match state {
                    datachannel_wrapper::ConnectionState::Connected => return Ok(()),
                    datachannel_wrapper::ConnectionState::Disconnected => {
                        return Err(io::Error::new(
                            io::ErrorKind::ConnectionAborted,
                            "WebRTC disconnected",
                        )
                        .into())
                    }
                    datachannel_wrapper::ConnectionState::Failed => {
                        return Err(io::Error::new(
                            io::ErrorKind::ConnectionAborted,
                            "WebRTC failed",
                        )
                        .into())
                    }
                    datachannel_wrapper::ConnectionState::Closed => {
                        return Err(io::Error::new(
                            io::ErrorKind::ConnectionAborted,
                            "WebRTC closed",
                        )
                        .into())
                    }
                    _ => {}
                }
            }
        }
    }

    async fn connect_offer(
        stun_servers: Vec<String>,
    ) -> Result<WebRtcEndpoint, Box<dyn std::error::Error>> {
        let (mut endpoint, mut event_rx) = create_endpoint(stun_servers).await?;
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
        wait_connected(&mut event_rx).await?;
        Ok(endpoint)
    }

    async fn connect_answer(
        stun_servers: Vec<String>,
    ) -> Result<WebRtcEndpoint, Box<dyn std::error::Error>> {
        let offer_sdp = read_pasted_sdp("Paste offer SDP base64 from the offer side.")?;
        let (mut endpoint, mut event_rx) = create_endpoint(stun_servers).await?;
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
        wait_connected(&mut event_rx).await?;
        Ok(endpoint)
    }

    async fn connect_signal_offer(
        signal_url: String,
        session: String,
        stun_servers: Vec<String>,
    ) -> Result<WebRtcEndpoint, Box<dyn std::error::Error>> {
        let url = build_signal_url(&signal_url, &session, "offer");
        let (mut ws, _) = connect_async(&url).await?;
        println!("nsmb-net-bridge signaling: connected {}", url);

        let hello = wait_signal_hello(&mut ws).await?;
        let server_ice_servers = parse_server_ice_servers(&hello);
        let stun_servers = if stun_servers.is_empty() {
            server_ice_servers
        } else {
            stun_servers
        };

        let (mut endpoint, mut event_rx) = create_endpoint(stun_servers).await?;
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
        wait_connected(&mut event_rx).await?;
        Ok(endpoint)
    }

    async fn connect_signal_answer(
        signal_url: String,
        session: String,
        stun_servers: Vec<String>,
    ) -> Result<WebRtcEndpoint, Box<dyn std::error::Error>> {
        let url = build_signal_url(&signal_url, &session, "answer");
        let (mut ws, _) = connect_async(&url).await?;
        println!("nsmb-net-bridge signaling: connected {}", url);

        let hello = wait_signal_hello(&mut ws).await?;
        let server_ice_servers = parse_server_ice_servers(&hello);
        let stun_servers = if stun_servers.is_empty() {
            server_ice_servers
        } else {
            stun_servers
        };

        let offer = wait_signal_sdp(&mut ws, "offer").await?;
        let offer_sdp = offer
            .get("sdp")
            .and_then(|v| v.as_str())
            .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidData, "missing offer SDP"))?;

        let (mut endpoint, mut event_rx) = create_endpoint(stun_servers).await?;
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

        wait_connected(&mut event_rx).await?;
        Ok(endpoint)
    }

    pub async fn run_manual_webrtc(
        side: String,
        local_bind: SocketAddr,
        local_target: Option<SocketAddr>,
        stun_servers: Vec<String>,
    ) -> Result<(), Box<dyn std::error::Error>> {
        let endpoint = match side.as_str() {
            "webrtc-offer" => connect_offer(stun_servers).await?,
            "webrtc-answer" => connect_answer(stun_servers).await?,
            _ => unreachable!("validated by caller"),
        };
        run_webrtc_udp_tunnel(endpoint.data_channel, local_bind, local_target).await
    }

    pub async fn run_signaling_webrtc(
        side: String,
        local_bind: SocketAddr,
        local_target: Option<SocketAddr>,
        stun_servers: Vec<String>,
        signal_url: String,
        session: String,
    ) -> Result<(), Box<dyn std::error::Error>> {
        let endpoint = match role_from_side(&side) {
            "offer" => connect_signal_offer(signal_url, session, stun_servers).await?,
            "answer" => connect_signal_answer(signal_url, session, stun_servers).await?,
            _ => unreachable!("validated by caller"),
        };
        run_webrtc_udp_tunnel(endpoint.data_channel, local_bind, local_target).await
    }

    pub async fn run_loopback_smoke(
        stun_servers: Vec<String>,
    ) -> Result<(), Box<dyn std::error::Error>> {
        let stun_servers = if stun_servers.is_empty() {
            Vec::new()
        } else {
            stun_servers
        };
        let (mut offer, mut offer_events) = create_endpoint(stun_servers.clone()).await?;
        let (mut answer, mut answer_events) = create_endpoint(stun_servers).await?;

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
            wait_connected(&mut offer_events),
            wait_connected(&mut answer_events)
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
        let offer = connect_signal_offer(
            signal_url.clone(),
            "loopback".to_owned(),
            stun_servers.clone(),
        );
        let answer = connect_signal_answer(signal_url, "loopback".to_owned(), stun_servers);

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
        data_channel: datachannel_wrapper::DataChannel,
        local_bind: SocketAddr,
        local_target: Option<SocketAddr>,
    ) -> Result<(), Box<dyn std::error::Error>> {
        let local_socket = TokioUdpSocket::bind(local_bind).await?;
        let local_addr = local_socket.local_addr()?;
        let local_target = Arc::new(TokioMutex::new(local_target));
        let (mut dc_tx, mut dc_rx) = data_channel.split();
        let mut app_buf = [0u8; MAX_DATAGRAM_SIZE];
        let mut app_to_webrtc_packets = 0u64;
        let mut app_to_webrtc_bytes = 0u64;
        let mut webrtc_to_app_packets = 0u64;
        let mut webrtc_to_app_bytes = 0u64;
        let mut dropped_no_local_target = 0u64;
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
                    app_to_webrtc_packets += 1;
                    app_to_webrtc_bytes += len as u64;
                }
                msg = dc_rx.receive() => {
                    let Some(msg) = msg else {
                        return Err(io::Error::new(io::ErrorKind::UnexpectedEof, "WebRTC data channel closed").into());
                    };
                    let target = *local_target.lock().await;
                    let Some(target) = target else {
                        dropped_no_local_target += 1;
                        continue;
                    };
                    local_socket.send_to(&msg, target).await?;
                    webrtc_to_app_packets += 1;
                    webrtc_to_app_bytes += msg.len() as u64;
                }
                _ = stats_tick.tick() => {
                    println!(
                        "nsmb-net-bridge webrtc: t={:.1}s app->rtc={}pkts/{}B rtc->app={}pkts/{}B droppedNoLocalTarget={}",
                        start.elapsed().as_secs_f32(),
                        app_to_webrtc_packets,
                        app_to_webrtc_bytes,
                        webrtc_to_app_packets,
                        webrtc_to_app_bytes,
                        dropped_no_local_target
                    );
                }
            }
        }
    }
}
