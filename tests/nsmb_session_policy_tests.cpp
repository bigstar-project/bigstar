#include "NsmbNetplayProtocol.h"

#include <chrono>
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

void TestFrameActivationPolicy() {
  using namespace NsmbNetplayPoC::SessionPolicy;
  CHECK(ShouldPumpNetworkAtFrame(false, 100, 0, 90));
  CHECK(ShouldPumpNetworkAtFrame(true, 0, 0, 90));
  CHECK(!ShouldPumpNetworkAtFrame(true, 100, 89, 90));
  CHECK(ShouldPumpNetworkAtFrame(true, 100, 90, 90));

  CHECK(LogicalInputFrame(false, 800, 100, 900) == 900);
  CHECK(LogicalInputFrame(true, std::nullopt, 100, 900) == 900);
  CHECK(LogicalInputFrame(true, 800, 100, 799) == 799);
  CHECK(LogicalInputFrame(true, 800, 100, 800) == 100);
  CHECK(LogicalInputFrame(true, 800, 100, 850) == 150);
}

void TestRuntimeReadyOrderingAndSendState() {
  using NsmbNetplayPoC::SessionPolicy::Runtime;
  Runtime runtime;
  const auto sentAt = Runtime::Clock::time_point(std::chrono::seconds(5));

  CHECK(!runtime.MatchSeedSent());
  runtime.MarkMatchSeedSent();
  CHECK(runtime.MatchSeedSent());
  CHECK(runtime.CanSendStartReady(false));
  runtime.MarkStartReadySent(sentAt);
  CHECK(runtime.StartReadySent());
  CHECK(runtime.StartReadySendCount() == 1);
  CHECK(runtime.LastStartReadySentAt() == sentAt);
  CHECK(!runtime.CanSendStartReady(false));
  CHECK(runtime.CanSendStartReady(true));

  runtime.ReceiveRemoteReady(90);
  CHECK(runtime.RemoteReadyFrame() == 90);
  CHECK(!runtime.RemoteReadyAfterLocal());
  runtime.BeginLocalReady(100);
  CHECK(runtime.LocalReadyFrame() == 100);
  CHECK(!runtime.RemoteReadyAfterLocal());
  runtime.ReceiveRemoteReady(101);
  CHECK(runtime.RemoteReadyFrame() == 101);
  CHECK(runtime.RemoteReadyAfterLocal());
  runtime.BeginLocalReady(999);
  CHECK(runtime.LocalReadyFrame() == 100);

  Runtime zeroSentinel;
  zeroSentinel.BeginLocalReady(0);
  zeroSentinel.ReceiveRemoteReady(0);
  zeroSentinel.MarkInputEpochPrimed(0);
  CHECK(!zeroSentinel.LocalReadyFrame());
  CHECK(!zeroSentinel.RemoteReadyFrame());
  CHECK(!zeroSentinel.InputEpochPrimedFor(0));
}

void TestRuntimeResetContracts() {
  using NsmbNetplayPoC::SessionPolicy::Runtime;
  Runtime runtime;
  const auto sentAt = Runtime::Clock::time_point(std::chrono::seconds(7));
  runtime.MarkMatchSeedSent();
  runtime.BeginLocalReady(100);
  runtime.ReceiveRemoteReady(101);
  runtime.MarkStartReadySent(sentAt);
  runtime.MarkWaitedForPeerAtStart();
  runtime.MarkInputEpochPrimed(80);
  CHECK(runtime.InputEpochPrimedFor(80));

  runtime.OnPeerConnected();
  CHECK(!runtime.MatchSeedSent());
  CHECK(!runtime.StartReadySent());
  CHECK(!runtime.RemoteReadyFrame());
  CHECK(!runtime.RemoteReadyAfterLocal());
  CHECK(runtime.LocalReadyFrame() == 100);
  CHECK(runtime.WaitedForPeerAtStart());
  CHECK(runtime.StartReadySendCount() == 1);
  CHECK(runtime.InputEpochPrimedFor(80));

  runtime.ReceiveRemoteReady(102);
  runtime.MarkStartReadySent(sentAt);
  runtime.ResetReadyWaitAfterTimeout();
  CHECK(!runtime.LocalReadyFrame());
  CHECK(runtime.RemoteReadyFrame() == 102);
  CHECK(!runtime.RemoteReadyAfterLocal());
  CHECK(!runtime.StartReadySent());
  CHECK(runtime.StartReadySendCount() == 2);

  runtime.MarkMatchSeedSent();
  runtime.ResetStartHandshake();
  CHECK(runtime.MatchSeedSent());
  CHECK(!runtime.WaitedForPeerAtStart());
  CHECK(!runtime.StartReadySent());
  CHECK(runtime.StartReadySendCount() == 0);
  CHECK(runtime.LastStartReadySentAt() == Runtime::Clock::time_point{});
  CHECK(!runtime.LocalReadyFrame());
  CHECK(!runtime.RemoteReadyFrame());
  CHECK(!runtime.RemoteReadyAfterLocal());
  CHECK(!runtime.InputEpochPrimedFor(80));
}

} // namespace

int main() {
  TestStartFrameAndReceivePolicy();
  TestResendPolicy();
  TestFrameActivationPolicy();
  TestRuntimeReadyOrderingAndSendState();
  TestRuntimeResetContracts();

  if (Failures != 0) {
    std::fprintf(stderr, "nsmb session policy tests failed: %d\n", Failures);
    return 1;
  }
  std::printf("nsmb session policy tests passed\n");
  return 0;
}
