#include "NsmbNetplayTransport.h"

#include <mutex>

namespace NsmbNetplayTransport {
namespace {

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

void Transport::HandleConnected(ENetPeer *peer) {
  Peer = peer;
  if (ConnectingPeer == peer)
    ConnectingPeer = nullptr;
}

void Transport::HandleDisconnected(ENetPeer *peer) {
  if (Peer == peer)
    Peer = nullptr;
  if (ConnectingPeer == peer)
    ConnectingPeer = nullptr;
}

int Transport::Service(ENetEvent &event, std::uint32_t timeoutMs) {
  return Host ? enet_host_service(Host, &event, timeoutMs) : 0;
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
