#ifndef NSMB_GAME_STATE_READER_H
#define NSMB_GAME_STATE_READER_H

#include "NsmbGameState.h"

#include <vector>

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

struct ObjectLifecycleSummary {
  melonDS::u32 Total = 0;
  melonDS::u32 NotCreated = 0;
  melonDS::u32 Active = 0;
  melonDS::u32 Dead = 0;
  melonDS::u32 SkipUpdate = 0;
  melonDS::u32 SkipRender = 0;
  melonDS::u32 FirstNotCreatedID = 0;
  melonDS::u32 FirstNotCreatedBase = 0;
  melonDS::u32 FirstNotCreatedFlags = 0;
  melonDS::u32 SecondNotCreatedID = 0;
  melonDS::u32 SecondNotCreatedBase = 0;
  melonDS::u32 SecondNotCreatedFlags = 0;
  melonDS::u32 ActiveID[GameStateModel::kObjectTraceSlots]{};
  melonDS::u32 ActiveSettings[GameStateModel::kObjectTraceSlots]{};
  melonDS::u32 ActiveBase[GameStateModel::kObjectTraceSlots]{};
};

struct GameStateObjectScanEntry {
  ObjectScanSample Actor;
  melonDS::u32 Offset = 0;
  melonDS::u32 VTable = 0;
  melonDS::u16 ObjectID = 0;
  melonDS::u8 LifecycleState = 0;
  melonDS::u8 Type = 0;
  melonDS::u8 SkipFlags = 0;
};

struct GameStateObjectScanCache {
  std::vector<GameStateObjectScanEntry> Entries;
  ObjectLifecycleSummary Lifecycle;
};

class ScopedGameStateObjectScanCache {
public:
  explicit ScopedGameStateObjectScanCache(
      const GameStateObjectScanCache &cache);
  ~ScopedGameStateObjectScanCache();

  ScopedGameStateObjectScanCache(const ScopedGameStateObjectScanCache &) =
      delete;
  ScopedGameStateObjectScanCache &
  operator=(const ScopedGameStateObjectScanCache &) = delete;

private:
  const GameStateObjectScanCache *Previous;
};

GameStateObjectScanCache BuildGameStateObjectScanCache(melonDS::NDS *nds);
bool HasActiveObjectScanCache();
melonDS::u32 FindObjectBaseByID(melonDS::NDS *nds, melonDS::u16 objectID);
ObjectScanSample FindVsBattleStarCandidate(melonDS::NDS *nds);
ObjectScanSample FindObjectByIDAndSettings(melonDS::NDS *nds,
                                           melonDS::u16 expectedObjectID,
                                           melonDS::u32 expectedSettings);
ObjectScanSample FindObjectByID(melonDS::NDS *nds,
                                melonDS::u16 expectedObjectID);
ObjectScanSample FindObjectByIDAndSettingsLoose(melonDS::NDS *nds,
                                                melonDS::u16 expectedObjectID,
                                                melonDS::u32 expectedSettings);
ObjectPairScanSample FindObjectPairByIDSortedX(melonDS::NDS *nds,
                                               melonDS::u16 expectedObjectID);
PlayerActorScanSample FindPlayerActors(melonDS::NDS *nds);
bool ReadPlayerActorByBase(melonDS::NDS *nds, melonDS::u32 base,
                           melonDS::u32 expectedGUID, ObjectScanSample &actor);
melonDS::u32 FindCachedObjectBaseByID(melonDS::u16 objectID);
bool ReadObjectByBase(melonDS::NDS *nds, melonDS::u32 base,
                      melonDS::u32 expectedGUID, melonDS::u16 expectedObjectID,
                      melonDS::u32 expectedSettings, ObjectScanSample &actor);
std::vector<ObjectScanSample> FindActiveObjectsByIDAndSettings(
    melonDS::NDS *nds, melonDS::u16 expectedObjectID,
    melonDS::u32 expectedSettings, bool includeStateType2 = false);
ObjectScanSample FindNewestActiveObjectByIDAndSettings(
    melonDS::NDS *nds, melonDS::u16 expectedObjectID,
    melonDS::u32 expectedSettings, bool includeStateType2 = false);
ObjectLifecycleSummary SummarizeObjectLifecycle(melonDS::NDS *nds);

void ReadCoreState(melonDS::NDS *nds, GameStateModel::GameStateSample &sample);
void ReadBattleStarState(melonDS::NDS *nds,
                         GameStateModel::GameStateSample &sample);
void ReadPlayerAndCameraGlobals(melonDS::NDS *nds,
                                GameStateModel::GameStateSample &sample);
void ReadMvlGlobals(melonDS::NDS *nds, GameStateModel::GameStateSample &sample);
void ReadStageObjectState(melonDS::NDS *nds, melonDS::u32 stageSceneSettings,
                          GameStateModel::GameStateSample &sample);
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
