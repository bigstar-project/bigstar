#include "NsmbGameState.h"
#include "NsmbGameStateWriter.h"
#include "NsmbAiObservation.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace {

int Failures = 0;

void Check(bool condition, const char *expression, int line) {
  if (condition)
    return;
  std::fprintf(stderr, "line %d: CHECK failed: %s\n", line, expression);
  Failures++;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

bool EndsWith(const std::string &value, const std::string &suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
             0;
}

void TestEveryWireWordRoundTrips() {
  using namespace NsmbNetplayPoC;
  WireProtocol::WireGameState original{};
  auto *words = reinterpret_cast<melonDS::u32 *>(&original);
  for (std::size_t index = 0; index < sizeof(original) / sizeof(*words);
       index++)
    words[index] = 0x10000000u + static_cast<melonDS::u32>(index);
  original.Magic = WireProtocol::kMagic;
  original.Version = WireProtocol::kVersion;
  original.Kind = WireProtocol::kWireKindState;

  GameStateModel::DecodedGameState decoded;
  CHECK(GameStateModel::DecodeWireGameState(original, decoded));
  const WireProtocol::WireGameState encoded =
      GameStateModel::EncodeWireGameState(decoded.Frame, decoded.Instance,
                                          decoded.Sample, decoded.Hashes);
  CHECK(std::memcmp(&original, &encoded, sizeof(original)) == 0);
}

void TestMalformedHeadersAreRejected() {
  using namespace NsmbNetplayPoC;
  const GameStateModel::GameStateSample sample;
  const GameStateModel::GameStateSyncHashes hashes;
  const auto valid = GameStateModel::EncodeWireGameState(7, 2, sample, hashes);
  CHECK(valid.Magic == WireProtocol::kMagic);
  CHECK(valid.Version == WireProtocol::kVersion);
  CHECK(valid.Kind == WireProtocol::kWireKindState);
  CHECK(valid.Frame == 7);
  CHECK(valid.Instance == 2);

  GameStateModel::DecodedGameState decoded;
  auto invalid = valid;
  invalid.Magic = 0;
  CHECK(!GameStateModel::DecodeWireGameState(invalid, decoded));
  invalid = valid;
  invalid.Version++;
  CHECK(!GameStateModel::DecodeWireGameState(invalid, decoded));
  invalid = valid;
  invalid.Kind = WireProtocol::kWireKindWorldState;
  CHECK(!GameStateModel::DecodeWireGameState(invalid, decoded));
}

void TestGameStateHashes() {
  using namespace NsmbNetplayPoC::GameStateModel;
  GameStateSample sample;
  const melonDS::u64 emptyHash = ComputeBasicGameStateHash(sample);
  CHECK(emptyHash == 0xE8381D02137D9773ull);

  sample.StageID = 0x01020304;
  CHECK(ComputeBasicGameStateHash(sample) != emptyHash);
  sample.StageID = 0;
  sample.MovingHazardVelY = 0xA1A2A3A4;
  CHECK(ComputeBasicGameStateHash(sample) != emptyHash);
  sample.MovingHazardVelY = 0;
  sample.Hash = 0xFFFFFFFFFFFFFFFFull;
  CHECK(ComputeBasicGameStateHash(sample) == emptyHash);

  GameStateSyncHashes hashes;
  hashes.Basic = 0x0102030405060708ull;
  hashes.PlayerGlobal = 0x1112131415161718ull;
  hashes.WifiCandidate = 0x2122232425262728ull;
  hashes.RenderCandidate = 0x3132333435363738ull;
  CHECK(CombinedGameStateHash(hashes) == 0x81FED12C6E7300F0ull);
}

void TestWorldActorPredictionContract() {
  using namespace NsmbNetplayPoC;
  WireProtocol::WireWorldActorState state{};
  state.PosX = 0x00001000u;
  state.PosY = 0xFFFFFF00u;
  state.PosZ = 0x7FFFFFF0u;
  state.PrevX = 0x00000800u;
  state.PrevY = 0x00000400u;
  state.PrevZ = 0xFFFFFFF0u;
  state.VelX = 0x00000100u;
  state.VelY = 0xFFFFFF80u;
  state.VelZ = 0x00000020u;

  const GameStateWriter::ObjectTransform predicted =
      GameStateWriter::PredictWorldActorTransform(state, 4);
  CHECK(predicted.PosX == 0x00001400u);
  CHECK(predicted.PosY == 0xFFFFFD00u);
  CHECK(predicted.PosZ == 0x80000070u);
  CHECK(predicted.PrevX == 0x00000C00u);
  CHECK(predicted.PrevY == 0x00000200u);
  CHECK(predicted.PrevZ == 0x00000070u);
  CHECK(predicted.VelX == state.VelX);
  CHECK(predicted.VelY == state.VelY);
  CHECK(predicted.VelZ == state.VelZ);
}

void TestRemoteStateStoreSelectionAndRestart() {
  using namespace NsmbNetplayPoC;
  GameStateModel::RemoteStateStore store;

  GameStateModel::DecodedGameState first;
  first.Instance = 2;
  first.Frame = 10;
  first.Sample.StageID = 10;
  first.Hashes.Basic = 0x10;
  store.StoreGameState(first);
  auto second = first;
  second.Frame = 20;
  second.Sample.StageID = 20;
  second.Hashes.Basic = 0x20;
  store.StoreGameState(second);

  const auto *hashes = store.FindGameStateHashes(2, 20);
  CHECK(hashes != nullptr);
  CHECK(hashes && hashes->Basic == 0x20);
  CHECK(store.FindGameState(2, 15) == nullptr);
  CHECK(store.FindGameState(3, 20) == nullptr);

  GameStateModel::GameStateSample selected;
  melonDS::u32 selectedFrame = 0;
  CHECK(store.FindLatestGameState(2, 15, selected, selectedFrame));
  CHECK(selectedFrame == 10);
  CHECK(selected.StageID == 10);
  CHECK(!store.FindLatestGameState(1, 100, selected, selectedFrame));
  CHECK(selectedFrame == 0);

  WireProtocol::WireWorldState world{};
  world.Frame = 20;
  world.Star.GUID = 20;
  CHECK(store.StoreWorldState(world));
  world.Frame = 19;
  world.Star.GUID = 19;
  CHECK(!store.StoreWorldState(world));
  CHECK(store.WorldState() && store.WorldState()->Frame == 20);
  CHECK(store.WorldState() && store.WorldState()->Star.GUID == 20);
  world.Frame = 20;
  world.Star.GUID = 21;
  CHECK(store.StoreWorldState(world));
  CHECK(store.WorldState() && store.WorldState()->Star.GUID == 21);

  WireProtocol::WireMovingHazardState hazard{};
  hazard.Frame = 30;
  CHECK(store.StoreMovingHazardState(hazard));
  store.ResetForRestart();
  CHECK(store.FindGameStateHashes(2, 20) == nullptr);
  CHECK(store.FindGameState(2, 20) == nullptr);
  CHECK(store.WorldState() == nullptr);
  CHECK(store.MovingHazardState() == nullptr);
}

void TestStateSyncHashComparisonContract() {
  using namespace NsmbNetplayPoC;
  GameStateModel::StateSyncRuntime runtime;
  GameStateModel::GameStateSyncHashes local;
  local.Basic = 1;
  local.PlayerGlobal = 2;
  CHECK(!runtime.RecordLocalGameStateHashes(3, 10, local));

  GameStateModel::DecodedGameState remote;
  remote.Instance = 3;
  remote.Frame = 10;
  remote.Hashes = local;
  remote.Hashes.PlayerGlobal = 3;
  auto mismatch = runtime.RecordRemoteGameState(remote);
  CHECK(mismatch.has_value());
  CHECK(mismatch && mismatch->InstanceID == 3);
  CHECK(mismatch && mismatch->Frame == 10);
  CHECK(mismatch && mismatch->Local.PlayerGlobal == 2);
  CHECK(mismatch && mismatch->Remote.PlayerGlobal == 3);
  CHECK(runtime.RecordRemoteGameState(remote).has_value());

  remote.Frame = 20;
  remote.Hashes = local;
  CHECK(!runtime.RecordRemoteGameState(remote));
  CHECK(!runtime.RecordLocalGameStateHashes(3, 20, local));

  CHECK(!runtime.BeginGameStateSync(-1, 10));
  CHECK(!runtime.BeginGameStateSync(3, 0));
  CHECK(runtime.BeginGameStateSync(3, 10));
  CHECK(!runtime.BeginGameStateSync(3, 10));
  CHECK(runtime.BeginGameStateSync(4, 10));
}

void TestStateSyncRuntimeRestartContract() {
  using namespace NsmbNetplayPoC;
  GameStateModel::StateSyncRuntime runtime;
  GameStateModel::GameStateSyncHashes hashes;
  hashes.Basic = 1;
  CHECK(!runtime.RecordLocalGameStateHashes(3, 10, hashes));
  GameStateModel::DecodedGameState remote;
  remote.Instance = 3;
  remote.Frame = 10;
  remote.Hashes.Basic = 2;
  CHECK(runtime.RecordRemoteGameState(remote).has_value());
  CHECK(runtime.BeginGameStateSync(3, 10));
  runtime.LastSentWorldStateFrame[3] = 12;
  runtime.PlayerActorBaseCache[3][1] = 14;
  runtime.PlayerActorGUIDCache[3][1] = 15;
  runtime.WorldStarActorBaseCache[3] = 16;
  runtime.WorldStarActorGUIDCache[3] = 17;
  runtime.WorldMovingHazardBaseCache[3] = 30;
  runtime.WorldMovingHazardGUIDCache[3] = 31;
  runtime.WorldMovingHazardCacheCounts[3] = 1;
  runtime.WorldMovingHazardBaseCaches[3][0] = 32;
  runtime.WorldMovingHazardGUIDCaches[3][0] = 33;
  runtime.WorldMovingHazardRemoteGUIDMaps[3][0] = 34;
  runtime.WorldMovingHazardLocalGUIDMaps[3][0] = 35;
  runtime.LastTracedWorldMovingHazardsFrame[3] = 38;
  runtime.LastTracedWorldEffectsFrame[3] = 39;
  runtime.LastTracedWorldObjectLifecyclesFrame[3] = 40;
  runtime.PlayerActorBaseCache[4][1] = 41;

  runtime.ResetForRestart(3);

  CHECK(runtime.RemoteState.FindGameState(3, 10) == nullptr);
  CHECK(!runtime.RecordRemoteGameState(remote));
  CHECK(runtime.BeginGameStateSync(3, 10));
  CHECK(runtime.LastSentWorldStateFrame[3] == 0);
  CHECK(runtime.PlayerActorBaseCache[3][1] == 0);
  CHECK(runtime.PlayerActorGUIDCache[3][1] == 0);
  CHECK(runtime.WorldStarActorBaseCache[3] == 0);
  CHECK(runtime.WorldStarActorGUIDCache[3] == 0);
  CHECK(runtime.WorldMovingHazardBaseCache[3] == 0);
  CHECK(runtime.WorldMovingHazardGUIDCache[3] == 0);
  CHECK(runtime.WorldMovingHazardCacheCounts[3] == 0);
  CHECK(runtime.WorldMovingHazardBaseCaches[3][0] == 0);
  CHECK(runtime.WorldMovingHazardGUIDCaches[3][0] == 0);
  CHECK(runtime.WorldMovingHazardRemoteGUIDMaps[3][0] == 0);
  CHECK(runtime.WorldMovingHazardLocalGUIDMaps[3][0] == 0);
  CHECK(runtime.LastTracedWorldMovingHazardsFrame[3] == 38);
  CHECK(runtime.LastTracedWorldEffectsFrame[3] == 39);
  CHECK(runtime.LastTracedWorldObjectLifecyclesFrame[3] == 40);
  CHECK(runtime.PlayerActorBaseCache[4][1] == 41);
}

void TestGameStateTraceRowFormatting() {
  using namespace NsmbNetplayPoC::GameStateModel;
  GameStateSample sample;
  sample.StageID = 0xA1;
  sample.ObjectActiveBase[kObjectTraceSlots - 1] = 0xB2;
  sample.PlayerCount = 0xC3;
  sample.PlayerActor1PowerupGainTimer = 0xD4;

  std::ostringstream basic;
  WriteGameStateTraceRow(basic, 3, 42, sample, nullptr);
  const std::string basicRow = basic.str();
  CHECK(basicRow.rfind("3,42,0xa1", 0) == 0);
  CHECK(EndsWith(basicRow, ",0xb2\n"));
  CHECK(basicRow.find("0xc3") == std::string::npos);
  std::ostringstream basicHeader;
  WriteGameStateTraceHeader(basicHeader, false);
  const std::string basicHeaderText = basicHeader.str();
  CHECK(basicHeaderText.rfind("instance,frame,stageID", 0) == 0);
  CHECK(basicHeaderText.find("playerCount") == std::string::npos);
  CHECK(std::count(basicHeaderText.begin(), basicHeaderText.end(), ',') ==
        std::count(basicRow.begin(), basicRow.end(), ','));

  GameStateTraceHashes hashes;
  hashes.PlayerGlobal = 0x1111111111111111ull;
  hashes.WifiCandidate = 0x2222222222222222ull;
  hashes.RenderCandidate = 0x3333333333333333ull;
  hashes.NetState = 0x4444444444444444ull;
  std::ostringstream extended;
  WriteGameStateTraceRow(extended, 3, 42, sample, &hashes);
  const std::string extendedRow = extended.str();
  CHECK(extendedRow.find(",0xb2,0xc3,") != std::string::npos);
  CHECK(extendedRow.find(",0x1111111111111111,0x2222222222222222,"
                         "0x3333333333333333,0x4444444444444444,") !=
        std::string::npos);
  CHECK(EndsWith(extendedRow, ",0xd4\n"));
  std::ostringstream extendedHeader;
  WriteGameStateTraceHeader(extendedHeader, true);
  const std::string extendedHeaderText = extendedHeader.str();
  CHECK(extendedHeaderText.find(",playerCount,") != std::string::npos);
  CHECK(std::count(extendedHeaderText.begin(), extendedHeaderText.end(), ',') ==
        std::count(extendedRow.begin(), extendedRow.end(), ','));
  extended << 10;
  CHECK(EndsWith(extended.str(), "\n10"));
}

void TestGameStateTraceWriterLifecycle() {
  using namespace NsmbNetplayPoC::GameStateModel;
  const auto unique = std::chrono::high_resolution_clock::now()
                          .time_since_epoch()
                          .count();
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("nsmb_game_state_trace_" + std::to_string(unique) + ".csv");

  GameStateTraceWriter writer;
  CHECK(!writer.IsOpen());
  CHECK(writer.Open(path.string(), false));
  CHECK(writer.IsOpen());

  GameStateSample sample;
  sample.StageID = 0xA1;
  CHECK(!writer.Write(-1, 42, sample, nullptr));
  CHECK(!writer.Write(3, 0, sample, nullptr));
  CHECK(writer.Write(3, 42, sample, nullptr));
  CHECK(!writer.Write(3, 42, sample, nullptr));
  CHECK(writer.Write(4, 42, sample, nullptr));
  writer.ResetForRestart(3);
  CHECK(!writer.Write(3, 0, sample, nullptr));
  CHECK(writer.Write(3, 42, sample, nullptr));
  writer.Close();
  CHECK(!writer.IsOpen());

  std::ifstream input(path);
  const std::string contents((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
  CHECK(contents.rfind("instance,frame,stageID", 0) == 0);
  CHECK(std::count(contents.begin(), contents.end(), '\n') == 4);
  CHECK(contents.find("3,42,0xa1") != std::string::npos);
  CHECK(contents.find("4,42,0xa1") != std::string::npos);
  input.close();
  std::error_code removeError;
  std::filesystem::remove(path, removeError);
  CHECK(!removeError);
}

void TestAIObservationRuntime() {
  using NsmbNetplayPoC::AIObservation::LogKind;
  NsmbNetplayPoC::AIObservation::Runtime runtime;
  CHECK(runtime.AppliedInput(0, 0) == nullptr);

  NsmbNetplayPoC::InputState input;
  input.KeyMask = 0x123;
  input.Touching = true;
  input.TouchX = 45;
  input.TouchY = 67;
  runtime.RecordAppliedInput(0, 42, 1, input);
  const auto *record = runtime.AppliedInput(0, 1);
  CHECK(record != nullptr);
  CHECK(record && record->Frame == 42u);
  CHECK(record && record->Input.KeyMask == 0x123u);
  CHECK(record && record->Input.Touching);
  runtime.RecordAppliedInput(16, 99, 0, input);
  CHECK(runtime.AppliedInput(16, 0) == nullptr);

  int confidence = 0;
  int heuristic = 0;
  bool tracked = false;
  CHECK(runtime.ResolveFireballOwner(0, 3, 0, 55, 1, confidence,
                                     heuristic, tracked) == 0);
  CHECK(tracked);
  CHECK(confidence == 75);
  CHECK(heuristic == 11);

  CHECK(runtime.ResolveFireballOwner(0, 3, 1, 55, 2, confidence,
                                     heuristic, tracked) == 0);
  CHECK(tracked);
  CHECK(runtime.ResolveFireballOwner(0, 3, 1, 80, 3, confidence,
                                     heuristic, tracked) == 1);
  CHECK(confidence == 95);
  CHECK(heuristic == 13);

  runtime.InvalidateFireballOwner(0, 3);
  CHECK(runtime.ResolveFireballOwner(0, 3, -1, 0, 0, confidence,
                                     heuristic, tracked) == -1);
  CHECK(!tracked);
  runtime.ResolveFireballOwner(0, 3, 0, 80, 4, confidence, heuristic,
                               tracked);
  runtime.UpdateFireballHandler(0, 0x12345678);
  CHECK(runtime.ResolveFireballOwner(0, 3, -1, 0, 0, confidence,
                                     heuristic, tracked) == -1);
  CHECK(!tracked);

  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("nsmb-ai-observation-runtime-" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()));
  const std::filesystem::path v1Path = root / "nested" / "v1.jsonl";
  const std::filesystem::path v2Path = root / "v2.jsonl";
  const std::filesystem::path v3Path = root / "v3.jsonl";
  CHECK(runtime.OpenLog(LogKind::V1, v1Path.string()));
  CHECK(runtime.OpenLog(LogKind::V2, v2Path.string()));
  CHECK(runtime.OpenLog(LogKind::V3, v3Path.string()));
  CHECK(runtime.CanWriteLog(LogKind::V1));
  CHECK(runtime.CanWriteLog(LogKind::V2));
  CHECK(runtime.CanWriteLog(LogKind::V3));

  runtime.Log(LogKind::V1) << "v1-a\n";
  runtime.RecordLogLine(LogKind::V1, 2);
  runtime.Log(LogKind::V1) << "v1-b\n";
  runtime.RecordLogLine(LogKind::V1, 2);
  {
    std::ifstream input(v1Path);
    const std::string contents((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    CHECK(contents == "v1-a\nv1-b\n");
  }

  runtime.Log(LogKind::V2) << "v2\n";
  runtime.RecordLogLine(LogKind::V2, 0);
  runtime.Log(LogKind::V3) << "v3\n";
  runtime.RecordLogLine(LogKind::V3, 1);
  runtime.CloseLogs();
  CHECK(!runtime.CanWriteLog(LogKind::V1));
  CHECK(!runtime.CanWriteLog(LogKind::V2));
  CHECK(!runtime.CanWriteLog(LogKind::V3));
  for (const auto &[path, expected] :
       std::array<std::pair<std::filesystem::path, std::string>, 3>{
           std::pair{v1Path, std::string("v1-a\nv1-b\n")},
           std::pair{v2Path, std::string("v2\n")},
           std::pair{v3Path, std::string("v3\n")}}) {
    std::ifstream input(path);
    const std::string contents((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    CHECK(contents == expected);
  }

  CHECK(runtime.OpenLog(LogKind::V1, v1Path.string()));
  runtime.Log(LogKind::V1) << "reopened\n";
  runtime.CloseLogs();
  {
    std::ifstream input(v1Path);
    const std::string contents((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    CHECK(contents == "reopened\n");
  }

  NsmbNetplayPoC::AIObservation::Runtime configuredRuntime;
  NsmbNetplayPoC::Config::DiagnosticsConfig config;
  config.AIPlayLogPath = (root / "configured" / "v1.jsonl").string();
  config.AIObservationV2Path = (root / "configured" / "v2.jsonl").string();
  config.AIObservationV3Path = (root / "configured" / "v3.jsonl").string();
  configuredRuntime.OpenConfiguredLogs(false, config);
  CHECK(!configuredRuntime.CanWriteLog(LogKind::V1));
  CHECK(!configuredRuntime.CanWriteLog(LogKind::V2));
  CHECK(!configuredRuntime.CanWriteLog(LogKind::V3));
  configuredRuntime.OpenConfiguredLogs(true, config);
  CHECK(configuredRuntime.CanWriteLog(LogKind::V1));
  CHECK(configuredRuntime.CanWriteLog(LogKind::V2));
  CHECK(configuredRuntime.CanWriteLog(LogKind::V3));
  configuredRuntime.CloseLogs();
  CHECK(std::filesystem::exists(config.AIPlayLogPath));
  CHECK(std::filesystem::exists(config.AIObservationV2Path));
  CHECK(std::filesystem::exists(config.AIObservationV3Path));

  std::error_code removeError;
  std::filesystem::remove_all(root, removeError);
  CHECK(!removeError);
}

} // namespace

int main() {
  TestEveryWireWordRoundTrips();
  TestMalformedHeadersAreRejected();
  TestGameStateHashes();
  TestWorldActorPredictionContract();
  TestRemoteStateStoreSelectionAndRestart();
  TestStateSyncHashComparisonContract();
  TestStateSyncRuntimeRestartContract();
  TestGameStateTraceRowFormatting();
  TestGameStateTraceWriterLifecycle();
  TestAIObservationRuntime();
  if (Failures != 0) {
    std::fprintf(stderr, "nsmb game state tests failed: %d\n", Failures);
    return 1;
  }
  std::printf("nsmb game state tests passed\n");
  return 0;
}
