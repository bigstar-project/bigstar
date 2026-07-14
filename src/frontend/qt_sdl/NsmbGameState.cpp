#include "NsmbGameState.h"

#include <iterator>
#include <ostream>

namespace NsmbNetplayPoC::GameStateModel {

melonDS::u64 GameStateKey(int instanceID, melonDS::u32 frame) {
  return (static_cast<melonDS::u64>(static_cast<melonDS::u32>(instanceID))
          << 32) |
         frame;
}

melonDS::u64 PlayerStateKey(melonDS::u32 player, melonDS::u32 frame) {
  return (static_cast<melonDS::u64>(player) << 32) | frame;
}

namespace {

template <typename State>
bool StoreLatest(std::optional<State> &stored, const State &received) {
  if (stored && received.Frame < stored->Frame)
    return false;
  stored = received;
  return true;
}

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

void WriteGameStateTraceHeader(std::ostream &out, bool extended) {
  out << "instance,frame,stageID,stageGroup,vsMode,localPlayerID,arm9PC,arm9LR,"
         "arm9SP,arm9CPSR,appFrameLength,appUpdateTask,appSleepPhase,"
         "appSleepControl,appSleeping,appSleepPhaseTimer,appSleepWakeUpTimer,"
         "appBootParam,appBootTarget,appBootScene,ggid,netCurrentLanguage,"
         "netLocalAid,netState14,netState1C,netState20,netState24,"
         "netExpectedConsoleCount,netMultiBootSession,netSessionState,"
         "netModuleState,netMaxSessionChildren,netMaxConsoleCount,netState5C,"
         "netPacketTick,netPacketKeys,netPacketAction,netPacketByte5,"
         "netPacketByte6,netPacketByte7,netRandomValue,netRandomCallCount,"
         "netRandomBranchAddress,inputConsole0Held,inputConsole0Pressed,"
         "inputConsole1Held,inputConsole1Pressed,inputPlayer0Held,"
         "inputPlayer1Held,inputPlayer0Pressed,inputPlayer1Pressed,"
         "stageActorFreezeFlag,sceneIsSceneActive,scenePreviousSceneID,"
         "sceneNextSceneID,sceneCurrentSceneID,sceneNextSceneSettings,"
         "vsStarFound,vsStarGuid,vsStarBase,vsStarSettings,vsStarStateType,"
         "vsStarFlags,vsStarX,vsStarY,vsStarZ,vsStarActorFound,vsStarActorGuid,"
         "vsStarActorBase,vsStarActorSettings,vsStarActorStateType,"
         "vsStarActorFlags,vsStarActorX,vsStarActorY,vsStarActorZ,"
         "playerActor0Found,playerActor0Guid,playerActor0Base,"
         "playerActor0Settings,playerActor0StateType,playerActor0Flags,"
         "playerActor0X,playerActor0Y,playerActor0Z,playerActor0PrevX,"
         "playerActor0PrevY,playerActor0PrevZ,playerActor0VelX,"
         "playerActor0VelY,playerActor0VelZ,playerActor0PlayerID,"
         "playerActor0TransitionStep,playerActor0SignalLock,"
         "playerActor0Flag192,playerActor0Flags728,playerActor0Flags72C,"
         "playerActor0Flags730,playerActor0TransitFunc,playerActor0TransitArg,"
         "playerActor1Found,playerActor1Guid,playerActor1Base,"
         "playerActor1Settings,playerActor1StateType,playerActor1Flags,"
         "playerActor1X,playerActor1Y,playerActor1Z,playerActor1PrevX,"
         "playerActor1PrevY,playerActor1PrevZ,playerActor1VelX,"
         "playerActor1VelY,playerActor1VelZ,playerActor1PlayerID,"
         "playerActor1TransitionStep,playerActor1SignalLock,"
         "playerActor1Flag192,playerActor1Flags728,playerActor1Flags72C,"
         "playerActor1Flags730,playerActor1TransitFunc,playerActor1TransitArg,"
         "playerTransitionStatus0,playerTransitionStatus1,vsConnectFound,"
         "vsConnectBase,vsConnectWord078,vsConnectWord07C,vsConnectByte0E2,"
         "vsConnectByte106,vsConnectWord114,vsConnectWord118,vsConnectWord120,"
         "vsConnectWord128,vsConnectWord138,vsConnectWord13C,vsConnectWord140,"
         "vsConnectWord144,vsConnectWord148,vsConnectByte153,vsConnectByte154,"
         "vsConnectByte155,vsConnectByte156,vsConnectByte157,vsConnectByte158,"
         "vsConnectWord154,courseSelectFound,courseSelectBase,"
         "courseSelectSettings,courseSelectWord060,courseSelectWord064,"
         "courseSelectWord068,courseSelectWord06C,courseSelectWord070,"
         "courseSelectWord074,courseSelectWord078,courseSelectWord07C,"
         "courseSelectWord080,courseSelectWord084,courseSelectWord088,"
         "courseSelectWord08C,courseSelectWord090,stageCameraFound,"
         "stageCameraWord190,stageCameraWord194,stageCameraWord19C,"
         "stageCameraWord1A0,stageActorManagerFound,stageActorManagerBase,"
         "stageActorManagerStateType,stageControllerFound,stageControllerBase,"
         "stageControllerStateType,mvlObject267Found,mvlObject267Base,"
         "mvlObject267StateType,mvlObject267LeftFound,mvlObject267LeftBase,"
         "mvlObject267LeftStateType,mvlObject267LeftX,mvlObject267LeftY,"
         "mvlObject267LeftZ,mvlObject267RightFound,mvlObject267RightBase,"
         "mvlObject267RightStateType,mvlObject267RightX,mvlObject267RightY,"
         "mvlObject267RightZ,mvlGlobal965C,mvlGlobal9670,mvlGlobal9674,"
         "mvlGlobal9694_0,mvlGlobal9694_1,mvlStageLayoutGateCAC6C,"
         "mvlStageLayoutGateCAC74,mvlStageLayoutGateCAC7C,"
         "mvlStageLayoutGateCACDC,mvlStageLayoutGateCAE80,"
         "mvlStageLayoutGateCAE74,mvlStageLayoutGateCAEB8,"
         "mvlStageLayoutGateCAF20,mvlStageLayoutGateCAF40,"
         "mvlStageLayoutGateCA8C0,mvlStageLayoutGateCA8D0,"
         "mvlStageLayoutGateCAD30,mvlManagerBase,mvlManagerVTable,"
         "mvlManagerGuid,mvlManagerSettings,mvlManagerObjectId,"
         "mvlManagerStateType,mvlManagerFlags,mvlManagerUnk54,"
         "mvlManagerResourcesHeap,mvlManagerWordA8CC,mvlManagerWordA8D0,"
         "mvlManagerWordA8D4,mvlManagerWordA8D8,mvlManagerWordA8DC,"
         "mvlManagerWordA8E0,mvlManagerWordA8E4,mvlManagerHalfA8E8,"
         "mvlManagerHalfA8EA,mvlManagerByteA8EC,mvlManagerHalf494,"
         "mvlManagerHalf4A0,stageSceneFound,stageSceneBase,stageSceneSettings,"
         "stageSceneStateType,stageSceneFlags,stageSceneWord154,"
         "stageSceneWord160,stageSceneWord5618,stageSceneWord561C,"
         "stageSceneWord563C,stageSceneByte5643,stageSceneByte5644,"
         "stageSceneByte5645,stageSceneByte5646,stageSceneByte5648,"
         "stageSceneByte5649,stageSceneUpdateDispatchFunc,"
         "stageSceneUpdateDispatchArg,stageSceneRenderDispatchFunc,"
         "stageSceneRenderDispatchArg,stageSceneGlobal9280,"
         "stageSceneGlobal9284,stageSceneGlobal928C,stageSceneGlobal92B4,"
         "stageSceneGlobal92C0,stageSceneGlobal92C8,stageSceneGlobal92CC,"
         "stageSceneGlobal92D0,stageLiquidPlayerSlot,stageLiquidHeight0,"
         "stageLiquidHeight1,movingHazardFound,movingHazardGuid,"
         "movingHazardSettings,movingHazardStateType,movingHazardFlags,"
         "movingHazardX,movingHazardY,movingHazardZ,movingHazardVelX,"
         "movingHazardVelY,movingHazardLastStepX,movingHazardLastStepY,"
         "movingHazardLastStepZ,movingHazardVelH,movingHazardTargetVelH,"
         "movingHazardAccelV,movingHazardTargetVelV,movingHazardAccelH,"
         "movingHazardTargetVelX,movingHazardTargetVelY,movingHazardTargetVelZ,"
         "objectScanTotal,objectNotCreatedCount,objectActiveCount,"
         "objectDeadCount,objectSkipUpdateCount,objectSkipRenderCount,"
         "objectFirstNotCreatedId,objectFirstNotCreatedBase,"
         "objectFirstNotCreatedFlags,objectSecondNotCreatedId,"
         "objectSecondNotCreatedBase,objectSecondNotCreatedFlags,"
         "objectActiveId0,objectActiveSettings0,objectActiveBase0,"
         "objectActiveId1,objectActiveSettings1,objectActiveBase1,"
         "objectActiveId2,objectActiveSettings2,objectActiveBase2,"
         "objectActiveId3,objectActiveSettings3,objectActiveBase3,"
         "objectActiveId4,objectActiveSettings4,objectActiveBase4,"
         "objectActiveId5,objectActiveSettings5,objectActiveBase5,"
         "objectActiveId6,objectActiveSettings6,objectActiveBase6,"
         "objectActiveId7,objectActiveSettings7,objectActiveBase7,"
         "objectActiveId8,objectActiveSettings8,objectActiveBase8,"
         "objectActiveId9,objectActiveSettings9,objectActiveBase9,"
         "objectActiveId10,objectActiveSettings10,objectActiveBase10,"
         "objectActiveId11,objectActiveSettings11,objectActiveBase11,"
         "objectActiveId12,objectActiveSettings12,objectActiveBase12,"
         "objectActiveId13,objectActiveSettings13,objectActiveBase13,"
         "objectActiveId14,objectActiveSettings14,objectActiveBase14,"
         "objectActiveId15,objectActiveSettings15,objectActiveBase15";
  if (extended)
    out << ",playerCount,player0Powerup,player1Powerup,player0InventoryPowerup,"
           "player1InventoryPowerup,player0Dead,player1Dead,player0Character,"
           "player1Character,player0Lives,player1Lives,player0BattleStars,"
           "player1BattleStars,player0Coins,player1Coins,player0Score,"
           "player1Score,player0DisplayedStars,player1DisplayedStars,"
           "player0Deaths,player1Deaths,player0CollectedStars,"
           "player1CollectedStars,vsCoinCount,entranceSpawnID0,"
           "entranceSpawnID1,entranceTransitionFlags0,entranceTransitionFlags1,"
           "entranceSpawnPtr0,entranceSpawnPtr1,stageCameraBase,"
           "stageCameraTargetX,stageCameraTargetY,stageCameraTargetZ,"
           "stageCameraPositionX,stageCameraPositionY,stageCameraPositionZ,"
           "stageCameraUpX,stageCameraUpY,stageCameraUpZ,stageCameraUnk114,"
           "stageCameraUnk118,stageCameraUnk11C,stageCameraUnk128,"
           "stageCameraUnk12C,stageCameraRoll130,stageCameraGlobalX0,"
           "stageCameraGlobalX1,stageCameraGlobalY0,stageCameraGlobalY1,"
           "stageCameraGlobalWidth0,stageCameraGlobalWidth1,"
           "stageCameraGlobalHeight0,stageCameraGlobalHeight1,"
           "playerCameraFocusPosX0,playerCameraFocusPosX1,"
           "playerCameraFocusPosY0,playerCameraFocusPosY1,"
           "playerCameraFocusPosZ0,playerCameraFocusPosZ1,"
           "playerCameraFocusVelX0,playerCameraFocusVelX1,"
           "playerCameraFocusVelY0,playerCameraFocusVelY1,"
           "playerCameraFocusVelZ0,playerCameraFocusVelZ1,stageDisplayCameraX,"
           "cameraDbgCA880,cameraDbgCAE04,cameraDbgCAE14,cameraDbgCAD6C,"
           "cameraDbgCAD8C,cameraDbgCADB4,cameraDbgCAE60,cameraDbgCAE64,"
           "playerGlobalHash,wifiCandidateHash,renderCandidateHash,"
           "netStateHash,playerActor0ActionFlag,playerActor0SubActionFlag,"
           "playerActor0PhysicsFlag,playerActor0DamageCooldown,"
           "playerActor1ActionFlag,playerActor1SubActionFlag,"
           "playerActor1PhysicsFlag,playerActor1DamageCooldown,"
           "playerActor0LinkedActor,playerActor0TransitionFlag,"
           "playerActor0CollisionFlag,playerActor0EnvironmentFlag,"
           "playerActor0UpdateLocked,playerActor0ControlState,"
           "playerActor0CharacterIDBase,playerActor0RequestedPowerup,"
           "playerActor0CurrentPowerup,playerActor0PreviousPowerup,"
           "playerActor0TransitioningFlag,playerActor0CameraFocusMode,"
           "playerActor0DefeatedFlag,playerActor0PlayerBaseID,"
           "playerActor0VisibleFlag,playerActor0PowerupPhase,"
           "playerActor0PowerupTimer,playerActor0PowerupGainTimer,"
           "playerActor1LinkedActor,playerActor1TransitionFlag,"
           "playerActor1CollisionFlag,playerActor1EnvironmentFlag,"
           "playerActor1UpdateLocked,playerActor1ControlState,"
           "playerActor1CharacterIDBase,playerActor1RequestedPowerup,"
           "playerActor1CurrentPowerup,playerActor1PreviousPowerup,"
           "playerActor1TransitioningFlag,playerActor1CameraFocusMode,"
           "playerActor1DefeatedFlag,playerActor1PlayerBaseID,"
           "playerActor1VisibleFlag,playerActor1PowerupPhase,"
           "playerActor1PowerupTimer,playerActor1PowerupGainTimer";
  out << '\n';
}

void WriteGameStateTraceRow(std::ostream &out, int instanceID,
                            melonDS::u32 frame, const GameStateSample &sample,
                            const GameStateTraceHashes *extendedHashes) {
  out << std::dec << instanceID << ',' << frame << ",0x" << std::hex
      << sample.StageID << ",0x" << sample.StageGroup << ",0x" << sample.VsMode
      << ",0x" << sample.LocalPlayerID << ",0x" << sample.Arm9PC << ",0x"
      << sample.Arm9LR << ",0x" << sample.Arm9SP << ",0x" << sample.Arm9CPSR
      << ",0x" << sample.AppFrameLength << ",0x" << sample.AppUpdateTask
      << ",0x" << sample.AppSleepPhase << ",0x" << sample.AppSleepControl
      << ",0x" << sample.AppSleeping << ",0x" << sample.AppSleepPhaseTimer
      << ",0x" << sample.AppSleepWakeUpTimer << ",0x" << sample.AppBootParam
      << ",0x" << sample.AppBootTarget << ",0x" << sample.AppBootScene << ",0x"
      << sample.GGID << ",0x" << sample.NetCurrentLanguage << ",0x"
      << sample.NetLocalAid << ",0x" << sample.NetState14 << ",0x"
      << sample.NetState1C << ",0x" << sample.NetState20 << ",0x"
      << sample.NetState24 << ",0x" << sample.NetExpectedConsoleCount << ",0x"
      << sample.NetMultiBootSession << ",0x" << sample.NetSessionState << ",0x"
      << sample.NetModuleState << ",0x" << sample.NetMaxSessionChildren << ",0x"
      << sample.NetMaxConsoleCount << ",0x" << sample.NetState5C << ",0x"
      << sample.NetPacketTick << ",0x" << sample.NetPacketKeys << ",0x"
      << sample.NetPacketAction << ",0x" << sample.NetPacketByte5 << ",0x"
      << sample.NetPacketByte6 << ",0x" << sample.NetPacketByte7 << ",0x"
      << sample.NetRandomValue << ",0x" << sample.NetRandomCallCount << ",0x"
      << sample.NetRandomBranchAddress << ",0x" << sample.InputConsole0Held
      << ",0x" << sample.InputConsole0Pressed << ",0x"
      << sample.InputConsole1Held << ",0x" << sample.InputConsole1Pressed
      << ",0x" << sample.InputPlayer0Held << ",0x" << sample.InputPlayer1Held
      << ",0x" << sample.InputPlayer0Pressed << ",0x"
      << sample.InputPlayer1Pressed << ",0x" << sample.StageActorFreezeFlag
      << ",0x" << sample.SceneIsSceneActive << ",0x"
      << sample.ScenePreviousSceneID << ",0x" << sample.SceneNextSceneID
      << ",0x" << sample.SceneCurrentSceneID << ",0x"
      << sample.SceneNextSceneSettings << ",0x" << sample.VsStarFound << ",0x"
      << sample.VsStarGUID << ",0x" << sample.VsStarBase << ",0x"
      << sample.VsStarSettings << ",0x" << sample.VsStarStateType << ",0x"
      << sample.VsStarFlags << ",0x" << sample.VsStarPosX << ",0x"
      << sample.VsStarPosY << ",0x" << sample.VsStarPosZ << ",0x"
      << sample.VsStarActorFound << ",0x" << sample.VsStarActorGUID << ",0x"
      << sample.VsStarActorBase << ",0x" << sample.VsStarActorSettings << ",0x"
      << sample.VsStarActorStateType << ",0x" << sample.VsStarActorFlags
      << ",0x" << sample.VsStarActorPosX << ",0x" << sample.VsStarActorPosY
      << ",0x" << sample.VsStarActorPosZ << ",0x" << sample.PlayerActor0Found
      << ",0x" << sample.PlayerActor0GUID << ",0x" << sample.PlayerActor0Base
      << ",0x" << sample.PlayerActor0Settings << ",0x"
      << sample.PlayerActor0StateType << ",0x" << sample.PlayerActor0Flags
      << ",0x" << sample.PlayerActor0PosX << ",0x" << sample.PlayerActor0PosY
      << ",0x" << sample.PlayerActor0PosZ << ",0x" << sample.PlayerActor0PrevX
      << ",0x" << sample.PlayerActor0PrevY << ",0x" << sample.PlayerActor0PrevZ
      << ",0x" << sample.PlayerActor0VelX << ",0x" << sample.PlayerActor0VelY
      << ",0x" << sample.PlayerActor0VelZ << ",0x"
      << sample.PlayerActor0PlayerID << ",0x"
      << sample.PlayerActor0TransitionStep << ",0x"
      << sample.PlayerActor0SignalLock << ",0x" << sample.PlayerActor0Flag192
      << ",0x" << sample.PlayerActor0Flags728 << ",0x"
      << sample.PlayerActor0Flags72C << ",0x" << sample.PlayerActor0Flags730
      << ",0x" << sample.PlayerActor0TransitFunc << ",0x"
      << sample.PlayerActor0TransitArg << ",0x" << sample.PlayerActor1Found
      << ",0x" << sample.PlayerActor1GUID << ",0x" << sample.PlayerActor1Base
      << ",0x" << sample.PlayerActor1Settings << ",0x"
      << sample.PlayerActor1StateType << ",0x" << sample.PlayerActor1Flags
      << ",0x" << sample.PlayerActor1PosX << ",0x" << sample.PlayerActor1PosY
      << ",0x" << sample.PlayerActor1PosZ << ",0x" << sample.PlayerActor1PrevX
      << ",0x" << sample.PlayerActor1PrevY << ",0x" << sample.PlayerActor1PrevZ
      << ",0x" << sample.PlayerActor1VelX << ",0x" << sample.PlayerActor1VelY
      << ",0x" << sample.PlayerActor1VelZ << ",0x"
      << sample.PlayerActor1PlayerID << ",0x"
      << sample.PlayerActor1TransitionStep << ",0x"
      << sample.PlayerActor1SignalLock << ",0x" << sample.PlayerActor1Flag192
      << ",0x" << sample.PlayerActor1Flags728 << ",0x"
      << sample.PlayerActor1Flags72C << ",0x" << sample.PlayerActor1Flags730
      << ",0x" << sample.PlayerActor1TransitFunc << ",0x"
      << sample.PlayerActor1TransitArg << ",0x"
      << sample.PlayerTransitionStatus0 << ",0x"
      << sample.PlayerTransitionStatus1 << ",0x" << sample.VsConnectFound
      << ",0x" << sample.VsConnectBase << ",0x" << sample.VsConnectWord078
      << ",0x" << sample.VsConnectWord07C << ",0x" << sample.VsConnectByte0E2
      << ",0x" << sample.VsConnectByte106 << ",0x" << sample.VsConnectWord114
      << ",0x" << sample.VsConnectWord118 << ",0x" << sample.VsConnectWord120
      << ",0x" << sample.VsConnectWord128 << ",0x" << sample.VsConnectWord138
      << ",0x" << sample.VsConnectWord13C << ",0x" << sample.VsConnectWord140
      << ",0x" << sample.VsConnectWord144 << ",0x" << sample.VsConnectWord148
      << ",0x" << sample.VsConnectByte153 << ",0x" << sample.VsConnectByte154
      << ",0x" << sample.VsConnectByte155 << ",0x" << sample.VsConnectByte156
      << ",0x" << sample.VsConnectByte157 << ",0x" << sample.VsConnectByte158
      << ",0x" << sample.VsConnectWord154 << ",0x" << sample.CourseSelectFound
      << ",0x" << sample.CourseSelectBase << ",0x"
      << sample.CourseSelectSettings << ",0x" << sample.CourseSelectWord060
      << ",0x" << sample.CourseSelectWord064 << ",0x"
      << sample.CourseSelectWord068 << ",0x" << sample.CourseSelectWord06C
      << ",0x" << sample.CourseSelectWord070 << ",0x"
      << sample.CourseSelectWord074 << ",0x" << sample.CourseSelectWord078
      << ",0x" << sample.CourseSelectWord07C << ",0x"
      << sample.CourseSelectWord080 << ",0x" << sample.CourseSelectWord084
      << ",0x" << sample.CourseSelectWord088 << ",0x"
      << sample.CourseSelectWord08C << ",0x" << sample.CourseSelectWord090
      << ",0x" << sample.StageCameraFound << ",0x" << sample.StageCameraWord190
      << ",0x" << sample.StageCameraWord194 << ",0x"
      << sample.StageCameraWord19C << ",0x" << sample.StageCameraWord1A0
      << ",0x" << sample.StageActorManagerFound << ",0x"
      << sample.StageActorManagerBase << ",0x"
      << sample.StageActorManagerStateType << ",0x"
      << sample.StageControllerFound << ",0x" << sample.StageControllerBase
      << ",0x" << sample.StageControllerStateType << ",0x"
      << sample.MvlObject267Found << ",0x" << sample.MvlObject267Base << ",0x"
      << sample.MvlObject267StateType << ",0x" << sample.MvlObject267LeftFound
      << ",0x" << sample.MvlObject267LeftBase << ",0x"
      << sample.MvlObject267LeftStateType << ",0x"
      << sample.MvlObject267LeftPosX << ",0x" << sample.MvlObject267LeftPosY
      << ",0x" << sample.MvlObject267LeftPosZ << ",0x"
      << sample.MvlObject267RightFound << ",0x" << sample.MvlObject267RightBase
      << ",0x" << sample.MvlObject267RightStateType << ",0x"
      << sample.MvlObject267RightPosX << ",0x" << sample.MvlObject267RightPosY
      << ",0x" << sample.MvlObject267RightPosZ << ",0x" << sample.MvlGlobal965C
      << ",0x" << sample.MvlGlobal9670 << ",0x" << sample.MvlGlobal9674 << ",0x"
      << sample.MvlGlobal9694_0 << ",0x" << sample.MvlGlobal9694_1 << ",0x"
      << sample.MvlStageLayoutGateCAC6C << ",0x"
      << sample.MvlStageLayoutGateCAC74 << ",0x"
      << sample.MvlStageLayoutGateCAC7C << ",0x"
      << sample.MvlStageLayoutGateCACDC << ",0x"
      << sample.MvlStageLayoutGateCAE80 << ",0x"
      << sample.MvlStageLayoutGateCAE74 << ",0x"
      << sample.MvlStageLayoutGateCAEB8 << ",0x"
      << sample.MvlStageLayoutGateCAF20 << ",0x"
      << sample.MvlStageLayoutGateCAF40 << ",0x"
      << sample.MvlStageLayoutGateCA8C0 << ",0x"
      << sample.MvlStageLayoutGateCA8D0 << ",0x"
      << sample.MvlStageLayoutGateCAD30 << ",0x" << sample.MvlManagerBase
      << ",0x" << sample.MvlManagerVTable << ",0x" << sample.MvlManagerGUID
      << ",0x" << sample.MvlManagerSettings << ",0x"
      << sample.MvlManagerObjectID << ",0x" << sample.MvlManagerStateType
      << ",0x" << sample.MvlManagerFlags << ",0x" << sample.MvlManagerUnk54
      << ",0x" << sample.MvlManagerResourcesHeap << ",0x"
      << sample.MvlManagerWordA8CC << ",0x" << sample.MvlManagerWordA8D0
      << ",0x" << sample.MvlManagerWordA8D4 << ",0x"
      << sample.MvlManagerWordA8D8 << ",0x" << sample.MvlManagerWordA8DC
      << ",0x" << sample.MvlManagerWordA8E0 << ",0x"
      << sample.MvlManagerWordA8E4 << ",0x" << sample.MvlManagerHalfA8E8
      << ",0x" << sample.MvlManagerHalfA8EA << ",0x"
      << sample.MvlManagerByteA8EC << ",0x" << sample.MvlManagerHalf494 << ",0x"
      << sample.MvlManagerHalf4A0 << ",0x" << sample.StageSceneFound << ",0x"
      << sample.StageSceneBase << ",0x" << sample.StageSceneSettings << ",0x"
      << sample.StageSceneStateType << ",0x" << sample.StageSceneFlags << ",0x"
      << sample.StageSceneWord154 << ",0x" << sample.StageSceneWord160 << ",0x"
      << sample.StageSceneWord5618 << ",0x" << sample.StageSceneWord561C
      << ",0x" << sample.StageSceneWord563C << ",0x"
      << sample.StageSceneByte5643 << ",0x" << sample.StageSceneByte5644
      << ",0x" << sample.StageSceneByte5645 << ",0x"
      << sample.StageSceneByte5646 << ",0x" << sample.StageSceneByte5648
      << ",0x" << sample.StageSceneByte5649 << ",0x"
      << sample.StageSceneUpdateDispatchFunc << ",0x"
      << sample.StageSceneUpdateDispatchArg << ",0x"
      << sample.StageSceneRenderDispatchFunc << ",0x"
      << sample.StageSceneRenderDispatchArg << ",0x"
      << sample.StageSceneGlobal9280 << ",0x" << sample.StageSceneGlobal9284
      << ",0x" << sample.StageSceneGlobal928C << ",0x"
      << sample.StageSceneGlobal92B4 << ",0x" << sample.StageSceneGlobal92C0
      << ",0x" << sample.StageSceneGlobal92C8 << ",0x"
      << sample.StageSceneGlobal92CC << ",0x" << sample.StageSceneGlobal92D0
      << ",0x" << sample.StageLiquidPlayerSlot << ",0x"
      << sample.StageLiquidHeight0 << ",0x" << sample.StageLiquidHeight1
      << ",0x" << sample.MovingHazardFound << ",0x" << sample.MovingHazardGUID
      << ",0x" << sample.MovingHazardSettings << ",0x"
      << sample.MovingHazardStateType << ",0x" << sample.MovingHazardFlags
      << ",0x" << sample.MovingHazardPosX << ",0x" << sample.MovingHazardPosY
      << ",0x" << sample.MovingHazardPosZ << ",0x" << sample.MovingHazardVelX
      << ",0x" << sample.MovingHazardVelY << ",0x"
      << sample.MovingHazardLastStepX << ",0x" << sample.MovingHazardLastStepY
      << ",0x" << sample.MovingHazardLastStepZ << ",0x"
      << sample.MovingHazardVelH << ",0x" << sample.MovingHazardTargetVelH
      << ",0x" << sample.MovingHazardAccelV << ",0x"
      << sample.MovingHazardTargetVelV << ",0x" << sample.MovingHazardAccelH
      << ",0x" << sample.MovingHazardTargetVelX << ",0x"
      << sample.MovingHazardTargetVelY << ",0x" << sample.MovingHazardTargetVelZ
      << ",0x" << sample.ObjectScanTotal << ",0x"
      << sample.ObjectNotCreatedCount << ",0x" << sample.ObjectActiveCount
      << ",0x" << sample.ObjectDeadCount << ",0x"
      << sample.ObjectSkipUpdateCount << ",0x" << sample.ObjectSkipRenderCount
      << ",0x" << sample.ObjectFirstNotCreatedID << ",0x"
      << sample.ObjectFirstNotCreatedBase << ",0x"
      << sample.ObjectFirstNotCreatedFlags << ",0x"
      << sample.ObjectSecondNotCreatedID << ",0x"
      << sample.ObjectSecondNotCreatedBase << ",0x"
      << sample.ObjectSecondNotCreatedFlags;
  for (int i = 0; i < kObjectTraceSlots; i++) {
    out << ",0x" << sample.ObjectActiveID[i] << ",0x"
        << sample.ObjectActiveSettings[i] << ",0x"
        << sample.ObjectActiveBase[i];
  }

  if (extendedHashes) {
    const melonDS::u64 playerGlobalHash = extendedHashes->PlayerGlobal;
    const melonDS::u64 wifiCandidateHash = extendedHashes->WifiCandidate;
    const melonDS::u64 renderCandidateHash = extendedHashes->RenderCandidate;
    const melonDS::u64 netStateHash = extendedHashes->NetState;

    out << ",0x" << sample.PlayerCount << ",0x" << sample.Player0Powerup
        << ",0x" << sample.Player1Powerup << ",0x"
        << sample.Player0InventoryPowerup << ",0x"
        << sample.Player1InventoryPowerup << ",0x" << sample.Player0Dead
        << ",0x" << sample.Player1Dead << ",0x" << sample.Player0Character
        << ",0x" << sample.Player1Character << ",0x" << sample.Player0Lives
        << ",0x" << sample.Player1Lives << ",0x" << sample.Player0BattleStars
        << ",0x" << sample.Player1BattleStars << ",0x" << sample.Player0Coins
        << ",0x" << sample.Player1Coins << ",0x" << sample.Player0Score << ",0x"
        << sample.Player1Score << ",0x" << sample.Player0DisplayedStars << ",0x"
        << sample.Player1DisplayedStars << ",0x" << sample.Player0Deaths
        << ",0x" << sample.Player1Deaths << ",0x"
        << sample.Player0CollectedStars << ",0x" << sample.Player1CollectedStars
        << ",0x" << sample.VsCoinCount << ",0x" << sample.EntranceSpawnID0
        << ",0x" << sample.EntranceSpawnID1 << ",0x"
        << sample.EntranceTransitionFlags0 << ",0x"
        << sample.EntranceTransitionFlags1 << ",0x" << sample.EntranceSpawnPtr0
        << ",0x" << sample.EntranceSpawnPtr1 << ",0x" << sample.StageCameraBase
        << ",0x" << sample.StageCameraTargetX << ",0x"
        << sample.StageCameraTargetY << ",0x" << sample.StageCameraTargetZ
        << ",0x" << sample.StageCameraPositionX << ",0x"
        << sample.StageCameraPositionY << ",0x" << sample.StageCameraPositionZ
        << ",0x" << sample.StageCameraUpX << ",0x" << sample.StageCameraUpY
        << ",0x" << sample.StageCameraUpZ << ",0x" << sample.StageCameraUnk114
        << ",0x" << sample.StageCameraUnk118 << ",0x"
        << sample.StageCameraUnk11C << ",0x" << sample.StageCameraUnk128
        << ",0x" << sample.StageCameraUnk12C << ",0x"
        << sample.StageCameraRoll130 << ",0x" << sample.StageCameraGlobalX0
        << ",0x" << sample.StageCameraGlobalX1 << ",0x"
        << sample.StageCameraGlobalY0 << ",0x" << sample.StageCameraGlobalY1
        << ",0x" << sample.StageCameraGlobalWidth0 << ",0x"
        << sample.StageCameraGlobalWidth1 << ",0x"
        << sample.StageCameraGlobalHeight0 << ",0x"
        << sample.StageCameraGlobalHeight1 << ",0x"
        << sample.PlayerCameraFocusPosX0 << ",0x"
        << sample.PlayerCameraFocusPosX1 << ",0x"
        << sample.PlayerCameraFocusPosY0 << ",0x"
        << sample.PlayerCameraFocusPosY1 << ",0x"
        << sample.PlayerCameraFocusPosZ0 << ",0x"
        << sample.PlayerCameraFocusPosZ1 << ",0x"
        << sample.PlayerCameraFocusVelX0 << ",0x"
        << sample.PlayerCameraFocusVelX1 << ",0x"
        << sample.PlayerCameraFocusVelY0 << ",0x"
        << sample.PlayerCameraFocusVelY1 << ",0x"
        << sample.PlayerCameraFocusVelZ0 << ",0x"
        << sample.PlayerCameraFocusVelZ1 << ",0x" << sample.StageDisplayCameraX
        << ",0x" << sample.CameraDbgCA880 << ",0x" << sample.CameraDbgCAE04
        << ",0x" << sample.CameraDbgCAE14 << ",0x" << sample.CameraDbgCAD6C
        << ",0x" << sample.CameraDbgCAD8C << ",0x" << sample.CameraDbgCADB4
        << ",0x" << sample.CameraDbgCAE60 << ",0x" << sample.CameraDbgCAE64
        << ",0x" << playerGlobalHash << ",0x" << wifiCandidateHash << ",0x"
        << renderCandidateHash << ",0x" << netStateHash << ",0x"
        << sample.PlayerActor0ActionFlag << ",0x"
        << sample.PlayerActor0SubActionFlag << ",0x"
        << sample.PlayerActor0PhysicsFlag << ",0x"
        << sample.PlayerActor0DamageCooldown << ",0x"
        << sample.PlayerActor1ActionFlag << ",0x"
        << sample.PlayerActor1SubActionFlag << ",0x"
        << sample.PlayerActor1PhysicsFlag << ",0x"
        << sample.PlayerActor1DamageCooldown << ",0x"
        << sample.PlayerActor0LinkedActor << ",0x"
        << sample.PlayerActor0TransitionFlag << ",0x"
        << sample.PlayerActor0CollisionFlag << ",0x"
        << sample.PlayerActor0EnvironmentFlag << ",0x"
        << sample.PlayerActor0UpdateLocked << ",0x"
        << sample.PlayerActor0ControlState << ",0x"
        << sample.PlayerActor0CharacterIDBase << ",0x"
        << sample.PlayerActor0RequestedPowerup << ",0x"
        << sample.PlayerActor0CurrentPowerup << ",0x"
        << sample.PlayerActor0PreviousPowerup << ",0x"
        << sample.PlayerActor0TransitioningFlag << ",0x"
        << sample.PlayerActor0CameraFocusMode << ",0x"
        << sample.PlayerActor0DefeatedFlag << ",0x"
        << sample.PlayerActor0PlayerBaseID << ",0x"
        << sample.PlayerActor0VisibleFlag << ",0x"
        << sample.PlayerActor0PowerupPhase << ",0x"
        << sample.PlayerActor0PowerupTimer << ",0x"
        << sample.PlayerActor0PowerupGainTimer << ",0x"
        << sample.PlayerActor1LinkedActor << ",0x"
        << sample.PlayerActor1TransitionFlag << ",0x"
        << sample.PlayerActor1CollisionFlag << ",0x"
        << sample.PlayerActor1EnvironmentFlag << ",0x"
        << sample.PlayerActor1UpdateLocked << ",0x"
        << sample.PlayerActor1ControlState << ",0x"
        << sample.PlayerActor1CharacterIDBase << ",0x"
        << sample.PlayerActor1RequestedPowerup << ",0x"
        << sample.PlayerActor1CurrentPowerup << ",0x"
        << sample.PlayerActor1PreviousPowerup << ",0x"
        << sample.PlayerActor1TransitioningFlag << ",0x"
        << sample.PlayerActor1CameraFocusMode << ",0x"
        << sample.PlayerActor1DefeatedFlag << ",0x"
        << sample.PlayerActor1PlayerBaseID << ",0x"
        << sample.PlayerActor1VisibleFlag << ",0x"
        << sample.PlayerActor1PowerupPhase << ",0x"
        << sample.PlayerActor1PowerupTimer << ",0x"
        << sample.PlayerActor1PowerupGainTimer;
  }

  out << std::dec << '\n';
}

#undef NSMB_GAME_STATE_WIRE_FIELDS

void RemoteStateStore::ResetForRestart() {
  GameStateHashes_.clear();
  GameStates_.clear();
  PlayerStates_.clear();
  WorldState_.reset();
  MovingHazardState_.reset();
  WorldActorSnapshot_.reset();
  WorldEffectState_.reset();
}

void RemoteStateStore::StoreGameState(const DecodedGameState &state) {
  const melonDS::u64 key =
      GameStateKey(static_cast<int>(state.Instance), state.Frame);
  GameStateHashes_[key] = state.Hashes;
  GameStates_[key] = state.Sample;
}

std::size_t RemoteStateStore::StorePlayerState(
    const WireProtocol::WirePlayerState &state) {
  PlayerStates_[PlayerStateKey(state.Player, state.Frame)] = state;
  while (PlayerStates_.size() > PlayerHistoryLimit)
    PlayerStates_.erase(PlayerStates_.begin());
  return PlayerStates_.size();
}

bool RemoteStateStore::StoreWorldState(
    const WireProtocol::WireWorldState &state) {
  return StoreLatest(WorldState_, state);
}

bool RemoteStateStore::StoreMovingHazardState(
    const WireProtocol::WireMovingHazardState &state) {
  return StoreLatest(MovingHazardState_, state);
}

bool RemoteStateStore::StoreWorldActorSnapshot(
    const WireProtocol::WireWorldActorSnapshotState &state) {
  return StoreLatest(WorldActorSnapshot_, state);
}

bool RemoteStateStore::StoreWorldEffectState(
    const WireProtocol::WireWorldEffectState &state) {
  return StoreLatest(WorldEffectState_, state);
}

const GameStateSyncHashes *RemoteStateStore::FindGameStateHashes(
    int instanceID, melonDS::u32 frame) const {
  const auto found = GameStateHashes_.find(GameStateKey(instanceID, frame));
  return found != GameStateHashes_.end() ? &found->second : nullptr;
}

const GameStateSample *RemoteStateStore::FindGameState(
    int instanceID, melonDS::u32 frame) const {
  const auto found = GameStates_.find(GameStateKey(instanceID, frame));
  return found != GameStates_.end() ? &found->second : nullptr;
}

bool RemoteStateStore::FindLatestGameState(int instanceID,
                                           melonDS::u32 frame,
                                           GameStateSample &state,
                                           melonDS::u32 &stateFrame) const {
  const melonDS::u64 firstKey = GameStateKey(instanceID, 0);
  auto found = GameStates_.upper_bound(GameStateKey(instanceID, frame));
  if (found == GameStates_.begin()) {
    stateFrame = 0;
    return false;
  }
  --found;
  if (found->first < firstKey) {
    stateFrame = 0;
    return false;
  }
  state = found->second;
  stateFrame = static_cast<melonDS::u32>(found->first);
  return true;
}

bool RemoteStateStore::FindLatestPlayerState(
    melonDS::u32 player, melonDS::u32 frame,
    WireProtocol::WirePlayerState &state, melonDS::u32 &stateFrame) const {
  const melonDS::u64 firstKey = PlayerStateKey(player, 0);
  auto found = PlayerStates_.upper_bound(PlayerStateKey(player, frame));
  if (found == PlayerStates_.begin()) {
    stateFrame = 0;
    return false;
  }
  --found;
  if (found->first < firstKey) {
    stateFrame = 0;
    return false;
  }
  state = found->second;
  stateFrame = state.Frame;
  return true;
}

const WireProtocol::WireWorldState *RemoteStateStore::WorldState() const {
  return WorldState_ ? &*WorldState_ : nullptr;
}

const WireProtocol::WireMovingHazardState *
RemoteStateStore::MovingHazardState() const {
  return MovingHazardState_ ? &*MovingHazardState_ : nullptr;
}

const WireProtocol::WireWorldActorSnapshotState *
RemoteStateStore::WorldActorSnapshot() const {
  return WorldActorSnapshot_ ? &*WorldActorSnapshot_ : nullptr;
}

const WireProtocol::WireWorldEffectState *
RemoteStateStore::WorldEffectState() const {
  return WorldEffectState_ ? &*WorldEffectState_ : nullptr;
}

std::size_t RemoteStateStore::PlayerStateCount() const {
  return PlayerStates_.size();
}

void StateSyncRuntime::ResetForRestart(int instanceID) {
  if (instanceID < 0 || instanceID >= 16)
    return;

  LocalGameStateHashes.clear();
  RemoteState.ResetForRestart();
  LastSentGameStateFrame[instanceID] = 0;
  LastSentPlayerStateFrame[instanceID] = 0;
  LastSentWorldStateFrame[instanceID] = 0;
  for (int player = 0; player < 2; player++) {
    LastAppliedPlayerGlobalsFrame[instanceID][player] = 0;
    PlayerActorBaseCache[instanceID][player] = 0;
    PlayerActorGUIDCache[instanceID][player] = 0;
  }

  WorldStarActorBaseCache[instanceID] = 0;
  WorldStarActorGUIDCache[instanceID] = 0;
  LastSpawnedWorldItemRemoteGUID[instanceID] = 0;
  LastConfirmedWorldItemRemoteGUID[instanceID] = 0;
  PendingWorldItemRemoteGUID[instanceID] = 0;
  PendingWorldItemFirstMissingFrame[instanceID] = 0;
  LastSpawnedNeutralWorldItemRemoteGUID[instanceID] = 0;
  LastConfirmedNeutralWorldItemRemoteGUID[instanceID] = 0;
  PendingNeutralWorldItemRemoteGUID[instanceID] = 0;
  PendingNeutralWorldItemFirstMissingFrame[instanceID] = 0;
  LastSpawnedDroppedStarItemRemoteGUID[instanceID] = 0;
  LastConfirmedDroppedStarItemRemoteGUID[instanceID] = 0;
  PendingDroppedStarItemRemoteGUID[instanceID] = 0;
  PendingDroppedStarItemFirstMissingFrame[instanceID] = 0;
  WorldMovingHazardBaseCache[instanceID] = 0;
  WorldMovingHazardGUIDCache[instanceID] = 0;
  WorldMovingHazardCacheCounts[instanceID] = 0;
  std::fill(std::begin(WorldMovingHazardBaseCaches[instanceID]),
            std::end(WorldMovingHazardBaseCaches[instanceID]), 0);
  std::fill(std::begin(WorldMovingHazardGUIDCaches[instanceID]),
            std::end(WorldMovingHazardGUIDCaches[instanceID]), 0);
  std::fill(std::begin(WorldMovingHazardRemoteGUIDMaps[instanceID]),
            std::end(WorldMovingHazardRemoteGUIDMaps[instanceID]), 0);
  std::fill(std::begin(WorldMovingHazardLocalGUIDMaps[instanceID]),
            std::end(WorldMovingHazardLocalGUIDMaps[instanceID]), 0);
  std::fill(std::begin(WorldActorSnapshotRemoteGUIDMaps[instanceID]),
            std::end(WorldActorSnapshotRemoteGUIDMaps[instanceID]), 0);
  std::fill(std::begin(WorldActorSnapshotLocalGUIDMaps[instanceID]),
            std::end(WorldActorSnapshotLocalGUIDMaps[instanceID]), 0);
}

} // namespace NsmbNetplayPoC::GameStateModel
