#ifndef NSMB_PHASE_RECOVERY_H
#define NSMB_PHASE_RECOVERY_H

#include "types.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace NsmbMvlNetplay::PhaseRecovery
{

struct PacingDecision
{
    double SpeedRatio = 1.0;
    bool SkipFrameLimit = false;
    bool Evaluated = false;
    long long OffsetUs = 0;
    long long RttUs = 0;
    int PendingAdvanceFrames = 0;
    unsigned long long TotalAdvanceFrames = 0;
    std::size_t OffsetSamples = 0;
    std::size_t RttSamples = 0;
};

class Runtime
{
public:
    using Clock = std::chrono::steady_clock;

    void Configure(bool enabled);
    void Reset();
    void RecordLocalInput(
        melonDS::u32 generation,
        melonDS::u32 frame,
        Clock::time_point now);
    void RecordRemoteAck(melonDS::u32 ackFrame, Clock::time_point now);
    void ObserveRemoteInput(
        melonDS::u32 generation,
        melonDS::u32 frame,
        Clock::time_point now);
    PacingDecision Consume(melonDS::u32 localFrame, Clock::time_point now);
    PacingDecision Snapshot() const;

private:
    void EnsureGeneration(melonDS::u32 generation);
    void Evaluate(melonDS::u32 localFrame);
    static long long RobustAverage(std::vector<long long> samples);
    static void PushSample(
        std::vector<long long>& samples,
        long long value,
        std::size_t capacity);

    bool Enabled_ = false;
    std::optional<melonDS::u32> Generation_;
    std::optional<melonDS::u32> LastLocalFrame_;
    std::optional<melonDS::u32> LastObservedRemoteFrame_;
    std::optional<melonDS::u32> LastRemoteAckFrame_;
    std::optional<melonDS::u32> LastEvaluationFrame_;
    std::optional<melonDS::u32> LastAdvanceFrame_;
    Clock::time_point LastLocalSendAt_ {};
    Clock::time_point LastRemoteObservationAt_ {};
    std::map<melonDS::u32, Clock::time_point> LocalSendTimes_;
    std::vector<long long> OffsetSamples_;
    std::vector<long long> RttSamples_;
    double SpeedRatio_ = 1.0;
    long long OffsetUs_ = 0;
    long long RttUs_ = 0;
    int PendingAdvanceFrames_ = 0;
    unsigned long long TotalAdvanceFrames_ = 0;
};

}

#endif
