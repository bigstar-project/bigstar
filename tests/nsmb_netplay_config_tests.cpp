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

void TestUnsignedListParsingPreservesLegacySemantics() {
  using NsmbNetplayPoC::Config::ParseU32List;
  CHECK(ParseU32List(nullptr).empty());
  CHECK(ParseU32List("").empty());
  const auto values = ParseU32List(" 1, 0x10, invalid, , 03 ");
  CHECK(values.size() == 4);
  CHECK(values[0] == 1u);
  CHECK(values[1] == 16u);
  CHECK(values[2] == 0u);
  CHECK(values[3] == 3u);
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

void TestRollbackConfigDefaultsAndBackendAliases() {
  using NsmbNetplayPoC::Config::RollbackBackend;
  MapEnvironment environment;
  auto config = NsmbNetplayPoC::Config::LoadRollbackConfig(environment);
  CHECK(!config.Enabled);
  CHECK(!config.Resimulate);
  CHECK(config.InputWaitUs == 0);
  CHECK(config.PredictionProbeModulo == 0);
  CHECK(config.PredictionProbeOffset == 0);
  CHECK(config.PredictionProbeLimit == -1);
  CHECK(config.PredictionProbeKeyMask == 1u);
  CHECK(config.Backend == RollbackBackend::Savestate);
  CHECK(config.Window == 20);
  CHECK(config.CheckpointInterval == 1);
  CHECK(config.DeltaKeyframeInterval == 10);
  CHECK(config.MainRAMPageSize == 4096);
  CHECK(config.NSMBHeapScanRanges);
  CHECK(config.NSMBScanInterval == 1);
  CHECK(config.NSMBHeapScanInterval == 1);

  const std::pair<const char *, RollbackBackend> aliases[] = {
      {"core-lite", RollbackBackend::CoreLite},
      {"coresparse", RollbackBackend::CoreSparse},
      {"core-delta", RollbackBackend::CoreDelta},
      {"coreframedelta", RollbackBackend::CoreFrameDelta},
      {"core-preimage", RollbackBackend::CorePreimage},
      {"tiny-core-preimage", RollbackBackend::TinyCorePreimage},
      {"nsmb-ranges", RollbackBackend::NSMBRanges},
      {"nsmbcoreranges", RollbackBackend::NSMBCoreRanges},
      {"nsmb-tiny-core", RollbackBackend::NSMBTinyCoreRanges},
      {"ram", RollbackBackend::ARM9RAM},
      {"unknown", RollbackBackend::Savestate},
  };
  for (const auto &alias : aliases) {
    environment.Values["MELONDS_NSML_ROLLBACK_BACKEND"] = alias.first;
    config = NsmbNetplayPoC::Config::LoadRollbackConfig(environment);
    CHECK(config.Backend == alias.second);
  }
}

void TestRollbackConfigReadsClampsAndDependencies() {
  MapEnvironment environment;
  environment.Values = {
      {"MELONDS_NSML_ROLLBACK", "1"},
      {"MELONDS_NSML_ROLLBACK_RESIMULATE", "1"},
      {"MELONDS_NSML_ROLLBACK_RESIM_SKIP_RENDER", "1"},
      {"MELONDS_NSML_ROLLBACK_RESIM_SKIP_INTERMEDIATE_CHECKPOINTS", "1"},
      {"MELONDS_NSML_ROLLBACK_INPUT_WAIT_US", "99999"},
      {"MELONDS_NSML_ROLLBACK_RESTORE_PROBE", "1"},
      {"MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_MODULO", "7"},
      {"MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_OFFSET", "20"},
      {"MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_LIMIT", "20000"},
      {"MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_START_FRAME", "100"},
      {"MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_END_FRAME", "90"},
      {"MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_KEY_MASK", "0"},
      {"MELONDS_NSML_ROLLBACK_WINDOW", "999"},
      {"MELONDS_NSML_ROLLBACK_CHECKPOINT_INTERVAL", "0"},
      {"MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL", "99"},
      {"MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE", "300"},
      {"MELONDS_NSML_ROLLBACK_CORE_SKIP_MASK", "99"},
      {"MELONDS_NSML_ROLLBACK_TINY_CORE_FLAGS", "9999"},
      {"MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES", "0"},
      {"MELONDS_NSML_ROLLBACK_NSMB_SCAN_INTERVAL", "77"},
      {"MELONDS_NSML_ROLLBACK_DELTA_PAGE_TRACE_MAX_RUNS", "0"},
      {"MELONDS_NSML_ROLLBACK_RESIMULATE_DELAY_FRAMES", "99"},
      {"MELONDS_NSML_ROLLBACK_MAX_RESIM_FRAMES", "-1"},
  };

  const auto config = NsmbNetplayPoC::Config::LoadRollbackConfig(environment);
  CHECK(config.Enabled);
  CHECK(config.Resimulate);
  CHECK(config.SkipRenderDuringResim);
  CHECK(config.SkipIntermediateResimCheckpoints);
  CHECK(config.InputWaitUs == 20000);
  CHECK(config.RestoreProbe);
  CHECK(config.PredictionProbeModulo == 7);
  CHECK(config.PredictionProbeOffset == 6);
  CHECK(config.PredictionProbeLimit == 10000);
  CHECK(config.PredictionProbeStartFrame == 100u);
  CHECK(config.PredictionProbeEndFrame == 100u);
  CHECK(config.PredictionProbeKeyMask == 1u);
  CHECK(config.Window == 180);
  CHECK(config.CheckpointInterval == 1);
  CHECK(config.DeltaKeyframeInterval == 60);
  CHECK(config.MainRAMPageSize == 4096);
  CHECK(config.CoreSkipMask == 31);
  CHECK(config.TinyCoreFlags == 2047);
  CHECK(!config.NSMBHeapScanRanges);
  CHECK(config.NSMBScanInterval == 77);
  CHECK(config.NSMBHeapScanInterval == 77);
  CHECK(config.DeltaPageTraceMaxRuns == 1);
  CHECK(config.ResimulateDelayFrames == 30);
  CHECK(config.MaxResimFrames == 0);
}

void TestMvlConfigDefaults() {
  const MapEnvironment environment;
  const auto config = NsmbNetplayPoC::Config::LoadMvlConfig(environment);
  CHECK(!config.DirectBootEnabled);
  CHECK(!config.DirectBootHostOnly);
  CHECK(!config.DirectBootClientOnly);
  CHECK(config.DirectBootFrame == 900u);
  CHECK(config.DirectBootScene == 0x0F);
  CHECK(config.DirectBootStage == 0);
  CHECK(config.StageSequence.empty());
  CHECK(config.DirectBootPlayerID == -1);
  CHECK(config.StageSceneSettings == 0x00B4FF00u);
  CHECK(config.CourseMode == "fixed");
  CHECK(config.InvalidCourseMode.empty());
  CHECK(config.TargetWins == 2);
  CHECK(config.BigStarTarget == 5);
  CHECK(!config.RuntimeConfigEnabled);
  CHECK(config.InitialLives == 3u);
  CHECK(config.LifeModeSelector == 2u);
  CHECK(config.BigStarSelector == 1u);
  CHECK(!config.NormalizeEntranceSpawnWrites);
  CHECK(!config.AutoRestartAfterResult);
  CHECK(config.AutoRestartDelayFrames == 120u);
  CHECK(config.AutoRestartBootstrapFrame == 120u);
}

void TestMvlConfigReadsClampsAndPreservesPriority() {
  MapEnvironment environment;
  environment.Values = {
      {"MELONDS_NSML_DIRECT_MVL_BOOT", "1"},
      {"MELONDS_NSML_DIRECT_MVL_BOOT_HOST_ONLY", "1"},
      {"MELONDS_NSML_DIRECT_MVL_BOOT_CLIENT_ONLY", "1"},
      {"MELONDS_NSML_DIRECT_MVL_BOOT_FRAME", "-4"},
      {"MELONDS_NSML_DIRECT_MVL_BOOT_SCENE", "99999"},
      {"MELONDS_NSML_DIRECT_MVL_BOOT_STAGE", "9"},
      {"MELONDS_NSML_MVL_STAGE", "-3"},
      {"MELONDS_NSML_MVL_STAGE_SEQUENCE", " 4, 9, invalid, , 2 "},
      {"MELONDS_NSML_DIRECT_MVL_BOOT_PLAYER_ID", "7"},
      {"MELONDS_NSML_MVL_COURSE_MODE", "unsupported"},
      {"MELONDS_NSML_MVL_WINS", "0"},
      {"MELONDS_NSML_MVL_BIG_STARS", "99"},
      {"MELONDS_NSML_MVL_LIVES", "5"},
      {"MELONDS_NSML_NORMALIZE_MVL_ENTRANCE_SPAWN_WRITES", "1"},
      {"MELONDS_NSML_MVL_AUTO_RESTART_AFTER_RESULT", "1"},
      {"MELONDS_NSML_MVL_AUTO_RESTART_DELAY_FRAMES", "-1"},
      {"MELONDS_NSML_MVL_AUTO_RESTART_BOOTSTRAP_FRAME", "2000000"},
  };

  auto config = NsmbNetplayPoC::Config::LoadMvlConfig(environment);
  CHECK(config.DirectBootEnabled);
  CHECK(config.DirectBootHostOnly);
  CHECK(config.DirectBootClientOnly);
  CHECK(config.DirectBootFrame == 0u);
  CHECK(config.DirectBootScene == 0xFFFF);
  CHECK(config.StageSequence == std::vector<int>({4, 4, 0, 2}));
  CHECK(config.DirectBootStage == 4);
  CHECK(config.DirectBootPlayerID == 7);
  CHECK(config.StageSceneSettings == 0x00B4FF00u);
  CHECK(config.CourseMode == "fixed");
  CHECK(config.InvalidCourseMode == "unsupported");
  CHECK(config.TargetWins == 1);
  CHECK(config.BigStarTarget == 10);
  CHECK(config.RuntimeConfigEnabled);
  CHECK(config.InitialLives == 5u);
  CHECK(config.LifeModeSelector == 0u);
  CHECK(config.BigStarSelector == 2u);
  CHECK(config.NormalizeEntranceSpawnWrites);
  CHECK(config.AutoRestartAfterResult);
  CHECK(config.AutoRestartDelayFrames == 1u);
  CHECK(config.AutoRestartBootstrapFrame == 1000000u);

  environment.Values["MELONDS_NSML_MVL_SCENE_SETTINGS"] = "invalid";
  config = NsmbNetplayPoC::Config::LoadMvlConfig(environment);
  CHECK(config.StageSceneSettings == 0u);
}

void TestStateSyncConfigDefaultsAndApplyModes() {
  MapEnvironment environment;
  auto config = NsmbNetplayPoC::Config::LoadStateSyncConfig(environment);
  CHECK(!config.GameEnabled);
  CHECK(!config.GameExtended);
  CHECK(!config.GameApplyEnabled);
  CHECK(config.GameApplyCriticalGlobals);
  CHECK(config.GameApplyStarObjects);
  CHECK(config.GameApplyStageObjects);
  CHECK(config.GameApplyPlayerActors);
  CHECK(!config.GameApplyRemotePlayerOnly);
  CHECK(config.GameInterval == 60);
  CHECK(!config.PlayerEnabled);
  CHECK(config.PlayerInterval == 1);
  CHECK(config.PlayerMaxPredictFrames == 2);
  CHECK(!config.WorldEnabled);
  CHECK(config.WorldApplyStarActor);
  CHECK(config.WorldInterval == 2);
  CHECK(config.WorldMaxPredictFrames == 1);
  CHECK(config.WorldTraceObjectLifecyclesInterval == 60);

  environment.Values["MELONDS_NSML_STATE_APPLY_MODE"] = "critical";
  config = NsmbNetplayPoC::Config::LoadStateSyncConfig(environment);
  CHECK(config.GameApplyCriticalGlobals);
  CHECK(config.GameApplyStarObjects);
  CHECK(!config.GameApplyStageObjects);
  CHECK(!config.GameApplyPlayerActors);

  environment.Values["MELONDS_NSML_STATE_APPLY_MODE"] = "globals";
  config = NsmbNetplayPoC::Config::LoadStateSyncConfig(environment);
  CHECK(config.GameApplyCriticalGlobals);
  CHECK(!config.GameApplyStarObjects);
  CHECK(!config.GameApplyStageObjects);
  CHECK(!config.GameApplyPlayerActors);

  environment.Values["MELONDS_NSML_STATE_APPLY_MODE"] = "objects";
  config = NsmbNetplayPoC::Config::LoadStateSyncConfig(environment);
  CHECK(!config.GameApplyCriticalGlobals);
  CHECK(config.GameApplyStarObjects);
  CHECK(config.GameApplyStageObjects);
  CHECK(config.GameApplyPlayerActors);

  environment.Values["MELONDS_NSML_STATE_APPLY_MODE"] = "remote-player";
  config = NsmbNetplayPoC::Config::LoadStateSyncConfig(environment);
  CHECK(!config.GameApplyCriticalGlobals);
  CHECK(!config.GameApplyStarObjects);
  CHECK(!config.GameApplyStageObjects);
  CHECK(config.GameApplyPlayerActors);
  CHECK(config.GameApplyRemotePlayerOnly);

  environment.Values["MELONDS_NSML_STATE_APPLY_MODE"] = "unknown";
  config = NsmbNetplayPoC::Config::LoadStateSyncConfig(environment);
  CHECK(config.GameApplyCriticalGlobals);
  CHECK(config.GameApplyStarObjects);
  CHECK(config.GameApplyStageObjects);
  CHECK(config.GameApplyPlayerActors);
  CHECK(!config.GameApplyRemotePlayerOnly);
}

void TestStateSyncConfigReadsClampsAndSkipPriorities() {
  MapEnvironment environment;
  environment.Values = {
      {"MELONDS_NSML_STATE_SYNC", "1"},
      {"MELONDS_NSML_STATE_SYNC_EXTENDED", "1"},
      {"MELONDS_NSML_STATE_APPLY", "1"},
      {"MELONDS_NSML_STATE_SYNC_INTERVAL", "0"},
      {"MELONDS_NSML_PLAYER_STATE_SYNC", "1"},
      {"MELONDS_NSML_PLAYER_STATE_APPLY", "1"},
      {"MELONDS_NSML_PLAYER_STATE_GLOBALS", "1"},
      {"MELONDS_NSML_PLAYER_STATE_SYNC_INTERVAL", "-4"},
      {"MELONDS_NSML_PLAYER_STATE_MAX_PREDICT_FRAMES", "-1"},
      {"MELONDS_NSML_WORLD_STATE_SYNC", "1"},
      {"MELONDS_NSML_WORLD_STATE_APPLY", "1"},
      {"MELONDS_NSML_WORLD_STATE_SKIP_STAR", "1"},
      {"MELONDS_NSML_WORLD_STATE_SPAWN_ITEM", "1"},
      {"MELONDS_NSML_WORLD_STATE_APPLY_MOVING_HAZARD", "1"},
      {"MELONDS_NSML_WORLD_STATE_SKIP_MOVING_HAZARD", "1"},
      {"MELONDS_NSML_WORLD_STATE_APPLY_EFFECTS", "1"},
      {"MELONDS_NSML_WORLD_STATE_SKIP_EFFECTS", "1"},
      {"MELONDS_NSML_WORLD_STATE_APPLY_ACTOR_SNAPSHOT", "1"},
      {"MELONDS_NSML_WORLD_STATE_TRACE_MOVING_HAZARDS", "1"},
      {"MELONDS_NSML_WORLD_STATE_TRACE_OBJECT_LIFECYCLES", "1"},
      {"MELONDS_NSML_WORLD_STATE_TRACE_ACTOR_INTERNALS", "1"},
      {"MELONDS_NSML_WORLD_STATE_TRACE_EFFECTS", "1"},
      {"MELONDS_NSML_WORLD_STATE_TRACE_OBJECT_LIFECYCLES_INTERVAL", "0"},
      {"MELONDS_NSML_WORLD_STATE_TRACE_OBJECT_LIFECYCLES_START_FRAME", "-1"},
      {"MELONDS_NSML_WORLD_STATE_TRACE_OBJECT_LIFECYCLES_END_FRAME", "-2"},
      {"MELONDS_NSML_WORLD_STATE_SYNC_INTERVAL", "0"},
      {"MELONDS_NSML_WORLD_STATE_MAX_PREDICT_FRAMES", "-3"},
      {"MELONDS_NSML_WORLD_STATE_ACTOR_RESCAN_INTERVAL", "-4"},
  };

  const auto config = NsmbNetplayPoC::Config::LoadStateSyncConfig(environment);
  CHECK(config.GameEnabled);
  CHECK(config.GameExtended);
  CHECK(config.GameApplyEnabled);
  CHECK(config.GameInterval == 1);
  CHECK(config.PlayerEnabled);
  CHECK(config.PlayerApplyEnabled);
  CHECK(config.PlayerGlobalsEnabled);
  CHECK(config.PlayerInterval == 1);
  CHECK(config.PlayerMaxPredictFrames == 0);
  CHECK(config.WorldEnabled);
  CHECK(config.WorldApplyEnabled);
  CHECK(!config.WorldApplyStarActor);
  CHECK(config.WorldSpawnItem);
  CHECK(!config.WorldApplyMovingHazard);
  CHECK(!config.WorldApplyEffects);
  CHECK(config.WorldApplyActorSnapshot);
  CHECK(config.WorldTraceMovingHazards);
  CHECK(config.WorldTraceObjectLifecycles);
  CHECK(config.WorldTraceActorInternals);
  CHECK(config.WorldTraceEffects);
  CHECK(config.WorldTraceObjectLifecyclesInterval == 1);
  CHECK(config.WorldTraceObjectLifecyclesStartFrame == 0u);
  CHECK(config.WorldTraceObjectLifecyclesEndFrame == 0u);
  CHECK(config.WorldInterval == 1);
  CHECK(config.WorldMaxPredictFrames == 0);
  CHECK(config.WorldActorRescanInterval == 0);
}

} // namespace

int main() {
  TestFlagsPreserveExistingSemantics();
  TestStringsPreserveFallbackSemantics();
  TestIntegerParsingPreservesBaseAndFallback();
  TestDoubleParsingPreservesFallback();
  TestUnsignedParsingPreservesExistingInvalidValueBehavior();
  TestUnsignedListParsingPreservesLegacySemantics();
  TestBootstrapConfigDefaults();
  TestBootstrapConfigReadsAndClampsEnvironment();
  TestConnectionConfigDefaultsAndRoleFallback();
  TestConnectionConfigReadsExistingValuesAndClamps();
  TestInputConfigDefaultsPreserveLegacyInitializationOrder();
  TestInputConfigReadsClampsAndNormalizesRanges();
  TestRollbackConfigDefaultsAndBackendAliases();
  TestRollbackConfigReadsClampsAndDependencies();
  TestMvlConfigDefaults();
  TestMvlConfigReadsClampsAndPreservesPriority();
  TestStateSyncConfigDefaultsAndApplyModes();
  TestStateSyncConfigReadsClampsAndSkipPriorities();

  if (Failures != 0) {
    std::fprintf(stderr, "nsmb netplay config tests failed: %d\n", Failures);
    return 1;
  }

  std::printf("nsmb netplay config tests passed\n");
  return 0;
}
