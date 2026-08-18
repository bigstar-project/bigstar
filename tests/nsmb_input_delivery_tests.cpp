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

using NsmbMvlNetplay::InputDelivery::DecideSend;
using NsmbMvlNetplay::InputDelivery::Runtime;
using NsmbMvlNetplay::InputDelivery::SendConfig;

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
    NsmbMvlNetplay::InputState current;
    current.KeyMask = 0xAAA;
    std::map<melonDS::u32, NsmbMvlNetplay::InputState> localInputs;
    localInputs[0].KeyMask = 0xFFE;
    localInputs[2].KeyMask = 0xFDF;

    const auto entries = NsmbMvlNetplay::InputDelivery::SelectBundleInputs(
        7,
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

void TestUnackedBundleSelectionAndAckWindow()
{
    using NsmbMvlNetplay::InputDelivery::AckUpdate;
    using NsmbMvlNetplay::InputDelivery::SelectUnackedInputs;
    std::map<melonDS::u32, NsmbMvlNetplay::InputState> localInputs;
    for (melonDS::u32 frame = 100; frame <= 110; frame++)
        localInputs[frame].KeyMask = 0xF00u + frame;
    NsmbMvlNetplay::InputState current = localInputs[110];

    auto entries = SelectUnackedInputs(
        3, 110, current, 4, localInputs, 106);
    CHECK(entries.size() == 4);
    CHECK(entries[0].Frame == 107);
    CHECK(entries[3].Frame == 110);

    entries = SelectUnackedInputs(
        3, 110, current, 4, localInputs, std::nullopt);
    CHECK(entries.size() == 4);
    CHECK(entries[0].Frame == 100);
    CHECK(entries[3].Frame == 103);

    Runtime runtime;
    CHECK(runtime.RecordRemoteAck(106, 110, 0) == AckUpdate::Advanced);
    CHECK(runtime.RemoteAckFrame() == 106);
    CHECK(runtime.RecordRemoteAck(105, 110, 0) == AckUpdate::Stale);
    CHECK(runtime.RecordRemoteAck(111, 110, 0) == AckUpdate::Future);
    CHECK(runtime.RemoteAckFrame() == 106);

    const auto payload = runtime.BuildPayload(
        3, 110, current, 3, localInputs, 205);
    std::optional<melonDS::u32> ackFrame;
    std::vector<NsmbMvlNetplay::InputProtocol::FramedInput> decoded;
    CHECK(NsmbMvlNetplay::InputProtocol::DecodeInputBundle(
        payload.data(), payload.size(), decoded, &ackFrame));
    CHECK(ackFrame == 205);
    CHECK(decoded.size() == 4);
    CHECK(decoded[0].Frame == 107);
    CHECK(decoded[3].Frame == 110);

    runtime.Clear();
    CHECK(runtime.RecordRemoteAck(100, 110, 0) == AckUpdate::Advanced);
    const auto fullWindowPayload = runtime.BuildPayload(
        3, 110, current, 3, localInputs, 205);
    CHECK(NsmbMvlNetplay::InputProtocol::DecodeInputBundle(
        fullWindowPayload.data(), fullWindowPayload.size(), decoded));
    CHECK(decoded.size() == 10);
    CHECK(decoded[0].Frame == 101);
    CHECK(decoded[9].Frame == 110);

    runtime.Clear();
    CHECK(!runtime.RemoteAckFrame());
}

void TestDelayedInputReleaseUsesFrameOrWallClock()
{
    const auto now = std::chrono::steady_clock::time_point(std::chrono::seconds(10));
    const auto releaseTime = now + std::chrono::milliseconds(50);
    CHECK(!NsmbMvlNetplay::InputDelivery::ShouldReleaseDelayedInput(
        9, now, 10, releaseTime));
    CHECK(NsmbMvlNetplay::InputDelivery::ShouldReleaseDelayedInput(
        10, now, 10, releaseTime));
    CHECK(NsmbMvlNetplay::InputDelivery::ShouldReleaseDelayedInput(
        9, releaseTime, 10, releaseTime));
}

void TestDelayProgressUsesGenerationLocalInputFrame()
{
    using NsmbMvlNetplay::InputDelivery::SelectDelayProgressFrame;
    constexpr melonDS::u32 noFrame = 0;

    CHECK(SelectDelayProgressFrame(872, 9953, noFrame) == 872);
    CHECK(SelectDelayProgressFrame(noFrame, 9953, noFrame) == 9953);

    Runtime runtime;
    std::map<melonDS::u32, NsmbMvlNetplay::InputState> localInputs;
    NsmbMvlNetplay::InputState input;
    input.KeyMask = 0xFFE;
    SendConfig config;
    config.DelayFrames = 8;
    const auto now = Runtime::Clock::time_point(std::chrono::seconds(10));
    runtime.Prepare(1, 872, input, config, localInputs, now);

    std::vector<std::vector<char>> due;
    const auto collect = [&due](const std::vector<char>& payload) {
        due.push_back(payload);
    };
    runtime.DrainDue(
        SelectDelayProgressFrame(872, 9953, noFrame), now, collect);
    CHECK(due.empty());
    CHECK(runtime.PendingCount() == 1);

    runtime.DrainDue(
        SelectDelayProgressFrame(880, 9961, noFrame), now, collect);
    CHECK(due.size() == 1);
    CHECK(runtime.PendingCount() == 0);
}

void TestRuntimePreparesWirePayloads()
{
    Runtime runtime;
    std::map<melonDS::u32, NsmbMvlNetplay::InputState> localInputs;
    NsmbMvlNetplay::InputState current;
    current.KeyMask = 0xFFE;
    const auto now = Runtime::Clock::time_point(std::chrono::seconds(10));

    auto prepared = runtime.Prepare(7, 4, current, {}, localInputs, now);
    CHECK(!prepared.Decision.Drop);
    CHECK(!prepared.Decision.Bundle);
    NsmbMvlNetplay::InputProtocol::FramedInput single;
    CHECK(NsmbMvlNetplay::InputProtocol::DecodeInput(
        prepared.ImmediatePayload.data(), prepared.ImmediatePayload.size(), single));
    CHECK(single.Frame == 4);
    CHECK(single.Input.KeyMask == 0xFFE);

    localInputs[2].KeyMask = 0xFFB;
    localInputs[3].KeyMask = 0xFF7;
    SendConfig bundle;
    bundle.UseHistoryBundle = true;
    bundle.BundleHistory = 2;
    prepared = runtime.Prepare(7, 4, current, bundle, localInputs, now);
    std::vector<NsmbMvlNetplay::InputProtocol::FramedInput> entries;
    CHECK(prepared.Decision.Bundle);
    CHECK(NsmbMvlNetplay::InputProtocol::DecodeInputBundle(
        prepared.ImmediatePayload.data(), prepared.ImmediatePayload.size(), entries));
    CHECK(entries.size() == 3);
    CHECK(entries[0].Frame == 2 && entries[0].Input.KeyMask == 0xFFB);
    CHECK(entries[1].Frame == 3 && entries[1].Input.KeyMask == 0xFF7);
    CHECK(entries[2].Frame == 4 && entries[2].Input.KeyMask == 0xFFE);

    SendConfig drop;
    drop.DropModulo = 1;
    prepared = runtime.Prepare(7, 5, current, drop, localInputs, now);
    CHECK(prepared.Decision.Drop);
    CHECK(prepared.ImmediatePayload.empty());
    CHECK(runtime.PendingCount() == 0);
}

void TestRuntimeOwnsDelayedQueue()
{
    Runtime runtime;
    std::map<melonDS::u32, NsmbMvlNetplay::InputState> localInputs;
    NsmbMvlNetplay::InputState current;
    current.KeyMask = 0xFDF;
    SendConfig config;
    config.DelayFrames = 3;
    const auto now = Runtime::Clock::time_point(std::chrono::seconds(10));

    const auto prepared = runtime.Prepare(7, 10, current, config, localInputs, now);
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
    NsmbMvlNetplay::InputProtocol::FramedInput decoded;
    CHECK(NsmbMvlNetplay::InputProtocol::DecodeInput(
        due[0].data(), due[0].size(), decoded));
    CHECK(decoded.Frame == 10 && decoded.Input.KeyMask == 0xFDF);

    runtime.Prepare(7, 20, current, config, localInputs, now);
    due.clear();
    runtime.DrainDue(20, now + std::chrono::milliseconds(50), collect);
    CHECK(due.size() == 1);
    runtime.Prepare(7, 30, current, config, localInputs, now);
    runtime.Prepare(7, 31, current, config, localInputs, now);
    due.clear();
    runtime.DrainDue(33, now, collect);
    CHECK(due.size() == 1);
    CHECK(runtime.PendingCount() == 1);
    runtime.Clear();
    CHECK(runtime.PendingCount() == 0);
}

void TestJitterCanDeliverPacketsOutOfOrder()
{
    Runtime runtime;
    std::map<melonDS::u32, NsmbMvlNetplay::InputState> localInputs;
    NsmbMvlNetplay::InputState input;
    SendConfig config;
    config.DelayFrames = 0;
    config.JitterFrames = 2;
    const auto now = Runtime::Clock::time_point(std::chrono::seconds(10));

    input.KeyMask = 0xFFE;
    const auto delayed = runtime.Prepare(
        7, 11, input, config, localInputs, now);
    CHECK(delayed.ImmediatePayload.empty());
    CHECK(delayed.Decision.DelayFrames == 2);

    input.KeyMask = 0xFFD;
    const auto immediate = runtime.Prepare(
        7, 12, input, config, localInputs, now);
    CHECK(!immediate.ImmediatePayload.empty());
    CHECK(immediate.Decision.DelayFrames == 0);

    std::vector<melonDS::u32> deliveryOrder;
    NsmbMvlNetplay::InputProtocol::FramedInput decoded;
    CHECK(NsmbMvlNetplay::InputProtocol::DecodeInput(
        immediate.ImmediatePayload.data(), immediate.ImmediatePayload.size(),
        decoded));
    deliveryOrder.push_back(decoded.Frame);
    runtime.DrainDue(
        13, now,
        [&deliveryOrder](const std::vector<char>& payload) {
            NsmbMvlNetplay::InputProtocol::FramedInput entry;
            CHECK(NsmbMvlNetplay::InputProtocol::DecodeInput(
                payload.data(), payload.size(), entry));
            deliveryOrder.push_back(entry.Frame);
        });

    CHECK(deliveryOrder.size() == 2);
    CHECK(deliveryOrder[0] == 12);
    CHECK(deliveryOrder[1] == 11);
    CHECK(runtime.PendingCount() == 0);
}

void TestRuntimeDrainsAllForGenerationTransition()
{
    Runtime runtime;
    std::map<melonDS::u32, NsmbMvlNetplay::InputState> localInputs;
    NsmbMvlNetplay::InputState first;
    first.KeyMask = 0xFFE;
    NsmbMvlNetplay::InputState second;
    second.KeyMask = 0xFFD;
    SendConfig config;
    config.DelayFrames = 120;
    const auto now = Runtime::Clock::time_point(std::chrono::seconds(10));

    runtime.Prepare(4, 20, first, config, localInputs, now);
    runtime.Prepare(4, 21, second, config, localInputs, now);
    CHECK(runtime.PendingCount() == 2);

    std::vector<std::vector<char>> drained;
    runtime.DrainAll([&drained](const std::vector<char>& payload) {
        drained.push_back(payload);
    });
    CHECK(runtime.PendingCount() == 0);
    CHECK(drained.size() == 2);

    NsmbMvlNetplay::InputProtocol::FramedInput decoded;
    CHECK(NsmbMvlNetplay::InputProtocol::DecodeInput(
        drained[0].data(), drained[0].size(), decoded));
    CHECK(decoded.Generation == 4);
    CHECK(decoded.Frame == 20 && decoded.Input.KeyMask == 0xFFE);
    CHECK(NsmbMvlNetplay::InputProtocol::DecodeInput(
        drained[1].data(), drained[1].size(), decoded));
    CHECK(decoded.Frame == 21 && decoded.Input.KeyMask == 0xFFD);
}

}

int main()
{
    TestDefaultDecision();
    TestDropModuloAndRangeBoundaries();
    TestBundleAndDelayBoundaries();
    TestBundleInputSelection();
    TestUnackedBundleSelectionAndAckWindow();
    TestDelayedInputReleaseUsesFrameOrWallClock();
    TestDelayProgressUsesGenerationLocalInputFrame();
    TestRuntimePreparesWirePayloads();
    TestRuntimeOwnsDelayedQueue();
    TestJitterCanDeliverPacketsOutOfOrder();
    TestRuntimeDrainsAllForGenerationTransition();

    if (Failures != 0)
    {
        std::fprintf(stderr, "nsmb input delivery tests failed: %d\n", Failures);
        return 1;
    }
    std::printf("nsmb input delivery tests passed\n");
    return 0;
}
