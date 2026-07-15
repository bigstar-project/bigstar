#ifndef NSMB_GAME_STATE_WRITER_H
#define NSMB_GAME_STATE_WRITER_H

#include "NsmbGameState.h"
#include "NsmbGameStateReader.h"

namespace melonDS {
class NDS;
}

namespace NsmbNetplayPoC::GameStateWriter {

struct ObjectTransform {
  melonDS::u32 PosX = 0;
  melonDS::u32 PosY = 0;
  melonDS::u32 PosZ = 0;
  melonDS::u32 PrevX = 0;
  melonDS::u32 PrevY = 0;
  melonDS::u32 PrevZ = 0;
  melonDS::u32 VelX = 0;
  melonDS::u32 VelY = 0;
  melonDS::u32 VelZ = 0;
};

struct PlayerDeathCounterPatchResult {
  melonDS::u32 OldDeaths[2]{};
  melonDS::u32 OldLives[2]{};
};

struct PlayerBytePairPatchResult {
  melonDS::u8 OldValues[2]{};
};

struct PlayerPowerupPatchResult {
  melonDS::u8 OldGlobalValues[2]{};
  melonDS::u32 ActorBases[2]{};
  melonDS::u8 OldActorStates[2]{0xFF, 0xFF};
  melonDS::u8 OldActorForms[2]{0xFF, 0xFF};
};

struct PlayerStarCounterPatchResult {
  melonDS::u32 OldBattleStars[2]{};
  melonDS::u32 OldDisplayedStars[2]{};
  melonDS::u32 OldCollectedStars[2]{};
};

inline ObjectTransform PredictWorldActorTransform(
    const WireProtocol::WireWorldActorState &state,
    melonDS::u32 predictFrames) {
  return {
      state.PosX + state.VelX * predictFrames,
      state.PosY + state.VelY * predictFrames,
      state.PosZ + state.VelZ * predictFrames,
      state.PrevX + state.VelX * predictFrames,
      state.PrevY + state.VelY * predictFrames,
      state.PrevZ + state.VelZ * predictFrames,
      state.VelX,
      state.VelY,
      state.VelZ,
  };
}

struct GameStateApplyOptions {
  bool RemotePlayerOnly = false;
  int RemotePlayer = 0;
  bool CriticalGlobals = false;
  bool StageObjects = false;
  bool StarObjects = false;
  bool PlayerActors = false;
  melonDS::u32 StageSceneSettings = 0;
};

struct GameStateApplyResult {
  bool RemotePlayerApplied = false;
};

struct ApplyTraceOptions {
  bool Enabled = false;
  int Interval = 1;

  bool ShouldTrace(melonDS::u32 frame) const {
    return Enabled &&
           (Interval <= 1 ||
            frame % static_cast<melonDS::u32>(Interval) == 0);
  }
};

struct WorldStateApplyOptions {
  int InstanceID = 0;
  melonDS::u32 Frame = 0;
  int MaxPredictFrames = 0;
  int ActorRescanInterval = 0;
  bool Client = false;
  bool ApplyStarActor = false;
  ApplyTraceOptions Trace;
};

struct MovingHazardApplyOptions {
  int InstanceID = 0;
  melonDS::u32 Frame = 0;
  int MaxPredictFrames = 0;
  int ActorRescanInterval = 0;
  bool TraceMapping = false;
};

struct PlayerStateApplyOptions {
  int InstanceID = 0;
  int RemotePlayer = 0;
  melonDS::u32 Frame = 0;
  melonDS::u32 SampleFrame = 0;
  int MaxPredictFrames = 0;
  bool ApplyGlobals = false;
  ApplyTraceOptions Trace;
};

bool WriteObjectTransformByBase(melonDS::NDS *nds, melonDS::u32 base,
                                melonDS::u32 posX, melonDS::u32 posY,
                                melonDS::u32 posZ, melonDS::u32 prevX,
                                melonDS::u32 prevY, melonDS::u32 prevZ,
                                 melonDS::u32 velX, melonDS::u32 velY,
                                 melonDS::u32 velZ);
bool WriteObjectTransformAndClearMotionByBase(
    melonDS::NDS *nds, melonDS::u32 base, melonDS::u32 posX,
    melonDS::u32 posY, melonDS::u32 posZ);
bool WritePlayerDeathCounterPatch(
    melonDS::NDS *nds, const melonDS::u32 deaths[2], bool writeLives,
    const melonDS::u32 lives[2], PlayerDeathCounterPatchResult &result);
bool WritePlayerInventoryPowerupPatch(
    melonDS::NDS *nds, const melonDS::u8 values[2],
    PlayerBytePairPatchResult &result);
bool WritePlayerPowerupPatch(melonDS::NDS *nds,
                             const melonDS::u8 values[2],
                             PlayerPowerupPatchResult &result);
bool WritePlayerStarCounterPatch(
    melonDS::NDS *nds, const melonDS::u32 battleStars[2],
    const melonDS::u32 displayedStars[2],
    const melonDS::u32 collectedStars[2],
    PlayerStarCounterPatchResult &result);
bool ApplyWireWorldActorState(
    melonDS::NDS *nds, const WireProtocol::WireWorldActorState &state,
    melonDS::u32 predictFrames, melonDS::u32 localBase);
bool ApplyWireWorldMovingHazardState(
    melonDS::NDS *nds, const WireProtocol::WireWorldActorState &state,
    melonDS::u32 predictFrames, melonDS::u32 localBase);
melonDS::u64 WorldActorMatchDistance(
    const WireProtocol::WireWorldActorState &remoteActor,
    const GameStateReader::ObjectScanSample &localActor);

bool WritePlayerRuntimeStateByBase(
    melonDS::NDS *nds, melonDS::u32 base,
    const WireProtocol::WirePlayerState &state);
bool WritePlayerMinimalTransitionStateByBase(
    melonDS::NDS *nds, melonDS::u32 base,
    const WireProtocol::WirePlayerState &state);
bool IsPlayerInActorTransition(melonDS::NDS *nds, melonDS::u32 base);
bool WritePlayerGlobalState(melonDS::NDS *nds,
                            const WireProtocol::WirePlayerState &state);

GameStateApplyResult ApplyGameState(
    melonDS::NDS *nds, const GameStateModel::GameStateSample &sample,
    const GameStateApplyOptions &options);
void ApplyWorldState(melonDS::NDS *nds,
                     const WireProtocol::WireWorldState &sample,
                     GameStateModel::StateSyncRuntime &runtime,
                     const WorldStateApplyOptions &options);
void ApplyMovingHazardState(
    melonDS::NDS *nds, const WireProtocol::WireMovingHazardState &sample,
    GameStateModel::StateSyncRuntime &runtime,
    const MovingHazardApplyOptions &options);
void ApplyPlayerState(melonDS::NDS *nds,
                      const WireProtocol::WirePlayerState &sample,
                      GameStateModel::StateSyncRuntime &runtime,
                      const PlayerStateApplyOptions &options);

} // namespace NsmbNetplayPoC::GameStateWriter

#endif
