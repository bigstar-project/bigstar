#pragma once

#include "NsmbGameState.h"
#include "NsmbNetplayConfig.h"

#include <chrono>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace NsmbNetplayPoC::Diagnostics {

constexpr std::size_t kDiagnosticRingCapacity = 720;

struct DiagnosticPlayerSnapshot {
  melonDS::u32 Found = 0;
  melonDS::u32 Base = 0;
  melonDS::u32 GUID = 0;
  melonDS::u32 Settings = 0;
  melonDS::u32 StateType = 0;
  melonDS::u32 Flags = 0;
  melonDS::u32 PosX = 0;
  melonDS::u32 PosY = 0;
  melonDS::u32 PosZ = 0;
  melonDS::u32 PrevX = 0;
  melonDS::u32 PrevY = 0;
  melonDS::u32 PrevZ = 0;
  melonDS::u32 VelX = 0;
  melonDS::u32 VelY = 0;
  melonDS::u32 VelZ = 0;
  melonDS::u32 ActionFlag = 0;
  melonDS::u32 SubActionFlag = 0;
  melonDS::u32 PhysicsFlag = 0;
  melonDS::u32 DamageCooldown = 0;
  melonDS::u32 TransitionFlag = 0;
  melonDS::u32 CollisionFlag = 0;
  melonDS::u32 EnvironmentFlag = 0;
  melonDS::u32 LinkedActor = 0;
  melonDS::u32 TransitionStep = 0;
  melonDS::u32 UpdateLocked = 0;
  melonDS::u32 CharacterIDBase = 0;
  melonDS::u32 TransitioningFlag = 0;
  melonDS::u32 CameraFocusMode = 0;
  melonDS::u32 DefeatedFlag = 0;
  melonDS::u32 PlayerBaseID = 0;
  melonDS::u32 VisibleFlag = 0;
  melonDS::u32 TransitFunc = 0;
  melonDS::u32 TransitArg = 0;
  melonDS::u32 Powerup = 0;
  melonDS::u32 InventoryPowerup = 0;
  melonDS::u32 Dead = 0;
  melonDS::u32 Character = 0;
  melonDS::u32 TransitionStatus = 0;
  melonDS::u32 Lives = 0;
  melonDS::u32 BattleStars = 0;
  melonDS::u32 Coins = 0;
  melonDS::u32 Score = 0;
  melonDS::u32 DisplayedStars = 0;
  melonDS::u32 Deaths = 0;
  melonDS::u32 CollectedStars = 0;
};

struct DiagnosticFrameSnapshot {
  bool Valid = false;
  melonDS::u32 Frame = 0;
  melonDS::u32 Instance = 0;
  melonDS::u32 StageID = 0;
  melonDS::u32 StageGroup = 0;
  melonDS::u32 VsMode = 0;
  melonDS::u32 LocalPlayerID = 0;
  melonDS::u32 SceneCurrentSceneID = 0;
  melonDS::u32 SceneNextSceneID = 0;
  melonDS::u32 StageActorFreezeFlag = 0;
  melonDS::u32 PlayerCount = 0;
  melonDS::u32 InputConsole0Held = 0;
  melonDS::u32 InputConsole1Held = 0;
  melonDS::u32 InputPlayer0Held = 0;
  melonDS::u32 InputPlayer1Held = 0;
  melonDS::u32 LastSentInputFrame = 0;
  melonDS::u32 LastReceivedInputFrame = 0;
  melonDS::u64 PlayerGlobalHash = 0;
  melonDS::u64 PlayerGlobalHash0 = 0;
  melonDS::u64 PlayerGlobalHash1 = 0;
  melonDS::u64 PlayerActorHash0 = 0;
  melonDS::u64 PlayerActorHash1 = 0;
  melonDS::u32 StageCameraGlobalX0 = 0;
  melonDS::u32 StageCameraGlobalX1 = 0;
  melonDS::u32 StageCameraGlobalY0 = 0;
  melonDS::u32 StageCameraGlobalY1 = 0;
  melonDS::u32 StageCameraGlobalWidth0 = 0;
  melonDS::u32 StageCameraGlobalWidth1 = 0;
  melonDS::u32 StageCameraGlobalHeight0 = 0;
  melonDS::u32 StageCameraGlobalHeight1 = 0;
  DiagnosticPlayerSnapshot Player[2];
};

struct PlayerLifeState {
  melonDS::u32 Lives[2]{};
  melonDS::u32 Deaths[2]{};
  melonDS::u32 Dead[2]{};
  melonDS::u32 Transition[2]{};
};

struct PlayerLifeObservation {
  bool Accepted = false;
  bool Changed = false;
  bool HadPrevious = false;
  PlayerLifeState Previous;
};

enum class RuntimePatchLogKind : std::size_t {
  ForceDeathCounters,
  ForcePowerups,
  ForceInventoryPowerups,
  ForceStarCounters,
  ScriptRemotePacket,
  Count,
};

class Runtime {
public:
  using TimePoint = std::chrono::steady_clock::time_point;

  struct ActiveFrameSample {
    bool Recorded = false;
    bool Spike = false;
    std::uint64_t ElapsedUs = 0;
    melonDS::u32 RollbackRestoreDelta = 0;
    melonDS::u32 RollbackResimulateDelta = 0;
  };

  struct ActiveFrameSummary {
    bool Started = false;
    melonDS::u32 StartFrame = 0;
    melonDS::u32 Frames = 0;
    std::int64_t ElapsedMs = 0;
    melonDS::u32 Samples = 0;
    std::uint64_t TotalUs = 0;
    std::uint64_t MaxUs = 0;
    melonDS::u32 MaxFrame = 0;
    melonDS::u32 Over16ms = 0;
    melonDS::u32 Over25ms = 0;
    melonDS::u32 Over33ms = 0;
  };

  Runtime();
  ~Runtime();

  Runtime(const Runtime &) = delete;
  Runtime &operator=(const Runtime &) = delete;

  bool ConfigureFrameHeartbeat(int interval, const std::string &path);
  bool PublishFrameHeartbeat(int instanceID, melonDS::u32 frame, bool active);
  bool ConfigureHashLog(const std::string &path, bool screenHashEnabled);
  bool RecordFrameHash(int instanceID, melonDS::u32 frame,
                       melonDS::u64 stateHash, melonDS::u64 screenHash);
  bool WriteDiagnosticEvent(const std::string &path,
                            const std::string &json);
  void StartTestTimer(TimePoint now);
  std::int64_t TestElapsedMs(TimePoint now) const;
  bool StartActiveTimer(int instanceID, melonDS::u32 frame, TimePoint now);
  bool IsActiveTimerStarted(int instanceID) const;
  ActiveFrameSample RecordActiveFrameTiming(
      int instanceID, melonDS::u32 frame, TimePoint now, bool traceSpikes,
      std::uint64_t spikeThresholdUs, melonDS::u32 rollbackRestoreCount,
      melonDS::u32 rollbackResimulateCount);
  ActiveFrameSummary ActiveFrameTimingSummary(int instanceID,
                                              melonDS::u32 endFrame,
                                              TimePoint now) const;
  bool ShouldTraceGameplayHeartbeat(int instanceID, melonDS::u32 frame,
                                    melonDS::u32 startFrame, int interval);
  std::optional<DiagnosticFrameSnapshot>
  LatestDiagnosticSnapshot(int instanceID) const;
  void RecordDiagnosticSnapshot(int instanceID,
                                const DiagnosticFrameSnapshot &snapshot);
  std::vector<DiagnosticFrameSnapshot>
  DiagnosticSnapshotWindow(int instanceID, std::size_t frameCount) const;
  void ScheduleDiagnosticPostTrigger(int instanceID,
                                     melonDS::u32 untilFrame);
  std::optional<melonDS::u32>
  TakeDueDiagnosticPostTrigger(int instanceID, melonDS::u32 frame);
  bool ShouldEmitDiagnosticMismatch(int instanceID, melonDS::u32 frame,
                                    melonDS::u32 cooldownFrames);
  bool ShouldEmitDiagnosticLifeEvent(int instanceID, int player,
                                     melonDS::u32 frame,
                                     bool transitionOnly,
                                     melonDS::u32 cooldownFrames);
  bool ShouldEmitDiagnosticPitTransition(int instanceID, int player,
                                         melonDS::u32 frame,
                                         melonDS::u32 cooldownFrames);
  bool ShouldEmitDiagnosticPositionAnomaly(int instanceID, int player,
                                           melonDS::u32 frame,
                                           melonDS::u32 cooldownFrames);
  PlayerLifeObservation ObservePlayerLifeState(int instanceID,
                                               const PlayerLifeState &current);
  bool TakeRuntimePatchLog(int instanceID, RuntimePatchLogKind kind);
  void ResetRuntimePatchLog(int instanceID, RuntimePatchLogKind kind);
  void StartHangDiagnostics(const Config::DiagnosticsConfig &config, bool host);
  void Stop();

  void TracePhase(const char *event, const char *phase, int instanceID = -1,
                  melonDS::u32 frame = 0, melonDS::u32 logicalFrame = 0,
                  melonDS::u32 sendFrame = 0);
  void UpdateNetplaySnapshot(melonDS::u32 lastSentFrame,
                             melonDS::u32 lastReceivedFrame,
                             melonDS::u32 frameForLead,
                             melonDS::u32 noFrameLimit, std::size_t localQueue,
                             std::size_t remoteQueue, std::size_t delayedQueue,
                             int peerState, int connectingPeerState);
  void ResetNetplaySnapshot(melonDS::u32 noFrameLimit);

  void RecordENetService(int result);
  void RecordENetEvent(int type, melonDS::u32 data);
  void RecordENetReceive(std::uint64_t unixMs);
  void RecordENetSend(int result, std::size_t bytes, std::uint64_t unixMs);

  void BeginRemoteWait(melonDS::u32 targetFrame, std::uint64_t unixMs);
  void ProgressRemoteWait(std::uint64_t unixMs);
  void EndRemoteWait();

  void UpdateGameSnapshot(int instanceID, melonDS::u32 frame,
                          const GameStateModel::GameStateSample &sample,
                          std::uint64_t unixMs);

private:
  struct Impl;
  std::unique_ptr<Impl> State;
};

} // namespace NsmbNetplayPoC::Diagnostics
