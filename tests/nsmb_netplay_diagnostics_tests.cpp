#include "NsmbNetplayDiagnostics.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
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

void TestRuntimePatchLogContract() {
  using NsmbNetplayPoC::Diagnostics::Runtime;
  using NsmbNetplayPoC::Diagnostics::RuntimePatchLogKind;

  Runtime runtime;
  CHECK(!runtime.TakeRuntimePatchLog(
      -1, RuntimePatchLogKind::ForceDeathCounters));
  CHECK(!runtime.TakeRuntimePatchLog(0, RuntimePatchLogKind::Count));
  CHECK(runtime.TakeRuntimePatchLog(
      0, RuntimePatchLogKind::ForceDeathCounters));
  CHECK(!runtime.TakeRuntimePatchLog(
      0, RuntimePatchLogKind::ForceDeathCounters));
  CHECK(runtime.TakeRuntimePatchLog(
      0, RuntimePatchLogKind::ForcePowerups));
  CHECK(runtime.TakeRuntimePatchLog(
      1, RuntimePatchLogKind::ForceDeathCounters));

  runtime.ResetRuntimePatchLog(
      -1, RuntimePatchLogKind::ForceDeathCounters);
  runtime.ResetRuntimePatchLog(
      0, RuntimePatchLogKind::ForceDeathCounters);
  CHECK(runtime.TakeRuntimePatchLog(
      0, RuntimePatchLogKind::ForceDeathCounters));
  CHECK(!runtime.TakeRuntimePatchLog(
      0, RuntimePatchLogKind::ForcePowerups));
}

void TestDiagnosticJsonAndPositionContract() {
  using namespace NsmbNetplayPoC;
  using Diagnostics::DiagnosticFrameSnapshot;
  using Diagnostics::DiagnosticPlayerSnapshot;

  DiagnosticPlayerSnapshot player;
  player.Found = 1;
  player.Base = 0x12;
  player.GUID = 3;
  player.TransitioningFlag = 4;
  player.DefeatedFlag = 5;
  player.VisibleFlag = 6;
  player.CollectedStars = 7;
  std::ostringstream playerJson;
  Diagnostics::AppendDiagnosticPlayerJson(playerJson, player);
  const std::string playerText = playerJson.str();
  CHECK(playerText.rfind(
            "{\"found\":1,\"base\":\"0x00000012\",\"guid\":3,", 0) ==
        0);
  CHECK(playerText.find("\"transitioningFlag\":4") != std::string::npos);
  CHECK(playerText.find("\"defeatedFlag\":5") != std::string::npos);
  CHECK(playerText.find("\"visibleFlag\":6") != std::string::npos);
  const std::string playerSuffix = "\"collectedStars\":7}";
  CHECK(playerText.size() >= playerSuffix.size());
  CHECK(playerText.compare(playerText.size() - playerSuffix.size(),
                           playerSuffix.size(), playerSuffix) == 0);

  std::ostringstream hexState;
  hexState << std::hex << std::nouppercase;
  Diagnostics::AppendJsonHex32(hexState, "word", 0xabcdef01);
  hexState << ':' << 15;
  CHECK(hexState.str() == "\"word\":\"0xABCDEF01\":f");

  DiagnosticFrameSnapshot frame;
  frame.Valid = true;
  frame.Frame = 20;
  frame.Instance = 2;
  frame.PlayerGlobalHash = 0x123456789ABCDEF0ull;
  frame.Player[0] = player;
  frame.Player[1].GUID = 99;
  std::ostringstream frameJson;
  Diagnostics::AppendDiagnosticFrameJson(frameJson, frame);
  const std::string frameText = frameJson.str();
  CHECK(frameText.rfind("{\"frame\":20,\"instance\":2,", 0) == 0);
  CHECK(frameText.find(
            "\"playerGlobalHash\":\"0x123456789ABCDEF0\"") !=
        std::string::npos);
  CHECK(frameText.find("\"players\":[{\"found\":1") !=
        std::string::npos);
  const std::string frameSuffix = "\"collectedStars\":0}]}";
  CHECK(frameText.size() >= frameSuffix.size());
  CHECK(frameText.compare(frameText.size() - frameSuffix.size(),
                          frameSuffix.size(), frameSuffix) == 0);

  GameStateModel::GameStateSample sample;
  sample.PlayerActor0GUID = 11;
  sample.PlayerActor1GUID = 22;
  sample.Player0Lives = 3;
  sample.Player1Lives = 4;
  std::ostringstream gamePlayer0;
  std::ostringstream gamePlayer1;
  Diagnostics::AppendGameStatePlayerJson(gamePlayer0, sample, 0);
  Diagnostics::AppendGameStatePlayerJson(gamePlayer1, sample, 1);
  CHECK(gamePlayer0.str().find("\"guid\":11") != std::string::npos);
  CHECK(gamePlayer0.str().find("\"lives\":3") != std::string::npos);
  CHECK(gamePlayer1.str().find("\"guid\":22") != std::string::npos);
  CHECK(gamePlayer1.str().find("\"lives\":4") != std::string::npos);

  constexpr melonDS::s32 fixedOne = 0x1000;
  constexpr melonDS::s32 margin = 512 * fixedOne;
  constexpr melonDS::s32 largeDelta = 256 * fixedOne;
  DiagnosticFrameSnapshot current;
  current.Valid = true;
  current.Player[0].Found = 1;
  current.Player[0].VisibleFlag = 1;
  current.StageCameraGlobalX0 = 0;
  current.StageCameraGlobalY0 = 0;
  current.Player[0].PosX = static_cast<melonDS::u32>(-margin);
  CHECK(!Diagnostics::IsPlayerScreenPositionAnomalous(current, nullptr, 0));
  current.Player[0].PosX = static_cast<melonDS::u32>(-margin - 1);
  CHECK(Diagnostics::IsPlayerScreenPositionAnomalous(current, nullptr, 0));
  current.Player[0].PosX = static_cast<melonDS::u32>(256 * fixedOne + margin);
  CHECK(!Diagnostics::IsPlayerScreenPositionAnomalous(current, nullptr, 0));
  current.Player[0].PosX++;
  CHECK(Diagnostics::IsPlayerScreenPositionAnomalous(current, nullptr, 0));

  current.Player[0].PosX = 100 * fixedOne;
  DiagnosticFrameSnapshot previous = current;
  previous.Player[0].PosX =
      current.Player[0].PosX - static_cast<melonDS::u32>(largeDelta);
  CHECK(!Diagnostics::IsPlayerScreenPositionAnomalous(current, &previous, 0));
  previous.Player[0].PosX--;
  CHECK(Diagnostics::IsPlayerScreenPositionAnomalous(current, &previous, 0));
  current.Player[0].Dead = 1;
  CHECK(!Diagnostics::IsPlayerScreenPositionAnomalous(current, &previous, 0));
  CHECK(!Diagnostics::IsPlayerScreenPositionAnomalous(current, &previous, -1));

  current.Player[0].Dead = 0;
  current.Player[0].PosX = 2 * fixedOne;
  current.Player[0].PosY = 3 * fixedOne;
  previous.Player[0].PosX = fixedOne;
  previous.Player[0].PosY = fixedOne;
  std::ostringstream context;
  Diagnostics::AppendDiagnosticPlayerContextJson(context, current, &previous,
                                                  0);
  CHECK(context.str().find("\"cameraWidth\":\"0x00100000\"") !=
        std::string::npos);
  CHECK(context.str().find("\"cameraHeight\":\"0x000C0000\"") !=
        std::string::npos);
  CHECK(context.str().find("\"screenXPx\":2") != std::string::npos);
  CHECK(context.str().find("\"deltaYPx\":2") != std::string::npos);
  CHECK(context.str().find("\"previous\":{") != std::string::npos);
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
  TestRuntimePatchLogContract();
  TestDiagnosticJsonAndPositionContract();
  if (Failures != 0) {
    std::printf("nsmb_netplay_diagnostics_tests: %d failure(s)\n", Failures);
    return 1;
  }
  std::printf("nsmb_netplay_diagnostics_tests: pass\n");
  return 0;
}
