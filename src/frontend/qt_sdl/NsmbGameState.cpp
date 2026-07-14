#include "NsmbGameState.h"

namespace NsmbNetplayPoC::GameStateModel {

namespace {

#define NSMB_GAME_STATE_WIRE_FIELDS(X)                                         \
  X(StageID)                                                                   \
  X(StageGroup)                                                                \
  X(VsMode)                                                                    \
  X(LocalPlayerID)                                                             \
  X(GGID)                                                                      \
  X(NetRandomValue)                                                            \
  X(NetRandomCallCount)                                                        \
  X(NetRandomBranchAddress)                                                    \
  X(VsStarFound)                                                               \
  X(VsStarGUID)                                                                \
  X(VsStarBase)                                                                \
  X(VsStarSettings)                                                            \
  X(VsStarStateType)                                                           \
  X(VsStarFlags)                                                               \
  X(VsStarPosX)                                                                \
  X(VsStarPosY)                                                                \
  X(VsStarPosZ)                                                                \
  X(VsStarActorFound)                                                          \
  X(VsStarActorGUID)                                                           \
  X(VsStarActorBase)                                                           \
  X(VsStarActorSettings)                                                       \
  X(VsStarActorStateType)                                                      \
  X(VsStarActorFlags)                                                          \
  X(VsStarActorPosX)                                                           \
  X(VsStarActorPosY)                                                           \
  X(VsStarActorPosZ)                                                           \
  X(PlayerActor0Found)                                                         \
  X(PlayerActor0GUID)                                                          \
  X(PlayerActor0Settings)                                                      \
  X(PlayerActor0PosX)                                                          \
  X(PlayerActor0PosY)                                                          \
  X(PlayerActor0PosZ)                                                          \
  X(PlayerActor0PrevX)                                                         \
  X(PlayerActor0PrevY)                                                         \
  X(PlayerActor0PrevZ)                                                         \
  X(PlayerActor0VelX)                                                          \
  X(PlayerActor0VelY)                                                          \
  X(PlayerActor0VelZ)                                                          \
  X(PlayerActor1Found)                                                         \
  X(PlayerActor1GUID)                                                          \
  X(PlayerActor1Settings)                                                      \
  X(PlayerActor1PosX)                                                          \
  X(PlayerActor1PosY)                                                          \
  X(PlayerActor1PosZ)                                                          \
  X(PlayerActor1PrevX)                                                         \
  X(PlayerActor1PrevY)                                                         \
  X(PlayerActor1PrevZ)                                                         \
  X(PlayerActor1VelX)                                                          \
  X(PlayerActor1VelY)                                                          \
  X(PlayerActor1VelZ)                                                          \
  X(PlayerCount)                                                               \
  X(Player0BattleStars)                                                        \
  X(Player1BattleStars)                                                        \
  X(Player0Coins)                                                              \
  X(Player1Coins)                                                              \
  X(Player0Score)                                                              \
  X(Player1Score)                                                              \
  X(Player0DisplayedStars)                                                     \
  X(Player1DisplayedStars)                                                     \
  X(Player0Deaths)                                                             \
  X(Player1Deaths)                                                             \
  X(Player0CollectedStars)                                                     \
  X(Player1CollectedStars)                                                     \
  X(VsCoinCount)                                                               \
  X(StageCameraFound)                                                          \
  X(StageCameraWord190)                                                        \
  X(StageCameraWord194)                                                        \
  X(StageCameraWord19C)                                                        \
  X(StageCameraWord1A0)                                                        \
  X(StageSceneFound)                                                           \
  X(StageSceneWord154)                                                         \
  X(StageSceneWord160)                                                         \
  X(MovingHazardFound)                                                         \
  X(MovingHazardGUID)                                                          \
  X(MovingHazardSettings)                                                      \
  X(MovingHazardStateType)                                                     \
  X(MovingHazardFlags)                                                         \
  X(MovingHazardPosX)                                                          \
  X(MovingHazardPosY)                                                          \
  X(MovingHazardPosZ)                                                          \
  X(MovingHazardVelX)                                                          \
  X(MovingHazardVelY)

} // namespace

WireProtocol::WireGameState
EncodeWireGameState(melonDS::u32 frame, melonDS::u32 instance,
                    const GameStateSample &sample,
                    const GameStateSyncHashes &hashes) {
  WireProtocol::WireGameState packet{};
  packet.Magic = WireProtocol::kMagic;
  packet.Version = WireProtocol::kVersion;
  packet.Kind = WireProtocol::kWireKindState;
  packet.Frame = frame;
  packet.Instance = instance;
#define COPY_TO_WIRE(name) packet.name = sample.name;
  NSMB_GAME_STATE_WIRE_FIELDS(COPY_TO_WIRE)
#undef COPY_TO_WIRE
  packet.BasicHashLo = static_cast<melonDS::u32>(hashes.Basic & 0xFFFFFFFFu);
  packet.BasicHashHi = static_cast<melonDS::u32>(hashes.Basic >> 32);
  packet.PlayerGlobalHashLo =
      static_cast<melonDS::u32>(hashes.PlayerGlobal & 0xFFFFFFFFu);
  packet.PlayerGlobalHashHi =
      static_cast<melonDS::u32>(hashes.PlayerGlobal >> 32);
  packet.WifiCandidateHashLo =
      static_cast<melonDS::u32>(hashes.WifiCandidate & 0xFFFFFFFFu);
  packet.WifiCandidateHashHi =
      static_cast<melonDS::u32>(hashes.WifiCandidate >> 32);
  packet.RenderCandidateHashLo =
      static_cast<melonDS::u32>(hashes.RenderCandidate & 0xFFFFFFFFu);
  packet.RenderCandidateHashHi =
      static_cast<melonDS::u32>(hashes.RenderCandidate >> 32);
  return packet;
}

bool DecodeWireGameState(const WireProtocol::WireGameState &packet,
                         DecodedGameState &decoded) {
  if (packet.Magic != WireProtocol::kMagic ||
      packet.Version != WireProtocol::kVersion ||
      packet.Kind != WireProtocol::kWireKindState)
    return false;

  DecodedGameState result;
  result.Frame = packet.Frame;
  result.Instance = packet.Instance;
#define COPY_FROM_WIRE(name) result.Sample.name = packet.name;
  NSMB_GAME_STATE_WIRE_FIELDS(COPY_FROM_WIRE)
#undef COPY_FROM_WIRE
  result.Hashes.Basic = (static_cast<melonDS::u64>(packet.BasicHashHi) << 32) |
                        packet.BasicHashLo;
  result.Hashes.PlayerGlobal =
      (static_cast<melonDS::u64>(packet.PlayerGlobalHashHi) << 32) |
      packet.PlayerGlobalHashLo;
  result.Hashes.WifiCandidate =
      (static_cast<melonDS::u64>(packet.WifiCandidateHashHi) << 32) |
      packet.WifiCandidateHashLo;
  result.Hashes.RenderCandidate =
      (static_cast<melonDS::u64>(packet.RenderCandidateHashHi) << 32) |
      packet.RenderCandidateHashLo;
  result.Sample.Hash = result.Hashes.Basic;
  decoded = result;
  return true;
}

#undef NSMB_GAME_STATE_WIRE_FIELDS

} // namespace NsmbNetplayPoC::GameStateModel
