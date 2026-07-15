#include "NsmbMvlGameHooks.h"

#include "NsmbGameStateReader.h"
#include "NsmbGameStateWriter.h"
#include "NsmbMvlRuntime.h"
#include "NsmbNetplayDiagnostics.h"

#include "NDS.h"

#include <cstdio>
#include <cstring>

namespace NsmbNetplayPoC::MvlGameHooks {
namespace {

constexpr melonDS::u32 kMainRAMBase = 0x02000000;
constexpr melonDS::u32 kGameStageGroupAddr = 0x02085A18;
constexpr melonDS::u32 kGameVsModeAddr = 0x02085A84;
constexpr melonDS::u32 kNetGGIDAddr = 0x02088858;
constexpr melonDS::u32 kNetRandomCallCountAddr = 0x02088A48;
constexpr melonDS::u32 kNetRandomValueAddr = 0x02088A68;
constexpr melonDS::u32 kGameRandomCallCountAddr = 0x02085A54;
constexpr melonDS::u32 kGameRandomValueAddr = 0x02085A70;
constexpr melonDS::u16 kVsBattleStarActorObjectID = 0x0022;
constexpr melonDS::u32 kVsBattleStarActorSettings = 0x00000001;
constexpr melonDS::u32 kMvlRuntimeConfigAddr = 0x020C5360;
constexpr melonDS::u32 kMvlRuntimeConfigMagic = 0x434C564D; // "MVLC"
constexpr melonDS::u32 kMvlRuntimeConfigStageOffset = 0x04;
constexpr melonDS::u32 kMvlRuntimeConfigSceneSettingsOffset = 0x08;
constexpr melonDS::u32 kMvlRuntimeConfigInitialLivesOffset = 0x0C;
constexpr melonDS::u32 kMvlRuntimeConfigLifeModeSelectorOffset = 0x10;
constexpr melonDS::u32 kMvlRuntimeConfigBigStarSelectorOffset = 0x14;
constexpr melonDS::u32 kMvlCameraModeFlagsAddr = 0x020CA880;

bool IsMarioVsLuigiGameplay(melonDS::NDS *nds) {
  return nds && nds->ARM9Read32(kGameStageGroupAddr) == 9 &&
         nds->ARM9Read32(kGameVsModeAddr) == 1;
}

bool IsMarioVsLuigiGGID(melonDS::u32 value) {
  return value == 0x42 || value == 0x00400150;
}

bool IsValidInstance(int instanceID) {
  return instanceID >= 0 &&
         instanceID < static_cast<int>(MvlRuntime::kInstanceCount);
}

} // namespace

void ApplyRuntimeConfig(const Context &context, melonDS::NDS *nds) {
  if (!context.Mvl.RuntimeConfigEnabled || !nds)
    return;

  nds->ARM9Write32(kMvlRuntimeConfigAddr, kMvlRuntimeConfigMagic);
  nds->ARM9Write32(kMvlRuntimeConfigAddr + kMvlRuntimeConfigStageOffset,
                   static_cast<melonDS::u32>(context.CurrentStage));
  nds->ARM9Write32(kMvlRuntimeConfigAddr + kMvlRuntimeConfigSceneSettingsOffset,
                   context.CurrentStageSceneSettings);
  nds->ARM9Write32(kMvlRuntimeConfigAddr + kMvlRuntimeConfigInitialLivesOffset,
                   context.Mvl.InitialLives);
  nds->ARM9Write32(kMvlRuntimeConfigAddr +
                       kMvlRuntimeConfigLifeModeSelectorOffset,
                   context.Mvl.LifeModeSelector);
  nds->ARM9Write32(kMvlRuntimeConfigAddr +
                       kMvlRuntimeConfigBigStarSelectorOffset,
                   context.Mvl.BigStarSelector);
}

void ApplyPlayerStickToStar(const Context &context, int instanceID,
                            melonDS::u32 frame, melonDS::NDS *nds) {
  const Config::RuntimePatchConfig &patches = context.Patches;
  if (patches.PlayerStickToStarStartFrame == 0 &&
      patches.PlayerStickToStarEndFrame == 0)
    return;
  if (frame < patches.PlayerStickToStarStartFrame ||
      frame > patches.PlayerStickToStarEndFrame)
    return;
  if (!IsValidInstance(instanceID) || !nds || !nds->MainRAM)
    return;

  GameStateReader::ObjectScanSample star =
      GameStateReader::FindObjectByIDAndSettings(
          nds, kVsBattleStarActorObjectID, kVsBattleStarActorSettings);
  if (!star.Found)
    star = GameStateReader::FindVsBattleStarCandidate(nds);
  const GameStateReader::PlayerActorScanSample players =
      GameStateReader::FindPlayerActors(nds);
  const GameStateReader::ObjectScanSample &player =
      patches.PlayerStickToStarSlot == 1 ? players.Actor1 : players.Actor0;
  if (!star.Found || !player.Found) {
    if (frame == patches.PlayerStickToStarStartFrame) {
      std::printf("NSMB Test: player stick to VS star skipped inst=%d frame=%u "
                  "star=%u player=%u\n",
                  instanceID, frame, star.Found, player.Found);
    }
    return;
  }

  GameStateWriter::WriteObjectTransformAndClearMotionByBase(
      nds, player.Base, star.PosX, star.PosY, star.PosZ);
  if (frame == patches.PlayerStickToStarStartFrame) {
    std::printf(
        "NSMB Test: started player stick to VS star inst=%d frame=%u-%u "
        "slot=%d playerGuid=0x%X starGuid=0x%X pos=0x%08X,0x%08X,0x%08X\n",
        instanceID, patches.PlayerStickToStarStartFrame,
        patches.PlayerStickToStarEndFrame, patches.PlayerStickToStarSlot,
        player.GUID, star.GUID, star.PosX, star.PosY, star.PosZ);
  }
}

void ForcePlayerDeathCounters(const Context &context, int instanceID,
                              melonDS::u32 frame, melonDS::NDS *nds) {
  const Config::RuntimePatchConfig &patches = context.Patches;
  if (!patches.ForcePlayerDeathCountersEnabled || !nds || !nds->MainRAM)
    return;
  if (!MvlRuntime::IsRoleAllowed(context.IsHost,
                                 patches.ForcePlayerDeathCountersHostOnly,
                                 patches.ForcePlayerDeathCountersClientOnly))
    return;
  if (!IsValidInstance(instanceID) ||
      !MvlRuntime::IsFrameInRange(frame,
                                  patches.ForcePlayerDeathCountersStartFrame,
                                  patches.ForcePlayerDeathCountersEndFrame) ||
      !IsMarioVsLuigiGameplay(nds))
    return;

  const melonDS::u32 deaths[2]{patches.ForcePlayerDeathCounter0,
                               patches.ForcePlayerDeathCounter1};
  const melonDS::u32 lives[2]{patches.ForcePlayerLife0,
                              patches.ForcePlayerLife1};
  GameStateWriter::PlayerDeathCounterPatchResult result;
  if (!GameStateWriter::WritePlayerDeathCounterPatch(
          nds, deaths, patches.ForcePlayerLivesEnabled, lives, result))
    return;

  if (context.Diagnostics.TakeRuntimePatchLog(
          instanceID, Diagnostics::RuntimePatchLogKind::ForceDeathCounters)) {
    std::printf(
        "NSMB Test: force player death counters inst=%d frame=%u range=%u-%u "
        "old=%u/%u value=%u/%u lives=%u/%u->%u/%u enabled=%d\n",
        instanceID, frame, patches.ForcePlayerDeathCountersStartFrame,
        patches.ForcePlayerDeathCountersEndFrame, result.OldDeaths[0],
        result.OldDeaths[1], patches.ForcePlayerDeathCounter0,
        patches.ForcePlayerDeathCounter1, result.OldLives[0],
        result.OldLives[1], patches.ForcePlayerLife0, patches.ForcePlayerLife1,
        patches.ForcePlayerLivesEnabled ? 1 : 0);
  }
}

void ForcePlayerInventoryPowerups(const Context &context, int instanceID,
                                  melonDS::u32 frame, melonDS::NDS *nds) {
  const Config::RuntimePatchConfig &patches = context.Patches;
  if (!patches.ForcePlayerInventoryPowerupsEnabled || !nds || !nds->MainRAM ||
      !IsValidInstance(instanceID) ||
      !MvlRuntime::IsFrameInRange(
          frame, patches.ForcePlayerInventoryPowerupsStartFrame,
          patches.ForcePlayerInventoryPowerupsEndFrame) ||
      !IsMarioVsLuigiGameplay(nds))
    return;

  const melonDS::u8 values[2]{
      static_cast<melonDS::u8>(patches.ForcePlayerInventoryPowerup0 & 0xFF),
      static_cast<melonDS::u8>(patches.ForcePlayerInventoryPowerup1 & 0xFF)};
  GameStateWriter::PlayerBytePairPatchResult result;
  if (!GameStateWriter::WritePlayerInventoryPowerupPatch(nds, values, result))
    return;

  if (context.Diagnostics.TakeRuntimePatchLog(
          instanceID,
          Diagnostics::RuntimePatchLogKind::ForceInventoryPowerups)) {
    std::printf("NSMB Test: force player inventory powerups inst=%d frame=%u "
                "range=%u-%u old=%u/%u value=%u/%u\n",
                instanceID, frame,
                patches.ForcePlayerInventoryPowerupsStartFrame,
                patches.ForcePlayerInventoryPowerupsEndFrame,
                result.OldValues[0], result.OldValues[1], values[0], values[1]);
  }
}

void ForcePlayerPowerups(const Context &context, int instanceID,
                         melonDS::u32 frame, melonDS::NDS *nds) {
  const Config::RuntimePatchConfig &patches = context.Patches;
  if (!patches.ForcePlayerPowerupsEnabled || !nds || !nds->MainRAM ||
      !IsValidInstance(instanceID) ||
      !MvlRuntime::IsFrameInRange(frame, patches.ForcePlayerPowerupsStartFrame,
                                  patches.ForcePlayerPowerupsEndFrame) ||
      !IsMarioVsLuigiGameplay(nds))
    return;

  const melonDS::u8 values[2]{
      static_cast<melonDS::u8>(patches.ForcePlayerPowerup0 & 0xFF),
      static_cast<melonDS::u8>(patches.ForcePlayerPowerup1 & 0xFF)};
  GameStateWriter::PlayerPowerupPatchResult result;
  if (!GameStateWriter::WritePlayerPowerupPatch(nds, values, result))
    return;

  if (context.Diagnostics.TakeRuntimePatchLog(
          instanceID, Diagnostics::RuntimePatchLogKind::ForcePowerups)) {
    std::printf(
        "NSMB Test: force player active powerups inst=%d frame=%u range=%u-%u "
        "globalOld=%u/%u value=%u/%u actorBase=0x%08X/0x%08X "
        "actorStateOld=%u/%u actorFormOld=%u/%u\n",
        instanceID, frame, patches.ForcePlayerPowerupsStartFrame,
        patches.ForcePlayerPowerupsEndFrame, result.OldGlobalValues[0],
        result.OldGlobalValues[1], values[0], values[1], result.ActorBases[0],
        result.ActorBases[1], result.OldActorStates[0],
        result.OldActorStates[1], result.OldActorForms[0],
        result.OldActorForms[1]);
  }
}

void ForcePlayerStarCounters(const Context &context, int instanceID,
                             melonDS::u32 frame, melonDS::NDS *nds) {
  const Config::RuntimePatchConfig &patches = context.Patches;
  if (!patches.ForcePlayerStarCountersEnabled || !nds || !nds->MainRAM ||
      !IsValidInstance(instanceID) ||
      !MvlRuntime::IsFrameInRange(frame,
                                  patches.ForcePlayerStarCountersStartFrame,
                                  patches.ForcePlayerStarCountersEndFrame) ||
      !IsMarioVsLuigiGameplay(nds))
    return;

  const melonDS::u32 battleStars[2]{patches.ForcePlayerBattleStars0,
                                    patches.ForcePlayerBattleStars1};
  const melonDS::u32 displayedStars[2]{patches.ForcePlayerDisplayedStars0,
                                       patches.ForcePlayerDisplayedStars1};
  const melonDS::u32 collectedStars[2]{patches.ForcePlayerCollectedStars0,
                                       patches.ForcePlayerCollectedStars1};
  GameStateWriter::PlayerStarCounterPatchResult result;
  if (!GameStateWriter::WritePlayerStarCounterPatch(
          nds, battleStars, displayedStars, collectedStars, result))
    return;

  if (context.Diagnostics.TakeRuntimePatchLog(
          instanceID, Diagnostics::RuntimePatchLogKind::ForceStarCounters)) {
    std::printf(
        "NSMB Test: force player star counters inst=%d frame=%u range=%u-%u "
        "battle=%u/%u->%u/%u displayed=%u/%u->%u/%u collected=%u/%u->%u/%u\n",
        instanceID, frame, patches.ForcePlayerStarCountersStartFrame,
        patches.ForcePlayerStarCountersEndFrame, result.OldBattleStars[0],
        result.OldBattleStars[1], battleStars[0], battleStars[1],
        result.OldDisplayedStars[0], result.OldDisplayedStars[1],
        displayedStars[0], displayedStars[1], result.OldCollectedStars[0],
        result.OldCollectedStars[1], collectedStars[0], collectedStars[1]);
  }
}

void ClearCameraInitHold(const Context &context, int instanceID,
                         melonDS::u32 frame, melonDS::NDS *nds) {
  const Config::MvlCameraInitHoldConfig &camera = context.Mvl.CameraInitHold;
  if (!camera.Enabled || !nds || !nds->MainRAM ||
      !IsValidInstance(instanceID) ||
      context.Runtime.Instances[instanceID].ClearCameraInitHoldApplied ||
      !MvlRuntime::IsFrameInRange(frame, camera.StartFrame, camera.EndFrame) ||
      !MvlRuntime::IsRoleAllowed(context.IsHost, camera.HostOnly,
                                 camera.ClientOnly) ||
      !IsMarioVsLuigiGameplay(nds))
    return;

  const melonDS::u8 oldValue = nds->ARM9Read8(kMvlCameraModeFlagsAddr);
  if ((oldValue & 0x08) == 0)
    return;

  const melonDS::u8 newValue = static_cast<melonDS::u8>(oldValue & ~0x08u);
  nds->ARM9Write8(kMvlCameraModeFlagsAddr, newValue);
  context.Runtime.Instances[instanceID].ClearCameraInitHoldApplied = true;
  std::printf("NSMB Test: clear MvL camera init hold inst=%d frame=%u "
              "addr=%08X old=0x%02X value=0x%02X\n",
              instanceID, frame, kMvlCameraModeFlagsAddr, oldValue, newValue);
  std::fflush(stdout);
}

bool WriteRandomSeed(melonDS::NDS *nds, melonDS::u32 seed) {
  constexpr melonDS::u32 kNetRandomValueOffset =
      kNetRandomValueAddr - kMainRAMBase;
  constexpr melonDS::u32 kNetRandomCallCountOffset =
      kNetRandomCallCountAddr - kMainRAMBase;
  constexpr melonDS::u32 kGameRandomValueOffset =
      kGameRandomValueAddr - kMainRAMBase;
  constexpr melonDS::u32 kGameRandomCallCountOffset =
      kGameRandomCallCountAddr - kMainRAMBase;
  if (!nds || !nds->MainRAM ||
      kNetRandomValueOffset + sizeof(seed) > nds->MainRAMMask + 1 ||
      kGameRandomValueOffset + sizeof(seed) > nds->MainRAMMask + 1)
    return false;

  std::memcpy(&nds->MainRAM[kNetRandomValueOffset], &seed, sizeof(seed));
  nds->MainRAM[kNetRandomCallCountOffset] = 0;
  std::memcpy(&nds->MainRAM[kGameRandomValueOffset], &seed, sizeof(seed));
  nds->MainRAM[kGameRandomCallCountOffset] = 0;
  return true;
}

void ApplyNetRandomPatch(const Context &context, int instanceID,
                         melonDS::u32 frame, melonDS::NDS *nds) {
  if (!nds || !nds->MainRAM || !context.Mvl.NetRandom.Enabled ||
      !IsValidInstance(instanceID) ||
      context.Runtime.Instances[instanceID].NetRandomPatchApplied)
    return;

  bool shouldPatch = frame == context.Mvl.NetRandom.Frame;
  melonDS::u8 randomCallCountBeforePatch = 0;
  melonDS::u8 gameRandomCallCountBeforePatch = 0;
  if (context.Mvl.NetRandom.Auto) {
    const melonDS::u32 ggid = nds->ARM9Read32(kNetGGIDAddr);
    randomCallCountBeforePatch = nds->ARM9Read8(kNetRandomCallCountAddr);
    gameRandomCallCountBeforePatch = nds->ARM9Read8(kGameRandomCallCountAddr);
    shouldPatch = IsMarioVsLuigiGameplay(nds) || IsMarioVsLuigiGGID(ggid);
  }
  if (!shouldPatch)
    return;

  const melonDS::u32 patchValue =
      (!context.Mvl.MatchSeedSequence.empty() ||
       context.Mvl.AutoRestartAfterResult)
          ? context.Runtime.MatchSeedForGame(instanceID, context.Mvl)
          : context.Mvl.NetRandom.Value;
  if (!WriteRandomSeed(nds, patchValue))
    return;
  context.Runtime.Instances[instanceID].NetRandomPatchApplied = true;
  std::printf(
      "NSMB Test: patched Net/Game random inst=%d frame=%u value=0x%08X "
      "auto=%d oldNetCount=0x%02X oldGameCount=0x%02X resetCount=1\n",
      instanceID, frame, patchValue, context.Mvl.NetRandom.Auto ? 1 : 0,
      randomCallCountBeforePatch, gameRandomCallCountBeforePatch);
}

} // namespace NsmbNetplayPoC::MvlGameHooks
