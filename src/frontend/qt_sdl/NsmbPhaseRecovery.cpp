#include "NsmbPhaseRecovery.h"

#include <algorithm>
#include <cmath>

namespace NsmbMvlNetplay::PhaseRecovery
{

namespace
{

constexpr long long kFrameDurationUs = 1'000'000 / 60;
constexpr std::size_t kSampleWindow = 30;
constexpr std::size_t kMinimumSamples = 10;
constexpr melonDS::u32 kEvaluationIntervalFrames = 30;
constexpr melonDS::u32 kRecoveryStartFrame = 120;
constexpr melonDS::u32 kAdvanceSpacingFrames = 5;
constexpr int kMaxAdvanceFrames = 3;
constexpr long long kAdvanceThresholdUs = 26'683;
constexpr long long kCorrectionPeriodUs = 30'000'000;
constexpr long long kMaximumRttUs = 10'000'000;
constexpr auto kObservationFreshness = std::chrono::milliseconds(500);
constexpr melonDS::u32 kSendTimestampHistoryFrames = 1024;

}

void Runtime::Configure(bool enabled)
{
    if (Enabled_ == enabled)
        return;
    Reset();
    Enabled_ = enabled;
}

void Runtime::Reset()
{
    Generation_.reset();
    LastLocalFrame_.reset();
    LastObservedRemoteFrame_.reset();
    LastRemoteAckFrame_.reset();
    LastEvaluationFrame_.reset();
    LastAdvanceFrame_.reset();
    LastLocalSendAt_ = {};
    LastRemoteObservationAt_ = {};
    LocalSendTimes_.clear();
    OffsetSamples_.clear();
    RttSamples_.clear();
    SpeedRatio_ = 1.0;
    OffsetUs_ = 0;
    RttUs_ = 0;
    PendingAdvanceFrames_ = 0;
    TotalAdvanceFrames_ = 0;
}

void Runtime::EnsureGeneration(melonDS::u32 generation)
{
    if (Generation_ && *Generation_ == generation)
        return;
    Reset();
    Generation_ = generation;
}

void Runtime::RecordLocalInput(
    melonDS::u32 generation,
    melonDS::u32 frame,
    Clock::time_point now)
{
    if (!Enabled_)
        return;
    EnsureGeneration(generation);
    LastLocalFrame_ = frame;
    LastLocalSendAt_ = now;
    LocalSendTimes_.try_emplace(frame, now);

    while (!LocalSendTimes_.empty()
        && LocalSendTimes_.begin()->first < frame
        && frame - LocalSendTimes_.begin()->first > kSendTimestampHistoryFrames)
    {
        LocalSendTimes_.erase(LocalSendTimes_.begin());
    }
}

void Runtime::RecordRemoteAck(melonDS::u32 ackFrame, Clock::time_point now)
{
    if (!Enabled_ || (LastRemoteAckFrame_ && ackFrame <= *LastRemoteAckFrame_))
        return;
    LastRemoteAckFrame_ = ackFrame;

    const auto sent = LocalSendTimes_.find(ackFrame);
    if (sent == LocalSendTimes_.end() || now <= sent->second)
        return;
    const long long rttUs = std::chrono::duration_cast<std::chrono::microseconds>(
        now - sent->second).count();
    if (rttUs <= 0 || rttUs > kMaximumRttUs)
        return;
    PushSample(RttSamples_, rttUs, kSampleWindow);
    RttUs_ = RobustAverage(RttSamples_);
}

void Runtime::ObserveRemoteInput(
    melonDS::u32 generation,
    melonDS::u32 frame,
    Clock::time_point now)
{
    if (!Enabled_)
        return;
    EnsureGeneration(generation);
    if (!LastLocalFrame_ || RttSamples_.size() < 3
        || (LastObservedRemoteFrame_ && frame <= *LastObservedRemoteFrame_))
    {
        return;
    }

    const long long elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
        now - LastLocalSendAt_).count();
    const long long frameDelta = static_cast<long long>(*LastLocalFrame_)
        - static_cast<long long>(frame);
    const long long offsetUs = elapsedUs - (RttUs_ / 2)
        + frameDelta * kFrameDurationUs;
    PushSample(OffsetSamples_, offsetUs, kSampleWindow);
    LastObservedRemoteFrame_ = frame;
    LastRemoteObservationAt_ = now;
}

void Runtime::Evaluate(melonDS::u32 localFrame)
{
    OffsetUs_ = RobustAverage(OffsetSamples_);
    const double correction = static_cast<double>(OffsetUs_)
        / static_cast<double>(kCorrectionPeriodUs);
    SpeedRatio_ = std::clamp(1.0 - correction, 0.995, 1.01);

    if (localFrame < kRecoveryStartFrame || OffsetUs_ >= -kAdvanceThresholdUs)
    {
        PendingAdvanceFrames_ = 0;
        return;
    }

    const int needed = std::max(
        1,
        static_cast<int>(std::llround(
            static_cast<double>(-OffsetUs_)
            / static_cast<double>(kFrameDurationUs))));
    PendingAdvanceFrames_ = std::max(
        PendingAdvanceFrames_, std::min(kMaxAdvanceFrames, needed));
}

PacingDecision Runtime::Consume(melonDS::u32 localFrame, Clock::time_point now)
{
    PacingDecision decision = Snapshot();
    if (!Enabled_)
        return decision;

    if (LastRemoteObservationAt_ == Clock::time_point {}
        || now - LastRemoteObservationAt_ > kObservationFreshness)
    {
        SpeedRatio_ = 1.0;
        PendingAdvanceFrames_ = 0;
        return Snapshot();
    }

    if (OffsetSamples_.size() >= kMinimumSamples
        && (!LastEvaluationFrame_
            || localFrame - *LastEvaluationFrame_ >= kEvaluationIntervalFrames))
    {
        Evaluate(localFrame);
        LastEvaluationFrame_ = localFrame;
        decision.Evaluated = true;
    }

    bool skipFrameLimit = false;
    if (PendingAdvanceFrames_ > 0
        && (!LastAdvanceFrame_
            || localFrame - *LastAdvanceFrame_ >= kAdvanceSpacingFrames))
    {
        PendingAdvanceFrames_--;
        LastAdvanceFrame_ = localFrame;
        TotalAdvanceFrames_++;
        skipFrameLimit = true;
    }

    PacingDecision current = Snapshot();
    current.Evaluated = decision.Evaluated;
    current.SkipFrameLimit = skipFrameLimit;
    return current;
}

PacingDecision Runtime::Snapshot() const
{
    PacingDecision decision;
    decision.SpeedRatio = SpeedRatio_;
    decision.OffsetUs = OffsetUs_;
    decision.RttUs = RttUs_;
    decision.PendingAdvanceFrames = PendingAdvanceFrames_;
    decision.TotalAdvanceFrames = TotalAdvanceFrames_;
    decision.OffsetSamples = OffsetSamples_.size();
    decision.RttSamples = RttSamples_.size();
    return decision;
}

long long Runtime::RobustAverage(std::vector<long long> samples)
{
    if (samples.empty())
        return 0;
    std::sort(samples.begin(), samples.end());
    std::size_t first = 0;
    std::size_t last = samples.size();
    if (samples.size() >= 3)
    {
        const std::size_t third = samples.size() / 3;
        first = third;
        last = samples.size() - third;
    }
    long long sum = 0;
    for (std::size_t index = first; index < last; index++)
        sum += samples[index];
    return sum / static_cast<long long>(last - first);
}

void Runtime::PushSample(
    std::vector<long long>& samples,
    long long value,
    std::size_t capacity)
{
    if (samples.size() == capacity)
        samples.erase(samples.begin());
    samples.push_back(value);
}

}
