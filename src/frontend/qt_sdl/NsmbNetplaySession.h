#ifndef NSMB_NETPLAY_SESSION_H
#define NSMB_NETPLAY_SESSION_H

#include "NsmbInputDelivery.h"
#include "NsmbInputTimeline.h"
#include "NsmbNetplayConfig.h"
#include "NsmbNetplayCoordinator.h"
#include "NsmbNetplayDiagnostics.h"
#include "NsmbNetplayProtocol.h"
#include "NsmbNetplayTransport.h"

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>

namespace melonDS {
class NDS;
}

namespace NsmbMvlNetplay::NetplaySession {

struct Runtime {
  SessionPolicy::Runtime Handshake;
  InputDelivery::Runtime Delivery;
  std::condition_variable InputCond;
  bool NetworkPumpThreadStarted = false;
  bool NetworkPumpStop = false;
  std::thread NetworkPumpThread;
};

struct Context {
  const Config::BootstrapConfig &Bootstrap;
  const Config::DiagnosticsConfig &Diagnostics;
  Config::ConnectionConfig &Connection;
  const Config::PacketBridgeConfig &PacketBridge;
  const Config::InputConfig &Input;
  const Config::RollbackConfig &Rollback;
  const Config::HarnessConfig &Harness;
  Config::MvlConfig &Mvl;
  InputTimeline::Runtime &Inputs;
  Coordination::Runtime &Coordinator;
  Diagnostics::Runtime &DiagnosticsRuntime;
  NsmbNetplayTransport::Transport &Transport;
  Runtime &State;
  std::mutex &Mutex;
  bool Enabled = false;
  bool Ready = false;
  bool TestEnabled = false;
  bool Host = false;
};

struct Hooks {
  std::function<void(const char *, melonDS::u32, melonDS::u32)>
      EmitStartReadyEventLocked;
  std::function<void(const void *, std::size_t, melonDS::NDS *, melonDS::u32)>
      ReceivePacketBridgeLocked;
  std::function<void(const void *, std::size_t)> ReceiveGameStateLocked;
  std::function<bool(melonDS::NDS *)> IsGameplayStartReady;
  std::function<SessionProtocol::Message(int, melonDS::NDS *)>
      BuildStartReady;
};

void PumpLocked(Context context, const Hooks &hooks,
                melonDS::NDS *nds = nullptr, melonDS::u32 localFrame = 0);
void StartNetworkPumpThreadIfNeeded(Context context, const Hooks &hooks);
void StopNetworkPumpThread(Context context);

void SendMatchSeedLocked(Context context);
void SendStartReadyLocked(Context context, const Hooks &hooks,
                          melonDS::u32 frame, bool force = false);
void MaybeResendStartReadyLocked(Context context, const Hooks &hooks,
                                 bool allowBeforeAccepted = false);
void SendInputLocked(Context context, const Hooks &hooks, melonDS::u32 frame,
                     const InputState &input);
void MaybeResendLatestInputForFrameLeadLocked(Context context,
                                              const Hooks &hooks);

void PrintInputHealthLocked(Context context, const char *event,
                            melonDS::u32 frame, melonDS::u32 logicalFrame,
                            melonDS::u32 sendFrame, unsigned long long waitedUs,
                            unsigned long long throttleUs,
                            unsigned long long networkUs, int lead,
                            bool hasRemoteInput, bool predictedRemoteInput);
int CurrentInputLead(Context context, melonDS::u32 sendFrame);
void PrimeInputEpochLocked(Context context, melonDS::u32 localFrame);
bool IsPastTestInputRange(Context context, melonDS::u32 targetFrame);
InputState WaitForRemoteInput(Context context, const Hooks &hooks,
                              melonDS::u32 targetFrame);
bool TryWaitForRollbackRemoteInputLocked(Context context, const Hooks &hooks,
                                         std::unique_lock<std::mutex> &lock,
                                         melonDS::NDS *nds,
                                         melonDS::u32 localFrame,
                                         melonDS::u32 targetFrame,
                                         InputState &input);
void WaitForMatchSeed(Context context, const Hooks &hooks);
void WaitForPeer(Context context, const Hooks &hooks, bool force = false);
bool ShouldPumpNetworkAtFrame(Context context, melonDS::u32 syncFrame,
                              melonDS::u32 sendStartFrame);
melonDS::u32 LogicalFrame(Context context, melonDS::u32 rawFrame);
void WaitForPeerAtStartBarrier(Context context, const Hooks &hooks,
                               int instanceID, melonDS::u32 syncFrame);
void WaitForRemoteStartReady(Context context, const Hooks &hooks,
                             int instanceID, melonDS::NDS *nds,
                             melonDS::u32 syncFrame);
void ThrottleFrameLead(Context context, const Hooks &hooks, melonDS::NDS *nds,
                       melonDS::u32 frame, melonDS::u32 sendFrame);
void WaitForRollbackPredictionHorizon(
    Context context, const Hooks &hooks, melonDS::NDS *nds,
    melonDS::u32 rawFrame, melonDS::u32 logicalFrame,
    melonDS::u32 sendFrame);

} // namespace NsmbMvlNetplay::NetplaySession

#endif
