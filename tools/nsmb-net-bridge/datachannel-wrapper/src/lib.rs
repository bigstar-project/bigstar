pub use datachannel::{
    sdp, CandidatePair, ConnectionState, DataChannelInit, GatheringState, IceCandidate, IceState,
    Reliability, RtcConfig, SdpType, SessionDescription, SignalingState, TransportPolicy,
};

pub struct PeerConnection {
    peer_conn: Box<datachannel::RtcPeerConnection<PeerConnectionHandler>>,
    data_channel_rx: tokio::sync::mpsc::Receiver<DataChannel>,
}

type PendingDataChannelReceiver = (
    tokio::sync::mpsc::Receiver<Vec<u8>>,
    std::sync::Arc<tokio::sync::Mutex<DataChannelState>>,
);

impl PeerConnection {
    pub fn new(
        config: RtcConfig,
    ) -> Result<
        (
            Self,
            tokio::sync::mpsc::Receiver<PeerConnectionEvent>,
        ),
        std::io::Error,
    > {
        let (event_tx, event_rx) = tokio::sync::mpsc::channel(32);
        let (data_channel_tx, data_channel_rx) = tokio::sync::mpsc::channel(1);
        let handler = PeerConnectionHandler {
            event_tx,
            pending_dc_receiver: None,
            data_channel_tx,
        };
        let peer_conn =
            datachannel::RtcPeerConnection::new(&config, handler).map_err(datachannel_error)?;
        Ok((
            Self {
                peer_conn,
                data_channel_rx,
            },
            event_rx,
        ))
    }

    pub fn create_data_channel(
        &mut self,
        label: &str,
        init: DataChannelInit,
    ) -> Result<DataChannel, std::io::Error> {
        let (message_tx, message_rx) = tokio::sync::mpsc::channel(32);
        let (open_tx, open_rx) = tokio::sync::oneshot::channel();
        let state = std::sync::Arc::new(tokio::sync::Mutex::new(DataChannelState {
            open_rx: Some(open_rx),
            error: None,
        }));
        let handler = DataChannelHandler {
            message_tx: Some(message_tx),
            open_tx: Some(open_tx),
            state: state.clone(),
        };
        let dc = self
            .peer_conn
            .create_data_channel_ex(label, handler, &init)
            .map_err(datachannel_error)?;
        Ok(DataChannel {
            sender: DataChannelSender { state, dc },
            receiver: DataChannelReceiver { message_rx },
        })
    }

    pub async fn accept(&mut self) -> Option<DataChannel> {
        self.data_channel_rx.recv().await
    }

    pub fn set_local_description(&mut self, sdp_type: SdpType) -> Result<(), std::io::Error> {
        self.peer_conn
            .set_local_description(sdp_type)
            .map_err(datachannel_error)
    }

    pub fn set_remote_description(
        &mut self,
        description: SessionDescription,
    ) -> Result<(), std::io::Error> {
        self.peer_conn
            .set_remote_description(&description)
            .map_err(datachannel_error)
    }

    pub fn local_description(&self) -> Option<SessionDescription> {
        self.peer_conn.local_description()
    }

    pub fn remote_description(&self) -> Option<SessionDescription> {
        self.peer_conn.remote_description()
    }

    pub fn add_remote_candidate(&mut self, candidate: IceCandidate) -> Result<(), std::io::Error> {
        self.peer_conn
            .add_remote_candidate(&candidate)
            .map_err(datachannel_error)
    }

    pub fn local_address(&self) -> Option<String> {
        self.peer_conn.local_address()
    }

    pub fn remote_address(&self) -> Option<String> {
        self.peer_conn.remote_address()
    }

    pub fn selected_candidate_pair(&self) -> Option<CandidatePair> {
        self.peer_conn.selected_candidate_pair()
    }
}

struct PeerConnectionHandler {
    event_tx: tokio::sync::mpsc::Sender<PeerConnectionEvent>,
    pending_dc_receiver: Option<PendingDataChannelReceiver>,
    data_channel_tx: tokio::sync::mpsc::Sender<DataChannel>,
}

#[derive(Debug)]
pub enum PeerConnectionEvent {
    SessionDescription(Box<SessionDescription>),
    IceCandidate(IceCandidate),
    ConnectionStateChange(ConnectionState),
    GatheringStateChange(GatheringState),
    SignalingStateChange(SignalingState),
    IceStateChange(IceState),
}

impl datachannel::PeerConnectionHandler for PeerConnectionHandler {
    type DCH = DataChannelHandler;

    fn data_channel_handler(&mut self, _info: datachannel::DataChannelInfo) -> Self::DCH {
        let (message_tx, message_rx) = tokio::sync::mpsc::channel(32);
        let (open_tx, open_rx) = tokio::sync::oneshot::channel();
        let state = std::sync::Arc::new(tokio::sync::Mutex::new(DataChannelState {
            open_rx: Some(open_rx),
            error: None,
        }));
        self.pending_dc_receiver = Some((message_rx, state.clone()));
        DataChannelHandler {
            message_tx: Some(message_tx),
            open_tx: Some(open_tx),
            state,
        }
    }

    fn on_description(&mut self, description: SessionDescription) {
        let _ = self
            .event_tx
            .blocking_send(PeerConnectionEvent::SessionDescription(Box::new(description)));
    }

    fn on_candidate(&mut self, candidate: IceCandidate) {
        let _ = self
            .event_tx
            .blocking_send(PeerConnectionEvent::IceCandidate(candidate));
    }

    fn on_connection_state_change(&mut self, state: ConnectionState) {
        send_callback_event(
            &self.event_tx,
            PeerConnectionEvent::ConnectionStateChange(state),
        );
    }

    fn on_gathering_state_change(&mut self, state: GatheringState) {
        let _ = self
            .event_tx
            .blocking_send(PeerConnectionEvent::GatheringStateChange(state));
    }

    fn on_signaling_state_change(&mut self, state: SignalingState) {
        let _ = self
            .event_tx
            .blocking_send(PeerConnectionEvent::SignalingStateChange(state));
    }

    fn on_ice_state_change(&mut self, state: IceState) {
        send_callback_event(&self.event_tx, PeerConnectionEvent::IceStateChange(state));
    }

    fn on_data_channel(&mut self, dc: Box<datachannel::RtcDataChannel<Self::DCH>>) {
        let Some((message_rx, state)) = self.pending_dc_receiver.take() else {
            return;
        };
        let _ = self.data_channel_tx.blocking_send(DataChannel {
            sender: DataChannelSender { state, dc },
            receiver: DataChannelReceiver { message_rx },
        });
    }
}

fn send_callback_event(
    tx: &tokio::sync::mpsc::Sender<PeerConnectionEvent>,
    event: PeerConnectionEvent,
) {
    match tokio::runtime::Handle::try_current() {
        Ok(_) => {
            let _ = tx.try_send(event);
        }
        Err(_) => {
            let _ = tx.blocking_send(event);
        }
    }
}

struct DataChannelState {
    open_rx: Option<tokio::sync::oneshot::Receiver<()>>,
    error: Option<String>,
}

pub struct DataChannel {
    sender: DataChannelSender,
    receiver: DataChannelReceiver,
}

impl DataChannel {
    pub async fn send(&mut self, message: &[u8]) -> Result<(), std::io::Error> {
        self.sender.send(message).await
    }

    pub async fn receive(&mut self) -> Option<Vec<u8>> {
        self.receiver.receive().await
    }

    pub fn split(self) -> (DataChannelSender, DataChannelReceiver) {
        (self.sender, self.receiver)
    }
}

pub struct DataChannelSender {
    state: std::sync::Arc<tokio::sync::Mutex<DataChannelState>>,
    dc: Box<datachannel::RtcDataChannel<DataChannelHandler>>,
}

impl DataChannelSender {
    pub async fn send(&mut self, message: &[u8]) -> Result<(), std::io::Error> {
        let mut state = self.state.lock().await;
        if let Some(error) = &state.error {
            return Err(std::io::Error::other(error.clone()));
        }
        if let Some(open_rx) = state.open_rx.take() {
            open_rx.await.map_err(|_| {
                std::io::Error::new(std::io::ErrorKind::NotConnected, "not connected")
            })?;
        }
        self.dc.send(message).map_err(datachannel_error)
    }

    pub fn unsplit(self, receiver: DataChannelReceiver) -> DataChannel {
        DataChannel {
            sender: self,
            receiver,
        }
    }
}

pub struct DataChannelReceiver {
    message_rx: tokio::sync::mpsc::Receiver<Vec<u8>>,
}

impl DataChannelReceiver {
    pub async fn receive(&mut self) -> Option<Vec<u8>> {
        self.message_rx.recv().await
    }

    pub fn unsplit(self, sender: DataChannelSender) -> DataChannel {
        sender.unsplit(self)
    }
}

struct DataChannelHandler {
    state: std::sync::Arc<tokio::sync::Mutex<DataChannelState>>,
    open_tx: Option<tokio::sync::oneshot::Sender<()>>,
    message_tx: Option<tokio::sync::mpsc::Sender<Vec<u8>>>,
}

impl datachannel::DataChannelHandler for DataChannelHandler {
    fn on_open(&mut self) {
        if let Some(tx) = self.open_tx.take() {
            let _ = tx.send(());
        }
    }

    fn on_closed(&mut self) {
        self.message_tx = None;
    }

    fn on_error(&mut self, error: &str) {
        self.state.blocking_lock().error = Some(error.to_owned());
    }

    fn on_message(&mut self, message: &[u8]) {
        if let Some(tx) = self.message_tx.as_mut() {
            let _ = tx.blocking_send(message.to_vec());
        }
    }

    fn on_buffered_amount_low(&mut self) {}

    fn on_available(&mut self) {}
}

fn datachannel_error(error: datachannel::Error) -> std::io::Error {
    match error {
        datachannel::Error::InvalidArg => {
            std::io::Error::new(std::io::ErrorKind::InvalidInput, "invalid argument")
        }
        datachannel::Error::Runtime => std::io::Error::other("runtime error"),
        datachannel::Error::NotAvailable => {
            std::io::Error::new(std::io::ErrorKind::WouldBlock, "not available")
        }
        datachannel::Error::TooSmall => {
            std::io::Error::new(std::io::ErrorKind::InvalidInput, "buffer too small")
        }
        datachannel::Error::Unkown => std::io::Error::other("unknown"),
        datachannel::Error::BadString(value) => {
            std::io::Error::new(std::io::ErrorKind::InvalidInput, format!("bad string: {value}"))
        }
    }
}
