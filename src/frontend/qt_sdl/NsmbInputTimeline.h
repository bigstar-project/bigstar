#ifndef NSMBINPUTTIMELINE_H
#define NSMBINPUTTIMELINE_H

#include "NsmbMvlNetplayRuntime.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <istream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace NsmbMvlNetplay::InputTimeline
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
    bool RetainConfirmation = false;
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
    std::optional<InputState> TakePredictionProbeConfirmation(
        melonDS::u32 frame);

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
    InputMap PredictionProbeConfirmations_;
    std::optional<InputState> LastConfirmedInput_;
    std::optional<melonDS::u32> PendingRollbackFrame_;
    std::optional<melonDS::u32> PendingRollbackObservedFrame_;
    melonDS::u32 PredictionCount_ = 0;
    melonDS::u32 PredictionProbeCount_ = 0;
    melonDS::u32 MismatchCount_ = 0;
};

struct RemoteInputStoreResult
{
    melonDS::u32 PreviousLastReceived = 0;
    ConfirmedInputResult Confirmation;
};

struct ReplayFrameInputs
{
    InputState Local;
    InputState Remote;
    InputState Players[2];
    bool RemotePredicted = false;
};

class Runtime
{
public:
    using InputMap = PredictionRuntime::InputMap;

    void ResetForRestart(melonDS::u32 noFrameLimit);
    RemoteInputStoreResult StoreRemote(
        melonDS::u32 frame,
        const InputState& input,
        std::optional<melonDS::u32> localFrame,
        bool confirmPrediction,
        melonDS::u32 noFrameLimit);
    void PruneHistory(melonDS::u32 keepFromFrame);
    void PrimeEpoch(
        melonDS::u32 startFrame,
        melonDS::u32 delay,
        const InputState& neutralInput,
        melonDS::u32 noFrameLimit);
    int Lead(melonDS::u32 sendFrame, melonDS::u32 noFrameLimit) const;
    void RecordRemoteInputWait(unsigned long long elapsedUs, unsigned long long loops);
    void RecordFrameLeadThrottle(unsigned long long elapsedUs, unsigned long long loops);

    PredictionRuntime RollbackInputs;
    InputMap LocalInputs;
    InputMap RemoteInputs;
    std::chrono::steady_clock::time_point LastInputFrameLeadResendAt;
    int InputFrameLeadResendCount = 0;
    melonDS::u32 LastTracedSentInputFrame = 0;
    melonDS::u32 LastTracedReceivedInputFrame = 0;
    melonDS::u32 LastReceivedInputFrame = 0;
    melonDS::u32 LastSentInputFrame = 0;
    melonDS::u32 LastInputHealthSummaryFrame = 0;
    melonDS::u32 LastInputHealthRemoteWaitFrame = 0;
    melonDS::u32 LastInputHealthThrottleFrame = 0;
    melonDS::u32 LastInputHealthThrottleResolvedFrame = 0;
    melonDS::u32 LastInputHealthReceiveGapFrame = 0;
    melonDS::u32 LastInputHealthSendGapFrame = 0;
    melonDS::u32 LastInputFrameThrottleTraceFrame = 0;
    unsigned long long RemoteInputWaitCount = 0;
    unsigned long long RemoteInputWaitLoops = 0;
    unsigned long long RemoteInputWaitUs = 0;
    unsigned long long RemoteInputWaitMaxUs = 0;
    unsigned long long FrameLeadThrottleCount = 0;
    unsigned long long FrameLeadThrottleLoops = 0;
    unsigned long long FrameLeadThrottleUs = 0;
    unsigned long long FrameLeadThrottleMaxUs = 0;
};

std::optional<ReplayFrameInputs> ResolveReplayFrameInputs(
    Runtime& runtime,
    melonDS::u32 frame,
    int localPlayer,
    const InputState& neutralInput,
    const PredictionProbe& probe = {});

class Recorder
{
public:
    Recorder() = default;
    ~Recorder();

    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    bool Open(
        const std::string& path,
        melonDS::u32 startFrame,
        melonDS::u32 endFrame,
        int instanceID);
    bool IsOpen() const;
    void Record(int instanceID, melonDS::u32 frame, const InputState& input);
    void Close();

private:
    void FlushSpanLocked();

    mutable std::mutex Mutex_;
    std::atomic<bool> Enabled_ { false };
    std::ofstream Output_;
    melonDS::u32 StartFrame_ = 0;
    melonDS::u32 EndFrame_ = 0;
    int InstanceID_ = -1;
    bool HasSpan_ = false;
    melonDS::u32 SpanStart_ = 0;
    melonDS::u32 SpanEnd_ = 0;
    InputState SpanInput_;
    int PendingSpans_ = 0;
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
