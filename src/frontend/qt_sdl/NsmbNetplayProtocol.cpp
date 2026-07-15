#include "NsmbNetplayProtocol.h"

#include <algorithm>
#include <cstring>

namespace NsmbNetplayPoC::SessionProtocol {

namespace {

constexpr melonDS::u32 kMagic = 0x4C4D534E; // "NSML", little endian
constexpr melonDS::u32 kVersion = 1;
constexpr melonDS::u32 kMatchSeedKind = 0x44454553;  // "SEED", little endian
constexpr melonDS::u32 kStartReadyKind = 0x54525453; // "STRT", little endian

struct WireMessage {
  melonDS::u32 Magic;
  melonDS::u32 Version;
  melonDS::u32 Kind;
  melonDS::u32 Value;
};

static_assert(sizeof(WireMessage) == kSessionPacketSize);

melonDS::u32 ToWireKind(MessageKind kind) {
  switch (kind) {
  case MessageKind::MatchSeed:
    return kMatchSeedKind;
  case MessageKind::StartReady:
    return kStartReadyKind;
  }
  return 0;
}

bool FromWireKind(melonDS::u32 kind, MessageKind &result) {
  switch (kind) {
  case kMatchSeedKind:
    result = MessageKind::MatchSeed;
    return true;
  case kStartReadyKind:
    result = MessageKind::StartReady;
    return true;
  default:
    return false;
  }
}

} // namespace

std::vector<char> Encode(const Message &message) {
  const WireMessage wire{
      kMagic,
      kVersion,
      ToWireKind(message.Kind),
      message.Value,
  };
  std::vector<char> payload(sizeof(wire));
  std::memcpy(payload.data(), &wire, sizeof(wire));
  return payload;
}

bool Decode(const void *data, std::size_t size, Message &message) {
  if (!data || size != sizeof(WireMessage))
    return false;

  WireMessage wire;
  std::memcpy(&wire, data, sizeof(wire));
  MessageKind kind;
  if (wire.Magic != kMagic || wire.Version != kVersion ||
      !FromWireKind(wire.Kind, kind))
    return false;

  message = {kind, wire.Value};
  return true;
}

} // namespace NsmbNetplayPoC::SessionProtocol

namespace NsmbNetplayPoC::SessionPolicy {

melonDS::u32 FirstGameplayInputFrame(melonDS::u32 netplayStartFrame,
                                     int inputDelay) {
  return netplayStartFrame + static_cast<melonDS::u32>(std::max(0, inputDelay));
}

bool HasPostStartRemoteInput(bool hasReceivedInputFrame,
                             melonDS::u32 lastReceivedInputFrame,
                             melonDS::u32 netplayStartFrame, int inputDelay) {
  return hasReceivedInputFrame &&
         lastReceivedInputFrame >=
             FirstGameplayInputFrame(netplayStartFrame, inputDelay);
}

bool IsOldStartReady(bool inputNetplayOnly, melonDS::u32 netplayStartFrame,
                     melonDS::u32 receivedReadyFrame) {
  return inputNetplayOnly && netplayStartFrame != 0 &&
         receivedReadyFrame < netplayStartFrame;
}

bool ShouldAcceptStartReady(bool hasRemoteReadyFrame,
                            bool remoteReadyAfterLocal,
                            bool hasPostStartRemoteInput) {
  return hasRemoteReadyFrame &&
         (remoteReadyAfterLocal || hasPostStartRemoteInput);
}

bool ShouldResendStartReady(const StartReadyResendState &state) {
  if (!state.HasPeer || !state.InputNetplayOnly ||
      (!state.AllowBeforeAccepted && !state.WaitedForPeerAtStart) ||
      !state.StartReadySent || !state.HasLocalReadyFrame) {
    return false;
  }

  if (HasPostStartRemoteInput(state.HasReceivedInputFrame,
                              state.LastReceivedInputFrame,
                              state.NetplayStartFrame, state.InputDelay)) {
    return false;
  }

  return state.SendCount <= 0 ||
         state.ElapsedSinceLastSendMs >= kStartReadyResendIntervalMs;
}

void Runtime::ResetStartHandshake() {
  WaitedForPeerAtStart_ = false;
  StartReadySent_ = false;
  StartReadySendCount_ = 0;
  LastStartReadySentAt_ = {};
  LocalReadyFrame_.reset();
  RemoteReadyFrame_.reset();
  RemoteReadyAfterLocal_ = false;
  InputEpochPrimedStartFrame_.reset();
}

void Runtime::OnPeerConnected() {
  MatchSeedSent_ = false;
  StartReadySent_ = false;
  RemoteReadyFrame_.reset();
  RemoteReadyAfterLocal_ = false;
}

void Runtime::ResetReadyWaitAfterTimeout() {
  LocalReadyFrame_.reset();
  RemoteReadyAfterLocal_ = false;
  StartReadySent_ = false;
}

bool Runtime::MatchSeedSent() const { return MatchSeedSent_; }

void Runtime::MarkMatchSeedSent() { MatchSeedSent_ = true; }

bool Runtime::CanSendStartReady(bool force) const {
  return !StartReadySent_ || force;
}

void Runtime::MarkStartReadySent(Clock::time_point sentAt) {
  StartReadySent_ = true;
  StartReadySendCount_++;
  LastStartReadySentAt_ = sentAt;
}

void Runtime::BeginLocalReady(melonDS::u32 frame) {
  if (LocalReadyFrame_)
    return;
  if (frame == 0)
    LocalReadyFrame_.reset();
  else
    LocalReadyFrame_ = frame;
  RemoteReadyAfterLocal_ = false;
}

void Runtime::ReceiveRemoteReady(melonDS::u32 frame) {
  if (frame == 0)
    RemoteReadyFrame_.reset();
  else
    RemoteReadyFrame_ = frame;
  if (LocalReadyFrame_)
    RemoteReadyAfterLocal_ = true;
}

void Runtime::MarkWaitedForPeerAtStart() { WaitedForPeerAtStart_ = true; }

void Runtime::MarkInputEpochPrimed(melonDS::u32 startFrame) {
  if (startFrame == 0)
    InputEpochPrimedStartFrame_.reset();
  else
    InputEpochPrimedStartFrame_ = startFrame;
}

bool Runtime::WaitedForPeerAtStart() const { return WaitedForPeerAtStart_; }

bool Runtime::StartReadySent() const { return StartReadySent_; }

int Runtime::StartReadySendCount() const { return StartReadySendCount_; }

Runtime::Clock::time_point Runtime::LastStartReadySentAt() const {
  return LastStartReadySentAt_;
}

std::optional<melonDS::u32> Runtime::LocalReadyFrame() const {
  return LocalReadyFrame_;
}

std::optional<melonDS::u32> Runtime::RemoteReadyFrame() const {
  return RemoteReadyFrame_;
}

bool Runtime::RemoteReadyAfterLocal() const { return RemoteReadyAfterLocal_; }

bool Runtime::InputEpochPrimedFor(melonDS::u32 startFrame) const {
  return InputEpochPrimedStartFrame_ == startFrame;
}

} // namespace NsmbNetplayPoC::SessionPolicy

namespace NsmbNetplayPoC::PacketClassifier {

namespace {

bool IsGamePacketSize(std::size_t size, const KnownPacketSizes &known) {
  return size == known.NSMLPacket || size == known.PlayerState ||
         size == known.WorldState || size == known.MovingHazardState ||
         size == known.GameState;
}

} // namespace

PacketClass Classify(std::size_t packetSize, const KnownPacketSizes &sizes) {
  // Preserve the historical dispatch precedence. Input and session packets
  // win size collisions; larger unknown payloads are attempted as bundles.
  if (packetSize == sizes.Input)
    return PacketClass::Input;
  if (packetSize > sizes.Session && !IsGamePacketSize(packetSize, sizes))
    return PacketClass::InputBundleCandidate;
  if (packetSize == sizes.Session)
    return PacketClass::Session;
  if (packetSize == sizes.NSMLPacket)
    return PacketClass::NSMLPacket;
  if (packetSize == sizes.PlayerState)
    return PacketClass::PlayerState;
  if (packetSize == sizes.WorldState)
    return PacketClass::WorldState;
  if (packetSize == sizes.MovingHazardState)
    return PacketClass::MovingHazardState;
  if (packetSize == sizes.GameState)
    return PacketClass::GameState;
  return PacketClass::Unknown;
}

} // namespace NsmbNetplayPoC::PacketClassifier
