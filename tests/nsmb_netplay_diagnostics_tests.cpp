#include "NsmbNetplayDiagnostics.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace {

int Failures = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);         \
      Failures++;                                                              \
    }                                                                          \
  } while (false)

std::vector<melonDS::u32> ReadFrames(const std::filesystem::path &path) {
  std::ifstream input(path);
  std::vector<melonDS::u32> frames;
  melonDS::u32 frame = 0;
  while (input >> frame)
    frames.push_back(frame);
  return frames;
}

std::string ReadText(const std::filesystem::path &path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void TestFrameHeartbeatFileContract() {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      "nsmb_netplay_diagnostics_heartbeat_test.txt";
  std::error_code error;
  std::filesystem::remove(path, error);

  NsmbNetplayPoC::Diagnostics::Runtime runtime;
  CHECK(runtime.ConfigureFrameHeartbeat(2, path.string()));
  CHECK(!runtime.PublishFrameHeartbeat(0, 2, false));
  CHECK(!runtime.PublishFrameHeartbeat(-1, 2, true));
  CHECK(!runtime.PublishFrameHeartbeat(0, 1, true));
  CHECK(runtime.PublishFrameHeartbeat(0, 2, true));
  CHECK(!runtime.PublishFrameHeartbeat(0, 2, true));
  CHECK(runtime.PublishFrameHeartbeat(1, 2, true));
  CHECK(runtime.PublishFrameHeartbeat(0, 4, true));

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
  std::vector<melonDS::u32> frames;
  while (std::chrono::steady_clock::now() < deadline) {
    frames = ReadFrames(path);
    if (!frames.empty() && frames.back() == 4)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  runtime.Stop();

  CHECK(!frames.empty());
  CHECK(frames.back() == 4);
  for (const melonDS::u32 frame : frames)
    CHECK((frame % 2) == 0);
  std::filesystem::remove(path, error);
}

void TestConsoleOnlyHeartbeatContract() {
  NsmbNetplayPoC::Diagnostics::Runtime runtime;
  CHECK(!runtime.ConfigureFrameHeartbeat(3, {}));
  CHECK(!runtime.PublishFrameHeartbeat(0, 2, true));
  CHECK(runtime.PublishFrameHeartbeat(0, 3, true));
  CHECK(!runtime.PublishFrameHeartbeat(0, 3, true));
}

void TestHashLogContract() {
  const std::filesystem::path basicPath =
      std::filesystem::temp_directory_path() /
      "nsmb_netplay_diagnostics_hash_test.csv";
  const std::filesystem::path screenPath =
      std::filesystem::temp_directory_path() /
      "nsmb_netplay_diagnostics_screen_hash_test.csv";
  std::error_code error;
  std::filesystem::remove(basicPath, error);
  std::filesystem::remove(screenPath, error);

  {
    NsmbNetplayPoC::Diagnostics::Runtime runtime;
    CHECK(runtime.ConfigureHashLog(basicPath.string(), false));
    CHECK(!runtime.RecordFrameHash(-1, 60, 0x1, 0));
    CHECK(!runtime.RecordFrameHash(0, 0, 0x1, 0));
    CHECK(runtime.RecordFrameHash(0, 60, 0x1234, 0));
    CHECK(!runtime.RecordFrameHash(0, 60, 0x9999, 0));
    CHECK(runtime.RecordFrameHash(1, 60, 0xabcd, 0));
    runtime.Stop();
  }
  CHECK(ReadText(basicPath) ==
        "instance,frame,hash\n0,60,1234\n1,60,abcd\n");

  {
    NsmbNetplayPoC::Diagnostics::Runtime runtime;
    CHECK(runtime.ConfigureHashLog(screenPath.string(), true));
    CHECK(runtime.RecordFrameHash(0, 1, 0x1a, 0x2b));
    runtime.Stop();
  }
  CHECK(ReadText(screenPath) ==
        "instance,frame,hash,screenHash\n0,1,1a,2b\n");

  std::filesystem::remove(basicPath, error);
  std::filesystem::remove(screenPath, error);
}

void TestDiagnosticEventLogContract() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      "nsmb_netplay_diagnostics_event_test";
  const std::filesystem::path firstPath = root / "nested" / "events.jsonl";
  const std::filesystem::path secondPath = root / "other.jsonl";
  std::error_code error;
  std::filesystem::remove_all(root, error);

  NsmbNetplayPoC::Diagnostics::Runtime runtime;
  CHECK(!runtime.WriteDiagnosticEvent({}, "{}"));
  CHECK(runtime.WriteDiagnosticEvent(firstPath.string(),
                                     "{\"event\":\"started\"}"));
  CHECK(ReadText(firstPath) == "{\"event\":\"started\"}\n");
  CHECK(runtime.WriteDiagnosticEvent(firstPath.string(),
                                     "{\"event\":\"ready\"}"));
  CHECK(ReadText(firstPath) ==
        "{\"event\":\"started\"}\n{\"event\":\"ready\"}\n");
  CHECK(runtime.WriteDiagnosticEvent(secondPath.string(),
                                     "{\"event\":\"moved\"}"));
  runtime.Stop();

  CHECK(ReadText(secondPath) == "{\"event\":\"moved\"}\n");
  std::filesystem::remove_all(root, error);
}

void TestPerformanceRuntimeContract() {
  using Runtime = NsmbNetplayPoC::Diagnostics::Runtime;
  const Runtime::TimePoint start = Runtime::TimePoint{} + std::chrono::seconds(1);

  Runtime runtime;
  CHECK(runtime.TestElapsedMs(start) == 0);
  runtime.StartTestTimer(start);
  runtime.StartTestTimer(start + std::chrono::milliseconds(500));
  CHECK(runtime.TestElapsedMs(start + std::chrono::milliseconds(1500)) ==
        1500);

  CHECK(!runtime.StartActiveTimer(-1, 100, start));
  CHECK(runtime.StartActiveTimer(0, 100, start));
  CHECK(!runtime.StartActiveTimer(0, 200, start));
  CHECK(runtime.IsActiveTimerStarted(0));
  CHECK(!runtime.IsActiveTimerStarted(16));

  Runtime::ActiveFrameSample sample = runtime.RecordActiveFrameTiming(
      0, 100, start + std::chrono::microseconds(1000), true, 20000, 0, 0);
  CHECK(!sample.Recorded);
  sample = runtime.RecordActiveFrameTiming(
      0, 101, start + std::chrono::microseconds(17667), true, 20000, 1, 2);
  CHECK(sample.Recorded);
  CHECK(sample.ElapsedUs == 16667);
  CHECK(!sample.Spike);
  sample = runtime.RecordActiveFrameTiming(
      0, 102, start + std::chrono::microseconds(34335), true, 20000, 2, 3);
  CHECK(sample.Recorded);
  CHECK(sample.ElapsedUs == 16668);
  CHECK(!sample.Spike);
  sample = runtime.RecordActiveFrameTiming(
      0, 103, start + std::chrono::microseconds(59336), true, 20000, 5, 7);
  CHECK(sample.Spike);
  CHECK(sample.RollbackRestoreDelta == 5);
  CHECK(sample.RollbackResimulateDelta == 7);
  sample = runtime.RecordActiveFrameTiming(
      0, 104, start + std::chrono::microseconds(92671), true, 20000, 8, 9);
  CHECK(sample.Spike);
  CHECK(sample.RollbackRestoreDelta == 3);
  CHECK(sample.RollbackResimulateDelta == 2);

  const Runtime::ActiveFrameSummary summary = runtime.ActiveFrameTimingSummary(
      0, 105, start + std::chrono::milliseconds(100));
  CHECK(summary.Started);
  CHECK(summary.StartFrame == 100);
  CHECK(summary.Frames == 5);
  CHECK(summary.ElapsedMs == 100);
  CHECK(summary.Samples == 4);
  CHECK(summary.TotalUs == 91671);
  CHECK(summary.MaxUs == 33335);
  CHECK(summary.MaxFrame == 104);
  CHECK(summary.Over16ms == 3);
  CHECK(summary.Over25ms == 2);
  CHECK(summary.Over33ms == 1);

  CHECK(!runtime.ShouldTraceGameplayHeartbeat(-1, 20, 20, 10));
  CHECK(!runtime.ShouldTraceGameplayHeartbeat(0, 10, 20, 10));
  CHECK(runtime.ShouldTraceGameplayHeartbeat(0, 20, 20, 10));
  CHECK(!runtime.ShouldTraceGameplayHeartbeat(0, 20, 20, 10));
  CHECK(!runtime.ShouldTraceGameplayHeartbeat(0, 21, 20, 10));
  CHECK(runtime.ShouldTraceGameplayHeartbeat(0, 30, 20, 10));
  CHECK(runtime.ShouldTraceGameplayHeartbeat(1, 20, 20, 10));
}

void TestDiagnosticSnapshotRuntimeContract() {
  using NsmbNetplayPoC::Diagnostics::DiagnosticFrameSnapshot;
  using NsmbNetplayPoC::Diagnostics::Runtime;
  using NsmbNetplayPoC::Diagnostics::kDiagnosticRingCapacity;

  Runtime runtime;
  CHECK(!runtime.LatestDiagnosticSnapshot(-1));
  CHECK(runtime.DiagnosticSnapshotWindow(16, 10).empty());

  DiagnosticFrameSnapshot snapshot;
  snapshot.Valid = true;
  snapshot.Instance = 0;
  snapshot.Frame = 1;
  snapshot.Player[0].PosX = 100;
  runtime.RecordDiagnosticSnapshot(-1, snapshot);
  runtime.RecordDiagnosticSnapshot(0, snapshot);
  auto latest = runtime.LatestDiagnosticSnapshot(0);
  CHECK(latest && latest->Frame == 1 && latest->Player[0].PosX == 100);

  snapshot.Frame = 2;
  snapshot.Player[0].PosX = 200;
  runtime.RecordDiagnosticSnapshot(0, snapshot);
  std::vector<DiagnosticFrameSnapshot> window =
      runtime.DiagnosticSnapshotWindow(0, 1);
  CHECK(window.size() == 1 && window[0].Frame == 2);
  window = runtime.DiagnosticSnapshotWindow(0, 2);
  CHECK(window.size() == 2 && window[0].Frame == 1 &&
        window[1].Frame == 2);

  Runtime wrapped;
  for (std::size_t frame = 1; frame <= kDiagnosticRingCapacity; frame++) {
    snapshot.Frame = static_cast<melonDS::u32>(frame);
    wrapped.RecordDiagnosticSnapshot(0, snapshot);
  }
  CHECK(!wrapped.LatestDiagnosticSnapshot(0));
  window = wrapped.DiagnosticSnapshotWindow(0, 3);
  CHECK(window.size() == 3 && window[0].Frame == 718 &&
        window[1].Frame == 719 && window[2].Frame == 720);
  snapshot.Frame = 721;
  wrapped.RecordDiagnosticSnapshot(0, snapshot);
  latest = wrapped.LatestDiagnosticSnapshot(0);
  CHECK(latest && latest->Frame == 721);
  window = wrapped.DiagnosticSnapshotWindow(0, 3);
  CHECK(window.size() == 3 && window[0].Frame == 719 &&
        window[1].Frame == 720 && window[2].Frame == 721);

  CHECK(!runtime.TakeDueDiagnosticPostTrigger(0, 100));
  runtime.ScheduleDiagnosticPostTrigger(-1, 120);
  CHECK(!runtime.TakeDueDiagnosticPostTrigger(0, 120));
  runtime.ScheduleDiagnosticPostTrigger(0, 120);
  CHECK(!runtime.TakeDueDiagnosticPostTrigger(0, 119));
  const auto trigger = runtime.TakeDueDiagnosticPostTrigger(0, 120);
  CHECK(trigger && *trigger == 120);
  CHECK(!runtime.TakeDueDiagnosticPostTrigger(0, 121));
}

void TestDiagnosticEventThrottleContract() {
  using NsmbNetplayPoC::Diagnostics::Runtime;
  Runtime runtime;

  CHECK(!runtime.ShouldEmitDiagnosticMismatch(-1, 100, 300));
  CHECK(runtime.ShouldEmitDiagnosticMismatch(0, 100, 300));
  CHECK(!runtime.ShouldEmitDiagnosticMismatch(0, 399, 300));
  CHECK(runtime.ShouldEmitDiagnosticMismatch(0, 400, 300));
  CHECK(runtime.ShouldEmitDiagnosticMismatch(1, 100, 300));

  CHECK(!runtime.ShouldEmitDiagnosticLifeEvent(0, -1, 100, false, 300));
  CHECK(runtime.ShouldEmitDiagnosticLifeEvent(0, 0, 100, false, 300));
  CHECK(!runtime.ShouldEmitDiagnosticLifeEvent(0, 0, 100, false, 300));
  CHECK(!runtime.ShouldEmitDiagnosticLifeEvent(0, 0, 399, true, 300));
  CHECK(runtime.ShouldEmitDiagnosticLifeEvent(0, 0, 400, true, 300));
  CHECK(runtime.ShouldEmitDiagnosticLifeEvent(0, 0, 401, false, 300));
  CHECK(runtime.ShouldEmitDiagnosticLifeEvent(0, 1, 100, false, 300));

  CHECK(!runtime.ShouldEmitDiagnosticPitTransition(0, 2, 100, 120));
  CHECK(runtime.ShouldEmitDiagnosticPitTransition(0, 0, 100, 120));
  CHECK(!runtime.ShouldEmitDiagnosticPitTransition(0, 0, 219, 120));
  CHECK(runtime.ShouldEmitDiagnosticPitTransition(0, 0, 220, 120));
  CHECK(runtime.ShouldEmitDiagnosticPitTransition(0, 1, 100, 120));

  CHECK(!runtime.ShouldEmitDiagnosticPositionAnomaly(16, 0, 100, 120));
  CHECK(runtime.ShouldEmitDiagnosticPositionAnomaly(0, 0, 100, 120));
  CHECK(!runtime.ShouldEmitDiagnosticPositionAnomaly(0, 0, 219, 120));
  CHECK(runtime.ShouldEmitDiagnosticPositionAnomaly(0, 0, 220, 120));
  CHECK(runtime.ShouldEmitDiagnosticPositionAnomaly(1, 0, 100, 120));
}

void TestPlayerLifeObservationContract() {
  using NsmbNetplayPoC::Diagnostics::PlayerLifeState;
  using NsmbNetplayPoC::Diagnostics::Runtime;

  Runtime runtime;
  PlayerLifeState initial;
  CHECK(sizeof(PlayerLifeState) == 8 * sizeof(melonDS::u32));
  initial.Lives[0] = 5;
  initial.Lives[1] = 4;
  initial.Deaths[0] = 1;
  initial.Deaths[1] = 2;
  initial.Dead[0] = 0;
  initial.Dead[1] = 1;
  initial.Transition[0] = 3;
  initial.Transition[1] = 4;

  auto observation = runtime.ObservePlayerLifeState(-1, initial);
  CHECK(!observation.Accepted);
  observation = runtime.ObservePlayerLifeState(0, initial);
  CHECK(observation.Accepted);
  CHECK(observation.Changed);
  CHECK(!observation.HadPrevious);

  observation = runtime.ObservePlayerLifeState(0, initial);
  CHECK(observation.Accepted);
  CHECK(!observation.Changed);
  CHECK(observation.HadPrevious);
  CHECK(observation.Previous.Lives[0] == 5);
  CHECK(observation.Previous.Deaths[1] == 2);
  CHECK(observation.Previous.Dead[1] == 1);
  CHECK(observation.Previous.Transition[0] == 3);

  PlayerLifeState changed = initial;
  changed.Lives[0] = 4;
  changed.Deaths[1] = 3;
  changed.Dead[0] = 1;
  changed.Transition[1] = 5;
  observation = runtime.ObservePlayerLifeState(0, changed);
  CHECK(observation.Changed);
  CHECK(observation.HadPrevious);
  CHECK(observation.Previous.Lives[0] == 5);
  CHECK(observation.Previous.Deaths[1] == 2);
  CHECK(observation.Previous.Dead[0] == 0);
  CHECK(observation.Previous.Transition[1] == 4);

  observation = runtime.ObservePlayerLifeState(1, changed);
  CHECK(observation.Changed);
  CHECK(!observation.HadPrevious);
}

} // namespace

int main() {
  TestFrameHeartbeatFileContract();
  TestConsoleOnlyHeartbeatContract();
  TestHashLogContract();
  TestDiagnosticEventLogContract();
  TestPerformanceRuntimeContract();
  TestDiagnosticSnapshotRuntimeContract();
  TestDiagnosticEventThrottleContract();
  TestPlayerLifeObservationContract();
  if (Failures != 0) {
    std::printf("nsmb_netplay_diagnostics_tests: %d failure(s)\n", Failures);
    return 1;
  }
  std::printf("nsmb_netplay_diagnostics_tests: pass\n");
  return 0;
}
