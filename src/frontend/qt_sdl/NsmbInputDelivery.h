#ifndef NSMBINPUTDELIVERY_H
#define NSMBINPUTDELIVERY_H

#include "NsmbInputProtocol.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <vector>

namespace NsmbMvlNetplay::InputDelivery
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
    melonDS::u32 generation,
    melonDS::u32 frame,
    const InputState& currentInput,
    int history,
    const std::map<melonDS::u32, InputState>& localInputs);
bool ShouldReleaseDelayedInput(
    melonDS::u32 currentFrame,
    std::chrono::steady_clock::time_point now,
    melonDS::u32 releaseFrame,
    std::chrono::steady_clock::time_point releaseTime);
melonDS::u32 SelectDelayProgressFrame(
    melonDS::u32 lastSentInputFrame,
    melonDS::u32 fallbackRawFrame,
    melonDS::u32 noFrame);

struct PreparedSend
{
    SendDecision Decision;
    std::vector<char> ImmediatePayload;
};

class Runtime
{
public:
    using Clock = std::chrono::steady_clock;

    PreparedSend Prepare(
        melonDS::u32 generation,
        melonDS::u32 frame,
        const InputState& input,
        const SendConfig& config,
        const std::map<melonDS::u32, InputState>& localInputs,
        Clock::time_point now);
    void DrainDue(
        melonDS::u32 currentFrame,
        Clock::time_point now,
        const std::function<void(const std::vector<char>&)>& send);
    void DrainAll(
        const std::function<void(const std::vector<char>&)>& send);
    std::vector<char> BuildPayload(
        melonDS::u32 generation,
        melonDS::u32 frame,
        const InputState& input,
        int bundleHistory,
        const std::map<melonDS::u32, InputState>& localInputs) const;
    void Clear();
    std::size_t PendingCount() const;

private:
    struct PendingPayload
    {
        melonDS::u32 ReleaseFrame = 0;
        Clock::time_point ReleaseTime {};
        std::vector<char> Payload;
    };

    std::vector<PendingPayload> Pending_;
};

}

#endif
