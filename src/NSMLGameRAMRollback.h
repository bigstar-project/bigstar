#ifndef NSML_GAME_RAM_ROLLBACK_H
#define NSML_GAME_RAM_ROLLBACK_H

#include "types.h"

namespace melonDS::NSMLGameRAMRollback
{

constexpr bool CanFinalizeTransaction(
    bool restorePending,
    bool historyReachedExitGate,
    bool historyEnabled,
    u32 historyIndex,
    u32 historyCount,
    u32 gameFrame,
    u32 historyStartFrame)
{
    if (restorePending || historyCount == 0)
        return false;
    if (!historyReachedExitGate && !historyEnabled)
        return false;
    if (historyIndex < historyCount || gameFrame < historyStartFrame)
        return false;
    return gameFrame - historyStartFrame >= historyCount;
}

} // namespace melonDS::NSMLGameRAMRollback

#endif
