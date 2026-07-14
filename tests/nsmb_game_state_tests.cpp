#include "NsmbGameState.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace {

int Failures = 0;

void Check(bool condition, const char *expression, int line) {
  if (condition)
    return;
  std::fprintf(stderr, "line %d: CHECK failed: %s\n", line, expression);
  Failures++;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

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

} // namespace

int main() {
  TestEveryWireWordRoundTrips();
  TestMalformedHeadersAreRejected();
  TestGameStateHashes();
  if (Failures != 0) {
    std::fprintf(stderr, "nsmb game state tests failed: %d\n", Failures);
    return 1;
  }
  std::printf("nsmb game state tests passed\n");
  return 0;
}
