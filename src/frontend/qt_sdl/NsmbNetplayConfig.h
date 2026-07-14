#ifndef NSMBNETPLAYCONFIG_H
#define NSMBNETPLAYCONFIG_H

#include <cstdint>
#include <string>

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

struct ConnectionConfig {
  bool Client = false;
  int Delay = 6;
  int WarmupFrames = 0;
  int Port = 8065;
  int LocalInstance = 0;
  std::uint32_t StartFrame = 0;
  bool LocalWaitsForRemote = true;
  bool RemoteInputTimeoutFatal = false;
  std::string PeerHost = "127.0.0.1";
};

struct InputConfig {
  int SendDelayFrames = 0;
  int SendJitterFrames = 0;
  std::uint32_t SendDelayStartFrame = 0;
  std::uint32_t SendDelayEndFrame = 0;
  bool UseHistoryBundle = false;
  int BundleHistory = 0;
  int DropModulo = 0;
  int DropOffset = 0;
  std::uint32_t DropStartFrame = 0;
  std::uint32_t DropEndFrame = 0;
  int MaxFrameLead = -1;
  bool NetplayOnly = false;
  bool NetplayTrace = false;
  bool HealthTrace = false;
  int HealthTraceInterval = 120;
  int HealthTraceWaitThresholdMs = 16;
  int WaitPollUs = 100;
};

enum class RollbackBackend {
  Savestate,
  CoreLite,
  CoreSparse,
  CoreDelta,
  CoreFrameDelta,
  CorePreimage,
  TinyCorePreimage,
  NSMBRanges,
  NSMBCoreRanges,
  NSMBTinyCoreRanges,
  ARM9RAM,
};

struct RollbackConfig {
  bool Enabled = false;
  bool Resimulate = false;
  bool SkipRenderDuringResim = false;
  bool SkipIntermediateResimCheckpoints = false;
  int InputWaitUs = 0;
  bool RestoreProbe = false;
  int PredictionProbeModulo = 0;
  int PredictionProbeOffset = 0;
  int PredictionProbeLimit = -1;
  std::uint32_t PredictionProbeStartFrame = 0;
  std::uint32_t PredictionProbeEndFrame = 0;
  std::uint32_t PredictionProbeKeyMask = 1;
  RollbackBackend Backend = RollbackBackend::Savestate;
  int Window = 20;
  int CheckpointInterval = 1;
  int DeltaKeyframeInterval = 10;
  int MainRAMPageSize = 4096;
  int CoreSkipMask = 0;
  int TinyCoreFlags = 0;
  bool NSMBWideRanges = false;
  bool NSMBDeltaDiscoveredRanges = false;
  bool NSMBActorArenaRanges = false;
  bool NSMBArm9StackRange = false;
  bool NSMBSkipInputRanges = false;
  bool NSMBRestoreDiffTrace = false;
  bool NSMBProcessListRanges = false;
  bool NSMBHeapScanRanges = true;
  int NSMBScanInterval = 1;
  int NSMBHeapScanInterval = 1;
  bool DeltaPageTrace = false;
  std::uint32_t DeltaPageTraceStartFrame = 0;
  std::uint32_t DeltaPageTraceEndFrame = 0;
  int DeltaPageTraceMaxRuns = 12;
  int ResimulateDelayFrames = 0;
  int MaxResimFrames = 0;
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
ConnectionConfig LoadConnectionConfig(const Environment &environment,
                                      bool testEnabled);
ConnectionConfig LoadConnectionConfig(bool testEnabled);
InputConfig LoadInputConfig(const Environment &environment,
                            bool netplayOnlyForMaxFrameLeadDefault);
InputConfig LoadInputConfig(bool netplayOnlyForMaxFrameLeadDefault);
RollbackConfig LoadRollbackConfig(const Environment &environment);
RollbackConfig LoadRollbackConfig();

bool EnvFlag(const char *name);
const char *EnvCString(const char *name, const char *fallback);
int EnvInt(const char *name, int fallback);
double EnvDouble(const char *name, double fallback);
std::uint32_t EnvU32(const char *name, std::uint32_t fallback);
bool EnvHasValue(const char *name);

} // namespace NsmbNetplayPoC::Config

#endif
