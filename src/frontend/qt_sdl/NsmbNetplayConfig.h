#ifndef NSMBNETPLAYCONFIG_H
#define NSMBNETPLAYCONFIG_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace NsmbMvlNetplay::Config {

using FrameRange = std::pair<std::uint32_t, std::uint32_t>;

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
  // Immutable configured baseline. Runtime code must use the two fields below
  // so a peer-local restore frame never becomes a shared input coordinate.
  std::uint32_t StartFrame = 0;
  std::uint32_t LocalStartupRawFrame = 0;
  std::uint32_t SharedLogicalEpoch = 0;
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

struct RuntimePatchConfig {
  std::uint32_t PlayerStickToStarStartFrame = 0;
  std::uint32_t PlayerStickToStarEndFrame = 0;
  int PlayerStickToStarSlot = 0;
  bool ForcePlayerDeathCountersEnabled = false;
  bool ForcePlayerDeathCountersHostOnly = false;
  bool ForcePlayerDeathCountersClientOnly = false;
  std::uint32_t ForcePlayerDeathCountersStartFrame = 0;
  std::uint32_t ForcePlayerDeathCountersEndFrame = 0;
  std::uint32_t ForcePlayerDeathCounter0 = 0;
  std::uint32_t ForcePlayerDeathCounter1 = 0;
  bool ForcePlayerLivesEnabled = false;
  std::uint32_t ForcePlayerLife0 = 5;
  std::uint32_t ForcePlayerLife1 = 5;
  bool ForcePlayerPowerupsEnabled = false;
  std::uint32_t ForcePlayerPowerupsStartFrame = 0;
  std::uint32_t ForcePlayerPowerupsEndFrame = 0;
  std::uint32_t ForcePlayerPowerup0 = 0;
  std::uint32_t ForcePlayerPowerup1 = 0;
  bool ForcePlayerInventoryPowerupsEnabled = false;
  std::uint32_t ForcePlayerInventoryPowerupsStartFrame = 0;
  std::uint32_t ForcePlayerInventoryPowerupsEndFrame = 0;
  std::uint32_t ForcePlayerInventoryPowerup0 = 0;
  std::uint32_t ForcePlayerInventoryPowerup1 = 0;
  bool ForcePlayerStarCountersEnabled = false;
  std::uint32_t ForcePlayerStarCountersStartFrame = 0;
  std::uint32_t ForcePlayerStarCountersEndFrame = 0;
  std::uint32_t ForcePlayerBattleStars0 = 0;
  std::uint32_t ForcePlayerBattleStars1 = 0;
  std::uint32_t ForcePlayerDisplayedStars0 = 0;
  std::uint32_t ForcePlayerDisplayedStars1 = 0;
  std::uint32_t ForcePlayerCollectedStars0 = 0;
  std::uint32_t ForcePlayerCollectedStars1 = 0;
  bool TracePlayerLifeChanges = false;
  bool PacketBridgeJitHelperPatchEnabled = false;
  std::uint32_t PacketBridgeJitHelperPatchFrame = 0;
};

struct HarnessConfig {
  std::string InputScriptPath;
  bool FrameBarrierEnabled = false;
  bool SerialRunEnabled = false;
  int SeedWaitTimeoutMs = 10000;
  bool WaitForPeerBeforeStart = false;
  bool WaitForPeerAtNetplayStart = false;
  bool DeferNetworkUntilStart = false;
  bool NetplayFrameBarrierEnabled = false;
  bool NeutralizePolledInput = false;
  bool NeutralizePolledInputPreserveTouch = false;
  bool NetworkPumpThreadEnabled = false;
  int NetworkPumpSleepUs = 250;
  std::string StateSaveDir;
  std::uint32_t StateSaveFrame = 0;
  std::string StateLoadDir;
  std::uint32_t StateLoadFrame = 0;
  bool StateLoadFrameSet = false;
};

struct PacketBridgeConfig {
  bool Enabled = false;
  bool Only = false;
  bool AllowPreGame = false;
  bool TraceEnabled = false;
  int LocalPlayerOverride = -1;
  bool DirectCaptureEnabled = false;
  bool ForceTickEnabled = false;
  std::uint32_t ForceTickStartFrame = 0;
  int ForceTickBase = -1;
  int ForceGameLocalPlayerID = -1;
  std::uint32_t ForceGameLocalPlayerIDStartFrame = 0;
  bool ForceGameLocalPlayerIDEarly = false;
  int MaxFrameLead = -1;
  int ThrottleTimeoutMs = 5000;
  std::uint32_t ThrottleStartFrame = 0;
  int LocalInputDelay = 0;
  bool NeutralizeLocalInput = false;
  bool PreserveLocalTouch = false;
  int SendDelayFrames = 0;
  int SendJitterFrames = 0;
};

enum class RollbackBackend {
  Savestate,
  CoreLite,
  CoreSparse,
  CoreDelta,
  CoreFrameDelta,
  CorePreimage,
  TinyCorePreimage,
};

struct RollbackConfig {
  bool Enabled = false;
  bool Resimulate = false;
  bool SkipRenderDuringResim = false;
  bool SkipIntermediateResimCheckpoints = false;
  bool SkipJitReset = false;
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
  int ResimulateDelayFrames = 0;
  int MaxResimFrames = 0;
};

struct MvlCameraInitHoldConfig {
  bool Enabled = false;
  bool HostOnly = false;
  bool ClientOnly = false;
  std::uint32_t StartFrame = 840;
  std::uint32_t EndFrame = 0;
};

struct MvlNetRandomConfig {
  bool Enabled = false;
  bool Auto = false;
  std::uint32_t Frame = 0;
  std::uint32_t Value = 0;
};

struct MvlConfig {
  int Stage = 0;
  std::vector<int> StageSequence;
  std::uint32_t StageSceneSettings = 0x00B4FF00;
  std::string CourseMode = "fixed";
  std::string InvalidCourseMode;
  int TargetWins = 2;
  int BigStarTarget = 5;
  bool RuntimeConfigEnabled = false;
  std::uint32_t InitialLives = 3;
  std::uint32_t LifeModeSelector = 2;
  std::uint32_t BigStarSelector = 1;
  bool AutoRestartAfterResult = false;
  std::uint32_t AutoRestartDelayFrames = 120;
  std::uint32_t AutoRestartBootstrapFrame = 120;
  MvlCameraInitHoldConfig CameraInitHold;
  MvlNetRandomConfig NetRandom;
  bool MatchSeedConfigured = false;
  std::uint32_t MatchSeed = 0;
  std::vector<std::uint32_t> MatchSeedSequence;
};

struct DiagnosticsConfig {
  bool HangDiagnosticsEnabled = false;
  int HangWatchdogIntervalMs = 1000;
  int HangThresholdMs = 8000;
  std::string HangWatchdogPath;
  std::string HangPhaseEventsPath;
  std::string HangDumpPath;
  std::uint32_t ActiveFpsStartFrame = 0;
  int ActiveFrameSpikeThresholdUs = 25000;
  bool ActiveFrameSpikeTrace = false;
  int FrameHeartbeatInterval = 0;
  int GameplayHeartbeatInterval = 0;
  std::string FrameHeartbeatPath;
  std::string InputRecordPath;
  std::uint32_t InputRecordStartFrame = 0;
  std::uint32_t InputRecordEndFrame = 0;
  int InputRecordInstance = -1;
  bool ScreenHashEnabled = false;
  std::string HashLogPath;
  std::string ScreenshotDir;
  int ScreenshotInterval = 0;
  bool ScreenshotRegisterTrace = false;
  std::string RamDumpDir;
  int RamDumpInterval = 0;
  std::string RamDumpFrames;
  std::string GameStateTracePath;
  std::string DiagnosticsPath;
  std::string DiagnosticEventsPath;
  bool DiagnosticEventsEnabled = false;
  int DiagnosticRingFrames = 360;
  int GameStateTraceInterval = 60;
  std::uint32_t GameStateTraceStartFrame = 0;
  std::uint32_t GameStateTraceEndFrame = 0;
  bool GameStateTraceExtended = false;
  std::string AIPlayLogPath;
  std::string AIObservationV2Path;
  std::string AIObservationV3Path;
  int AIPlayLogInterval = 1;
  int AIPlayLogFlushInterval = 60;
  std::uint32_t AIPlayLogStartFrame = 0;
  std::uint32_t AIPlayLogEndFrame = 0;
  int AIPlayLogMaxObjects = 32;
  int AIObservationV2StageFilter = -1;
  int AIObservationV3StageFilter = -1;
  bool AIPlayLogGameplayOnly = true;
};

struct RuleAISettings {
  bool Enabled = false;
  bool HostOnly = false;
  bool ClientOnly = false;
  std::string PlayerSpec = "remote";
  std::uint32_t StartFrame = 0;
  int HorizontalDeadzone = 0x4000;
  int HorizontalWrapWidth = 0x400000;
  int CloseRange = 0x22000;
  int HazardHorizontalRange = 0x40000;
  int HazardVerticalRange = 0x50000;
  int JumpInterval = 42;
  int JumpFrames = 9;
  bool TraceEnabled = false;
  int TraceInterval = 60;
};

struct ImitationAISettings {
  bool Enabled = false;
  bool HostOnly = false;
  bool ClientOnly = false;
  std::string PlayerSpec = "remote";
  std::uint32_t StartFrame = 0;
  double Threshold = 0.5;
  std::uint32_t AllowedHeldMask = 0x8F3;
  bool HazardGuardEnabled = true;
  int HazardGuardHorizontalRange = 0x40000;
  int HazardGuardVerticalRange = 0x50000;
  int HazardGuardCloseRange = 0x10000;
  bool TraceEnabled = false;
  int TraceInterval = 60;
  int InferInterval = 16;
  int NeutralHoldFrames = 8;
  bool WarnMissingFeatures = true;
  std::string ModelPath;
};

struct AIConfig {
  RuleAISettings Rule;
  ImitationAISettings Imitation;
};

struct StateSyncConfig {
  bool GameEnabled = false;
  bool GameExtended = false;
  bool GameApplyEnabled = false;
  bool GameApplyCriticalGlobals = true;
  bool GameApplyStarObjects = true;
  bool GameApplyStageObjects = true;
  bool GameApplyPlayerActors = true;
  bool GameApplyRemotePlayerOnly = false;
  int GameInterval = 60;
  bool WorldTraceMovingHazards = false;
  bool WorldTraceObjectLifecycles = false;
  bool WorldTraceActorInternals = false;
  bool WorldTraceEffects = false;
  int WorldTraceObjectLifecyclesInterval = 60;
  std::uint32_t WorldTraceObjectLifecyclesStartFrame = 0;
  std::uint32_t WorldTraceObjectLifecyclesEndFrame = 0;
};

bool ParseFlag(const char *value);
const char *ValueOr(const char *value, const char *fallback);
int ParseInt(const char *value, int fallback);
double ParseDouble(const char *value, double fallback);
std::uint32_t ParseU32(const char *value, std::uint32_t fallback);
std::vector<std::uint32_t> ParseU32List(const char *value);
bool ParseFrameRanges(const char *value, std::vector<FrameRange> &ranges);
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
std::vector<std::uint32_t> ReadU32List(const Environment &environment,
                                       const char *name);

BootstrapConfig LoadBootstrapConfig(const Environment &environment);
BootstrapConfig LoadBootstrapConfig();
ConnectionConfig LoadConnectionConfig(const Environment &environment,
                                      bool testEnabled);
ConnectionConfig LoadConnectionConfig(bool testEnabled);
InputConfig LoadInputConfig(const Environment &environment,
                            bool netplayOnlyForMaxFrameLeadDefault);
InputConfig LoadInputConfig(bool netplayOnlyForMaxFrameLeadDefault);
RuntimePatchConfig LoadRuntimePatchConfig(const Environment &environment);
RuntimePatchConfig LoadRuntimePatchConfig();
HarnessConfig LoadHarnessConfig(const Environment &environment);
HarnessConfig LoadHarnessConfig();
PacketBridgeConfig LoadPacketBridgeConfig(const Environment &environment);
PacketBridgeConfig LoadPacketBridgeConfig();
RollbackConfig LoadRollbackConfig(const Environment &environment);
RollbackConfig LoadRollbackConfig();
MvlConfig LoadMvlConfig(const Environment &environment);
MvlConfig LoadMvlConfig();
DiagnosticsConfig LoadDiagnosticsConfig(const Environment &environment,
                                        int diagnosticRingCapacity);
DiagnosticsConfig LoadDiagnosticsConfig(int diagnosticRingCapacity);
AIConfig LoadAIConfig(const Environment &environment);
AIConfig LoadAIConfig();
StateSyncConfig LoadStateSyncConfig(const Environment &environment);
StateSyncConfig LoadStateSyncConfig();

bool EnvFlag(const char *name);
const char *EnvCString(const char *name, const char *fallback);
int EnvInt(const char *name, int fallback);
double EnvDouble(const char *name, double fallback);
std::uint32_t EnvU32(const char *name, std::uint32_t fallback);
bool EnvHasValue(const char *name);

} // namespace NsmbMvlNetplay::Config

#endif
