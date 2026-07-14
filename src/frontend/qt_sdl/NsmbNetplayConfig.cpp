#include "NsmbNetplayConfig.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>

namespace NsmbNetplayPoC::Config {

namespace {

class ProcessEnvironment final : public Environment {
public:
  const char *Get(const char *name) const override { return std::getenv(name); }
};

std::uint32_t ComposeMvlSceneSettingsForStage(int stage) {
  const auto clampedStage = static_cast<std::uint32_t>(std::clamp(stage, 0, 4));
  return ((0xB4u + clampedStage) << 16) | 0xFF00u;
}

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

std::vector<std::uint32_t> ParseU32List(const char *value) {
  std::vector<std::uint32_t> values;
  if (!value || !value[0])
    return values;

  std::stringstream stream(value);
  std::string token;
  while (std::getline(stream, token, ',')) {
    token.erase(std::remove_if(token.begin(), token.end(),
                               [](unsigned char character) {
                                 return std::isspace(character) != 0;
                               }),
                token.end());
    if (token.empty())
      continue;
    values.push_back(
        static_cast<std::uint32_t>(std::strtoul(token.c_str(), nullptr, 0)));
  }
  return values;
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

std::vector<std::uint32_t> ReadU32List(const Environment &environment,
                                       const char *name) {
  return ParseU32List(environment.Get(name));
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

PacketBridgeConfig LoadPacketBridgeConfig(const Environment &environment) {
  PacketBridgeConfig config;
  config.Enabled = ReadFlag(environment, "MELONDS_NSML_PACKET_BRIDGE");
  config.Only = ReadFlag(environment, "MELONDS_NSML_PACKET_BRIDGE_ONLY");
  config.AllowPreGame =
      ReadFlag(environment, "MELONDS_NSML_PACKET_BRIDGE_ALLOW_PRE_GAME");
  config.TraceEnabled =
      ReadFlag(environment, "MELONDS_NSML_PACKET_BRIDGE_TRACE");
  config.SendLocalPlayerOnly =
      !ReadFlag(environment, "MELONDS_NSML_PACKET_BRIDGE_SEND_ALL");
  config.WaitEnabled = ReadFlag(environment, "MELONDS_NSML_PACKET_BRIDGE_WAIT");
  config.WaitTimeoutMs = std::max(
      0, ReadInt(environment, "MELONDS_NSML_PACKET_BRIDGE_WAIT_TIMEOUT_MS", 0));
  config.WaitStartFrame = static_cast<std::uint32_t>(
      std::max(0, ReadInt(environment,
                          "MELONDS_NSML_PACKET_BRIDGE_WAIT_START_FRAME", 0)));
  config.WaitTickAhead = std::clamp(
      ReadInt(environment, "MELONDS_NSML_PACKET_BRIDGE_WAIT_TICK_AHEAD", 0), 0,
      32);
  config.DirectCaptureEnabled =
      ReadFlag(environment, "MELONDS_NSML_PACKET_BRIDGE_DIRECT_CAPTURE");
  config.ForceTickEnabled =
      ReadFlag(environment, "MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK");
  config.ForceTickStartFrame = static_cast<std::uint32_t>(std::max(
      0, ReadInt(environment,
                 "MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK_START_FRAME", 0)));
  config.ForceTickBase =
      ReadInt(environment, "MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK_BASE", -1);
  config.ForceNetReady =
      ReadFlag(environment, "MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY");
  config.ForceNetReadyStartFrame = static_cast<std::uint32_t>(std::max(
      0, ReadInt(environment,
                 "MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_START_FRAME", 0)));
  config.ForceNetReadyEndFrame = static_cast<std::uint32_t>(std::max(
      0, ReadInt(environment,
                 "MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_END_FRAME", 0)));
  config.ForceNetReadyHostOnly = ReadFlag(
      environment, "MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_HOST_ONLY");
  config.ForceNetReadyClientOnly = ReadFlag(
      environment, "MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_CLIENT_ONLY");
  config.ForceNetReadyState10 = ReadFlag(
      environment, "MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_STATE10");
  config.ForceNetReadyState10ClientOnly = ReadFlag(
      environment,
      "MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_STATE10_CLIENT_ONLY");
  config.ForceGameLocalPlayerID = ReadInt(
      environment, "MELONDS_NSML_PACKET_BRIDGE_FORCE_GAME_LOCAL_PLAYER_ID", -1);
  config.ForceGameLocalPlayerIDStartFrame = static_cast<std::uint32_t>(std::max(
      0,
      ReadInt(
          environment,
          "MELONDS_NSML_PACKET_BRIDGE_FORCE_GAME_LOCAL_PLAYER_ID_START_FRAME",
          0)));
  config.ForceGameLocalPlayerIDEarly =
      ReadFlag(environment,
               "MELONDS_NSML_PACKET_BRIDGE_FORCE_GAME_LOCAL_PLAYER_ID_EARLY");
  config.MaxPumpEvents = std::clamp(
      ReadInt(environment, "MELONDS_NSML_PACKET_BRIDGE_MAX_PUMP_EVENTS",
              PacketBridgePumpEventLimit),
      1, PacketBridgePumpEventLimit);
  config.MaxTickLead =
      ReadInt(environment, "MELONDS_NSML_PACKET_BRIDGE_MAX_TICK_LEAD", -1);
  config.MaxFrameLead =
      ReadInt(environment, "MELONDS_NSML_PACKET_BRIDGE_MAX_FRAME_LEAD", -1);
  config.ThrottleTimeoutMs = std::max(
      0, ReadInt(environment, "MELONDS_NSML_PACKET_BRIDGE_THROTTLE_TIMEOUT_MS",
                 5000));
  config.ThrottleStartFrame = static_cast<std::uint32_t>(std::max(
      0, ReadInt(environment, "MELONDS_NSML_PACKET_BRIDGE_THROTTLE_START_FRAME",
                 0)));
  config.LocalInputDelay =
      std::max(0, ReadInt(environment,
                          "MELONDS_NSML_PACKET_BRIDGE_LOCAL_INPUT_DELAY", 0));
  config.NeutralizeLocalInput = ReadFlag(
      environment, "MELONDS_NSML_PACKET_BRIDGE_NEUTRALIZE_LOCAL_INPUT");
  config.PreserveLocalTouch =
      ReadFlag(environment, "MELONDS_NSML_PACKET_BRIDGE_PRESERVE_LOCAL_TOUCH");
  config.SendDelayFrames =
      std::max(0, ReadInt(environment,
                          "MELONDS_NSML_PACKET_BRIDGE_SEND_DELAY_FRAMES", 0));
  config.SendJitterFrames =
      std::max(0, ReadInt(environment,
                          "MELONDS_NSML_PACKET_BRIDGE_SEND_JITTER_FRAMES", 0));
  return config;
}

PacketBridgeConfig LoadPacketBridgeConfig() {
  return LoadPacketBridgeConfig(GetProcessEnvironment());
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

MvlConfig LoadMvlConfig(const Environment &environment) {
  MvlConfig config;
  config.DirectBootEnabled =
      ReadFlag(environment, "MELONDS_NSML_DIRECT_MVL_BOOT");
  config.DirectBootHostOnly =
      ReadFlag(environment, "MELONDS_NSML_DIRECT_MVL_BOOT_HOST_ONLY");
  config.DirectBootClientOnly =
      ReadFlag(environment, "MELONDS_NSML_DIRECT_MVL_BOOT_CLIENT_ONLY");
  config.DirectBootFrame = static_cast<std::uint32_t>(std::max(
      0, ReadInt(environment, "MELONDS_NSML_DIRECT_MVL_BOOT_FRAME", 900)));
  config.DirectBootScene = std::clamp(
      ReadInt(environment, "MELONDS_NSML_DIRECT_MVL_BOOT_SCENE", 0x0F), 0,
      0xFFFF);

  const int directStage = std::clamp(
      ReadInt(environment, "MELONDS_NSML_DIRECT_MVL_BOOT_STAGE", 0), 0, 4);
  const int mvlStage = std::clamp(
      ReadInt(environment, "MELONDS_NSML_MVL_STAGE", directStage), 0, 4);
  config.DirectBootStage = mvlStage;
  for (const std::uint32_t stage :
       ReadU32List(environment, "MELONDS_NSML_MVL_STAGE_SEQUENCE"))
    config.StageSequence.push_back(std::clamp(static_cast<int>(stage), 0, 4));
  if (!config.StageSequence.empty())
    config.DirectBootStage = config.StageSequence.front();
  config.DirectBootPlayerID =
      ReadInt(environment, "MELONDS_NSML_DIRECT_MVL_BOOT_PLAYER_ID", -1);

  if (ReadHasValue(environment, "MELONDS_NSML_MVL_SCENE_SETTINGS")) {
    config.StageSceneSettings =
        ReadU32(environment, "MELONDS_NSML_MVL_SCENE_SETTINGS", 0x00B4FF00);
  } else {
    const auto sceneStage = static_cast<int>(
        ReadU32(environment, "MELONDS_NSML_MVL_STAGE",
                ReadU32(environment, "MELONDS_NSML_DIRECT_MVL_BOOT_STAGE", 0)));
    config.StageSceneSettings = ComposeMvlSceneSettingsForStage(sceneStage);
  }

  config.CourseMode =
      ReadCString(environment, "MELONDS_NSML_MVL_COURSE_MODE", "fixed");
  if (config.CourseMode != "fixed" && config.CourseMode != "random" &&
      config.CourseMode != "select") {
    config.InvalidCourseMode = config.CourseMode;
    config.CourseMode = "fixed";
  }
  config.TargetWins =
      std::clamp(ReadInt(environment, "MELONDS_NSML_MVL_WINS", 2), 1, 3);
  config.BigStarTarget =
      std::clamp(ReadInt(environment, "MELONDS_NSML_MVL_BIG_STARS", 5), 3, 10);
  config.RuntimeConfigEnabled =
      ReadHasValue(environment, "MELONDS_NSML_MVL_STAGE") ||
      ReadHasValue(environment, "MELONDS_NSML_MVL_SCENE_SETTINGS") ||
      ReadHasValue(environment, "MELONDS_NSML_MVL_BIG_STARS") ||
      ReadHasValue(environment, "MELONDS_NSML_MVL_LIVES");
  const std::string lives =
      ReadCString(environment, "MELONDS_NSML_MVL_LIVES", "endless");
  config.InitialLives = lives == "5" ? 5u : 3u;
  config.LifeModeSelector = lives == "endless" || lives == "Endless" ? 2u : 0u;
  config.BigStarSelector = config.BigStarTarget == 3    ? 0u
                           : config.BigStarTarget == 10 ? 2u
                                                        : 1u;
  config.NormalizeEntranceSpawnWrites =
      ReadFlag(environment, "MELONDS_NSML_NORMALIZE_MVL_ENTRANCE_SPAWN_WRITES");
  config.AutoRestartAfterResult =
      ReadFlag(environment, "MELONDS_NSML_MVL_AUTO_RESTART_AFTER_RESULT");
  config.AutoRestartDelayFrames = static_cast<std::uint32_t>(
      std::max(1, ReadInt(environment,
                          "MELONDS_NSML_MVL_AUTO_RESTART_DELAY_FRAMES", 120)));
  config.AutoRestartBootstrapFrame = static_cast<std::uint32_t>(
      std::clamp(ReadInt(environment,
                         "MELONDS_NSML_MVL_AUTO_RESTART_BOOTSTRAP_FRAME", 120),
                 0, 1000000));

  return config;
}

MvlConfig LoadMvlConfig() { return LoadMvlConfig(GetProcessEnvironment()); }

DiagnosticsConfig LoadDiagnosticsConfig(const Environment &environment,
                                        int diagnosticRingCapacity) {
  DiagnosticsConfig config;
  config.HangDiagnosticsEnabled =
      ReadFlag(environment, "MELONDS_NSML_HANG_DIAGNOSTICS");
  config.HangWatchdogIntervalMs = std::clamp(
      ReadInt(environment, "MELONDS_NSML_WATCHDOG_INTERVAL_MS", 1000), 100,
      60000);
  config.HangThresholdMs =
      std::clamp(ReadInt(environment, "MELONDS_NSML_HANG_THRESHOLD_MS", 8000),
                 1000, 300000);
  config.HangWatchdogPath =
      ReadCString(environment, "MELONDS_NSML_WATCHDOG_FILE", "");
  config.HangPhaseEventsPath =
      ReadCString(environment, "MELONDS_NSML_PHASE_EVENTS_FILE", "");
  config.HangDumpPath =
      ReadCString(environment, "MELONDS_NSML_HANG_DUMP_FILE", "");
  config.ActiveFpsStartFrame = static_cast<std::uint32_t>(std::max(
      0, ReadInt(environment, "MELONDS_NSML_ACTIVE_FPS_START_FRAME", 0)));
  config.ActiveFrameSpikeThresholdUs =
      std::clamp(
          ReadInt(environment, "MELONDS_NSML_FPS_SPIKE_THRESHOLD_MS", 25), 1,
          1000) *
      1000;
  config.ActiveFrameSpikeTrace =
      ReadFlag(environment, "MELONDS_NSML_FPS_SPIKE_TRACE");
  config.FrameHeartbeatInterval = std::clamp(
      ReadInt(environment, "MELONDS_NSML_FRAME_HEARTBEAT_INTERVAL", 0), 0,
      3600);
  config.GameplayHeartbeatInterval = std::clamp(
      ReadInt(environment, "MELONDS_NSML_GAMEPLAY_HEARTBEAT_INTERVAL", 0), 0,
      3600);
  config.FrameHeartbeatPath =
      ReadCString(environment, "MELONDS_NSML_FRAME_HEARTBEAT_FILE", "");
  config.InputRecordPath =
      ReadCString(environment, "MELONDS_NSML_INPUT_RECORD_FILE", "");
  if (!config.InputRecordPath.empty()) {
    config.InputRecordStartFrame = static_cast<std::uint32_t>(std::max(
        0, ReadInt(environment, "MELONDS_NSML_INPUT_RECORD_START_FRAME", 0)));
    config.InputRecordEndFrame = static_cast<std::uint32_t>(std::max(
        0, ReadInt(environment, "MELONDS_NSML_INPUT_RECORD_END_FRAME", 0)));
    if (config.InputRecordEndFrame != 0 &&
        config.InputRecordEndFrame < config.InputRecordStartFrame)
      config.InputRecordEndFrame = config.InputRecordStartFrame;
    config.InputRecordInstance =
        ReadInt(environment, "MELONDS_NSML_INPUT_RECORD_INSTANCE", -1);
    if (config.InputRecordInstance < 0 || config.InputRecordInstance >= 16)
      config.InputRecordInstance = -1;
  }
  config.ScreenHashEnabled = ReadFlag(environment, "MELONDS_NSML_SCREEN_HASH");
  config.HashLogPath = ReadCString(environment, "MELONDS_NSML_HASH_LOG", "");
  config.ScreenshotDir =
      ReadCString(environment, "MELONDS_NSML_SCREENSHOT_DIR", "");
  config.ScreenshotInterval =
      std::max(0, ReadInt(environment, "MELONDS_NSML_SCREENSHOT_INTERVAL", 0));
  config.RamDumpDir = ReadCString(environment, "MELONDS_NSML_RAM_DUMP_DIR", "");
  config.RamDumpInterval =
      std::max(0, ReadInt(environment, "MELONDS_NSML_RAM_DUMP_INTERVAL", 0));
  config.RamDumpFrames =
      ReadCString(environment, "MELONDS_NSML_RAM_DUMP_FRAMES", "");
  config.GameStateTracePath =
      ReadCString(environment, "MELONDS_NSML_GAME_STATE_TRACE", "");
  config.DiagnosticsPath =
      ReadCString(environment, "MELONDS_NSML_DIAGNOSTICS_FILE", "");
  config.DiagnosticEventsPath =
      ReadCString(environment, "MELONDS_NSML_DIAGNOSTIC_EVENTS_FILE", "");
  config.DiagnosticEventsEnabled =
      ReadFlag(environment, "MELONDS_NSML_DIAGNOSTIC_EVENTS") ||
      !config.DiagnosticEventsPath.empty();
  if (ReadFlag(environment, "MELONDS_NSML_DIAGNOSTIC_EVENTS_DISABLE"))
    config.DiagnosticEventsEnabled = false;
  config.DiagnosticRingFrames = std::clamp(
      ReadInt(environment, "MELONDS_NSML_DIAGNOSTIC_RING_FRAMES", 360), 60,
      std::max(60, diagnosticRingCapacity));
  if (config.DiagnosticEventsEnabled && config.DiagnosticEventsPath.empty() &&
      !config.DiagnosticsPath.empty()) {
    std::filesystem::path eventsPath(config.DiagnosticsPath);
    eventsPath.replace_filename("melonds-events.jsonl");
    config.DiagnosticEventsPath = eventsPath.string();
  }
  config.GameStateTraceInterval = std::max(
      1, ReadInt(environment, "MELONDS_NSML_GAME_STATE_TRACE_INTERVAL", 60));
  config.GameStateTraceStartFrame = static_cast<std::uint32_t>(std::max(
      0, ReadInt(environment, "MELONDS_NSML_GAME_STATE_TRACE_START_FRAME", 0)));
  config.GameStateTraceEndFrame = static_cast<std::uint32_t>(std::max(
      0, ReadInt(environment, "MELONDS_NSML_GAME_STATE_TRACE_END_FRAME", 0)));
  config.GameStateTraceExtended =
      ReadFlag(environment, "MELONDS_NSML_GAME_STATE_TRACE_EXTENDED");
  config.AIPlayLogPath =
      ReadCString(environment, "MELONDS_NSML_AI_PLAY_LOG", "");
  config.AIObservationV2Path =
      ReadCString(environment, "MELONDS_NSML_AI_OBSERVATION_V2_LOG", "");
  config.AIObservationV3Path =
      ReadCString(environment, "MELONDS_NSML_AI_OBSERVATION_V3_LOG", "");
  config.AIPlayLogInterval =
      std::max(1, ReadInt(environment, "MELONDS_NSML_AI_PLAY_LOG_INTERVAL", 1));
  config.AIPlayLogFlushInterval = std::max(
      0, ReadInt(environment, "MELONDS_NSML_AI_PLAY_LOG_FLUSH_INTERVAL", 60));
  config.AIPlayLogStartFrame = static_cast<std::uint32_t>(std::max(
      0, ReadInt(environment, "MELONDS_NSML_AI_PLAY_LOG_START_FRAME", 0)));
  config.AIPlayLogEndFrame = static_cast<std::uint32_t>(std::max(
      0, ReadInt(environment, "MELONDS_NSML_AI_PLAY_LOG_END_FRAME", 0)));
  config.AIPlayLogMaxObjects = std::clamp(
      ReadInt(environment, "MELONDS_NSML_AI_PLAY_LOG_MAX_OBJECTS", 32), 0, 256);
  config.AIObservationV2StageFilter =
      ReadHasValue(environment, "MELONDS_NSML_AI_OBSERVATION_V2_STAGE_FILTER")
          ? std::clamp(ReadInt(environment,
                               "MELONDS_NSML_AI_OBSERVATION_V2_STAGE_FILTER",
                               -1),
                       -1, 4)
          : -1;
  config.AIObservationV3StageFilter =
      ReadHasValue(environment, "MELONDS_NSML_AI_OBSERVATION_V3_STAGE_FILTER")
          ? std::clamp(ReadInt(environment,
                               "MELONDS_NSML_AI_OBSERVATION_V3_STAGE_FILTER",
                               -1),
                       -1, 4)
          : -1;
  config.AIPlayLogGameplayOnly =
      !ReadFlag(environment, "MELONDS_NSML_AI_PLAY_LOG_INCLUDE_NON_GAMEPLAY");
  return config;
}

DiagnosticsConfig LoadDiagnosticsConfig(int diagnosticRingCapacity) {
  return LoadDiagnosticsConfig(GetProcessEnvironment(), diagnosticRingCapacity);
}

StateSyncConfig LoadStateSyncConfig(const Environment &environment) {
  StateSyncConfig config;
  config.GameEnabled = ReadFlag(environment, "MELONDS_NSML_STATE_SYNC");
  config.GameExtended =
      ReadFlag(environment, "MELONDS_NSML_STATE_SYNC_EXTENDED");
  config.GameApplyEnabled = ReadFlag(environment, "MELONDS_NSML_STATE_APPLY");

  const std::string applyMode =
      ReadCString(environment, "MELONDS_NSML_STATE_APPLY_MODE", "");
  if (applyMode == "critical") {
    config.GameApplyStageObjects = false;
    config.GameApplyPlayerActors = false;
  } else if (applyMode == "globals") {
    config.GameApplyStarObjects = false;
    config.GameApplyStageObjects = false;
    config.GameApplyPlayerActors = false;
  } else if (applyMode == "objects") {
    config.GameApplyCriticalGlobals = false;
  } else if (applyMode == "remote-player") {
    config.GameApplyCriticalGlobals = false;
    config.GameApplyStarObjects = false;
    config.GameApplyStageObjects = false;
    config.GameApplyRemotePlayerOnly = true;
  }
  config.GameInterval =
      std::max(1, ReadInt(environment, "MELONDS_NSML_STATE_SYNC_INTERVAL", 60));

  config.PlayerEnabled =
      ReadFlag(environment, "MELONDS_NSML_PLAYER_STATE_SYNC");
  config.PlayerApplyEnabled =
      ReadFlag(environment, "MELONDS_NSML_PLAYER_STATE_APPLY");
  config.PlayerGlobalsEnabled =
      ReadFlag(environment, "MELONDS_NSML_PLAYER_STATE_GLOBALS");
  config.PlayerInterval = std::max(
      1, ReadInt(environment, "MELONDS_NSML_PLAYER_STATE_SYNC_INTERVAL", 1));
  config.PlayerMaxPredictFrames =
      std::max(0, ReadInt(environment,
                          "MELONDS_NSML_PLAYER_STATE_MAX_PREDICT_FRAMES", 2));

  config.WorldEnabled = ReadFlag(environment, "MELONDS_NSML_WORLD_STATE_SYNC");
  config.WorldApplyEnabled =
      ReadFlag(environment, "MELONDS_NSML_WORLD_STATE_APPLY");
  config.WorldApplyStarActor =
      !ReadFlag(environment, "MELONDS_NSML_WORLD_STATE_SKIP_STAR");
  config.WorldSpawnItem =
      ReadFlag(environment, "MELONDS_NSML_WORLD_STATE_SPAWN_ITEM");
  config.WorldApplyMovingHazard =
      ReadFlag(environment, "MELONDS_NSML_WORLD_STATE_APPLY_MOVING_HAZARD") &&
      !ReadFlag(environment, "MELONDS_NSML_WORLD_STATE_SKIP_MOVING_HAZARD");
  config.WorldApplyEffects =
      ReadFlag(environment, "MELONDS_NSML_WORLD_STATE_APPLY_EFFECTS") &&
      !ReadFlag(environment, "MELONDS_NSML_WORLD_STATE_SKIP_EFFECTS");
  config.WorldApplyActorSnapshot =
      ReadFlag(environment, "MELONDS_NSML_WORLD_STATE_APPLY_ACTOR_SNAPSHOT");
  config.WorldTraceMovingHazards =
      ReadFlag(environment, "MELONDS_NSML_WORLD_STATE_TRACE_MOVING_HAZARDS");
  config.WorldTraceObjectLifecycles =
      ReadFlag(environment, "MELONDS_NSML_WORLD_STATE_TRACE_OBJECT_LIFECYCLES");
  config.WorldTraceActorInternals =
      ReadFlag(environment, "MELONDS_NSML_WORLD_STATE_TRACE_ACTOR_INTERNALS");
  config.WorldTraceEffects =
      ReadFlag(environment, "MELONDS_NSML_WORLD_STATE_TRACE_EFFECTS");
  config.WorldTraceObjectLifecyclesInterval = std::max(
      1,
      ReadInt(environment,
              "MELONDS_NSML_WORLD_STATE_TRACE_OBJECT_LIFECYCLES_INTERVAL", 60));
  config.WorldTraceObjectLifecyclesStartFrame =
      static_cast<std::uint32_t>(std::max(
          0, ReadInt(
                 environment,
                 "MELONDS_NSML_WORLD_STATE_TRACE_OBJECT_LIFECYCLES_START_FRAME",
                 0)));
  config.WorldTraceObjectLifecyclesEndFrame =
      static_cast<std::uint32_t>(std::max(
          0,
          ReadInt(environment,
                  "MELONDS_NSML_WORLD_STATE_TRACE_OBJECT_LIFECYCLES_END_FRAME",
                  0)));
  config.WorldInterval = std::max(
      1, ReadInt(environment, "MELONDS_NSML_WORLD_STATE_SYNC_INTERVAL", 2));
  config.WorldMaxPredictFrames =
      std::max(0, ReadInt(environment,
                          "MELONDS_NSML_WORLD_STATE_MAX_PREDICT_FRAMES", 1));
  config.WorldActorRescanInterval =
      std::max(0, ReadInt(environment,
                          "MELONDS_NSML_WORLD_STATE_ACTOR_RESCAN_INTERVAL", 0));
  return config;
}

StateSyncConfig LoadStateSyncConfig() {
  return LoadStateSyncConfig(GetProcessEnvironment());
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

std::vector<std::uint32_t> EnvU32List(const char *name) {
  return ReadU32List(GetProcessEnvironment(), name);
}

bool EnvHasValue(const char *name) {
  return ReadHasValue(GetProcessEnvironment(), name);
}

} // namespace NsmbNetplayPoC::Config
