#ifndef NSMBPACKETBRIDGERUNTIME_H
#define NSMBPACKETBRIDGERUNTIME_H

#include "NsmbNetplayConfig.h"
#include "NsmbNetplayPoC.h"
#include "NsmbNetplayProtocol.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <map>
#include <optional>
#include <vector>

namespace NsmbNetplayPoC::PacketBridge {

inline constexpr melonDS::u32 kUnsetProgress = 0xFFFFFFFF;

struct ReceivedProgress {
  melonDS::u32 Tick = kUnsetProgress;
  melonDS::u32 Frame = kUnsetProgress;
};

enum class JitHookRestoreAction {
  Disabled,
  KeepApplied,
  Schedule,
};

struct JitHookRestoreResult {
  JitHookRestoreAction Action = JitHookRestoreAction::Disabled;
  melonDS::u32 ResumeFrame = 0;
};

InputState SelectPlayerInput(int player, int localPlayer,
                             const InputState &localInput,
                             const InputState &remoteInput,
                             bool hasRemoteInput);
melonDS::u32 CanonicalTick(const Config::PacketBridgeConfig &config,
                           melonDS::u32 frame, melonDS::u32 observedTick);
bool IsAcceptedIncomingPacket(const WireProtocol::WireNSMLPacket &packet,
                              melonDS::u32 restartCutoffFrame);

class Runtime {
public:
  using Clock = std::chrono::steady_clock;

  void ResetQueuesForRestart();
  void ResetStartupHookState(int instanceID);

  void StorePacketInput(melonDS::u32 frame, const InputState &input);
  std::optional<InputState> PacketInput(melonDS::u32 frame) const;
  void PrunePacketInputs(melonDS::u32 frame, melonDS::u32 historyFrames);
  std::size_t PacketInputCount() const;

  void QueuePendingPacket(const WireProtocol::WireNSMLPacket &packet);
  std::vector<WireProtocol::WireNSMLPacket> TakePendingPackets();
  std::size_t PendingPacketCount() const;

  std::optional<WireProtocol::WireNSMLPacket>
  PrepareOutgoingPacket(melonDS::u32 frame, melonDS::u32 player,
                        melonDS::u32 tick, const melonDS::u8 packetBytes[52],
                        const Config::PacketBridgeConfig &config,
                        Clock::time_point now);
  std::vector<WireProtocol::WireNSMLPacket>
  TakeDueOutgoingPackets(melonDS::u32 frame, Clock::time_point now);
  std::size_t DelayedPacketCount() const;

  bool RecordReceivedPacket(melonDS::u32 player, melonDS::u32 tick,
                            melonDS::u32 frame);
  ReceivedProgress ReceivedPacketProgress(melonDS::u32 player) const;
  bool ShouldTraceSentTick(melonDS::u32 tick);
  bool ShouldTraceFrameThrottle(melonDS::u32 frame);
  bool MarkForcedTickFrame(int instanceID, melonDS::u32 frame);

  JitHookRestoreResult
  ScheduleJitHookAfterRestore(int instanceID, melonDS::u32 restoreFrame,
                              melonDS::u32 checkpointFrame,
                              const Config::RuntimePatchConfig &config);
  bool ShouldApplyJitHook(int instanceID, melonDS::u32 frame,
                          melonDS::u32 configuredPatchFrame) const;
  void MarkJitHookApplied(int instanceID);
  bool IsJitHookApplied(int instanceID) const;
  melonDS::u32 JitHookResumeFrame(int instanceID) const;

private:
  struct DelayedPacket {
    melonDS::u32 ReleaseFrame = 0;
    Clock::time_point ReleaseTime{};
    WireProtocol::WireNSMLPacket Packet{};
  };

  std::map<melonDS::u32, InputState> PacketInputs_;
  std::vector<WireProtocol::WireNSMLPacket> PendingPackets_;
  std::vector<DelayedPacket> DelayedPackets_;
  melonDS::u32 LastSentTick_ = kUnsetProgress;
  std::array<ReceivedProgress, 2> ReceivedProgress_{};
  melonDS::u32 LastFrameThrottleFrame_ = kUnsetProgress;
  std::array<melonDS::u32, 16> LastForcedTickFrame_{};
  std::array<bool, 16> JitHookApplied_{};
  std::array<melonDS::u32, 16> JitHookResumeFrame_{};
};

} // namespace NsmbNetplayPoC::PacketBridge

#endif
