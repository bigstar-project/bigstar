#include "NsmbInputDelivery.h"

#include <chrono>
#include <cstdio>
#include <map>

namespace
{

int Failures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::fprintf(stderr, "line %d: CHECK failed: %s\n", line, expression);
    Failures++;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

using NsmbNetplayPoC::InputDelivery::DecideSend;
using NsmbNetplayPoC::InputDelivery::SendConfig;

void TestDefaultDecision()
{
    const auto decision = DecideSend(120, {});
    CHECK(!decision.Drop);
    CHECK(!decision.Bundle);
    CHECK(decision.BundleHistory == 0);
    CHECK(decision.DelayFrames == 0);
}

void TestDropModuloAndRangeBoundaries()
{
    SendConfig config;
    config.DropModulo = 4;
    config.DropOffset = 1;
    CHECK(!DecideSend(4, config).Drop);
    CHECK(DecideSend(5, config).Drop);
    CHECK(!DecideSend(6, config).Drop);

    config = {};
    config.DropStartFrame = 10;
    config.DropEndFrame = 12;
    CHECK(!DecideSend(9, config).Drop);
    CHECK(DecideSend(10, config).Drop);
    CHECK(DecideSend(12, config).Drop);
    CHECK(!DecideSend(13, config).Drop);

    config.DropEndFrame = 0;
    CHECK(DecideSend(100000, config).Drop);
    config.DropStartFrame = 0;
    CHECK(!DecideSend(100000, config).Drop);
}

void TestBundleAndDelayBoundaries()
{
    SendConfig config;
    config.UseHistoryBundle = true;
    config.BundleHistory = 8;
    config.DelayFrames = 3;
    config.JitterFrames = 2;
    config.DelayStartFrame = 100;
    config.DelayEndFrame = 102;

    CHECK(DecideSend(99, config).Bundle);
    CHECK(DecideSend(99, config).DelayFrames == 0);
    CHECK(DecideSend(100, config).DelayFrames == 4);
    CHECK(DecideSend(101, config).DelayFrames == 5);
    CHECK(DecideSend(102, config).DelayFrames == 3);
    CHECK(DecideSend(103, config).DelayFrames == 0);
    CHECK(DecideSend(100, config).Bundle);
    CHECK(DecideSend(100, config).BundleHistory == 8);

    config.BundleHistory = 100;
    CHECK(DecideSend(100, config).BundleHistory == 31);
    config.BundleHistory = 0;
    CHECK(!DecideSend(100, config).Bundle);
}

void TestBundleInputSelection()
{
    NsmbNetplayPoC::InputState current;
    current.KeyMask = 0xAAA;
    std::map<melonDS::u32, NsmbNetplayPoC::InputState> localInputs;
    localInputs[0].KeyMask = 0xFFE;
    localInputs[2].KeyMask = 0xFDF;

    const auto entries = NsmbNetplayPoC::InputDelivery::SelectBundleInputs(
        2,
        current,
        31,
        localInputs);
    CHECK(entries.size() == 3);
    CHECK(entries[0].Frame == 0);
    CHECK(entries[0].Input.KeyMask == 0xFFE);
    CHECK(entries[1].Frame == 1);
    CHECK(entries[1].Input.KeyMask == 0xAAA);
    CHECK(entries[2].Frame == 2);
    CHECK(entries[2].Input.KeyMask == 0xFDF);
}

void TestDelayedInputReleaseUsesFrameOrWallClock()
{
    const auto now = std::chrono::steady_clock::time_point(std::chrono::seconds(10));
    const auto releaseTime = now + std::chrono::milliseconds(50);
    CHECK(!NsmbNetplayPoC::InputDelivery::ShouldReleaseDelayedInput(
        9, now, 10, releaseTime));
    CHECK(NsmbNetplayPoC::InputDelivery::ShouldReleaseDelayedInput(
        10, now, 10, releaseTime));
    CHECK(NsmbNetplayPoC::InputDelivery::ShouldReleaseDelayedInput(
        9, releaseTime, 10, releaseTime));
}

}

int main()
{
    TestDefaultDecision();
    TestDropModuloAndRangeBoundaries();
    TestBundleAndDelayBoundaries();
    TestBundleInputSelection();
    TestDelayedInputReleaseUsesFrameOrWallClock();

    if (Failures != 0)
    {
        std::fprintf(stderr, "nsmb input delivery tests failed: %d\n", Failures);
        return 1;
    }
    std::printf("nsmb input delivery tests passed\n");
    return 0;
}
