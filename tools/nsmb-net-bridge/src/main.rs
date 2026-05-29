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
  nsmb-net-bridge webrtc-offer  --local-bind ADDR [--local-target ADDR] [--stun URI]
  nsmb-net-bridge webrtc-answer --local-bind ADDR [--local-target ADDR] [--stun URI]
  nsmb-net-bridge webrtc-loopback-smoke [--stun URI]

examples:
  host bridge:
    nsmb-net-bridge udp --local-bind 127.0.0.1:0 --local-target 127.0.0.1:8165 --bridge-bind 127.0.0.1:9001 --bridge-peer 127.0.0.1:9002

  client bridge:
    nsmb-net-bridge udp --local-bind 127.0.0.1:8265 --bridge-bind 127.0.0.1:9002 --bridge-peer 127.0.0.1:9001

  manual WebRTC offer side:
    nsmb-net-bridge webrtc-offer --local-bind 127.0.0.1:0 --local-target 127.0.0.1:8165

  manual WebRTC answer side:
    nsmb-net-bridge webrtc-answer --local-bind 127.0.0.1:8265
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
) -> Result<(SocketAddr, Option<SocketAddr>, Vec<String>), String> {
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
    let stun_servers = if stun_servers.is_empty() {
        vec!["stun:stun.l.google.com:19302".to_owned()]
    } else {
        stun_servers
    };

    Ok((local_bind, local_target, stun_servers))
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
            let (local_bind, local_target, stun_servers) =
                parse_local_config(&args[1..]).map_err(|err| {
                    io::Error::new(io::ErrorKind::InvalidInput, format!("{err}\n{}", usage()))
                })?;
            run_manual_webrtc(args[0].as_str(), local_bind, local_target, stun_servers)?;
        }
        "webrtc-loopback-smoke" => {
            let stun_servers = args[1..]
                .windows(2)
                .filter_map(|w| (w[0] == "--stun").then(|| w[1].clone()))
                .collect::<Vec<_>>();
            run_webrtc_loopback_smoke(stun_servers)?;
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

#[cfg(feature = "webrtc")]
fn run_webrtc_loopback_smoke(stun_servers: Vec<String>) -> Result<(), Box<dyn std::error::Error>> {
    let runtime = tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()?;
    runtime.block_on(webrtc::run_loopback_smoke(stun_servers))
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
mod webrtc {
    use super::{SocketAddr, MAX_DATAGRAM_SIZE};
    use base64::prelude::*;
    use std::io::{self, Write};
    use std::sync::Arc;
    use std::time::{Duration, Instant};
    use tokio::net::UdpSocket as TokioUdpSocket;
    use tokio::sync::Mutex as TokioMutex;

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
