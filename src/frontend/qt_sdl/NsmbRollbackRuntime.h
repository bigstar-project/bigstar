#ifndef NSMB_ROLLBACK_RUNTIME_H
#define NSMB_ROLLBACK_RUNTIME_H

#include "NsmbInputTimeline.h"
#include "NsmbNetplayConfig.h"
#include "NsmbRollbackStore.h"

#include <functional>
#include <mutex>

namespace melonDS {
class NDS;
}

namespace NsmbNetplayPoC::RollbackRuntime {

struct Context {
  const Config::RollbackConfig &Config;
  const Config::InputConfig &Input;
  InputTimeline::Runtime &Inputs;
  RollbackStorage::Store &Store;
  RollbackStorage::Statistics &Statistics;
  std::mutex &Mutex;
  melonDS::u32 NetplayStartFrame = 0;
};

struct ResimulationHooks {
  std::function<int()> CurrentLocalPlayer;
  std::function<void(int, melonDS::u32, melonDS::NDS *)> ApplyFramePatches;
  std::function<void(int, melonDS::u32, melonDS::NDS *, int, const InputState &,
                     const InputState &, bool, bool)>
      WritePacketBridgeInputs;
  std::function<InputState(const InputState &)> PrepareRuntimeInput;
  std::function<void(melonDS::u32, melonDS::NDS *)> ApplyPostFramePatches;
};

const char *BackendName(const Config::RollbackConfig &config);
bool IsPreimageBackend(const Config::RollbackConfig &config);
bool IsValidMainRAMRange(melonDS::NDS *nds, melonDS::u32 address,
                         melonDS::u32 length);
bool ReadMainRAMAddressU32(melonDS::NDS *nds, melonDS::u32 address,
                           melonDS::u32 &value);
void InvalidateMainRAMJIT(const Config::RollbackConfig &config,
                          melonDS::NDS *nds, melonDS::u32 length);

bool ResolveRemoteInputLocked(Context context, melonDS::u32 frame,
                              InputState &input, bool &predicted);
void SaveCheckpointIfNeeded(Context context, int instanceID,
                            melonDS::u32 frame, melonDS::NDS *nds);
bool RestoreCheckpointForProbeIfNeeded(Context context, int instanceID,
                                       melonDS::u32 frame, melonDS::NDS *nds);
bool ResimulateIfNeeded(Context context, const ResimulationHooks &hooks,
                        int instanceID, melonDS::u32 frame, melonDS::NDS *nds);
void TraceStatsIfNeeded(Context context, melonDS::u32 frame);

} // namespace NsmbNetplayPoC::RollbackRuntime

#endif
