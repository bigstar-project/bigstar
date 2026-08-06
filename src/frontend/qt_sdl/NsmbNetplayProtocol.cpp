#include "NsmbNetplayProtocol.h"

#include <algorithm>
#include <cstring>

namespace NsmbMvlNetplay::SessionProtocol {

namespace {

constexpr melonDS::u32 kMagic = 0x4C4D534E; // "NSML", little endian
constexpr melonDS::u32 kVersion = 2;
constexpr melonDS::u32 kMatchSeedKind = 0x44454553;  // "SEED", little endian
constexpr melonDS::u32 kStartReadyKind = 0x54525453; // "STRT", little endian

struct WireMessage {
  melonDS::u32 Magic;
  melonDS::u32 Version;
  melonDS::u32 Kind;
  melonDS::u32 Value;
  melonDS::u32 Generation;
  melonDS::u32 RawReadyFrame;
  melonDS::u32 SharedLogicalEpoch;
  melonDS::u32 StageID;
  melonDS::u32 StageGroup;
  melonDS::u32 MatchSeed;
  melonDS::u32 PacketTick;
  melonDS::u32 RngValue;
  melonDS::u32 RngCallCount;
  melonDS::u32 RngBranchAddress;
  melonDS::u32 SemanticHashLo;
  melonDS::u32 SemanticHashHi;
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
      message.Generation,
      message.RawReadyFrame,
      message.SharedLogicalEpoch,
      message.StageID,
      message.StageGroup,
      message.MatchSeed,
      message.PacketTick,
      message.RngValue,
      message.RngCallCount,
      message.RngBranchAddress,
      static_cast<melonDS::u32>(message.SemanticHash & 0xFFFFFFFFu),
      static_cast<melonDS::u32>(message.SemanticHash >> 32),
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

  message = {kind,
             wire.Value,
             wire.Generation,
             wire.RawReadyFrame,
             wire.SharedLogicalEpoch,
             wire.StageID,
             wire.StageGroup,
             wire.MatchSeed,
             wire.PacketTick,
             wire.RngValue,
             wire.RngCallCount,
             wire.RngBranchAddress,
             (static_cast<melonDS::u64>(wire.SemanticHashHi) << 32) |
                 wire.SemanticHashLo};
  return true;
}

} // namespace NsmbMvlNetplay::SessionProtocol

namespace NsmbMvlNetplay::SessionPolicy {

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

StartReadyValidation ValidateStartReady(const SessionProtocol::Message &local,
                                        const SessionProtocol::Message &remote,
                                        melonDS::u32 expectedGeneration,
                                        melonDS::u32 sharedLogicalEpoch) {
  if (local.Generation != expectedGeneration ||
      remote.Generation != expectedGeneration)
    return StartReadyValidation::GenerationMismatch;
  if (sharedLogicalEpoch == 0 || local.SharedLogicalEpoch == 0 ||
      remote.SharedLogicalEpoch == 0)
    return StartReadyValidation::EpochMissing;
  if (local.SharedLogicalEpoch != sharedLogicalEpoch ||
      remote.SharedLogicalEpoch != sharedLogicalEpoch)
    return StartReadyValidation::EpochMismatch;
  if (local.StageID != remote.StageID ||
      local.StageGroup != remote.StageGroup)
    return StartReadyValidation::StageMismatch;
  if (local.MatchSeed != remote.MatchSeed)
    return StartReadyValidation::SeedMismatch;
  if (local.PacketTick != remote.PacketTick)
    return StartReadyValidation::PacketTickMismatch;
  if (local.RngValue != remote.RngValue ||
      local.RngCallCount != remote.RngCallCount ||
      local.RngBranchAddress != remote.RngBranchAddress)
    return StartReadyValidation::RngMismatch;
  if (local.SemanticHash != remote.SemanticHash)
    return StartReadyValidation::SemanticStateMismatch;
  return StartReadyValidation::Match;
}

const char *StartReadyValidationName(StartReadyValidation result) {
  switch (result) {
  case StartReadyValidation::Match:
    return "match";
  case StartReadyValidation::GenerationMismatch:
    return "generation-mismatch";
  case StartReadyValidation::EpochMissing:
    return "epoch-missing";
  case StartReadyValidation::EpochMismatch:
    return "epoch-mismatch";
  case StartReadyValidation::StageMismatch:
    return "stage-mismatch";
  case StartReadyValidation::SeedMismatch:
    return "seed-mismatch";
  case StartReadyValidation::PacketTickMismatch:
    return "packet-tick-mismatch";
  case StartReadyValidation::RngMismatch:
    return "rng-mismatch";
  case StartReadyValidation::SemanticStateMismatch:
    return "semantic-state-mismatch";
  }
  return "unknown";
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

bool ShouldPumpNetworkAtFrame(bool deferUntilStart,
                              melonDS::u32 netplayStartFrame,
                              melonDS::u32 syncFrame,
                              melonDS::u32 sendStartFrame) {
  return !deferUntilStart || netplayStartFrame == 0 ||
         syncFrame >= sendStartFrame;
}

melonDS::u32 LogicalInputFrame(bool inputNetplayOnly,
                               std::optional<melonDS::u32> localReadyFrame,
                               melonDS::u32 netplayStartFrame,
                               melonDS::u32 rawFrame) {
  if (!inputNetplayOnly || !localReadyFrame || rawFrame < *localReadyFrame)
    return rawFrame;
  return netplayStartFrame + (rawFrame - *localReadyFrame);
}

void Runtime::ResetStartHandshake() {
  WaitedForPeerAtStart_ = false;
  StartReadySent_ = false;
  StartReadySendCount_ = 0;
  LastStartReadySentAt_ = {};
  LocalReadyFrame_.reset();
  RemoteReadyFrame_.reset();
  LocalReady_.reset();
  RemoteReady_.reset();
  RemoteReadyAfterLocal_ = false;
  InputEpochPrimedStartFrame_.reset();
  Validation_ = StartReadyValidation::GenerationMismatch;
}

void Runtime::BeginGeneration(melonDS::u32 generation,
                              melonDS::u32 sharedLogicalEpoch, bool host) {
  ResetStartHandshake();
  Generation_ = generation;
  SharedLogicalEpoch_ = host ? sharedLogicalEpoch : 0;
  LogicalEpochEstablished_ = host && sharedLogicalEpoch != 0;
}

void Runtime::OnPeerConnected() {
  MatchSeedSent_ = false;
  StartReadySent_ = false;
  RemoteReadyFrame_.reset();
  RemoteReady_.reset();
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

void Runtime::BeginLocalReady(const SessionProtocol::Message &message) {
  if (LocalReadyFrame_)
    return;
  if (message.RawReadyFrame == 0) {
    LocalReadyFrame_.reset();
    LocalReady_.reset();
  } else {
    LocalReadyFrame_ = message.RawReadyFrame;
    LocalReady_ = message;
  }
  RemoteReadyAfterLocal_ = false;
}

void Runtime::ReceiveRemoteReady(const SessionProtocol::Message &message) {
  if (message.RawReadyFrame == 0) {
    RemoteReadyFrame_.reset();
    RemoteReady_.reset();
  } else {
    RemoteReadyFrame_ = message.RawReadyFrame;
    RemoteReady_ = message;
  }
  if (LocalReadyFrame_)
    RemoteReadyAfterLocal_ = true;
}

void Runtime::AdoptHostLogicalEpoch(melonDS::u32 epoch) {
  SharedLogicalEpoch_ = epoch;
  LogicalEpochEstablished_ = epoch != 0;
}

void Runtime::MarkStartReadyValidation(StartReadyValidation validation) {
  Validation_ = validation;
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

std::optional<SessionProtocol::Message> Runtime::LocalReady() const {
  return LocalReady_;
}

std::optional<SessionProtocol::Message> Runtime::RemoteReady() const {
  return RemoteReady_;
}

melonDS::u32 Runtime::Generation() const { return Generation_; }

melonDS::u32 Runtime::SharedLogicalEpoch() const {
  return SharedLogicalEpoch_;
}

bool Runtime::LogicalEpochEstablished() const {
  return LogicalEpochEstablished_;
}

StartReadyValidation Runtime::Validation() const { return Validation_; }

bool Runtime::StartReadyValidated() const {
  return Validation_ == StartReadyValidation::Match;
}

bool Runtime::RemoteReadyAfterLocal() const { return RemoteReadyAfterLocal_; }

bool Runtime::InputEpochPrimedFor(melonDS::u32 startFrame) const {
  return InputEpochPrimedStartFrame_ == startFrame;
}

} // namespace NsmbMvlNetplay::SessionPolicy

namespace NsmbMvlNetplay::PacketClassifier {

namespace {

bool IsGamePacketSize(std::size_t size, const KnownPacketSizes &known) {
  return size == known.NSMLPacket || size == known.GameState;
}

} // namespace

PacketClass Classify(std::size_t packetSize, const KnownPacketSizes &sizes) {
  // Preserve the historical dispatch precedence. Input and session packets
  // win size collisions; larger unknown payloads are attempted as bundles.
  if (packetSize == sizes.Input)
    return PacketClass::Input;
  if (packetSize == sizes.Session)
    return PacketClass::Session;
  if (packetSize == sizes.NSMLPacket)
    return PacketClass::NSMLPacket;
  if (packetSize == sizes.GameState)
    return PacketClass::GameState;
  if (packetSize > sizes.Input && !IsGamePacketSize(packetSize, sizes))
    return PacketClass::InputBundleCandidate;
  return PacketClass::Unknown;
}

} // namespace NsmbMvlNetplay::PacketClassifier
