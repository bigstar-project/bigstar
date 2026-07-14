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
  TestGameStateTraceRowFormatting();
  if (Failures != 0) {
    std::fprintf(stderr, "nsmb game state tests failed: %d\n", Failures);
    return 1;
  }
  std::printf("nsmb game state tests passed\n");
  return 0;
}
