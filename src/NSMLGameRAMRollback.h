#ifndef NSML_GAME_RAM_ROLLBACK_H
#define NSML_GAME_RAM_ROLLBACK_H

#include "types.h"

#include <cstdlib>

namespace melonDS::NSMLGameRAMRollback
{

// Core notification before a normal game-loop checkpoint, outside replay history.
constexpr u32 NormalInputGate = 0x102;
constexpr u32 LoopStartAddress = 0x02004EC8;

inline bool HasJitInputGate()
{
#if defined(JIT_ENABLED) && defined(__x86_64__)
    static const bool enabled = [] {
        const char* value = std::getenv("MELONDS_NSML_ROM_GAME_TICK_PROBE_GAME_RAM_ROLLBACK");
        return value && *value && *value != '0';
    }();
    return enabled;
#else
    return false;
#endif
}

constexpr u32 RequiredHistoryCount(u32 restoreFrame, u32 currentFrame)
{
    // A checkpoint precedes the input/update of restoreFrame. Replaying the
    // inclusive [restoreFrame, currentFrame] interval replaces the current
    // outer frame's normal game tick. Executing currentFrame + 1 as well would
    // advance the game once more on every correction, while input/checkpoint
    // labels still advance by one outer frame. The next normal gate captures
    // the checkpoint for currentFrame + 1; it must not be replayed in advance.
    if (currentFrame < restoreFrame || currentFrame == 0xFFFFFFFFu)
        return 0;
    const u32 rollbackDepth = currentFrame - restoreFrame;
    // Keep the no-frame sentinel out of both the frame and count domains.
    if (rollbackDepth > 0xFFFFFFFDu)
        return 0;
    return rollbackDepth + 1;
}

constexpr u32 MaxRollbackDepthForHistory(u32 historyCapacity)
{
    return historyCapacity != 0 ? historyCapacity - 1 : 0;
}

class CheckpointFrameTimeline
{
public:
    void Reset()
    {
        Active = false;
        LogicalFrame = 0;
    }

    bool SetLogicalFrame(u32 frame)
    {
        const bool invalidatesCheckpoints = !Active || frame < LogicalFrame;
        Active = true;
        LogicalFrame = frame;
        return invalidatesCheckpoints;
    }

    u32 CaptureFrame(u32 rawFrame) const
    {
        return Active ? LogicalFrame : rawFrame;
    }

private:
    bool Active = false;
    u32 LogicalFrame = 0;
};

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
