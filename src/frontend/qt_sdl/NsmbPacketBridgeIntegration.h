#ifndef NSMB_PACKET_BRIDGE_INTEGRATION_H
#define NSMB_PACKET_BRIDGE_INTEGRATION_H

#include "NsmbNetplayConfig.h"
#include "NsmbMvlNetplayRuntime.h"
#include "NsmbPacketBridgeRuntime.h"

#include <cstddef>
#include <functional>
#include <mutex>

namespace melonDS {
class NDS;
}

namespace NsmbNetplayTransport {
class Transport;
}

namespace NsmbMvlNetplay::PacketBridge {

struct IntegrationContext {
  const Config::PacketBridgeConfig &Config;
  const Config::RuntimePatchConfig &RuntimePatch;
  const Config::InputConfig &Input;
  const Config::ConnectionConfig &Connection;
  Runtime &State;
  NsmbNetplayTransport::Transport &Transport;
  std::mutex &Mutex;
};

struct IntegrationHooks {
  std::function<void()> SendMatchSeedLocked;
  std::function<void(melonDS::NDS *, melonDS::u32)> PumpNetworkLocked;
  std::function<InputState(const InputState &)> PrepareRuntimeInput;
  std::function<InputState(int, melonDS::u32, melonDS::NDS *, int,
                           const InputState &)>
      ApplyAutomatedInput;
  std::function<void(int, melonDS::u32, int, const InputState &)>
      RecordAppliedInput;
};

melonDS::u32 LocalPlayerID(IntegrationContext context, melonDS::NDS *nds);
void ReceivePacketLocked(IntegrationContext context, const void *data,
                         std::size_t size, melonDS::NDS *nds,
                         melonDS::u32 localFrame,
                         melonDS::u32 restartCutoffFrame);
void ApplyPendingPacketsLocked(IntegrationContext context, melonDS::NDS *nds);
void PumpLocked(IntegrationContext context, const IntegrationHooks &hooks,
                melonDS::NDS *nds, melonDS::u32 frame);
void CaptureAndSendPacketLocked(IntegrationContext context,
                                const IntegrationHooks &hooks,
                                melonDS::u32 frame, melonDS::NDS *nds);
void ForceTickIfNeeded(IntegrationContext context, int instanceID,
                       melonDS::u32 frame, melonDS::NDS *nds);
void ForceGameLocalPlayerIDIfNeeded(IntegrationContext context,
                                    melonDS::u32 frame, melonDS::NDS *nds);
void ThrottleFrameLead(IntegrationContext context,
                       const IntegrationHooks &hooks, melonDS::NDS *nds,
                       melonDS::u32 frame);
void WriteJitScratchInputs(IntegrationContext context,
                           const IntegrationHooks &hooks, int instanceID,
                           melonDS::u32 frame, melonDS::NDS *nds,
                           int localPlayer, const InputState &localInput,
                           const InputState &remoteInput, bool hasRemoteInput,
                           bool predictedRemoteInput);
void ApplyJitHelperPatchIfNeeded(IntegrationContext context, int instanceID,
                                 melonDS::u32 frame, melonDS::NDS *nds);

} // namespace NsmbMvlNetplay::PacketBridge

#endif
