#include "NsmbNetplayProtocol.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int Failures = 0;

void Check(bool condition, const char *expression, int line) {
  if (condition)
    return;
  std::fprintf(stderr, "line %d: CHECK failed: %s\n", line, expression);
  Failures++;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

std::vector<std::uint8_t> Bytes(const std::vector<char> &payload) {
  return {payload.begin(), payload.end()};
}

void TestMatchSeedGoldenBytesAndRoundTrip() {
  using namespace NsmbMvlNetplay::SessionProtocol;
  Message message;
  message.Kind = MessageKind::MatchSeed;
  message.Value = 0x11223344;
  const auto payload = Encode(message);
  const std::vector<std::uint8_t> expected{
      0x4E, 0x53, 0x4D, 0x4C, 0x02, 0x00, 0x00, 0x00,
      0x53, 0x45, 0x45, 0x44, 0x44, 0x33, 0x22, 0x11,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };
  CHECK(Bytes(payload) == expected);

  Message decoded{MessageKind::StartReady, 0};
  CHECK(Decode(payload.data(), payload.size(), decoded));
  CHECK(decoded.Kind == MessageKind::MatchSeed);
  CHECK(decoded.Value == 0x11223344);
}

void TestStartReadyGoldenBytesAndRoundTrip() {
  using namespace NsmbMvlNetplay::SessionProtocol;
  Message message;
  message.Kind = MessageKind::StartReady;
  message.Value = 0xA1B2C3D4;
  message.Generation = 3;
  message.RawReadyFrame = 0x1234;
  message.SharedLogicalEpoch = 0x5678;
  message.StageID = 5;
  message.StageGroup = 2;
  message.MatchSeed = 0x10203040;
  message.PacketTick = 0x50607080;
  message.RngValue = 0x90A0B0C0;
  message.RngCallCount = 9;
  message.RngBranchAddress = 0x0202F814;
  message.SemanticHash = 0x1122334455667788ULL;
  const auto payload = Encode(message);
  const std::vector<std::uint8_t> expected{
      0x4E, 0x53, 0x4D, 0x4C, 0x02, 0x00, 0x00, 0x00,
      0x53, 0x54, 0x52, 0x54, 0xD4, 0xC3, 0xB2, 0xA1,
      0x03, 0x00, 0x00, 0x00, 0x34, 0x12, 0x00, 0x00,
      0x78, 0x56, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
      0x02, 0x00, 0x00, 0x00, 0x40, 0x30, 0x20, 0x10,
      0x80, 0x70, 0x60, 0x50, 0xC0, 0xB0, 0xA0, 0x90,
      0x09, 0x00, 0x00, 0x00, 0x14, 0xF8, 0x02, 0x02,
      0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
  };
  CHECK(Bytes(payload) == expected);

  Message decoded;
  CHECK(Decode(payload.data(), payload.size(), decoded));
  CHECK(decoded.Kind == MessageKind::StartReady);
  CHECK(decoded.Value == 0xA1B2C3D4);
  CHECK(decoded.Generation == message.Generation);
  CHECK(decoded.RawReadyFrame == message.RawReadyFrame);
  CHECK(decoded.SharedLogicalEpoch == message.SharedLogicalEpoch);
  CHECK(decoded.StageID == message.StageID);
  CHECK(decoded.StageGroup == message.StageGroup);
  CHECK(decoded.MatchSeed == message.MatchSeed);
  CHECK(decoded.PacketTick == message.PacketTick);
  CHECK(decoded.RngValue == message.RngValue);
  CHECK(decoded.RngCallCount == message.RngCallCount);
  CHECK(decoded.RngBranchAddress == message.RngBranchAddress);
  CHECK(decoded.SemanticHash == message.SemanticHash);
}

void TestMalformedPacketsAreRejected() {
  using namespace NsmbMvlNetplay::SessionProtocol;
  const auto valid = Encode({MessageKind::MatchSeed, 7});
  Message decoded{MessageKind::StartReady, 99};
  CHECK(!Decode(nullptr, valid.size(), decoded));
  CHECK(!Decode(valid.data(), valid.size() - 1, decoded));

  auto badMagic = valid;
  badMagic[0] = 0;
  CHECK(!Decode(badMagic.data(), badMagic.size(), decoded));

  auto badVersion = valid;
  badVersion[4] = 1;
  CHECK(!Decode(badVersion.data(), badVersion.size(), decoded));

  auto badKind = valid;
  badKind[8] = 0;
  CHECK(!Decode(badKind.data(), badKind.size(), decoded));
  CHECK(decoded.Kind == MessageKind::StartReady);
  CHECK(decoded.Value == 99);
}

} // namespace

int main() {
  TestMatchSeedGoldenBytesAndRoundTrip();
  TestStartReadyGoldenBytesAndRoundTrip();
  TestMalformedPacketsAreRejected();

  if (Failures != 0) {
    std::fprintf(stderr, "nsmb session protocol tests failed: %d\n", Failures);
    return 1;
  }
  std::printf("nsmb session protocol tests passed\n");
  return 0;
}
