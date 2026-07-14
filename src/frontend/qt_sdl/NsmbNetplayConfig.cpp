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

ConnectionConfig LoadConnectionConfig(const Environment &environment,
                                      bool testEnabled) {
  ConnectionConfig config;
  const char *role = environment.Get("MELONDS_NSML_ROLE");
  if (!HasValue(role))
    role = environment.Get("MELONDS_NSML_LAN_ROLE");
  config.Client = HasValue(role) && std::strcmp(role, "client") == 0;

  config.Delay = std::max(0, ReadInt(environment, "MELONDS_NSML_DELAY", 6));
  config.WarmupFrames =
      std::max(0, ReadInt(environment, "MELONDS_NSML_NETPLAY_WARMUP_FRAMES",
                          testEnabled ? config.Delay * 2 : 0));
  config.Port = ReadInt(environment, "MELONDS_NSML_PORT", 8065);
  config.LocalInstance = ReadInt(environment, "MELONDS_NSML_LOCAL_INSTANCE",
                                 config.Client ? 1 : 0);
  config.StartFrame = static_cast<std::uint32_t>(
      std::max(0, ReadInt(environment, "MELONDS_NSML_NETPLAY_START_FRAME", 0)));
  config.LocalWaitsForRemote =
      !ReadFlag(environment, "MELONDS_NSML_NO_LOCAL_WAIT");
  config.RemoteInputTimeoutFatal =
      ReadFlag(environment, "MELONDS_NSML_REMOTE_INPUT_TIMEOUT_FATAL");
  config.PeerHost = ReadCString(environment, "MELONDS_NSML_PEER", "127.0.0.1");
  return config;
}

ConnectionConfig LoadConnectionConfig(bool testEnabled) {
  return LoadConnectionConfig(GetProcessEnvironment(), testEnabled);
}

InputConfig LoadInputConfig(const Environment &environment,
                            bool netplayOnlyForMaxFrameLeadDefault) {
  InputConfig config;
  config.SendDelayFrames = std::max(
      0, ReadInt(environment, "MELONDS_NSML_INPUT_SEND_DELAY_FRAMES", 0));
  config.SendJitterFrames = std::max(
      0, ReadInt(environment, "MELONDS_NSML_INPUT_SEND_JITTER_FRAMES", 0));
  config.SendDelayStartFrame = static_cast<std::uint32_t>(std::clamp(
      ReadInt(environment, "MELONDS_NSML_INPUT_SEND_DELAY_START_FRAME", 0), 0,
      1000000));
  config.SendDelayEndFrame = static_cast<std::uint32_t>(std::clamp(
      ReadInt(environment, "MELONDS_NSML_INPUT_SEND_DELAY_END_FRAME", 0), 0,
      1000000));
  if (config.SendDelayEndFrame != 0 &&
      config.SendDelayEndFrame < config.SendDelayStartFrame)
    config.SendDelayEndFrame = config.SendDelayStartFrame;

  config.UseHistoryBundle =
      ReadFlag(environment, "MELONDS_NSML_INPUT_UNRELIABLE");
  config.BundleHistory = std::clamp(
      ReadInt(environment, "MELONDS_NSML_INPUT_BUNDLE_HISTORY", 0), 0, 31);
  config.DropModulo =
      std::max(0, ReadInt(environment, "MELONDS_NSML_INPUT_DROP_MODULO", 0));
  config.DropOffset =
      std::max(0, ReadInt(environment, "MELONDS_NSML_INPUT_DROP_OFFSET", 0));
  if (config.DropModulo > 0)
    config.DropOffset %= config.DropModulo;
  config.DropStartFrame = static_cast<std::uint32_t>(
      std::clamp(ReadInt(environment, "MELONDS_NSML_INPUT_DROP_START_FRAME", 0),
                 0, 1000000));
  config.DropEndFrame = static_cast<std::uint32_t>(
      std::clamp(ReadInt(environment, "MELONDS_NSML_INPUT_DROP_END_FRAME", 0),
                 0, 1000000));
  if (config.DropEndFrame > 0 && config.DropEndFrame < config.DropStartFrame)
    config.DropEndFrame = config.DropStartFrame;

  config.MaxFrameLead =
      ReadInt(environment, "MELONDS_NSML_INPUT_MAX_FRAME_LEAD",
              netplayOnlyForMaxFrameLeadDefault ? 2 : -1);
  config.NetplayOnly = ReadFlag(environment, "MELONDS_NSML_INPUT_NETPLAY_ONLY");
  config.NetplayTrace =
      ReadFlag(environment, "MELONDS_NSML_INPUT_NETPLAY_TRACE");
  config.HealthTrace = ReadFlag(environment, "MELONDS_NSML_INPUT_HEALTH_TRACE");
  config.HealthTraceInterval = std::clamp(
      ReadInt(environment, "MELONDS_NSML_INPUT_HEALTH_TRACE_INTERVAL", 120), 1,
      3600);
  config.HealthTraceWaitThresholdMs = std::clamp(
      ReadInt(environment, "MELONDS_NSML_INPUT_HEALTH_TRACE_WAIT_THRESHOLD_MS",
              16),
      1, 5000);
  config.WaitPollUs = std::clamp(
      ReadInt(environment, "MELONDS_NSML_INPUT_WAIT_POLL_US", 100), 50, 5000);
  return config;
}

InputConfig LoadInputConfig(bool netplayOnlyForMaxFrameLeadDefault) {
  return LoadInputConfig(GetProcessEnvironment(),
                         netplayOnlyForMaxFrameLeadDefault);
}

RollbackConfig LoadRollbackConfig(const Environment &environment) {
  RollbackConfig config;
  config.Enabled = ReadFlag(environment, "MELONDS_NSML_ROLLBACK");
  config.Resimulate = ReadFlag(environment, "MELONDS_NSML_ROLLBACK_RESIMULATE");
  config.SkipRenderDuringResim =
      ReadFlag(environment, "MELONDS_NSML_ROLLBACK_RESIM_SKIP_RENDER");
  config.SkipIntermediateResimCheckpoints = ReadFlag(
      environment, "MELONDS_NSML_ROLLBACK_RESIM_SKIP_INTERMEDIATE_CHECKPOINTS");
  config.InputWaitUs = std::clamp(
      ReadInt(environment, "MELONDS_NSML_ROLLBACK_INPUT_WAIT_US", 0), 0, 20000);
  config.RestoreProbe =
      ReadFlag(environment, "MELONDS_NSML_ROLLBACK_RESTORE_PROBE");
  config.PredictionProbeModulo = std::clamp(
      ReadInt(environment, "MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_MODULO", 0),
      0, 600);
  config.PredictionProbeOffset = std::clamp(
      ReadInt(environment, "MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_OFFSET", 0),
      0, std::max(0, config.PredictionProbeModulo - 1));
  config.PredictionProbeLimit = std::clamp(
      ReadInt(environment, "MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_LIMIT", -1),
      -1, 10000);
  config.PredictionProbeStartFrame = static_cast<std::uint32_t>(std::clamp(
      ReadInt(environment, "MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_START_FRAME",
              0),
      0, 1000000));
  config.PredictionProbeEndFrame = static_cast<std::uint32_t>(
      std::clamp(ReadInt(environment,
                         "MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_END_FRAME", 0),
                 0, 1000000));
  if (config.PredictionProbeEndFrame != 0 &&
      config.PredictionProbeEndFrame < config.PredictionProbeStartFrame)
    config.PredictionProbeEndFrame = config.PredictionProbeStartFrame;
  config.PredictionProbeKeyMask = static_cast<std::uint32_t>(std::clamp(
      ReadInt(environment, "MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_KEY_MASK",
              0x1),
      1, 0xFFF));

  const char *backend =
      ReadCString(environment, "MELONDS_NSML_ROLLBACK_BACKEND", "savestate");
  if (!std::strcmp(backend, "corelite") || !std::strcmp(backend, "core-lite"))
    config.Backend = RollbackBackend::CoreLite;
  else if (!std::strcmp(backend, "coresparse") ||
           !std::strcmp(backend, "core-sparse"))
    config.Backend = RollbackBackend::CoreSparse;
  else if (!std::strcmp(backend, "coredelta") ||
           !std::strcmp(backend, "core-delta"))
    config.Backend = RollbackBackend::CoreDelta;
  else if (!std::strcmp(backend, "coreframedelta") ||
           !std::strcmp(backend, "core-frame-delta"))
    config.Backend = RollbackBackend::CoreFrameDelta;
  else if (!std::strcmp(backend, "corepreimage") ||
           !std::strcmp(backend, "core-preimage"))
    config.Backend = RollbackBackend::CorePreimage;
  else if (!std::strcmp(backend, "tinycorepreimage") ||
           !std::strcmp(backend, "tiny-core-preimage"))
    config.Backend = RollbackBackend::TinyCorePreimage;
  else if (!std::strcmp(backend, "nsmbranges") ||
           !std::strcmp(backend, "nsmb-ranges"))
    config.Backend = RollbackBackend::NSMBRanges;
  else if (!std::strcmp(backend, "nsmbcoreranges") ||
           !std::strcmp(backend, "nsmb-core-ranges"))
    config.Backend = RollbackBackend::NSMBCoreRanges;
  else if (!std::strcmp(backend, "nsmbtinycore") ||
           !std::strcmp(backend, "nsmb-tiny-core"))
    config.Backend = RollbackBackend::NSMBTinyCoreRanges;
  else if (!std::strcmp(backend, "arm9ram") || !std::strcmp(backend, "ram"))
    config.Backend = RollbackBackend::ARM9RAM;

  config.Window = std::clamp(
      ReadInt(environment, "MELONDS_NSML_ROLLBACK_WINDOW", 20), 1, 180);
  config.CheckpointInterval = std::clamp(
      ReadInt(environment, "MELONDS_NSML_ROLLBACK_CHECKPOINT_INTERVAL", 1), 1,
      30);
  config.DeltaKeyframeInterval = std::clamp(
      ReadInt(environment, "MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL", 10),
      1, 60);
  config.MainRAMPageSize = std::clamp(
      ReadInt(environment, "MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE", 4096),
      256, 4096);
  if ((config.MainRAMPageSize & (config.MainRAMPageSize - 1)) != 0)
    config.MainRAMPageSize = 4096;
  config.CoreSkipMask = std::clamp(
      ReadInt(environment, "MELONDS_NSML_ROLLBACK_CORE_SKIP_MASK", 0), 0, 31);
  config.TinyCoreFlags = std::clamp(
      ReadInt(environment, "MELONDS_NSML_ROLLBACK_TINY_CORE_FLAGS", 0), 0,
      2047);
  config.NSMBWideRanges =
      ReadFlag(environment, "MELONDS_NSML_ROLLBACK_NSMB_WIDE_RANGES");
  config.NSMBDeltaDiscoveredRanges = ReadFlag(
      environment, "MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES");
  config.NSMBActorArenaRanges =
      ReadFlag(environment, "MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES");
  config.NSMBArm9StackRange =
      ReadFlag(environment, "MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE");
  config.NSMBSkipInputRanges =
      ReadFlag(environment, "MELONDS_NSML_ROLLBACK_NSMB_SKIP_INPUT_RANGES");
  config.NSMBRestoreDiffTrace =
      ReadFlag(environment, "MELONDS_NSML_ROLLBACK_NSMB_RESTORE_DIFF_TRACE");
  config.NSMBProcessListRanges =
      ReadFlag(environment, "MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES");
  config.NSMBHeapScanRanges =
      ReadInt(environment, "MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES", 1) !=
      0;
  config.NSMBScanInterval = std::clamp(
      ReadInt(environment, "MELONDS_NSML_ROLLBACK_NSMB_SCAN_INTERVAL", 1), 1,
      600);
  config.NSMBHeapScanInterval = std::clamp(
      ReadInt(environment, "MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL",
              config.NSMBScanInterval),
      1, 1800);
  config.DeltaPageTrace =
      ReadFlag(environment, "MELONDS_NSML_ROLLBACK_DELTA_PAGE_TRACE");
  config.DeltaPageTraceStartFrame = static_cast<std::uint32_t>(std::max(
      0, ReadInt(environment,
                 "MELONDS_NSML_ROLLBACK_DELTA_PAGE_TRACE_START_FRAME", 0)));
  config.DeltaPageTraceEndFrame = static_cast<std::uint32_t>(std::max(
      0, ReadInt(environment,
                 "MELONDS_NSML_ROLLBACK_DELTA_PAGE_TRACE_END_FRAME", 0)));
  config.DeltaPageTraceMaxRuns =
      std::clamp(ReadInt(environment,
                         "MELONDS_NSML_ROLLBACK_DELTA_PAGE_TRACE_MAX_RUNS", 12),
                 1, 80);
  config.ResimulateDelayFrames = std::clamp(
      ReadInt(environment, "MELONDS_NSML_ROLLBACK_RESIMULATE_DELAY_FRAMES", 0),
      0, 30);
  config.MaxResimFrames = std::clamp(
      ReadInt(environment, "MELONDS_NSML_ROLLBACK_MAX_RESIM_FRAMES", 0), 0, 30);
  return config;
}

RollbackConfig LoadRollbackConfig() {
  return LoadRollbackConfig(GetProcessEnvironment());
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
