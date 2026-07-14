#include "NsmbSessionPolicy.h"

#include <algorithm>

namespace NsmbNetplayPoC::SessionPolicy {

melonDS::u32 FirstGameplayInputFrame(melonDS::u32 netplayStartFrame,
                                     int inputDelay) {
  return netplayStartFrame + static_cast<melonDS::u32>(std::max(0, inputDelay));
}

bool HasPostStartRemoteInput(bool hasReceivedInputFrame,
                             melonDS::u32 lastReceivedInputFrame,
                             melonDS::u32 netplayStartFrame, int inputDelay) {
  return hasReceivedInputFrame &&
         lastReceivedInputFrame >=
             FirstGameplayInputFrame(netplayStartFrame, inputDelay);
}

bool IsOldStartReady(bool inputNetplayOnly, melonDS::u32 netplayStartFrame,
                     melonDS::u32 receivedReadyFrame) {
  return inputNetplayOnly && netplayStartFrame != 0 &&
         receivedReadyFrame < netplayStartFrame;
}

bool ShouldAcceptStartReady(bool hasRemoteReadyFrame,
                            bool remoteReadyAfterLocal,
                            bool hasPostStartRemoteInput) {
  return hasRemoteReadyFrame &&
         (remoteReadyAfterLocal || hasPostStartRemoteInput);
}

bool ShouldResendStartReady(const StartReadyResendState &state) {
  if (!state.HasPeer || !state.InputNetplayOnly ||
      (!state.AllowBeforeAccepted && !state.WaitedForPeerAtStart) ||
      !state.StartReadySent || !state.HasLocalReadyFrame) {
    return false;
  }

  if (HasPostStartRemoteInput(state.HasReceivedInputFrame,
                              state.LastReceivedInputFrame,
                              state.NetplayStartFrame, state.InputDelay)) {
    return false;
  }

  return state.SendCount <= 0 ||
         state.ElapsedSinceLastSendMs >= kStartReadyResendIntervalMs;
}

} // namespace NsmbNetplayPoC::SessionPolicy
