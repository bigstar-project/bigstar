#include "NsmbGameStateWriter.h"

#include "NDS.h"

#include <cstdint>
#include <cstring>

namespace NsmbNetplayPoC::GameStateWriter {

namespace {

constexpr melonDS::u32 kMainRAMBase = 0x02000000;
constexpr melonDS::u32 kNetRandomBranchAddressAddr = 0x0208885C;
constexpr melonDS::u32 kNetRandomCallCountAddr = 0x02088A48;
constexpr melonDS::u32 kNetRandomValueAddr = 0x02088A68;
constexpr melonDS::u32 kGamePlayerDeadAddr = 0x0208B328;
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
constexpr melonDS::u16 kVsBattleStarActorObjectID = 0x0022;
constexpr melonDS::u32 kVsBattleStarActorSettings = 0x00000001;
constexpr melonDS::u16 kStageSceneObjectID = 0x0003;
constexpr melonDS::u16 kStageCameraObjectID = 0x013C;

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

} // namespace NsmbNetplayPoC::GameStateWriter
