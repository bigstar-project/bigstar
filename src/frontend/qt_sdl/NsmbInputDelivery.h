#ifndef NSMBINPUTDELIVERY_H
#define NSMBINPUTDELIVERY_H

#include "NsmbInputProtocol.h"

#include <chrono>
#include <map>
#include <vector>

namespace NsmbNetplayPoC::InputDelivery
{

struct SendConfig
{
    bool UseHistoryBundle = false;
    int BundleHistory = 0;
    int DropModulo = 0;
    int DropOffset = 0;
    melonDS::u32 DropStartFrame = 0;
    melonDS::u32 DropEndFrame = 0;
    int DelayFrames = 0;
    int JitterFrames = 0;
    melonDS::u32 DelayStartFrame = 0;
    melonDS::u32 DelayEndFrame = 0;
};

struct SendDecision
{
    bool Drop = false;
    bool Bundle = false;
    int BundleHistory = 0;
    int DelayFrames = 0;
};

SendDecision DecideSend(melonDS::u32 frame, const SendConfig& config);
std::vector<InputProtocol::FramedInput> SelectBundleInputs(
    melonDS::u32 frame,
    const InputState& currentInput,
    int history,
    const std::map<melonDS::u32, InputState>& localInputs);
bool ShouldReleaseDelayedInput(
    melonDS::u32 currentFrame,
    std::chrono::steady_clock::time_point now,
    melonDS::u32 releaseFrame,
    std::chrono::steady_clock::time_point releaseTime);

}

#endif
