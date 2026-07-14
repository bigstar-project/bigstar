#include "NsmbNetplayConfig.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

namespace {

int Failures = 0;

class MapEnvironment final : public NsmbNetplayPoC::Config::Environment {
public:
  const char *Get(const char *name) const override {
    const auto it = Values.find(name);
    return it == Values.end() ? nullptr : it->second.c_str();
  }

  std::map<std::string, std::string> Values;
};

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

void TestBootstrapConfigDefaults() {
  const MapEnvironment environment;
  const auto config = NsmbNetplayPoC::Config::LoadBootstrapConfig(environment);
  CHECK(!config.Enabled);
  CHECK(!config.TestEnabled);
  CHECK(config.TestFrames == 0u);
  CHECK(config.TestInstanceCount == 1);
  CHECK(config.HashEnabled);
  CHECK(config.HashInterval == 60);
  CHECK(config.WaitTimeoutMs == 60000);
  CHECK(config.QuitGraceMs == 0);
  CHECK(!config.InputTraceEnabled);
  CHECK(config.InputTraceInterval == 60);
}

void TestBootstrapConfigReadsAndClampsEnvironment() {
  MapEnvironment environment;
  environment.Values = {
      {"MELONDS_NSML_POC", "1"},
      {"MELONDS_NSML_TEST", "1"},
      {"MELONDS_NSML_TEST_FRAMES", "-20"},
      {"MELONDS_NSML_TEST_INSTANCES", "99"},
      {"MELONDS_NSML_DISABLE_HASH", "1"},
      {"MELONDS_NSML_HASH_INTERVAL", "0"},
      {"MELONDS_NSML_WAIT_TIMEOUT_MS", "-1"},
      {"MELONDS_NSML_QUIT_GRACE_MS", "250"},
      {"MELONDS_NSML_INPUT_TRACE", "trace"},
      {"MELONDS_NSML_INPUT_TRACE_INTERVAL", "0"},
  };

  const auto config = NsmbNetplayPoC::Config::LoadBootstrapConfig(environment);
  CHECK(config.Enabled);
  CHECK(config.TestEnabled);
  CHECK(config.TestFrames == 0u);
  CHECK(config.TestInstanceCount == 16);
  CHECK(!config.HashEnabled);
  CHECK(config.HashInterval == 1);
  CHECK(config.WaitTimeoutMs == 0);
  CHECK(config.QuitGraceMs == 250);
  CHECK(config.InputTraceEnabled);
  CHECK(config.InputTraceInterval == 1);
}

void TestConnectionConfigDefaultsAndRoleFallback() {
  MapEnvironment environment;
  auto config =
      NsmbNetplayPoC::Config::LoadConnectionConfig(environment, false);
  CHECK(!config.Client);
  CHECK(config.Delay == 6);
  CHECK(config.WarmupFrames == 0);
  CHECK(config.Port == 8065);
  CHECK(config.LocalInstance == 0);
  CHECK(config.StartFrame == 0u);
  CHECK(config.LocalWaitsForRemote);
  CHECK(!config.RemoteInputTimeoutFatal);
  CHECK(config.PeerHost == "127.0.0.1");

  config = NsmbNetplayPoC::Config::LoadConnectionConfig(environment, true);
  CHECK(config.WarmupFrames == 12);

  environment.Values["MELONDS_NSML_LAN_ROLE"] = "client";
  config = NsmbNetplayPoC::Config::LoadConnectionConfig(environment, false);
  CHECK(config.Client);
  CHECK(config.LocalInstance == 1);

  environment.Values["MELONDS_NSML_ROLE"] = "host";
  config = NsmbNetplayPoC::Config::LoadConnectionConfig(environment, false);
  CHECK(!config.Client);
  CHECK(config.LocalInstance == 0);
}

void TestConnectionConfigReadsExistingValuesAndClamps() {
  MapEnvironment environment;
  environment.Values = {
      {"MELONDS_NSML_ROLE", "client"},
      {"MELONDS_NSML_DELAY", "-3"},
      {"MELONDS_NSML_NETPLAY_WARMUP_FRAMES", "-2"},
      {"MELONDS_NSML_PORT", "9000"},
      {"MELONDS_NSML_LOCAL_INSTANCE", "7"},
      {"MELONDS_NSML_NETPLAY_START_FRAME", "-5"},
      {"MELONDS_NSML_NO_LOCAL_WAIT", "1"},
      {"MELONDS_NSML_REMOTE_INPUT_TIMEOUT_FATAL", "1"},
      {"MELONDS_NSML_PEER", "192.0.2.10"},
  };

  const auto config =
      NsmbNetplayPoC::Config::LoadConnectionConfig(environment, true);
  CHECK(config.Client);
  CHECK(config.Delay == 0);
  CHECK(config.WarmupFrames == 0);
  CHECK(config.Port == 9000);
  CHECK(config.LocalInstance == 7);
  CHECK(config.StartFrame == 0u);
  CHECK(!config.LocalWaitsForRemote);
  CHECK(config.RemoteInputTimeoutFatal);
  CHECK(config.PeerHost == "192.0.2.10");
}

void TestInputConfigDefaultsPreserveLegacyInitializationOrder() {
  MapEnvironment environment;
  auto config = NsmbNetplayPoC::Config::LoadInputConfig(environment, false);
  CHECK(config.SendDelayFrames == 0);
  CHECK(config.SendJitterFrames == 0);
  CHECK(config.SendDelayStartFrame == 0u);
  CHECK(config.SendDelayEndFrame == 0u);
  CHECK(!config.UseHistoryBundle);
  CHECK(config.BundleHistory == 0);
  CHECK(config.DropModulo == 0);
  CHECK(config.DropOffset == 0);
  CHECK(config.DropStartFrame == 0u);
  CHECK(config.DropEndFrame == 0u);
  CHECK(config.MaxFrameLead == -1);
  CHECK(!config.NetplayOnly);
  CHECK(!config.NetplayTrace);
  CHECK(!config.HealthTrace);
  CHECK(config.HealthTraceInterval == 120);
  CHECK(config.HealthTraceWaitThresholdMs == 16);
  CHECK(config.WaitPollUs == 100);

  environment.Values["MELONDS_NSML_INPUT_NETPLAY_ONLY"] = "1";
  config = NsmbNetplayPoC::Config::LoadInputConfig(environment, false);
  CHECK(config.NetplayOnly);
  CHECK(config.MaxFrameLead == -1);
  config = NsmbNetplayPoC::Config::LoadInputConfig(environment, true);
  CHECK(config.MaxFrameLead == 2);
}

void TestInputConfigReadsClampsAndNormalizesRanges() {
  MapEnvironment environment;
  environment.Values = {
      {"MELONDS_NSML_INPUT_SEND_DELAY_FRAMES", "-2"},
      {"MELONDS_NSML_INPUT_SEND_JITTER_FRAMES", "4"},
      {"MELONDS_NSML_INPUT_SEND_DELAY_START_FRAME", "100"},
      {"MELONDS_NSML_INPUT_SEND_DELAY_END_FRAME", "90"},
      {"MELONDS_NSML_INPUT_UNRELIABLE", "1"},
      {"MELONDS_NSML_INPUT_BUNDLE_HISTORY", "99"},
      {"MELONDS_NSML_INPUT_DROP_MODULO", "11"},
      {"MELONDS_NSML_INPUT_DROP_OFFSET", "25"},
      {"MELONDS_NSML_INPUT_DROP_START_FRAME", "200"},
      {"MELONDS_NSML_INPUT_DROP_END_FRAME", "150"},
      {"MELONDS_NSML_INPUT_MAX_FRAME_LEAD", "7"},
      {"MELONDS_NSML_INPUT_NETPLAY_TRACE", "1"},
      {"MELONDS_NSML_INPUT_HEALTH_TRACE", "1"},
      {"MELONDS_NSML_INPUT_HEALTH_TRACE_INTERVAL", "0"},
      {"MELONDS_NSML_INPUT_HEALTH_TRACE_WAIT_THRESHOLD_MS", "9000"},
      {"MELONDS_NSML_INPUT_WAIT_POLL_US", "1"},
  };

  const auto config =
      NsmbNetplayPoC::Config::LoadInputConfig(environment, false);
  CHECK(config.SendDelayFrames == 0);
  CHECK(config.SendJitterFrames == 4);
  CHECK(config.SendDelayStartFrame == 100u);
  CHECK(config.SendDelayEndFrame == 100u);
  CHECK(config.UseHistoryBundle);
  CHECK(config.BundleHistory == 31);
  CHECK(config.DropModulo == 11);
  CHECK(config.DropOffset == 3);
  CHECK(config.DropStartFrame == 200u);
  CHECK(config.DropEndFrame == 200u);
  CHECK(config.MaxFrameLead == 7);
  CHECK(config.NetplayTrace);
  CHECK(config.HealthTrace);
  CHECK(config.HealthTraceInterval == 1);
  CHECK(config.HealthTraceWaitThresholdMs == 5000);
  CHECK(config.WaitPollUs == 50);
}

} // namespace

int main() {
  TestFlagsPreserveExistingSemantics();
  TestStringsPreserveFallbackSemantics();
  TestIntegerParsingPreservesBaseAndFallback();
  TestDoubleParsingPreservesFallback();
  TestUnsignedParsingPreservesExistingInvalidValueBehavior();
  TestBootstrapConfigDefaults();
  TestBootstrapConfigReadsAndClampsEnvironment();
  TestConnectionConfigDefaultsAndRoleFallback();
  TestConnectionConfigReadsExistingValuesAndClamps();
  TestInputConfigDefaultsPreserveLegacyInitializationOrder();
  TestInputConfigReadsClampsAndNormalizesRanges();

  if (Failures != 0) {
    std::fprintf(stderr, "nsmb netplay config tests failed: %d\n", Failures);
    return 1;
  }

  std::printf("nsmb netplay config tests passed\n");
  return 0;
}
