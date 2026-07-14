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

bool WriteObjectTransformByBase(melonDS::NDS *nds, melonDS::u32 base,
                                melonDS::u32 posX, melonDS::u32 posY,
                                melonDS::u32 posZ, melonDS::u32 prevX,
                                melonDS::u32 prevY, melonDS::u32 prevZ,
                                melonDS::u32 velX, melonDS::u32 velY,
                                melonDS::u32 velZ);
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

} // namespace NsmbNetplayPoC::GameStateWriter

#endif
