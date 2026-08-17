#include "NsmbNetplayProtocol.h"

#include <cstdio>

namespace {

int Failures = 0;

void Check(bool condition, const char *expression, int line) {
  if (condition)
    return;
  std::fprintf(stderr, "line %d: CHECK failed: %s\n", line, expression);
  Failures++;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

NsmbMvlNetplay::PacketClassifier::KnownPacketSizes Sizes() {
  return {28, 64, 80, 384};
}

void TestEveryKnownClass() {
  using namespace NsmbMvlNetplay::PacketClassifier;
  const KnownPacketSizes sizes = Sizes();
  CHECK(Classify(28, sizes) == PacketClass::Input);
  CHECK(Classify(64, sizes) == PacketClass::Session);
  CHECK(Classify(80, sizes) == PacketClass::NSMLPacket);
  CHECK(Classify(384, sizes) == PacketClass::GameState);
}

void TestBundleCandidateAndUnknownSizes() {
  using namespace NsmbMvlNetplay::PacketClassifier;
  const KnownPacketSizes sizes = Sizes();
  CHECK(Classify(29, sizes) == PacketClass::InputBundleCandidate);
  CHECK(Classify(36, sizes) == PacketClass::InputBundleCandidate);
  CHECK(Classify(96, sizes) == PacketClass::InputBundleCandidate);
  CHECK(Classify(112, sizes) == PacketClass::InputBundleCandidate);
  CHECK(Classify(128, sizes) == PacketClass::InputBundleCandidate);
  CHECK(Classify(144, sizes) == PacketClass::InputBundleCandidate);
  CHECK(Classify(160, sizes) == PacketClass::InputBundleCandidate);
  CHECK(Classify(0, sizes) == PacketClass::Unknown);
  CHECK(Classify(27, sizes) == PacketClass::Unknown);
}

void TestHistoricalPrecedenceIsPreserved() {
  using namespace NsmbMvlNetplay::PacketClassifier;
  KnownPacketSizes sizes = Sizes();
  sizes.NSMLPacket = sizes.Input;
  CHECK(Classify(sizes.Input, sizes) == PacketClass::Input);
}

} // namespace

int main() {
  TestEveryKnownClass();
  TestBundleCandidateAndUnknownSizes();
  TestHistoricalPrecedenceIsPreserved();

  if (Failures != 0) {
    std::fprintf(stderr, "nsmb packet classifier tests failed: %d\n", Failures);
    return 1;
  }
  std::printf("nsmb packet classifier tests passed\n");
  return 0;
}
