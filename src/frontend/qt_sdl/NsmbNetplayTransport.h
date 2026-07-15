#ifndef NSMBNETPLAYTRANSPORT_H
#define NSMBNETPLAYTRANSPORT_H

#include <cstddef>
#include <cstdint>
#include <string>

#include <enet/enet.h>

namespace NsmbNetplayTransport {

inline constexpr int SendUnavailable = -2;

struct InitializeOptions {
  bool Client = false;
  std::uint16_t Port = 8065;
  std::string PeerHost = "127.0.0.1";
};

enum class InitializeResult {
  Success,
  ENetInitializationFailed,
  HostCreationFailed,
};

class Transport {
public:
  Transport() = default;
  ~Transport();

  Transport(const Transport &) = delete;
  Transport &operator=(const Transport &) = delete;

  InitializeResult Initialize(const InitializeOptions &options);
  void Shutdown();

  bool HasHost() const;
  bool IsConnected() const;
  bool IsConnecting() const;
  bool IsPeer(const ENetPeer *peer) const;
  int PeerState() const;
  int ConnectingPeerState() const;
  std::uint16_t BoundPort() const;

  void HandleConnected(ENetPeer *peer);
  void HandleDisconnected(ENetPeer *peer);

  int Service(ENetEvent &event, std::uint32_t timeoutMs = 0);
  int Send(const void *data, std::size_t size, std::uint32_t flags, bool flush);
  void Flush();

private:
  ENetHost *Host = nullptr;
  ENetPeer *ConnectingPeer = nullptr;
  ENetPeer *Peer = nullptr;
  bool ENetAcquired = false;
};

} // namespace NsmbNetplayTransport

#endif
