#pragma once

#include "NsmbGameState.h"
#include "NsmbNetplayConfig.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace NsmbNetplayPoC::Diagnostics {

class HangRuntime {
public:
  HangRuntime();
  ~HangRuntime();

  HangRuntime(const HangRuntime &) = delete;
  HangRuntime &operator=(const HangRuntime &) = delete;

  void Start(const Config::DiagnosticsConfig &config, bool host);
  void Stop();

  void TracePhase(const char *event, const char *phase, int instanceID = -1,
                  melonDS::u32 frame = 0,
                  melonDS::u32 logicalFrame = 0,
                  melonDS::u32 sendFrame = 0);
  void UpdateNetplaySnapshot(melonDS::u32 lastSentFrame,
                             melonDS::u32 lastReceivedFrame,
                             melonDS::u32 frameForLead,
                             melonDS::u32 noFrameLimit,
                             std::size_t localQueue,
                             std::size_t remoteQueue,
                             std::size_t delayedQueue, int peerState,
                             int connectingPeerState);
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
  std::unique_ptr<Impl> Runtime;
};

} // namespace NsmbNetplayPoC::Diagnostics
