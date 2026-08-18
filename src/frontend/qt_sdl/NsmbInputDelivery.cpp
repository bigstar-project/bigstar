#include "NsmbInputDelivery.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace NsmbMvlNetplay::InputDelivery
{

SendDecision DecideSend(melonDS::u32 frame, const SendConfig& config)
{
    SendDecision decision;

    const bool dropByModulo = config.DropModulo > 0
        && (frame % static_cast<melonDS::u32>(config.DropModulo))
            == static_cast<melonDS::u32>(config.DropOffset);
    const bool dropByRange = config.DropStartFrame > 0
        && frame >= config.DropStartFrame
        && (config.DropEndFrame == 0 || frame <= config.DropEndFrame);
    decision.Drop = dropByModulo || dropByRange;

    decision.BundleHistory = std::clamp(config.BundleHistory, 0, 31);
    decision.Bundle = config.UseHistoryBundle && decision.BundleHistory > 0;

    const bool delayActive = frame >= config.DelayStartFrame
        && (config.DelayEndFrame == 0 || frame <= config.DelayEndFrame);
    const int jitterFrames = delayActive && config.JitterFrames > 0
        ? static_cast<int>(frame % static_cast<melonDS::u32>(config.JitterFrames + 1))
        : 0;
    decision.DelayFrames = delayActive
        ? std::max(0, config.DelayFrames) + jitterFrames
        : 0;

    return decision;
}

std::vector<InputProtocol::FramedInput> SelectBundleInputs(
    melonDS::u32 generation,
    melonDS::u32 frame,
    const InputState& currentInput,
    int history,
    const std::map<melonDS::u32, InputState>& localInputs)
{
    history = std::clamp(history, 0, 31);
    std::vector<InputProtocol::FramedInput> entries;
    entries.reserve(static_cast<std::size_t>(history + 1));
    for (int offset = history; offset >= 0; offset--)
    {
        if (static_cast<melonDS::u32>(offset) > frame)
            continue;

        const melonDS::u32 entryFrame = frame - static_cast<melonDS::u32>(offset);
        InputState entryInput = currentInput;
        const auto existing = localInputs.find(entryFrame);
        if (existing != localInputs.end())
            entryInput = existing->second;
        entries.push_back({ generation, entryFrame, entryInput });
    }
    return entries;
}

std::vector<InputProtocol::FramedInput> SelectUnackedInputs(
    melonDS::u32 generation,
    melonDS::u32 frame,
    const InputState& currentInput,
    int maxEntries,
    const std::map<melonDS::u32, InputState>& localInputs,
    std::optional<melonDS::u32> remoteAckFrame)
{
    maxEntries = std::clamp(
        maxEntries, 1,
        static_cast<int>(InputProtocol::kMaxInputBundleEntries));
    melonDS::u32 firstFrame = frame;
    if (remoteAckFrame && *remoteAckFrame < frame)
        firstFrame = *remoteAckFrame + 1;
    else if (!remoteAckFrame && !localInputs.empty())
        firstFrame = std::min(frame, localInputs.begin()->first);

    if (!localInputs.empty())
        firstFrame = std::max(firstFrame, localInputs.begin()->first);
    if (firstFrame > frame)
        firstFrame = frame;

    std::vector<InputProtocol::FramedInput> entries;
    entries.reserve(static_cast<std::size_t>(maxEntries));
    for (melonDS::u32 entryFrame = firstFrame;
         entryFrame <= frame
             && entries.size() < static_cast<std::size_t>(maxEntries);
         entryFrame++)
    {
        InputState entryInput = currentInput;
        const auto existing = localInputs.find(entryFrame);
        if (existing != localInputs.end())
            entryInput = existing->second;
        entries.push_back({ generation, entryFrame, entryInput });
        if (entryFrame == std::numeric_limits<melonDS::u32>::max())
            break;
    }
    return entries;
}

bool ShouldReleaseDelayedInput(
    melonDS::u32 currentFrame,
    std::chrono::steady_clock::time_point now,
    melonDS::u32 releaseFrame,
    std::chrono::steady_clock::time_point releaseTime)
{
    return releaseFrame <= currentFrame || now >= releaseTime;
}

melonDS::u32 SelectDelayProgressFrame(
    melonDS::u32 lastSentInputFrame,
    melonDS::u32 fallbackRawFrame,
    melonDS::u32 noFrame)
{
    return lastSentInputFrame == noFrame
        ? fallbackRawFrame
        : lastSentInputFrame;
}

PreparedSend Runtime::Prepare(
    melonDS::u32 generation,
    melonDS::u32 frame,
    const InputState& input,
    const SendConfig& config,
    const std::map<melonDS::u32, InputState>& localInputs,
    Clock::time_point now,
    std::optional<melonDS::u32> ackFrameToSend)
{
    PreparedSend prepared;
    prepared.Decision = DecideSend(frame, config);
    if (prepared.Decision.Drop)
        return prepared;

    std::vector<char> payload = prepared.Decision.Bundle
        ? BuildPayload(
            generation, frame, input, prepared.Decision.BundleHistory,
            localInputs, ackFrameToSend)
        : InputProtocol::EncodeInput({ generation, frame, input });
    if (prepared.Decision.DelayFrames <= 0)
    {
        prepared.ImmediatePayload = std::move(payload);
        return prepared;
    }

    const int delayFrames = prepared.Decision.DelayFrames;
    Pending_.push_back({
        frame + static_cast<melonDS::u32>(delayFrames),
        now + std::chrono::milliseconds((delayFrames * 1000 + 59) / 60),
        std::move(payload),
    });
    return prepared;
}

void Runtime::DrainDue(
    melonDS::u32 currentFrame,
    Clock::time_point now,
    const std::function<void(const std::vector<char>&)>& send)
{
    for (auto pending = Pending_.begin(); pending != Pending_.end();)
    {
        if (ShouldReleaseDelayedInput(
                currentFrame,
                now,
                pending->ReleaseFrame,
                pending->ReleaseTime))
        {
            send(pending->Payload);
            pending = Pending_.erase(pending);
        }
        else
        {
            ++pending;
        }
    }
}

void Runtime::DrainAll(
    const std::function<void(const std::vector<char>&)>& send)
{
    for (const PendingPayload& pending : Pending_)
        send(pending.Payload);
    Pending_.clear();
}

std::vector<char> Runtime::BuildPayload(
    melonDS::u32 generation,
    melonDS::u32 frame,
    const InputState& input,
    int bundleHistory,
    const std::map<melonDS::u32, InputState>& localInputs,
    std::optional<melonDS::u32> ackFrameToSend) const
{
    if (bundleHistory <= 0)
        return InputProtocol::EncodeInput({ generation, frame, input });
    // The correction history capacity and the network resend window are
    // different contracts.  An ACK needs a full round trip, so a 12-entry
    // correction history is too small for 8-9 frames of one-way delay.  The
    // wire limit remains below the transport MTU while covering 32 unacked
    // input frames.
    return InputProtocol::EncodeInputBundle(
        SelectUnackedInputs(
            generation, frame, input,
            static_cast<int>(InputProtocol::kMaxInputBundleEntries), localInputs,
            RemoteAckFrame_),
        ackFrameToSend);
}

AckUpdate Runtime::RecordRemoteAck(
    melonDS::u32 ackFrame,
    melonDS::u32 lastSentInputFrame,
    melonDS::u32 noFrame)
{
    if (lastSentInputFrame == noFrame || ackFrame > lastSentInputFrame)
        return AckUpdate::Future;
    if (RemoteAckFrame_ && ackFrame <= *RemoteAckFrame_)
        return AckUpdate::Stale;
    RemoteAckFrame_ = ackFrame;
    return AckUpdate::Advanced;
}

std::optional<melonDS::u32> Runtime::RemoteAckFrame() const
{
    return RemoteAckFrame_;
}

void Runtime::Clear()
{
    Pending_.clear();
    RemoteAckFrame_.reset();
}

std::size_t Runtime::PendingCount() const
{
    return Pending_.size();
}

}
