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

NsmbNetplayPoC::PacketClassifier::KnownPacketSizes Sizes() {
  return {24, 16, 80, 176};
}

void TestEveryKnownClass() {
  using namespace NsmbNetplayPoC::PacketClassifier;
  const KnownPacketSizes sizes = Sizes();
  CHECK(Classify(24, sizes) == PacketClass::Input);
  CHECK(Classify(16, sizes) == PacketClass::Session);
  CHECK(Classify(80, sizes) == PacketClass::NSMLPacket);
  CHECK(Classify(176, sizes) == PacketClass::GameState);
}

void TestBundleCandidateAndUnknownSizes() {
  using namespace NsmbNetplayPoC::PacketClassifier;
  const KnownPacketSizes sizes = Sizes();
  CHECK(Classify(17, sizes) == PacketClass::InputBundleCandidate);
  CHECK(Classify(64, sizes) == PacketClass::InputBundleCandidate);
  CHECK(Classify(96, sizes) == PacketClass::InputBundleCandidate);
  CHECK(Classify(112, sizes) == PacketClass::InputBundleCandidate);
  CHECK(Classify(128, sizes) == PacketClass::InputBundleCandidate);
  CHECK(Classify(144, sizes) == PacketClass::InputBundleCandidate);
  CHECK(Classify(160, sizes) == PacketClass::InputBundleCandidate);
  CHECK(Classify(0, sizes) == PacketClass::Unknown);
  CHECK(Classify(15, sizes) == PacketClass::Unknown);
}

void TestHistoricalPrecedenceIsPreserved() {
  using namespace NsmbNetplayPoC::PacketClassifier;
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
