#include "NsmbNetplayCoordinator.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

namespace {

int Failures = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);         \
      Failures++;                                                              \
    }                                                                          \
  } while (false)

using NsmbNetplayPoC::Coordination::FrameBarrierKind;
using NsmbNetplayPoC::Coordination::NetplayStartWaitResult;
using NsmbNetplayPoC::Coordination::Runtime;

void TestFrameBarrier() {
  Runtime runtime;
  std::atomic<bool> firstResult{false};
  std::thread first([&] {
    firstResult = runtime.WaitAtFrameBarrier(FrameBarrierKind::Before, 0, 42, 2,
                                             500, "test");
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  CHECK(runtime.WaitAtFrameBarrier(FrameBarrierKind::Before, 1, 42, 2, 500,
                                   "test"));
  first.join();
  CHECK(firstResult.load());

  std::atomic<bool> nextResult{false};
  std::thread next([&] {
    nextResult = runtime.WaitAtFrameBarrier(FrameBarrierKind::Before, 1, 43, 2,
                                            500, "test");
  });
  CHECK(runtime.WaitAtFrameBarrier(FrameBarrierKind::Before, 0, 43, 2, 500,
                                   "test"));
  next.join();
  CHECK(nextResult.load());

  CHECK(!runtime.WaitAtFrameBarrier(FrameBarrierKind::After, 0, 9, 2, 20,
                                    "timeout-test"));
}

void TestSerialTurn() {
  Runtime runtime;
  std::atomic<bool> secondRan{false};
  std::thread second([&] {
    CHECK(runtime.WaitForSerialTurn(1, 0, 2, 500));
    secondRan = true;
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  CHECK(!secondRan.load());
  CHECK(runtime.WaitForSerialTurn(0, 0, 2, 500));
  runtime.AdvanceSerialTurn(0, 0, 2);
  second.join();
  CHECK(secondRan.load());
  runtime.AdvanceSerialTurn(1, 0, 2);
  CHECK(runtime.WaitForSerialTurn(0, 1, 2, 20));
}

void TestNetplayStartBarrier() {
  Runtime runtime;
  std::atomic<NetplayStartWaitResult> peerResult{
      NetplayStartWaitResult::TimedOut};
  std::thread peer(
      [&] { peerResult = runtime.WaitForNetplayStart(1, 0, 2, 840, 500); });
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  CHECK(runtime.WaitForNetplayStart(0, 0, 2, 840, 500) ==
        NetplayStartWaitResult::LocalLeader);
  runtime.CompleteNetplayStartWait();
  peer.join();
  CHECK(peerResult.load() == NetplayStartWaitResult::PeerComplete);
  CHECK(runtime.WaitForNetplayStart(0, 0, 2, 840, 20) ==
        NetplayStartWaitResult::AlreadyComplete);

  runtime.ResetNetplayStartWait();
  CHECK(runtime.WaitForNetplayStart(1, 0, 2, 840, 20) ==
        NetplayStartWaitResult::TimedOut);
}

void TestStateCoordination() {
  Runtime runtime;
  CHECK(!runtime.IsStateSaved(0));
  CHECK(!runtime.AllStatesSaved(2));
  CHECK(!runtime.TryBeginLocalMPSave(2));
  runtime.MarkStateSaved(0);
  CHECK(!runtime.AllStatesSaved(2));
  runtime.MarkStateSaved(1);
  CHECK(runtime.AllStatesSaved(2));
  CHECK(runtime.TryBeginLocalMPSave(2));
  CHECK(!runtime.TryBeginLocalMPSave(2));

  CHECK(!runtime.IsStateLoaded(0));
  runtime.MarkStateLoaded(0);
  CHECK(!runtime.AllStatesLoaded(2));
  runtime.MarkStateLoaded(1);
  CHECK(runtime.AllStatesLoaded(2));

  CHECK(runtime.TryBeginLocalMPLoad());
  CHECK(!runtime.TryBeginLocalMPLoad());
  CHECK(!runtime.LocalMPLoadStatus().first);
  runtime.FinishLocalMPLoad(true);
  const auto [finished, loaded] = runtime.LocalMPLoadStatus();
  CHECK(finished);
  CHECK(loaded);
}

} // namespace

int main() {
  TestFrameBarrier();
  TestSerialTurn();
  TestNetplayStartBarrier();
  TestStateCoordination();
  if (Failures != 0) {
    std::printf("nsmb_netplay_coordinator_tests: %d failure(s)\n", Failures);
    return 1;
  }
  std::printf("nsmb_netplay_coordinator_tests: pass\n");
  return 0;
}
