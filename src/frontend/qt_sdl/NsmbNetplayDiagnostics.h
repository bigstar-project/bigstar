#pragma once

#include "NsmbGameState.h"
#include "NsmbNetplayConfig.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace NsmbNetplayPoC::Diagnostics {

class Runtime {
public:
  using TimePoint = std::chrono::steady_clock::time_point;

  struct ActiveFrameSample {
    bool Recorded = false;
    bool Spike = false;
    std::uint64_t ElapsedUs = 0;
    melonDS::u32 RollbackRestoreDelta = 0;
    melonDS::u32 RollbackResimulateDelta = 0;
  };

  struct ActiveFrameSummary {
    bool Started = false;
    melonDS::u32 StartFrame = 0;
    melonDS::u32 Frames = 0;
    std::int64_t ElapsedMs = 0;
    melonDS::u32 Samples = 0;
    std::uint64_t TotalUs = 0;
    std::uint64_t MaxUs = 0;
    melonDS::u32 MaxFrame = 0;
    melonDS::u32 Over16ms = 0;
    melonDS::u32 Over25ms = 0;
    melonDS::u32 Over33ms = 0;
  };

  Runtime();
  ~Runtime();

  Runtime(const Runtime &) = delete;
  Runtime &operator=(const Runtime &) = delete;

  bool ConfigureFrameHeartbeat(int interval, const std::string &path);
  bool PublishFrameHeartbeat(int instanceID, melonDS::u32 frame, bool active);
  bool ConfigureHashLog(const std::string &path, bool screenHashEnabled);
  bool RecordFrameHash(int instanceID, melonDS::u32 frame,
                       melonDS::u64 stateHash, melonDS::u64 screenHash);
  bool WriteDiagnosticEvent(const std::string &path,
                            const std::string &json);
  void StartTestTimer(TimePoint now);
  std::int64_t TestElapsedMs(TimePoint now) const;
  bool StartActiveTimer(int instanceID, melonDS::u32 frame, TimePoint now);
  bool IsActiveTimerStarted(int instanceID) const;
  ActiveFrameSample RecordActiveFrameTiming(
      int instanceID, melonDS::u32 frame, TimePoint now, bool traceSpikes,
      std::uint64_t spikeThresholdUs, melonDS::u32 rollbackRestoreCount,
      melonDS::u32 rollbackResimulateCount);
  ActiveFrameSummary ActiveFrameTimingSummary(int instanceID,
                                              melonDS::u32 endFrame,
                                              TimePoint now) const;
  bool ShouldTraceGameplayHeartbeat(int instanceID, melonDS::u32 frame,
                                    melonDS::u32 startFrame, int interval);
  void StartHangDiagnostics(const Config::DiagnosticsConfig &config, bool host);
  void Stop();

  void TracePhase(const char *event, const char *phase, int instanceID = -1,
                  melonDS::u32 frame = 0, melonDS::u32 logicalFrame = 0,
                  melonDS::u32 sendFrame = 0);
  void UpdateNetplaySnapshot(melonDS::u32 lastSentFrame,
                             melonDS::u32 lastReceivedFrame,
                             melonDS::u32 frameForLead,
                             melonDS::u32 noFrameLimit, std::size_t localQueue,
                             std::size_t remoteQueue, std::size_t delayedQueue,
                             int peerState, int connectingPeerState);
  void ResetNetplaySnapshot(melonDS::u32 noFrameLimit);

  void RecordENetService(int result);
  void RecordENetEvent(int type, melonDS::u32 data);
  void RecordENetReceive(std::uint64_t unixMs);
  void RecordENetSend(int result, std::size_t bytes, std::uint64_t unixMs);

  void BeginRemoteWait(melonDS::u32 targetFrame, std::uint64_t unixMs);
  void ProgressRemoteWait(std::uint64_t unixMs);
  void EndRemoteWait();

  void UpdateGameSnapshot(int instanceID, melonDS::u32 frame,
                          const GameStateModel::GameStateSample &sample,
                          std::uint64_t unixMs);

private:
  struct Impl;
  std::unique_ptr<Impl> State;
};

} // namespace NsmbNetplayPoC::Diagnostics
