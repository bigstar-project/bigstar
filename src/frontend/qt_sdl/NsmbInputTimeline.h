#ifndef NSMBINPUTTIMELINE_H
#define NSMBINPUTTIMELINE_H

#include "NsmbNetplayPoC.h"

#include <istream>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace NsmbNetplayPoC::InputTimeline
{

struct InputSpan
{
    int Instance = -1;
    melonDS::u32 Start = 0;
    melonDS::u32 End = 0;
    InputState Input;
};

struct PredictionProbe
{
    int Modulo = 0;
    int Offset = 0;
    int Limit = -1;
    melonDS::u32 StartFrame = 0;
    std::optional<melonDS::u32> EndFrame;
    melonDS::u32 KeyMask = 1;
};

struct PredictedInput
{
    InputState Input;
    bool Predicted = false;
};

struct ConfirmedInputResult
{
    bool Mismatch = false;
    bool FrameAlreadySimulated = false;
    InputState PredictedInput;
};

class PredictionRuntime
{
public:
    using InputMap = std::map<melonDS::u32, InputState>;

    ConfirmedInputResult Confirm(
        melonDS::u32 frame,
        const InputState& input,
        std::optional<melonDS::u32> localFrame);
    PredictedInput Resolve(
        melonDS::u32 frame,
        const InputMap& confirmedInputs,
        const InputState& neutralInput,
        const PredictionProbe& probe);
    void Prune(
        melonDS::u32 currentFrame,
        melonDS::u32 window,
        const InputMap& confirmedInputs);
    void ClearPredictions();

    const InputMap& Predictions() const;
    std::optional<melonDS::u32> PendingRollbackFrame() const;
    std::optional<melonDS::u32> PendingRollbackObservedFrame() const;
    void ClearPendingRollbackFrame();
    void ClearPendingRollback();

    melonDS::u32 PredictionCount() const;
    melonDS::u32 PredictionProbeCount() const;
    melonDS::u32 MismatchCount() const;

private:
    InputMap Predictions_;
    std::optional<InputState> LastConfirmedInput_;
    std::optional<melonDS::u32> PendingRollbackFrame_;
    std::optional<melonDS::u32> PendingRollbackObservedFrame_;
    melonDS::u32 PredictionCount_ = 0;
    melonDS::u32 PredictionProbeCount_ = 0;
    melonDS::u32 MismatchCount_ = 0;
};

enum class ParseErrorKind
{
    None,
    Open,
    Target,
    Range,
    Input,
    Touch,
};

struct ParseError
{
    ParseErrorKind Kind = ParseErrorKind::None;
    int Line = 0;
};

bool ParseInputSpec(const std::string& spec, InputState& input);
bool ParseInputScript(std::istream& input, std::vector<InputSpan>& spans, ParseError& error);
bool LoadInputScriptFile(
    const std::string& path,
    std::vector<InputSpan>& spans,
    ParseError& error);
InputState Apply(
    const std::vector<InputSpan>& spans,
    int instanceID,
    melonDS::u32 frame,
    const InputState& fallback);

}

#endif
