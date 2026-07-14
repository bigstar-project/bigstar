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
using NsmbNetplayPoC::InputDelivery::Runtime;
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

void TestRuntimePreparesWirePayloads()
{
    Runtime runtime;
    std::map<melonDS::u32, NsmbNetplayPoC::InputState> localInputs;
    NsmbNetplayPoC::InputState current;
    current.KeyMask = 0xFFE;
    const auto now = Runtime::Clock::time_point(std::chrono::seconds(10));

    auto prepared = runtime.Prepare(4, current, {}, localInputs, now);
    CHECK(!prepared.Decision.Drop);
    CHECK(!prepared.Decision.Bundle);
    NsmbNetplayPoC::InputProtocol::FramedInput single;
    CHECK(NsmbNetplayPoC::InputProtocol::DecodeInput(
        prepared.ImmediatePayload.data(), prepared.ImmediatePayload.size(), single));
    CHECK(single.Frame == 4);
    CHECK(single.Input.KeyMask == 0xFFE);

    localInputs[2].KeyMask = 0xFFB;
    localInputs[3].KeyMask = 0xFF7;
    SendConfig bundle;
    bundle.UseHistoryBundle = true;
    bundle.BundleHistory = 2;
    prepared = runtime.Prepare(4, current, bundle, localInputs, now);
    std::vector<NsmbNetplayPoC::InputProtocol::FramedInput> entries;
    CHECK(prepared.Decision.Bundle);
    CHECK(NsmbNetplayPoC::InputProtocol::DecodeInputBundle(
        prepared.ImmediatePayload.data(), prepared.ImmediatePayload.size(), entries));
    CHECK(entries.size() == 3);
    CHECK(entries[0].Frame == 2 && entries[0].Input.KeyMask == 0xFFB);
    CHECK(entries[1].Frame == 3 && entries[1].Input.KeyMask == 0xFF7);
    CHECK(entries[2].Frame == 4 && entries[2].Input.KeyMask == 0xFFE);

    SendConfig drop;
    drop.DropModulo = 1;
    prepared = runtime.Prepare(5, current, drop, localInputs, now);
    CHECK(prepared.Decision.Drop);
    CHECK(prepared.ImmediatePayload.empty());
    CHECK(runtime.PendingCount() == 0);
}

void TestRuntimeOwnsDelayedQueue()
{
    Runtime runtime;
    std::map<melonDS::u32, NsmbNetplayPoC::InputState> localInputs;
    NsmbNetplayPoC::InputState current;
    current.KeyMask = 0xFDF;
    SendConfig config;
    config.DelayFrames = 3;
    const auto now = Runtime::Clock::time_point(std::chrono::seconds(10));

    const auto prepared = runtime.Prepare(10, current, config, localInputs, now);
    CHECK(prepared.ImmediatePayload.empty());
    CHECK(prepared.Decision.DelayFrames == 3);
    CHECK(runtime.PendingCount() == 1);
    std::vector<std::vector<char>> due;
    const auto collect = [&due](const std::vector<char>& payload) {
        due.push_back(payload);
    };
    runtime.DrainDue(12, now + std::chrono::milliseconds(49), collect);
    CHECK(due.empty());

    runtime.DrainDue(13, now, collect);
    CHECK(due.size() == 1);
    CHECK(runtime.PendingCount() == 0);
    NsmbNetplayPoC::InputProtocol::FramedInput decoded;
    CHECK(NsmbNetplayPoC::InputProtocol::DecodeInput(
        due[0].data(), due[0].size(), decoded));
    CHECK(decoded.Frame == 10 && decoded.Input.KeyMask == 0xFDF);

    runtime.Prepare(20, current, config, localInputs, now);
    due.clear();
    runtime.DrainDue(20, now + std::chrono::milliseconds(50), collect);
    CHECK(due.size() == 1);
    runtime.Prepare(30, current, config, localInputs, now);
    runtime.Prepare(31, current, config, localInputs, now);
    due.clear();
    runtime.DrainDue(33, now, collect);
    CHECK(due.size() == 1);
    CHECK(runtime.PendingCount() == 1);
    runtime.Clear();
    CHECK(runtime.PendingCount() == 0);
}

}

int main()
{
    TestDefaultDecision();
    TestDropModuloAndRangeBoundaries();
    TestBundleAndDelayBoundaries();
    TestBundleInputSelection();
    TestDelayedInputReleaseUsesFrameOrWallClock();
    TestRuntimePreparesWirePayloads();
    TestRuntimeOwnsDelayedQueue();

    if (Failures != 0)
    {
        std::fprintf(stderr, "nsmb input delivery tests failed: %d\n", Failures);
        return 1;
    }
    std::printf("nsmb input delivery tests passed\n");
    return 0;
}
