#include "NsmbGameplayDiagnostics.h"

#include "NsmbGameStateReader.h"
#include "NsmbRollbackRuntime.h"

#include "NDS.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace NsmbNetplayPoC::GameplayDiagnostics {
namespace {

constexpr std::size_t kTrackedWorldMovingHazardCount = 4;
constexpr melonDS::u32 kNoFrameLimit = 0;
constexpr melonDS::u32 kDiagnosticPostTriggerFrames = 120;
constexpr melonDS::u32 kDiagnosticRepeatedAnomalyFrames = 120;
constexpr melonDS::u32 kPlayerPitDeathTransitStateAddr = 0x021196B0;
constexpr melonDS::u32 kGamePlayerDeadAddr = 0x0208B328;
constexpr melonDS::u32 kGamePlayerTransitionStatusAddr = 0x0208B354;
constexpr melonDS::u32 kGamePlayerLivesAddr = 0x0208B364;
constexpr melonDS::u32 kGamePlayerDeathsAddr = 0x0208B394;
constexpr melonDS::u16 kVsMovingHazardObjectID = 0x0053;
constexpr melonDS::u32 kVsMovingHazardSettings = 0x00000000;

const char *RoleName(const Context &context) {
  return context.Host ? "host" : "client";
}

std::string Hex64(melonDS::u64 value) {
  std::ostringstream out;
  out << std::hex << std::uppercase << std::setw(16) << std::setfill('0')
      << static_cast<unsigned long long>(value);
  return out.str();
}

void WriteDiagnosticsJson(const Context &context, const std::string &json) {
  if (context.Diagnostics.DiagnosticsPath.empty())
    return;

  const std::filesystem::path path(context.Diagnostics.DiagnosticsPath);
  const std::filesystem::path tmp = path.string() + ".tmp";
  std::error_code error;
  if (path.has_parent_path())
    std::filesystem::create_directories(path.parent_path(), error);

  {
    std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
    if (!file) {
      std::printf("NSMB PoC: failed to open diagnostics file: %s\n",
                  tmp.string().c_str());
      return;
    }
    file << json;
    file.flush();
    if (!file) {
      std::printf("NSMB PoC: failed to write diagnostics file: %s\n",
                  tmp.string().c_str());
      return;
    }
  }

  std::filesystem::remove(path, error);
  error.clear();
  std::filesystem::rename(tmp, path, error);
  if (error) {
    std::printf("NSMB PoC: failed to publish diagnostics file: %s error=%s\n",
                path.string().c_str(), error.message().c_str());
  }
}

void WriteGameStateMismatchDiagnostics(
    const Context &context, int instanceID, melonDS::u32 frame,
    const GameStateModel::GameStateSyncHashes &local,
    const GameStateModel::GameStateSyncHashes &remote) {
  const melonDS::u64 localHash = GameStateModel::CombinedGameStateHash(local);
  const melonDS::u64 remoteHash = GameStateModel::CombinedGameStateHash(remote);
  const bool basicMatches = local.Basic == remote.Basic;
  const bool playerGlobalMatches = local.PlayerGlobal == remote.PlayerGlobal;
  const bool wifiCandidateMatches = local.WifiCandidate == remote.WifiCandidate;
  const bool renderCandidateMatches =
      local.RenderCandidate == remote.RenderCandidate;

  std::ostringstream line;
  line << "NSMB PoC: game state mismatch inst=" << instanceID
       << " frame=" << frame << " local=" << Hex64(localHash)
       << " remote=" << Hex64(remoteHash) << " basic=" << (basicMatches ? 1 : 0)
       << " playerGlobal=" << (playerGlobalMatches ? 1 : 0)
       << " wifiCandidate=" << (wifiCandidateMatches ? 1 : 0)
       << " renderCandidate=" << (renderCandidateMatches ? 1 : 0);

  std::ostringstream json;
  json << "{\n"
       << "  \"game_state_mismatch\": {\n"
       << "    \"instance\": " << instanceID << ",\n"
       << "    \"frame\": " << frame << ",\n"
       << "    \"local_hash\": \"" << Hex64(localHash) << "\",\n"
       << "    \"remote_hash\": \"" << Hex64(remoteHash) << "\",\n"
       << "    \"basic_matches\": " << (basicMatches ? "true" : "false")
       << ",\n"
       << "    \"player_global_matches\": "
       << (playerGlobalMatches ? "true" : "false") << ",\n"
       << "    \"wifi_candidate_matches\": "
       << (wifiCandidateMatches ? "true" : "false") << ",\n"
       << "    \"render_candidate_matches\": "
       << (renderCandidateMatches ? "true" : "false") << ",\n"
       << "    \"line\": \"" << Diagnostics::JsonEscape(line.str()) << "\"\n"
       << "  }\n"
       << "}\n";
  WriteDiagnosticsJson(context, json.str());
}

void WriteDiagnosticEventLocked(Context context, const std::string &json) {
  if (!context.Diagnostics.DiagnosticEventsEnabled ||
      context.Diagnostics.DiagnosticEventsPath.empty()) {
    return;
  }
  if (!context.Runtime.WriteDiagnosticEvent(
          context.Diagnostics.DiagnosticEventsPath, json)) {
    context.Diagnostics.DiagnosticEventsEnabled = false;
  }
}

std::vector<Diagnostics::DiagnosticFrameSnapshot>
DiagnosticRingWindow(const Context &context, int instanceID) {
  if (instanceID < 0 || instanceID >= 16)
    return {};
  const std::size_t ringFrames = static_cast<std::size_t>(
      std::clamp(context.Diagnostics.DiagnosticRingFrames, 1,
                 static_cast<int>(Diagnostics::kDiagnosticRingCapacity)));
  return context.Runtime.DiagnosticSnapshotWindow(instanceID, ringFrames);
}

void EmitDiagnosticPitTransitionEvent(
    Context context, int instanceID,
    const Diagnostics::DiagnosticFrameSnapshot &snapshot,
    const Diagnostics::DiagnosticFrameSnapshot *previous, int player) {
  if (!context.Diagnostics.DiagnosticEventsEnabled || instanceID < 0 ||
      instanceID >= 16 || player < 0 || player > 1) {
    return;
  }
  if (snapshot.Player[player].TransitFunc != kPlayerPitDeathTransitStateAddr) {
    return;
  }
  if (previous && previous->Valid &&
      previous->Player[player].TransitFunc == kPlayerPitDeathTransitStateAddr) {
    return;
  }
  if (!context.Runtime.ShouldEmitDiagnosticPitTransition(
          instanceID, player, snapshot.Frame,
          kDiagnosticRepeatedAnomalyFrames)) {
    return;
  }

  context.Runtime.ScheduleDiagnosticPostTrigger(
      instanceID, snapshot.Frame + kDiagnosticPostTriggerFrames);
  const std::string json = Diagnostics::FormatDiagnosticPlayerSnapshotEvent(
      "player_pit_transition", RoleName(context), instanceID, snapshot,
      previous, player, DiagnosticRingWindow(context, instanceID));
  std::lock_guard<std::mutex> lock(context.Mutex);
  WriteDiagnosticEventLocked(context, json);
}

void EmitDiagnosticPositionAnomalyEvent(
    Context context, int instanceID,
    const Diagnostics::DiagnosticFrameSnapshot &snapshot,
    const Diagnostics::DiagnosticFrameSnapshot *previous, int player) {
  if (!context.Diagnostics.DiagnosticEventsEnabled || instanceID < 0 ||
      instanceID >= 16 || player < 0 || player > 1) {
    return;
  }
  if (!Diagnostics::IsPlayerScreenPositionAnomalous(snapshot, previous,
                                                    player)) {
    return;
  }
  if (!context.Runtime.ShouldEmitDiagnosticPositionAnomaly(
          instanceID, player, snapshot.Frame,
          kDiagnosticRepeatedAnomalyFrames)) {
    return;
  }

  const std::string json = Diagnostics::FormatDiagnosticPlayerSnapshotEvent(
      "player_position_anomaly", RoleName(context), instanceID, snapshot,
      previous, player, DiagnosticRingWindow(context, instanceID));
  std::lock_guard<std::mutex> lock(context.Mutex);
  WriteDiagnosticEventLocked(context, json);
}

void EmitGameStateMismatchEventLocked(
    Context context, int instanceID, melonDS::u32 frame,
    const GameStateModel::GameStateSyncHashes &local,
    const GameStateModel::GameStateSyncHashes &remote) {
  if (!context.Diagnostics.DiagnosticEventsEnabled || instanceID < 0 ||
      instanceID >= 16 || local.PlayerGlobal == remote.PlayerGlobal) {
    return;
  }
  if (!context.Runtime.ShouldEmitDiagnosticMismatch(instanceID, frame, 300))
    return;

  context.Runtime.ScheduleDiagnosticPostTrigger(
      instanceID, frame + kDiagnosticPostTriggerFrames);

  GameStateModel::GameStateSample remoteSample;
  const GameStateModel::GameStateSample *remoteSamplePtr = nullptr;
  if (const GameStateModel::GameStateSample *stored =
          context.GameSync.RemoteState.FindGameState(instanceID, frame)) {
    remoteSample = *stored;
    remoteSamplePtr = &remoteSample;
  }

  const std::optional<Diagnostics::DiagnosticFrameSnapshot> latestSnapshot =
      context.Runtime.LatestDiagnosticSnapshot(instanceID);
  const Diagnostics::DiagnosticFrameSnapshot *latest =
      latestSnapshot ? &latestSnapshot.value() : nullptr;
  WriteDiagnosticEventLocked(
      context, Diagnostics::FormatPlayerGlobalMismatchEvent(
                   RoleName(context), instanceID, frame, local, remote, latest,
                   remoteSamplePtr, DiagnosticRingWindow(context, instanceID)));
}

std::vector<Diagnostics::DiagnosticMovingHazardSnapshot>
ReadNearbyDiagnosticHazards(melonDS::NDS *nds) {
  const std::vector<GameStateReader::ObjectScanSample> actors =
      GameStateReader::FindActiveObjectsByIDAndSettings(
          nds, kVsMovingHazardObjectID, kVsMovingHazardSettings);
  const std::size_t count =
      std::min<std::size_t>(actors.size(), kTrackedWorldMovingHazardCount);
  std::vector<Diagnostics::DiagnosticMovingHazardSnapshot> hazards;
  hazards.reserve(count);
  for (std::size_t i = 0; i < count; i++) {
    Diagnostics::DiagnosticMovingHazardSnapshot hazard;
    hazard.GUID = actors[i].GUID;
    hazard.Base = actors[i].Base;
    hazard.PosX = actors[i].PosX;
    hazard.PosY = actors[i].PosY;
    hazard.VelX = actors[i].VelX;
    hazard.VelY = actors[i].VelY;
    hazard.StateType = actors[i].StateType;
    hazard.Flags = actors[i].Flags;
    hazards.push_back(hazard);
  }
  return hazards;
}

void EmitPlayerLifeEvent(Context context, int instanceID, melonDS::u32 frame,
                         int player, const char *reason,
                         const GameStateModel::GameStateSample &sample,
                         melonDS::NDS *nds) {
  if (!context.Diagnostics.DiagnosticEventsEnabled || instanceID < 0 ||
      instanceID >= 16 || player < 0 || player > 1) {
    return;
  }
  const bool transitionOnly =
      reason && std::strcmp(reason, "death-transition") == 0;
  if (!context.Runtime.ShouldEmitDiagnosticLifeEvent(instanceID, player, frame,
                                                     transitionOnly, 300)) {
    return;
  }

  if (!transitionOnly) {
    context.Runtime.ScheduleDiagnosticPostTrigger(
        instanceID, frame + kDiagnosticPostTriggerFrames);
  }

  const std::vector<Diagnostics::DiagnosticMovingHazardSnapshot> hazards =
      ReadNearbyDiagnosticHazards(nds);
  const std::vector<Diagnostics::DiagnosticFrameSnapshot> ring =
      transitionOnly ? std::vector<Diagnostics::DiagnosticFrameSnapshot>{}
                     : DiagnosticRingWindow(context, instanceID);
  const std::string json = Diagnostics::FormatPlayerLifeEvent(
      RoleName(context), reason, instanceID, frame, player, sample, hazards,
      !transitionOnly, ring);

  std::lock_guard<std::mutex> lock(context.Mutex);
  WriteDiagnosticEventLocked(context, json);
}

} // namespace

void StartHangWatchdog(Context context) {
  context.Runtime.StartHangDiagnostics(context.Diagnostics, context.Host);
}

void Stop(Context context) { context.Runtime.Stop(); }

void RecordActiveFrameTiming(Context context, int instanceID,
                             melonDS::u32 frame) {
  RollbackStorage::StatisticsSnapshot rollbackStats;
  if (context.Diagnostics.ActiveFrameSpikeTrace)
    rollbackStats = context.RollbackStats.Snapshot();
  const Diagnostics::Runtime::ActiveFrameSample sample =
      context.Runtime.RecordActiveFrameTiming(
          instanceID, frame, std::chrono::steady_clock::now(),
          context.Diagnostics.ActiveFrameSpikeTrace,
          static_cast<std::uint64_t>(
              context.Diagnostics.ActiveFrameSpikeThresholdUs),
          rollbackStats.RestoreCount, rollbackStats.ResimulateCount);
  if (!sample.Spike)
    return;

  std::printf(
      "NSMB PerfSpike: inst=%d frame=%u frameTimeUs=%llu thresholdUs=%d "
      "rollbackRestores=%u rollbackResims=%u rollbackRestoreDelta=%u "
      "rollbackResimDelta=%u saveMaxUs=%llu restoreMaxUs=%llu "
      "resimRunMaxUs=%llu resimSaveMaxUs=%llu resimTotalMaxUs=%llu\n",
      instanceID, frame, static_cast<unsigned long long>(sample.ElapsedUs),
      context.Diagnostics.ActiveFrameSpikeThresholdUs,
      rollbackStats.RestoreCount, rollbackStats.ResimulateCount,
      sample.RollbackRestoreDelta, sample.RollbackResimulateDelta,
      rollbackStats.CheckpointSaveMaxUs, rollbackStats.CheckpointRestoreMaxUs,
      rollbackStats.ResimRunFrameMaxUs, rollbackStats.ResimCheckpointSaveMaxUs,
      rollbackStats.ResimCorrectionMaxUs);
}

void ReportGameStateMismatchLocked(
    Context context, const GameStateModel::GameStateHashMismatch &mismatch) {
  const int instanceID = mismatch.InstanceID;
  const melonDS::u32 frame = mismatch.Frame;
  const GameStateModel::GameStateSyncHashes &local = mismatch.Local;
  const GameStateModel::GameStateSyncHashes &remote = mismatch.Remote;
  WriteGameStateMismatchDiagnostics(context, instanceID, frame, local, remote);
  EmitGameStateMismatchEventLocked(context, instanceID, frame, local, remote);
  std::printf("NSMB PoC: game state mismatch inst=%d frame=%u local=%016llX "
              "remote=%016llX basic=%d playerGlobal=%d wifiCandidate=%d "
              "renderCandidate=%d\n",
              instanceID, frame,
              static_cast<unsigned long long>(
                  GameStateModel::CombinedGameStateHash(local)),
              static_cast<unsigned long long>(
                  GameStateModel::CombinedGameStateHash(remote)),
              local.Basic == remote.Basic ? 1 : 0,
              local.PlayerGlobal == remote.PlayerGlobal ? 1 : 0,
              local.WifiCandidate == remote.WifiCandidate ? 1 : 0,
              local.RenderCandidate == remote.RenderCandidate ? 1 : 0);
  std::printf(
      "NSMB PoC: game state components local basic=%016llX "
      "playerGlobal=%016llX wifiCandidate=%016llX renderCandidate=%016llX\n",
      static_cast<unsigned long long>(local.Basic),
      static_cast<unsigned long long>(local.PlayerGlobal),
      static_cast<unsigned long long>(local.WifiCandidate),
      static_cast<unsigned long long>(local.RenderCandidate));
  std::printf(
      "NSMB PoC: game state components remote basic=%016llX "
      "playerGlobal=%016llX wifiCandidate=%016llX renderCandidate=%016llX\n",
      static_cast<unsigned long long>(remote.Basic),
      static_cast<unsigned long long>(remote.PlayerGlobal),
      static_cast<unsigned long long>(remote.WifiCandidate),
      static_cast<unsigned long long>(remote.RenderCandidate));
}

void EmitStartReadyEventLocked(Context context, const char *direction,
                               melonDS::u32 localFrame,
                               melonDS::u32 remoteFrame) {
  if (!context.Diagnostics.DiagnosticEventsEnabled)
    return;
  WriteDiagnosticEventLocked(
      context, Diagnostics::FormatStartReadyEvent(
                   RoleName(context), direction, localFrame, remoteFrame,
                   kNoFrameLimit, context.Connection.StartFrame,
                   context.InputRuntime.LastSentInputFrame,
                   context.InputRuntime.LastReceivedInputFrame,
                   context.InputRuntime.LocalInputs.size(),
                   context.InputRuntime.RemoteInputs.size(),
                   context.SessionRuntime.Delivery.PendingCount()));
}

void EmitStartupEvent(Context context) {
  if (!context.Diagnostics.DiagnosticEventsEnabled)
    return;
  WriteDiagnosticEventLocked(
      context,
      Diagnostics::FormatDiagnosticStartupEvent(
          RoleName(context), context.Diagnostics.DiagnosticRingFrames,
          context.StateSync.GameEnabled, context.StateSync.GameExtended,
          context.StateSync.GameInterval, context.Diagnostics.DiagnosticsPath,
          context.Diagnostics.DiagnosticEventsPath));
}

void RecordSnapshotIfNeeded(Context context, int instanceID, melonDS::u32 frame,
                            melonDS::NDS *nds) {
  if (!context.Diagnostics.DiagnosticEventsEnabled || !nds || !nds->MainRAM ||
      instanceID < 0 || instanceID >= 16 ||
      frame < context.Connection.StartFrame) {
    return;
  }

  Diagnostics::DiagnosticFrameSnapshot snapshot;
  snapshot.Valid = true;
  snapshot.Frame = frame;
  snapshot.Instance = static_cast<melonDS::u32>(instanceID);
  GameStateReader::ReadDiagnosticFrameSnapshot(nds, snapshot);
  snapshot.LastSentInputFrame = context.InputRuntime.LastSentInputFrame;
  snapshot.LastReceivedInputFrame = context.InputRuntime.LastReceivedInputFrame;
  GameStateReader::ReadDiagnosticPlayerSnapshot(
      instanceID, frame, nds, 0, context.GameSync, snapshot.Player[0]);
  GameStateReader::ReadDiagnosticPlayerSnapshot(
      instanceID, frame, nds, 1, context.GameSync, snapshot.Player[1]);
  if (snapshot.Player[0].Found && RollbackRuntime::IsValidMainRAMRange(
                                      nds, snapshot.Player[0].Base, 0xC00)) {
    snapshot.PlayerActorHash0 =
        GameStateReader::HashMainRAMRange(nds, snapshot.Player[0].Base, 0xC00);
  }
  if (snapshot.Player[1].Found && RollbackRuntime::IsValidMainRAMRange(
                                      nds, snapshot.Player[1].Base, 0xC00)) {
    snapshot.PlayerActorHash1 =
        GameStateReader::HashMainRAMRange(nds, snapshot.Player[1].Base, 0xC00);
  }

  const std::optional<Diagnostics::DiagnosticFrameSnapshot> previousSnapshot =
      context.Runtime.LatestDiagnosticSnapshot(instanceID);
  const Diagnostics::DiagnosticFrameSnapshot *previous =
      previousSnapshot ? &previousSnapshot.value() : nullptr;
  for (int player = 0; player < 2; player++) {
    EmitDiagnosticPositionAnomalyEvent(context, instanceID, snapshot, previous,
                                       player);
    EmitDiagnosticPitTransitionEvent(context, instanceID, snapshot, previous,
                                     player);
  }

  context.Runtime.RecordDiagnosticSnapshot(instanceID, snapshot);
  const std::optional<melonDS::u32> triggerFrame =
      context.Runtime.TakeDueDiagnosticPostTrigger(instanceID, frame);
  if (triggerFrame) {
    const std::string json = Diagnostics::FormatDiagnosticPostWindowEvent(
        RoleName(context), instanceID, frame, triggerFrame.value(),
        DiagnosticRingWindow(context, instanceID));
    std::lock_guard<std::mutex> lock(context.Mutex);
    WriteDiagnosticEventLocked(context, json);
  }
}

void TracePlayerLifeChanges(Context context, const Hooks &hooks, int instanceID,
                            melonDS::u32 frame, melonDS::NDS *nds) {
  if ((!context.RuntimePatch.TracePlayerLifeChanges &&
       !context.Diagnostics.DiagnosticEventsEnabled) ||
      !nds || !nds->MainRAM || instanceID < 0 || instanceID >= 16) {
    return;
  }

  Diagnostics::PlayerLifeState current;
  current.Lives[0] = nds->ARM9Read32(kGamePlayerLivesAddr);
  current.Lives[1] =
      nds->ARM9Read32(kGamePlayerLivesAddr + sizeof(melonDS::u32));
  current.Deaths[0] = nds->ARM9Read32(kGamePlayerDeathsAddr);
  current.Deaths[1] =
      nds->ARM9Read32(kGamePlayerDeathsAddr + sizeof(melonDS::u32));
  current.Dead[0] = nds->ARM9Read8(kGamePlayerDeadAddr);
  current.Dead[1] = nds->ARM9Read8(kGamePlayerDeadAddr + 1);
  current.Transition[0] = nds->ARM9Read32(kGamePlayerTransitionStatusAddr);
  current.Transition[1] =
      nds->ARM9Read32(kGamePlayerTransitionStatusAddr + sizeof(melonDS::u32));
  const Diagnostics::PlayerLifeObservation observation =
      context.Runtime.ObservePlayerLifeState(instanceID, current);
  if (!observation.Accepted || !observation.Changed)
    return;

  if (!hooks.IsGameplay(nds) && frame < 800)
    return;

  const GameStateModel::GameStateSample sample = hooks.ReadGameState(nds);
  if (context.RuntimePatch.TracePlayerLifeChanges) {
    std::printf(
        "NSMB LifeDelta: inst=%d frame=%u lives=%u/%u deaths=%u/%u dead=%u/%u "
        "trans=%u/%u cam={x=%08X/%08X y=%08X/%08X w=%08X/%08X h=%08X/%08X} "
        "p0={found=%u base=%08X pid11E=%u pid7B4=%u def=%u tring=%u updLock=%u "
        "vis=%u x=%08X y=%08X vel=%08X/%08X flags=%08X act=%08X sub=%08X "
        "phy=%08X transFlag=%08X coll=%08X env=%08X linked=%08X "
        "transitFunc=%08X transitArg=%08X} p1={found=%u base=%08X pid11E=%u "
        "pid7B4=%u def=%u tring=%u updLock=%u vis=%u x=%08X y=%08X "
        "vel=%08X/%08X flags=%08X act=%08X sub=%08X phy=%08X transFlag=%08X "
        "coll=%08X env=%08X linked=%08X transitFunc=%08X transitArg=%08X}\n",
        instanceID, frame, sample.Player0Lives, sample.Player1Lives,
        sample.Player0Deaths, sample.Player1Deaths, sample.Player0Dead,
        sample.Player1Dead, sample.PlayerTransitionStatus0,
        sample.PlayerTransitionStatus1, sample.StageCameraGlobalX0,
        sample.StageCameraGlobalX1, sample.StageCameraGlobalY0,
        sample.StageCameraGlobalY1, sample.StageCameraGlobalWidth0,
        sample.StageCameraGlobalWidth1, sample.StageCameraGlobalHeight0,
        sample.StageCameraGlobalHeight1, sample.PlayerActor0Found,
        sample.PlayerActor0Base, sample.PlayerActor0PlayerID,
        sample.PlayerActor0PlayerBaseID, sample.PlayerActor0DefeatedFlag,
        sample.PlayerActor0TransitioningFlag, sample.PlayerActor0UpdateLocked,
        sample.PlayerActor0VisibleFlag, sample.PlayerActor0PosX,
        sample.PlayerActor0PosY, sample.PlayerActor0VelX,
        sample.PlayerActor0VelY, sample.PlayerActor0Flags,
        sample.PlayerActor0ActionFlag, sample.PlayerActor0SubActionFlag,
        sample.PlayerActor0PhysicsFlag, sample.PlayerActor0TransitionFlag,
        sample.PlayerActor0CollisionFlag, sample.PlayerActor0EnvironmentFlag,
        sample.PlayerActor0LinkedActor, sample.PlayerActor0TransitFunc,
        sample.PlayerActor0TransitArg, sample.PlayerActor1Found,
        sample.PlayerActor1Base, sample.PlayerActor1PlayerID,
        sample.PlayerActor1PlayerBaseID, sample.PlayerActor1DefeatedFlag,
        sample.PlayerActor1TransitioningFlag, sample.PlayerActor1UpdateLocked,
        sample.PlayerActor1VisibleFlag, sample.PlayerActor1PosX,
        sample.PlayerActor1PosY, sample.PlayerActor1VelX,
        sample.PlayerActor1VelY, sample.PlayerActor1Flags,
        sample.PlayerActor1ActionFlag, sample.PlayerActor1SubActionFlag,
        sample.PlayerActor1PhysicsFlag, sample.PlayerActor1TransitionFlag,
        sample.PlayerActor1CollisionFlag, sample.PlayerActor1EnvironmentFlag,
        sample.PlayerActor1LinkedActor, sample.PlayerActor1TransitFunc,
        sample.PlayerActor1TransitArg);
  }

  if (!observation.HadPrevious)
    return;
  const Diagnostics::PlayerLifeState &previous = observation.Previous;
  const bool player0DeathEvent =
      (sample.PlayerActor0Found != 0 || current.Deaths[0] != 0 ||
       current.Dead[0] != 0) &&
      (current.Deaths[0] > previous.Deaths[0] ||
       current.Lives[0] < previous.Lives[0] ||
       (current.Dead[0] != previous.Dead[0] && current.Dead[0] != 0));
  const bool player1DeathEvent =
      (sample.PlayerActor1Found != 0 || current.Deaths[1] != 0 ||
       current.Dead[1] != 0) &&
      (current.Deaths[1] > previous.Deaths[1] ||
       current.Lives[1] < previous.Lives[1] ||
       (current.Dead[1] != previous.Dead[1] && current.Dead[1] != 0));
  const bool player0TransitionEvent =
      !player0DeathEvent && current.Transition[0] != previous.Transition[0] &&
      (sample.PlayerActor0DefeatedFlag != 0 ||
       sample.PlayerActor0TransitioningFlag != 0);
  const bool player1TransitionEvent =
      !player1DeathEvent && current.Transition[1] != previous.Transition[1] &&
      (sample.PlayerActor1DefeatedFlag != 0 ||
       sample.PlayerActor1TransitioningFlag != 0);
  if (player0DeathEvent) {
    EmitPlayerLifeEvent(context, instanceID, frame, 0, "death", sample, nds);
  } else if (player0TransitionEvent) {
    EmitPlayerLifeEvent(context, instanceID, frame, 0, "death-transition",
                        sample, nds);
  }
  if (player1DeathEvent) {
    EmitPlayerLifeEvent(context, instanceID, frame, 1, "death", sample, nds);
  } else if (player1TransitionEvent) {
    EmitPlayerLifeEvent(context, instanceID, frame, 1, "death-transition",
                        sample, nds);
  }
}

void TraceGameplayHeartbeatIfNeeded(Context context, int instanceID,
                                    melonDS::u32 frame, melonDS::NDS *nds) {
  if (!nds || !nds->MainRAM ||
      !context.Runtime.ShouldTraceGameplayHeartbeat(
          instanceID, frame, context.Connection.StartFrame,
          context.Diagnostics.GameplayHeartbeatInterval)) {
    return;
  }
  const GameStateReader::GameStateObjectScanCache cache =
      GameStateReader::BuildGameStateObjectScanCache(nds);
  const GameStateReader::ScopedGameStateObjectScanCache scopedCache(cache);
  const GameStateReader::PlayerActorScanSample players =
      GameStateReader::FindPlayerActors(nds);
  const GameStateReader::ObjectLifecycleSummary objects =
      GameStateReader::SummarizeObjectLifecycle(nds);
  std::printf("NSMB GameplayHeartbeat: role=%s inst=%d frame=%u "
              "p0=%u/%08X/%08X/%08X/%08X/%08X p1=%u/%08X/%08X/%08X/%08X/%08X "
              "objects=%u/%u/%u/%u/%u/%u",
              RoleName(context), instanceID, frame, players.Actor0.Found,
              players.Actor0.PosX, players.Actor0.PosY, players.Actor0.VelX,
              players.Actor0.VelY, players.Actor0.Flags, players.Actor1.Found,
              players.Actor1.PosX, players.Actor1.PosY, players.Actor1.VelX,
              players.Actor1.VelY, players.Actor1.Flags, objects.Total,
              objects.Active, objects.Dead, objects.NotCreated,
              objects.SkipUpdate, objects.SkipRender);
  std::printf(" activeIds=");
  for (std::size_t i = 0; i < GameStateModel::kObjectTraceSlots; i++) {
    if (i != 0)
      std::printf(",");
    std::printf("%03X:%08X", objects.ActiveID[i], objects.ActiveSettings[i]);
  }
  const std::vector<GameStateReader::ObjectScanSample> hazards =
      GameStateReader::FindActiveObjectsByIDAndSettings(
          nds, kVsMovingHazardObjectID, kVsMovingHazardSettings);
  std::printf(" hazards=");
  const std::size_t hazardCount =
      std::min(hazards.size(), kTrackedWorldMovingHazardCount);
  for (std::size_t i = 0; i < kTrackedWorldMovingHazardCount; i++) {
    if (i != 0)
      std::printf(",");
    if (i >= hazardCount) {
      std::printf("-");
      continue;
    }
    const GameStateReader::ObjectScanSample &hazard = hazards[i];
    std::printf("%u:%08X:%08X:%08X:%u:%08X", hazard.GUID, hazard.PosX,
                hazard.PosY, hazard.VelX, hazard.StateType, hazard.Flags);
  }
  std::printf("\n");
  std::fflush(stdout);
}

void CaptureScreenshotIfNeeded(Context context, int instanceID,
                               melonDS::u32 frame, melonDS::NDS *nds) {
  if (!nds ||
      !Diagnostics::ShouldCaptureScreenshotFrame(context.Diagnostics, frame)) {
    return;
  }

  void *topBuffer = nullptr;
  void *bottomBuffer = nullptr;
  Diagnostics::ScreenshotFrame screenshot;
  screenshot.FramebufferAvailable =
      nds->GPU.GetFramebuffers(&topBuffer, &bottomBuffer);
  screenshot.TopBuffer = topBuffer;
  screenshot.BottomBuffer = bottomBuffer;
  if (screenshot.FramebufferAvailable && topBuffer && bottomBuffer) {
    screenshot.DisplayControlA = nds->ARM9Read32(0x04000000);
    screenshot.DisplayControlB = nds->ARM9Read32(0x04001000);
    screenshot.DisplayStatus = nds->ARM9Read16(0x04000004);
    screenshot.PowerControl = nds->ARM9Read16(0x04000304);
    screenshot.BlendControlA = nds->ARM9Read16(0x04000050);
    screenshot.BlendY_A = nds->ARM9Read16(0x04000054);
    screenshot.BlendControlB = nds->ARM9Read16(0x04001050);
    screenshot.BlendY_B = nds->ARM9Read16(0x04001054);
    screenshot.NetState = nds->ARM9Read8(0x02088804);
    screenshot.NetFlags = nds->ARM9Read16(0x0208883C);
  }
  Diagnostics::CaptureScreenshot(context.Diagnostics, instanceID, frame,
                                 screenshot);
}

} // namespace NsmbNetplayPoC::GameplayDiagnostics
