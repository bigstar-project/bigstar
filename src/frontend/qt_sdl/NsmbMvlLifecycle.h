#ifndef NSMB_MVL_LIFECYCLE_H
#define NSMB_MVL_LIFECYCLE_H

#include "NsmbGameState.h"
#include "NsmbInputTimeline.h"
#include "NsmbMvlRuntime.h"
#include "NsmbNetplayConfig.h"
#include "NsmbNetplayCoordinator.h"
#include "NsmbNetplayDiagnostics.h"
#include "NsmbNetplaySession.h"
#include "NsmbPacketBridgeRuntime.h"

#include <functional>
#include <mutex>

namespace melonDS {
class NDS;
}

namespace NsmbMvlNetplay::MvlLifecycle {

struct Context {
  Config::MvlConfig &Mvl;
  Config::ConnectionConfig &Connection;
  Config::PacketBridgeConfig &PacketBridge;
  Config::InputConfig &Input;
  Config::RuntimePatchConfig &RuntimePatch;
  const Config::RollbackConfig &Rollback;
  MvlRuntime::Runtime &Runtime;
  PacketBridge::Runtime &PacketBridgeRuntime;
  NetplaySession::Runtime &Session;
  InputTimeline::Runtime &Inputs;
  Coordination::Runtime &Coordinator;
  Diagnostics::Runtime &DiagnosticsRuntime;
  GameStateModel::StateSyncRuntime &GameSync;
  GameStateModel::GameStateTraceWriter &GameStateTrace;
  std::mutex &Mutex;
  int &CurrentStage;
  melonDS::u32 &CurrentStageSceneSettings;
  bool Host = false;
  bool Client = false;
};

struct Hooks {
  std::function<GameStateModel::GameStateSample(melonDS::NDS *)>
      ReadGameStateSample;
};

melonDS::u32 ComposeSceneSettingsForStage(int stage);
int StageForGame(Context context, int instanceID);
void RefreshGameSelection(Context context, int instanceID);
melonDS::u32 MatchSeedForGame(Context context, int instanceID);
melonDS::u32 RestartPacketCutoffFrame(Context context);

void SaveBootstrapCheckpointIfNeeded(Context context, int instanceID,
                                     melonDS::u32 frame, melonDS::NDS *nds);
bool RestartAfterResultIfNeeded(Context context, int instanceID,
                                melonDS::u32 frame, melonDS::NDS *nds);
bool ShouldPauseInputForRestart(Context context, int instanceID,
                                melonDS::NDS *nds);
void SaveGameplayCheckpointIfNeeded(Context context, const Hooks &hooks,
                                    int instanceID, melonDS::u32 frame,
                                    melonDS::NDS *nds);

} // namespace NsmbMvlNetplay::MvlLifecycle

#endif
