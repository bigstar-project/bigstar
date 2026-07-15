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
  const auto payload = Encode({MessageKind::MatchSeed, 0x11223344});
  const std::vector<std::uint8_t> expected{
      0x4E, 0x53, 0x4D, 0x4C, 0x01, 0x00, 0x00, 0x00,
      0x53, 0x45, 0x45, 0x44, 0x44, 0x33, 0x22, 0x11,
  };
  CHECK(Bytes(payload) == expected);

  Message decoded{MessageKind::StartReady, 0};
  CHECK(Decode(payload.data(), payload.size(), decoded));
  CHECK(decoded.Kind == MessageKind::MatchSeed);
  CHECK(decoded.Value == 0x11223344);
}

void TestStartReadyGoldenBytesAndRoundTrip() {
  using namespace NsmbMvlNetplay::SessionProtocol;
  const auto payload = Encode({MessageKind::StartReady, 0xA1B2C3D4});
  const std::vector<std::uint8_t> expected{
      0x4E, 0x53, 0x4D, 0x4C, 0x01, 0x00, 0x00, 0x00,
      0x53, 0x54, 0x52, 0x54, 0xD4, 0xC3, 0xB2, 0xA1,
  };
  CHECK(Bytes(payload) == expected);

  Message decoded;
  CHECK(Decode(payload.data(), payload.size(), decoded));
  CHECK(decoded.Kind == MessageKind::StartReady);
  CHECK(decoded.Value == 0xA1B2C3D4);
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
  badVersion[4] = 2;
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
