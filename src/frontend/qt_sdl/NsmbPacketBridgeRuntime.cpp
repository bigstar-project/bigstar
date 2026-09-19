#include "NsmbPacketBridgeRuntime.h"

#include <algorithm>
#include <cstring>

namespace NsmbMvlNetplay::PacketBridge {

namespace {

bool ValidInstance(int instanceID) {
  return instanceID >= 0 && instanceID < 16;
}

bool UpdateTraceMarker(melonDS::u32 value, melonDS::u32 &lastValue) {
  if (lastValue == value)
    return false;
  lastValue = value;
  return true;
}

} // namespace

void GameTickInputQueue::Reset(melonDS::u32 generation) {
  Generation_ = generation;
  Inputs_.clear();
  LastQueuedFrame_.reset();
  AppliedFrame_.reset();
}

bool GameTickInputQueue::Enqueue(const GameTickInput &input) {
  if (Inputs_.size() >= Capacity ||
      (LastQueuedFrame_ && input.Frame != *LastQueuedFrame_ + 1))
    return false;
  Inputs_.push_back(input);
  LastQueuedFrame_ = input.Frame;
  return true;
}

std::optional<GameTickInput> GameTickInputQueue::Consume() {
  if (Inputs_.empty())
    return std::nullopt;
  const auto input = Inputs_.front();
  Inputs_.pop_front();
  AppliedFrame_ = input.Frame;
  return input;
}

std::optional<std::array<melonDS::u32, 4>> BuildGameTickInputBoundaryPatch(
    melonDS::u32 address, const std::array<melonDS::u32, 4> &words,
    melonDS::u32 markerLiteral) {
  if ((address & 3) || address < 0x02001B50 || address > 0x02001CD0 ||
      markerLiteral != 0x04FFFA28)
    return std::nullopt;
  const auto callsInputUpdate = [](melonDS::u32 pc, melonDS::u32 word) {
    return (word & 0xFF000000) == 0xEB000000 &&
           pc + 8 + (static_cast<melonDS::s32>(word << 8) >> 6) == 0x02005230;
  };
  if ((words[0] & 0xFFFFF000) == 0xE59F2000 &&
      words[1] == 0xE3A01002 && words[2] == 0xE5821000 &&
      callsInputUpdate(address + 12, words[3]))
    return words; // Already installed, e.g. a restored patched checkpoint.
  if (!callsInputUpdate(address, words[0]) ||
      (words[1] & 0xFFFFF000) != 0xE59F2000 ||
      (words[1] & 0xFFF) > 0xFFB ||
      words[2] != 0xE3A01003 || words[3] != 0xE5821000)
    return std::nullopt;
  return std::array<melonDS::u32, 4>{
      words[1] + 4, 0xE3A01002, words[3], words[0] - 3};
}

InputState SelectPlayerInput(int player, int localPlayer,
                             const InputState &localInput,
                             const InputState &remoteInput,
                             bool hasRemoteInput) {
  if (player == localPlayer)
    return localInput;
  if (hasRemoteInput)
    return remoteInput;
  return {};
}

melonDS::u32 CanonicalTick(const Config::PacketBridgeConfig &config,
                           melonDS::u32 sharedLogicalEpoch,
                           melonDS::u32 frame, melonDS::u32 observedTick) {
  const melonDS::u32 startFrame = sharedLogicalEpoch != 0
                                      ? sharedLogicalEpoch
                                      : config.ForceTickStartFrame;
  if (!config.ForceTickEnabled || frame < startFrame ||
      config.ForceTickBase < 0)
    return observedTick;
  return (static_cast<melonDS::u32>(config.ForceTickBase) +
          (frame - startFrame)) &
         0xFFFF;
}

bool IsAcceptedIncomingPacket(const WireProtocol::WireNSMLPacket &packet,
                              melonDS::u32 expectedGeneration) {
  if (packet.Magic != WireProtocol::kMagic ||
      packet.Version != WireProtocol::kVersion ||
      packet.Kind != WireProtocol::kWireKindPacket || packet.Player > 1)
    return false;
  return packet.Generation == expectedGeneration;
}

bool ShouldWriteJitScratchInputs(bool jitHookApplied, bool inputNetplayOnly,
                                 bool inputEpochReady,
                                 melonDS::u32 sharedLogicalEpoch,
                                 melonDS::u32 logicalFrame) {
  if (!jitHookApplied)
    return false;
  if (!inputNetplayOnly)
    return true;
  return inputEpochReady && sharedLogicalEpoch != 0 &&
         logicalFrame >= sharedLogicalEpoch;
}

void Runtime::ResetQueuesForRestart() {
  PacketInputs_.clear();
  PendingPackets_.clear();
  DelayedPackets_.clear();
}

void Runtime::ResetStartupHookState(int instanceID) {
  if (!ValidInstance(instanceID))
    return;
  JitHookApplied_[instanceID] = false;
  JitHookResumeFrame_[instanceID] = 0;
  LastForcedTickFrame_[instanceID] = 0;
}

void Runtime::StorePacketInput(melonDS::u32 frame, const InputState &input) {
  PacketInputs_[frame] = input;
}

std::optional<InputState> Runtime::PacketInput(melonDS::u32 frame) const {
  auto input = PacketInputs_.find(frame);
  if (input == PacketInputs_.end() && frame > 0)
    input = PacketInputs_.find(frame - 1);
  if (input == PacketInputs_.end())
    return std::nullopt;
  return input->second;
}

void Runtime::PrunePacketInputs(melonDS::u32 frame,
                                melonDS::u32 historyFrames) {
  const melonDS::u32 keepFrom =
      frame > historyFrames ? frame - historyFrames : 0;
  for (auto input = PacketInputs_.begin(); input != PacketInputs_.end();) {
    if (input->first < keepFrom)
      input = PacketInputs_.erase(input);
    else
      ++input;
  }
}

std::size_t Runtime::PacketInputCount() const { return PacketInputs_.size(); }

void Runtime::QueuePendingPacket(const WireProtocol::WireNSMLPacket &packet) {
  PendingPackets_.push_back(packet);
}

std::vector<WireProtocol::WireNSMLPacket> Runtime::TakePendingPackets() {
  std::vector<WireProtocol::WireNSMLPacket> packets;
  packets.swap(PendingPackets_);
  return packets;
}

std::size_t Runtime::PendingPacketCount() const {
  return PendingPackets_.size();
}

std::optional<WireProtocol::WireNSMLPacket> Runtime::PrepareOutgoingPacket(
    melonDS::u32 generation, melonDS::u32 frame, melonDS::u32 player,
    melonDS::u32 tick,
    const melonDS::u8 packetBytes[52], const Config::PacketBridgeConfig &config,
    Clock::time_point now) {
  if (!packetBytes || player > 1)
    return std::nullopt;

  WireProtocol::WireNSMLPacket packet{};
  packet.Magic = WireProtocol::kMagic;
  packet.Version = WireProtocol::kVersion;
  packet.Kind = WireProtocol::kWireKindPacket;
  packet.Generation = generation;
  packet.Frame = frame;
  packet.Player = player;
  packet.Tick = tick;
  std::memcpy(packet.Packet, packetBytes, sizeof(packet.Packet));

  const int jitterFrames =
      config.SendJitterFrames > 0
          ? static_cast<int>(
                frame % static_cast<melonDS::u32>(config.SendJitterFrames + 1))
          : 0;
  const int sendDelayFrames = config.SendDelayFrames + jitterFrames;
  if (sendDelayFrames <= 0)
    return packet;

  DelayedPackets_.push_back(
      {frame + static_cast<melonDS::u32>(sendDelayFrames),
       now + std::chrono::milliseconds((sendDelayFrames * 1000 + 59) / 60),
       packet});
  return std::nullopt;
}

std::vector<WireProtocol::WireNSMLPacket>
Runtime::TakeDueOutgoingPackets(melonDS::u32 frame, Clock::time_point now) {
  std::vector<WireProtocol::WireNSMLPacket> packets;
  for (auto delayed = DelayedPackets_.begin();
       delayed != DelayedPackets_.end();) {
    if (delayed->ReleaseFrame <= frame || now >= delayed->ReleaseTime) {
      packets.push_back(delayed->Packet);
      delayed = DelayedPackets_.erase(delayed);
    } else {
      ++delayed;
    }
  }
  return packets;
}

std::size_t Runtime::DelayedPacketCount() const {
  return DelayedPackets_.size();
}

bool Runtime::RecordReceivedPacket(melonDS::u32 player, melonDS::u32 tick,
                                   melonDS::u32 frame) {
  if (player >= ReceivedProgress_.size())
    return false;
  ReceivedProgress &progress = ReceivedProgress_[player];
  const bool newTick = progress.Tick != tick;
  progress.Tick = tick;
  progress.Frame = frame;
  return newTick;
}

ReceivedProgress Runtime::ReceivedPacketProgress(melonDS::u32 player) const {
  if (player >= ReceivedProgress_.size())
    return {};
  return ReceivedProgress_[player];
}

bool Runtime::ShouldTraceSentTick(melonDS::u32 tick) {
  return UpdateTraceMarker(tick, LastSentTick_);
}

bool Runtime::ShouldTraceFrameThrottle(melonDS::u32 frame) {
  return UpdateTraceMarker(frame, LastFrameThrottleFrame_);
}

bool Runtime::MarkForcedTickFrame(int instanceID, melonDS::u32 frame) {
  if (!ValidInstance(instanceID) || LastForcedTickFrame_[instanceID] == frame)
    return false;
  LastForcedTickFrame_[instanceID] = frame;
  return true;
}

JitHookRestoreResult
Runtime::ScheduleJitHookAfterRestore(int instanceID, melonDS::u32 restoreFrame,
                                     melonDS::u32 checkpointFrame,
                                     const Config::RuntimePatchConfig &config) {
  if (!ValidInstance(instanceID))
    return {};

  JitHookResumeFrame_[instanceID] = 0;
  if (!config.PacketBridgeJitHelperPatchEnabled) {
    JitHookApplied_[instanceID] = false;
    return {};
  }
  if (checkpointFrame >= config.PacketBridgeJitHelperPatchFrame) {
    JitHookApplied_[instanceID] = true;
    return {JitHookRestoreAction::KeepApplied, 0};
  }

  melonDS::u32 delay = config.PacketBridgeJitHelperPatchFrame - checkpointFrame;
  if (delay > 6)
    delay -= 6;
  JitHookApplied_[instanceID] = false;
  JitHookResumeFrame_[instanceID] = restoreFrame + delay;
  return {JitHookRestoreAction::Schedule, JitHookResumeFrame_[instanceID]};
}

bool Runtime::ShouldApplyJitHook(int instanceID, melonDS::u32 frame,
                                 melonDS::u32 configuredPatchFrame) const {
  if (!ValidInstance(instanceID) || JitHookApplied_[instanceID])
    return false;
  melonDS::u32 patchFrame = configuredPatchFrame;
  if (JitHookResumeFrame_[instanceID] != 0)
    patchFrame = std::max(patchFrame, JitHookResumeFrame_[instanceID]);
  return frame >= patchFrame;
}

void Runtime::MarkJitHookApplied(int instanceID) {
  if (!ValidInstance(instanceID))
    return;
  JitHookApplied_[instanceID] = true;
  JitHookResumeFrame_[instanceID] = 0;
}

bool Runtime::IsJitHookApplied(int instanceID) const {
  return ValidInstance(instanceID) && JitHookApplied_[instanceID];
}

melonDS::u32 Runtime::JitHookResumeFrame(int instanceID) const {
  return ValidInstance(instanceID) ? JitHookResumeFrame_[instanceID] : 0;
}

} // namespace NsmbMvlNetplay::PacketBridge
