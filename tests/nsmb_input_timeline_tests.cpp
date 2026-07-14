#include "NsmbInputTimeline.h"

#include <cstdio>
#include <sstream>
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

NsmbNetplayPoC::InputState Input(melonDS::u32 keys)
{
    NsmbNetplayPoC::InputState input;
    input.KeyMask = keys;
    return input;
}

bool SameInput(
    const NsmbNetplayPoC::InputState& left,
    const NsmbNetplayPoC::InputState& right)
{
    return left.KeyMask == right.KeyMask
        && left.Touching == right.Touching
        && left.TouchX == right.TouchX
        && left.TouchY == right.TouchY;
}

void TestPredictionAndConfirmation()
{
    using NsmbNetplayPoC::InputTimeline::PredictionRuntime;
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
    using NsmbNetplayPoC::InputTimeline::PredictionRuntime;
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
    using NsmbNetplayPoC::InputTimeline::PredictionProbe;
    using NsmbNetplayPoC::InputTimeline::PredictionRuntime;
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

void TestButtonAndMaskParsing()
{
    NsmbNetplayPoC::InputState input;
    CHECK(NsmbNetplayPoC::InputTimeline::ParseInputSpec("RIGHT+A+Y", input));
    CHECK(input.KeyMask == (0xFFFu & ~(1u << 4) & ~(1u << 0) & ~(1u << 11)));
    CHECK(NsmbNetplayPoC::InputTimeline::ParseInputSpec("mask=0x1234", input));
    CHECK(input.KeyMask == 0x234u);
    CHECK(NsmbNetplayPoC::InputTimeline::ParseInputSpec("NEUTRAL", input));
    CHECK(input.KeyMask == 0xFFFu);
    CHECK(!NsmbNetplayPoC::InputTimeline::ParseInputSpec("A+INVALID", input));
}

void TestTimelineTargetsTouchAndFirstMatch()
{
    std::istringstream script(
        "# fixture\n"
        "0-9 A\n"
        "inst1 10-19 RIGHT+B 300,250\n"
        "ALL 20-29 LEFT\n"
        "inst1 10-19 Y\n");
    std::vector<NsmbNetplayPoC::InputTimeline::InputSpan> spans;
    NsmbNetplayPoC::InputTimeline::ParseError error;
    CHECK(NsmbNetplayPoC::InputTimeline::ParseInputScript(script, spans, error));
    CHECK(spans.size() == 4);

    NsmbNetplayPoC::InputState fallback;
    fallback.KeyMask = 0x321;
    const auto all = NsmbNetplayPoC::InputTimeline::Apply(spans, 0, 5, fallback);
    CHECK((all.KeyMask & 1u) == 0);

    const auto wrongInstance = NsmbNetplayPoC::InputTimeline::Apply(spans, 0, 15, fallback);
    CHECK(wrongInstance.KeyMask == fallback.KeyMask);

    const auto instance = NsmbNetplayPoC::InputTimeline::Apply(spans, 1, 15, fallback);
    CHECK((instance.KeyMask & (1u << 4)) == 0);
    CHECK((instance.KeyMask & (1u << 1)) == 0);
    CHECK((instance.KeyMask & (1u << 11)) != 0);
    CHECK(instance.Touching);
    CHECK(instance.TouchX == 255);
    CHECK(instance.TouchY == 191);

    const auto explicitAll = NsmbNetplayPoC::InputTimeline::Apply(spans, 7, 25, fallback);
    CHECK((explicitAll.KeyMask & (1u << 5)) == 0);
}

void CheckParseError(const char* text, NsmbNetplayPoC::InputTimeline::ParseErrorKind kind)
{
    std::istringstream script(text);
    std::vector<NsmbNetplayPoC::InputTimeline::InputSpan> spans;
    NsmbNetplayPoC::InputTimeline::ParseError error;
    CHECK(!NsmbNetplayPoC::InputTimeline::ParseInputScript(script, spans, error));
    CHECK(error.Kind == kind);
    CHECK(error.Line == 1);
}

void TestParseErrorsAreClassified()
{
    using NsmbNetplayPoC::InputTimeline::ParseErrorKind;
    CheckParseError("player0 0-1 A\n", ParseErrorKind::Target);
    CheckParseError("ALL 10 A\n", ParseErrorKind::Range);
    CheckParseError("ALL 9-1 A\n", ParseErrorKind::Input);
    CheckParseError("ALL 0-1 INVALID\n", ParseErrorKind::Input);
    CheckParseError("ALL 0-1 A invalid-touch\n", ParseErrorKind::Touch);
}

}

int main()
{
    TestPredictionAndConfirmation();
    TestPredictionMismatchScheduling();
    TestPredictionProbeAndPrune();
    TestButtonAndMaskParsing();
    TestTimelineTargetsTouchAndFirstMatch();
    TestParseErrorsAreClassified();

    if (Failures != 0)
    {
        std::fprintf(stderr, "nsmb input timeline tests failed: %d\n", Failures);
        return 1;
    }
    std::printf("nsmb input timeline tests passed\n");
    return 0;
}
