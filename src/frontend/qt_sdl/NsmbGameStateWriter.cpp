#include "NsmbGameStateWriter.h"

#include "NDS.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace NsmbNetplayPoC::GameStateWriter {

namespace {

constexpr melonDS::u32 kMainRAMBase = 0x02000000;
constexpr melonDS::u32 kNetRandomBranchAddressAddr = 0x0208885C;
constexpr melonDS::u32 kNetRandomCallCountAddr = 0x02088A48;
constexpr melonDS::u32 kNetRandomValueAddr = 0x02088A68;
constexpr melonDS::u32 kGamePlayerPowerupAddr = 0x0208B324;
constexpr melonDS::u32 kGamePlayerDeadAddr = 0x0208B328;
constexpr melonDS::u32 kGamePlayerInventoryPowerupAddr = 0x0208B32C;
constexpr melonDS::u32 kGamePlayerTransitionStatusAddr = 0x0208B354;
constexpr melonDS::u32 kGamePlayerCountAddr = 0x0208B348;
constexpr melonDS::u32 kGamePlayerLivesAddr = 0x0208B364;
constexpr melonDS::u32 kGamePlayerBattleStarsAddr = 0x0208B36C;
constexpr melonDS::u32 kGamePlayerCoinsAddr = 0x0208B37C;
constexpr melonDS::u32 kGamePlayerScoreAddr = 0x0208B384;
constexpr melonDS::u32 kGamePlayerDisplayedStarsAddr = 0x0208B38C;
constexpr melonDS::u32 kGamePlayerDeathsAddr = 0x0208B394;
constexpr melonDS::u32 kGamePlayerCollectedStarsAddr = 0x0208B39C;
constexpr melonDS::u32 kGameVsCoinCountAddr = 0x0208B37C;
constexpr melonDS::u32 kPlayerBaseActionFlagOffset = 0x778;
constexpr melonDS::u32 kPlayerBaseSubActionFlagOffset = 0x77C;
constexpr melonDS::u32 kPlayerBasePhysicsFlagOffset = 0x780;
constexpr melonDS::u32 kPlayerBaseTransitionFlagOffset = 0x784;
constexpr melonDS::u32 kPlayerBaseCollisionFlagOffset = 0x788;
constexpr melonDS::u32 kPlayerBaseEnvironmentFlagOffset = 0x790;
constexpr melonDS::u32 kPlayerBaseDamageCooldownOffset = 0x79C;
constexpr melonDS::u32 kPlayerBaseUpdateLockedOffset = 0x7A8;
constexpr melonDS::u32 kPlayerBaseCharacterIDOffset = 0x7AA;
constexpr melonDS::u32 kPlayerBaseTransitioningFlagOffset = 0x7B0;
constexpr melonDS::u32 kPlayerBaseCameraFocusModeOffset = 0x7B2;
constexpr melonDS::u32 kPlayerBaseDefeatedFlagOffset = 0x7B3;
constexpr melonDS::u32 kPlayerBasePlayerIDOffset = 0x7B4;
constexpr melonDS::u32 kPlayerBaseVisibleFlagOffset = 0x7B5;
constexpr melonDS::u32 kPlayerBaseTransitionStepOffset = 0xBAD;
constexpr melonDS::u32 kPlayerActorPlayerIDOffset = 0x11E;
constexpr melonDS::u32 kPlayerBasePowerupStateOffset = 0x7AB;
constexpr melonDS::u32 kPlayerBasePowerupFormStateOffset = 0x7AC;
constexpr melonDS::u32 kPlayerBasePowerupSubStateOffset = 0x7AD;
constexpr melonDS::u16 kVsBattleStarActorObjectID = 0x0022;
constexpr melonDS::u32 kVsBattleStarActorSettings = 0x00000001;
constexpr melonDS::u32 kEffectVTableStart = 0x02126A24;
constexpr melonDS::u32 kEffectVTablePtr = 0x02126A2C;
constexpr melonDS::u32 kWorldEffectWordStart = 0x04;
constexpr melonDS::u32 kWorldEffectWordEnd = 0xAC;
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

bool ReadMainRAMU8(melonDS::NDS *nds, melonDS::u32 offset,
                   melonDS::u8 &value) {
  if (!nds || !nds->MainRAM || offset + sizeof(value) > nds->MainRAMMask + 1)
    return false;
  value = nds->MainRAM[offset];
  return true;
}

bool WriteMainRAMU32(melonDS::NDS *nds, melonDS::u32 offset,
                     melonDS::u32 value) {
  if (!nds || !nds->MainRAM || offset + sizeof(value) > nds->MainRAMMask + 1)
    return false;
  std::memcpy(&nds->MainRAM[offset], &value, sizeof(value));
  return true;
}

bool WriteMainRAMU8(melonDS::NDS *nds, melonDS::u32 offset,
                    melonDS::u8 value) {
  if (!nds || !nds->MainRAM || offset + sizeof(value) > nds->MainRAMMask + 1)
    return false;
  nds->MainRAM[offset] = value;
  return true;
}

bool MainRAMOffsetFromAddr(melonDS::NDS *nds, melonDS::u32 address,
                           melonDS::u32 size, melonDS::u32 &offset) {
  if (!nds || !nds->MainRAM || address < kMainRAMBase)
    return false;
  offset = address - kMainRAMBase;
  return offset + size <= nds->MainRAMMask + 1;
}

bool IsValidMainRAMRange(melonDS::NDS *nds, melonDS::u32 address,
                         melonDS::u32 length) {
  if (!nds || !nds->MainRAM || length == 0 || address < kMainRAMBase)
    return false;
  const melonDS::u32 offset = address - kMainRAMBase;
  const melonDS::u32 ramLength = nds->MainRAMMask + 1;
  return offset < ramLength && length <= ramLength - offset;
}

bool ReadMainRAMAddressU32(melonDS::NDS *nds, melonDS::u32 address,
                           melonDS::u32 &value) {
  if (!IsValidMainRAMRange(nds, address, sizeof(value)))
    return false;
  std::memcpy(&value, nds->MainRAM + (address - kMainRAMBase), sizeof(value));
  return true;
}

bool WriteMainRAMAddrU8IfChanged(melonDS::NDS *nds, melonDS::u32 address,
                                 melonDS::u8 value) {
  melonDS::u32 offset = 0;
  if (!MainRAMOffsetFromAddr(nds, address, sizeof(value), offset))
    return false;
  if (nds->MainRAM[offset] == value)
    return true;
  return WriteMainRAMU8(nds, offset, value);
}

bool WriteMainRAMAddrU32IfChanged(melonDS::NDS *nds, melonDS::u32 address,
                                  melonDS::u32 value) {
  melonDS::u32 offset = 0;
  melonDS::u32 current = 0;
  if (!MainRAMOffsetFromAddr(nds, address, sizeof(value), offset) ||
      !ReadMainRAMU32(nds, offset, current))
    return false;
  if (current == value)
    return true;
  return WriteMainRAMU32(nds, offset, value);
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

bool ApplyWireWorldActorState(melonDS::NDS *nds,
                              const WireProtocol::WireWorldActorState &state,
                              melonDS::u32 predictFrames,
                              melonDS::u32 localBase) {
  if (!state.Found)
    return false;
  const ObjectTransform transform =
      PredictWorldActorTransform(state, predictFrames);
  return WriteObjectTransformByBase(
      nds, localBase, transform.PosX, transform.PosY, transform.PosZ,
      transform.PrevX, transform.PrevY, transform.PrevZ, transform.VelX,
      transform.VelY, transform.VelZ);
}

bool ApplyWireWorldMovingHazardState(
    melonDS::NDS *nds, const WireProtocol::WireWorldActorState &state,
    melonDS::u32 predictFrames, melonDS::u32 localBase) {
  if (!state.Found)
    return false;
  nds->ARM9Write16(localBase + 0x0E,
                   static_cast<melonDS::u16>(state.StateType));
  nds->ARM9Write32(localBase + 0x10, state.Flags);
  const ObjectTransform transform =
      PredictWorldActorTransform(state, predictFrames);
  if (!WriteObjectTransformByBase(
          nds, localBase, transform.PosX, transform.PosY, transform.PosZ,
          transform.PosX, transform.PosY, transform.PosZ, transform.VelX,
          transform.VelY, 0))
    return false;
  nds->ARM9Write32(localBase + 0x80, state.LastStepX);
  nds->ARM9Write32(localBase + 0x84, state.LastStepY);
  nds->ARM9Write32(localBase + 0x88, state.LastStepZ);
  nds->ARM9Write32(localBase + 0xB0, state.VelH);
  nds->ARM9Write32(localBase + 0xB4, state.TargetVelH);
  nds->ARM9Write32(localBase + 0xB8, state.AccelV);
  nds->ARM9Write32(localBase + 0xBC, state.TargetVelV);
  nds->ARM9Write32(localBase + 0xC0, state.AccelH);
  nds->ARM9Write32(localBase + 0xE0, state.TargetVelX);
  nds->ARM9Write32(localBase + 0xE4, state.TargetVelY);
  nds->ARM9Write32(localBase + 0xE8, state.TargetVelZ);
  return true;
}

melonDS::u64 WorldActorMatchDistance(
    const WireProtocol::WireWorldActorState &remoteActor,
    const GameStateReader::ObjectScanSample &localActor) {
  const auto componentDistance = [](melonDS::u32 lhs, melonDS::u32 rhs) {
    const std::int64_t delta =
        static_cast<std::int64_t>(static_cast<std::int32_t>(lhs)) -
        static_cast<std::int64_t>(static_cast<std::int32_t>(rhs));
    return static_cast<melonDS::u64>(delta < 0 ? -delta : delta);
  };
  return componentDistance(remoteActor.PosX, localActor.PosX) +
         componentDistance(remoteActor.PosY, localActor.PosY) +
         componentDistance(remoteActor.PosZ, localActor.PosZ);
}

bool WritePlayerRuntimeStateByBase(
    melonDS::NDS *nds, melonDS::u32 base,
    const WireProtocol::WirePlayerState &state) {
  if (!nds || !nds->MainRAM || base < kMainRAMBase)
    return false;
  const melonDS::u32 offset = base - kMainRAMBase;
  const melonDS::u32 ramLength = nds->MainRAMMask + 1;
  if (offset + kPlayerBaseVisibleFlagOffset + 1 > ramLength)
    return false;
  nds->ARM9Write32(base + kPlayerBaseActionFlagOffset, state.ActionFlag);
  nds->ARM9Write32(base + kPlayerBaseSubActionFlagOffset,
                   state.SubActionFlag);
  nds->ARM9Write32(base + kPlayerBasePhysicsFlagOffset, state.PhysicsFlag);
  nds->ARM9Write32(base + kPlayerBaseTransitionFlagOffset,
                   state.TransitionFlag);
  nds->ARM9Write32(base + kPlayerBaseCollisionFlagOffset,
                   state.CollisionFlag);
  nds->ARM9Write32(base + kPlayerBaseEnvironmentFlagOffset,
                   state.EnvironmentFlag);
  nds->ARM9Write16(base + kPlayerBaseDamageCooldownOffset,
                   static_cast<melonDS::u16>(state.DamageCooldown & 0xFFFFu));
  nds->ARM9Write8(base + kPlayerBaseUpdateLockedOffset,
                  static_cast<melonDS::u8>(state.RuntimeFlags0 & 0xFFu));
  nds->ARM9Write8(
      base + kPlayerBaseCharacterIDOffset,
      static_cast<melonDS::u8>((state.RuntimeFlags0 >> 8) & 0xFFu));
  nds->ARM9Write8(
      base + kPlayerBaseTransitioningFlagOffset,
      static_cast<melonDS::u8>((state.RuntimeFlags0 >> 16) & 0xFFu));
  nds->ARM9Write8(
      base + kPlayerBaseCameraFocusModeOffset,
      static_cast<melonDS::u8>((state.RuntimeFlags0 >> 24) & 0xFFu));
  nds->ARM9Write8(base + kPlayerBaseDefeatedFlagOffset,
                  static_cast<melonDS::u8>(state.RuntimeFlags1 & 0xFFu));
  nds->ARM9Write8(
      base + kPlayerBasePlayerIDOffset,
      static_cast<melonDS::u8>((state.RuntimeFlags1 >> 8) & 0xFFu));
  nds->ARM9Write8(
      base + kPlayerBaseVisibleFlagOffset,
      static_cast<melonDS::u8>((state.RuntimeFlags1 >> 16) & 0xFFu));
  return true;
}

bool WritePlayerMinimalTransitionStateByBase(
    melonDS::NDS *nds, melonDS::u32 base,
    const WireProtocol::WirePlayerState &state) {
  if (!nds || !nds->MainRAM || base < kMainRAMBase)
    return false;
  bool ok = true;
  ok = WriteMainRAMAddrU8IfChanged(
           nds, base + kPlayerBaseDefeatedFlagOffset,
           static_cast<melonDS::u8>(state.RuntimeFlags1 & 0xFFu)) &&
       ok;
  ok = WriteMainRAMAddrU8IfChanged(
           nds, base + kPlayerBaseVisibleFlagOffset,
           static_cast<melonDS::u8>((state.RuntimeFlags1 >> 16) & 0xFFu)) &&
       ok;
  return ok;
}

bool IsPlayerInActorTransition(melonDS::NDS *nds, melonDS::u32 base) {
  if (!nds || !nds->MainRAM || base < kMainRAMBase)
    return false;
  const melonDS::u32 offset = base - kMainRAMBase;
  const melonDS::u32 ramLength = nds->MainRAMMask + 1;
  if (offset + kPlayerBaseTransitionStepOffset + 1 > ramLength)
    return false;
  return nds->ARM9Read8(base + kPlayerBaseTransitionStepOffset) != 1;
}

bool WritePlayerGlobalState(melonDS::NDS *nds,
                            const WireProtocol::WirePlayerState &state) {
  if (!nds || !nds->MainRAM || state.Player > 1)
    return false;
  const melonDS::u32 player = state.Player;
  bool ok = true;
  melonDS::u32 currentDeaths = 0;
  melonDS::u8 currentDead = 0;
  const melonDS::u32 deathsAddress =
      kGamePlayerDeathsAddr + sizeof(melonDS::u32) * player;
  const melonDS::u32 deadAddress = kGamePlayerDeadAddr + player;
  melonDS::u32 deathsOffset = 0;
  melonDS::u32 deadOffset = 0;
  if (MainRAMOffsetFromAddr(nds, deathsAddress, sizeof(currentDeaths),
                           deathsOffset))
    ReadMainRAMU32(nds, deathsOffset, currentDeaths);
  if (MainRAMOffsetFromAddr(nds, deadAddress, sizeof(currentDead), deadOffset))
    ReadMainRAMU8(nds, deadOffset, currentDead);

  const bool deathEvent = state.Dead != 0 || currentDead != 0 ||
                          state.Deaths != currentDeaths;
  const bool starEvent = state.BattleStars != 0 ||
                         state.DisplayedStars != 0 ||
                         state.CollectedStars != 0;
  if (state.Lives != 0)
    ok = WriteMainRAMAddrU32IfChanged(
             nds, kGamePlayerLivesAddr + sizeof(melonDS::u32) * player,
             state.Lives) &&
         ok;
  ok = WriteMainRAMAddrU32IfChanged(
           nds, kGamePlayerCoinsAddr + sizeof(melonDS::u32) * player,
           state.Coins) &&
       ok;
  if (deathEvent) {
    ok = WriteMainRAMAddrU8IfChanged(
             nds, deadAddress, static_cast<melonDS::u8>(state.Dead & 0xFFu)) &&
         ok;
    ok = WriteMainRAMAddrU32IfChanged(nds, deathsAddress, state.Deaths) && ok;
    ok = WriteMainRAMAddrU32IfChanged(
             nds,
             kGamePlayerTransitionStatusAddr +
                 sizeof(melonDS::u32) * player,
             state.TransitionStatus) &&
         ok;
  }
  if (starEvent) {
    ok = WriteMainRAMAddrU32IfChanged(
             nds,
             kGamePlayerBattleStarsAddr + sizeof(melonDS::u32) * player,
             state.BattleStars) &&
         ok;
    ok = WriteMainRAMAddrU32IfChanged(
             nds,
             kGamePlayerDisplayedStarsAddr + sizeof(melonDS::u32) * player,
             state.DisplayedStars) &&
         ok;
    ok = WriteMainRAMAddrU32IfChanged(
             nds,
             kGamePlayerCollectedStarsAddr + sizeof(melonDS::u32) * player,
             state.CollectedStars) &&
         ok;
  }
  return ok;
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

void ApplyWorldActorSnapshotState(
    melonDS::NDS *nds,
    const WireProtocol::WireWorldActorSnapshotState &sample,
    GameStateModel::StateSyncRuntime &runtime,
    const WorldActorSnapshotApplyOptions &options) {
  const std::vector<GameStateReader::WorldActorSnapshotCandidate> localActors =
      GameStateReader::CollectWorldActorSnapshotCandidates(nds);
  const std::size_t remoteCount = std::min(
      static_cast<std::size_t>(sample.Count),
      WireProtocol::kMaxWorldActorSnapshots);
  if (remoteCount == 0 || localActors.empty())
    return;

  std::array<int, WireProtocol::kMaxWorldActorSnapshots> localIndices{};
  std::array<bool, WireProtocol::kMaxWorldActorSnapshots> localUsed{};
  localIndices.fill(-1);
  std::array<melonDS::u32, WireProtocol::kMaxWorldActorSnapshots>
      nextRemoteGUIDs{};
  std::array<melonDS::u32, WireProtocol::kMaxWorldActorSnapshots>
      nextLocalGUIDs{};

  for (std::size_t index = 0; index < remoteCount; index++) {
    const WireProtocol::WireWorldObjectActorState &remoteActor =
        sample.Actors[index];
    for (std::size_t mapIndex = 0;
         mapIndex < WireProtocol::kMaxWorldActorSnapshots; mapIndex++) {
      if (runtime.WorldActorSnapshotRemoteGUIDMaps[options.InstanceID]
                                                    [mapIndex] !=
          remoteActor.Actor.GUID)
        continue;
      const melonDS::u32 localGUID =
          runtime.WorldActorSnapshotLocalGUIDMaps[options.InstanceID]
                                                 [mapIndex];
      for (std::size_t localIndex = 0;
           localIndex < localActors.size() &&
           localIndex < WireProtocol::kMaxWorldActorSnapshots;
           localIndex++) {
        if (!localUsed[localIndex] &&
            localActors[localIndex].Actor.GUID == localGUID &&
            localActors[localIndex].ObjectID == remoteActor.ObjectID &&
            localActors[localIndex].Actor.Settings ==
                remoteActor.Actor.Settings) {
          localIndices[index] = static_cast<int>(localIndex);
          localUsed[localIndex] = true;
          break;
        }
      }
      break;
    }
  }

  for (std::size_t index = 0; index < remoteCount; index++) {
    if (localIndices[index] >= 0)
      continue;
    const WireProtocol::WireWorldObjectActorState &remoteActor =
        sample.Actors[index];
    std::size_t closestIndex = localActors.size();
    melonDS::u64 closestDistance = std::numeric_limits<melonDS::u64>::max();
    for (std::size_t localIndex = 0;
         localIndex < localActors.size() &&
         localIndex < WireProtocol::kMaxWorldActorSnapshots;
         localIndex++) {
      if (localUsed[localIndex])
        continue;
      const GameStateReader::WorldActorSnapshotCandidate &localActor =
          localActors[localIndex];
      if (localActor.ObjectID != remoteActor.ObjectID ||
          localActor.Actor.Settings != remoteActor.Actor.Settings ||
          localActor.Actor.StateType == 0 || localActor.Actor.StateType > 2)
        continue;
      const melonDS::u64 distance =
          WorldActorMatchDistance(remoteActor.Actor, localActor.Actor);
      if (distance < closestDistance ||
          (distance == closestDistance &&
           (closestIndex == localActors.size() ||
            localActor.Actor.GUID <
                localActors[closestIndex].Actor.GUID))) {
        closestIndex = localIndex;
        closestDistance = distance;
      }
    }
    if (closestIndex == localActors.size())
      continue;
    localIndices[index] = static_cast<int>(closestIndex);
    localUsed[closestIndex] = true;
  }

  const melonDS::u32 predictFrames = std::min(
      options.Frame > sample.Frame ? options.Frame - sample.Frame : 0,
      static_cast<melonDS::u32>(std::max(0, options.MaxPredictFrames)));
  melonDS::u32 applied = 0;
  bool mapChanged = false;
  for (std::size_t index = 0; index < remoteCount; index++) {
    if (localIndices[index] < 0)
      continue;
    const WireProtocol::WireWorldObjectActorState &remoteActor =
        sample.Actors[index];
    const GameStateReader::WorldActorSnapshotCandidate &localActor =
        localActors[localIndices[index]];
    if (ApplyWireWorldActorState(nds, remoteActor.Actor, predictFrames,
                                 localActor.Actor.Base)) {
      applied++;
      nextRemoteGUIDs[index] = remoteActor.Actor.GUID;
      nextLocalGUIDs[index] = localActor.Actor.GUID;
    }
  }

  for (std::size_t index = 0;
       index < WireProtocol::kMaxWorldActorSnapshots; index++) {
    mapChanged =
        mapChanged ||
        runtime.WorldActorSnapshotRemoteGUIDMaps[options.InstanceID][index] !=
            nextRemoteGUIDs[index] ||
        runtime.WorldActorSnapshotLocalGUIDMaps[options.InstanceID][index] !=
            nextLocalGUIDs[index];
    runtime.WorldActorSnapshotRemoteGUIDMaps[options.InstanceID][index] =
        nextRemoteGUIDs[index];
    runtime.WorldActorSnapshotLocalGUIDMaps[options.InstanceID][index] =
        nextLocalGUIDs[index];
  }

  if (options.Trace.ShouldTrace(options.Frame)) {
    std::printf(
        "NSMB WorldActors: apply inst=%d frame=%u sampleFrame=%u count=%zu applied=%u predict=%u mapChanged=%d\n",
        options.InstanceID, options.Frame, sample.Frame, remoteCount, applied,
        predictFrames, mapChanged ? 1 : 0);
  }
}

void ApplyWorldState(melonDS::NDS *nds,
                     const WireProtocol::WireWorldState &sample,
                     GameStateModel::StateSyncRuntime &runtime,
                     const WorldStateApplyOptions &options) {
  const melonDS::u32 predictFrames = std::min(
      options.Frame > sample.Frame ? options.Frame - sample.Frame : 0,
      static_cast<melonDS::u32>(std::max(0, options.MaxPredictFrames)));
  bool starApplied = false;
  if (options.Client) {
    const GameStateReader::ObjectScanSample star =
        GameStateReader::GetWorldActorCached(
        options.InstanceID, options.Frame, nds, kVsBattleStarActorObjectID,
        kVsBattleStarActorSettings, runtime.WorldStarActorBaseCache,
        runtime.WorldStarActorGUIDCache, options.ActorRescanInterval);
    starApplied = options.ApplyStarActor && star.StateType == 1 &&
                  sample.Star.StateType == 1 &&
                  ApplyWireWorldActorState(nds, sample.Star, predictFrames,
                                           star.Base);
  }

  if (options.Trace.ShouldTrace(options.Frame)) {
    std::printf(
        "NSMB WorldState: apply inst=%d frame=%u sampleFrame=%u predict=%u star=%d\n",
        options.InstanceID, options.Frame, sample.Frame, predictFrames,
        starApplied ? 1 : 0);
  }
}

void ApplyMovingHazardState(
    melonDS::NDS *nds, const WireProtocol::WireMovingHazardState &sample,
    GameStateModel::StateSyncRuntime &runtime,
    const MovingHazardApplyOptions &options) {
  const std::vector<GameStateReader::ObjectScanSample> localActors =
      GameStateReader::GetWorldMovingHazardsCached(
          options.InstanceID, options.Frame, nds, runtime,
          options.ActorRescanInterval);
  const std::size_t remoteCount = std::min(
      static_cast<std::size_t>(sample.Count),
      WireProtocol::kMaxWorldMovingHazards);
  if (localActors.size() != remoteCount)
    return;

  const melonDS::u32 predictFrames = std::min(
      options.Frame > sample.Frame ? options.Frame - sample.Frame : 0,
      static_cast<melonDS::u32>(std::max(0, options.MaxPredictFrames)));
  std::array<int, WireProtocol::kMaxWorldMovingHazards> localIndices{};
  std::array<bool, WireProtocol::kMaxWorldMovingHazards> localUsed{};
  std::array<melonDS::u32, WireProtocol::kMaxWorldMovingHazards>
      nextRemoteGUIDs{};
  std::array<melonDS::u32, WireProtocol::kMaxWorldMovingHazards>
      nextLocalGUIDs{};
  localIndices.fill(-1);

  for (std::size_t index = 0; index < remoteCount; index++) {
    const WireProtocol::WireWorldActorState &remoteActor =
        sample.Actors[index];
    for (std::size_t mapIndex = 0;
         mapIndex < WireProtocol::kMaxWorldMovingHazards; mapIndex++) {
      if (runtime.WorldMovingHazardRemoteGUIDMaps[options.InstanceID]
                                                 [mapIndex] != remoteActor.GUID)
        continue;
      const melonDS::u32 localGUID =
          runtime.WorldMovingHazardLocalGUIDMaps[options.InstanceID][mapIndex];
      for (std::size_t localIndex = 0; localIndex < localActors.size();
           localIndex++) {
        if (!localUsed[localIndex] &&
            localActors[localIndex].GUID == localGUID) {
          localIndices[index] = static_cast<int>(localIndex);
          localUsed[localIndex] = true;
          break;
        }
      }
      break;
    }
  }

  for (std::size_t index = 0; index < remoteCount; index++) {
    if (localIndices[index] >= 0)
      continue;
    const WireProtocol::WireWorldActorState &remoteActor =
        sample.Actors[index];
    std::size_t closestIndex = localActors.size();
    melonDS::u64 closestDistance = std::numeric_limits<melonDS::u64>::max();
    for (std::size_t localIndex = 0; localIndex < localActors.size();
         localIndex++) {
      if (localUsed[localIndex])
        continue;
      const melonDS::u64 distance =
          WorldActorMatchDistance(remoteActor, localActors[localIndex]);
      if (distance < closestDistance ||
          (distance == closestDistance &&
           (closestIndex == localActors.size() ||
            localActors[localIndex].GUID < localActors[closestIndex].GUID))) {
        closestIndex = localIndex;
        closestDistance = distance;
      }
    }
    if (closestIndex == localActors.size())
      return;
    localIndices[index] = static_cast<int>(closestIndex);
    localUsed[closestIndex] = true;
  }

  bool mapChanged = false;
  for (std::size_t index = 0; index < remoteCount; index++) {
    const GameStateReader::ObjectScanSample &localActor =
        localActors[localIndices[index]];
    const WireProtocol::WireWorldActorState &remoteActor =
        sample.Actors[index];
    nextRemoteGUIDs[index] = remoteActor.GUID;
    nextLocalGUIDs[index] = localActor.GUID;
    mapChanged =
        mapChanged ||
        runtime.WorldMovingHazardRemoteGUIDMaps[options.InstanceID][index] !=
            nextRemoteGUIDs[index] ||
        runtime.WorldMovingHazardLocalGUIDMaps[options.InstanceID][index] !=
            nextLocalGUIDs[index];
    ApplyWireWorldMovingHazardState(nds, remoteActor, predictFrames,
                                    localActor.Base);
  }

  for (std::size_t index = 0;
       index < WireProtocol::kMaxWorldMovingHazards; index++) {
    mapChanged =
        mapChanged ||
        runtime.WorldMovingHazardRemoteGUIDMaps[options.InstanceID][index] !=
            nextRemoteGUIDs[index] ||
        runtime.WorldMovingHazardLocalGUIDMaps[options.InstanceID][index] !=
            nextLocalGUIDs[index];
    runtime.WorldMovingHazardRemoteGUIDMaps[options.InstanceID][index] =
        nextRemoteGUIDs[index];
    runtime.WorldMovingHazardLocalGUIDMaps[options.InstanceID][index] =
        nextLocalGUIDs[index];
  }
  if (mapChanged && options.TraceMapping) {
    std::printf("NSMB WorldHazards: map inst=%d frame=%u count=%zu",
                options.InstanceID, options.Frame, remoteCount);
    for (std::size_t index = 0; index < remoteCount; index++)
      std::printf(" slot%zu=%u/%u", index, nextRemoteGUIDs[index],
                  nextLocalGUIDs[index]);
    std::printf("\n");
  }
}

void ApplyWorldEffectState(
    melonDS::NDS *nds,
    const WireProtocol::WireWorldEffectState &sample) {
  for (std::size_t index = 0;
       index < std::min<std::size_t>(sample.Count,
                                     WireProtocol::kMaxWorldEffects);
       index++) {
    const WireProtocol::WireWorldEffectSlot &remote = sample.Effects[index];
    if (!remote.Found ||
        !IsValidMainRAMRange(nds, remote.Base,
                             kWorldEffectWordEnd + sizeof(melonDS::u32)))
      continue;
    melonDS::u32 localVTable = 0;
    if (!ReadMainRAMAddressU32(nds, remote.Base, localVTable) ||
        (localVTable != kEffectVTablePtr &&
         localVTable != kEffectVTableStart))
      continue;
    for (std::size_t wordIndex = 0;
         wordIndex < WireProtocol::kWorldEffectWordCount; wordIndex++) {
      const melonDS::u32 relativeOffset =
          kWorldEffectWordStart +
          static_cast<melonDS::u32>(wordIndex * sizeof(melonDS::u32));
      const melonDS::u32 address = remote.Base + relativeOffset;
      if ((address & 3u) == 0)
        nds->ARM9Write32(address, remote.Words[wordIndex]);
    }
  }
}

void ApplyPlayerState(melonDS::NDS *nds,
                      const WireProtocol::WirePlayerState &sample,
                      GameStateModel::StateSyncRuntime &runtime,
                      const PlayerStateApplyOptions &options) {
  const bool shouldApplyGlobals =
      options.ApplyGlobals &&
      options.SampleFrame >
          runtime.LastAppliedPlayerGlobalsFrame[options.InstanceID]
                                                   [options.RemotePlayer];
  const bool globalsApplied =
      shouldApplyGlobals && WritePlayerGlobalState(nds, sample);
  if (globalsApplied)
    runtime.LastAppliedPlayerGlobalsFrame[options.InstanceID]
                                               [options.RemotePlayer] =
        options.SampleFrame;
  if (!sample.Found) {
    if (options.Trace.ShouldTrace(options.Frame)) {
      std::printf(
          "NSMB PlayerState: apply globals-only inst=%d frame=%u sampleFrame=%u remotePlayer=%d globals=%d lives=%u deaths=%u dead=%u stars=%u\n",
          options.InstanceID, options.Frame, options.SampleFrame,
          options.RemotePlayer, globalsApplied ? 1 : 0, sample.Lives,
          sample.Deaths, sample.Dead, sample.BattleStars);
    }
    return;
  }

  const GameStateReader::ObjectScanSample localActor =
      GameStateReader::GetPlayerActorCached(
          options.InstanceID, options.RemotePlayer, nds, runtime);
  const melonDS::u32 localBase = localActor.Found ? localActor.Base : 0;
  const bool transitionMinimalApply =
      options.ApplyGlobals && localActor.Found &&
      IsPlayerInActorTransition(nds, localBase);
  const melonDS::u32 predictFrames = std::min(
      options.Frame - options.SampleFrame,
      static_cast<melonDS::u32>(std::max(0, options.MaxPredictFrames)));
  const bool transformApplied =
      !transitionMinimalApply &&
      WriteObjectTransformByBase(
          nds, localBase, sample.PosX + sample.VelX * predictFrames,
          sample.PosY + sample.VelY * predictFrames,
          sample.PosZ + sample.VelZ * predictFrames,
          sample.PrevX + sample.VelX * predictFrames,
          sample.PrevY + sample.VelY * predictFrames,
          sample.PrevZ + sample.VelZ * predictFrames, sample.VelX,
          sample.VelY, sample.VelZ);
  const bool runtimeApplied =
      transitionMinimalApply
          ? WritePlayerMinimalTransitionStateByBase(nds, localBase, sample)
          : (transformApplied &&
             WritePlayerRuntimeStateByBase(nds, localBase, sample));

  if (options.Trace.ShouldTrace(options.Frame)) {
    std::printf(
        "NSMB PlayerState: apply inst=%d frame=%u sampleFrame=%u remotePlayer=%d predict=%u transitionMin=%d transform=%d runtime=%d globals=%d pos=%08X/%08X vel=%08X/%08X lives=%u deaths=%u dead=%u stars=%u\n",
        options.InstanceID, options.Frame, options.SampleFrame,
        options.RemotePlayer, predictFrames, transitionMinimalApply ? 1 : 0,
        transformApplied ? 1 : 0, runtimeApplied ? 1 : 0,
        globalsApplied ? 1 : 0, sample.PosX, sample.PosY, sample.VelX,
        sample.VelY, sample.Lives, sample.Deaths, sample.Dead,
        sample.BattleStars);
  }
}

} // namespace NsmbNetplayPoC::GameStateWriter
