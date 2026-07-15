#pragma once

#include "NsmbGameState.h"
#include "NsmbInputTimeline.h"
#include "NsmbNetplayConfig.h"
#include "NsmbNetplayDiagnostics.h"
#include "NsmbNetplaySession.h"
#include "NsmbRollbackStore.h"

#include <functional>
#include <mutex>

namespace melonDS {
class NDS;
}

namespace NsmbMvlNetplay::GameplayDiagnostics {

struct Context {
  Config::DiagnosticsConfig &Diagnostics;
  const Config::ConnectionConfig &Connection;
  const Config::StateSyncConfig &StateSync;
  const Config::RuntimePatchConfig &RuntimePatch;
  Diagnostics::Runtime &Runtime;
  InputTimeline::Runtime &InputRuntime;
  NetplaySession::Runtime &SessionRuntime;
  GameStateModel::StateSyncRuntime &GameSync;
  RollbackStorage::Statistics &RollbackStats;
  std::mutex &Mutex;
  bool Host = false;
};

struct Hooks {
  std::function<GameStateModel::GameStateSample(melonDS::NDS *)> ReadGameState;
  std::function<bool(melonDS::NDS *)> IsGameplay;
};

void StartHangWatchdog(Context context);
void Stop(Context context);
void RecordActiveFrameTiming(Context context, int instanceID,
                             melonDS::u32 frame);
void ReportGameStateMismatchLocked(
    Context context, const GameStateModel::GameStateHashMismatch &mismatch);
void EmitStartReadyEventLocked(Context context, const char *direction,
                               melonDS::u32 localFrame,
                               melonDS::u32 remoteFrame);
void EmitStartupEvent(Context context);
void RecordSnapshotIfNeeded(Context context, int instanceID, melonDS::u32 frame,
                            melonDS::NDS *nds);
void TracePlayerLifeChanges(Context context, const Hooks &hooks, int instanceID,
                            melonDS::u32 frame, melonDS::NDS *nds);
void TraceGameplayHeartbeatIfNeeded(Context context, int instanceID,
                                    melonDS::u32 frame, melonDS::NDS *nds);
void CaptureScreenshotIfNeeded(Context context, int instanceID,
                               melonDS::u32 frame, melonDS::NDS *nds);

} // namespace NsmbMvlNetplay::GameplayDiagnostics
