#include "NsmbNetplaySession.h"

#include "NsmbInputProtocol.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <thread>
#include <vector>

namespace NsmbMvlNetplay::NetplaySession {
namespace {

constexpr melonDS::u32 kNoFrame = 0;
constexpr int kPumpEventLimit = 64;

unsigned long long NowUnixMs() {
  return static_cast<unsigned long long>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

InputState NeutralInput() { return {}; }

void TraceHangPhase(Context context, const char *event, const char *phase,
                    int instanceID = -1, melonDS::u32 frame = 0,
                    melonDS::u32 logicalFrame = 0, melonDS::u32 sendFrame = 0) {
  context.DiagnosticsRuntime.TracePhase(event, phase, instanceID, frame,
                                        logicalFrame, sendFrame);
}

void UpdateHangSnapshotLocked(Context context, melonDS::u32 frameForLead) {
  context.DiagnosticsRuntime.UpdateNetplaySnapshot(
      context.Inputs.LastSentInputFrame, context.Inputs.LastReceivedInputFrame,
      frameForLead, kNoFrame, context.Inputs.LocalInputs.size(),
      context.Inputs.RemoteInputs.size(), context.State.Delivery.PendingCount(),
      context.Transport.PeerState(), context.Transport.ConnectingPeerState());
}

void SendInputPayloadNowLocked(Context context, const void *data,
                               std::size_t size, melonDS::u32 flags) {
  if (!context.Transport.IsConnected())
    return;
  const melonDS::u32 frame = context.Inputs.LastSentInputFrame;
  TraceHangPhase(context, "begin", "enet-send-input", -1, frame, frame, frame);
  const int result = context.Transport.Send(data, size, flags, true);
  if (result == NsmbNetplayTransport::SendUnavailable)
    return;
  context.DiagnosticsRuntime.RecordENetSend(result, size, NowUnixMs());
  UpdateHangSnapshotLocked(context, frame);
  TraceHangPhase(context, "end", "enet-send-input", -1, frame, frame, frame);
}

void FlushDelayedInputsLocked(Context context, melonDS::u32 frame) {
  if (!context.Transport.IsConnected() ||
      context.State.Delivery.PendingCount() == 0)
    return;
  context.State.Delivery.DrainDue(frame, std::chrono::steady_clock::now(),
                                  [context](const std::vector<char> &payload) {
                                    SendInputPayloadNowLocked(
                                        context, payload.data(), payload.size(),
                                        ENET_PACKET_FLAG_RELIABLE);
                                  });
}

void StoreRemoteInputLocked(Context context, melonDS::u32 frame,
                            const InputState &receivedInput,
                            melonDS::u32 localFrame) {
  if (context.Input.NetplayOnly && context.Connection.StartFrame != 0 &&
      frame < context.Connection.StartFrame) {
    if (context.Input.NetplayTrace &&
        context.Inputs.LastTracedReceivedInputFrame != frame) {
      context.Inputs.LastTracedReceivedInputFrame = frame;
      std::printf("NSMB InputNetplay: ignored old input frame=%u "
                  "currentStart=%u\n",
                  frame, context.Connection.StartFrame);
    }
    return;
  }

  const auto stored = context.Inputs.StoreRemote(
      frame, receivedInput,
      localFrame == kNoFrame ? std::optional<melonDS::u32>{}
                             : std::optional<melonDS::u32>{localFrame},
      context.Rollback.Enabled, kNoFrame);
  if (context.Rollback.Enabled && stored.Confirmation.Mismatch &&
      context.Input.NetplayTrace) {
    const auto &confirmation = stored.Confirmation;
    const melonDS::u32 pendingFrame =
        context.Inputs.RollbackInputs.PendingRollbackFrame().value_or(kNoFrame);
    std::printf(
        "NSMB Rollback: prediction mismatch frame=%u predicted={keys=0x%03X "
        "touch=%d x=%u y=%u} actual={keys=0x%03X touch=%d x=%u y=%u} "
        "pending=%u mismatches=%u\n",
        frame, confirmation.PredictedInput.KeyMask,
        confirmation.PredictedInput.Touching ? 1 : 0,
        confirmation.PredictedInput.TouchX, confirmation.PredictedInput.TouchY,
        receivedInput.KeyMask, receivedInput.Touching ? 1 : 0,
        receivedInput.TouchX, receivedInput.TouchY, pendingFrame,
        context.Inputs.RollbackInputs.MismatchCount());
    if (!confirmation.FrameAlreadySimulated)
      std::printf("NSMB Rollback: current/future mismatch applied without "
                  "rollback frame=%u localFrame=%u\n",
                  frame, localFrame);
    std::fflush(stdout);
  }

  const melonDS::u32 previousLastReceived = stored.PreviousLastReceived;
  const melonDS::u32 frameForLead =
      context.Inputs.LastSentInputFrame == kNoFrame
          ? frame
          : context.Inputs.LastSentInputFrame;
  UpdateHangSnapshotLocked(context, frameForLead);
  if (context.Input.HealthTrace && previousLastReceived != kNoFrame &&
      frame > previousLastReceived + 1 &&
      context.Inputs.LastInputHealthReceiveGapFrame != frame) {
    context.Inputs.LastInputHealthReceiveGapFrame = frame;
    PrintInputHealthLocked(context, "recv-gap", localFrame, frame,
                           context.Inputs.LastSentInputFrame, 0, 0, 0,
                           CurrentInputLead(context, frameForLead), true,
                           false);
  }
  context.State.InputCond.notify_all();
  if ((context.Bootstrap.InputTraceEnabled || context.Input.NetplayTrace) &&
      frame != context.Inputs.LastTracedReceivedInputFrame &&
      (context.Bootstrap.InputTraceInterval <= 1 ||
       frame % static_cast<melonDS::u32>(
                   context.Bootstrap.InputTraceInterval) ==
           0)) {
    context.Inputs.LastTracedReceivedInputFrame = frame;
    std::printf("NSMB MvL Netplay: recv input tUnixMs=%llu frame=%u keys=0x%03X "
                "remoteQueue=%zu lastSent=%u lead=%d localFrame=%u\n",
                NowUnixMs(), frame, receivedInput.KeyMask,
                context.Inputs.RemoteInputs.size(),
                context.Inputs.LastSentInputFrame,
                CurrentInputLead(context, frameForLead), localFrame);
    std::fflush(stdout);
  }
}

void HandleReceivedInputLocked(Context context, const void *data,
                               std::size_t size, melonDS::u32 localFrame) {
  InputProtocol::FramedInput packet;
  if (InputProtocol::DecodeInput(data, size, packet))
    StoreRemoteInputLocked(context, packet.Frame, packet.Input, localFrame);
}

void HandleReceivedInputBundleLocked(Context context, const void *data,
                                     std::size_t size,
                                     melonDS::u32 localFrame) {
  std::vector<InputProtocol::FramedInput> entries;
  if (!InputProtocol::DecodeInputBundle(data, size, entries))
    return;
  for (const InputProtocol::FramedInput &entry : entries)
    StoreRemoteInputLocked(context, entry.Frame, entry.Input, localFrame);
}

void HandleReceivedSessionLocked(Context context, const Hooks &hooks,
                                 const void *data, std::size_t size,
                                 melonDS::u32 localFrame) {
  SessionProtocol::Message message;
  if (!SessionProtocol::Decode(data, size, message))
    return;

  if (message.Kind == SessionProtocol::MessageKind::MatchSeed) {
    context.Mvl.MatchSeed = message.Value;
    context.Mvl.MatchSeedConfigured = true;
    context.State.InputCond.notify_all();
    if (context.Harness.StateLoadDir.empty() && !context.PacketBridge.Only) {
      context.Mvl.NetRandom.Value = message.Value;
      context.Mvl.NetRandom.Enabled = true;
      context.Mvl.NetRandom.Auto = true;
    }
    std::printf("NSMB MvL Netplay: received match seed 0x%08X\n", message.Value);
    return;
  }

  if (SessionPolicy::IsOldStartReady(context.Input.NetplayOnly,
                                     context.Connection.StartFrame,
                                     message.Value)) {
    std::printf("NSMB InputNetplay: ignored old start ready frame=%u "
                "currentStart=%u\n",
                message.Value, context.Connection.StartFrame);
    return;
  }
  context.State.Handshake.ReceiveRemoteReady(message.Value);
  hooks.EmitStartReadyEventLocked("recv", localFrame, message.Value);
  context.State.InputCond.notify_all();
  std::printf("NSMB InputNetplay: received start ready frame=%u\n",
              message.Value);
}

void TraceRemoteInputWaitSpike(Context context, melonDS::u32 targetFrame,
                               unsigned long long elapsedUs,
                               unsigned long long loops) {
  if (!context.Diagnostics.ActiveFrameSpikeTrace ||
      elapsedUs < static_cast<unsigned long long>(std::min(
                      context.Diagnostics.ActiveFrameSpikeThresholdUs, 10000)))
    return;
  std::printf("NSMB RemoteInputWaitSpike: frame=%u waitedMs=%.3f loops=%llu\n",
              targetFrame, static_cast<double>(elapsedUs) / 1000.0, loops);
}

void NetworkPumpThreadMain(Context context, const Hooks &hooks) {
  for (;;) {
    int sleepUs = 250;
    {
      std::lock_guard<std::mutex> lock(context.Mutex);
      if (context.State.NetworkPumpStop)
        break;
      PumpLocked(context, hooks);
      sleepUs = std::max(50, context.Harness.NetworkPumpSleepUs);
    }
    std::this_thread::sleep_for(std::chrono::microseconds(sleepUs));
  }
}

} // namespace

void PumpLocked(Context context, const Hooks &hooks, melonDS::NDS *nds,
                melonDS::u32 localFrame) {
  if (!context.Transport.HasHost())
    return;

  TraceHangPhase(context, "begin", "enet-service", -1, localFrame, localFrame,
                 localFrame);
  FlushDelayedInputsLocked(context, localFrame);

  ENetEvent event;
  for (int index = 0; index < kPumpEventLimit; index++) {
    const int result = context.Transport.Service(event);
    context.DiagnosticsRuntime.RecordENetService(result);
    if (result <= 0) {
      UpdateHangSnapshotLocked(context, localFrame);
      break;
    }

    context.DiagnosticsRuntime.RecordENetEvent(static_cast<int>(event.type),
                                               event.data);
    switch (event.type) {
    case ENET_EVENT_TYPE_CONNECT:
      context.Transport.HandleConnected(event.peer);
      context.State.Handshake.OnPeerConnected();
      UpdateHangSnapshotLocked(context, localFrame);
      context.State.InputCond.notify_all();
      std::printf("NSMB MvL Netplay: peer connected tUnixMs=%llu localFrame=%u peer=%d "
                  "connectingPeer=%d lastSent=%u lastRecv=%u localQueue=%zu "
                  "remoteQueue=%zu\n",
                  NowUnixMs(), localFrame,
                  context.Transport.IsConnected() ? 1 : 0,
                  context.Transport.IsConnecting() ? 1 : 0,
                  context.Inputs.LastSentInputFrame,
                  context.Inputs.LastReceivedInputFrame,
                  context.Inputs.LocalInputs.size(),
                  context.Inputs.RemoteInputs.size());
      std::fflush(stdout);
      break;

    case ENET_EVENT_TYPE_RECEIVE: {
      context.DiagnosticsRuntime.RecordENetReceive(NowUnixMs());
      const PacketClassifier::PacketClass packetClass =
          PacketClassifier::Classify(event.packet->dataLength,
                                     {InputProtocol::kInputPacketSize,
                                      SessionProtocol::kSessionPacketSize,
                                      sizeof(WireProtocol::WireNSMLPacket),
                                      sizeof(WireProtocol::WireGameState)});
      if (packetClass == PacketClassifier::PacketClass::Input)
        HandleReceivedInputLocked(context, event.packet->data,
                                  event.packet->dataLength, localFrame);
      else if (packetClass ==
               PacketClassifier::PacketClass::InputBundleCandidate)
        HandleReceivedInputBundleLocked(context, event.packet->data,
                                        event.packet->dataLength, localFrame);
      else if (packetClass == PacketClassifier::PacketClass::Session)
        HandleReceivedSessionLocked(context, hooks, event.packet->data,
                                    event.packet->dataLength, localFrame);
      else if (packetClass == PacketClassifier::PacketClass::NSMLPacket)
        hooks.ReceivePacketBridgeLocked(
            event.packet->data, event.packet->dataLength, nds, localFrame);
      else if (packetClass == PacketClassifier::PacketClass::GameState)
        hooks.ReceiveGameStateLocked(event.packet->data,
                                     event.packet->dataLength);
      enet_packet_destroy(event.packet);
      UpdateHangSnapshotLocked(context, localFrame);
      break;
    }

    case ENET_EVENT_TYPE_DISCONNECT:
      std::printf(
          "NSMB MvL Netplay: peer disconnected tUnixMs=%llu localFrame=%u "
          "eventPeerMatches=%d peerBefore=%d connectingPeer=%d lastSent=%u "
          "lastRecv=%u lead=%d localQueue=%zu remoteQueue=%zu delayed=%zu "
          "resendCount=%d netplayStart=%u localReady=%u remoteReady=%u "
          "remoteReadyAfterLocal=%d eventData=%u\n",
          NowUnixMs(), localFrame, context.Transport.IsPeer(event.peer) ? 1 : 0,
          context.Transport.IsConnected() ? 1 : 0,
          context.Transport.IsConnecting() ? 1 : 0,
          context.Inputs.LastSentInputFrame,
          context.Inputs.LastReceivedInputFrame,
          CurrentInputLead(context,
                           context.Inputs.LastSentInputFrame == kNoFrame
                               ? localFrame
                               : context.Inputs.LastSentInputFrame),
          context.Inputs.LocalInputs.size(), context.Inputs.RemoteInputs.size(),
          context.State.Delivery.PendingCount(),
          context.Inputs.InputFrameLeadResendCount,
          context.Connection.StartFrame,
          context.State.Handshake.LocalReadyFrame().value_or(kNoFrame),
          context.State.Handshake.RemoteReadyFrame().value_or(kNoFrame),
          context.State.Handshake.RemoteReadyAfterLocal() ? 1 : 0, event.data);
      context.Transport.HandleDisconnected(event.peer);
      UpdateHangSnapshotLocked(context, localFrame);
      std::fflush(stdout);
      break;

    default:
      break;
    }
  }
  context.Transport.Flush();
  TraceHangPhase(context, "end", "enet-service", -1, localFrame, localFrame,
                 localFrame);
}

void StartNetworkPumpThreadIfNeeded(Context context, const Hooks &hooks) {
  if (!context.Harness.NetworkPumpThreadEnabled ||
      context.State.NetworkPumpThreadStarted)
    return;
  context.State.NetworkPumpStop = false;
  context.State.NetworkPumpThreadStarted = true;
  context.State.NetworkPumpThread =
      std::thread([context, &hooks] { NetworkPumpThreadMain(context, hooks); });
  std::printf("NSMB MvL Netplay: network pump thread started sleepUs=%d "
              "inputWaitPollUs=%d rollbackInputWaitUs=%d\n",
              context.Harness.NetworkPumpSleepUs, context.Input.WaitPollUs,
              context.Rollback.InputWaitUs);
  std::fflush(stdout);
}

void StopNetworkPumpThread(Context context) {
  std::thread thread;
  {
    std::lock_guard<std::mutex> lock(context.Mutex);
    if (!context.State.NetworkPumpThreadStarted)
      return;
    context.State.NetworkPumpStop = true;
    context.State.InputCond.notify_all();
    thread = std::move(context.State.NetworkPumpThread);
    context.State.NetworkPumpThreadStarted = false;
  }
  if (thread.joinable())
    thread.join();
}

void SendMatchSeedLocked(Context context) {
  if (!context.Transport.IsConnected() || !context.Host ||
      !context.Mvl.MatchSeedConfigured ||
      context.State.Handshake.MatchSeedSent())
    return;
  const std::vector<char> payload = SessionProtocol::Encode(
      {SessionProtocol::MessageKind::MatchSeed, context.Mvl.MatchSeed});
  if (context.Transport.Send(payload.data(), payload.size(),
                             ENET_PACKET_FLAG_RELIABLE,
                             true) == NsmbNetplayTransport::SendUnavailable)
    return;
  context.State.Handshake.MarkMatchSeedSent();
  std::printf("NSMB MvL Netplay: sent match seed 0x%08X\n", context.Mvl.MatchSeed);
}

void SendStartReadyLocked(Context context, const Hooks &hooks,
                          melonDS::u32 frame, bool force) {
  if (!context.Transport.IsConnected() ||
      !context.State.Handshake.CanSendStartReady(force))
    return;
  const std::vector<char> payload = SessionProtocol::Encode(
      {SessionProtocol::MessageKind::StartReady, frame});
  if (context.Transport.Send(payload.data(), payload.size(),
                             ENET_PACKET_FLAG_RELIABLE,
                             true) == NsmbNetplayTransport::SendUnavailable)
    return;
  context.State.Handshake.MarkStartReadySent(std::chrono::steady_clock::now());
  hooks.EmitStartReadyEventLocked(
      force ? "resend" : "send", frame,
      context.State.Handshake.RemoteReadyFrame().value_or(kNoFrame));
  std::printf("NSMB InputNetplay: %s start ready frame=%u count=%d\n",
              force ? "resent" : "sent", frame,
              context.State.Handshake.StartReadySendCount());
  std::fflush(stdout);
}

void MaybeResendStartReadyLocked(Context context, const Hooks &hooks,
                                 bool allowBeforeAccepted) {
  const auto now = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - context.State.Handshake.LastStartReadySentAt())
                           .count();
  if (!SessionPolicy::ShouldResendStartReady(
          {context.Transport.IsConnected(), context.Input.NetplayOnly,
           allowBeforeAccepted, context.State.Handshake.WaitedForPeerAtStart(),
           context.State.Handshake.StartReadySent(),
           context.State.Handshake.LocalReadyFrame().has_value(),
           context.Inputs.LastReceivedInputFrame != kNoFrame,
           context.Inputs.LastReceivedInputFrame, context.Connection.StartFrame,
           context.Connection.Delay,
           context.State.Handshake.StartReadySendCount(), elapsed}))
    return;
  SendStartReadyLocked(
      context, hooks,
      context.State.Handshake.LocalReadyFrame().value_or(kNoFrame), true);
}

void SendInputLocked(Context context, const Hooks &hooks, melonDS::u32 frame,
                     const InputState &input) {
  if (!context.Transport.IsConnected())
    return;
  SendMatchSeedLocked(context);
  MaybeResendStartReadyLocked(context, hooks);

  const melonDS::u32 previousLastSent = context.Inputs.LastSentInputFrame;
  context.Inputs.LastSentInputFrame = frame;
  UpdateHangSnapshotLocked(context, frame);
  if (context.Input.HealthTrace && previousLastSent != kNoFrame &&
      frame > previousLastSent + 1 &&
      context.Inputs.LastInputHealthSendGapFrame != frame) {
    context.Inputs.LastInputHealthSendGapFrame = frame;
    PrintInputHealthLocked(context, "send-gap", frame, frame, frame, 0, 0, 0,
                           CurrentInputLead(context, frame), false, false);
  }

  const InputDelivery::PreparedSend prepared = context.State.Delivery.Prepare(
      frame, input,
      {context.Input.UseHistoryBundle, context.Input.BundleHistory,
       context.Input.DropModulo, context.Input.DropOffset,
       context.Input.DropStartFrame, context.Input.DropEndFrame,
       context.Input.SendDelayFrames, context.Input.SendJitterFrames,
       context.Input.SendDelayStartFrame, context.Input.SendDelayEndFrame},
      context.Inputs.LocalInputs, std::chrono::steady_clock::now());
  if (prepared.Decision.Drop) {
    if (context.Input.NetplayTrace)
      std::printf("NSMB InputNetplay: dropped local input packet frame=%u "
                  "modulo=%d offset=%d range=%u-%u\n",
                  frame, context.Input.DropModulo, context.Input.DropOffset,
                  context.Input.DropStartFrame, context.Input.DropEndFrame);
    return;
  }
  if (!prepared.ImmediatePayload.empty())
    SendInputPayloadNowLocked(context, prepared.ImmediatePayload.data(),
                              prepared.ImmediatePayload.size(),
                              ENET_PACKET_FLAG_RELIABLE);

  if ((context.Bootstrap.InputTraceEnabled || context.Input.NetplayTrace) &&
      frame != context.Inputs.LastTracedSentInputFrame &&
      (context.Bootstrap.InputTraceInterval <= 1 ||
       frame % static_cast<melonDS::u32>(
                   context.Bootstrap.InputTraceInterval) ==
           0)) {
    context.Inputs.LastTracedSentInputFrame = frame;
    std::printf(
        "NSMB MvL Netplay: sent input tUnixMs=%llu frame=%u keys=0x%03X "
        "localQueue=%zu lastRecv=%u lead=%d bundle=%d "
        "delayedFrames=%d peer=%d\n",
        NowUnixMs(), frame, input.KeyMask, context.Inputs.LocalInputs.size(),
        context.Inputs.LastReceivedInputFrame, CurrentInputLead(context, frame),
        prepared.Decision.Bundle ? 1 : 0, prepared.Decision.DelayFrames,
        context.Transport.IsConnected() ? 1 : 0);
    std::fflush(stdout);
  }
}

void MaybeResendLatestInputForFrameLeadLocked(Context context, const Hooks &) {
  if (!context.Transport.IsConnected() || !context.Input.NetplayOnly ||
      context.Inputs.LastSentInputFrame == kNoFrame)
    return;
  const auto now = std::chrono::steady_clock::now();
  if (context.Inputs.InputFrameLeadResendCount > 0 &&
      now - context.Inputs.LastInputFrameLeadResendAt <
          std::chrono::milliseconds(50))
    return;
  const auto input =
      context.Inputs.LocalInputs.find(context.Inputs.LastSentInputFrame);
  if (input == context.Inputs.LocalInputs.end())
    return;

  const std::vector<char> payload = context.State.Delivery.BuildPayload(
      context.Inputs.LastSentInputFrame, input->second,
      context.Input.BundleHistory, context.Inputs.LocalInputs);
  const std::size_t payloadBytes = payload.size();
  SendInputPayloadNowLocked(context, payload.data(), payload.size(),
                            ENET_PACKET_FLAG_RELIABLE);
  context.Inputs.LastInputFrameLeadResendAt = now;
  context.Inputs.InputFrameLeadResendCount++;
  if (context.Input.NetplayTrace) {
    std::printf(
        "NSMB InputNetplay: resent latest input tUnixMs=%llu frame=%u count=%d "
        "payloadBytes=%zu bundleHistory=%d lastRecv=%u lead=%d localQueue=%zu "
        "remoteQueue=%zu delayed=%zu peer=%d\n",
        NowUnixMs(), context.Inputs.LastSentInputFrame,
        context.Inputs.InputFrameLeadResendCount, payloadBytes,
        context.Input.BundleHistory, context.Inputs.LastReceivedInputFrame,
        CurrentInputLead(context, context.Inputs.LastSentInputFrame),
        context.Inputs.LocalInputs.size(), context.Inputs.RemoteInputs.size(),
        context.State.Delivery.PendingCount(),
        context.Transport.IsConnected() ? 1 : 0);
    std::fflush(stdout);
  }
}

void PrintInputHealthLocked(Context context, const char *event,
                            melonDS::u32 frame, melonDS::u32 logicalFrame,
                            melonDS::u32 sendFrame, unsigned long long waitedUs,
                            unsigned long long throttleUs,
                            unsigned long long networkUs, int lead,
                            bool hasRemoteInput, bool predictedRemoteInput) {
  if (!context.Input.HealthTrace)
    return;
  UpdateHangSnapshotLocked(context, sendFrame);
  std::printf(
      "NSMB InputHealth: tUnixMs=%llu event=%s frame=%u logicalFrame=%u "
      "sendFrame=%u lastSent=%u lastRecv=%u lead=%d localQueue=%zu "
      "remoteQueue=%zu delayed=%zu waitMs=%.3f throttleMs=%.3f "
      "networkMs=%.3f hasRemote=%d predicted=%d rollback=%d peer=%d "
      "connectingPeer=%d resendCount=%d netplayStart=%u localReady=%u "
      "remoteReady=%u remoteReadyAfterLocal=%d\n",
      NowUnixMs(), event, frame, logicalFrame, sendFrame,
      context.Inputs.LastSentInputFrame, context.Inputs.LastReceivedInputFrame,
      lead, context.Inputs.LocalInputs.size(),
      context.Inputs.RemoteInputs.size(), context.State.Delivery.PendingCount(),
      static_cast<double>(waitedUs) / 1000.0,
      static_cast<double>(throttleUs) / 1000.0,
      static_cast<double>(networkUs) / 1000.0, hasRemoteInput ? 1 : 0,
      predictedRemoteInput ? 1 : 0, context.Rollback.Enabled ? 1 : 0,
      context.Transport.IsConnected() ? 1 : 0,
      context.Transport.IsConnecting() ? 1 : 0,
      context.Inputs.InputFrameLeadResendCount, context.Connection.StartFrame,
      context.State.Handshake.LocalReadyFrame().value_or(kNoFrame),
      context.State.Handshake.RemoteReadyFrame().value_or(kNoFrame),
      context.State.Handshake.RemoteReadyAfterLocal() ? 1 : 0);
  std::fflush(stdout);
}

int CurrentInputLead(Context context, melonDS::u32 sendFrame) {
  return context.Inputs.Lead(sendFrame, kNoFrame);
}

void PrimeInputEpochLocked(Context context, melonDS::u32 localFrame) {
  if (!context.Input.NetplayOnly || context.Connection.StartFrame == 0 ||
      context.State.Handshake.InputEpochPrimedFor(
          context.Connection.StartFrame))
    return;
  const melonDS::u32 delay =
      static_cast<melonDS::u32>(std::max(0, context.Connection.Delay));
  const melonDS::u32 firstInputFrame = context.Connection.StartFrame + delay;
  context.Inputs.PrimeEpoch(context.Connection.StartFrame, delay,
                            NeutralInput(), kNoFrame);
  context.State.Handshake.MarkInputEpochPrimed(context.Connection.StartFrame);
  context.State.InputCond.notify_all();
  std::printf("NSMB InputNetplay: primed epoch start localFrame=%u "
              "logicalStart=%u firstInput=%u delay=%u\n",
              localFrame, context.Connection.StartFrame, firstInputFrame,
              delay);
  std::fflush(stdout);
}

bool IsPastTestInputRange(Context context, melonDS::u32 targetFrame) {
  return context.TestEnabled && context.Bootstrap.TestFrames != kNoFrame &&
         targetFrame >= context.Bootstrap.TestFrames;
}

InputState WaitForRemoteInput(Context context, const Hooks &hooks,
                              melonDS::u32 targetFrame) {
  if ((context.PacketBridge.Only || context.Input.NetplayOnly) &&
      context.Connection.StartFrame != 0 &&
      targetFrame < context.Connection.StartFrame)
    return NeutralInput();

  const auto start = std::chrono::steady_clock::now();
  unsigned long long loops = 0;
  bool waitTraceStarted = false;
  long long lastProgressSecond = -1;
  if (context.Diagnostics.HangDiagnosticsEnabled) {
    context.DiagnosticsRuntime.BeginRemoteWait(targetFrame, NowUnixMs());
    TraceHangPhase(context, "begin", "remote-input-wait", -1, targetFrame,
                   targetFrame, targetFrame);
  }
  for (;;) {
    loops++;
    {
      std::lock_guard<std::mutex> lock(context.Mutex);
      PumpLocked(context, hooks);
      MaybeResendStartReadyLocked(context, hooks);
      MaybeResendLatestInputForFrameLeadLocked(context, hooks);

      const auto input = context.Inputs.RemoteInputs.find(targetFrame);
      if (input != context.Inputs.RemoteInputs.end()) {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start)
                .count();
        const auto waitedUs =
            static_cast<unsigned long long>(std::max<long long>(0, elapsed));
        context.Inputs.RecordRemoteInputWait(waitedUs, loops);
        TraceRemoteInputWaitSpike(context, targetFrame, waitedUs, loops);
        if (context.Input.HealthTrace &&
            waitedUs >= static_cast<unsigned long long>(
                            context.Input.HealthTraceWaitThresholdMs) *
                            1000ULL &&
            context.Inputs.LastInputHealthRemoteWaitFrame != targetFrame) {
          context.Inputs.LastInputHealthRemoteWaitFrame = targetFrame;
          const melonDS::u32 leadFrame =
              context.Inputs.LastSentInputFrame == kNoFrame
                  ? targetFrame
                  : context.Inputs.LastSentInputFrame;
          PrintInputHealthLocked(
              context, "remote-wait-resolved", targetFrame, targetFrame,
              context.Inputs.LastSentInputFrame, waitedUs, 0, 0,
              CurrentInputLead(context, leadFrame), true, false);
        }
        context.DiagnosticsRuntime.EndRemoteWait();
        TraceHangPhase(context, "end", "remote-input-wait", -1, targetFrame,
                       targetFrame, context.Inputs.LastSentInputFrame);
        return input->second;
      }

      if (context.Input.HealthTrace) {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start)
                .count();
        const auto waitedUs =
            static_cast<unsigned long long>(std::max<long long>(0, elapsed));
        const long long progressSecond =
            std::max<long long>(0, elapsed) / 1000000LL;
        if (!waitTraceStarted || progressSecond > lastProgressSecond) {
          context.DiagnosticsRuntime.ProgressRemoteWait(NowUnixMs());
          TraceHangPhase(context,
                         progressSecond == 0 ? "wait-start" : "wait-progress",
                         "remote-input-wait", -1, targetFrame, targetFrame,
                         context.Inputs.LastSentInputFrame);
          waitTraceStarted = true;
          lastProgressSecond = progressSecond;
          const melonDS::u32 leadFrame =
              context.Inputs.LastSentInputFrame == kNoFrame
                  ? targetFrame
                  : context.Inputs.LastSentInputFrame;
          PrintInputHealthLocked(context,
                                 progressSecond == 0 ? "remote-wait-start"
                                                     : "remote-wait-progress",
                                 targetFrame, targetFrame,
                                 context.Inputs.LastSentInputFrame, waitedUs, 0,
                                 0, CurrentInputLead(context, leadFrame), false,
                                 false);
        }
      }
    }

    if (context.TestEnabled && context.Bootstrap.WaitTimeoutMs > 0) {
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - start)
              .count();
      if (elapsed >= context.Bootstrap.WaitTimeoutMs) {
        std::lock_guard<std::mutex> lock(context.Mutex);
        const melonDS::u32 leadFrame =
            context.Inputs.LastSentInputFrame == kNoFrame
                ? targetFrame
                : context.Inputs.LastSentInputFrame;
        std::printf(
            "NSMB Test: remote input timeout tUnixMs=%llu frame=%u "
            "waitedMs=%d actualElapsedMs=%lld loops=%llu peer=%d "
            "connectingPeer=%d lastSent=%u lastRecv=%u lead=%d "
            "localQueue=%zu remoteQueue=%zu delayed=%zu resendCount=%d "
            "netplayStart=%u localReady=%u remoteReady=%u\n",
            NowUnixMs(), targetFrame, context.Bootstrap.WaitTimeoutMs,
            static_cast<long long>(elapsed), loops,
            context.Transport.IsConnected() ? 1 : 0,
            context.Transport.IsConnecting() ? 1 : 0,
            context.Inputs.LastSentInputFrame,
            context.Inputs.LastReceivedInputFrame,
            CurrentInputLead(context, leadFrame),
            context.Inputs.LocalInputs.size(),
            context.Inputs.RemoteInputs.size(),
            context.State.Delivery.PendingCount(),
            context.Inputs.InputFrameLeadResendCount,
            context.Connection.StartFrame,
            context.State.Handshake.LocalReadyFrame().value_or(kNoFrame),
            context.State.Handshake.RemoteReadyFrame().value_or(kNoFrame));
        std::fflush(stdout);
        context.DiagnosticsRuntime.EndRemoteWait();
        TraceHangPhase(context, "timeout", "remote-input-wait", -1, targetFrame,
                       targetFrame, context.Inputs.LastSentInputFrame);
        if (context.Connection.RemoteInputTimeoutFatal)
          std::_Exit(70);
        const auto waited =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start)
                .count();
        const auto waitedUs =
            static_cast<unsigned long long>(std::max<long long>(0, waited));
        context.Inputs.RecordRemoteInputWait(waitedUs, loops);
        TraceRemoteInputWaitSpike(context, targetFrame, waitedUs, loops);
        return NeutralInput();
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

bool TryWaitForRollbackRemoteInputLocked(Context context, const Hooks &hooks,
                                         std::unique_lock<std::mutex> &lock,
                                         melonDS::NDS *nds,
                                         melonDS::u32 localFrame,
                                         melonDS::u32 targetFrame,
                                         InputState &input) {
  if (context.Rollback.InputWaitUs <= 0 || !lock.owns_lock())
    return false;
  if ((context.PacketBridge.Only || context.Input.NetplayOnly) &&
      context.Connection.StartFrame != 0 &&
      targetFrame < context.Connection.StartFrame)
    return false;

  const auto start = std::chrono::steady_clock::now();
  unsigned long long loops = 0;
  for (;;) {
    loops++;
    PumpLocked(context, hooks, nds, localFrame);
    const auto received = context.Inputs.RemoteInputs.find(targetFrame);
    if (received != context.Inputs.RemoteInputs.end()) {
      input = received->second;
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - start)
              .count();
      context.Inputs.RecordRemoteInputWait(
          static_cast<unsigned long long>(std::max<long long>(0, elapsed)),
          loops);
      return true;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    const long long remaining =
        static_cast<long long>(context.Rollback.InputWaitUs) - elapsed;
    if (remaining <= 0) {
      context.Inputs.RecordRemoteInputWait(
          static_cast<unsigned long long>(std::max<long long>(0, elapsed)),
          loops);
      if (context.Input.NetplayTrace) {
        std::printf(
            "NSMB Rollback: input wait timeout frame=%u waitedUs=%lld\n",
            targetFrame,
            static_cast<long long>(std::max<long long>(0, elapsed)));
        std::fflush(stdout);
      }
      return false;
    }
    const int pollUs = std::clamp(static_cast<int>(std::min<long long>(
                                      remaining, context.Input.WaitPollUs)),
                                  50, 5000);
    context.State.InputCond.wait_for(lock, std::chrono::microseconds(pollUs));
  }
}

void WaitForMatchSeed(Context context, const Hooks &hooks) {
  if (!context.Enabled || context.Host || context.Mvl.MatchSeedConfigured)
    return;
  const auto start = std::chrono::steady_clock::now();
  for (;;) {
    {
      std::lock_guard<std::mutex> lock(context.Mutex);
      PumpLocked(context, hooks);
      if (context.Mvl.MatchSeedConfigured)
        return;
    }
    if (context.Harness.SeedWaitTimeoutMs > 0) {
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - start)
              .count();
      if (elapsed >= context.Harness.SeedWaitTimeoutMs) {
        std::printf("NSMB MvL Netplay: match seed wait timeout waitedMs=%d\n",
                    context.Harness.SeedWaitTimeoutMs);
        return;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

void WaitForPeer(Context context, const Hooks &hooks, bool force) {
  if (!context.Enabled || (!force && !context.Harness.WaitForPeerBeforeStart) ||
      !context.Host || context.Transport.IsConnected())
    return;
  const auto start = std::chrono::steady_clock::now();
  for (;;) {
    {
      std::lock_guard<std::mutex> lock(context.Mutex);
      PumpLocked(context, hooks);
      if (context.Transport.IsConnected())
        return;
    }
    if (context.Harness.SeedWaitTimeoutMs > 0) {
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - start)
              .count();
      if (elapsed >= context.Harness.SeedWaitTimeoutMs) {
        std::printf("NSMB MvL Netplay: peer wait timeout waitedMs=%d\n",
                    context.Harness.SeedWaitTimeoutMs);
        return;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

bool ShouldPumpNetworkAtFrame(Context context, melonDS::u32 syncFrame,
                              melonDS::u32 sendStartFrame) {
  return SessionPolicy::ShouldPumpNetworkAtFrame(
      context.Harness.DeferNetworkUntilStart, context.Connection.StartFrame,
      syncFrame, sendStartFrame);
}

melonDS::u32 LogicalFrame(Context context, melonDS::u32 rawFrame) {
  return SessionPolicy::LogicalInputFrame(
      context.Input.NetplayOnly, context.State.Handshake.LocalReadyFrame(),
      context.Connection.StartFrame, rawFrame);
}

void WaitForPeerAtStartBarrier(Context context, const Hooks &hooks,
                               int instanceID, melonDS::u32 syncFrame) {
  if (!context.Enabled || !context.Harness.WaitForPeerAtNetplayStart ||
      !context.Host || context.Input.NetplayOnly ||
      context.Connection.StartFrame == 0 ||
      syncFrame != context.Connection.StartFrame || instanceID < 0 ||
      instanceID >= 16 || context.State.Handshake.WaitedForPeerAtStart())
    return;

  const auto result = context.Coordinator.WaitForNetplayStart(
      instanceID, context.Connection.LocalInstance,
      context.Bootstrap.TestInstanceCount, syncFrame,
      context.Harness.SeedWaitTimeoutMs);
  if (result != Coordination::NetplayStartWaitResult::LocalLeader)
    return;
  std::printf("NSMB MvL Netplay: waiting for peer at netplay start frame=%u\n",
              syncFrame);
  std::fflush(stdout);
  WaitForPeer(context, hooks, true);
  {
    std::lock_guard<std::mutex> lock(context.Mutex);
    context.State.Handshake.MarkWaitedForPeerAtStart();
  }
  context.Coordinator.CompleteNetplayStartWait();
  std::printf("NSMB MvL Netplay: peer wait at netplay start finished frame=%u\n",
              syncFrame);
  std::fflush(stdout);
}

void WaitForRemoteStartReady(Context context, const Hooks &hooks,
                             melonDS::NDS *nds, melonDS::u32 syncFrame) {
  if (!context.Enabled || !context.Input.NetplayOnly ||
      !context.Harness.WaitForPeerAtNetplayStart ||
      context.Connection.StartFrame == 0 ||
      syncFrame < context.Connection.StartFrame ||
      context.State.Handshake.WaitedForPeerAtStart() ||
      !hooks.IsGameplayStartReady(nds))
    return;

  context.State.Handshake.BeginLocalReady(syncFrame);
  std::printf("NSMB InputNetplay: waiting for remote gameplay start ready "
              "localFrame=%u logicalStart=%u\n",
              syncFrame, context.Connection.StartFrame);
  std::fflush(stdout);
  const auto start = std::chrono::steady_clock::now();
  for (;;) {
    {
      std::lock_guard<std::mutex> lock(context.Mutex);
      PumpLocked(context, hooks, nds, syncFrame);
      SendMatchSeedLocked(context);
      SendStartReadyLocked(context, hooks, syncFrame);
      MaybeResendStartReadyLocked(context, hooks, true);
      const bool hasPostStartRemoteInput =
          SessionPolicy::HasPostStartRemoteInput(
              context.Inputs.LastReceivedInputFrame != kNoFrame,
              context.Inputs.LastReceivedInputFrame,
              context.Connection.StartFrame, context.Connection.Delay);
      if (SessionPolicy::ShouldAcceptStartReady(
              context.State.Handshake.RemoteReadyFrame().has_value(),
              context.State.Handshake.RemoteReadyAfterLocal(),
              hasPostStartRemoteInput)) {
        context.State.Handshake.MarkWaitedForPeerAtStart();
        PrimeInputEpochLocked(context, syncFrame);
        const melonDS::u32 remoteReadyFrame =
            context.State.Handshake.RemoteReadyFrame().value_or(kNoFrame);
        hooks.EmitStartReadyEventLocked("accept", syncFrame, remoteReadyFrame);
        std::printf("NSMB InputNetplay: remote gameplay start ready accepted "
                    "remoteFrame=%u localFrame=%u logicalStart=%u\n",
                    remoteReadyFrame, syncFrame, context.Connection.StartFrame);
        std::fflush(stdout);
        return;
      }
    }

    if (context.Harness.SeedWaitTimeoutMs > 0) {
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - start)
              .count();
      if (elapsed >= context.Harness.SeedWaitTimeoutMs) {
        std::printf("NSMB InputNetplay: remote start ready wait timeout "
                    "frame=%u waitedMs=%d\n",
                    syncFrame, context.Harness.SeedWaitTimeoutMs);
        std::fflush(stdout);
        context.State.Handshake.ResetReadyWaitAfterTimeout();
        return;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

void ThrottleFrameLead(Context context, const Hooks &hooks, melonDS::NDS *nds,
                       melonDS::u32 frame, melonDS::u32 sendFrame) {
  if (!context.Input.NetplayOnly || context.Input.MaxFrameLead < 0 ||
      !context.Enabled || !context.Ready ||
      (context.Connection.StartFrame != 0 &&
       frame < context.Connection.StartFrame) ||
      IsPastTestInputRange(context, sendFrame))
    return;

  TraceHangPhase(context, "begin", "frame-lead-throttle", -1, frame, frame,
                 sendFrame);
  const auto start = std::chrono::steady_clock::now();
  bool blocked = false;
  unsigned long long loops = 0;
  for (;;) {
    loops++;
    melonDS::u32 remoteFrame = kNoFrame;
    {
      std::lock_guard<std::mutex> lock(context.Mutex);
      PumpLocked(context, hooks, nds, frame);
      MaybeResendStartReadyLocked(context, hooks);
      remoteFrame = context.Inputs.LastReceivedInputFrame;
    }
    if (remoteFrame == kNoFrame) {
      TraceHangPhase(context, "end", "frame-lead-throttle", -1, frame, frame,
                     sendFrame);
      return;
    }

    const int lead =
        static_cast<int>(sendFrame) - static_cast<int>(remoteFrame);
    if (lead <= context.Input.MaxFrameLead) {
      if (blocked) {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start)
                .count();
        const auto waitedUs =
            static_cast<unsigned long long>(std::max<long long>(0, elapsed));
        context.Inputs.RecordFrameLeadThrottle(waitedUs, loops);
        if (context.Input.HealthTrace &&
            context.Inputs.LastInputHealthThrottleResolvedFrame != frame) {
          std::lock_guard<std::mutex> lock(context.Mutex);
          context.Inputs.LastInputHealthThrottleResolvedFrame = frame;
          PrintInputHealthLocked(
              context, "throttle-resolved", frame, frame, sendFrame, 0,
              waitedUs, 0, CurrentInputLead(context, sendFrame), true, false);
        }
      }
      TraceHangPhase(context, "end", "frame-lead-throttle", -1, frame, frame,
                     sendFrame);
      return;
    }
    blocked = true;
    TraceHangPhase(context, "blocked", "frame-lead-throttle", -1, frame, frame,
                   sendFrame);
    {
      std::lock_guard<std::mutex> lock(context.Mutex);
      MaybeResendLatestInputForFrameLeadLocked(context, hooks);
    }

    if (context.Input.NetplayTrace &&
        context.Inputs.LastInputFrameThrottleTraceFrame != frame) {
      context.Inputs.LastInputFrameThrottleTraceFrame = frame;
      std::printf("NSMB InputNetplay: frame throttle frame=%u sendFrame=%u "
                  "remoteInputFrame=%u lead=%d maxLead=%d\n",
                  frame, sendFrame, remoteFrame, lead,
                  context.Input.MaxFrameLead);
      std::fflush(stdout);
    }
    if (context.Input.HealthTrace &&
        context.Inputs.LastInputHealthThrottleFrame != frame) {
      std::lock_guard<std::mutex> lock(context.Mutex);
      context.Inputs.LastInputHealthThrottleFrame = frame;
      PrintInputHealthLocked(
          context, "throttle-blocked", frame, frame, sendFrame, 0, 0, 0,
          CurrentInputLead(context, sendFrame), false, false);
    }

    if (context.TestEnabled && context.Bootstrap.WaitTimeoutMs > 0) {
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - start)
              .count();
      if (elapsed >= context.Bootstrap.WaitTimeoutMs) {
        std::printf("NSMB Test: input frame throttle timeout frame=%u "
                    "sendFrame=%u remoteInputFrame=%u lead=%d waitedMs=%d\n",
                    frame, sendFrame, remoteFrame, lead,
                    context.Bootstrap.WaitTimeoutMs);
        std::fflush(stdout);
        TraceHangPhase(context, "timeout", "frame-lead-throttle", -1, frame,
                       frame, sendFrame);
        if (context.Connection.RemoteInputTimeoutFatal)
          std::_Exit(71);
        if (blocked) {
          const auto waited =
              std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::steady_clock::now() - start)
                  .count();
          context.Inputs.RecordFrameLeadThrottle(
              static_cast<unsigned long long>(std::max<long long>(0, waited)),
              loops);
        }
        return;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

} // namespace NsmbMvlNetplay::NetplaySession
