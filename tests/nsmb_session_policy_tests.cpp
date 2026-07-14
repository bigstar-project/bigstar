#include "NsmbSessionPolicy.h"

#include <cstdio>

namespace {

int Failures = 0;

void Check(bool condition, const char *expression, int line) {
  if (condition)
    return;
  std::fprintf(stderr, "line %d: CHECK failed: %s\n", line, expression);
  Failures++;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

void TestStartFrameAndReceivePolicy() {
  using namespace NsmbNetplayPoC::SessionPolicy;
  CHECK(FirstGameplayInputFrame(100, -3) == 100);
  CHECK(FirstGameplayInputFrame(100, 4) == 104);
  CHECK(!HasPostStartRemoteInput(false, 999, 100, 4));
  CHECK(!HasPostStartRemoteInput(true, 103, 100, 4));
  CHECK(HasPostStartRemoteInput(true, 104, 100, 4));

  CHECK(!IsOldStartReady(false, 100, 99));
  CHECK(!IsOldStartReady(true, 0, 0));
  CHECK(IsOldStartReady(true, 100, 99));
  CHECK(!IsOldStartReady(true, 100, 100));

  CHECK(!ShouldAcceptStartReady(false, true, true));
  CHECK(!ShouldAcceptStartReady(true, false, false));
  CHECK(ShouldAcceptStartReady(true, true, false));
  CHECK(ShouldAcceptStartReady(true, false, true));
}

void TestResendPolicy() {
  using namespace NsmbNetplayPoC::SessionPolicy;
  StartReadyResendState state;
  state.HasPeer = true;
  state.InputNetplayOnly = true;
  state.WaitedForPeerAtStart = true;
  state.StartReadySent = true;
  state.HasLocalReadyFrame = true;
  state.NetplayStartFrame = 100;
  state.InputDelay = 4;
  state.SendCount = 1;
  state.ElapsedSinceLastSendMs = 250;
  CHECK(ShouldResendStartReady(state));

  state.HasPeer = false;
  CHECK(!ShouldResendStartReady(state));
  state.HasPeer = true;
  state.InputNetplayOnly = false;
  CHECK(!ShouldResendStartReady(state));
  state.InputNetplayOnly = true;
  state.WaitedForPeerAtStart = false;
  CHECK(!ShouldResendStartReady(state));
  state.AllowBeforeAccepted = true;
  CHECK(ShouldResendStartReady(state));
  state.StartReadySent = false;
  CHECK(!ShouldResendStartReady(state));
  state.StartReadySent = true;
  state.HasLocalReadyFrame = false;
  CHECK(!ShouldResendStartReady(state));
  state.HasLocalReadyFrame = true;

  state.ElapsedSinceLastSendMs = 249;
  CHECK(!ShouldResendStartReady(state));
  state.SendCount = 0;
  CHECK(ShouldResendStartReady(state));
  state.SendCount = 1;
  state.ElapsedSinceLastSendMs = 250;

  state.HasReceivedInputFrame = true;
  state.LastReceivedInputFrame = 103;
  CHECK(ShouldResendStartReady(state));
  state.LastReceivedInputFrame = 104;
  CHECK(!ShouldResendStartReady(state));
}

} // namespace

int main() {
  TestStartFrameAndReceivePolicy();
  TestResendPolicy();

  if (Failures != 0) {
    std::fprintf(stderr, "nsmb session policy tests failed: %d\n", Failures);
    return 1;
  }
  std::printf("nsmb session policy tests passed\n");
  return 0;
}
