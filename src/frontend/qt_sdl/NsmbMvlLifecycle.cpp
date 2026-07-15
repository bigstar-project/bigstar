#include "NsmbMvlLifecycle.h"

#include "NsmbMvlGameHooks.h"
#include "NsmbRollbackRuntime.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "NDS.h"
#include "Platform.h"
#include "Savestate.h"

namespace NsmbNetplayPoC::MvlLifecycle {
namespace {

constexpr melonDS::u32 kNoFrame = 0;
constexpr melonDS::u32 kGameStageIDAddr = 0x02085A14;
constexpr melonDS::u32 kGameStageGroupAddr = 0x02085A18;
constexpr melonDS::u32 kGameVsModeAddr = 0x02085A84;
constexpr melonDS::u32 kNetLocalAidAddr = 0x020887F0;
constexpr melonDS::u32 kGamePlayerDeadAddr = 0x0208B328;
constexpr melonDS::u32 kGamePlayerLivesAddr = 0x0208B364;
constexpr melonDS::u32 kGamePlayerBattleStarsAddr = 0x0208B36C;
constexpr melonDS::u32 kGamePlayerDisplayedStarsAddr = 0x0208B38C;
constexpr melonDS::u32 kGamePlayerDeathsAddr = 0x0208B394;
constexpr melonDS::u32 kGamePlayerCollectedStarsAddr = 0x0208B39C;
constexpr melonDS::u32 kSceneIsSceneActiveAddr = 0x0203BD28;
constexpr melonDS::u32 kScenePreviousSceneIDAddr = 0x0203BD2C;
constexpr melonDS::u32 kSceneNextSceneIDAddr = 0x0203BD30;
constexpr melonDS::u32 kSceneCurrentSceneIDAddr = 0x0203BD34;
constexpr melonDS::u32 kSceneNextSceneSettingsAddr = 0x02088F38;
constexpr melonDS::u16 kResultsScene = 0x000A;

MvlGameHooks::Context GameHooksContext(Context context) {
  return {
      context.Mvl,     context.RuntimePatch, context.DiagnosticsRuntime,
      context.Runtime, context.CurrentStage, context.CurrentStageSceneSettings,
      context.Host};
}

bool WriteARM9U32(melonDS::NDS *nds, melonDS::u32 address, melonDS::u32 value) {
  if (!nds || (address & 3) != 0)
    return false;
  nds->ARM9Write32(address, value);
  return true;
}

void ResetSyncStateForRestart(Context context, int instanceID,
                              melonDS::u32 frame) {
  if (instanceID < 0 || instanceID >= 16)
    return;
  std::lock_guard<std::mutex> lock(context.Mutex);
  context.GameSync.ResetForRestart(instanceID);
  context.PacketBridgeRuntime.ResetQueuesForRestart();
  context.Session.Delivery.Clear();
  context.Inputs.ResetForRestart(kNoFrame);
  context.DiagnosticsRuntime.ResetNetplaySnapshot(kNoFrame);
  context.Session.Handshake.ResetStartHandshake();
  context.GameStateTrace.ResetForRestart(instanceID);
  std::printf("NSMB MvL auto restart: reset sync caches inst=%d frame=%u "
              "cutoff=%u\n",
              instanceID, frame, RestartPacketCutoffFrame(context));
  std::fflush(stdout);
}

void RebaseStartupFrames(Context context, int instanceID,
                         melonDS::u32 restartFrame) {
  if (instanceID < 0 || instanceID >= 16)
    return;
  context.Runtime.RebaseStartupFrame(restartFrame,
                                     context.Connection.StartFrame);
  context.Runtime.RebaseStartupFrame(restartFrame,
                                     context.PacketBridge.ForceTickStartFrame);
  context.Runtime.RebaseStartupFrame(
      restartFrame, context.PacketBridge.ForceGameLocalPlayerIDStartFrame);
  context.Runtime.RebaseStartupFrame(restartFrame,
                                     context.PacketBridge.ThrottleStartFrame);
  context.Runtime.RebaseStartupFrame(restartFrame,
                                     context.Input.SendDelayStartFrame);
  context.Runtime.RebaseStartupFrame(restartFrame,
                                     context.Input.SendDelayEndFrame);
  context.Runtime.RebaseStartupFrame(
      restartFrame, context.RuntimePatch.PacketBridgeJitHelperPatchFrame);
  context.Runtime.RebaseStartupFrame(restartFrame,
                                     context.Mvl.CameraInitHold.StartFrame);
  context.Runtime.RebaseStartupFrame(restartFrame,
                                     context.Mvl.CameraInitHold.EndFrame);
  context.Runtime.SetStartupFrameBase(restartFrame);
  std::printf("NSMB MvL auto restart: rebased startup frames inst=%d "
              "restartFrame=%u netplayStart=%u packetJit=%u\n",
              instanceID, restartFrame, context.Connection.StartFrame,
              context.RuntimePatch.PacketBridgeJitHelperPatchFrame);
  std::fflush(stdout);
}

void RebaseStartupFramesFromCheckpoint(Context context, int instanceID,
                                       melonDS::u32 restoreFrame,
                                       melonDS::u32 checkpointFrame) {
  if (instanceID < 0 || instanceID >= 16)
    return;
  const auto rebase = [&](melonDS::u32 &target) {
    context.Runtime.RebaseStartupFrameFromCheckpoint(restoreFrame,
                                                     checkpointFrame, target);
  };
  rebase(context.Connection.StartFrame);
  rebase(context.PacketBridge.ForceTickStartFrame);
  rebase(context.PacketBridge.ForceGameLocalPlayerIDStartFrame);
  rebase(context.PacketBridge.ThrottleStartFrame);
  rebase(context.Input.SendDelayStartFrame);
  rebase(context.Input.SendDelayEndFrame);
  rebase(context.RuntimePatch.PacketBridgeJitHelperPatchFrame);
  rebase(context.Mvl.CameraInitHold.StartFrame);
  rebase(context.Mvl.CameraInitHold.EndFrame);
  context.Runtime.SetStartupFrameBase(restoreFrame > checkpointFrame
                                          ? restoreFrame - checkpointFrame
                                          : restoreFrame);
  std::printf("NSMB MvL auto restart: rebased startup frames from checkpoint "
              "inst=%d restoreFrame=%u checkpointFrame=%u netplayStart=%u "
              "packetJit=%u\n",
              instanceID, restoreFrame, checkpointFrame,
              context.Connection.StartFrame,
              context.RuntimePatch.PacketBridgeJitHelperPatchFrame);
  std::fflush(stdout);
}

void ResetStartupHookState(Context context, int instanceID) {
  if (instanceID < 0 || instanceID >= 16)
    return;
  context.Runtime.ResetStartupHookState(instanceID);
  context.PacketBridgeRuntime.ResetStartupHookState(instanceID);
  context.Coordinator.ResetNetplayStartWait();
  context.Session.Handshake.ResetStartHandshake();
  context.Inputs.LastInputFrameLeadResendAt = {};
  context.Inputs.InputFrameLeadResendCount = 0;
  context.Coordinator.ResetNetplayLockstep(instanceID);
}

void ScheduleJitHelperPatchAfterRestore(Context context, int instanceID,
                                        melonDS::u32 restoreFrame,
                                        melonDS::u32 checkpointFrame) {
  if (instanceID < 0 || instanceID >= 16)
    return;
  const PacketBridge::JitHookRestoreResult result =
      context.PacketBridgeRuntime.ScheduleJitHookAfterRestore(
          instanceID, restoreFrame, checkpointFrame, context.RuntimePatch);
  if (result.Action == PacketBridge::JitHookRestoreAction::Disabled)
    return;
  if (result.Action == PacketBridge::JitHookRestoreAction::KeepApplied) {
    std::printf("NSMB MvL auto restart: keeping packet bridge JIT helper patch "
                "inst=%d restoreFrame=%u checkpointFrame=%u patchFrame=%u\n",
                instanceID, restoreFrame, checkpointFrame,
                context.RuntimePatch.PacketBridgeJitHelperPatchFrame);
    std::fflush(stdout);
    return;
  }
  std::printf("NSMB MvL auto restart: scheduled packet bridge JIT helper patch "
              "inst=%d restoreFrame=%u checkpointFrame=%u patchFrame=%u "
              "resumeFrame=%u\n",
              instanceID, restoreFrame, checkpointFrame,
              context.RuntimePatch.PacketBridgeJitHelperPatchFrame,
              result.ResumeFrame);
  std::fflush(stdout);
}

bool RestoreBootstrapCheckpoint(Context context, int instanceID,
                                melonDS::u32 frame, melonDS::NDS *nds,
                                melonDS::u32 requestedSeed) {
  if (!nds || instanceID < 0 || instanceID >= 16)
    return false;
  MvlRuntime::InstanceState &restart = context.Runtime.Instances[instanceID];
  if (restart.BootstrapCheckpoint.Buffer.empty())
    return false;

  ResetStartupHookState(context, instanceID);
  RebaseStartupFramesFromCheckpoint(context, instanceID, frame,
                                    restart.BootstrapCheckpoint.Frame);
  melonDS::Savestate state(
      restart.BootstrapCheckpoint.Buffer.data(),
      static_cast<melonDS::u32>(restart.BootstrapCheckpoint.Buffer.size()),
      false);
  if (state.Error || !nds->DoSavestate(&state) || state.Error) {
    std::printf("NSMB MvL auto restart: failed to restore bootstrap checkpoint "
                "inst=%d frame=%u bytes=%zu\n",
                instanceID, frame, restart.BootstrapCheckpoint.Buffer.size());
    std::fflush(stdout);
    return false;
  }

  RollbackRuntime::InvalidateMainRAMJIT(context.Rollback, nds,
                                        nds->MainRAMMask + 1);
  melonDS::Platform::MP_Begin(nds->UserData);
  context.Runtime.Instances[instanceID].NetRandomPatchApplied = false;
  context.Mvl.NetRandom.Value = requestedSeed;
  context.Mvl.NetRandom.Enabled = true;
  context.Mvl.NetRandom.Auto = true;
  if (context.Host)
    WriteARM9U32(nds, kNetLocalAidAddr, 0);
  else if (context.Client)
    WriteARM9U32(nds, kNetLocalAidAddr, 1);
  MvlGameHooks::WriteRandomSeed(nds, requestedSeed);
  MvlGameHooks::ApplyRuntimeConfig(GameHooksContext(context), nds);
  std::printf("NSMB MvL auto restart: restored bootstrap checkpoint inst=%d "
              "frame=%u seed=0x%08X bytes=%zu\n",
              instanceID, frame, requestedSeed,
              restart.BootstrapCheckpoint.Buffer.size());
  std::fflush(stdout);
  return true;
}

bool ResetConsoleForNextMatch(Context context, int instanceID,
                              melonDS::u32 frame, melonDS::NDS *nds,
                              melonDS::u32 requestedSeed) {
  if (!nds || instanceID < 0 || instanceID >= 16 || !nds->GetNDSCart())
    return false;
  ResetStartupHookState(context, instanceID);
  RebaseStartupFrames(context, instanceID, frame);
  nds->Reset();
  MvlGameHooks::ApplyRuntimeConfig(GameHooksContext(context), nds);
  MvlGameHooks::WriteRandomSeed(nds, requestedSeed);
  nds->SetupDirectBoot(std::string{});
  nds->Start();
  melonDS::Platform::MP_Begin(nds->UserData);
  MvlGameHooks::ApplyRuntimeConfig(GameHooksContext(context), nds);
  MvlGameHooks::WriteRandomSeed(nds, requestedSeed);
  context.Mvl.NetRandom.Enabled = true;
  context.Mvl.NetRandom.Auto = true;
  context.Runtime.Instances[instanceID].NetRandomPatchApplied = false;
  context.Mvl.NetRandom.Value = requestedSeed;
  std::printf(
      "NSMB MvL auto restart: hard reset console for next match inst=%d "
      "frame=%u seed=0x%08X\n",
      instanceID, frame, requestedSeed);
  std::fflush(stdout);
  return true;
}

MvlRuntime::ResultSnapshot ReadResultSnapshot(Context context,
                                              melonDS::NDS *nds) {
  MvlRuntime::ResultSnapshot result{};
  if (!nds)
    return result;
  for (melonDS::u32 player = 0; player < 2; player++) {
    const melonDS::u32 offset = sizeof(melonDS::u32) * player;
    result.BattleStars[player] =
        nds->ARM9Read32(kGamePlayerBattleStarsAddr + offset);
    result.DisplayedStars[player] =
        nds->ARM9Read32(kGamePlayerDisplayedStarsAddr + offset);
    result.CollectedStars[player] =
        nds->ARM9Read32(kGamePlayerCollectedStarsAddr + offset);
    result.Lives[player] = nds->ARM9Read32(kGamePlayerLivesAddr + offset);
    result.Deaths[player] = nds->ARM9Read32(kGamePlayerDeathsAddr + offset);
    result.Dead[player] = nds->ARM9Read8(kGamePlayerDeadAddr + player);
    if (context.Mvl.LifeModeSelector != 2 && result.Dead[player] != 0) {
      result.Lives[player] = 0;
      result.Deaths[player] =
          std::max(result.Deaths[player], context.Mvl.InitialLives);
    }
  }
  return result;
}

} // namespace

melonDS::u32 ComposeSceneSettingsForStage(int stage) {
  const melonDS::u32 clamped =
      static_cast<melonDS::u32>(std::clamp(stage, 0, 4));
  return ((0xB4u + clamped) << 16) | 0xFF00u;
}

int StageForGame(Context context, int instanceID) {
  return context.Runtime.StageForGame(instanceID, context.Mvl,
                                      context.CurrentStage);
}

void RefreshGameSelection(Context context, int instanceID) {
  if (instanceID < 0 || instanceID >= 16)
    return;
  context.CurrentStage = std::clamp(StageForGame(context, instanceID), 0, 4);
  context.CurrentStageSceneSettings =
      ComposeSceneSettingsForStage(context.CurrentStage);
}

melonDS::u32 MatchSeedForGame(Context context, int instanceID) {
  return context.Runtime.MatchSeedForGame(instanceID, context.Mvl);
}

melonDS::u32 RestartPacketCutoffFrame(Context context) {
  return context.Runtime.RestartPacketCutoffFrame();
}

void SaveBootstrapCheckpointIfNeeded(Context context, int instanceID,
                                     melonDS::u32 frame, melonDS::NDS *nds) {
  if (!context.Mvl.AutoRestartAfterResult || context.Mvl.TargetWins <= 1 ||
      !nds || instanceID < 0 || instanceID >= 16)
    return;
  MvlRuntime::InstanceState &restart = context.Runtime.Instances[instanceID];
  if (!restart.BootstrapCheckpoint.Buffer.empty() ||
      restart.RestartCount != 0 || restart.InResult)
    return;
  const melonDS::u32 stageGroup = nds->ARM9Read32(kGameStageGroupAddr);
  const melonDS::u16 currentScene = nds->ARM9Read16(kSceneCurrentSceneIDAddr);
  const melonDS::u16 nextScene = nds->ARM9Read16(kSceneNextSceneIDAddr);
  const bool ready = frame >= context.Mvl.AutoRestartBootstrapFrame &&
                     stageGroup != 9 && currentScene == 0x0004 &&
                     nextScene == 0x0006 &&
                     nds->ARM9Read16(kSceneIsSceneActiveAddr) != 0;
  if (!ready)
    return;

  melonDS::Savestate state;
  if (state.Error || !nds->DoSavestate(&state) || state.Error) {
    if (!restart.BootstrapCheckpoint.Logged) {
      std::printf("NSMB MvL auto restart: failed to save bootstrap checkpoint "
                  "inst=%d frame=%u\n",
                  instanceID, frame);
      std::fflush(stdout);
      restart.BootstrapCheckpoint.Logged = true;
    }
    return;
  }
  restart.BootstrapCheckpoint.Buffer.assign(
      reinterpret_cast<const char *>(state.Buffer()),
      reinterpret_cast<const char *>(state.Buffer()) + state.Length());
  restart.BootstrapCheckpoint.Frame = frame;
  restart.BootstrapCheckpoint.Logged = true;
  std::printf("NSMB MvL auto restart: saved bootstrap checkpoint inst=%d "
              "frame=%u bytes=%u scene=%04X stageGroup=%u\n",
              instanceID, frame, state.Length(), currentScene, stageGroup);
  std::fflush(stdout);
}

bool RestartAfterResultIfNeeded(Context context, int instanceID,
                                melonDS::u32 frame, melonDS::NDS *nds) {
  if (!context.Mvl.AutoRestartAfterResult || context.Mvl.TargetWins < 1 ||
      !nds || instanceID < 0 || instanceID >= 16)
    return false;
  MvlRuntime::InstanceState &restart = context.Runtime.Instances[instanceID];
  const melonDS::u16 currentScene = nds->ARM9Read16(kSceneCurrentSceneIDAddr);
  if (currentScene != kResultsScene) {
    if (restart.RestartCount > 0 && currentScene == 0x0003 &&
        nds->ARM9Read16(kScenePreviousSceneIDAddr) == kResultsScene &&
        nds->ARM9Read16(kSceneNextSceneIDAddr) == 0x0003 &&
        frame - restart.LastRestartFrame >= 30)
      nds->ARM9Write16(kSceneNextSceneIDAddr, 0x0181);
    restart.InResult = false;
    restart.ResultScored = false;
    restart.ResultUnresolvedLogged = false;
    return false;
  }

  if (!restart.InResult) {
    restart.InResult = true;
    restart.ResultUnresolvedLogged = false;
    restart.ResultFrame = frame;
    return false;
  }
  if (!restart.ResultScored) {
    const MvlRuntime::ResultSnapshot result = ReadResultSnapshot(context, nds);
    const int winner = MvlRuntime::ResolveResultWinner(result);
    if (winner < 0) {
      if (!restart.ResultUnresolvedLogged &&
          frame - restart.ResultFrame >= context.Mvl.AutoRestartDelayFrames) {
        std::printf("NSMB MvL auto restart: result unresolved inst=%d frame=%u "
                    "stars=%u/%u displayed=%u/%u collected=%u/%u lives=%u/%u "
                    "deaths=%u/%u dead=%u/%u matchWins=%d/%d target=%d\n",
                    instanceID, frame, result.BattleStars[0],
                    result.BattleStars[1], result.DisplayedStars[0],
                    result.DisplayedStars[1], result.CollectedStars[0],
                    result.CollectedStars[1], result.Lives[0], result.Lives[1],
                    result.Deaths[0], result.Deaths[1], result.Dead[0],
                    result.Dead[1], restart.Wins[0], restart.Wins[1],
                    context.Mvl.TargetWins);
        std::fflush(stdout);
        restart.ResultUnresolvedLogged = true;
      }
      return false;
    }
    restart.Wins[winner]++;
    restart.ResultScored = true;
    std::printf("NSMB MvL auto restart: result inst=%d frame=%u winner=%d "
                "stars=%u/%u displayed=%u/%u collected=%u/%u lives=%u/%u "
                "deaths=%u/%u dead=%u/%u matchWins=%d/%d target=%d\n",
                instanceID, frame, winner, result.BattleStars[0],
                result.BattleStars[1], result.DisplayedStars[0],
                result.DisplayedStars[1], result.CollectedStars[0],
                result.CollectedStars[1], result.Lives[0], result.Lives[1],
                result.Deaths[0], result.Deaths[1], result.Dead[0],
                result.Dead[1], restart.Wins[0], restart.Wins[1],
                context.Mvl.TargetWins);
    std::fflush(stdout);
  }

  if (std::max(restart.Wins[0], restart.Wins[1]) >= context.Mvl.TargetWins ||
      frame - restart.ResultFrame < context.Mvl.AutoRestartDelayFrames)
    return false;

  const int nextRestartCount = restart.RestartCount + 1;
  restart.RestartCount = nextRestartCount;
  const int requestedStage =
      std::clamp(StageForGame(context, instanceID), 0, 4);
  const melonDS::u32 requestedSeed = MatchSeedForGame(context, instanceID);
  context.CurrentStage = requestedStage;
  context.CurrentStageSceneSettings =
      ComposeSceneSettingsForStage(requestedStage);
  MvlGameHooks::WriteRandomSeed(nds, requestedSeed);

  int restartPath = 0;
  if (RestoreBootstrapCheckpoint(context, instanceID, frame, nds,
                                 requestedSeed)) {
    restartPath = 3;
  } else if (!restart.GameplayCheckpoint.Buffer.empty() &&
             restart.GameplayCheckpoint.Stage == requestedStage) {
    melonDS::Savestate state(
        restart.GameplayCheckpoint.Buffer.data(),
        static_cast<melonDS::u32>(restart.GameplayCheckpoint.Buffer.size()),
        false);
    if (!state.Error && nds->DoSavestate(&state) && !state.Error) {
      restartPath = 1;
      RollbackRuntime::InvalidateMainRAMJIT(context.Rollback, nds,
                                            nds->MainRAMMask + 1);
      melonDS::Platform::MP_Begin(nds->UserData);
      ScheduleJitHelperPatchAfterRestore(context, instanceID, frame,
                                         restart.GameplayCheckpoint.Frame);
      WriteARM9U32(nds, kGameStageGroupAddr, 0x00000009);
      WriteARM9U32(nds, kGameVsModeAddr, 0x00000001);
      WriteARM9U32(nds, kSceneNextSceneSettingsAddr,
                   context.CurrentStageSceneSettings);
      MvlGameHooks::ApplyRuntimeConfig(GameHooksContext(context), nds);
      MvlGameHooks::WriteRandomSeed(nds, requestedSeed);
    }
  } else if (ResetConsoleForNextMatch(context, instanceID, frame, nds,
                                      requestedSeed)) {
    restartPath = 4;
  }

  if (restartPath == 0) {
    restart.RestartCount = nextRestartCount - 1;
    std::printf("NSMB MvL auto restart: failed inst=%d frame=%u nextGame=%d "
                "requestedStage=%d seed=0x%08X "
                "reason=no-compatible-checkpoint\n",
                instanceID, frame, nextRestartCount + 1, requestedStage,
                requestedSeed);
    std::fflush(stdout);
    return false;
  }
  restart.LastRestartFrame = frame;
  ResetSyncStateForRestart(context, instanceID, frame);
  restart.InResult = false;
  restart.ResultScored = false;
  restart.ResultUnresolvedLogged = false;
  const int actualStage = static_cast<int>(nds->ARM9Read32(kGameStageIDAddr));
  std::printf("NSMB MvL auto restart: inst=%d frame=%u nextGame=%d stage=%d "
              "requestedStage=%d seed=0x%08X matchWins=%d/%d target=%d "
              "checkpoint=%d\n",
              instanceID, frame, restart.RestartCount + 1, actualStage,
              requestedStage, requestedSeed, restart.Wins[0], restart.Wins[1],
              context.Mvl.TargetWins, restartPath);
  std::fflush(stdout);
  return true;
}

bool ShouldPauseInputForRestart(Context context, int instanceID,
                                melonDS::NDS *nds) {
  if (!context.Input.NetplayOnly || !context.Mvl.AutoRestartAfterResult ||
      context.Mvl.TargetWins <= 1 || !nds || instanceID < 0 ||
      instanceID >= 16 ||
      nds->ARM9Read16(kSceneCurrentSceneIDAddr) != kResultsScene)
    return false;
  const MvlRuntime::InstanceState &restart =
      context.Runtime.Instances[instanceID];
  return restart.InResult &&
         std::max(restart.Wins[0], restart.Wins[1]) < context.Mvl.TargetWins;
}

void SaveGameplayCheckpointIfNeeded(Context context, const Hooks &hooks,
                                    int instanceID, melonDS::u32 frame,
                                    melonDS::NDS *nds) {
  if (!context.Mvl.AutoRestartAfterResult || context.Mvl.TargetWins <= 1 ||
      !nds || !nds->MainRAM || instanceID < 0 || instanceID >= 16)
    return;
  MvlRuntime::InstanceState &restart = context.Runtime.Instances[instanceID];
  if (!restart.GameplayCheckpoint.Buffer.empty() || restart.InResult ||
      restart.RestartCount != 0 ||
      nds->ARM9Read16(kSceneCurrentSceneIDAddr) != 0x0003 ||
      nds->ARM9Read16(kSceneIsSceneActiveAddr) == 0 ||
      nds->ARM9Read32(kGameStageGroupAddr) != 9 ||
      nds->ARM9Read32(kGameVsModeAddr) != 1)
    return;

  const GameStateModel::GameStateSample sample = hooks.ReadGameStateSample(nds);
  if (!sample.PlayerActor0Found || !sample.PlayerActor1Found ||
      !sample.VsStarActorFound || !sample.StageSceneFound ||
      sample.StageSceneStateType != 1)
    return;

  melonDS::Savestate state;
  if (state.Error || !nds->DoSavestate(&state) || state.Error) {
    if (!restart.GameplayCheckpoint.Logged) {
      std::printf("NSMB MvL auto restart: failed to save checkpoint inst=%d "
                  "frame=%u\n",
                  instanceID, frame);
      std::fflush(stdout);
      restart.GameplayCheckpoint.Logged = true;
    }
    return;
  }
  restart.GameplayCheckpoint.Buffer.assign(
      reinterpret_cast<const char *>(state.Buffer()),
      reinterpret_cast<const char *>(state.Buffer()) + state.Length());
  restart.GameplayCheckpoint.Frame = frame;
  restart.GameplayCheckpoint.Stage = static_cast<int>(sample.StageID);
  restart.GameplayCheckpoint.Logged = true;
  std::printf("NSMB MvL auto restart: saved checkpoint inst=%d frame=%u "
              "bytes=%u stage=%u settings=0x%08X\n",
              instanceID, frame, state.Length(), sample.StageID,
              sample.StageSceneSettings);
  std::fflush(stdout);
}

} // namespace NsmbNetplayPoC::MvlLifecycle
