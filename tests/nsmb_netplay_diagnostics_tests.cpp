#include "NsmbNetplayDiagnostics.h"
#include "NsmbImitationAI.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
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

std::uint64_t Fnv1a64(const std::string &text) {
  std::uint64_t hash = 1469598103934665603ull;
  for (const unsigned char byte : text) {
    hash ^= byte;
    hash *= 1099511628211ull;
  }
  return hash;
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

void TestRamDumpFrameSelectionContract() {
  using namespace NsmbNetplayPoC;
  using Diagnostics::ShouldCaptureRamDumpFrame;
  const std::vector<std::pair<melonDS::u32, melonDS::u32>> ranges = {
      {10, 12}, {20, 20}};

  CHECK(!ShouldCaptureRamDumpFrame(0, 0, {}));
  CHECK(ShouldCaptureRamDumpFrame(0, 5, {}));
  CHECK(!ShouldCaptureRamDumpFrame(4, 5, {}));
  CHECK(ShouldCaptureRamDumpFrame(5, 5, {}));
  CHECK(!ShouldCaptureRamDumpFrame(9, 0, ranges));
  CHECK(ShouldCaptureRamDumpFrame(10, 0, ranges));
  CHECK(ShouldCaptureRamDumpFrame(12, 0, ranges));
  CHECK(!ShouldCaptureRamDumpFrame(13, 0, ranges));
  CHECK(ShouldCaptureRamDumpFrame(20, 0, ranges));

  Config::DiagnosticsConfig config;
  CHECK(!Diagnostics::ShouldCaptureScreenshotFrame(config, 0));
  config.ScreenshotDir = "screenshots";
  config.ScreenshotInterval = 5;
  CHECK(Diagnostics::ShouldCaptureScreenshotFrame(config, 0));
  CHECK(!Diagnostics::ShouldCaptureScreenshotFrame(config, 4));
  CHECK(Diagnostics::ShouldCaptureScreenshotFrame(config, 5));
}

void TestRamDumpArtifactContract() {
  using namespace NsmbNetplayPoC;
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() /
      "nsmb_netplay_diagnostics_ram_dump_test";
  std::error_code error;
  std::filesystem::remove_all(directory, error);

  Config::DiagnosticsConfig config;
  config.RamDumpDir = directory.string();
  const std::vector<std::pair<melonDS::u32, melonDS::u32>> ranges = {
      {10, 10}};
  const std::array<melonDS::u8, 8> mainRAM = {0, 1, 2, 3, 4, 5, 6, 7};
  const std::filesystem::path path =
      directory / "inst2_frame000010_mainram.bin";

  Diagnostics::CaptureRamDumpIfNeeded(config, ranges, 2, 9, mainRAM.data(),
                                      mainRAM.size());
  CHECK(!std::filesystem::exists(path));
  Diagnostics::CaptureRamDumpIfNeeded(config, ranges, 2, 10, mainRAM.data(),
                                      mainRAM.size());
  CHECK(std::filesystem::exists(path));
  std::ifstream input(path, std::ios::binary);
  const std::vector<char> actual{std::istreambuf_iterator<char>(input),
                                 std::istreambuf_iterator<char>()};
  const std::vector<char> expected(mainRAM.begin(), mainRAM.end());
  CHECK(actual == expected);
  input.close();
  std::filesystem::remove_all(directory, error);
  CHECK(!error);
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

void TestDiagnosticEventFormattingContract() {
  using namespace NsmbNetplayPoC;
  using Diagnostics::DiagnosticFrameSnapshot;

  CHECK(Diagnostics::JsonEscape("a\\b\"c\n\x01") ==
        "a\\\\b\\\"c\\n\\u0001");
  CHECK(Diagnostics::FormatStartReadyEvent(
            "host", "send", 10, 13, 0, 8, 11, 12, 2, 3, 4) ==
        "{\"event\":\"start_ready\",\"role\":\"host\",\"direction\":\"send\","
        "\"localFrame\":10,\"remoteFrame\":13,\"delta\":3,\"logicalStart\":8,"
        "\"lastSentInputFrame\":11,\"lastReceivedInputFrame\":12,"
        "\"localQueue\":2,\"remoteQueue\":3,\"delayedInputs\":4}");
  CHECK(Diagnostics::FormatStartReadyEvent(
            "client", nullptr, 0, 20, 0, 8, 0, 0, 0, 0, 0)
            .find("\"direction\":\"unknown\",\"localFrame\":0,"
                  "\"remoteFrame\":20,\"delta\":0") != std::string::npos);
  CHECK(Diagnostics::FormatDiagnosticStartupEvent(
            "client", 120, true, false, 3, "C:\\diag\".json", "events\n.jsonl") ==
        "{\"event\":\"diagnostic_started\",\"role\":\"client\",\"ringFrames\":120,"
        "\"stateSync\":true,\"stateSyncExtended\":false,\"stateSyncInterval\":3,"
        "\"diagnosticsFile\":\"C:\\\\diag\\\".json\","
        "\"eventsFile\":\"events\\n.jsonl\"}");

  DiagnosticFrameSnapshot previous;
  previous.Valid = true;
  previous.Frame = 19;
  previous.Instance = 2;
  previous.StageID = 9;
  previous.StageGroup = 8;
  previous.Player[0].Found = 1;
  previous.Player[0].VisibleFlag = 1;
  previous.Player[0].PosX = 0x1000;
  previous.Player[0].PosY = 0x2000;

  DiagnosticFrameSnapshot current = previous;
  current.Frame = 20;
  current.SceneCurrentSceneID = 7;
  current.SceneNextSceneID = 6;
  current.PlayerGlobalHash0 = 0x101;
  current.PlayerGlobalHash1 = 0x202;
  current.PlayerActorHash0 = 0x303;
  current.PlayerActorHash1 = 0x404;
  current.Player[0].PosX = 0x3000;
  current.Player[0].PosY = 0x5000;
  current.Player[0].VelX = 0x6000;
  current.Player[0].VelY = 0x7000;
  current.Player[0].Deaths = 2;
  current.Player[0].BattleStars = 3;
  current.Player[0].Coins = 4;
  current.Player[1].Found = 1;
  current.Player[1].Deaths = 5;
  current.Player[1].BattleStars = 6;
  current.Player[1].Coins = 7;
  const std::vector<DiagnosticFrameSnapshot> ring{previous, current};

  const std::string playerEvent =
      Diagnostics::FormatDiagnosticPlayerSnapshotEvent(
          "player_position_anomaly", "host", 2, current, &previous, 0, ring);
  const std::string postEvent = Diagnostics::FormatDiagnosticPostWindowEvent(
      "client", 2, 30, 30, ring);

  GameStateModel::GameStateSyncHashes localHashes;
  localHashes.Basic = 0x11;
  localHashes.PlayerGlobal = 0x22;
  localHashes.WifiCandidate = 0x33;
  localHashes.RenderCandidate = 0x44;
  GameStateModel::GameStateSyncHashes remoteHashes;
  remoteHashes.Basic = 0x55;
  remoteHashes.PlayerGlobal = 0x66;
  remoteHashes.WifiCandidate = 0x77;
  remoteHashes.RenderCandidate = 0x88;
  GameStateModel::GameStateSample remoteSample;
  remoteSample.StageID = 10;
  remoteSample.StageGroup = 8;
  remoteSample.PlayerActor0PosX = 0x3001;
  remoteSample.PlayerActor0PosY = 0x5000;
  remoteSample.PlayerActor0VelX = 0x6002;
  remoteSample.PlayerActor0VelY = 0x7000;
  remoteSample.PlayerActor1PosX = 0x8000;
  remoteSample.PlayerActor1PosY = 0x9000;
  remoteSample.PlayerActor1VelX = 0xA000;
  remoteSample.PlayerActor1VelY = 0xB000;
  remoteSample.Player0Deaths = 2;
  remoteSample.Player1Deaths = 8;
  remoteSample.Player0BattleStars = 3;
  remoteSample.Player1BattleStars = 9;
  remoteSample.VsCoinCount = 12;
  const std::string mismatchEvent =
      Diagnostics::FormatPlayerGlobalMismatchEvent(
          "host", 2, 20, localHashes, remoteHashes, &current, &remoteSample,
          ring);
  Diagnostics::DiagnosticMovingHazardSnapshot hazard;
  hazard.GUID = 31;
  hazard.Base = 0x10203040;
  hazard.PosX = 0x11223344;
  hazard.PosY = 0x55667788;
  hazard.VelX = 0x99AABBCC;
  hazard.VelY = 0xDDEEFF00;
  hazard.StateType = 2;
  hazard.Flags = 0xABCDEF01;
  const std::vector<Diagnostics::DiagnosticMovingHazardSnapshot> hazards{
      hazard};
  const std::string lifeEvent = Diagnostics::FormatPlayerLifeEvent(
      "client", "death", 2, 20, 1, remoteSample, hazards, true, ring);
  const std::string transitionEvent = Diagnostics::FormatPlayerLifeEvent(
      "host", "death-transition", 2, 21, 0, remoteSample, hazards, false,
      ring);

  CHECK(playerEvent.rfind(
            "{\"event\":\"player_position_anomaly\",\"role\":\"host\","
            "\"instance\":2,\"frame\":20,",
            0) == 0);
  CHECK(postEvent.rfind(
            "{\"event\":\"diagnostic_post_window\",\"role\":\"client\","
            "\"instance\":2,\"frame\":30,\"triggerUntilFrame\":30,",
            0) == 0);
  CHECK(mismatchEvent.find(
            "\"remoteSampleDiffs\":[{\"field\":\"stageID\","
            "\"local\":\"0x00000009\",\"remote\":\"0x0000000A\"}") !=
        std::string::npos);
  CHECK(lifeEvent.find(
            "\"nearbyMovingHazards\":[{\"guid\":31,"
            "\"base\":\"0x10203040\",\"x\":\"0x11223344\",") !=
        std::string::npos);
  CHECK(lifeEvent.find("\"ring\":[") != std::string::npos);
  CHECK(transitionEvent.find("\"reason\":\"death-transition\"") !=
        std::string::npos);
  CHECK(transitionEvent.find("\"ring\":[") == std::string::npos);
  CHECK(Fnv1a64(playerEvent) == 17232318831486508629ull);
  CHECK(Fnv1a64(postEvent) == 10915587500136319599ull);
  CHECK(Fnv1a64(mismatchEvent) == 1812141437166136686ull);
  CHECK(Fnv1a64(lifeEvent) == 5563945675449118778ull);
  CHECK(Fnv1a64(transitionEvent) == 9015990334359297348ull);
}

void TestStartupReportFormattingContract() {
  using namespace NsmbNetplayPoC;
  Config::BootstrapConfig bootstrap;
  bootstrap.TestFrames = 123;
  bootstrap.TestInstanceCount = 2;
  bootstrap.HashInterval = 17;
  bootstrap.InputTraceEnabled = true;
  Config::DiagnosticsConfig diagnostics;
  diagnostics.HashLogPath = "hash.csv";
  diagnostics.ScreenshotDir = "shots";
  diagnostics.RamDumpDir = "ram";
  diagnostics.GameStateTracePath = "state.csv";
  diagnostics.GameStateTraceInterval = 9;
  Config::HarnessConfig harness;
  harness.InputScriptPath = "input.txt";
  harness.FrameBarrierEnabled = true;
  harness.WaitForPeerBeforeStart = true;
  harness.NetworkPumpThreadEnabled = true;
  Config::StateSyncConfig stateSync;
  stateSync.GameEnabled = true;
  Config::PacketBridgeConfig packetBridge;
  packetBridge.Enabled = true;
  Config::MvlConfig mvl;
  mvl.NetRandom.Enabled = true;
  mvl.NetRandom.Value = 0xABCDEF01;
  mvl.MatchSeedConfigured = true;
  mvl.MatchSeed = 0x10203040;

  const std::string testReport = Diagnostics::FormatTestStartupReport(
      123456789, bootstrap, diagnostics, harness, stateSync, packetBridge, mvl,
      3, 4, 0x55667788);
  CHECK(testReport.rfind("NSMB Test: enabled tUnixMs=123456789 ", 0) == 0);
  CHECK(testReport.find("netRandomValue=0xABCDEF01") != std::string::npos);
  CHECK(testReport.find("mvlStage=4") != std::string::npos);
  CHECK(!testReport.empty() && testReport.back() == '\n');

  Config::ConnectionConfig connection;
  connection.Port = 9001;
  connection.PeerHost = "peer.example";
  connection.Delay = 8;
  connection.LocalInstance = 1;
  connection.StartFrame = 840;
  Config::InputConfig input;
  input.NetplayOnly = true;
  input.UseHistoryBundle = true;
  input.BundleHistory = 6;
  input.DropModulo = 11;
  Config::RollbackConfig rollback;
  rollback.Enabled = true;
  rollback.Backend = Config::RollbackBackend::CorePreimage;
  rollback.Window = 32;
  const std::string netplayReport = Diagnostics::FormatNetplayStartupReport(
      987654321, "client", connection, harness, packetBridge, input, rollback,
      "corepreimage", mvl, 3, 0x11223344);
  CHECK(netplayReport.rfind("NSMB PoC: enabled tUnixMs=987654321 role=client ",
                            0) == 0);
  CHECK(netplayReport.find("port=9001 peer=peer.example delay=8") !=
        std::string::npos);
  CHECK(netplayReport.find("rollback=1 rollbackBackend=corepreimage rollbackWindow=32") !=
        std::string::npos);
  CHECK(netplayReport.find("matchSeed=0x10203040 seedConfigured=1") !=
        std::string::npos);
  CHECK(!netplayReport.empty() && netplayReport.back() == '\n');

  CHECK(Fnv1a64(testReport) == 17818733127289311667ull);
  CHECK(Fnv1a64(netplayReport) == 3455746755802390722ull);
}

void TestAIStartupReportFormattingContract() {
  using namespace NsmbNetplayPoC;
  NsmbImitationAI::ModelInitializationResult disabled;
  CHECK(Diagnostics::FormatImitationModelInitializationReport(
            "ignored.json", disabled)
            .empty());

  NsmbImitationAI::ModelInitializationResult empty;
  empty.RequestedEnabled = true;
  empty.ModelPathEmpty = true;
  CHECK(Diagnostics::FormatImitationModelInitializationReport("", empty) ==
        "NSMB ImitationAI: enabled but MELONDS_NSML_IMITATION_AI_MODEL is "
        "empty\n");

  NsmbImitationAI::ModelInitializationResult failed;
  failed.RequestedEnabled = true;
  failed.Errors.TorchCompact = "torch error";
  failed.Errors.Compact = "compact error";
  failed.Errors.Linear = "linear error";
  const std::string failedReport =
      Diagnostics::FormatImitationModelInitializationReport("policy.json",
                                                            failed);
  CHECK(failedReport ==
        "NSMB ImitationAI: failed to load model path=policy.json "
        "torchCompactError=torch error compactError=compact error "
        "linearError=linear error\n");
  failed.Loaded = true;
  CHECK(Diagnostics::FormatImitationModelInitializationReport("policy.json",
                                                              failed)
            .empty());

  Config::AIConfig ai;
  ai.Rule.Enabled = true;
  ai.Rule.HostOnly = true;
  ai.Rule.PlayerSpec = "local";
  ai.Rule.StartFrame = 123;
  ai.Rule.HorizontalDeadzone = 0x111;
  ai.Rule.HorizontalWrapWidth = 0x222;
  ai.Rule.CloseRange = 0x333;
  ai.Rule.HazardHorizontalRange = 0x444;
  ai.Rule.HazardVerticalRange = 0x555;
  ai.Rule.JumpFrames = 6;
  ai.Rule.JumpInterval = 17;
  ai.Rule.TraceEnabled = true;
  ai.Rule.TraceInterval = 19;
  ai.Imitation.HostOnly = true;
  ai.Imitation.PlayerSpec = "remote";
  ai.Imitation.StartFrame = 456;
  ai.Imitation.Threshold = 0.625;
  ai.Imitation.AllowedHeldMask = 0x8A3;
  ai.Imitation.TraceEnabled = true;
  ai.Imitation.TraceInterval = 23;
  ai.Imitation.InferInterval = 29;
  ai.Imitation.NeutralHoldFrames = 31;
  ai.Imitation.ModelPath = "model.json";
  ai.Imitation.HazardGuardEnabled = true;
  ai.Imitation.HazardGuardHorizontalRange = 0x666;
  ai.Imitation.HazardGuardVerticalRange = 0x777;
  ai.Imitation.HazardGuardCloseRange = 0x888;

  NsmbImitationAI::ModelDescription model;
  model.Type = NsmbImitationAI::ModelType::TorchCompact;
  model.FeatureCount = 101;
  model.OutputCount = 7;
  model.Schema = "torch-schema";
  model.DetailSchema = "torch-labels";
  const std::string torchReport =
      Diagnostics::FormatAIStartupReport(ai, true, model);
  CHECK(torchReport.find("NSMB RuleAI: enabled player=local startFrame=123") ==
        0);
  CHECK(torchReport.find("modelType=torchCompact") != std::string::npos);
  CHECK(torchReport.find("features=101 heads=7 schema=torch-schema "
                         "labelSchema=torch-labels") != std::string::npos);
  CHECK(torchReport.find("hazardGuard enabled=1 horizontalRange=0x666") !=
        std::string::npos);

  model.Type = NsmbImitationAI::ModelType::Compact;
  model.FeatureCount = 202;
  model.OutputCount = 8;
  model.Schema = "compact-schema";
  model.DetailSchema = "compact-labels";
  const std::string compactReport =
      Diagnostics::FormatAIStartupReport(ai, true, model);
  CHECK(compactReport.find("modelType=compact") != std::string::npos);
  CHECK(compactReport.find("features=202 heads=8 schema=compact-schema "
                           "labelSchema=compact-labels") != std::string::npos);

  model.Type = NsmbImitationAI::ModelType::Linear;
  model.FeatureCount = 303;
  model.OutputCount = 9;
  model.Schema = "linear-schema";
  model.DetailSchema = "linear-features";
  const std::string linearReport =
      Diagnostics::FormatAIStartupReport(ai, true, model);
  CHECK(linearReport.find("modelType=linear threshold=0.625") !=
        std::string::npos);
  CHECK(linearReport.find("features=303 buttons=9 schema=linear-schema "
                          "featureSchema=linear-features") !=
        std::string::npos);

  CHECK(Diagnostics::FormatAIStartupReport(ai, false, model).find(
            "NSMB ImitationAI:") == std::string::npos);
  CHECK(Fnv1a64(failedReport) == 11833481489812603706ull);
  CHECK(Fnv1a64(torchReport) == 11100805931403676302ull);
  CHECK(Fnv1a64(compactReport) == 8465431890678921053ull);
  CHECK(Fnv1a64(linearReport) == 9194670781819015089ull);
}

} // namespace

int main() {
  TestFrameHeartbeatFileContract();
  TestConsoleOnlyHeartbeatContract();
  TestRamDumpFrameSelectionContract();
  TestRamDumpArtifactContract();
  TestHashLogContract();
  TestDiagnosticEventLogContract();
  TestPerformanceRuntimeContract();
  TestDiagnosticSnapshotRuntimeContract();
  TestDiagnosticEventThrottleContract();
  TestPlayerLifeObservationContract();
  TestRuntimePatchLogContract();
  TestDiagnosticJsonAndPositionContract();
  TestDiagnosticEventFormattingContract();
  TestStartupReportFormattingContract();
  TestAIStartupReportFormattingContract();
  if (Failures != 0) {
    std::printf("nsmb_netplay_diagnostics_tests: %d failure(s)\n", Failures);
    return 1;
  }
  std::printf("nsmb_netplay_diagnostics_tests: pass\n");
  return 0;
}
