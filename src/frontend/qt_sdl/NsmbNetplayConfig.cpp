#include "NsmbNetplayConfig.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace NsmbNetplayPoC::Config {

namespace {

class ProcessEnvironment final : public Environment {
public:
  const char *Get(const char *name) const override { return std::getenv(name); }
};

} // namespace

const Environment &GetProcessEnvironment() {
  static const ProcessEnvironment environment;
  return environment;
}

bool ParseFlag(const char *value) {
  return value && value[0] && std::strcmp(value, "0") != 0;
}

const char *ValueOr(const char *value, const char *fallback) {
  return value && value[0] ? value : fallback;
}

int ParseInt(const char *value, int fallback) {
  if (!value || !value[0])
    return fallback;

  char *end = nullptr;
  const long parsed = std::strtol(value, &end, 0);
  if (end == value)
    return fallback;
  return static_cast<int>(parsed);
}

double ParseDouble(const char *value, double fallback) {
  if (!value || !value[0])
    return fallback;

  char *end = nullptr;
  const double parsed = std::strtod(value, &end);
  if (end == value)
    return fallback;
  return parsed;
}

std::uint32_t ParseU32(const char *value, std::uint32_t fallback) {
  if (!value || !value[0])
    return fallback;
  return static_cast<std::uint32_t>(std::strtoul(value, nullptr, 0));
}

bool HasValue(const char *value) { return value && value[0]; }

bool ReadFlag(const Environment &environment, const char *name) {
  return ParseFlag(environment.Get(name));
}

const char *ReadCString(const Environment &environment, const char *name,
                        const char *fallback) {
  return ValueOr(environment.Get(name), fallback);
}

int ReadInt(const Environment &environment, const char *name, int fallback) {
  return ParseInt(environment.Get(name), fallback);
}

double ReadDouble(const Environment &environment, const char *name,
                  double fallback) {
  return ParseDouble(environment.Get(name), fallback);
}

std::uint32_t ReadU32(const Environment &environment, const char *name,
                      std::uint32_t fallback) {
  return ParseU32(environment.Get(name), fallback);
}

bool ReadHasValue(const Environment &environment, const char *name) {
  return HasValue(environment.Get(name));
}

BootstrapConfig LoadBootstrapConfig(const Environment &environment) {
  BootstrapConfig config;
  config.Enabled = ReadFlag(environment, "MELONDS_NSML_POC");
  config.TestEnabled = ReadFlag(environment, "MELONDS_NSML_TEST");
  config.TestFrames = static_cast<std::uint32_t>(
      std::max(0, ReadInt(environment, "MELONDS_NSML_TEST_FRAMES", 0)));
  config.TestInstanceCount =
      std::clamp(ReadInt(environment, "MELONDS_NSML_TEST_INSTANCES", 1), 1, 16);
  config.HashEnabled = !ReadFlag(environment, "MELONDS_NSML_DISABLE_HASH");
  config.HashInterval =
      std::max(1, ReadInt(environment, "MELONDS_NSML_HASH_INTERVAL", 60));
  config.WaitTimeoutMs =
      std::max(0, ReadInt(environment, "MELONDS_NSML_WAIT_TIMEOUT_MS", 60000));
  config.QuitGraceMs =
      std::max(0, ReadInt(environment, "MELONDS_NSML_QUIT_GRACE_MS", 0));
  config.InputTraceEnabled = ReadFlag(environment, "MELONDS_NSML_INPUT_TRACE");
  config.InputTraceInterval = std::max(
      1, ReadInt(environment, "MELONDS_NSML_INPUT_TRACE_INTERVAL", 60));
  return config;
}

BootstrapConfig LoadBootstrapConfig() {
  return LoadBootstrapConfig(GetProcessEnvironment());
}

bool EnvFlag(const char *name) {
  return ReadFlag(GetProcessEnvironment(), name);
}

const char *EnvCString(const char *name, const char *fallback) {
  return ReadCString(GetProcessEnvironment(), name, fallback);
}

int EnvInt(const char *name, int fallback) {
  return ReadInt(GetProcessEnvironment(), name, fallback);
}

double EnvDouble(const char *name, double fallback) {
  return ReadDouble(GetProcessEnvironment(), name, fallback);
}

std::uint32_t EnvU32(const char *name, std::uint32_t fallback) {
  return ReadU32(GetProcessEnvironment(), name, fallback);
}

bool EnvHasValue(const char *name) {
  return ReadHasValue(GetProcessEnvironment(), name);
}

} // namespace NsmbNetplayPoC::Config
