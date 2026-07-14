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
