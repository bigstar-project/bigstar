#ifndef NSMBNETPLAYCONFIG_H
#define NSMBNETPLAYCONFIG_H

#include <cstdint>

namespace NsmbNetplayPoC::Config {

bool ParseFlag(const char *value);
const char *ValueOr(const char *value, const char *fallback);
int ParseInt(const char *value, int fallback);
double ParseDouble(const char *value, double fallback);
std::uint32_t ParseU32(const char *value, std::uint32_t fallback);
bool HasValue(const char *value);

bool EnvFlag(const char *name);
const char *EnvCString(const char *name, const char *fallback);
int EnvInt(const char *name, int fallback);
double EnvDouble(const char *name, double fallback);
std::uint32_t EnvU32(const char *name, std::uint32_t fallback);
bool EnvHasValue(const char *name);

} // namespace NsmbNetplayPoC::Config

#endif
