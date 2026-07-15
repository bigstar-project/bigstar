#ifndef NSMB_MVL_GAME_HOOKS_H
#define NSMB_MVL_GAME_HOOKS_H

#include "NsmbNetplayConfig.h"
#include "types.h"

namespace melonDS {
class NDS;
}

namespace NsmbMvlNetplay {
namespace Diagnostics {
class Runtime;
}
namespace MvlRuntime {
class Runtime;
}

namespace MvlGameHooks {

struct Context {
  const Config::MvlConfig &Mvl;
  const Config::RuntimePatchConfig &Patches;
  Diagnostics::Runtime &Diagnostics;
  MvlRuntime::Runtime &Runtime;
  int CurrentStage = 0;
  melonDS::u32 CurrentStageSceneSettings = 0;
  bool IsHost = false;
};

void ApplyRuntimeConfig(const Context &context, melonDS::NDS *nds);
void ApplyPlayerStickToStar(const Context &context, int instanceID,
                            melonDS::u32 frame, melonDS::NDS *nds);
void ForcePlayerDeathCounters(const Context &context, int instanceID,
                              melonDS::u32 frame, melonDS::NDS *nds);
void ForcePlayerInventoryPowerups(const Context &context, int instanceID,
                                  melonDS::u32 frame, melonDS::NDS *nds);
void ForcePlayerPowerups(const Context &context, int instanceID,
                         melonDS::u32 frame, melonDS::NDS *nds);
void ForcePlayerStarCounters(const Context &context, int instanceID,
                             melonDS::u32 frame, melonDS::NDS *nds);
void ClearCameraInitHold(const Context &context, int instanceID,
                         melonDS::u32 frame, melonDS::NDS *nds);
bool WriteRandomSeed(melonDS::NDS *nds, melonDS::u32 seed);
void ApplyNetRandomPatch(const Context &context, int instanceID,
                         melonDS::u32 frame, melonDS::NDS *nds);

} // namespace MvlGameHooks
} // namespace NsmbMvlNetplay

#endif
