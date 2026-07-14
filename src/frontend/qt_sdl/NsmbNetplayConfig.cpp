#include "NsmbNetplayConfig.h"

#include <cstdlib>
#include <cstring>

namespace NsmbNetplayPoC::Config {

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

bool EnvFlag(const char *name) { return ParseFlag(std::getenv(name)); }

const char *EnvCString(const char *name, const char *fallback) {
  return ValueOr(std::getenv(name), fallback);
}

int EnvInt(const char *name, int fallback) {
  return ParseInt(std::getenv(name), fallback);
}

double EnvDouble(const char *name, double fallback) {
  return ParseDouble(std::getenv(name), fallback);
}

std::uint32_t EnvU32(const char *name, std::uint32_t fallback) {
  return ParseU32(std::getenv(name), fallback);
}

bool EnvHasValue(const char *name) { return HasValue(std::getenv(name)); }

} // namespace NsmbNetplayPoC::Config
