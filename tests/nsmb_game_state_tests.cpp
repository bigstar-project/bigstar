#include "NsmbGameState.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

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
  invalid.Kind = WireProtocol::kWireKindPlayerState;
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

  for (melonDS::u32 frame = 0;
       frame <= GameStateModel::RemoteStateStore::PlayerHistoryLimit; frame++) {
    WireProtocol::WirePlayerState player{};
    player.Player = 0;
    player.Frame = frame;
    player.PosX = frame;
    store.StorePlayerState(player);
  }
  CHECK(store.PlayerStateCount() ==
        GameStateModel::RemoteStateStore::PlayerHistoryLimit);
  WireProtocol::WirePlayerState player;
  CHECK(!store.FindLatestPlayerState(0, 0, player, selectedFrame));
  CHECK(store.FindLatestPlayerState(0, 150, player, selectedFrame));
  CHECK(selectedFrame == 150);
  CHECK(player.PosX == 150);
  CHECK(!store.FindLatestPlayerState(1, 240, player, selectedFrame));

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
  WireProtocol::WireWorldActorSnapshotState actors{};
  actors.Frame = 40;
  CHECK(store.StoreWorldActorSnapshot(actors));
  WireProtocol::WireWorldEffectState effects{};
  effects.Frame = 50;
  CHECK(store.StoreWorldEffectState(effects));

  store.ResetForRestart();
  CHECK(store.FindGameStateHashes(2, 20) == nullptr);
  CHECK(store.FindGameState(2, 20) == nullptr);
  CHECK(store.PlayerStateCount() == 0);
  CHECK(store.WorldState() == nullptr);
  CHECK(store.MovingHazardState() == nullptr);
  CHECK(store.WorldActorSnapshot() == nullptr);
  CHECK(store.WorldEffectState() == nullptr);
}

void TestStateSyncRuntimeRestartContract() {
  using namespace NsmbNetplayPoC;
  GameStateModel::StateSyncRuntime runtime;
  GameStateModel::GameStateSyncHashes hashes;
  hashes.Basic = 1;
  runtime.LocalGameStateHashes.emplace(GameStateModel::GameStateKey(3, 10),
                                       hashes);
  GameStateModel::DecodedGameState remote;
  remote.Instance = 3;
  remote.Frame = 10;
  runtime.RemoteState.StoreGameState(remote);
  runtime.GameStateMismatchSeen = true;
  runtime.LastSentGameStateFrame[3] = 10;
  runtime.LastSentPlayerStateFrame[3] = 11;
  runtime.LastSentWorldStateFrame[3] = 12;
  runtime.LastAppliedPlayerGlobalsFrame[3][1] = 13;
  runtime.PlayerActorBaseCache[3][1] = 14;
  runtime.PlayerActorGUIDCache[3][1] = 15;
  runtime.WorldStarActorBaseCache[3] = 16;
  runtime.WorldStarActorGUIDCache[3] = 17;
  runtime.LastSpawnedWorldItemRemoteGUID[3] = 18;
  runtime.LastConfirmedWorldItemRemoteGUID[3] = 19;
  runtime.PendingWorldItemRemoteGUID[3] = 20;
  runtime.PendingWorldItemFirstMissingFrame[3] = 21;
  runtime.LastSpawnedNeutralWorldItemRemoteGUID[3] = 22;
  runtime.LastConfirmedNeutralWorldItemRemoteGUID[3] = 23;
  runtime.PendingNeutralWorldItemRemoteGUID[3] = 24;
  runtime.PendingNeutralWorldItemFirstMissingFrame[3] = 25;
  runtime.LastSpawnedDroppedStarItemRemoteGUID[3] = 26;
  runtime.LastConfirmedDroppedStarItemRemoteGUID[3] = 27;
  runtime.PendingDroppedStarItemRemoteGUID[3] = 28;
  runtime.PendingDroppedStarItemFirstMissingFrame[3] = 29;
  runtime.WorldMovingHazardBaseCache[3] = 30;
  runtime.WorldMovingHazardGUIDCache[3] = 31;
  runtime.WorldMovingHazardCacheCounts[3] = 1;
  runtime.WorldMovingHazardBaseCaches[3][0] = 32;
  runtime.WorldMovingHazardGUIDCaches[3][0] = 33;
  runtime.WorldMovingHazardRemoteGUIDMaps[3][0] = 34;
  runtime.WorldMovingHazardLocalGUIDMaps[3][0] = 35;
  runtime.WorldActorSnapshotRemoteGUIDMaps[3][0] = 36;
  runtime.WorldActorSnapshotLocalGUIDMaps[3][0] = 37;
  runtime.LastTracedWorldMovingHazardsFrame[3] = 38;
  runtime.LastTracedWorldEffectsFrame[3] = 39;
  runtime.LastTracedWorldObjectLifecyclesFrame[3] = 40;
  runtime.PlayerActorBaseCache[4][1] = 41;

  runtime.ResetForRestart(3);

  CHECK(runtime.LocalGameStateHashes.empty());
  CHECK(runtime.RemoteState.FindGameState(3, 10) == nullptr);
  CHECK(runtime.GameStateMismatchSeen);
  CHECK(runtime.LastSentGameStateFrame[3] == 0);
  CHECK(runtime.LastSentPlayerStateFrame[3] == 0);
  CHECK(runtime.LastSentWorldStateFrame[3] == 0);
  CHECK(runtime.LastAppliedPlayerGlobalsFrame[3][1] == 0);
  CHECK(runtime.PlayerActorBaseCache[3][1] == 0);
  CHECK(runtime.PlayerActorGUIDCache[3][1] == 0);
  CHECK(runtime.WorldStarActorBaseCache[3] == 0);
  CHECK(runtime.WorldStarActorGUIDCache[3] == 0);
  CHECK(runtime.LastSpawnedWorldItemRemoteGUID[3] == 0);
  CHECK(runtime.LastConfirmedWorldItemRemoteGUID[3] == 0);
  CHECK(runtime.PendingWorldItemRemoteGUID[3] == 0);
  CHECK(runtime.PendingWorldItemFirstMissingFrame[3] == 0);
  CHECK(runtime.LastSpawnedNeutralWorldItemRemoteGUID[3] == 0);
  CHECK(runtime.LastConfirmedNeutralWorldItemRemoteGUID[3] == 0);
  CHECK(runtime.PendingNeutralWorldItemRemoteGUID[3] == 0);
  CHECK(runtime.PendingNeutralWorldItemFirstMissingFrame[3] == 0);
  CHECK(runtime.LastSpawnedDroppedStarItemRemoteGUID[3] == 0);
  CHECK(runtime.LastConfirmedDroppedStarItemRemoteGUID[3] == 0);
  CHECK(runtime.PendingDroppedStarItemRemoteGUID[3] == 0);
  CHECK(runtime.PendingDroppedStarItemFirstMissingFrame[3] == 0);
  CHECK(runtime.WorldMovingHazardBaseCache[3] == 0);
  CHECK(runtime.WorldMovingHazardGUIDCache[3] == 0);
  CHECK(runtime.WorldMovingHazardCacheCounts[3] == 0);
  CHECK(runtime.WorldMovingHazardBaseCaches[3][0] == 0);
  CHECK(runtime.WorldMovingHazardGUIDCaches[3][0] == 0);
  CHECK(runtime.WorldMovingHazardRemoteGUIDMaps[3][0] == 0);
  CHECK(runtime.WorldMovingHazardLocalGUIDMaps[3][0] == 0);
  CHECK(runtime.WorldActorSnapshotRemoteGUIDMaps[3][0] == 0);
  CHECK(runtime.WorldActorSnapshotLocalGUIDMaps[3][0] == 0);
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

} // namespace

int main() {
  TestEveryWireWordRoundTrips();
  TestMalformedHeadersAreRejected();
  TestGameStateHashes();
  TestRemoteStateStoreSelectionAndRestart();
  TestStateSyncRuntimeRestartContract();
  TestGameStateTraceRowFormatting();
  if (Failures != 0) {
    std::fprintf(stderr, "nsmb game state tests failed: %d\n", Failures);
    return 1;
  }
  std::printf("nsmb game state tests passed\n");
  return 0;
}
