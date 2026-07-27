use super::{PathBuf, SocketAddr, MAX_DATAGRAM_SIZE};
use base64::prelude::*;
use env_logger::{Builder as LogBuilder, Env as LogEnv, Target as LogTarget};
use futures_util::{SinkExt, StreamExt};
use std::collections::VecDeque;
use std::env;
use std::fs::{self, OpenOptions};
use std::io::{self, Write};
use std::net::UdpSocket;
use std::sync::Arc;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};
use tokio::net::{TcpListener, TcpStream, UdpSocket as TokioUdpSocket};
use tokio::sync::Mutex as TokioMutex;
use tokio_tungstenite::tungstenite::Message as WebSocketMessage;
use tokio_tungstenite::{accept_async, connect_async};

const DEFAULT_STUN_SERVER: &str = "stun:stun.l.google.com:19302";
const LOCAL_APP_REPLAY_INTERVAL: Duration = Duration::from_millis(100);
const LOCAL_APP_REPLAY_TTL: Duration = Duration::from_secs(8);
const LOCAL_APP_REPLAY_LIMIT: usize = 64;

#[derive(Debug)]
struct PendingLocalDatagram {
    payload: Vec<u8>,
    target: SocketAddr,
    first_sent: Instant,
    last_sent: Instant,
    attempts: u32,
}

struct WebRtcEndpoint {
    data_channel: datachannel_wrapper::DataChannel,
    peer_connection: datachannel_wrapper::PeerConnection,
    event_rx: tokio::sync::mpsc::Receiver<datachannel_wrapper::PeerConnectionEvent>,
}

#[derive(Clone, Copy, Debug, Default)]
struct ImpairmentConfig {
    base_delay_ms: u64,
    jitter_ms: u64,
    drop_modulo: u64,
    drop_burst_modulo: u64,
    drop_burst_len: u64,
}

impl ImpairmentConfig {
    fn from_env() -> Self {
        Self {
            base_delay_ms: parse_env_u64("BIGSTAR_NET_BRIDGE_DELAY_MS"),
            jitter_ms: parse_env_u64("BIGSTAR_NET_BRIDGE_JITTER_MS"),
            drop_modulo: parse_env_u64("BIGSTAR_NET_BRIDGE_DROP_MODULO"),
            drop_burst_modulo: parse_env_u64("BIGSTAR_NET_BRIDGE_DROP_BURST_MODULO"),
            drop_burst_len: parse_env_u64("BIGSTAR_NET_BRIDGE_DROP_BURST_LEN"),
        }
    }

    fn enabled(self) -> bool {
        self.base_delay_ms > 0
            || self.jitter_ms > 0
            || self.drop_modulo > 0
            || (self.drop_burst_modulo > 0 && self.drop_burst_len > 0)
    }
}

#[derive(Debug)]
struct ImpairmentState {
    config: ImpairmentConfig,
    rng: u64,
    app_to_webrtc_seq: u64,
    webrtc_to_app_seq: u64,
    app_to_webrtc_dropped: u64,
    webrtc_to_app_dropped: u64,
}

impl ImpairmentState {
    fn new(config: ImpairmentConfig) -> Self {
        Self {
            config,
            rng: 0x9E37_79B9_7F4A_7C15,
            app_to_webrtc_seq: 0,
            webrtc_to_app_seq: 0,
            app_to_webrtc_dropped: 0,
            webrtc_to_app_dropped: 0,
        }
    }

    async fn before_app_to_webrtc_send(&mut self) -> bool {
        self.app_to_webrtc_seq = self.app_to_webrtc_seq.wrapping_add(1);
        let seq = self.app_to_webrtc_seq;
        if self.should_drop(seq) {
            self.app_to_webrtc_dropped = self.app_to_webrtc_dropped.wrapping_add(1);
            return false;
        }
        self.delay().await;
        true
    }

    async fn before_webrtc_to_app_send(&mut self) -> bool {
        self.webrtc_to_app_seq = self.webrtc_to_app_seq.wrapping_add(1);
        let seq = self.webrtc_to_app_seq;
        if self.should_drop(seq) {
            self.webrtc_to_app_dropped = self.webrtc_to_app_dropped.wrapping_add(1);
            return false;
        }
        self.delay().await;
        true
    }

    fn should_drop(&self, seq: u64) -> bool {
        if self.config.drop_modulo > 0 && seq.is_multiple_of(self.config.drop_modulo) {
            return true;
        }
        self.config.drop_burst_modulo > 0
            && self.config.drop_burst_len > 0
            && (seq - 1) % self.config.drop_burst_modulo < self.config.drop_burst_len
    }

    async fn delay(&mut self) {
        let delay_ms = self.config.base_delay_ms + self.next_jitter_ms();
        if delay_ms > 0 {
            tokio::time::sleep(Duration::from_millis(delay_ms)).await;
        }
    }

    fn next_jitter_ms(&mut self) -> u64 {
        if self.config.jitter_ms == 0 {
            return 0;
        }
        self.rng = self
            .rng
            .wrapping_mul(6364136223846793005)
            .wrapping_add(1442695040888963407);
        self.rng % (self.config.jitter_ms + 1)
    }
}

fn parse_env_u64(name: &str) -> u64 {
    env::var(name)
        .ok()
        .and_then(|value| value.parse::<u64>().ok())
        .unwrap_or(0)
}

fn env_flag(name: &str) -> bool {
    env::var(name)
        .map(|value| {
            let normalized = value.trim().to_ascii_lowercase();
            !normalized.is_empty() && normalized != "0" && normalized != "false"
        })
        .unwrap_or(false)
}

fn env_path(name: &str) -> Option<PathBuf> {
    env::var_os(name).and_then(|value| {
        if value.as_os_str().is_empty() {
            None
        } else {
            Some(PathBuf::from(value))
        }
    })
}

fn unix_ms() -> u128 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|duration| duration.as_millis())
        .unwrap_or(0)
}

fn append_bridge_event(path: Option<&PathBuf>, value: serde_json::Value) {
    let Some(path) = path else {
        return;
    };
    if let Some(parent) = path.parent() {
        let _ = fs::create_dir_all(parent);
    }
    let line = match serde_json::to_string(&value) {
        Ok(line) => line,
        Err(error) => {
            eprintln!("bigstar-net-bridge diagnostics: event encode failed error={error}");
            return;
        }
    };
    match OpenOptions::new().create(true).append(true).open(path) {
        Ok(mut file) => {
            let _ = writeln!(file, "{line}");
        }
        Err(error) => {
            eprintln!(
                "bigstar-net-bridge diagnostics: event write failed path={} error={error}",
                path.display()
            );
        }
    }
}

pub struct SignalingWebRtcConfig {
    pub side: String,
    pub local_bind: SocketAddr,
    pub local_target: Option<SocketAddr>,
    pub stun_servers: Vec<String>,
    pub signal_url: String,
    pub session: String,
    pub status_file: Option<PathBuf>,
    pub fallback_to_default_stun: bool,
}

mod diagnostics;
use diagnostics::DiagnosticsReporter;

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

fn candidate_socket_key(candidate: &str) -> Option<String> {
    let mut parts = candidate.split_whitespace();
    let address = parts.nth(4)?;
    let port = parts.next()?;
    Some(socket_key(address, port))
}

fn socket_key(address: &str, port: &str) -> String {
    format!("{}:{port}", address.trim_matches(['[', ']']))
}

fn socket_address_key(value: &str) -> Option<String> {
    if let Some((address, port)) = value.rsplit_once("]:") {
        return Some(socket_key(address.trim_start_matches('['), port));
    }
    let (address, port) = value.rsplit_once(':')?;
    Some(socket_key(address, port))
}

fn candidate_by_address<'a>(candidates: &'a [String], address: &str) -> Option<&'a str> {
    let address = socket_address_key(address)?;
    candidates
        .iter()
        .find(|candidate| candidate_socket_key(candidate).as_deref() == Some(address.as_str()))
        .map(String::as_str)
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
    } else if matches!(local_type, "srflx" | "prflx") || matches!(remote_type, "srflx" | "prflx") {
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
    let (mut peer_connection, mut event_rx) = datachannel_wrapper::PeerConnection::new(rtc_config)?;
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

fn signal_peer_count(value: &serde_json::Value) -> Option<u64> {
    value.get("peerCount").and_then(|value| value.as_u64())
}

async fn wait_signal_hello(
    ws: &mut tokio_tungstenite::WebSocketStream<
        tokio_tungstenite::MaybeTlsStream<tokio::net::TcpStream>,
    >,
) -> Result<serde_json::Value, Box<dyn std::error::Error>> {
    loop {
        let Some(message) = ws.next().await else {
            return Err(
                io::Error::new(io::ErrorKind::UnexpectedEof, "signaling socket closed").into(),
            );
        };
        let message = message?;
        let WebSocketMessage::Text(text) = message else {
            continue;
        };
        let value = serde_json::from_str::<serde_json::Value>(&text)?;
        match value.get("type").and_then(|v| v.as_str()) {
            Some("hello") => {
                println!("bigstar-net-bridge signaling: {}", value);
                return Ok(value);
            }
            Some("peer-joined") | Some("pong") => {
                println!("bigstar-net-bridge signaling: {}", value);
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
                    "bigstar-net-bridge signaling: ignored message type {:?}",
                    other
                );
            }
        }
    }
}

async fn wait_signal_ready_for_offer(
    ws: &mut tokio_tungstenite::WebSocketStream<
        tokio_tungstenite::MaybeTlsStream<tokio::net::TcpStream>,
    >,
) -> Result<(), Box<dyn std::error::Error>> {
    loop {
        let Some(message) = ws.next().await else {
            return Err(
                io::Error::new(io::ErrorKind::UnexpectedEof, "signaling socket closed").into(),
            );
        };
        let message = message?;
        let WebSocketMessage::Text(text) = message else {
            continue;
        };
        let value = serde_json::from_str::<serde_json::Value>(&text)?;
        match value.get("type").and_then(|v| v.as_str()) {
            Some("ready-for-offer") => {
                println!("bigstar-net-bridge signaling: {}", value);
                return Ok(());
            }
            Some("peer-joined") if signal_peer_count(&value).is_some_and(|count| count >= 2) => {
                println!("bigstar-net-bridge signaling: {}", value);
                return Ok(());
            }
            Some("hello") | Some("peer-joined") | Some("pong") => {
                println!("bigstar-net-bridge signaling: {}", value);
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
                    "bigstar-net-bridge signaling: ignored message type {:?}",
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
            return Err(
                io::Error::new(io::ErrorKind::UnexpectedEof, "signaling socket closed").into(),
            );
        };
        let message = message?;
        let WebSocketMessage::Text(text) = message else {
            continue;
        };
        let value = serde_json::from_str::<serde_json::Value>(&text)?;
        match value.get("type").and_then(|v| v.as_str()) {
            Some("hello") | Some("peer-joined") | Some("ready-for-offer") | Some("pong") => {
                println!("bigstar-net-bridge signaling: {}", value);
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
                    "bigstar-net-bridge signaling: ignored message type {:?}",
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
            if let datachannel_wrapper::PeerConnectionEvent::ConnectionStateChange(state) = event {
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
        .ok_or_else(|| io::Error::other("missing local offer SDP"))?;
    print_sdp("offer", &offer.sdp.to_string());
    let answer_sdp = read_pasted_sdp("Paste answer SDP base64 from the answer side.")?;
    reporter.observe_remote_sdp("answer", &answer_sdp);
    endpoint
        .peer_connection
        .set_remote_description(datachannel_wrapper::SessionDescription {
            sdp_type: datachannel_wrapper::SdpType::Answer,
            sdp: datachannel_wrapper::sdp::parse_sdp(&answer_sdp, false)?,
        })?;
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
    reporter.observe_remote_sdp("offer", &offer_sdp);
    reporter.set_ice_servers(stun_servers.clone(), "cli-or-manual-default");
    let mut endpoint = create_endpoint(stun_servers, reporter).await?;
    endpoint
        .peer_connection
        .set_local_description(datachannel_wrapper::SdpType::Rollback)?;
    endpoint
        .peer_connection
        .set_remote_description(datachannel_wrapper::SessionDescription {
            sdp_type: datachannel_wrapper::SdpType::Offer,
            sdp: datachannel_wrapper::sdp::parse_sdp(&offer_sdp, false)?,
        })?;
    let answer = endpoint
        .peer_connection
        .local_description()
        .ok_or_else(|| io::Error::other("missing local answer SDP"))?;
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
    println!("bigstar-net-bridge signaling: connected {}", url);
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

    if signal_peer_count(&hello).is_none_or(|count| count < 2) {
        reporter.set_phase("waiting-peer-ready");
        wait_signal_ready_for_offer(&mut ws).await?;
    }

    let mut endpoint = create_endpoint(stun_servers, reporter).await?;
    let offer = endpoint
        .peer_connection
        .local_description()
        .ok_or_else(|| io::Error::other("missing local offer SDP"))?;
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
    println!("bigstar-net-bridge signaling: sent offer SDP");
    print_sdp("local offer", &offer.sdp.to_string());

    reporter.set_phase("waiting-answer-sdp");
    let answer = wait_signal_sdp(&mut ws, "answer").await?;
    let answer_sdp = answer
        .get("sdp")
        .and_then(|v| v.as_str())
        .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidData, "missing answer SDP"))?;
    reporter.observe_remote_sdp("answer", answer_sdp);
    endpoint
        .peer_connection
        .set_remote_description(datachannel_wrapper::SessionDescription {
            sdp_type: sdp_type_from_str(
                answer
                    .get("sdpType")
                    .and_then(|v| v.as_str())
                    .unwrap_or("answer"),
            )?,
            sdp: datachannel_wrapper::sdp::parse_sdp(answer_sdp, false)?,
        })?;
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
    println!("bigstar-net-bridge signaling: connected {}", url);
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
    reporter.observe_remote_sdp("offer", offer_sdp);
    let mut endpoint = create_endpoint(stun_servers, reporter).await?;
    endpoint
        .peer_connection
        .set_local_description(datachannel_wrapper::SdpType::Rollback)?;
    endpoint
        .peer_connection
        .set_remote_description(datachannel_wrapper::SessionDescription {
            sdp_type: sdp_type_from_str(
                offer
                    .get("sdpType")
                    .and_then(|v| v.as_str())
                    .unwrap_or("offer"),
            )?,
            sdp: datachannel_wrapper::sdp::parse_sdp(offer_sdp, false)?,
        })?;
    let answer = endpoint
        .peer_connection
        .local_description()
        .ok_or_else(|| io::Error::other("missing local answer SDP"))?;
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
    println!("bigstar-net-bridge signaling: sent answer SDP");
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
        "bigstar-net-bridge webrtc: start mode=manual role={} localBind={} localTarget={}",
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
    config: SignalingWebRtcConfig,
) -> Result<(), Box<dyn std::error::Error>> {
    let SignalingWebRtcConfig {
        side,
        local_bind,
        local_target,
        stun_servers,
        signal_url,
        session,
        status_file,
        fallback_to_default_stun,
    } = config;
    let role = role_from_side(&side);
    let mut reporter = DiagnosticsReporter::new(status_file, role);
    println!(
            "bigstar-net-bridge webrtc: start mode=signaling role={} localBind={} localTarget={} signalUrl={} session={}",
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
        .ok_or_else(|| io::Error::other("missing loopback offer SDP"))?
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
        .ok_or_else(|| io::Error::other("missing loopback answer SDP"))?
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
    offer_tx.send(b"bigstar-net-bridge-smoke").await?;
    let Some(received) = answer_rx.receive().await else {
        return Err(
            io::Error::new(io::ErrorKind::UnexpectedEof, "loopback data channel closed").into(),
        );
    };
    if received != b"bigstar-net-bridge-smoke" {
        return Err(io::Error::new(io::ErrorKind::InvalidData, "loopback payload mismatch").into());
    }

    println!("bigstar-net-bridge webrtc: loopback smoke passed");
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
    let ready = serde_json::json!({
        "type": "ready-for-offer",
        "peerCount": 2,
    })
    .to_string();
    first_tx
        .send(WebSocketMessage::Text(ready.clone().into()))
        .await?;
    second_tx.send(WebSocketMessage::Text(ready.into())).await?;

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
    offer_tx.send(b"bigstar-net-bridge-signaling-smoke").await?;
    let Some(received) = answer_rx.receive().await else {
        return Err(io::Error::new(
            io::ErrorKind::UnexpectedEof,
            "signaling loopback data channel closed",
        )
        .into());
    };
    if received != b"bigstar-net-bridge-signaling-smoke" {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "signaling loopback payload mismatch",
        )
        .into());
    }

    server.abort();
    println!("bigstar-net-bridge webrtc: signaling loopback smoke passed");
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
        if let Err(err) = run_signaling_webrtc(SignalingWebRtcConfig {
            side: "webrtc-offer".to_owned(),
            local_bind: offer_bridge_addr,
            local_target: Some(host_app_addr),
            stun_servers: offer_stun,
            signal_url: offer_signal,
            session: offer_session,
            status_file: None,
            fallback_to_default_stun: false,
        })
        .await
        {
            eprintln!("bigstar-net-bridge webrtc: offer smoke task failed: {err}");
        }
    });

    let answer_task = tokio::spawn(async move {
        if let Err(err) = run_signaling_webrtc(SignalingWebRtcConfig {
            side: "webrtc-answer".to_owned(),
            local_bind: answer_bridge_addr,
            local_target: None,
            stun_servers,
            signal_url,
            session,
            status_file: None,
            fallback_to_default_stun: false,
        })
        .await
        {
            eprintln!("bigstar-net-bridge webrtc: answer smoke task failed: {err}");
        }
    });

    wait_for_udp_payload(
        &client_app,
        answer_bridge_addr,
        &host_app,
        b"bigstar-net-bridge-client-to-host",
        "client-to-host",
    )
    .await?;
    wait_for_udp_payload(
        &host_app,
        offer_bridge_addr,
        &client_app,
        b"bigstar-net-bridge-host-to-client",
        "host-to-client",
    )
    .await?;

    offer_task.abort();
    answer_task.abort();
    server.abort();
    println!("bigstar-net-bridge webrtc: signaling udp pair smoke passed");
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
    let fixed_local_target = local_target.is_some();
    let local_target = Arc::new(TokioMutex::new(local_target));
    let WebRtcEndpoint {
        data_channel,
        peer_connection,
        mut event_rx,
    } = endpoint;
    let (mut dc_tx, mut dc_rx) = data_channel.split();
    let mut app_buf = [0u8; MAX_DATAGRAM_SIZE];
    let start = Instant::now();
    let detailed_logs = env_flag("BIGSTAR_DETAILED_LOGS");
    let liveness_logs = detailed_logs || env_flag("BIGSTAR_BRIDGE_LIVENESS");
    let bridge_events_path = env_path("BIGSTAR_BRIDGE_EVENTS_FILE");
    let mut stats_tick =
        tokio::time::interval(Duration::from_secs(if detailed_logs { 1 } else { 2 }));
    let mut local_replay_tick = tokio::time::interval(LOCAL_APP_REPLAY_INTERVAL);
    let mut impairment = ImpairmentState::new(ImpairmentConfig::from_env());
    let mut last_stats = reporter.stats;
    let mut last_impairment_app_to_webrtc_dropped = 0;
    let mut last_impairment_webrtc_to_app_dropped = 0;
    let mut last_app_to_webrtc_instant: Option<Instant> = None;
    let mut last_webrtc_to_app_instant: Option<Instant> = None;
    let mut last_app_to_webrtc_unix_ms: Option<u128> = None;
    let mut last_webrtc_to_app_unix_ms: Option<u128> = None;
    let mut peer_connection_state = "unknown".to_owned();
    let data_channel_state = "open".to_owned();
    let mut local_app_seen = false;
    let mut pending_local_replays: VecDeque<PendingLocalDatagram> = VecDeque::new();

    let initial_target = {
        let target = local_target.lock().await;
        target
            .map(|addr| addr.to_string())
            .unwrap_or_else(|| "learn".to_owned())
    };
    println!(
        "bigstar-net-bridge webrtc: connected local={} localTarget={}",
        local_addr, initial_target
    );
    append_bridge_event(
        bridge_events_path.as_ref(),
        serde_json::json!({
            "tUnixMs": unix_ms(),
            "event": "tunnel-start",
            "local": local_addr.to_string(),
            "localTarget": initial_target,
            "detailedLogs": detailed_logs,
            "livenessLogs": liveness_logs,
        }),
    );
    if impairment.config.enabled() {
        println!(
            "bigstar-net-bridge webrtc: impairment delayMs={} jitterMs={} dropModulo={} dropBurstModulo={} dropBurstLen={}",
            impairment.config.base_delay_ms,
            impairment.config.jitter_ms,
            impairment.config.drop_modulo,
            impairment.config.drop_burst_modulo,
            impairment.config.drop_burst_len
        );
    }

    loop {
        tokio::select! {
            recv = local_socket.recv_from(&mut app_buf) => {
                let (len, from) = match recv {
                    Ok(received) => received,
                    Err(err) if err.kind() == io::ErrorKind::ConnectionReset => {
                        println!("bigstar-net-bridge webrtc: ignored local UDP connection reset");
                        continue;
                    }
                    Err(err) => return Err(err.into()),
                };
                if !local_app_seen {
                    local_app_seen = true;
                    if !pending_local_replays.is_empty() {
                        println!(
                            "bigstar-net-bridge webrtc: local UDP app became reachable; stopped {} pending replays",
                            pending_local_replays.len()
                        );
                        pending_local_replays.clear();
                    }
                }
                {
                    let mut target = local_target.lock().await;
                    if target.is_none() {
                        *target = Some(from);
                        println!("bigstar-net-bridge webrtc: learned local target {}", from);
                    }
                }
                if !impairment.before_app_to_webrtc_send().await {
                    continue;
                }
                let send_start = Instant::now();
                match dc_tx.send(&app_buf[..len]).await {
                    Ok(()) => {
                        let now = Instant::now();
                        let now_unix_ms = unix_ms();
                        last_app_to_webrtc_instant = Some(now);
                        last_app_to_webrtc_unix_ms = Some(now_unix_ms);
                        if liveness_logs {
                            let send_ms = send_start.elapsed().as_secs_f64() * 1000.0;
                            if send_ms >= 10.0 {
                                append_bridge_event(
                                    bridge_events_path.as_ref(),
                                    serde_json::json!({
                                        "tUnixMs": now_unix_ms,
                                        "event": "slow-app-to-rtc-send",
                                        "bytes": len,
                                        "sendMs": send_ms,
                                    }),
                                );
                            }
                        }
                    }
                    Err(err) => {
                        append_bridge_event(
                            bridge_events_path.as_ref(),
                            serde_json::json!({
                                "tUnixMs": unix_ms(),
                                "event": "app-to-rtc-send-error",
                                "bytes": len,
                                "error": err.to_string(),
                            }),
                        );
                        return Err(err.into());
                    }
                }
                reporter.stats.app_to_webrtc_packets += 1;
                reporter.stats.app_to_webrtc_bytes += len as u64;
            }
            msg = dc_rx.receive() => {
                let Some(msg) = msg else {
                    append_bridge_event(
                        bridge_events_path.as_ref(),
                        serde_json::json!({
                            "tUnixMs": unix_ms(),
                            "event": "data-channel-closed",
                            "peerConnectionState": &peer_connection_state,
                        }),
                    );
                    return Err(io::Error::new(io::ErrorKind::UnexpectedEof, "WebRTC data channel closed").into());
                };
                let target = *local_target.lock().await;
                let Some(target) = target else {
                    reporter.stats.dropped_no_local_target += 1;
                    append_bridge_event(
                        bridge_events_path.as_ref(),
                        serde_json::json!({
                            "tUnixMs": unix_ms(),
                            "event": "drop-no-local-target",
                            "bytes": msg.len(),
                            "droppedNoLocalTarget": reporter.stats.dropped_no_local_target,
                        }),
                    );
                    continue;
                };
                if !impairment.before_webrtc_to_app_send().await {
                    continue;
                }
                let msg_len = msg.len();
                let send_start = Instant::now();
                match local_socket.send_to(&msg, target).await {
                    Ok(sent) => {
                        let now = Instant::now();
                        let now_unix_ms = unix_ms();
                        last_webrtc_to_app_instant = Some(now);
                        last_webrtc_to_app_unix_ms = Some(now_unix_ms);
                        if sent != msg_len || (liveness_logs && send_start.elapsed() >= Duration::from_millis(10)) {
                            append_bridge_event(
                                bridge_events_path.as_ref(),
                                serde_json::json!({
                                    "tUnixMs": now_unix_ms,
                                    "event": if sent == msg_len { "slow-rtc-to-app-send" } else { "partial-rtc-to-app-send" },
                                    "bytes": msg_len,
                                    "sent": sent,
                                    "target": target.to_string(),
                                    "sendMs": send_start.elapsed().as_secs_f64() * 1000.0,
                                }),
                            );
                        }
                    }
                    Err(err) => {
                        append_bridge_event(
                            bridge_events_path.as_ref(),
                            serde_json::json!({
                                "tUnixMs": unix_ms(),
                                "event": "rtc-to-app-send-error",
                                "bytes": msg_len,
                                "target": target.to_string(),
                                "error": err.to_string(),
                            }),
                        );
                        return Err(err.into());
                    }
                }
                if fixed_local_target && !local_app_seen {
                    let now = Instant::now();
                    if pending_local_replays.len() >= LOCAL_APP_REPLAY_LIMIT {
                        pending_local_replays.pop_front();
                    }
                    pending_local_replays.push_back(PendingLocalDatagram {
                        payload: msg,
                        target,
                        first_sent: now,
                        last_sent: now,
                        attempts: 1,
                    });
                }
                reporter.stats.webrtc_to_app_packets += 1;
                reporter.stats.webrtc_to_app_bytes += msg_len as u64;
            }
            event = event_rx.recv() => {
                let Some(event) = event else {
                    return Err(io::Error::new(io::ErrorKind::UnexpectedEof, "WebRTC event stream closed").into());
                };
                reporter.observe_event(&event);
                if let datachannel_wrapper::PeerConnectionEvent::ConnectionStateChange(state) = event {
                    peer_connection_state = format!("{state:?}");
                    append_bridge_event(
                        bridge_events_path.as_ref(),
                        serde_json::json!({
                            "tUnixMs": unix_ms(),
                            "event": "peer-connection-state",
                            "state": &peer_connection_state,
                            "lastAppToRtcUnixMs": last_app_to_webrtc_unix_ms,
                            "lastRtcToAppUnixMs": last_webrtc_to_app_unix_ms,
                        }),
                    );
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
            _ = local_replay_tick.tick() => {
                if local_app_seen || pending_local_replays.is_empty() {
                    continue;
                }
                let now = Instant::now();
                while pending_local_replays
                    .front()
                    .is_some_and(|pending| now.duration_since(pending.first_sent) > LOCAL_APP_REPLAY_TTL)
                {
                    pending_local_replays.pop_front();
                }
                for pending in pending_local_replays.iter_mut() {
                    if now.duration_since(pending.last_sent) < LOCAL_APP_REPLAY_INTERVAL {
                        continue;
                    }
                    if let Err(err) = local_socket.send_to(&pending.payload, pending.target).await {
                        append_bridge_event(
                            bridge_events_path.as_ref(),
                            serde_json::json!({
                                "tUnixMs": unix_ms(),
                                "event": "early-replay-send-error",
                                "bytes": pending.payload.len(),
                                "target": pending.target.to_string(),
                                "attempts": pending.attempts,
                                "error": err.to_string(),
                            }),
                        );
                        return Err(err.into());
                    }
                    pending.last_sent = now;
                    pending.attempts = pending.attempts.saturating_add(1);
                    if pending.attempts == 2 || pending.attempts % 20 == 0 {
                        println!(
                            "bigstar-net-bridge webrtc: replayed early remote UDP to local app target={} attempts={} bytes={}",
                            pending.target,
                            pending.attempts,
                            pending.payload.len()
                        );
                    }
                }
            }
            _ = stats_tick.tick() => {
                if detailed_logs {
                    let unix_ms = SystemTime::now()
                        .duration_since(UNIX_EPOCH)
                        .map(|duration| duration.as_millis())
                        .unwrap_or(0);
                    let delta_app_packets = reporter.stats.app_to_webrtc_packets
                        .saturating_sub(last_stats.app_to_webrtc_packets);
                    let delta_app_bytes = reporter.stats.app_to_webrtc_bytes
                        .saturating_sub(last_stats.app_to_webrtc_bytes);
                    let delta_rtc_packets = reporter.stats.webrtc_to_app_packets
                        .saturating_sub(last_stats.webrtc_to_app_packets);
                    let delta_rtc_bytes = reporter.stats.webrtc_to_app_bytes
                        .saturating_sub(last_stats.webrtc_to_app_bytes);
                    let delta_no_local_target = reporter.stats.dropped_no_local_target
                        .saturating_sub(last_stats.dropped_no_local_target);
                    let delta_impairment_app = impairment.app_to_webrtc_dropped
                        .saturating_sub(last_impairment_app_to_webrtc_dropped);
                    let delta_impairment_rtc = impairment.webrtc_to_app_dropped
                        .saturating_sub(last_impairment_webrtc_to_app_dropped);
                    let since_last_app_to_rtc_ms = last_app_to_webrtc_instant
                        .map(|instant| instant.elapsed().as_millis());
                    let since_last_rtc_to_app_ms = last_webrtc_to_app_instant
                        .map(|instant| instant.elapsed().as_millis());
                    println!(
                        "bigstar-net-bridge webrtc: tUnixMs={} t={:.1}s app->rtc={}pkts/{}B(+{}pkts/+{}B) rtc->app={}pkts/{}B(+{}pkts/+{}B) lastAppToRtc={:?} sinceLastAppToRtcMs={:?} lastRtcToApp={:?} sinceLastRtcToAppMs={:?} pcState={} dcState={} droppedNoLocalTarget={}({}+) impairmentDropAppToRtc={}({}+) impairmentDropRtcToApp={}({}+)",
                        unix_ms,
                        start.elapsed().as_secs_f32(),
                        reporter.stats.app_to_webrtc_packets,
                        reporter.stats.app_to_webrtc_bytes,
                        delta_app_packets,
                        delta_app_bytes,
                        reporter.stats.webrtc_to_app_packets,
                        reporter.stats.webrtc_to_app_bytes,
                        delta_rtc_packets,
                        delta_rtc_bytes,
                        last_app_to_webrtc_unix_ms,
                        since_last_app_to_rtc_ms,
                        last_webrtc_to_app_unix_ms,
                        since_last_rtc_to_app_ms,
                        &peer_connection_state,
                        &data_channel_state,
                        reporter.stats.dropped_no_local_target,
                        delta_no_local_target,
                        impairment.app_to_webrtc_dropped,
                        delta_impairment_app,
                        impairment.webrtc_to_app_dropped,
                        delta_impairment_rtc
                    );
                    if liveness_logs {
                        append_bridge_event(
                            bridge_events_path.as_ref(),
                            serde_json::json!({
                                "tUnixMs": unix_ms,
                                "event": "liveness",
                                "elapsedSeconds": start.elapsed().as_secs_f32(),
                                "appToRtcPackets": reporter.stats.app_to_webrtc_packets,
                                "appToRtcBytes": reporter.stats.app_to_webrtc_bytes,
                                "deltaAppToRtcPackets": delta_app_packets,
                                "deltaAppToRtcBytes": delta_app_bytes,
                                "rtcToAppPackets": reporter.stats.webrtc_to_app_packets,
                                "rtcToAppBytes": reporter.stats.webrtc_to_app_bytes,
                                "deltaRtcToAppPackets": delta_rtc_packets,
                                "deltaRtcToAppBytes": delta_rtc_bytes,
                                "lastAppToRtcUnixMs": last_app_to_webrtc_unix_ms,
                                "sinceLastAppToRtcMs": since_last_app_to_rtc_ms,
                                "lastRtcToAppUnixMs": last_webrtc_to_app_unix_ms,
                                "sinceLastRtcToAppMs": since_last_rtc_to_app_ms,
                                "peerConnectionState": &peer_connection_state,
                                "dataChannelState": &data_channel_state,
                                "droppedNoLocalTarget": reporter.stats.dropped_no_local_target,
                                "deltaDroppedNoLocalTarget": delta_no_local_target,
                                "impairmentDropAppToRtc": impairment.app_to_webrtc_dropped,
                                "deltaImpairmentDropAppToRtc": delta_impairment_app,
                                "impairmentDropRtcToApp": impairment.webrtc_to_app_dropped,
                                "deltaImpairmentDropRtcToApp": delta_impairment_rtc,
                            }),
                        );
                    }
                    last_stats = reporter.stats;
                    last_impairment_app_to_webrtc_dropped = impairment.app_to_webrtc_dropped;
                    last_impairment_webrtc_to_app_dropped = impairment.webrtc_to_app_dropped;
                } else {
                    println!(
                        "bigstar-net-bridge webrtc: t={:.1}s app->rtc={}pkts/{}B rtc->app={}pkts/{}B droppedNoLocalTarget={} impairmentDropAppToRtc={} impairmentDropRtcToApp={}",
                        start.elapsed().as_secs_f32(),
                        reporter.stats.app_to_webrtc_packets,
                        reporter.stats.app_to_webrtc_bytes,
                        reporter.stats.webrtc_to_app_packets,
                        reporter.stats.webrtc_to_app_bytes,
                        reporter.stats.dropped_no_local_target,
                        impairment.app_to_webrtc_dropped,
                        impairment.webrtc_to_app_dropped
                    );
                }
                reporter.observe_selected_addresses(
                    peer_connection.local_address(),
                    peer_connection.remote_address(),
                );
            }
        }
    }
}

#[cfg(test)]
mod tests;
