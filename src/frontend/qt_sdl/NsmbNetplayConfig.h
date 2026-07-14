#ifndef NSMBNETPLAYCONFIG_H
#define NSMBNETPLAYCONFIG_H

#include <cstdint>

namespace NsmbNetplayPoC::Config {

class Environment {
public:
  virtual ~Environment() = default;
  virtual const char *Get(const char *name) const = 0;
};

const Environment &GetProcessEnvironment();

struct BootstrapConfig {
  bool Enabled = false;
  bool TestEnabled = false;
  std::uint32_t TestFrames = 0;
  int TestInstanceCount = 1;
  bool HashEnabled = true;
  int HashInterval = 60;
  int WaitTimeoutMs = 60000;
  int QuitGraceMs = 0;
  bool InputTraceEnabled = false;
  int InputTraceInterval = 60;
};

bool ParseFlag(const char *value);
const char *ValueOr(const char *value, const char *fallback);
int ParseInt(const char *value, int fallback);
double ParseDouble(const char *value, double fallback);
std::uint32_t ParseU32(const char *value, std::uint32_t fallback);
bool HasValue(const char *value);

bool ReadFlag(const Environment &environment, const char *name);
const char *ReadCString(const Environment &environment, const char *name,
                        const char *fallback);
int ReadInt(const Environment &environment, const char *name, int fallback);
double ReadDouble(const Environment &environment, const char *name,
                  double fallback);
std::uint32_t ReadU32(const Environment &environment, const char *name,
                      std::uint32_t fallback);
bool ReadHasValue(const Environment &environment, const char *name);

BootstrapConfig LoadBootstrapConfig(const Environment &environment);
BootstrapConfig LoadBootstrapConfig();

bool EnvFlag(const char *name);
const char *EnvCString(const char *name, const char *fallback);
int EnvInt(const char *name, int fallback);
double EnvDouble(const char *name, double fallback);
std::uint32_t EnvU32(const char *name, std::uint32_t fallback);
bool EnvHasValue(const char *name);

} // namespace NsmbNetplayPoC::Config

#endif
