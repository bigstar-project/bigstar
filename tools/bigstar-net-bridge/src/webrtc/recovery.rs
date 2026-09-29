//! Rebuild only the WAN endpoint. The UDP socket and learned emulator address
//! live for the whole match, including reconnects.
use super::*;
use serde_json::{json, Value};
use tokio_tungstenite::{MaybeTlsStream, WebSocketStream};

type SignalSocket = WebSocketStream<MaybeTlsStream<TcpStream>>;
type Error = Box<dyn std::error::Error + Send + Sync>;

#[derive(Debug)]
struct NewConnection(u64);

impl std::fmt::Display for NewConnection {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "signaling connection superseded by {}", self.0)
    }
}
impl std::error::Error for NewConnection {}

async fn message(ws: &mut SignalSocket) -> Result<Value, Error> {
    loop {
        let item = ws
            .next()
            .await
            .ok_or_else(|| io::Error::other("signaling closed"))??;
        match item {
            WebSocketMessage::Text(text) => {
                let value: Value = serde_json::from_str(&text)?;
                if value["type"] == "error" {
                    return Err(
                        io::Error::other(format!("signaling error: {}", value["error"])).into(),
                    );
                }
                return Ok(value);
            }
            WebSocketMessage::Close(_) => return Err(io::Error::other("signaling closed").into()),
            _ => {}
        }
    }
}

fn connection_id(value: &Value) -> Option<u64> {
    (value["type"] == "ready-for-offer")
        .then(|| value["connectionId"].as_u64())
        .flatten()
}

async fn wait_ready(ws: &mut SignalSocket, after: u64) -> Result<u64, Error> {
    loop {
        let value = message(ws).await?;
        if let Some(id) = connection_id(&value).filter(|id| *id > after) {
            return Ok(id);
        }
    }
}

async fn wait_sdp(ws: &mut SignalSocket, id: u64, kind: &str) -> Result<String, Error> {
    loop {
        let value = message(ws).await?;
        if let Some(new_id) = connection_id(&value).filter(|new_id| *new_id > id) {
            return Err(Box::new(NewConnection(new_id)));
        }
        if value["type"] == "sdp" && value["sdpType"] == kind && value["connectionId"] == id {
            return value["sdp"]
                .as_str()
                .map(str::to_owned)
                .ok_or_else(|| io::Error::other("missing SDP").into());
        }
    }
}

async fn send_sdp(
    ws: &mut SignalSocket,
    endpoint: &WebRtcEndpoint,
    id: u64,
    kind: &str,
) -> Result<(), Error> {
    let description = endpoint
        .peer_connection
        .local_description()
        .ok_or_else(|| io::Error::other("missing local SDP"))?;
    ws.send(WebSocketMessage::Text(
        json!({
            "type": "sdp", "sdpType": kind, "sdp": description.sdp.to_string(), "connectionId": id,
        })
        .to_string()
        .into(),
    ))
    .await?;
    Ok(())
}

async fn negotiate(
    ws: &mut SignalSocket,
    id: u64,
    role: &str,
    ice: &[String],
    reporter: &mut DiagnosticsReporter,
) -> Result<WebRtcEndpoint, Error> {
    let offer = if role == "answer" {
        Some(wait_sdp(ws, id, "offer").await?)
    } else {
        None
    };
    let mut endpoint = create_endpoint(ice.to_vec(), reporter)
        .await
        .map_err(|error| io::Error::other(error.to_string()))?;
    let remote = if let Some(offer) = offer {
        endpoint
            .peer_connection
            .set_local_description(datachannel_wrapper::SdpType::Rollback)?;
        offer
    } else {
        send_sdp(ws, &endpoint, id, "offer").await?;
        wait_sdp(ws, id, "answer").await?
    };
    let remote_kind = if role == "answer" { "offer" } else { "answer" };
    reporter.observe_remote_sdp(remote_kind, &remote);
    endpoint
        .peer_connection
        .set_remote_description(datachannel_wrapper::SessionDescription {
            sdp_type: sdp_type_from_str(remote_kind)?,
            sdp: datachannel_wrapper::sdp::parse_sdp(&remote, false)?,
        })?;
    if role == "answer" {
        send_sdp(ws, &endpoint, id, "answer").await?;
    }
    wait_connected(&mut endpoint, reporter)
        .await
        .map_err(|error| io::Error::other(error.to_string()))?;
    Ok(endpoint)
}

async fn monitor_signal(ws: &mut SignalSocket, id: u64) -> Result<u64, Error> {
    let mut ping = tokio::time::interval(Duration::from_secs(5));
    let mut last_received = Instant::now();
    loop {
        tokio::select! {
            value = message(ws) => {
                let value = value?;
                last_received = Instant::now();
                if let Some(next) = connection_id(&value).filter(|next| *next > id) { return Ok(next); }
            }
            _ = ping.tick() => {
                if last_received.elapsed() >= Duration::from_secs(15) {
                    return Err(io::Error::other("signaling heartbeat timed out").into());
                }
                ws.send(WebSocketMessage::Text(json!({"type":"ping"}).to_string().into())).await?;
            }
        }
    }
}

async fn request_restart(ws: &mut SignalSocket, id: u64) -> Result<(), Error> {
    ws.send(WebSocketMessage::Text(
        json!({"type":"restart", "connectionId":id})
            .to_string()
            .into(),
    ))
    .await?;
    Ok(())
}

pub(super) async fn run(config: SignalingWebRtcConfig) -> Result<(), Error> {
    let role = role_from_side(&config.side);
    let mut reporter = DiagnosticsReporter::new(config.status_file, role);
    reporter.set_signaling(
        config.signal_url.split('?').next().unwrap_or_default(),
        &config.session,
    );
    let socket = TokioUdpSocket::bind(config.local_bind).await?;
    let target = Arc::new(TokioMutex::new(config.local_target));
    let url = format!(
        "{}&recovery=1",
        build_signal_url(&config.signal_url, &config.session, role)
    );
    // Authentication query parameters are deliberately absent from this log.
    println!(
        "bigstar-net-bridge recovery: started role={role} local={}",
        socket.local_addr()?
    );
    let mut ws: Option<SignalSocket> = None;
    let mut ice = config.stun_servers;
    if !ice.is_empty() {
        reporter.set_ice_servers(ice.clone(), "command-line");
    }
    let mut pending_id = None;
    let mut last_id = 0;
    let mut deadline = Instant::now() + Duration::from_secs(45);
    loop {
        let attempt = async {
            while test_outage_active() {
                tokio::time::sleep(Duration::from_millis(100)).await;
            }
            if ws.is_none() {
                let (mut signal, _) = connect_async(&url).await?;
                let hello = wait_signal_hello(&mut signal)
                    .await
                    .map_err(|error| io::Error::other(error.to_string()))?;
                if hello["recoveryVersion"] != 1 {
                    return Err(io::Error::new(
                        io::ErrorKind::Unsupported,
                        "server does not support reconnect; update signaling server",
                    )
                    .into());
                }
                if ice.is_empty() {
                    ice = parse_server_ice_servers(&hello);
                    if ice.is_empty() && config.fallback_to_default_stun {
                        ice.push(DEFAULT_STUN_SERVER.to_owned());
                    }
                    reporter.set_ice_servers(ice.clone(), "signaling-server");
                }
                ws = Some(signal);
                last_id = 0;
            }
            let signal = ws.as_mut().expect("signaling connected");
            let id = match pending_id.take() {
                Some(id) => id,
                None => wait_ready(signal, last_id).await?,
            };
            last_id = id;
            // One broken ICE attempt must leave time for another attempt.
            match tokio::time::timeout(
                Duration::from_secs(12),
                negotiate(signal, id, role, &ice, &mut reporter),
            )
            .await
            {
                Ok(result) => result,
                Err(_) => Err(io::Error::new(
                    io::ErrorKind::TimedOut,
                    "WebRTC negotiation timed out",
                )
                .into()),
            }
        };
        let established = {
            let connection =
                tokio::time::timeout_at(tokio::time::Instant::from_std(deadline), attempt);
            tokio::pin!(connection);
            let mut discarded = [0u8; 65536];
            loop {
                tokio::select! {
                    result = &mut connection => break result,
                    received = socket.recv_from(&mut discarded) => {
                        // UDP packets belong to the unavailable transport. Replaying
                        // old ENet connect requests can occupy the server's only
                        // peer slot with a connection the client already abandoned.
                        // ENet and the input protocol retransmit what is still needed.
                        if let Err(error) = received {
                            if error.kind() != io::ErrorKind::ConnectionReset {
                                return Err(error.into());
                            }
                        }
                    }
                }
            }
        };
        let endpoint = match established {
            Ok(Ok(endpoint)) => endpoint,
            Err(_) => {
                let error = io::Error::new(io::ErrorKind::TimedOut, "reconnect deadline exceeded");
                reporter.fail(&error);
                return Err(error.into());
            }
            Ok(Err(error)) => {
                if let Some(next) = error.downcast_ref::<NewConnection>() {
                    pending_id = Some(next.0);
                    continue;
                }
                if error
                    .downcast_ref::<io::Error>()
                    .is_some_and(|e| e.kind() == io::ErrorKind::Unsupported)
                {
                    reporter.fail(error.as_ref());
                    return Err(error);
                }
                println!("bigstar-net-bridge recovery: negotiation retry");
                if error
                    .downcast_ref::<io::Error>()
                    .is_some_and(|e| e.kind() == io::ErrorKind::TimedOut)
                {
                    if let Some(signal) = ws.as_mut() {
                        if request_restart(signal, last_id).await.is_err() {
                            ws = None;
                        }
                    }
                } else {
                    ws = None;
                }
                tokio::time::sleep(Duration::from_millis(500)).await;
                continue;
            }
        };
        reporter.finish_recovery();
        println!("bigstar-net-bridge recovery: connected connectionId={last_id}");
        let signal = ws.as_mut().expect("connected endpoint has signaling");
        let restart = tokio::select! {
            result = run_webrtc_udp_tunnel_on_socket(endpoint, &socket, &target, config.local_target.is_some(), &mut reporter) => {
                if let Err(error) = result { println!("bigstar-net-bridge recovery: tunnel ended: {error}"); }
                true
            }
            result = monitor_signal(signal, last_id) => {
                match result {
                    Ok(id) => pending_id = Some(id),
                    Err(_) => { ws = None; }
                }
                false
            }
        };
        if restart {
            if let Some(signal) = ws.as_mut() {
                if request_restart(signal, last_id).await.is_err() {
                    ws = None;
                }
            }
        }
        deadline = Instant::now() + Duration::from_secs(60);
        reporter.begin_recovery();
    }
}
