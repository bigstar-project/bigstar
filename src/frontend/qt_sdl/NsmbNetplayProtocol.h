#ifndef NSMBNETPLAYPROTOCOL_H
#define NSMBNETPLAYPROTOCOL_H

#include "types.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace NsmbNetplayPoC::WireProtocol {

constexpr melonDS::u32 kMagic = 0x4C4D534E; // "NSML", little endian
constexpr melonDS::u32 kVersion = 1;
constexpr melonDS::u32 kWireKindState = 0x54415453;  // "STAT", little endian
constexpr melonDS::u32 kWireKindPacket = 0x4B434150; // "PACK", little endian
constexpr melonDS::u32 kWireKindWorldState =
    0x41545357; // "WSTA", little endian
constexpr melonDS::u32 kWireKindMovingHazardState =
    0x415A4148; // "HAZA", little endian
constexpr std::size_t kMaxWorldMovingHazards = 4;

struct WireNSMLPacket {
  melonDS::u32 Magic;
  melonDS::u32 Version;
  melonDS::u32 Kind;
  melonDS::u32 Frame;
  melonDS::u32 Player;
  melonDS::u32 Tick;
  melonDS::u8 Packet[52];
};

static_assert(sizeof(WireNSMLPacket) == 76);

struct WireGameState {
  melonDS::u32 Magic;
  melonDS::u32 Version;
  melonDS::u32 Kind;
  melonDS::u32 Frame;
  melonDS::u32 Instance;
  melonDS::u32 StageID;
  melonDS::u32 StageGroup;
  melonDS::u32 VsMode;
  melonDS::u32 LocalPlayerID;
  melonDS::u32 GGID;
  melonDS::u32 NetRandomValue;
  melonDS::u32 NetRandomCallCount;
  melonDS::u32 NetRandomBranchAddress;
  melonDS::u32 VsStarFound;
  melonDS::u32 VsStarGUID;
  melonDS::u32 VsStarBase;
  melonDS::u32 VsStarSettings;
  melonDS::u32 VsStarStateType;
  melonDS::u32 VsStarFlags;
  melonDS::u32 VsStarPosX;
  melonDS::u32 VsStarPosY;
  melonDS::u32 VsStarPosZ;
  melonDS::u32 VsStarActorFound;
  melonDS::u32 VsStarActorGUID;
  melonDS::u32 VsStarActorBase;
  melonDS::u32 VsStarActorSettings;
  melonDS::u32 VsStarActorStateType;
  melonDS::u32 VsStarActorFlags;
  melonDS::u32 VsStarActorPosX;
  melonDS::u32 VsStarActorPosY;
  melonDS::u32 VsStarActorPosZ;
  melonDS::u32 PlayerActor0Found;
  melonDS::u32 PlayerActor0GUID;
  melonDS::u32 PlayerActor0Settings;
  melonDS::u32 PlayerActor0PosX;
  melonDS::u32 PlayerActor0PosY;
  melonDS::u32 PlayerActor0PosZ;
  melonDS::u32 PlayerActor0PrevX;
  melonDS::u32 PlayerActor0PrevY;
  melonDS::u32 PlayerActor0PrevZ;
  melonDS::u32 PlayerActor0VelX;
  melonDS::u32 PlayerActor0VelY;
  melonDS::u32 PlayerActor0VelZ;
  melonDS::u32 PlayerActor1Found;
  melonDS::u32 PlayerActor1GUID;
  melonDS::u32 PlayerActor1Settings;
  melonDS::u32 PlayerActor1PosX;
  melonDS::u32 PlayerActor1PosY;
  melonDS::u32 PlayerActor1PosZ;
  melonDS::u32 PlayerActor1PrevX;
  melonDS::u32 PlayerActor1PrevY;
  melonDS::u32 PlayerActor1PrevZ;
  melonDS::u32 PlayerActor1VelX;
  melonDS::u32 PlayerActor1VelY;
  melonDS::u32 PlayerActor1VelZ;
  melonDS::u32 PlayerCount;
  melonDS::u32 Player0BattleStars;
  melonDS::u32 Player1BattleStars;
  melonDS::u32 Player0Coins;
  melonDS::u32 Player1Coins;
  melonDS::u32 Player0Score;
  melonDS::u32 Player1Score;
  melonDS::u32 Player0DisplayedStars;
  melonDS::u32 Player1DisplayedStars;
  melonDS::u32 Player0Deaths;
  melonDS::u32 Player1Deaths;
  melonDS::u32 Player0CollectedStars;
  melonDS::u32 Player1CollectedStars;
  melonDS::u32 VsCoinCount;
  melonDS::u32 StageCameraFound;
  melonDS::u32 StageCameraWord190;
  melonDS::u32 StageCameraWord194;
  melonDS::u32 StageCameraWord19C;
  melonDS::u32 StageCameraWord1A0;
  melonDS::u32 StageSceneFound;
  melonDS::u32 StageSceneWord154;
  melonDS::u32 StageSceneWord160;
  melonDS::u32 MovingHazardFound;
  melonDS::u32 MovingHazardGUID;
  melonDS::u32 MovingHazardSettings;
  melonDS::u32 MovingHazardStateType;
  melonDS::u32 MovingHazardFlags;
  melonDS::u32 MovingHazardPosX;
  melonDS::u32 MovingHazardPosY;
  melonDS::u32 MovingHazardPosZ;
  melonDS::u32 MovingHazardVelX;
  melonDS::u32 MovingHazardVelY;
  melonDS::u32 BasicHashLo;
  melonDS::u32 BasicHashHi;
  melonDS::u32 PlayerGlobalHashLo;
  melonDS::u32 PlayerGlobalHashHi;
  melonDS::u32 WifiCandidateHashLo;
  melonDS::u32 WifiCandidateHashHi;
  melonDS::u32 RenderCandidateHashLo;
  melonDS::u32 RenderCandidateHashHi;
};

static_assert(sizeof(WireGameState) == 380);

struct WireWorldActorState {
  melonDS::u32 Found;
  melonDS::u32 GUID;
  melonDS::u32 Settings;
  melonDS::u32 StateType;
  melonDS::u32 Flags;
  melonDS::u32 PosX;
  melonDS::u32 PosY;
  melonDS::u32 PosZ;
  melonDS::u32 PrevX;
  melonDS::u32 PrevY;
  melonDS::u32 PrevZ;
  melonDS::u32 VelX;
  melonDS::u32 VelY;
  melonDS::u32 VelZ;
  melonDS::u32 LastStepX;
  melonDS::u32 LastStepY;
  melonDS::u32 LastStepZ;
  melonDS::u32 VelH;
  melonDS::u32 TargetVelH;
  melonDS::u32 AccelV;
  melonDS::u32 TargetVelV;
  melonDS::u32 AccelH;
  melonDS::u32 TargetVelX;
  melonDS::u32 TargetVelY;
  melonDS::u32 TargetVelZ;
};

struct WireWorldState {
  melonDS::u32 Magic;
  melonDS::u32 Version;
  melonDS::u32 Kind;
  melonDS::u32 Frame;
  melonDS::u32 Instance;
  WireWorldActorState Star;
};

static_assert(sizeof(WireWorldActorState) == 100);
static_assert(sizeof(WireWorldState) == 120);

struct WireMovingHazardState {
  melonDS::u32 Magic;
  melonDS::u32 Version;
  melonDS::u32 Kind;
  melonDS::u32 Frame;
  melonDS::u32 Instance;
  melonDS::u32 Count;
  WireWorldActorState Actors[kMaxWorldMovingHazards];
};

static_assert(sizeof(WireMovingHazardState) == 424);

} // namespace NsmbNetplayPoC::WireProtocol

namespace NsmbNetplayPoC::SessionProtocol {

constexpr std::size_t kSessionPacketSize = 16;

enum class MessageKind {
  MatchSeed,
  StartReady,
};

struct Message {
  MessageKind Kind = MessageKind::MatchSeed;
  melonDS::u32 Value = 0;
};

std::vector<char> Encode(const Message &message);
bool Decode(const void *data, std::size_t size, Message &message);

} // namespace NsmbNetplayPoC::SessionProtocol

namespace NsmbNetplayPoC::SessionPolicy {

constexpr std::int64_t kStartReadyResendIntervalMs = 250;

struct StartReadyResendState {
  bool HasPeer = false;
  bool InputNetplayOnly = false;
  bool AllowBeforeAccepted = false;
  bool WaitedForPeerAtStart = false;
  bool StartReadySent = false;
  bool HasLocalReadyFrame = false;
  bool HasReceivedInputFrame = false;
  melonDS::u32 LastReceivedInputFrame = 0;
  melonDS::u32 NetplayStartFrame = 0;
  int InputDelay = 0;
  int SendCount = 0;
  std::int64_t ElapsedSinceLastSendMs = 0;
};

melonDS::u32 FirstGameplayInputFrame(melonDS::u32 netplayStartFrame,
                                     int inputDelay);
bool HasPostStartRemoteInput(bool hasReceivedInputFrame,
                             melonDS::u32 lastReceivedInputFrame,
                             melonDS::u32 netplayStartFrame, int inputDelay);
bool IsOldStartReady(bool inputNetplayOnly, melonDS::u32 netplayStartFrame,
                     melonDS::u32 receivedReadyFrame);
bool ShouldAcceptStartReady(bool hasRemoteReadyFrame,
                            bool remoteReadyAfterLocal,
                            bool hasPostStartRemoteInput);
bool ShouldResendStartReady(const StartReadyResendState &state);

class Runtime {
public:
  using Clock = std::chrono::steady_clock;

  void ResetStartHandshake();
  void OnPeerConnected();
  void ResetReadyWaitAfterTimeout();

  bool MatchSeedSent() const;
  void MarkMatchSeedSent();
  bool CanSendStartReady(bool force) const;
  void MarkStartReadySent(Clock::time_point sentAt);

  void BeginLocalReady(melonDS::u32 frame);
  void ReceiveRemoteReady(melonDS::u32 frame);
  void MarkWaitedForPeerAtStart();
  void MarkInputEpochPrimed(melonDS::u32 startFrame);

  bool WaitedForPeerAtStart() const;
  bool StartReadySent() const;
  int StartReadySendCount() const;
  Clock::time_point LastStartReadySentAt() const;
  std::optional<melonDS::u32> LocalReadyFrame() const;
  std::optional<melonDS::u32> RemoteReadyFrame() const;
  bool RemoteReadyAfterLocal() const;
  bool InputEpochPrimedFor(melonDS::u32 startFrame) const;

private:
  bool MatchSeedSent_ = false;
  bool WaitedForPeerAtStart_ = false;
  bool StartReadySent_ = false;
  int StartReadySendCount_ = 0;
  Clock::time_point LastStartReadySentAt_;
  std::optional<melonDS::u32> LocalReadyFrame_;
  std::optional<melonDS::u32> RemoteReadyFrame_;
  bool RemoteReadyAfterLocal_ = false;
  std::optional<melonDS::u32> InputEpochPrimedStartFrame_;
};

} // namespace NsmbNetplayPoC::SessionPolicy

namespace NsmbNetplayPoC::PacketClassifier {

enum class PacketClass {
  Unknown,
  Input,
  InputBundleCandidate,
  Session,
  NSMLPacket,
  WorldState,
  MovingHazardState,
  GameState,
};

struct KnownPacketSizes {
  std::size_t Input = 0;
  std::size_t Session = 0;
  std::size_t NSMLPacket = 0;
  std::size_t WorldState = 0;
  std::size_t MovingHazardState = 0;
  std::size_t GameState = 0;
};

PacketClass Classify(std::size_t packetSize, const KnownPacketSizes &sizes);

} // namespace NsmbNetplayPoC::PacketClassifier

#endif
