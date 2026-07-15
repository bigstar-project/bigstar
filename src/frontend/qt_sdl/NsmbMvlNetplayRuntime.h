/*
    Experimental NSMB Mario vs Luigi netplay runtime.

    This is intentionally isolated from melonDS' regular LAN/local MP paths.
    It only overrides per-frame controller input when explicitly enabled.
*/

#ifndef NSMBMVLNETPLAYRUNTIME_H
#define NSMBMVLNETPLAYRUNTIME_H

#include "types.h"

namespace melonDS
{
class NDS;
}

namespace NsmbMvlNetplay
{

struct InputState
{
    melonDS::u32 KeyMask = 0xFFF;
    bool Touching = false;
    melonDS::u16 TouchX = 0;
    melonDS::u16 TouchY = 0;
};

struct PerformanceCounters
{
    unsigned long long RemoteInputWaitCount = 0;
    unsigned long long RemoteInputWaitUs = 0;
    unsigned long long RemoteInputWaitMaxUs = 0;
    unsigned long long FrameLeadThrottleCount = 0;
    unsigned long long FrameLeadThrottleUs = 0;
    unsigned long long FrameLeadThrottleMaxUs = 0;
    melonDS::u32 LastSentInputFrame = 0;
    melonDS::u32 LastReceivedInputFrame = 0;
    int InputLead = 0;
};

bool IsEnabled();
PerformanceCounters GetPerformanceCounters();
void InitFromEnvironment();
InputState BeforeRunFrame(int instanceID, melonDS::u32 frame, melonDS::NDS* nds, const InputState& polledInput);
void AfterRunFrame(int instanceID, melonDS::u32 frame, melonDS::NDS* nds);
bool ShouldQuitAfterFrame(int instanceID, melonDS::u32 frame);
void Shutdown();

}

#endif
