#ifndef NSMBSESSIONPOLICY_H
#define NSMBSESSIONPOLICY_H

#include "types.h"

#include <cstdint>

namespace NsmbNetplayPoC::SessionPolicy {

constexpr std::int64_t kStartReadyResendIntervalMs = 250;

struct StartReadyResendState {
  bool HasPeer = false;
  bool InputNetplayOnly = false;
  bool AllowBeforeAccepted = false;
  bool WaitedForPeerAtStart = false;
  bool StartReadySent = false;
  bool HasLocalReadyFrame = false;
  bool HasReceivedInputFrame = false;
  melonDS::u32 LastReceivedInputFrame = 0;
  melonDS::u32 NetplayStartFrame = 0;
  int InputDelay = 0;
  int SendCount = 0;
  std::int64_t ElapsedSinceLastSendMs = 0;
};

melonDS::u32 FirstGameplayInputFrame(melonDS::u32 netplayStartFrame,
                                     int inputDelay);
bool HasPostStartRemoteInput(bool hasReceivedInputFrame,
                             melonDS::u32 lastReceivedInputFrame,
                             melonDS::u32 netplayStartFrame, int inputDelay);
bool IsOldStartReady(bool inputNetplayOnly, melonDS::u32 netplayStartFrame,
                     melonDS::u32 receivedReadyFrame);
bool ShouldAcceptStartReady(bool hasRemoteReadyFrame,
                            bool remoteReadyAfterLocal,
                            bool hasPostStartRemoteInput);
bool ShouldResendStartReady(const StartReadyResendState &state);

} // namespace NsmbNetplayPoC::SessionPolicy

#endif
