#ifndef NSMB_GAME_STATE_READER_H
#define NSMB_GAME_STATE_READER_H

#include "NsmbGameState.h"

namespace melonDS {
class NDS;
}

namespace NsmbNetplayPoC::GameStateReader {

struct ObjectScanSample {
  melonDS::u32 Found = 0;
  melonDS::u32 GUID = 0;
  melonDS::u32 Base = 0;
  melonDS::u32 Settings = 0;
  melonDS::u32 StateType = 0;
  melonDS::u32 Flags = 0;
  melonDS::u32 PosX = 0;
  melonDS::u32 PosY = 0;
  melonDS::u32 PosZ = 0;
  melonDS::u32 PrevX = 0;
  melonDS::u32 PrevY = 0;
  melonDS::u32 PrevZ = 0;
  melonDS::u32 LastStepX = 0;
  melonDS::u32 LastStepY = 0;
  melonDS::u32 LastStepZ = 0;
  melonDS::u32 VelH = 0;
  melonDS::u32 TargetVelH = 0;
  melonDS::u32 AccelV = 0;
  melonDS::u32 TargetVelV = 0;
  melonDS::u32 AccelH = 0;
  melonDS::u32 VelX = 0;
  melonDS::u32 VelY = 0;
  melonDS::u32 VelZ = 0;
  melonDS::u32 TargetVelX = 0;
  melonDS::u32 TargetVelY = 0;
  melonDS::u32 TargetVelZ = 0;
};

struct PlayerActorScanSample {
  ObjectScanSample Actor0;
  ObjectScanSample Actor1;
};

struct ObjectPairScanSample {
  ObjectScanSample Left;
  ObjectScanSample Right;
};

void ReadCoreState(melonDS::NDS *nds, GameStateModel::GameStateSample &sample);
void ReadPlayerAndCameraGlobals(melonDS::NDS *nds,
                                GameStateModel::GameStateSample &sample);
void ReadMvlGlobals(melonDS::NDS *nds, GameStateModel::GameStateSample &sample);
void ReadProjectileGlobals(melonDS::NDS *nds,
                           GameStateModel::GameStateSample &sample);
void CopyPlayerActor(const ObjectScanSample &actor,
                     GameStateModel::GameStateSample &sample, int player);
void ReadPlayerTileDamage(melonDS::NDS *nds, const ObjectScanSample &actor,
                          GameStateModel::GameStateSample &sample, int player);
void ReadPlayerTransitionState(melonDS::NDS *nds, const ObjectScanSample &actor,
                               GameStateModel::GameStateSample &sample,
                               int player);
void ReadPlayerBaseRuntimeState(melonDS::NDS *nds,
                                const ObjectScanSample &actor,
                                GameStateModel::GameStateSample &sample,
                                int player);

} // namespace NsmbNetplayPoC::GameStateReader

#endif
