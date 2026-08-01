#include "NsmbInputTimeline.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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

NsmbMvlNetplay::InputState Input(melonDS::u32 keys)
{
    NsmbMvlNetplay::InputState input;
    input.KeyMask = keys;
    return input;
}

bool SameInput(
    const NsmbMvlNetplay::InputState& left,
    const NsmbMvlNetplay::InputState& right)
{
    return left.KeyMask == right.KeyMask
        && left.Touching == right.Touching
        && left.TouchX == right.TouchX
        && left.TouchY == right.TouchY;
}

void TestPredictionAndConfirmation()
{
    using NsmbMvlNetplay::InputTimeline::PredictionRuntime;
    PredictionRuntime timeline;
    PredictionRuntime::InputMap confirmed;
    const auto neutral = Input(0xFFF);

    auto resolved = timeline.Resolve(10, confirmed, neutral, {});
    CHECK(resolved.Predicted);
    CHECK(SameInput(resolved.Input, neutral));
    CHECK(timeline.PredictionCount() == 1);

    resolved = timeline.Resolve(11, confirmed, neutral, {});
    CHECK(resolved.Predicted);
    CHECK(SameInput(resolved.Input, neutral));

    const auto matching = timeline.Confirm(10, neutral, 12);
    CHECK(!matching.Mismatch);
    CHECK(!timeline.PendingRollbackFrame());
    confirmed.emplace(10, neutral);
    resolved = timeline.Resolve(10, confirmed, Input(0), {});
    CHECK(!resolved.Predicted);
    CHECK(SameInput(resolved.Input, neutral));

    timeline.ClearPredictions();
    resolved = timeline.Resolve(20, confirmed, Input(0), {});
    CHECK(resolved.Predicted);
    CHECK(SameInput(resolved.Input, neutral));
}

void TestPredictionMismatchScheduling()
{
    using NsmbMvlNetplay::InputTimeline::PredictionRuntime;
    PredictionRuntime timeline;
    PredictionRuntime::InputMap confirmed;
    const auto neutral = Input(0xFFF);
    timeline.Resolve(30, confirmed, neutral, {});
    timeline.Resolve(31, confirmed, neutral, {});
    timeline.Resolve(32, confirmed, neutral, {});

    const auto mismatch = timeline.Confirm(31, Input(0xFFE), 35);
    CHECK(mismatch.Mismatch);
    CHECK(mismatch.FrameAlreadySimulated);
    CHECK(SameInput(mismatch.PredictedInput, neutral));
    CHECK(timeline.PendingRollbackFrame() == 31);
    CHECK(timeline.PendingRollbackObservedFrame() == 35);
    CHECK(timeline.MismatchCount() == 1);
    CHECK(timeline.Predictions().count(30) == 1);
    CHECK(timeline.Predictions().count(31) == 0);
    CHECK(timeline.Predictions().count(32) == 0);

    timeline.Resolve(33, confirmed, neutral, {});
    timeline.Confirm(33, Input(0xFFC), 40);
    CHECK(timeline.PendingRollbackFrame() == 31);
    CHECK(timeline.PendingRollbackObservedFrame() == 35);

    timeline.Resolve(29, confirmed, neutral, {});
    timeline.Confirm(29, Input(0xFFB), std::nullopt);
    CHECK(timeline.PendingRollbackFrame() == 29);
    CHECK(timeline.PendingRollbackObservedFrame() == 29);

    timeline.ClearPendingRollbackFrame();
    CHECK(!timeline.PendingRollbackFrame());
    CHECK(timeline.PendingRollbackObservedFrame() == 29);
    timeline.ClearPendingRollback();
    CHECK(!timeline.PendingRollbackObservedFrame());

    timeline.Resolve(40, confirmed, neutral, {});
    const auto currentMismatch = timeline.Confirm(40, Input(0xFFD), 40);
    CHECK(currentMismatch.Mismatch);
    CHECK(!currentMismatch.FrameAlreadySimulated);
    CHECK(!timeline.PendingRollbackFrame());
}

void TestPredictionProbeAndPrune()
{
    using NsmbMvlNetplay::InputTimeline::PredictionProbe;
    using NsmbMvlNetplay::InputTimeline::PredictionRuntime;
    PredictionRuntime timeline;
    PredictionRuntime::InputMap confirmed;
    const auto neutral = Input(0xFFF);
    PredictionProbe probe;
    probe.Modulo = 2;
    probe.Offset = 1;
    probe.Limit = 2;
    probe.StartFrame = 5;
    probe.EndFrame = 9;
    probe.KeyMask = 0x1001;

    CHECK(SameInput(timeline.Resolve(4, confirmed, neutral, probe).Input, neutral));
    CHECK(timeline.Resolve(5, confirmed, neutral, probe).Input.KeyMask == 0xFFE);
    CHECK(timeline.Resolve(6, confirmed, neutral, probe).Input.KeyMask == 0xFFE);
    CHECK(timeline.Resolve(7, confirmed, neutral, probe).Input.KeyMask == 0xFFF);
    timeline.Resolve(8, confirmed, neutral, probe);
    CHECK(SameInput(timeline.Resolve(9, confirmed, neutral, probe).Input, neutral));
    CHECK(timeline.PredictionProbeCount() == 2);

    confirmed.emplace(4, neutral);
    confirmed.emplace(5, neutral);
    timeline.Prune(10, 4, confirmed);
    CHECK(timeline.Predictions().count(4) == 0);
    CHECK(timeline.Predictions().count(5) == 0);
    CHECK(timeline.Predictions().count(7) == 1);
}

void TestRuntimeRemoteStorePrimeAndPrune()
{
    using NsmbMvlNetplay::InputTimeline::Runtime;
    constexpr melonDS::u32 noFrameLimit = 0;
    Runtime runtime;
    const auto neutral = Input(0xFFF);

    runtime.PrimeEpoch(100, 4, neutral, noFrameLimit);
    CHECK(runtime.LocalInputs.size() == 4);
    CHECK(runtime.RemoteInputs.size() == 4);
    CHECK(runtime.LocalInputs.count(100) == 1);
    CHECK(runtime.RemoteInputs.count(103) == 1);
    CHECK(runtime.LastReceivedInputFrame == 103);

    const auto stored = runtime.StoreRemote(105, Input(0xFFE), 106, false, noFrameLimit);
    CHECK(stored.PreviousLastReceived == 103);
    CHECK(runtime.LastReceivedInputFrame == 105);
    CHECK(runtime.RemoteInputs.at(105).KeyMask == 0xFFE);

    const auto older = runtime.StoreRemote(104, Input(0xFFD), 106, false, noFrameLimit);
    CHECK(older.PreviousLastReceived == 105);
    CHECK(runtime.LastReceivedInputFrame == 105);

    runtime.LocalInputs.emplace(90, neutral);
    runtime.RemoteInputs.emplace(90, neutral);
    runtime.PruneHistory(100);
    CHECK(runtime.LocalInputs.count(90) == 0);
    CHECK(runtime.RemoteInputs.count(90) == 0);
    CHECK(runtime.LocalInputs.count(100) == 1);
    CHECK(runtime.RemoteInputs.count(105) == 1);
    CHECK(runtime.Lead(110, noFrameLimit) == 5);
}

void TestReplayFrameInputResolution()
{
    using NsmbMvlNetplay::InputTimeline::ResolveReplayFrameInputs;
    using NsmbMvlNetplay::InputTimeline::Runtime;
    Runtime runtime;
    const auto neutral = Input(0xFFF);
    const auto local = Input(0xFFE);
    auto remote = Input(0xFFD);
    remote.Touching = true;
    remote.TouchX = 42;
    remote.TouchY = 84;
    runtime.LocalInputs.emplace(40, local);
    runtime.RemoteInputs.emplace(40, remote);

    const auto playerZero = ResolveReplayFrameInputs(runtime, 40, 0, neutral);
    CHECK(playerZero.has_value());
    CHECK(playerZero && SameInput(playerZero->Local, local));
    CHECK(playerZero && SameInput(playerZero->Remote, remote));
    CHECK(playerZero && SameInput(playerZero->Players[0], local));
    CHECK(playerZero && SameInput(playerZero->Players[1], remote));
    CHECK(playerZero && !playerZero->RemotePredicted);

    const auto playerOne = ResolveReplayFrameInputs(runtime, 40, 1, neutral);
    CHECK(playerOne.has_value());
    CHECK(playerOne && SameInput(playerOne->Players[0], remote));
    CHECK(playerOne && SameInput(playerOne->Players[1], local));

    const auto predicted = ResolveReplayFrameInputs(runtime, 41, 1, neutral);
    CHECK(predicted.has_value());
    CHECK(predicted && SameInput(predicted->Local, neutral));
    CHECK(predicted && SameInput(predicted->Remote, neutral));
    CHECK(predicted && predicted->RemotePredicted);
    CHECK(!ResolveReplayFrameInputs(runtime, 40, -1, neutral));
    CHECK(!ResolveReplayFrameInputs(runtime, 40, 2, neutral));
}

void TestRuntimeRestartContractAndStatistics()
{
    using NsmbMvlNetplay::InputTimeline::Runtime;
    constexpr melonDS::u32 noFrameLimit = 0;
    Runtime runtime;
    const auto neutral = Input(0xFFF);
    const auto actual = Input(0xFFE);

    runtime.RollbackInputs.Resolve(10, runtime.RemoteInputs, neutral, {});
    const auto stored = runtime.StoreRemote(10, actual, 12, true, noFrameLimit);
    CHECK(stored.Confirmation.Mismatch);
    CHECK(runtime.RollbackInputs.PendingRollbackFrame() == 10);
    CHECK(runtime.RollbackInputs.MismatchCount() == 1);
    runtime.LocalInputs.emplace(10, neutral);
    runtime.LastSentInputFrame = 11;
    runtime.LastTracedSentInputFrame = 11;
    runtime.LastInputHealthSummaryFrame = 10;
    runtime.LastInputFrameLeadResendAt = std::chrono::steady_clock::now();
    runtime.InputFrameLeadResendCount = 2;
    runtime.RecordRemoteInputWait(100, 3);
    runtime.RecordFrameLeadThrottle(200, 5);

    runtime.ResetForRestart(noFrameLimit);

    CHECK(runtime.LocalInputs.empty());
    CHECK(runtime.RemoteInputs.empty());
    CHECK(runtime.RollbackInputs.Predictions().empty());
    CHECK(runtime.RollbackInputs.PendingRollbackFrame() == 10);
    CHECK(runtime.RollbackInputs.MismatchCount() == 1);
    CHECK(runtime.LastSentInputFrame == noFrameLimit);
    CHECK(runtime.LastReceivedInputFrame == noFrameLimit);
    CHECK(runtime.LastTracedSentInputFrame == noFrameLimit);
    CHECK(runtime.LastInputHealthSummaryFrame == noFrameLimit);
    CHECK(runtime.LastInputFrameLeadResendAt == std::chrono::steady_clock::time_point{});
    CHECK(runtime.InputFrameLeadResendCount == 0);
    CHECK(runtime.RemoteInputWaitCount == 1);
    CHECK(runtime.RemoteInputWaitLoops == 3);
    CHECK(runtime.RemoteInputWaitUs == 100);
    CHECK(runtime.RemoteInputWaitMaxUs == 100);
    CHECK(runtime.FrameLeadThrottleCount == 1);
    CHECK(runtime.FrameLeadThrottleLoops == 5);
    CHECK(runtime.FrameLeadThrottleUs == 200);
    CHECK(runtime.FrameLeadThrottleMaxUs == 200);

    const auto afterRestart = runtime.RollbackInputs.Resolve(20, runtime.RemoteInputs, neutral, {});
    CHECK(SameInput(afterRestart.Input, actual));
}

void TestButtonAndMaskParsing()
{
    NsmbMvlNetplay::InputState input;
    CHECK(NsmbMvlNetplay::InputTimeline::ParseInputSpec("RIGHT+A+Y", input));
    CHECK(input.KeyMask == (0xFFFu & ~(1u << 4) & ~(1u << 0) & ~(1u << 11)));
    CHECK(NsmbMvlNetplay::InputTimeline::ParseInputSpec("mask=0x1234", input));
    CHECK(input.KeyMask == 0x234u);
    CHECK(NsmbMvlNetplay::InputTimeline::ParseInputSpec("NEUTRAL", input));
    CHECK(input.KeyMask == 0xFFFu);
    CHECK(!NsmbMvlNetplay::InputTimeline::ParseInputSpec("A+INVALID", input));
}

void TestTimelineTargetsTouchAndFirstMatch()
{
    std::istringstream script(
        "# fixture\n"
        "0-9 A\n"
        "inst1 10-19 RIGHT+B 300,250\n"
        "ALL 20-29 LEFT\n"
        "inst1 10-19 Y\n");
    std::vector<NsmbMvlNetplay::InputTimeline::InputSpan> spans;
    NsmbMvlNetplay::InputTimeline::ParseError error;
    CHECK(NsmbMvlNetplay::InputTimeline::ParseInputScript(script, spans, error));
    CHECK(spans.size() == 4);

    NsmbMvlNetplay::InputState fallback;
    fallback.KeyMask = 0x321;
    const auto all = NsmbMvlNetplay::InputTimeline::Apply(spans, 0, 5, fallback);
    CHECK((all.KeyMask & 1u) == 0);

    const auto wrongInstance = NsmbMvlNetplay::InputTimeline::Apply(spans, 0, 15, fallback);
    CHECK(wrongInstance.KeyMask == fallback.KeyMask);

    const auto instance = NsmbMvlNetplay::InputTimeline::Apply(spans, 1, 15, fallback);
    CHECK((instance.KeyMask & (1u << 4)) == 0);
    CHECK((instance.KeyMask & (1u << 1)) == 0);
    CHECK((instance.KeyMask & (1u << 11)) != 0);
    CHECK(instance.Touching);
    CHECK(instance.TouchX == 255);
    CHECK(instance.TouchY == 191);

    const auto explicitAll = NsmbMvlNetplay::InputTimeline::Apply(spans, 7, 25, fallback);
    CHECK((explicitAll.KeyMask & (1u << 5)) == 0);
}

void CheckParseError(const char* text, NsmbMvlNetplay::InputTimeline::ParseErrorKind kind)
{
    std::istringstream script(text);
    std::vector<NsmbMvlNetplay::InputTimeline::InputSpan> spans;
    NsmbMvlNetplay::InputTimeline::ParseError error;
    CHECK(!NsmbMvlNetplay::InputTimeline::ParseInputScript(script, spans, error));
    CHECK(error.Kind == kind);
    CHECK(error.Line == 1);
}

void TestParseErrorsAreClassified()
{
    using NsmbMvlNetplay::InputTimeline::ParseErrorKind;
    CheckParseError("player0 0-1 A\n", ParseErrorKind::Target);
    CheckParseError("ALL 10 A\n", ParseErrorKind::Range);
    CheckParseError("ALL 9-1 A\n", ParseErrorKind::Input);
    CheckParseError("ALL 0-1 INVALID\n", ParseErrorKind::Input);
    CheckParseError("ALL 0-1 A invalid-touch\n", ParseErrorKind::Touch);
}

void TestInputRecorderSpanAndFilterContract()
{
    using NsmbMvlNetplay::InputTimeline::Recorder;
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "nsmb_input_timeline_recorder_test.txt";
    std::error_code error;
    std::filesystem::remove(path, error);

    Recorder recorder;
    CHECK(recorder.Open(path.string(), 10, 20, 1));
    CHECK(recorder.IsOpen());
    recorder.Record(0, 10, Input(0xFFE));
    recorder.Record(1, 9, Input(0xFFE));
    recorder.Record(1, 10, Input(0x1FFE));
    recorder.Record(1, 11, Input(0xFFE));
    recorder.Record(1, 11, Input(0xFFD));
    recorder.Record(1, 12, Input(0xFFD));
    recorder.Record(1, 13, Input(0xFFD));
    recorder.Record(1, 15, Input(0xFFE));

    auto touch = Input(0xFEF);
    touch.Touching = true;
    touch.TouchX = 12;
    touch.TouchY = 34;
    recorder.Record(1, 15, touch);
    recorder.Record(1, 21, Input(0xFFE));
    recorder.Close();
    CHECK(!recorder.IsOpen());

    std::ifstream input(path);
    std::ostringstream contents;
    contents << input.rdbuf();
    CHECK(contents.str() ==
        "# NSMB input recording generated by melonDS NSML PoC\n"
        "# startFrame=10 endFrame=20 instance=1\n"
        "10-11 A\n"
        "12-13 B\n"
        "15-15 RIGHT 12,34\n");
    std::filesystem::remove(path, error);
}

void TestInputRecorderPeriodicFlush()
{
    using NsmbMvlNetplay::InputTimeline::Recorder;
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "nsmb_input_timeline_recorder_flush_test.txt";
    std::error_code error;
    std::filesystem::remove(path, error);

    Recorder recorder;
    CHECK(recorder.Open(path.string(), 0, 0, -1));
    for (melonDS::u32 frame = 0; frame <= 64; frame++)
        recorder.Record(0, frame, Input((frame & 1) == 0 ? 0xFFE : 0xFFD));

    std::ifstream checkpoint(path);
    std::ostringstream beforeClose;
    beforeClose << checkpoint.rdbuf();
    CHECK(beforeClose.str().find("63-63 B\n") != std::string::npos);
    CHECK(beforeClose.str().find("64-64 A\n") == std::string::npos);

    recorder.Close();
    std::ifstream completed(path);
    std::ostringstream afterClose;
    afterClose << completed.rdbuf();
    CHECK(afterClose.str().find("64-64 A\n") != std::string::npos);
    std::filesystem::remove(path, error);
}

}

int main()
{
    TestPredictionAndConfirmation();
    TestPredictionMismatchScheduling();
    TestPredictionProbeAndPrune();
    TestRuntimeRemoteStorePrimeAndPrune();
    TestReplayFrameInputResolution();
    TestRuntimeRestartContractAndStatistics();
    TestButtonAndMaskParsing();
    TestTimelineTargetsTouchAndFirstMatch();
    TestParseErrorsAreClassified();
    TestInputRecorderSpanAndFilterContract();
    TestInputRecorderPeriodicFlush();

    if (Failures != 0)
    {
        std::fprintf(stderr, "nsmb input timeline tests failed: %d\n", Failures);
        return 1;
    }
    std::printf("nsmb input timeline tests passed\n");
    return 0;
}
