#ifndef NSMB_GAME_STATE_SYNC_H
#define NSMB_GAME_STATE_SYNC_H

#include "NsmbGameState.h"
#include "NsmbNetplayConfig.h"
#include "NsmbNetplayDiagnostics.h"
#include "NsmbNetplayTransport.h"

#include <cstddef>
#include <functional>
#include <mutex>

namespace melonDS {
class NDS;
}

namespace NsmbMvlNetplay::GameStateSync {

struct Context {
  const Config::BootstrapConfig &Bootstrap;
  const Config::DiagnosticsConfig &Diagnostics;
  const Config::StateSyncConfig &StateSync;
  const Config::InputConfig &Input;
  const Config::ConnectionConfig &Connection;
  Diagnostics::Runtime &DiagnosticsRuntime;
  GameStateModel::StateSyncRuntime &Runtime;
  GameStateModel::GameStateTraceWriter &TraceWriter;
  NsmbNetplayTransport::Transport &Transport;
  std::mutex &Mutex;
  melonDS::u32 StageSceneSettings = 0;
  bool Enabled = false;
  bool Client = false;
};

struct Hooks {
  std::function<void()> PumpNetworkLocked;
  std::function<int()> CurrentLocalPlayer;
  std::function<void(const GameStateModel::GameStateHashMismatch &)>
      ReportMismatchLocked;
};

GameStateModel::GameStateSample ReadSample(melonDS::NDS *nds,
                                           melonDS::u32 stageSceneSettings);
void HandleReceivedPacketLocked(Context context, const Hooks &hooks,
                                const void *data, std::size_t size);
void ApplyRemote(Context context, const Hooks &hooks, int instanceID,
                 melonDS::u32 frame, melonDS::NDS *nds);
void UpdateHangSnapshot(Context context, int instanceID, melonDS::u32 frame,
                        melonDS::NDS *nds);
void Trace(Context context, int instanceID, melonDS::u32 frame,
           melonDS::NDS *nds);
void TraceWorld(Context context, int instanceID, melonDS::u32 frame,
                melonDS::NDS *nds);
void Sync(Context context, const Hooks &hooks, int instanceID,
          melonDS::u32 frame, melonDS::NDS *nds);

} // namespace NsmbMvlNetplay::GameStateSync

#endif
