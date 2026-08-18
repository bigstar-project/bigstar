#include "NsmbPacketBridgeIntegration.h"

#include "NsmbTraceOutput.h"

#include "ARM.h"
#include "NDS.h"
#include "NsmbNetplayTransport.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <optional>
#include <thread>

namespace NsmbMvlNetplay::PacketBridge {
namespace {

constexpr melonDS::u32 kGameStageGroupAddr = 0x02085A18;
constexpr melonDS::u32 kGameLocalPlayerIDAddr = 0x02085A7C;
constexpr melonDS::u32 kGameVsModeAddr = 0x02085A84;
constexpr melonDS::u32 kNetPacketTickAddr = 0x020888E0;
constexpr melonDS::u32 kNetPacketActionAddr = 0x020888E4;
constexpr melonDS::u32 kPacketBridgeJitScratchBaseAddr = 0x023C1200;
constexpr melonDS::u32 kPacketBridgeJitScratchTickAddr =
    kPacketBridgeJitScratchBaseAddr;
constexpr melonDS::u32 kPacketBridgeJitScratchActionAddr =
    kPacketBridgeJitScratchBaseAddr + 0x04;
constexpr melonDS::u32 kPacketBridgeJitScratchKeysAddr =
    kPacketBridgeJitScratchBaseAddr + 0x08;
constexpr melonDS::u32 kPacketBridgeJitScratchPacketsAddr =
    kPacketBridgeJitScratchBaseAddr + 0x40;

bool IsMarioVsLuigiGameplay(melonDS::NDS *nds) {
  return nds && nds->ARM9Read32(kGameStageGroupAddr) == 9 &&
         nds->ARM9Read32(kGameVsModeAddr) == 1;
}

void SendWirePacketNowLocked(IntegrationContext context,
                             const WireProtocol::WireNSMLPacket &packet) {
  context.Transport.Send(&packet, sizeof(packet), ENET_PACKET_FLAG_RELIABLE,
                         true);
}

void FlushDelayedPacketsLocked(IntegrationContext context, melonDS::u32 frame) {
  if (!context.Config.Enabled || !context.Transport.IsConnected() ||
      context.State.DelayedPacketCount() == 0)
    return;

  for (const WireProtocol::WireNSMLPacket &packet :
       context.State.TakeDueOutgoingPackets(frame,
                                            std::chrono::steady_clock::now()))
    SendWirePacketNowLocked(context, packet);
}

void SendPacketLocked(IntegrationContext context, const IntegrationHooks &hooks,
                      melonDS::u32 frame, melonDS::u32 player,
                      melonDS::u32 tick, const melonDS::u8 packetBytes[52]) {
  if (!context.Config.Enabled || !context.Transport.IsConnected() ||
      !packetBytes || player > 1)
    return;

  hooks.SendMatchSeedLocked();
  const std::optional<WireProtocol::WireNSMLPacket> immediate =
      context.State.PrepareOutgoingPacket(context.Generation, frame, player,
                                          tick, packetBytes,
                                          context.Config,
                                          std::chrono::steady_clock::now());
  if (immediate)
    SendWirePacketNowLocked(context, *immediate);

  if (context.Config.TraceEnabled && context.State.ShouldTraceSentTick(tick)) {
    const melonDS::u32 keys = packetBytes[2] | (packetBytes[3] << 8);
    TraceOutput::Printf(
        "NSMB PacketBridge: send player=%u tick=0x%04X keys=0x%04X "
        "action=0x%02X b5=0x%02X b6=0x%02X b7=0x%02X bit=0x%02X "
        "frame=%u\n",
        player, tick, keys, packetBytes[4], packetBytes[5], packetBytes[6],
        packetBytes[7], packetBytes[0x29], frame);
  }
}

bool WriteARM9U32(melonDS::NDS *nds, melonDS::u32 address, melonDS::u32 value) {
  if (!nds || !nds->MainRAM || address < 0x02000000)
    return false;
  const melonDS::u32 offset = address - 0x02000000;
  const melonDS::u32 length = nds->MainRAMMask + 1;
  if (offset >= length || sizeof(value) > length - offset)
    return false;
  nds->ARM9Write32(address, value);
  return true;
}

} // namespace

melonDS::u32 LocalPlayerID(IntegrationContext context, melonDS::NDS *nds) {
  if (!nds)
    return 0;
  if (context.Config.Enabled && context.Config.LocalPlayerOverride >= 0 &&
      context.Config.LocalPlayerOverride <= 1)
    return static_cast<melonDS::u32>(context.Config.LocalPlayerOverride);
  if (context.Config.Enabled && context.Config.AllowPreGame &&
      !IsMarioVsLuigiGameplay(nds))
    return static_cast<melonDS::u32>(context.Connection.LocalInstance & 1);
  return nds->ARM9Read32(kGameLocalPlayerIDAddr) & 1;
}

void ReceivePacketLocked(IntegrationContext context, const void *data,
                         std::size_t size, melonDS::NDS *nds,
                         melonDS::u32 localFrame) {
  if (!data || size != sizeof(WireProtocol::WireNSMLPacket))
    return;
  WireProtocol::WireNSMLPacket packet;
  std::memcpy(&packet, data, size);
  if (!IsAcceptedIncomingPacket(packet, context.Generation))
    return;

  if (context.Config.Enabled && nds)
    melonDS::NSML_PushMarioVsLuigiRemotePacket(nds, packet.Player,
                                               packet.Packet);
  else
    context.State.QueuePendingPacket(packet);
  const bool newTick = context.State.RecordReceivedPacket(
      packet.Player, packet.Tick, packet.Frame);
  if (context.Config.TraceEnabled && newTick) {
    const melonDS::u32 keys = packet.Packet[2] | (packet.Packet[3] << 8);
    TraceOutput::Printf(
        "NSMB PacketBridge: recv player=%u tick=0x%04X keys=0x%04X "
        "action=0x%02X b5=0x%02X b6=0x%02X b7=0x%02X bit=0x%02X "
        "remoteFrame=%u localFrame=%u pending=%zu\n",
        packet.Player, packet.Tick, keys, packet.Packet[4], packet.Packet[5],
        packet.Packet[6], packet.Packet[7], packet.Packet[0x29], packet.Frame,
        localFrame, context.State.PendingPacketCount());
  }
}

void ApplyPendingPacketsLocked(IntegrationContext context, melonDS::NDS *nds) {
  if (!context.Config.Enabled || !nds ||
      context.State.PendingPacketCount() == 0)
    return;
  for (const WireProtocol::WireNSMLPacket &packet :
       context.State.TakePendingPackets())
    melonDS::NSML_PushMarioVsLuigiRemotePacket(nds, packet.Player,
                                               packet.Packet);
}

void PumpLocked(IntegrationContext context, const IntegrationHooks &hooks,
                melonDS::NDS *nds, melonDS::u32 frame) {
  FlushDelayedPacketsLocked(context, frame);
  hooks.PumpNetworkLocked(nds, frame);
  ApplyPendingPacketsLocked(context, nds);
  hooks.SendMatchSeedLocked();
}

void CaptureAndSendPacketLocked(IntegrationContext context,
                                const IntegrationHooks &hooks,
                                melonDS::u32 frame, melonDS::NDS *nds) {
  if (!context.Config.Enabled || !nds)
    return;

  melonDS::u8 packet[52]{};
  melonDS::u32 tick = 0;
  melonDS::u32 keys = 0;
  bool captured =
      melonDS::NSML_TakeMarioVsLuigiLocalPacket(nds, packet, &tick, &keys);
  if (!captured && context.Config.DirectCaptureEnabled)
    captured =
        melonDS::NSML_BuildMarioVsLuigiLocalPacket(nds, packet, &tick, &keys);
  if (!captured)
    return;

  const std::optional<InputState> overrideInput =
      context.State.PacketInput(frame);
  if (overrideInput) {
    keys = (~overrideInput->KeyMask) & 0x0FFF;
    packet[2] = static_cast<melonDS::u8>(keys & 0xFF);
    packet[3] = static_cast<melonDS::u8>((keys >> 8) & 0xFF);
  }
  if (context.Config.ForceTickEnabled &&
      frame >= context.Connection.SharedLogicalEpoch &&
      context.Config.ForceTickBase >= 0) {
    tick = CanonicalTick(context.Config,
                         context.Connection.SharedLogicalEpoch, frame, tick);
    packet[0] = static_cast<melonDS::u8>(tick & 0xFF);
    packet[1] = static_cast<melonDS::u8>((tick >> 8) & 0xFF);
  }

  const melonDS::u32 localPlayer = LocalPlayerID(context, nds);
  melonDS::NSML_PushMarioVsLuigiRemotePacket(nds, localPlayer, packet);
  SendPacketLocked(context, hooks, frame, localPlayer, tick, packet);
  context.State.PrunePacketInputs(frame, 240);
}

void ForceTickIfNeeded(IntegrationContext context, int instanceID,
                       melonDS::u32 frame, melonDS::NDS *nds) {
  if (!context.Config.ForceTickEnabled || !nds || instanceID < 0 ||
      instanceID >= 16 ||
      frame < context.Connection.SharedLogicalEpoch ||
      !context.State.MarkForcedTickFrame(instanceID, frame))
    return;
  const melonDS::u32 tick =
      CanonicalTick(context.Config, context.Connection.SharedLogicalEpoch,
                    frame, nds->ARM9Read16(kNetPacketTickAddr));
  nds->ARM9Write16(kNetPacketTickAddr, static_cast<melonDS::u16>(tick));
  if (context.Config.TraceEnabled && (frame % 60) == 0) {
    TraceOutput::Printf("NSMB PacketBridge: force tick=0x%04X frame=%u\n", tick,
                        frame);
  }
}

void ForceGameLocalPlayerIDIfNeeded(IntegrationContext context,
                                    melonDS::u32 frame, melonDS::NDS *nds) {
  if (!nds || context.Config.ForceGameLocalPlayerID < 0 ||
      frame < context.Config.ForceGameLocalPlayerIDStartFrame)
    return;
  if (!context.Config.ForceGameLocalPlayerIDEarly &&
      !IsMarioVsLuigiGameplay(nds))
    return;
  nds->ARM9Write32(
      kGameLocalPlayerIDAddr,
      static_cast<melonDS::u32>(context.Config.ForceGameLocalPlayerID & 1));
}

void ThrottleFrameLead(IntegrationContext context,
                       const IntegrationHooks &hooks, melonDS::NDS *nds,
                       melonDS::u32 frame) {
  if (context.Config.MaxFrameLead < 0 || !nds ||
      frame < context.Connection.SharedLogicalEpoch)
    return;

  const melonDS::u32 remotePlayer = LocalPlayerID(context, nds) ^ 1;
  const auto start = std::chrono::steady_clock::now();
  for (;;) {
    melonDS::u32 remoteTick = kUnsetProgress;
    melonDS::u32 remoteFrame = kUnsetProgress;
    {
      std::lock_guard<std::mutex> lock(context.Mutex);
      const ReceivedProgress progress =
          context.State.ReceivedPacketProgress(remotePlayer);
      remoteTick = progress.Tick;
      remoteFrame = progress.Frame;
    }
    if (remoteFrame == kUnsetProgress ||
        remoteFrame < context.Connection.SharedLogicalEpoch)
      return;
    const int lead = static_cast<int>(frame) - static_cast<int>(remoteFrame);
    if (lead <= context.Config.MaxFrameLead)
      return;

    if (context.Config.TraceEnabled &&
        context.State.ShouldTraceFrameThrottle(frame)) {
      TraceOutput::Printf(
          "NSMB PacketBridge: frame throttle frame=%u remotePlayer=%u "
          "remoteFrame=%u lead=%d maxLead=%d remoteTick=0x%04X\n",
          frame, remotePlayer, remoteFrame, lead, context.Config.MaxFrameLead,
          remoteTick & 0xFFFF);
    }
    {
      std::lock_guard<std::mutex> lock(context.Mutex);
      PumpLocked(context, hooks, nds, frame);
    }
    if (context.Config.ThrottleTimeoutMs > 0) {
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - start)
              .count();
      if (elapsed >= context.Config.ThrottleTimeoutMs) {
        if (context.Config.TraceEnabled) {
          TraceOutput::Printf(
              "NSMB PacketBridge: frame throttle timeout frame=%u "
              "remoteFrame=%u lead=%d waitedMs=%d\n",
              frame, remoteFrame, lead, context.Config.ThrottleTimeoutMs);
        }
        return;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

void WriteJitScratchInputs(IntegrationContext context,
                           const IntegrationHooks &hooks, int instanceID,
                           melonDS::u32 frame, melonDS::NDS *nds,
                           int localPlayer, const InputState &localInput,
                           const InputState &remoteInput, bool hasRemoteInput,
                           bool predictedRemoteInput) {
  if (!nds || !nds->MainRAM)
    return;

  melonDS::u32 tick = nds->ARM9Read16(kNetPacketTickAddr);
  if (context.Input.NetplayOnly &&
      context.Connection.SharedLogicalEpoch != 0 &&
      frame >= context.Connection.SharedLogicalEpoch) {
    tick = (frame - context.Connection.SharedLogicalEpoch) & 0xFFFF;
    nds->ARM9Write16(kNetPacketTickAddr, static_cast<melonDS::u16>(tick));
  }
  const melonDS::u8 action = nds->ARM9Read8(kNetPacketActionAddr);
  nds->ARM9Write16(kPacketBridgeJitScratchTickAddr,
                   static_cast<melonDS::u16>(tick));
  nds->ARM9Write8(kPacketBridgeJitScratchActionAddr, action);

  for (int player = 0; player < 2; player++) {
    InputState input = hooks.PrepareRuntimeInput(SelectPlayerInput(
        player, localPlayer, localInput, remoteInput, hasRemoteInput));
    input = hooks.ApplyAutomatedInput(instanceID, frame, nds, player, input);
    hooks.RecordAppliedInput(instanceID, frame, player, input);
    const melonDS::u32 keys = (~input.KeyMask) & 0x0FFF;
    nds->ARM9Write16(kPacketBridgeJitScratchKeysAddr +
                         static_cast<melonDS::u32>(player * 2),
                     static_cast<melonDS::u16>(keys));

    melonDS::u8 packet[52]{};
    packet[0] = static_cast<melonDS::u8>(tick & 0xFF);
    packet[1] = static_cast<melonDS::u8>((tick >> 8) & 0xFF);
    packet[2] = static_cast<melonDS::u8>(keys & 0xFF);
    packet[3] = static_cast<melonDS::u8>((keys >> 8) & 0xFF);
    packet[4] = action;
    packet[5] = input.Touching ? 1 : 0;
    packet[6] = static_cast<melonDS::u8>(std::min<int>(input.TouchX, 255));
    packet[7] = static_cast<melonDS::u8>(std::min<int>(input.TouchY, 191));
    for (melonDS::u32 index = 0; index < 44; index++)
      packet[8 + index] = nds->ARM9Read8(0x020888E8 + index);
    packet[0x29] = nds->ARM9Read8(0x02088A4C);

    const melonDS::u32 packetAddress = kPacketBridgeJitScratchPacketsAddr +
                                       static_cast<melonDS::u32>(player * 0x40);
    for (melonDS::u32 index = 0; index < sizeof(packet); index++)
      nds->ARM9Write8(packetAddress + index, packet[index]);

    // This is trace/scratch data only. Feeding the synthetic packet into the
    // lower queue corrupts NSMB's stage state on the JIT-helper route.
  }

  if (context.Input.NetplayTrace && (frame % 60) == 0) {
    const melonDS::u16 keys0 = nds->ARM9Read16(kPacketBridgeJitScratchKeysAddr);
    const melonDS::u16 keys1 =
        nds->ARM9Read16(kPacketBridgeJitScratchKeysAddr + 2);
    TraceOutput::Printf(
        "NSMB InputNetplay: inst=%d frame=%u localPlayer=%d hasRemote=%d "
        "predictedRemote=%d tick=0x%04X action=0x%02X keys0=0x%03X "
        "keys1=0x%03X\n",
        instanceID, frame, localPlayer, hasRemoteInput ? 1 : 0,
        predictedRemoteInput ? 1 : 0, static_cast<unsigned>(tick),
        static_cast<unsigned>(action), static_cast<unsigned>(keys0),
        static_cast<unsigned>(keys1));
  }
}

void ApplyJitHelperPatchIfNeeded(IntegrationContext context, int instanceID,
                                 melonDS::u32 frame, melonDS::NDS *nds) {
  if (!context.RuntimePatch.PacketBridgeJitHelperPatchEnabled || !nds ||
      !nds->MainRAM || instanceID < 0 || instanceID >= 16 ||
      !context.State.ShouldApplyJitHook(
          instanceID, frame,
          context.RuntimePatch.PacketBridgeJitHelperPatchFrame))
    return;

  const auto invalidateMainRAM = [&](melonDS::u32 start, melonDS::u32 end) {
    for (melonDS::u32 address = start; address <= end;
         address += sizeof(melonDS::u32)) {
      nds->JIT.CheckAndInvalidate<0, melonDS::ARMJIT_Memory::memregion_MainRAM>(
          address);
      nds->JIT.CheckAndInvalidate<1, melonDS::ARMJIT_Memory::memregion_MainRAM>(
          address);
    }
  };

  // Net::getConsoleKeys(u16): return scratchKeys[player].
  WriteARM9U32(nds, 0x0200E854, 0xE59F1008);
  WriteARM9U32(nds, 0x0200E858, 0xE0811080);
  WriteARM9U32(nds, 0x0200E85C, 0xE1D100B0);
  WriteARM9U32(nds, 0x0200E860, 0xE12FFF1E);
  WriteARM9U32(nds, 0x0200E864, kPacketBridgeJitScratchKeysAddr);
  // Net::getConsoleTouchPad(u16): write TPData{x,y,touch,0}.
  WriteARM9U32(nds, 0x0200E7D0, 0xE59F2024);
  WriteARM9U32(nds, 0x0200E7D4, 0xE0822301);
  WriteARM9U32(nds, 0x0200E7D8, 0xE5D23006);
  WriteARM9U32(nds, 0x0200E7DC, 0xE1C030B0);
  WriteARM9U32(nds, 0x0200E7E0, 0xE5D23007);
  WriteARM9U32(nds, 0x0200E7E4, 0xE1C030B2);
  WriteARM9U32(nds, 0x0200E7E8, 0xE5D23005);
  WriteARM9U32(nds, 0x0200E7EC, 0xE1C030B4);
  WriteARM9U32(nds, 0x0200E7F0, 0xE3A03000);
  WriteARM9U32(nds, 0x0200E7F4, 0xE1C030B6);
  WriteARM9U32(nds, 0x0200E7F8, 0xE12FFF1E);
  WriteARM9U32(nds, 0x0200E7FC, kPacketBridgeJitScratchPacketsAddr);
  invalidateMainRAM(0x0200E854, 0x0200E864);
  invalidateMainRAM(0x0200E7D0, 0x0200E7FC);

  context.State.MarkJitHookApplied(instanceID);
  std::printf("NSMB Test: packet bridge JIT keys/touch helper patch inst=%d "
              "frame=%u scratch=0x%08X\n",
              instanceID, frame, kPacketBridgeJitScratchBaseAddr);
}

} // namespace NsmbMvlNetplay::PacketBridge
