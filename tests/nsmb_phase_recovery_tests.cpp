#include "NsmbPhaseRecovery.h"

#include <chrono>
#include <cmath>
#include <cstdio>

namespace
{

int Failures = 0;

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n",               \
                __FILE__, __LINE__, #condition);                                \
            Failures++;                                                         \
        }                                                                       \
    } while (false)

using Runtime = NsmbMvlNetplay::PhaseRecovery::Runtime;
using Clock = Runtime::Clock;

Clock::time_point AtUs(long long value)
{
    return Clock::time_point(std::chrono::microseconds(value));
}

void PrimeRtt(Runtime& runtime)
{
    for (melonDS::u32 frame = 1; frame <= 3; frame++)
    {
        const long long sentUs = static_cast<long long>(frame) * 200'000;
        runtime.RecordLocalInput(4, frame, AtUs(sentUs));
        runtime.RecordRemoteAck(frame, AtUs(sentUs + 100'000));
    }
}

void AddOffsetSamples(
    Runtime& runtime,
    melonDS::u32 firstLocalFrame,
    long long offsetUs,
    int count)
{
    constexpr long long frameUs = 1'000'000 / 60;
    for (int index = 0; index < count; index++)
    {
        const melonDS::u32 localFrame = firstLocalFrame
            + static_cast<melonDS::u32>(index);
        const melonDS::u32 remoteFrame = localFrame + 12;
        const long long sentUs = 1'000'000
            + static_cast<long long>(index) * frameUs;
        runtime.RecordLocalInput(4, localFrame, AtUs(sentUs));
        const long long arrivalUs = sentUs + 50'000
            + 12 * frameUs + offsetUs;
        runtime.ObserveRemoteInput(4, remoteFrame, AtUs(arrivalUs));
    }
}

void TestRequiresRttBeforeCorrecting()
{
    Runtime runtime;
    runtime.Configure(true);
    for (melonDS::u32 frame = 120; frame < 135; frame++)
    {
        runtime.RecordLocalInput(1, frame, AtUs(frame * 20'000LL));
        runtime.ObserveRemoteInput(1, frame - 7, AtUs(frame * 20'000LL + 50'000));
    }
    const auto decision = runtime.Consume(135, AtUs(2'710'000));
    CHECK(decision.SpeedRatio == 1.0);
    CHECK(!decision.SkipFrameLimit);
    CHECK(decision.OffsetSamples == 0);
}

void TestAlignedClocksRemainNeutral()
{
    Runtime runtime;
    runtime.Configure(true);
    PrimeRtt(runtime);
    AddOffsetSamples(runtime, 120, 0, 10);
    const auto decision = runtime.Consume(130, AtUs(1'500'000));
    CHECK(decision.Evaluated);
    CHECK(std::abs(decision.OffsetUs) <= 2);
    CHECK(std::abs(decision.SpeedRatio - 1.0) < 0.000001);
    CHECK(!decision.SkipFrameLimit);
}

void TestLeaderSlowsWithinSlippiLimit()
{
    Runtime runtime;
    runtime.Configure(true);
    PrimeRtt(runtime);
    AddOffsetSamples(runtime, 120, 300'000, 10);
    const auto decision = runtime.Consume(130, AtUs(1'800'000));
    CHECK(decision.Evaluated);
    CHECK(decision.OffsetUs == 300'000);
    CHECK(decision.SpeedRatio == 0.995);
    CHECK(!decision.SkipFrameLimit);
}

void TestLaggerSpeedsAndDistributesAdvanceFrames()
{
    Runtime runtime;
    runtime.Configure(true);
    PrimeRtt(runtime);
    AddOffsetSamples(runtime, 120, -120'000, 10);

    auto decision = runtime.Consume(130, AtUs(1'500'000));
    CHECK(decision.Evaluated);
    CHECK(decision.SpeedRatio > 1.0);
    CHECK(decision.SpeedRatio <= 1.01);
    CHECK(decision.SkipFrameLimit);
    CHECK(decision.PendingAdvanceFrames == 2);

    decision = runtime.Consume(134, AtUs(1'510'000));
    CHECK(!decision.SkipFrameLimit);
    decision = runtime.Consume(135, AtUs(1'520'000));
    CHECK(decision.SkipFrameLimit);
    decision = runtime.Consume(140, AtUs(1'530'000));
    CHECK(decision.SkipFrameLimit);
    CHECK(decision.TotalAdvanceFrames == 3);
}

void TestStaleObservationsDisablePacing()
{
    Runtime runtime;
    runtime.Configure(true);
    PrimeRtt(runtime);
    AddOffsetSamples(runtime, 120, -120'000, 10);
    (void)runtime.Consume(130, AtUs(1'500'000));
    const auto stale = runtime.Consume(160, AtUs(2'000'000));
    CHECK(stale.SpeedRatio == 1.0);
    CHECK(!stale.SkipFrameLimit);
    CHECK(stale.PendingAdvanceFrames == 0);
}

void TestGenerationChangeResetsSamples()
{
    Runtime runtime;
    runtime.Configure(true);
    PrimeRtt(runtime);
    AddOffsetSamples(runtime, 120, -120'000, 10);
    CHECK(runtime.Snapshot().OffsetSamples == 10);
    runtime.RecordLocalInput(5, 1, AtUs(2'000'000));
    const auto reset = runtime.Snapshot();
    CHECK(reset.OffsetSamples == 0);
    CHECK(reset.RttSamples == 0);
    CHECK(reset.TotalAdvanceFrames == 0);
}

}

int main()
{
    TestRequiresRttBeforeCorrecting();
    TestAlignedClocksRemainNeutral();
    TestLeaderSlowsWithinSlippiLimit();
    TestLaggerSpeedsAndDistributesAdvanceFrames();
    TestStaleObservationsDisablePacing();
    TestGenerationChangeResetsSamples();

    if (Failures != 0)
    {
        std::fprintf(stderr, "nsmb phase recovery tests failed: %d\n", Failures);
        return 1;
    }
    std::printf("nsmb phase recovery tests passed\n");
    return 0;
}
