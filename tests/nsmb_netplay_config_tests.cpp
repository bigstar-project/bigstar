#include "NsmbNetplayConfig.h"

#include <cmath>
#include <cstdint>
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

void TestFlagsPreserveExistingSemantics() {
  using NsmbNetplayPoC::Config::ParseFlag;
  CHECK(!ParseFlag(nullptr));
  CHECK(!ParseFlag(""));
  CHECK(!ParseFlag("0"));
  CHECK(ParseFlag("1"));
  CHECK(ParseFlag("false"));
  CHECK(ParseFlag("00"));
}

void TestStringsPreserveFallbackSemantics() {
  using NsmbNetplayPoC::Config::HasValue;
  using NsmbNetplayPoC::Config::ValueOr;
  CHECK(std::strcmp(ValueOr(nullptr, "fallback"), "fallback") == 0);
  CHECK(std::strcmp(ValueOr("", "fallback"), "fallback") == 0);
  CHECK(std::strcmp(ValueOr("value", "fallback"), "value") == 0);
  CHECK(!HasValue(nullptr));
  CHECK(!HasValue(""));
  CHECK(HasValue("0"));
}

void TestIntegerParsingPreservesBaseAndFallback() {
  using NsmbNetplayPoC::Config::ParseInt;
  CHECK(ParseInt(nullptr, 17) == 17);
  CHECK(ParseInt("", 17) == 17);
  CHECK(ParseInt("invalid", 17) == 17);
  CHECK(ParseInt("42", 0) == 42);
  CHECK(ParseInt("-9", 0) == -9);
  CHECK(ParseInt("0x2A", 0) == 42);
  CHECK(ParseInt("052", 0) == 42);
  CHECK(ParseInt("12trailing", 0) == 12);
}

void TestDoubleParsingPreservesFallback() {
  using NsmbNetplayPoC::Config::ParseDouble;
  CHECK(ParseDouble(nullptr, 1.5) == 1.5);
  CHECK(ParseDouble("", 1.5) == 1.5);
  CHECK(ParseDouble("invalid", 1.5) == 1.5);
  CHECK(std::abs(ParseDouble("2.25", 0.0) - 2.25) < 0.000001);
  CHECK(std::abs(ParseDouble("3.5ms", 0.0) - 3.5) < 0.000001);
}

void TestUnsignedParsingPreservesExistingInvalidValueBehavior() {
  using NsmbNetplayPoC::Config::ParseU32;
  CHECK(ParseU32(nullptr, 99) == 99u);
  CHECK(ParseU32("", 99) == 99u);
  CHECK(ParseU32("0xFFFFFFFF", 0) == UINT32_MAX);
  CHECK(ParseU32("invalid", 99) == 0u);
}

} // namespace

int main() {
  TestFlagsPreserveExistingSemantics();
  TestStringsPreserveFallbackSemantics();
  TestIntegerParsingPreservesBaseAndFallback();
  TestDoubleParsingPreservesFallback();
  TestUnsignedParsingPreservesExistingInvalidValueBehavior();

  if (Failures != 0) {
    std::fprintf(stderr, "nsmb netplay config tests failed: %d\n", Failures);
    return 1;
  }

  std::printf("nsmb netplay config tests passed\n");
  return 0;
}
