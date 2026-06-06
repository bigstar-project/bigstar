use std::fs;
use std::io;
use std::path::PathBuf;
use std::time::Instant;

use serde_json::json;

use super::{candidate_by_address, candidate_type, selected_route, WebRtcEndpoint};

#[derive(Default)]
pub(super) struct PacketStats {
    pub(super) app_to_webrtc_packets: u64,
    pub(super) app_to_webrtc_bytes: u64,
    pub(super) webrtc_to_app_packets: u64,
    pub(super) webrtc_to_app_bytes: u64,
    pub(super) dropped_no_local_target: u64,
}

pub(super) struct DiagnosticsReporter {
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
    remote_candidates: Vec<String>,
    selected_local_candidate: Option<String>,
    selected_remote_candidate: Option<String>,
    selected_route: Option<String>,
    local_address: Option<String>,
    remote_address: Option<String>,
    last_error: Option<String>,
    started_at: Instant,
    pub(super) stats: PacketStats,
}

impl DiagnosticsReporter {
    pub(super) fn new(status_file: Option<PathBuf>, role: impl Into<String>) -> Self {
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
            remote_candidates: Vec::new(),
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

    pub(super) fn set_phase(&mut self, phase: &str) {
        self.phase = phase.to_owned();
        println!("nsmb-net-bridge diagnostics: phase={phase}");
        self.persist();
    }

    pub(super) fn set_signaling(&mut self, url: &str, session: &str) {
        self.signal_url = Some(url.to_owned());
        self.session = Some(session.to_owned());
        println!(
            "nsmb-net-bridge diagnostics: signalingUrl={} session={} role={}",
            url, session, self.role
        );
        self.persist();
    }

    pub(super) fn set_ice_servers(&mut self, servers: Vec<String>, source: &str) {
        self.ice_servers = servers;
        println!(
            "nsmb-net-bridge diagnostics: iceServers source={} values={:?}",
            source, self.ice_servers
        );
        self.persist();
    }

    pub(super) fn observe_remote_sdp(&mut self, label: &str, sdp: &str) {
        let mut candidates = Vec::new();
        for line in sdp.lines().map(str::trim) {
            if line.starts_with("a=candidate:") || line.starts_with("candidate:") {
                candidates.push(line.strip_prefix("a=").unwrap_or(line).to_owned());
            }
        }
        println!(
            "nsmb-net-bridge diagnostics: remoteSdp={} candidates={}",
            label,
            candidates.len()
        );
        self.remote_candidates = candidates;
        self.persist();
    }

    pub(super) fn observe_event(&mut self, event: &datachannel_wrapper::PeerConnectionEvent) {
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

    pub(super) fn observe_selected_pair(&mut self, endpoint: &WebRtcEndpoint) {
        self.local_address = endpoint.peer_connection.local_address();
        self.remote_address = endpoint.peer_connection.remote_address();
        let Some(local_address) = self.local_address.as_deref() else {
            self.persist();
            return;
        };
        let Some(remote_address) = self.remote_address.as_deref() else {
            self.persist();
            return;
        };
        let Some(local_candidate) = candidate_by_address(&self.local_candidates, local_address)
        else {
            self.persist();
            return;
        };
        let Some(remote_candidate) = candidate_by_address(&self.remote_candidates, remote_address)
        else {
            self.persist();
            return;
        };
        let route = selected_route(local_candidate, remote_candidate);
        if self.selected_local_candidate.as_deref() != Some(local_candidate)
            || self.selected_remote_candidate.as_deref() != Some(remote_candidate)
        {
            println!(
                    "nsmb-net-bridge webrtc: selected candidate pair route={} local={} remote={} localAddress={} remoteAddress={}",
                    route, local_candidate, remote_candidate, local_address, remote_address
                );
        }
        self.selected_local_candidate = Some(local_candidate.to_owned());
        self.selected_remote_candidate = Some(remote_candidate.to_owned());
        self.selected_route = Some(route.to_owned());
        self.persist();
    }

    pub(super) fn observe_selected_addresses(
        &mut self,
        local_address: Option<String>,
        remote_address: Option<String>,
    ) {
        self.local_address = local_address;
        self.remote_address = remote_address;
        let Some(local_address) = self.local_address.as_deref() else {
            self.persist();
            return;
        };
        let Some(remote_address) = self.remote_address.as_deref() else {
            self.persist();
            return;
        };
        let Some(local_candidate) = candidate_by_address(&self.local_candidates, local_address)
        else {
            self.persist();
            return;
        };
        let Some(remote_candidate) = candidate_by_address(&self.remote_candidates, remote_address)
        else {
            self.persist();
            return;
        };
        let route = selected_route(local_candidate, remote_candidate);
        if self.selected_local_candidate.as_deref() != Some(local_candidate)
            || self.selected_remote_candidate.as_deref() != Some(remote_candidate)
        {
            println!(
                    "nsmb-net-bridge webrtc: selected candidate pair route={} local={} remote={} localAddress={} remoteAddress={}",
                    route, local_candidate, remote_candidate, local_address, remote_address
                );
        }
        self.selected_local_candidate = Some(local_candidate.to_owned());
        self.selected_remote_candidate = Some(remote_candidate.to_owned());
        self.selected_route = Some(route.to_owned());
        self.persist();
    }

    pub(super) fn fail(&mut self, error: &dyn std::fmt::Display) {
        self.phase = "failed".to_owned();
        self.last_error = Some(error.to_string());
        eprintln!("nsmb-net-bridge diagnostics: failed: {error}");
        self.persist();
    }

    pub(super) fn persist(&self) {
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
            "remote_candidates": self.remote_candidates,
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
