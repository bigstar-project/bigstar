#include "NsmbNetplayTransport.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

int Failures = 0;

void Expect(bool condition, const char *message) {
  if (condition)
    return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  Failures++;
}

bool PumpUntilConnected(NsmbNetplayTransport::Transport &host,
                        NsmbNetplayTransport::Transport &client) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline) {
    for (NsmbNetplayTransport::Transport *transport : {&host, &client}) {
      ENetEvent event{};
      while (transport->Service(event) > 0) {
        if (event.type == ENET_EVENT_TYPE_CONNECT)
          transport->HandleConnected(event.peer);
        else if (event.type == ENET_EVENT_TYPE_DISCONNECT)
          transport->HandleDisconnected(event.peer);
        else if (event.type == ENET_EVENT_TYPE_RECEIVE)
          enet_packet_destroy(event.packet);
      }
    }
    if (host.IsConnected() && client.IsConnected())
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}

void TestLoopbackSendReceive() {
  NsmbNetplayTransport::Transport host;
  std::uint16_t port = 0;
  for (std::uint16_t candidate = 38650; candidate < 38750; candidate++) {
    if (host.Initialize({false, candidate, "127.0.0.1"}) ==
        NsmbNetplayTransport::InitializeResult::Success) {
      port = candidate;
      break;
    }
  }
  Expect(port != 0, "host binds an available loopback port");
  if (port == 0)
    return;

  Expect(host.HasHost(), "host owns an ENet host");
  Expect(host.BoundPort() == port, "host reports its bound port");
  Expect(!host.IsConnected(), "host begins disconnected");

  NsmbNetplayTransport::Transport client;
  Expect(client.Initialize({true, port, "127.0.0.1"}) ==
             NsmbNetplayTransport::InitializeResult::Success,
         "client creates an ENet host");
  Expect(client.IsConnecting(), "client queues its peer connection");
  Expect(PumpUntilConnected(host, client),
         "host and client connect over loopback");
  if (!host.IsConnected() || !client.IsConnected())
    return;

  Expect(host.PeerState() >= 0, "host exposes connected peer state");
  Expect(client.ConnectingPeerState() == -1,
         "client clears connecting peer after connect");

  constexpr std::array<unsigned char, 8> payload{
      0x4E, 0x53, 0x4D, 0x42, 0x10, 0x20, 0x30, 0x40,
  };
  Expect(client.Send(payload.data(), payload.size(), ENET_PACKET_FLAG_RELIABLE,
                     true) == 0,
         "client queues a reliable packet");

  bool received = false;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!received && std::chrono::steady_clock::now() < deadline) {
    ENetEvent event{};
    while (host.Service(event) > 0) {
      if (event.type == ENET_EVENT_TYPE_RECEIVE) {
        received = event.packet->dataLength == payload.size() &&
                   std::memcmp(event.packet->data, payload.data(),
                               payload.size()) == 0;
        enet_packet_destroy(event.packet);
      } else if (event.type == ENET_EVENT_TYPE_CONNECT) {
        host.HandleConnected(event.peer);
      } else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
        host.HandleDisconnected(event.peer);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  Expect(received, "host receives the exact packet bytes");

  client.Shutdown();
  Expect(!client.HasHost(), "client shutdown releases its host");
  host.Shutdown();
  Expect(!host.HasHost(), "host shutdown releases its host");
}

} // namespace

int main() {
  TestLoopbackSendReceive();
  if (Failures != 0) {
    std::fprintf(stderr, "%d transport test(s) failed\n", Failures);
    return 1;
  }
  std::printf("NsmbNetplayTransport tests passed\n");
  return 0;
}
