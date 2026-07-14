#include "NsmbGameState.h"

namespace NsmbNetplayPoC::GameStateModel {

namespace {

void MixGameStateValue(melonDS::u64 &hash, melonDS::u32 value) {
  for (int index = 0; index < 4; index++) {
    hash ^= (value >> (index * 8)) & 0xFF;
    hash *= 1099511628211ull;
  }
}

void MixGameStateValue(melonDS::u64 &hash, melonDS::u64 value) {
  MixGameStateValue(hash, static_cast<melonDS::u32>(value & 0xFFFFFFFFu));
  MixGameStateValue(hash, static_cast<melonDS::u32>(value >> 32));
}

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

melonDS::u64 ComputeBasicGameStateHash(const GameStateSample &sample) {
  melonDS::u64 hash = 1469598103934665603ull;
  MixGameStateValue(hash, sample.StageID);
  MixGameStateValue(hash, sample.StageGroup);
  MixGameStateValue(hash, sample.VsMode);
  MixGameStateValue(hash, sample.LocalPlayerID);
  MixGameStateValue(hash, sample.GGID);
  MixGameStateValue(hash, sample.NetState14);
  MixGameStateValue(hash, sample.NetState1C);
  MixGameStateValue(hash, sample.NetState20);
  MixGameStateValue(hash, sample.NetState24);
  MixGameStateValue(hash, sample.NetState5C);
  MixGameStateValue(hash, sample.NetPacketTick);
  MixGameStateValue(hash, sample.NetPacketKeys);
  MixGameStateValue(hash, sample.NetPacketAction);
  MixGameStateValue(hash, sample.NetPacketByte5);
  MixGameStateValue(hash, sample.NetPacketByte6);
  MixGameStateValue(hash, sample.NetPacketByte7);
  MixGameStateValue(hash, sample.NetRandomValue);
  MixGameStateValue(hash, sample.NetRandomCallCount);
  MixGameStateValue(hash, sample.NetRandomBranchAddress);
  MixGameStateValue(hash, sample.InputConsole0Held);
  MixGameStateValue(hash, sample.InputConsole0Pressed);
  MixGameStateValue(hash, sample.InputConsole1Held);
  MixGameStateValue(hash, sample.InputConsole1Pressed);
  MixGameStateValue(hash, sample.InputPlayer0Held);
  MixGameStateValue(hash, sample.InputPlayer1Held);
  MixGameStateValue(hash, sample.InputPlayer0Pressed);
  MixGameStateValue(hash, sample.InputPlayer1Pressed);
  MixGameStateValue(hash, sample.StageActorFreezeFlag);
  MixGameStateValue(hash, sample.VsStarFound);
  MixGameStateValue(hash, sample.VsStarGUID);
  MixGameStateValue(hash, sample.VsStarSettings);
  MixGameStateValue(hash, sample.VsStarStateType);
  MixGameStateValue(hash, sample.VsStarFlags);
  MixGameStateValue(hash, sample.VsStarPosX);
  MixGameStateValue(hash, sample.VsStarPosY);
  MixGameStateValue(hash, sample.VsStarPosZ);
  MixGameStateValue(hash, sample.VsStarActorFound);
  MixGameStateValue(hash, sample.VsStarActorGUID);
  MixGameStateValue(hash, sample.VsStarActorSettings);
  MixGameStateValue(hash, sample.VsStarActorStateType);
  MixGameStateValue(hash, sample.VsStarActorFlags);
  MixGameStateValue(hash, sample.VsStarActorPosX);
  MixGameStateValue(hash, sample.VsStarActorPosY);
  MixGameStateValue(hash, sample.VsStarActorPosZ);
  MixGameStateValue(hash, sample.PlayerActor0Found);
  MixGameStateValue(hash, sample.PlayerActor0GUID);
  MixGameStateValue(hash, sample.PlayerActor0Settings);
  MixGameStateValue(hash, sample.PlayerActor0PosX);
  MixGameStateValue(hash, sample.PlayerActor0PosY);
  MixGameStateValue(hash, sample.PlayerActor0PosZ);
  MixGameStateValue(hash, sample.PlayerActor0PrevX);
  MixGameStateValue(hash, sample.PlayerActor0PrevY);
  MixGameStateValue(hash, sample.PlayerActor0PrevZ);
  MixGameStateValue(hash, sample.PlayerActor0VelX);
  MixGameStateValue(hash, sample.PlayerActor0VelY);
  MixGameStateValue(hash, sample.PlayerActor0VelZ);
  MixGameStateValue(hash, sample.PlayerActor1Found);
  MixGameStateValue(hash, sample.PlayerActor1GUID);
  MixGameStateValue(hash, sample.PlayerActor1Settings);
  MixGameStateValue(hash, sample.PlayerActor1PosX);
  MixGameStateValue(hash, sample.PlayerActor1PosY);
  MixGameStateValue(hash, sample.PlayerActor1PosZ);
  MixGameStateValue(hash, sample.PlayerActor1PrevX);
  MixGameStateValue(hash, sample.PlayerActor1PrevY);
  MixGameStateValue(hash, sample.PlayerActor1PrevZ);
  MixGameStateValue(hash, sample.PlayerActor1VelX);
  MixGameStateValue(hash, sample.PlayerActor1VelY);
  MixGameStateValue(hash, sample.PlayerActor1VelZ);
  MixGameStateValue(hash, sample.PlayerCount);
  MixGameStateValue(hash, sample.Player0Powerup);
  MixGameStateValue(hash, sample.Player1Powerup);
  MixGameStateValue(hash, sample.Player0InventoryPowerup);
  MixGameStateValue(hash, sample.Player1InventoryPowerup);
  MixGameStateValue(hash, sample.Player0Dead);
  MixGameStateValue(hash, sample.Player1Dead);
  MixGameStateValue(hash, sample.Player0Character);
  MixGameStateValue(hash, sample.Player1Character);
  MixGameStateValue(hash, sample.Player0Lives);
  MixGameStateValue(hash, sample.Player1Lives);
  MixGameStateValue(hash, sample.Player0BattleStars);
  MixGameStateValue(hash, sample.Player1BattleStars);
  MixGameStateValue(hash, sample.Player0Coins);
  MixGameStateValue(hash, sample.Player1Coins);
  MixGameStateValue(hash, sample.Player0Score);
  MixGameStateValue(hash, sample.Player1Score);
  MixGameStateValue(hash, sample.Player0DisplayedStars);
  MixGameStateValue(hash, sample.Player1DisplayedStars);
  MixGameStateValue(hash, sample.Player0Deaths);
  MixGameStateValue(hash, sample.Player1Deaths);
  MixGameStateValue(hash, sample.Player0CollectedStars);
  MixGameStateValue(hash, sample.Player1CollectedStars);
  MixGameStateValue(hash, sample.VsCoinCount);
  MixGameStateValue(hash, sample.StageCameraFound);
  MixGameStateValue(hash, sample.StageCameraWord190);
  MixGameStateValue(hash, sample.StageCameraWord194);
  MixGameStateValue(hash, sample.StageCameraWord19C);
  MixGameStateValue(hash, sample.StageCameraWord1A0);
  MixGameStateValue(hash, sample.StageSceneFound);
  MixGameStateValue(hash, sample.StageSceneWord154);
  MixGameStateValue(hash, sample.StageSceneWord160);
  MixGameStateValue(hash, sample.VsConnectFound);
  MixGameStateValue(hash, sample.VsConnectWord078);
  MixGameStateValue(hash, sample.VsConnectWord07C);
  MixGameStateValue(hash, sample.VsConnectWord114);
  MixGameStateValue(hash, sample.VsConnectWord118);
  MixGameStateValue(hash, sample.VsConnectWord120);
  MixGameStateValue(hash, sample.VsConnectWord128);
  MixGameStateValue(hash, sample.VsConnectWord144);
  MixGameStateValue(hash, sample.VsConnectWord148);
  MixGameStateValue(hash, sample.VsConnectWord154);
  MixGameStateValue(hash, sample.CourseSelectFound);
  MixGameStateValue(hash, sample.CourseSelectSettings);
  MixGameStateValue(hash, sample.CourseSelectWord060);
  MixGameStateValue(hash, sample.CourseSelectWord064);
  MixGameStateValue(hash, sample.CourseSelectWord068);
  MixGameStateValue(hash, sample.CourseSelectWord06C);
  MixGameStateValue(hash, sample.CourseSelectWord070);
  MixGameStateValue(hash, sample.CourseSelectWord074);
  MixGameStateValue(hash, sample.CourseSelectWord078);
  MixGameStateValue(hash, sample.CourseSelectWord07C);
  MixGameStateValue(hash, sample.CourseSelectWord080);
  MixGameStateValue(hash, sample.CourseSelectWord084);
  MixGameStateValue(hash, sample.CourseSelectWord088);
  MixGameStateValue(hash, sample.CourseSelectWord08C);
  MixGameStateValue(hash, sample.CourseSelectWord090);
  MixGameStateValue(hash, sample.StageActorManagerFound);
  MixGameStateValue(hash, sample.StageActorManagerStateType);
  MixGameStateValue(hash, sample.StageControllerFound);
  MixGameStateValue(hash, sample.StageControllerStateType);
  MixGameStateValue(hash, sample.MvlObject267Found);
  MixGameStateValue(hash, sample.MvlObject267StateType);
  MixGameStateValue(hash, sample.MovingHazardFound);
  MixGameStateValue(hash, sample.MovingHazardGUID);
  MixGameStateValue(hash, sample.MovingHazardSettings);
  MixGameStateValue(hash, sample.MovingHazardStateType);
  MixGameStateValue(hash, sample.MovingHazardFlags);
  MixGameStateValue(hash, sample.MovingHazardPosX);
  MixGameStateValue(hash, sample.MovingHazardPosY);
  MixGameStateValue(hash, sample.MovingHazardPosZ);
  MixGameStateValue(hash, sample.MovingHazardVelX);
  MixGameStateValue(hash, sample.MovingHazardVelY);
  return hash;
}

melonDS::u64 CombinedGameStateHash(const GameStateSyncHashes &hashes) {
  melonDS::u64 combined = hashes.Basic;
  MixGameStateValue(combined, hashes.PlayerGlobal);
  MixGameStateValue(combined, hashes.WifiCandidate);
  MixGameStateValue(combined, hashes.RenderCandidate);
  return combined;
}

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
