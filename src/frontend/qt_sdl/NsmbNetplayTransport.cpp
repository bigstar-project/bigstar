#include "NsmbNetplayTransport.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace NsmbNetplayTransport {
namespace {

struct PeerSnapshot {
  enet_uint32 Now = 0, State = 0, AckAge = 0, EarliestTimeout = 0;
  enet_uint32 NextTimeout = 0, Minimum = 0, Maximum = 0, Limit = 0;
  enet_uint32 Rtt = 0, Variance = 0;
  struct Command {
    unsigned Kind = 0, Sequence = 0, Attempts = 0;
    enet_uint32 Age = 0, Timeout = 0;
  };
  std::array<Command, 8> Commands{};
  std::size_t Pending = 0, Queued = 0;
};

PeerSnapshot Snapshot(ENetPeer *peer) {
  PeerSnapshot s;
  if (!peer)
    return s;
  s.Now = enet_time_get();
  s.State = peer->state;
  s.AckAge = s.Now - peer->lastReceiveTime;
  s.EarliestTimeout = peer->earliestTimeout;
  s.NextTimeout = peer->nextTimeout;
  s.Minimum = peer->timeoutMinimum;
  s.Maximum = peer->timeoutMaximum;
  s.Limit = peer->timeoutLimit;
  s.Rtt = peer->roundTripTime;
  s.Variance = peer->roundTripTimeVariance;
  s.Pending = enet_list_size(&peer->sentReliableCommands);
  std::size_t index = 0;
  for (auto it = enet_list_begin(&peer->sentReliableCommands);
       it != enet_list_end(&peer->sentReliableCommands) &&
       index < s.Commands.size();
       it = enet_list_next(it), ++index) {
    const auto *command = reinterpret_cast<const ENetOutgoingCommand *>(it);
    s.Commands[index] = {
        unsigned(command->command.header.command & ENET_PROTOCOL_COMMAND_MASK),
        command->reliableSequenceNumber, command->sendAttempts,
        s.Now - command->sentTime, command->roundTripTimeout};
  }
  s.Queued = enet_list_size(&peer->outgoingCommands) +
             enet_list_size(&peer->outgoingSendReliableCommands);
  return s;
}

void TracePeer(const char *event, const PeerSnapshot &s) {
  // ENet resets its peer before delivering a disconnect event. Capture before
  // service; ACK age is not the age of the last gameplay packet.
  std::printf(
      "NSMB ENet: event=%s clock=%u state=%u ackAgeMs=%u earliest=%u "
      "nextTimeout=%u minimum=%u maximum=%u limit=%u rtt=%u variance=%u "
      "pending=%zu queued=%zu",
      event, s.Now, s.State, s.AckAge, s.EarliestTimeout, s.NextTimeout,
      s.Minimum, s.Maximum, s.Limit, s.Rtt, s.Variance, s.Pending, s.Queued);
  for (std::size_t i = 0; i < s.Pending && i < s.Commands.size(); ++i) {
    const auto &c = s.Commands[i];
    std::printf(" command=%u/%u/attempts:%u/age:%u/timeout:%u", c.Kind,
                c.Sequence, c.Attempts, c.Age, c.Timeout);
  }
  std::printf("\n");
  std::fflush(stdout);
}

std::mutex &GetENetMutex() {
  static auto *mutex = new std::mutex;
  return *mutex;
}

int &GetENetReferenceCount() {
  static auto *referenceCount = new int(0);
  return *referenceCount;
}

bool AcquireENet() {
  std::lock_guard<std::mutex> lock(GetENetMutex());
  int &referenceCount = GetENetReferenceCount();
  if (referenceCount == 0 && enet_initialize() != 0)
    return false;

  referenceCount++;
  return true;
}

void ReleaseENet() {
  std::lock_guard<std::mutex> lock(GetENetMutex());
  int &referenceCount = GetENetReferenceCount();
  if (referenceCount <= 0)
    return;

  referenceCount--;
  if (referenceCount == 0)
    enet_deinitialize();
}

} // namespace

Transport::~Transport() { Shutdown(); }

InitializeResult Transport::Initialize(const InitializeOptions &options) {
  Shutdown();
  Options = options;
  const char *trace = std::getenv("MELONDS_NSML_ENET_TRACE");
  TraceENet = trace && trace[0] == '1';
  LastTraceTime = 0;
  if (!AcquireENet())
    return InitializeResult::ENetInitializationFailed;
  ENetAcquired = true;

  if (!options.Client) {
    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = options.Port;
    Host = enet_host_create(&address, 1, 1, 0, 0);
  } else {
    Host = enet_host_create(nullptr, 1, 1, 0, 0);
    if (Host) {
      ENetAddress address{};
      enet_address_set_host(&address, options.PeerHost.c_str());
      address.port = options.Port;
      ConnectingPeer = enet_host_connect(Host, &address, 1, 0);
    }
  }

  if (!Host) {
    Shutdown();
    return InitializeResult::HostCreationFailed;
  }
  return InitializeResult::Success;
}

void Transport::Shutdown() {
  if (Host)
    enet_host_destroy(Host);
  Host = nullptr;
  ConnectingPeer = nullptr;
  Peer = nullptr;

  if (ENetAcquired) {
    ReleaseENet();
    ENetAcquired = false;
  }
}

bool Transport::HasHost() const { return Host != nullptr; }

bool Transport::IsConnected() const { return Peer != nullptr; }

bool Transport::IsConnecting() const { return ConnectingPeer != nullptr; }

bool Transport::IsPeer(const ENetPeer *peer) const { return Peer == peer; }

int Transport::PeerState() const {
  return Peer ? static_cast<int>(Peer->state) : -1;
}

int Transport::ConnectingPeerState() const {
  return ConnectingPeer ? static_cast<int>(ConnectingPeer->state) : -1;
}

std::uint16_t Transport::BoundPort() const {
  return Host ? Host->address.port : 0;
}

void Transport::HandleConnected(ENetPeer *peer, bool recoverable) {
  Peer = peer;
  if (recoverable)
    enet_peer_timeout(peer, 32, 3000, 5000);
  if (ConnectingPeer == peer)
    ConnectingPeer = nullptr;
}

void Transport::HandleDisconnected(ENetPeer *peer) {
  if (Peer == peer)
    Peer = nullptr;
  if (ConnectingPeer == peer)
    ConnectingPeer = nullptr;
}

void Transport::RetryConnection() {
  if (!Host || !Options.Client || Peer || ConnectingPeer)
    return;
  ENetAddress address{};
  if (enet_address_set_host(&address, Options.PeerHost.c_str()) != 0)
    return;
  address.port = Options.Port;
  ConnectingPeer = enet_host_connect(Host, &address, 1, 0);
  if (ConnectingPeer)
    enet_peer_timeout(ConnectingPeer, 32, 1000, 3000);
}

int Transport::Service(ENetEvent &event, std::uint32_t timeoutMs) {
  if (!Host) return 0;
  // Detailed snapshots are opt-in and only taken once ACKs are overdue. This
  // keeps the regular high-frequency network pump free of list walks.
  auto *peer = Peer ? Peer : ConnectingPeer;
  const bool overdue =
      TraceENet && peer && enet_time_get() - peer->lastReceiveTime >= 1000;
  const auto snapshot = TraceENet && overdue ? Snapshot(peer) : PeerSnapshot{};
  const int result = enet_host_service(Host, &event, timeoutMs);
  if (TraceENet && overdue && result > 0 &&
      event.type == ENET_EVENT_TYPE_DISCONNECT)
    TracePeer("disconnect-before-service", snapshot);
  else if (TraceENet && snapshot.State == ENET_PEER_STATE_CONNECTED &&
           snapshot.AckAge >= 1000 && snapshot.Now - LastTraceTime >= 250) {
    TracePeer("ack-stall-before-service", snapshot);
    LastTraceTime = snapshot.Now;
  }
  return result;
}

int Transport::Send(const void *data, std::size_t size, std::uint32_t flags,
                    bool flush) {
  if (!Peer)
    return SendUnavailable;

  ENetPacket *packet = enet_packet_create(data, size, flags);
  if (!packet)
    return SendUnavailable;

  const int result = enet_peer_send(Peer, 0, packet);
  if (result != 0)
    enet_packet_destroy(packet);
  if (flush)
    Flush();
  return result;
}

void Transport::Flush() {
  if (Host)
    enet_host_flush(Host);
}

} // namespace NsmbNetplayTransport
