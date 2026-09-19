#include "NsmbPacketBridgeRuntime.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using NsmbMvlNetplay::InputState;
using NsmbMvlNetplay::Config::PacketBridgeConfig;
using NsmbMvlNetplay::Config::RuntimePatchConfig;
using NsmbMvlNetplay::PacketBridge::CanonicalTick;
using NsmbMvlNetplay::PacketBridge::IsAcceptedIncomingPacket;
using NsmbMvlNetplay::PacketBridge::JitHookRestoreAction;
using NsmbMvlNetplay::PacketBridge::kUnsetProgress;
using NsmbMvlNetplay::PacketBridge::Runtime;
using NsmbMvlNetplay::PacketBridge::SelectPlayerInput;
using NsmbMvlNetplay::PacketBridge::ShouldWriteJitScratchInputs;
using NsmbMvlNetplay::WireProtocol::WireNSMLPacket;

void Require(bool condition, const std::string &message) {
  if (condition)
    return;
  std::cerr << "FAIL: " << message << '\n';
  std::exit(1);
}

WireNSMLPacket MakePacket(melonDS::u32 frame, melonDS::u32 player,
                          melonDS::u32 tick) {
  WireNSMLPacket packet{};
  packet.Generation = 1;
  packet.Frame = frame;
  packet.Player = player;
  packet.Tick = tick;
  return packet;
}

void TestPacketInputHistory() {
  Runtime runtime;
  Require(!runtime.PacketInput(0), "packet input starts empty");

  InputState first;
  first.KeyMask = 0x12;
  runtime.StorePacketInput(10, first);
  Require(runtime.PacketInput(10) && runtime.PacketInput(10)->KeyMask == 0x12,
          "exact packet input is returned");
  Require(runtime.PacketInput(11) && runtime.PacketInput(11)->KeyMask == 0x12,
          "previous frame input is the sole fallback");
  Require(!runtime.PacketInput(12), "fallback does not reach two frames back");

  InputState recent;
  recent.KeyMask = 0x34;
  runtime.StorePacketInput(300, recent);
  runtime.PrunePacketInputs(300, 240);
  Require(runtime.PacketInputCount() == 1 && runtime.PacketInput(300) &&
              runtime.PacketInput(300)->KeyMask == 0x34,
          "input pruning keeps the configured history boundary");
}

void TestInputSelectionAndCanonicalTick() {
  InputState local;
  local.KeyMask = 0x111;
  InputState remote;
  remote.KeyMask = 0x222;
  Require(SelectPlayerInput(0, 0, local, remote, true).KeyMask == 0x111 &&
              SelectPlayerInput(1, 0, local, remote, true).KeyMask == 0x222,
          "player input selection maps local and available remote input");
  Require(SelectPlayerInput(1, 0, local, remote, false).KeyMask == 0xFFF,
          "missing remote input falls back to neutral active-low keys");

  PacketBridgeConfig config;
  Require(CanonicalTick(config, 0, 100, 0x1234) == 0x1234,
          "disabled forced tick preserves the observed game tick");
  config.ForceTickEnabled = true;
  config.ForceTickStartFrame = 100;
  config.ForceTickBase = 0xFFF0;
  Require(CanonicalTick(config, 0, 99, 0x1234) == 0x1234 &&
              CanonicalTick(config, 0, 100, 0x1234) == 0xFFF0 &&
              CanonicalTick(config, 0, 132, 0x1234) == 0x0010 &&
              CanonicalTick(config, 200, 200, 0x1234) == 0xFFF0,
          "forced tick starts on its configured frame and wraps to 16 bits");
}

void TestIncomingPacketPolicy() {
  WireNSMLPacket packet{};
  packet.Magic = NsmbMvlNetplay::WireProtocol::kMagic;
  packet.Version = NsmbMvlNetplay::WireProtocol::kVersion;
  packet.Kind = NsmbMvlNetplay::WireProtocol::kWireKindPacket;
  packet.Generation = 4;
  packet.Frame = 100;
  packet.Player = 1;
  Require(IsAcceptedIncomingPacket(packet, 4),
          "packet from the current generation is accepted");
  Require(!IsAcceptedIncomingPacket(packet, 3) &&
              !IsAcceptedIncomingPacket(packet, 5),
          "packet from another generation is rejected");
  packet.Player = 2;
  Require(!IsAcceptedIncomingPacket(packet, 4),
          "out-of-range player packet is rejected");
  packet.Player = 1;
  packet.Version++;
  Require(!IsAcceptedIncomingPacket(packet, 4),
          "wire contract mismatch is rejected");
}

void TestPacketQueuesAndDelay() {
  Runtime runtime;
  runtime.QueuePendingPacket(MakePacket(10, 0, 1));
  runtime.QueuePendingPacket(MakePacket(11, 1, 2));
  Require(runtime.PendingPacketCount() == 2, "pending packets accumulate");
  const auto pending = runtime.TakePendingPackets();
  Require(pending.size() == 2 && pending[0].Frame == 10 &&
              pending[1].Frame == 11 && runtime.PendingPacketCount() == 0,
          "pending packet take preserves order and drains the queue");

  melonDS::u8 bytes[52]{};
  bytes[2] = 0xAA;
  PacketBridgeConfig config;
  const auto now = Runtime::Clock::time_point{};
  const auto immediate =
      runtime.PrepareOutgoingPacket(4, 5, 1, 9, bytes, config, now);
  Require(immediate && immediate->Frame == 5 && immediate->Player == 1 &&
              immediate->Tick == 9 && immediate->Generation == 4 &&
              immediate->Packet[2] == 0xAA,
          "zero-delay packet is encoded for immediate sending");
  Require(!runtime.PrepareOutgoingPacket(4, 5, 2, 9, bytes, config, now),
          "invalid player is rejected");

  config.SendDelayFrames = 2;
  config.SendJitterFrames = 3;
  Require(!runtime.PrepareOutgoingPacket(4, 5, 0, 10, bytes, config, now) &&
              runtime.DelayedPacketCount() == 1,
          "configured delay plus deterministic jitter queues the packet");
  Require(runtime.TakeDueOutgoingPackets(7, now + std::chrono::milliseconds(49))
              .empty(),
          "delayed packet remains queued before both deadlines");
  const auto dueByTime =
      runtime.TakeDueOutgoingPackets(7, now + std::chrono::milliseconds(50));
  Require(dueByTime.size() == 1 && dueByTime[0].Tick == 10,
          "wall-clock deadline releases delayed packet");

  Require(!runtime.PrepareOutgoingPacket(4, 8, 0, 11, bytes, config, now),
          "second delayed packet is queued");
  const auto dueByFrame = runtime.TakeDueOutgoingPackets(10, now);
  Require(dueByFrame.size() == 1 && dueByFrame[0].Tick == 11,
          "frame deadline independently releases delayed packet");
}

void TestProgressAndTraceMarkers() {
  Runtime runtime;
  Require(runtime.ReceivedPacketProgress(0).Tick == kUnsetProgress &&
              runtime.ReceivedPacketProgress(3).Frame == kUnsetProgress,
          "received progress starts and remains unset for invalid players");
  Require(runtime.RecordReceivedPacket(0, 20, 100),
          "first received tick is new");
  Require(!runtime.RecordReceivedPacket(0, 20, 101),
          "same received tick is not new");
  Require(runtime.ReceivedPacketProgress(0).Frame == 101 &&
              runtime.RecordReceivedPacket(1, 20, 200),
          "frame still advances and player progress is independent");

  Require(runtime.ShouldTraceSentTick(30) && !runtime.ShouldTraceSentTick(30) &&
              runtime.ShouldTraceSentTick(31),
          "sent tick trace is emitted once per value");
  Require(runtime.ShouldTraceFrameThrottle(60) &&
              !runtime.ShouldTraceFrameThrottle(60),
          "frame throttle trace is deduplicated");

  Require(!runtime.MarkForcedTickFrame(0, 0),
          "frame zero retains the old already-marked sentinel behavior");
  Require(runtime.MarkForcedTickFrame(0, 1) &&
              !runtime.MarkForcedTickFrame(0, 1) &&
              runtime.MarkForcedTickFrame(1, 1) &&
              !runtime.MarkForcedTickFrame(16, 1),
          "forced tick marker deduplicates per valid instance");
}

void TestJitHookRestorePolicy() {
  Runtime runtime;
  RuntimePatchConfig config;
  auto result = runtime.ScheduleJitHookAfterRestore(0, 4000, 700, config);
  Require(result.Action == JitHookRestoreAction::Disabled &&
              !runtime.IsJitHookApplied(0),
          "disabled JIT hook remains unapplied after restore");

  config.PacketBridgeJitHelperPatchEnabled = true;
  config.PacketBridgeJitHelperPatchFrame = 840;
  result = runtime.ScheduleJitHookAfterRestore(0, 4000, 900, config);
  Require(result.Action == JitHookRestoreAction::KeepApplied &&
              runtime.IsJitHookApplied(0) &&
              !runtime.ShouldApplyJitHook(0, 5000, 840),
          "checkpoint captured after patch keeps the hook applied");

  runtime.ResetStartupHookState(0);
  result = runtime.ScheduleJitHookAfterRestore(0, 4000, 700, config);
  Require(result.Action == JitHookRestoreAction::Schedule &&
              result.ResumeFrame == 4134 &&
              runtime.JitHookResumeFrame(0) == 4134 &&
              !runtime.ShouldApplyJitHook(0, 4133, 840) &&
              runtime.ShouldApplyJitHook(0, 4134, 840),
          "pre-patch checkpoint preserves the six-frame restore lead policy");
  runtime.MarkJitHookApplied(0);
  Require(runtime.IsJitHookApplied(0) && runtime.JitHookResumeFrame(0) == 0,
          "applying the hook clears its resume schedule");

  result = runtime.ScheduleJitHookAfterRestore(1, 4000, 839, config);
  Require(result.Action == JitHookRestoreAction::Schedule &&
              result.ResumeFrame == 4001,
          "short restore delay is not reduced below one frame");
}

void TestJitScratchInputGateUsesLogicalEpochAfterRestart() {
  Require(!ShouldWriteJitScratchInputs(false, true, true, 840, 840),
          "scratch input waits for the raw-frame JIT hook application");
  Require(ShouldWriteJitScratchInputs(true, false, false, 0, 0),
          "non-input-netplay scratch is available once the hook is applied");
  Require(!ShouldWriteJitScratchInputs(true, true, false, 840, 840),
          "input netplay waits for the accepted and primed input epoch");
  Require(!ShouldWriteJitScratchInputs(true, true, true, 840, 839) &&
              ShouldWriteJitScratchInputs(true, true, true, 840, 840),
          "input netplay opens on the shared logical epoch");

  Runtime restarted;
  RuntimePatchConfig config;
  config.PacketBridgeJitHelperPatchEnabled = true;
  config.PacketBridgeJitHelperPatchFrame = 2652;
  Require(restarted.ShouldApplyJitHook(
              0, 2652, config.PacketBridgeJitHelperPatchFrame),
          "restart reaches the rebased raw JIT hook frame");
  restarted.MarkJitHookApplied(0);
  Require(ShouldWriteJitScratchInputs(restarted.IsJitHookApplied(0), true,
                                      true, 840, 840),
          "restart writes scratch at logical epoch 840 even when the raw hook "
          "frame is 2652");

  Runtime lateRestart;
  config.PacketBridgeJitHelperPatchFrame = 6887;
  Require(lateRestart.ShouldApplyJitHook(
              0, 6887, config.PacketBridgeJitHelperPatchFrame),
          "late restart reaches its rebased raw JIT hook frame");
  lateRestart.MarkJitHookApplied(0);
  Require(ShouldWriteJitScratchInputs(lateRestart.IsJitHookApplied(0), true,
                                      true, 840, 840),
          "logical input application is independent of a later raw restart "
          "frame");
}

void TestRestartQueueReset() {
  Runtime runtime;
  InputState input;
  runtime.StorePacketInput(1, input);
  runtime.QueuePendingPacket(MakePacket(1, 0, 1));
  PacketBridgeConfig config;
  config.SendDelayFrames = 1;
  melonDS::u8 bytes[52]{};
  runtime.PrepareOutgoingPacket(1, 1, 0, 1, bytes, config,
                                Runtime::Clock::time_point{});
  runtime.RecordReceivedPacket(0, 7, 8);

  runtime.ResetQueuesForRestart();
  Require(runtime.PacketInputCount() == 0 &&
              runtime.PendingPacketCount() == 0 &&
              runtime.DelayedPacketCount() == 0,
          "restart clears all packet queues");
  Require(runtime.ReceivedPacketProgress(0).Tick == 7,
          "restart retains received progress like the previous implementation");
}

} // namespace

void TestGameTickInputBoundaries() {
  using NsmbMvlNetplay::PacketBridge::GameTickInput;
  using NsmbMvlNetplay::PacketBridge::GameTickInputQueue;
  GameTickInputQueue host;
  GameTickInputQueue client;
  host.Reset(3);
  client.Reset(3);
  GameTickInput pressed{11382, InputState{}, InputState{}, true};
  pressed.Remote.KeyMask = 0x7EF;
  pressed.Local.Touching = true;
  pressed.Local.TouchX = 217;
  pressed.Local.TouchY = 153;
  auto released = pressed;
  released.Frame = 11383;
  released.Remote.KeyMask = 0x7FF;
  released.Local.Touching = false;
  Require(host.Enqueue(pressed) && client.Enqueue(pressed), "queue first tick");
  const auto firstHost = host.Consume();
  const auto firstClient = client.Consume();
  Require(firstHost && firstClient && firstHost->Local.Touching &&
              firstHost->Local.TouchX == 217 && firstHost->Local.TouchY == 153,
          "touch coordinates survive deferred delivery");
  Require(host.Enqueue(released) && client.Enqueue(released), "queue release");
  const auto secondClient = client.Consume();
  auto next = released;
  next.Frame++;
  Require(host.Enqueue(next) && client.Enqueue(next), "display advances during host update");
  Require(host.AppliedFrame() == pressed.Frame && host.PendingCount() == 2,
          "a display frame cannot overwrite the in-progress game tick");
  const auto secondHost = host.Consume();
  Require(secondHost && secondClient && secondHost->Frame == secondClient->Frame &&
              secondHost->Remote.KeyMask == secondClient->Remote.KeyMask &&
              !secondHost->Local.Touching,
          "late game update consumes the same release, without skipping it");
  Require(!host.Enqueue(next), "duplicate capture cannot create an extra game tick");
  next.Frame += 2;
  Require(!host.Enqueue(next), "missing input cannot silently advance simulation");
  host.Reset(4);
  Require(!host.Consume() && !host.AppliedFrame() && host.PendingCount() == 0 &&
              host.Generation() == 4,
          "rematch discards previous generation inputs and applied frame");
  pressed.Frame = 840;
  Require(host.Enqueue(pressed), "rematch accepts a new logical epoch");
  Require(host.Consume().has_value() && !host.Consume(),
          "underflow cannot repeat the previous button edge");
  host.Reset(5);
  for (std::size_t i = 0; i < GameTickInputQueue::Capacity; ++i) {
    pressed.Frame = static_cast<melonDS::u32>(i);
    Require(host.Enqueue(pressed), "bounded backlog accepts available capacity");
  }
  pressed.Frame++;
  Require(!host.Enqueue(pressed), "stalled guest cannot allocate an unlimited backlog");
}

void TestGameTickInputGatePatch() {
  using NsmbMvlNetplay::PacketBridge::BuildGameTickInputBoundaryPatch;
  constexpr melonDS::u32 address = 0x02001C50;
  constexpr melonDS::u32 call = 0xEB000000 | ((0x02005230 - address - 8) >> 2);
  const std::array<melonDS::u32, 4> original{call, 0xE59F2040, 0xE3A01003, 0xE5821000};
  const auto patched = BuildGameTickInputBoundaryPatch(address, original, 0x04FFFA28);
  Require(patched.has_value(), "recognized GTP2 input gate can be patched");
  Require(address + 8 + ((*patched)[0] & 0xFFF) == address + 12 + (original[1] & 0xFFF),
          "relocated literal load still points at the marker address");
  Require(address + 12 + 8 + (((*patched)[3] & 0xFFFFFF) << 2) == 0x02005230 &&
              (*patched)[1] == 0xE3A01002 && (*patched)[2] == 0xE5821000,
          "input-begin marker precedes the original InputUpdate call");
  Require(BuildGameTickInputBoundaryPatch(address, *patched, 0x04FFFA28) == patched,
          "restored patched checkpoint is accepted idempotently");
  auto damaged = original;
  damaged[0]++;
  Require(!BuildGameTickInputBoundaryPatch(address, damaged, 0x04FFFA28),
          "wrong call destination is rejected before guest mutation");
  Require(!BuildGameTickInputBoundaryPatch(address, original, 0x12345678) &&
              !BuildGameTickInputBoundaryPatch(0x02001000, original, 0x04FFFA28),
          "unrecognized marker and code range are rejected");
}

int main() {
  TestGameTickInputBoundaries();
  TestGameTickInputGatePatch();
  TestPacketInputHistory();
  TestInputSelectionAndCanonicalTick();
  TestIncomingPacketPolicy();
  TestPacketQueuesAndDelay();
  TestProgressAndTraceMarkers();
  TestJitHookRestorePolicy();
  TestJitScratchInputGateUsesLogicalEpochAfterRestart();
  TestRestartQueueReset();
  std::cout << "nsmb_packet_bridge_runtime_tests: pass\n";
  return 0;
}
