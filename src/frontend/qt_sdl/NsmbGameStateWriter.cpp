#include "NsmbGameStateWriter.h"

#include "NDS.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace NsmbMvlNetplay::GameStateWriter {

namespace {

constexpr melonDS::u32 kMainRAMBase = 0x02000000;
constexpr melonDS::u32 kNetRandomBranchAddressAddr = 0x0208885C;
constexpr melonDS::u32 kNetRandomCallCountAddr = 0x02088A48;
constexpr melonDS::u32 kNetRandomValueAddr = 0x02088A68;
constexpr melonDS::u32 kGamePlayerPowerupAddr = 0x0208B324;
constexpr melonDS::u32 kGamePlayerInventoryPowerupAddr = 0x0208B32C;
constexpr melonDS::u32 kGamePlayerCountAddr = 0x0208B348;
constexpr melonDS::u32 kGamePlayerLivesAddr = 0x0208B364;
constexpr melonDS::u32 kGamePlayerBattleStarsAddr = 0x0208B36C;
constexpr melonDS::u32 kGamePlayerCoinsAddr = 0x0208B37C;
constexpr melonDS::u32 kGamePlayerScoreAddr = 0x0208B384;
constexpr melonDS::u32 kGamePlayerDisplayedStarsAddr = 0x0208B38C;
constexpr melonDS::u32 kGamePlayerDeathsAddr = 0x0208B394;
constexpr melonDS::u32 kGamePlayerCollectedStarsAddr = 0x0208B39C;
constexpr melonDS::u32 kGameVsCoinCountAddr = 0x0208B37C;
constexpr melonDS::u32 kPlayerActorPlayerIDOffset = 0x11E;
constexpr melonDS::u32 kPlayerBasePowerupStateOffset = 0x7AB;
constexpr melonDS::u32 kPlayerBasePowerupFormStateOffset = 0x7AC;
constexpr melonDS::u32 kPlayerBasePowerupSubStateOffset = 0x7AD;
constexpr melonDS::u16 kVsBattleStarActorObjectID = 0x0022;
constexpr melonDS::u32 kVsBattleStarActorSettings = 0x00000001;
constexpr melonDS::u16 kStageSceneObjectID = 0x0003;
constexpr melonDS::u16 kStageCameraObjectID = 0x013C;

bool IsARM9MainRAMAddress(melonDS::u32 address) {
  return (address & 0xFF000000u) == 0x02000000u;
}

bool ReadMainRAMU32(melonDS::NDS *nds, melonDS::u32 offset,
                    melonDS::u32 &value) {
  if (!nds || !nds->MainRAM || offset + sizeof(value) > nds->MainRAMMask + 1)
    return false;
  std::memcpy(&value, &nds->MainRAM[offset], sizeof(value));
  return true;
}

bool WriteMainRAMU32(melonDS::NDS *nds, melonDS::u32 offset,
                     melonDS::u32 value) {
  if (!nds || !nds->MainRAM || offset + sizeof(value) > nds->MainRAMMask + 1)
    return false;
  std::memcpy(&nds->MainRAM[offset], &value, sizeof(value));
  return true;
}

bool WriteObjectWordByIDAndSettings(melonDS::NDS *nds,
                                    melonDS::u16 expectedObjectID,
                                    melonDS::u32 expectedSettings,
                                    melonDS::u32 relativeOffset,
                                    melonDS::u32 value) {
  const GameStateReader::ObjectScanSample actor =
      GameStateReader::FindObjectByIDAndSettings(nds, expectedObjectID,
                                                 expectedSettings);
  if (!actor.Found || actor.Base < kMainRAMBase)
    return false;
  WriteMainRAMU32(nds, actor.Base - kMainRAMBase + relativeOffset, value);
  return true;
}

bool WriteObjectPositionByGUID(melonDS::NDS *nds, melonDS::u32 guid,
                               melonDS::u32 posX, melonDS::u32 posY,
                               melonDS::u32 posZ) {
  if (!nds || !nds->MainRAM || guid == 0)
    return false;
  const melonDS::u32 ramLength = nds->MainRAMMask + 1;
  if (ramLength < 0x120)
    return false;

  for (melonDS::u32 offset = 0; offset <= ramLength - 0x120; offset += 4) {
    melonDS::u32 candidateGUID = 0;
    if (!ReadMainRAMU32(nds, offset + 4, candidateGUID) ||
        candidateGUID != guid)
      continue;

    melonDS::u32 vtable = 0;
    melonDS::u16 objectID = 0;
    melonDS::u16 stateType = 0;
    melonDS::u32 flags = 0;
    std::memcpy(&vtable, &nds->MainRAM[offset], sizeof(vtable));
    std::memcpy(&objectID, &nds->MainRAM[offset + 0x0C], sizeof(objectID));
    std::memcpy(&stateType, &nds->MainRAM[offset + 0x0E], sizeof(stateType));
    std::memcpy(&flags, &nds->MainRAM[offset + 0x10], sizeof(flags));
    if (vtable < kMainRAMBase || vtable >= kMainRAMBase + ramLength ||
        objectID == 0 || objectID >= 0x400 ||
        (stateType != 1 && stateType != 2 && stateType != 3) ||
        flags >= 0x10000000)
      continue;

    WriteMainRAMU32(nds, offset + 0x60, posX);
    WriteMainRAMU32(nds, offset + 0x64, posY);
    WriteMainRAMU32(nds, offset + 0x68, posZ);
    return true;
  }
  return false;
}

bool WriteObjectPositionByIDAndSettings(melonDS::NDS *nds,
                                        melonDS::u16 expectedObjectID,
                                        melonDS::u32 expectedSettings,
                                        melonDS::u32 posX,
                                        melonDS::u32 posY,
                                        melonDS::u32 posZ) {
  const GameStateReader::ObjectScanSample actor =
      GameStateReader::FindObjectByIDAndSettings(nds, expectedObjectID,
                                                 expectedSettings);
  if (!actor.Found || actor.Base < kMainRAMBase)
    return false;
  const melonDS::u32 offset = actor.Base - kMainRAMBase;
  WriteMainRAMU32(nds, offset + 0x60, posX);
  WriteMainRAMU32(nds, offset + 0x64, posY);
  WriteMainRAMU32(nds, offset + 0x68, posZ);
  WriteMainRAMU32(nds, offset + 0x70, posX);
  WriteMainRAMU32(nds, offset + 0x74, posY);
  WriteMainRAMU32(nds, offset + 0x78, posZ);
  return true;
}

bool WriteVsBattleStarCandidatePosition(melonDS::NDS *nds,
                                        melonDS::u32 posX,
                                        melonDS::u32 posY,
                                        melonDS::u32 posZ) {
  const GameStateReader::ObjectScanSample actor =
      GameStateReader::FindVsBattleStarCandidate(nds);
  if (!actor.Found || actor.Base < kMainRAMBase)
    return false;
  const melonDS::u32 offset = actor.Base - kMainRAMBase;
  WriteMainRAMU32(nds, offset + 0x60, posX);
  WriteMainRAMU32(nds, offset + 0x64, posY);
  WriteMainRAMU32(nds, offset + 0x68, posZ);
  WriteMainRAMU32(nds, offset + 0x70, posX);
  WriteMainRAMU32(nds, offset + 0x74, posY);
  WriteMainRAMU32(nds, offset + 0x78, posZ);
  return true;
}

bool WriteObjectTransformByGUID(melonDS::NDS *nds, melonDS::u32 guid,
                                const ObjectTransform &transform) {
  if (!nds || !nds->MainRAM || guid == 0)
    return false;
  const melonDS::u32 ramLength = nds->MainRAMMask + 1;
  if (ramLength < 0x120)
    return false;

  for (melonDS::u32 offset = 0; offset <= ramLength - 0x120; offset += 4) {
    melonDS::u32 candidateGUID = 0;
    if (!ReadMainRAMU32(nds, offset + 4, candidateGUID) ||
        candidateGUID != guid)
      continue;

    melonDS::u32 vtable = 0;
    melonDS::u16 objectID = 0;
    melonDS::u16 stateType = 0;
    melonDS::u32 flags = 0;
    std::memcpy(&vtable, &nds->MainRAM[offset], sizeof(vtable));
    std::memcpy(&objectID, &nds->MainRAM[offset + 0x0C], sizeof(objectID));
    std::memcpy(&stateType, &nds->MainRAM[offset + 0x0E], sizeof(stateType));
    std::memcpy(&flags, &nds->MainRAM[offset + 0x10], sizeof(flags));
    if (vtable < kMainRAMBase || vtable >= kMainRAMBase + ramLength ||
        objectID == 0 || objectID >= 0x400 ||
        (stateType != 1 && stateType != 2 && stateType != 3) ||
        flags >= 0x10000000)
      continue;

    WriteMainRAMU32(nds, offset + 0x60, transform.PosX);
    WriteMainRAMU32(nds, offset + 0x64, transform.PosY);
    WriteMainRAMU32(nds, offset + 0x68, transform.PosZ);
    WriteMainRAMU32(nds, offset + 0x70, transform.PrevX);
    WriteMainRAMU32(nds, offset + 0x74, transform.PrevY);
    WriteMainRAMU32(nds, offset + 0x78, transform.PrevZ);
    WriteMainRAMU32(nds, offset + 0xD0, transform.VelX);
    WriteMainRAMU32(nds, offset + 0xD4, transform.VelY);
    WriteMainRAMU32(nds, offset + 0xD8, transform.VelZ);
    return true;
  }
  return false;
}

} // namespace

bool WriteObjectTransformByBase(melonDS::NDS *nds, melonDS::u32 base,
                                melonDS::u32 posX, melonDS::u32 posY,
                                melonDS::u32 posZ, melonDS::u32 prevX,
                                melonDS::u32 prevY, melonDS::u32 prevZ,
                                melonDS::u32 velX, melonDS::u32 velY,
                                melonDS::u32 velZ) {
  if (!nds || !nds->MainRAM || base < kMainRAMBase)
    return false;
  const melonDS::u32 offset = base - kMainRAMBase;
  const melonDS::u32 ramLength = nds->MainRAMMask + 1;
  if (offset + 0xDC > ramLength)
    return false;
  WriteMainRAMU32(nds, offset + 0x60, posX);
  WriteMainRAMU32(nds, offset + 0x64, posY);
  WriteMainRAMU32(nds, offset + 0x68, posZ);
  WriteMainRAMU32(nds, offset + 0x70, prevX);
  WriteMainRAMU32(nds, offset + 0x74, prevY);
  WriteMainRAMU32(nds, offset + 0x78, prevZ);
  WriteMainRAMU32(nds, offset + 0xD0, velX);
  WriteMainRAMU32(nds, offset + 0xD4, velY);
  WriteMainRAMU32(nds, offset + 0xD8, velZ);
  return true;
}

bool WriteObjectTransformAndClearMotionByBase(
    melonDS::NDS *nds, melonDS::u32 base, melonDS::u32 posX,
    melonDS::u32 posY, melonDS::u32 posZ) {
  if (!nds || !nds->MainRAM || base < kMainRAMBase)
    return false;
  const melonDS::u32 offset = base - kMainRAMBase;
  bool ok = true;
  ok = WriteMainRAMU32(nds, offset + 0x60, posX) && ok;
  ok = WriteMainRAMU32(nds, offset + 0x64, posY) && ok;
  ok = WriteMainRAMU32(nds, offset + 0x68, posZ) && ok;
  ok = WriteMainRAMU32(nds, offset + 0x70, posX) && ok;
  ok = WriteMainRAMU32(nds, offset + 0x74, posY) && ok;
  ok = WriteMainRAMU32(nds, offset + 0x78, posZ) && ok;
  ok = WriteMainRAMU32(nds, offset + 0x80, 0) && ok;
  ok = WriteMainRAMU32(nds, offset + 0x84, 0) && ok;
  ok = WriteMainRAMU32(nds, offset + 0x88, 0) && ok;
  ok = WriteMainRAMU32(nds, offset + 0xD0, 0) && ok;
  ok = WriteMainRAMU32(nds, offset + 0xD4, 0) && ok;
  ok = WriteMainRAMU32(nds, offset + 0xD8, 0) && ok;
  return ok;
}

bool WritePlayerDeathCounterPatch(
    melonDS::NDS *nds, const melonDS::u32 deaths[2], bool writeLives,
    const melonDS::u32 lives[2], PlayerDeathCounterPatchResult &result) {
  result = {};
  if (!nds || !nds->MainRAM)
    return false;
  result.OldDeaths[0] = nds->ARM9Read32(kGamePlayerDeathsAddr);
  result.OldDeaths[1] =
      nds->ARM9Read32(kGamePlayerDeathsAddr + sizeof(melonDS::u32));
  result.OldLives[0] = nds->ARM9Read32(kGamePlayerLivesAddr);
  result.OldLives[1] =
      nds->ARM9Read32(kGamePlayerLivesAddr + sizeof(melonDS::u32));
  nds->ARM9Write32(kGamePlayerDeathsAddr, deaths[0]);
  nds->ARM9Write32(kGamePlayerDeathsAddr + sizeof(melonDS::u32), deaths[1]);
  if (writeLives) {
    nds->ARM9Write32(kGamePlayerLivesAddr, lives[0]);
    nds->ARM9Write32(kGamePlayerLivesAddr + sizeof(melonDS::u32), lives[1]);
  }
  return true;
}

bool WritePlayerInventoryPowerupPatch(
    melonDS::NDS *nds, const melonDS::u8 values[2],
    PlayerBytePairPatchResult &result) {
  result = {};
  if (!nds || !nds->MainRAM)
    return false;
  result.OldValues[0] = nds->ARM9Read8(kGamePlayerInventoryPowerupAddr);
  result.OldValues[1] = nds->ARM9Read8(kGamePlayerInventoryPowerupAddr + 1);
  nds->ARM9Write8(kGamePlayerInventoryPowerupAddr, values[0]);
  nds->ARM9Write8(kGamePlayerInventoryPowerupAddr + 1, values[1]);
  return true;
}

bool WritePlayerPowerupPatch(melonDS::NDS *nds,
                             const melonDS::u8 values[2],
                             PlayerPowerupPatchResult &result) {
  result = {};
  if (!nds || !nds->MainRAM)
    return false;
  result.OldGlobalValues[0] = nds->ARM9Read8(kGamePlayerPowerupAddr);
  result.OldGlobalValues[1] = nds->ARM9Read8(kGamePlayerPowerupAddr + 1);
  nds->ARM9Write8(kGamePlayerPowerupAddr, values[0]);
  nds->ARM9Write8(kGamePlayerPowerupAddr + 1, values[1]);

  const GameStateReader::PlayerActorScanSample players =
      GameStateReader::FindPlayerActors(nds);
  const GameStateReader::ObjectScanSample actors[2]{players.Actor0,
                                                    players.Actor1};
  for (const GameStateReader::ObjectScanSample &actor : actors) {
    if (!actor.Found || !IsARM9MainRAMAddress(actor.Base))
      continue;
    const melonDS::u32 player =
        nds->ARM9Read8(actor.Base + kPlayerActorPlayerIDOffset) & 1u;
    result.ActorBases[player] = actor.Base;
    result.OldActorStates[player] =
        nds->ARM9Read8(actor.Base + kPlayerBasePowerupStateOffset);
    result.OldActorForms[player] =
        nds->ARM9Read8(actor.Base + kPlayerBasePowerupFormStateOffset);
    nds->ARM9Write8(actor.Base + kPlayerBasePowerupStateOffset,
                    values[player]);
    nds->ARM9Write8(actor.Base + kPlayerBasePowerupFormStateOffset,
                    values[player]);
    nds->ARM9Write8(actor.Base + kPlayerBasePowerupSubStateOffset, 0);
  }
  return true;
}

bool WritePlayerStarCounterPatch(
    melonDS::NDS *nds, const melonDS::u32 battleStars[2],
    const melonDS::u32 displayedStars[2],
    const melonDS::u32 collectedStars[2],
    PlayerStarCounterPatchResult &result) {
  result = {};
  if (!nds || !nds->MainRAM)
    return false;
  result.OldBattleStars[0] = nds->ARM9Read32(kGamePlayerBattleStarsAddr);
  result.OldBattleStars[1] =
      nds->ARM9Read32(kGamePlayerBattleStarsAddr + sizeof(melonDS::u32));
  result.OldDisplayedStars[0] =
      nds->ARM9Read32(kGamePlayerDisplayedStarsAddr);
  result.OldDisplayedStars[1] =
      nds->ARM9Read32(kGamePlayerDisplayedStarsAddr + sizeof(melonDS::u32));
  result.OldCollectedStars[0] =
      nds->ARM9Read32(kGamePlayerCollectedStarsAddr);
  result.OldCollectedStars[1] =
      nds->ARM9Read32(kGamePlayerCollectedStarsAddr + sizeof(melonDS::u32));
  nds->ARM9Write32(kGamePlayerBattleStarsAddr, battleStars[0]);
  nds->ARM9Write32(kGamePlayerBattleStarsAddr + sizeof(melonDS::u32),
                   battleStars[1]);
  nds->ARM9Write32(kGamePlayerDisplayedStarsAddr, displayedStars[0]);
  nds->ARM9Write32(kGamePlayerDisplayedStarsAddr + sizeof(melonDS::u32),
                   displayedStars[1]);
  nds->ARM9Write32(kGamePlayerCollectedStarsAddr, collectedStars[0]);
  nds->ARM9Write32(kGamePlayerCollectedStarsAddr + sizeof(melonDS::u32),
                   collectedStars[1]);
  return true;
}

GameStateApplyResult ApplyGameState(
    melonDS::NDS *nds, const GameStateModel::GameStateSample &sample,
    const GameStateApplyOptions &options) {
  GameStateApplyResult result;
  if (!nds || !nds->MainRAM)
    return result;

  if (options.RemotePlayerOnly) {
    const GameStateReader::PlayerActorScanSample localPlayers =
        GameStateReader::FindPlayerActors(nds);
    if (options.RemotePlayer == 0 && sample.PlayerActor0Found) {
      const melonDS::u32 localBase =
          localPlayers.Actor0.Found ? localPlayers.Actor0.Base : 0;
      result.RemotePlayerApplied = WriteObjectTransformByBase(
          nds, localBase, sample.PlayerActor0PosX, sample.PlayerActor0PosY,
          sample.PlayerActor0PosZ, sample.PlayerActor0PrevX,
          sample.PlayerActor0PrevY, sample.PlayerActor0PrevZ,
          sample.PlayerActor0VelX, sample.PlayerActor0VelY,
          sample.PlayerActor0VelZ);
    } else if (options.RemotePlayer == 1 && sample.PlayerActor1Found) {
      const melonDS::u32 localBase =
          localPlayers.Actor1.Found ? localPlayers.Actor1.Base : 0;
      result.RemotePlayerApplied = WriteObjectTransformByBase(
          nds, localBase, sample.PlayerActor1PosX, sample.PlayerActor1PosY,
          sample.PlayerActor1PosZ, sample.PlayerActor1PrevX,
          sample.PlayerActor1PrevY, sample.PlayerActor1PrevZ,
          sample.PlayerActor1VelX, sample.PlayerActor1VelY,
          sample.PlayerActor1VelZ);
    }
    return result;
  }

  if (options.CriticalGlobals) {
    nds->ARM9Write32(kNetRandomValueAddr, sample.NetRandomValue);
    nds->ARM9Write8(
        kNetRandomCallCountAddr,
        static_cast<melonDS::u8>(sample.NetRandomCallCount & 0xFF));
    nds->ARM9Write32(kNetRandomBranchAddressAddr,
                     sample.NetRandomBranchAddress);
    nds->ARM9Write32(kGamePlayerCountAddr, sample.PlayerCount);
    nds->ARM9Write32(kGamePlayerBattleStarsAddr, sample.Player0BattleStars);
    nds->ARM9Write32(kGamePlayerBattleStarsAddr + sizeof(melonDS::u32),
                     sample.Player1BattleStars);
    nds->ARM9Write32(kGamePlayerCoinsAddr, sample.Player0Coins);
    nds->ARM9Write32(kGamePlayerCoinsAddr + sizeof(melonDS::u32),
                     sample.Player1Coins);
    nds->ARM9Write32(kGamePlayerScoreAddr, sample.Player0Score);
    nds->ARM9Write32(kGamePlayerScoreAddr + sizeof(melonDS::u32),
                     sample.Player1Score);
    nds->ARM9Write32(kGamePlayerDisplayedStarsAddr,
                     sample.Player0DisplayedStars);
    nds->ARM9Write32(kGamePlayerDisplayedStarsAddr + sizeof(melonDS::u32),
                     sample.Player1DisplayedStars);
    nds->ARM9Write32(kGamePlayerDeathsAddr, sample.Player0Deaths);
    nds->ARM9Write32(kGamePlayerDeathsAddr + sizeof(melonDS::u32),
                     sample.Player1Deaths);
    nds->ARM9Write32(kGamePlayerCollectedStarsAddr,
                     sample.Player0CollectedStars);
    nds->ARM9Write32(kGamePlayerCollectedStarsAddr + sizeof(melonDS::u32),
                     sample.Player1CollectedStars);
    nds->ARM9Write32(kGameVsCoinCountAddr, sample.VsCoinCount);
  }

  if (options.StageObjects && sample.StageCameraFound) {
    WriteObjectWordByIDAndSettings(nds, kStageCameraObjectID, 0, 0x190,
                                   sample.StageCameraWord190);
    WriteObjectWordByIDAndSettings(nds, kStageCameraObjectID, 0, 0x194,
                                   sample.StageCameraWord194);
    WriteObjectWordByIDAndSettings(nds, kStageCameraObjectID, 0, 0x19C,
                                   sample.StageCameraWord19C);
    WriteObjectWordByIDAndSettings(nds, kStageCameraObjectID, 0, 0x1A0,
                                   sample.StageCameraWord1A0);
  }
  if (options.StageObjects && sample.StageSceneFound) {
    WriteObjectWordByIDAndSettings(nds, kStageSceneObjectID,
                                   options.StageSceneSettings, 0x154,
                                   sample.StageSceneWord154);
    WriteObjectWordByIDAndSettings(nds, kStageSceneObjectID,
                                   options.StageSceneSettings, 0x160,
                                   sample.StageSceneWord160);
  }
  if (options.StageObjects && sample.MovingHazardFound) {
    WriteObjectTransformByGUID(
        nds, sample.MovingHazardGUID,
        {sample.MovingHazardPosX, sample.MovingHazardPosY,
         sample.MovingHazardPosZ, sample.MovingHazardPosX,
         sample.MovingHazardPosY, sample.MovingHazardPosZ,
         sample.MovingHazardVelX, sample.MovingHazardVelY, 0});
  }
  if (options.StarObjects && sample.VsStarFound) {
    if (!WriteObjectPositionByGUID(nds, sample.VsStarGUID, sample.VsStarPosX,
                                   sample.VsStarPosY, sample.VsStarPosZ))
      WriteVsBattleStarCandidatePosition(nds, sample.VsStarPosX,
                                         sample.VsStarPosY,
                                         sample.VsStarPosZ);
  } else if (options.StarObjects) {
    WriteVsBattleStarCandidatePosition(nds, 0, 0, 0);
  }
  if (options.StarObjects && sample.VsStarActorFound) {
    if (!WriteObjectPositionByGUID(nds, sample.VsStarActorGUID,
                                   sample.VsStarActorPosX,
                                   sample.VsStarActorPosY,
                                   sample.VsStarActorPosZ))
      WriteObjectPositionByIDAndSettings(
          nds, kVsBattleStarActorObjectID, kVsBattleStarActorSettings,
          sample.VsStarActorPosX, sample.VsStarActorPosY,
          sample.VsStarActorPosZ);
  } else if (options.StarObjects) {
    WriteObjectPositionByIDAndSettings(nds, kVsBattleStarActorObjectID,
                                       kVsBattleStarActorSettings, 0, 0, 0);
  }
  if (options.PlayerActors && sample.PlayerActor0Found)
    WriteObjectTransformByGUID(
        nds, sample.PlayerActor0GUID,
        {sample.PlayerActor0PosX, sample.PlayerActor0PosY,
         sample.PlayerActor0PosZ, sample.PlayerActor0PrevX,
         sample.PlayerActor0PrevY, sample.PlayerActor0PrevZ,
         sample.PlayerActor0VelX, sample.PlayerActor0VelY,
         sample.PlayerActor0VelZ});
  if (options.PlayerActors && sample.PlayerActor1Found)
    WriteObjectTransformByGUID(
        nds, sample.PlayerActor1GUID,
        {sample.PlayerActor1PosX, sample.PlayerActor1PosY,
         sample.PlayerActor1PosZ, sample.PlayerActor1PrevX,
         sample.PlayerActor1PrevY, sample.PlayerActor1PrevZ,
         sample.PlayerActor1VelX, sample.PlayerActor1VelY,
         sample.PlayerActor1VelZ});
  return result;
}

} // namespace NsmbMvlNetplay::GameStateWriter
