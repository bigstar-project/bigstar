#pragma once

#include "NsmbNetplayConfig.h"
#include "NsmbNetplayCoordinator.h"
#include "types.h"

#include <mutex>

namespace melonDS {
class NDS;
}

namespace NsmbNetplayPoC::TestStateHarness {

struct Context {
  const Config::HarnessConfig &Harness;
  const Config::BootstrapConfig &Bootstrap;
  Coordination::Runtime &Coordinator;
  std::mutex &Mutex;
};

bool SaveState(Context context, int instanceID, melonDS::u32 frame,
               melonDS::NDS *nds);
bool SaveLocalMPState(Context context, melonDS::u32 frame);
bool LoadState(Context context, int instanceID, melonDS::u32 frame,
               melonDS::NDS *nds);

} // namespace NsmbNetplayPoC::TestStateHarness
