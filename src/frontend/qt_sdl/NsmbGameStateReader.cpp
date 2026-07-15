#include "NsmbGameStateReader.h"
#include "NsmbNetplayDiagnostics.h"

#include "NDS.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <set>

namespace NsmbNetplayPoC::GameStateReader {

namespace {

constexpr melonDS::u32 kMainRAMBase = 0x02000000;
constexpr melonDS::u16 kPlayerObjectID = 0x0015;
constexpr melonDS::u16 kVsBattleStarActorObjectID = 0x0022;
constexpr melonDS::u32 kVsBattleStarActorSettings = 0x00000001;
constexpr melonDS::u16 kVsBattleStarCandidateObjectID = 0x010C;
constexpr melonDS::u16 kVsWorldItemObjectID = 0x001F;
constexpr melonDS::u32 kVsNeutralWorldItemSettings = 0x00080000;
constexpr melonDS::u32 kVsWorldItemSettings = 0x00080002;
constexpr melonDS::u32 kVsDroppedStarItemSettings = 0x00090002;
constexpr melonDS::u16 kVsMovingHazardObjectID = 0x0053;
constexpr melonDS::u32 kVsMovingHazardSettings = 0x00000000;
constexpr melonDS::u16 kStageSceneObjectID = 0x0003;
constexpr melonDS::u16 kStageFXObjectID = 0x0012;
constexpr melonDS::u32 kStageSceneUpdateDispatchTableAddr = 0x020CA378;
constexpr melonDS::u32 kStageSceneRenderDispatchTableAddr = 0x020CA398;
constexpr melonDS::u16 kStageActorManagerObjectID = 0x012F;
constexpr melonDS::u16 kStageControllerObjectID = 0x0130;
constexpr melonDS::u16 kMvlObject267ID = 0x010B;
constexpr melonDS::u16 kVsConnectObjectID = 0x0006;
constexpr melonDS::u16 kCourseSelectObjectID = 0x0005;
constexpr melonDS::u16 kStageCameraObjectID = 0x013C;
constexpr melonDS::u32 kNSMBProcessExecuteListAddr = 0x0208FB18;
constexpr melonDS::u32 kNSMBProcessDeleteListAddr = 0x0208FB28;
constexpr melonDS::u32 kNSMBProcessRenderListAddr = 0x0208FB38;
constexpr melonDS::u32 kNSMBProcessCreateListAddr = 0x0208FB48;
constexpr melonDS::u32 kNSMBProcessIDLookupListsAddr = 0x0208FB58;

constexpr melonDS::u32 kGameWrapXAddr = 0x02085AA4;
constexpr melonDS::u32 kStageLayoutPtrAddr = 0x020CAD40;
constexpr melonDS::u32 kStageLayoutChunkPtrTableAddr = 0x020CAFE0;
constexpr melonDS::u32 kStageLayoutTileBehaviorBaseTableAddr = 0x020C8484;
constexpr melonDS::u32 kStageLayoutDynamicTileBehaviorTablePtrAddr = 0x020CAD28;
constexpr melonDS::u32 kStageBlocksPtrAddr = 0x0208B168;
constexpr melonDS::u32 kStageLayoutChunkMapOffset = 0x64;
constexpr melonDS::u32 kStageLayoutWrapMaskOffset = 0x470;
constexpr melonDS::u32 kStageLayoutPlayerDataOffset = 0x400;
constexpr melonDS::u32 kStageLayoutPlayerDataStride = 0x0C;
constexpr melonDS::u32 kStageLayoutPlayerCameraWrapOffset = 0x94;
constexpr melonDS::u32 kStageLayoutCameraWrapAddOffset = 0xA8E8;
constexpr melonDS::u32 kPlayerStageActorCollisionMgrOffset = 0x1D0;
constexpr melonDS::u32 kPlayerHitboxCenterOffsetX = 0x960;
constexpr melonDS::u32 kPlayerHitboxCenterOffsetY = 0x964;
constexpr melonDS::u32 kStageActorHitboxHalfWidthOffset = 0x13C;
constexpr melonDS::u32 kStageActorHitboxHalfHeightOffset = 0x140;
constexpr melonDS::u32 kCollisionMgrBottomSensorPtrOffset = 0x08;
constexpr melonDS::u32 kCollisionMgrTopSensorPtrOffset = 0x0C;
constexpr melonDS::u32 kCollisionMgrSideSensorPtrOffset = 0x10;
constexpr melonDS::u32 kCollisionMgrLineSensorPtrOffset = 0x14;
constexpr melonDS::u32 kCollisionMgrDeltaXOffset = 0x74;
constexpr melonDS::u32 kCollisionMgrDeltaYOffset = 0x78;
constexpr melonDS::u32 kCollisionMgrCollisionResultOffset = 0x7C;
constexpr melonDS::u32 kCollisionMgrGroundCollisionOffset = 0x80;
constexpr melonDS::u32 kCollisionMgrAttachedTileXOffset = 0x90;
constexpr melonDS::u32 kCollisionMgrAttachedTileYOffset = 0x92;
constexpr melonDS::u32 kCollisionMgrBottomModifierTileTypeOffset = 0x98;
constexpr melonDS::u32 kCollisionMgrBottomSlopeTypeOffset = 0x9A;
constexpr melonDS::u32 kCollisionMgrTopModifierTileTypeOffset = 0x9C;
constexpr melonDS::u32 kCollisionMgrTopSlopeTypeOffset = 0x9E;
constexpr melonDS::u32 kCollisionMgrSideModifierTileTypeOffset = 0xA0;
constexpr melonDS::u32 kCollisionMgrByteA4Offset = 0xA4;
constexpr melonDS::u32 kCollisionMgrByteA5Offset = 0xA5;
constexpr melonDS::u32 kCollisionMgrPreviousByteA4Offset = 0xA6;
constexpr melonDS::u32 kCollisionMgrPreviousByteA5Offset = 0xA7;
constexpr melonDS::u32 kCollisionMgrFlagsA8Offset = 0xA8;
constexpr melonDS::u32 kCollisionMgrTileByteABOffset = 0xAB;
constexpr melonDS::u32 kCollisionMgrModifierStateOffset = 0xB0;
constexpr melonDS::u32 kCollisionMgrUnknownB1Offset = 0xB1;
constexpr melonDS::u32 kPlayerBasePlayerIDOffset = 0x7B4;
constexpr melonDS::u32 kPlayerBaseActionFlagOffset = 0x778;
constexpr melonDS::u32 kPlayerBaseSubActionFlagOffset = 0x77C;
constexpr melonDS::u32 kPlayerBasePhysicsFlagOffset = 0x780;
constexpr melonDS::u32 kPlayerBaseTransitionFlagOffset = 0x784;
constexpr melonDS::u32 kPlayerBaseCollisionFlagOffset = 0x788;
constexpr melonDS::u32 kPlayerBaseEnvironmentFlagOffset = 0x790;
constexpr melonDS::u32 kPlayerBaseLinkedActorOffset = 0x688;
constexpr melonDS::u32 kPlayerBaseDamageCooldownOffset = 0x79C;
constexpr melonDS::u32 kPlayerBaseUpdateLockedOffset = 0x7A8;
constexpr melonDS::u32 kPlayerBaseCharacterIDOffset = 0x7AA;
constexpr melonDS::u32 kPlayerBaseTransitioningFlagOffset = 0x7B0;
constexpr melonDS::u32 kPlayerBaseCameraFocusModeOffset = 0x7B2;
constexpr melonDS::u32 kPlayerBaseDefeatedFlagOffset = 0x7B3;
constexpr melonDS::u32 kPlayerBaseVisibleFlagOffset = 0x7B5;
constexpr melonDS::u32 kPlayerBaseTransitionStepOffset = 0xBAD;
constexpr melonDS::u32 kPlayerBaseTransitFuncOffset = 0x990;
constexpr melonDS::u32 kPlayerBaseTransitArgOffset = 0x994;
constexpr melonDS::u32 kEffectVTableStart = 0x02126A24;
constexpr melonDS::u32 kEffectVTablePtr = 0x02126A2C;
constexpr melonDS::u32 kWorldEffectSlotBase = 0x021C3268;
constexpr melonDS::u32 kWorldEffectSlotStride = 0x1D4;
constexpr melonDS::u32 kWorldEffectSlotCount = 32;
constexpr melonDS::u32 kWorldEffectWordStart = 0x04;
constexpr melonDS::u32 kWorldEffectWordEnd = 0xAC;
constexpr melonDS::u32 kGamePlayerPowerupAddr = 0x0208B324;
constexpr melonDS::u32 kGamePlayerDeadAddr = 0x0208B328;
constexpr melonDS::u32 kGamePlayerInventoryPowerupAddr = 0x0208B32C;
constexpr melonDS::u32 kGamePlayerCharacterAddr = 0x0208B330;
constexpr melonDS::u32 kGamePlayerTransitionStatusAddr = 0x0208B354;
constexpr melonDS::u32 kGamePlayerCountAddr = 0x0208B348;
constexpr melonDS::u32 kGamePlayerLivesAddr = 0x0208B364;
constexpr melonDS::u32 kGamePlayerBattleStarsAddr = 0x0208B36C;
constexpr melonDS::u32 kGamePlayerCoinsAddr = 0x0208B37C;
constexpr melonDS::u32 kGamePlayerScoreAddr = 0x0208B384;
constexpr melonDS::u32 kGamePlayerDisplayedStarsAddr = 0x0208B38C;
constexpr melonDS::u32 kGamePlayerDeathsAddr = 0x0208B394;
constexpr melonDS::u32 kGamePlayerCollectedStarsAddr = 0x0208B39C;
using GameStateModel::AIPlayerTileProbeSample;
using GameStateModel::AITileGridSample;
using GameStateModel::AITileProbeSample;
using GameStateModel::kAITileGridHeight;
using GameStateModel::kAITileGridMinRelX;
using GameStateModel::kAITileGridMinRelY;
using GameStateModel::kAITileGridWidth;
using GameStateModel::kAITileProbeCount;
using GameStateModel::kObjectTraceSlots;
using GameStateModel::PlayerCollisionMgrSample;
using GameStateModel::PlayerHitboxSample;

thread_local const GameStateObjectScanCache *ActiveGameStateObjectScanCache =
    nullptr;

bool IsARM9MainRAMAddress(melonDS::u32 address) {
  return (address & 0xFF000000u) == 0x02000000u;
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

bool ReadMainRAMAddressU16(melonDS::NDS *nds, melonDS::u32 address,
                           melonDS::u16 &value) {
  if (!IsValidMainRAMRange(nds, address, sizeof(value)))
    return false;
  std::memcpy(&value, nds->MainRAM + (address - kMainRAMBase), sizeof(value));
  return true;
}

bool ReadMainRAMU32(melonDS::NDS *nds, melonDS::u32 offset,
                    melonDS::u32 &value) {
  if (!nds || !nds->MainRAM || offset > nds->MainRAMMask ||
      sizeof(value) > nds->MainRAMMask + 1 - offset)
    return false;
  std::memcpy(&value, nds->MainRAM + offset, sizeof(value));
  return true;
}

bool ReadMainRAMU16(melonDS::NDS *nds, melonDS::u32 offset,
                    melonDS::u16 &value) {
  if (!nds || !nds->MainRAM || offset > nds->MainRAMMask ||
      sizeof(value) > nds->MainRAMMask + 1 - offset)
    return false;
  std::memcpy(&value, nds->MainRAM + offset, sizeof(value));
  return true;
}

bool ReadMainRAMU8(melonDS::NDS *nds, melonDS::u32 offset, melonDS::u8 &value) {
  if (!nds || !nds->MainRAM || offset > nds->MainRAMMask)
    return false;
  value = nds->MainRAM[offset];
  return true;
}

std::int32_t SignedARM9U32(melonDS::u32 value) {
  return static_cast<std::int32_t>(value);
}

bool ReadStageLayoutTileBehavior(melonDS::NDS *nds, melonDS::u32 worldX,
                                 melonDS::u32 worldY, melonDS::u32 playerID,
                                 AITileProbeSample &out) {
  out.WorldX = worldX;
  out.WorldY = worldY;
  if (!nds || !nds->MainRAM) {
    out.Status = 1;
    return false;
  }

  const melonDS::u32 stageLayout = nds->ARM9Read32(kStageLayoutPtrAddr);
  const melonDS::u32 wrapX = nds->ARM9Read32(kGameWrapXAddr);
  out.StageLayout = stageLayout;
  if (!IsValidMainRAMRange(nds, stageLayout,
                           kStageLayoutCameraWrapAddOffset +
                               sizeof(melonDS::u16))) {
    out.Status = 2;
    return false;
  }

  std::int32_t pixelXSigned = SignedARM9U32(worldX) >> 12;
  const std::int32_t pixelYSigned = (-SignedARM9U32(worldY)) >> 12;
  if (wrapX != 0)
    pixelXSigned &= static_cast<std::int32_t>(wrapX >> 12);
  if (pixelYSigned < 0) {
    out.Status = 3;
    return false;
  }

  melonDS::u32 pixelX = static_cast<melonDS::u32>(pixelXSigned) & 0xFFFFu;
  const melonDS::u32 pixelY = static_cast<melonDS::u32>(pixelYSigned) & 0xFFFFu;
  const melonDS::u32 playerOffset =
      kStageLayoutPlayerDataOffset +
      (playerID & 1u) * kStageLayoutPlayerDataStride +
      kStageLayoutPlayerCameraWrapOffset;
  if (nds->ARM9Read16(stageLayout + playerOffset) == 0xFF00)
    pixelX = (pixelX +
              nds->ARM9Read16(stageLayout + kStageLayoutCameraWrapAddOffset)) &
             0xFFFFu;

  const melonDS::u32 stageBlocks = nds->ARM9Read32(kStageBlocksPtrAddr);
  if (IsValidMainRAMRange(nds, stageBlocks, 4) &&
      (nds->ARM9Read16(stageBlocks + 2) & 0x20) != 0)
    pixelX &= nds->ARM9Read16(stageLayout + kStageLayoutWrapMaskOffset);

  out.PixelX = pixelX;
  out.PixelY = pixelY;
  if (pixelX >= 0x2000 || pixelY >= 0x1000) {
    out.Status = 4;
    return false;
  }

  const melonDS::u32 chunkIndex = (pixelX >> 8) + ((pixelY >> 8) << 5);
  const melonDS::u32 chunkID =
      nds->ARM9Read8(stageLayout + kStageLayoutChunkMapOffset + chunkIndex);
  out.ChunkID = chunkID;
  const melonDS::u32 chunkPtr = nds->ARM9Read32(kStageLayoutChunkPtrTableAddr +
                                                chunkID * sizeof(melonDS::u32));
  out.ChunkPtr = chunkPtr;
  if (!IsValidMainRAMRange(nds, chunkPtr, 0x200)) {
    out.Status = 5;
    return false;
  }

  const melonDS::u32 tileOffset =
      (((pixelX & 0xF0u) >> 4) << 1) + ((pixelY & 0xF0u) << 1);
  const melonDS::u32 tileID = nds->ARM9Read16(chunkPtr + tileOffset);
  out.TileID = tileID;
  melonDS::u32 behavior = 0;
  melonDS::u32 behaviorTable = 0;
  if (tileID < 0x100) {
    behaviorTable = kStageLayoutTileBehaviorBaseTableAddr;
    out.BehaviorTable = behaviorTable;
    behavior = nds->ARM9Read32(behaviorTable + tileID * sizeof(melonDS::u32));
  } else if (tileID < 0x600) {
    behaviorTable =
        nds->ARM9Read32(kStageLayoutDynamicTileBehaviorTablePtrAddr);
    out.BehaviorTable = behaviorTable;
    const melonDS::u32 behaviorOffset = (tileID - 0x100) * sizeof(melonDS::u32);
    if (!IsValidMainRAMRange(nds, behaviorTable + behaviorOffset,
                             sizeof(melonDS::u32))) {
      out.Status = 6;
      return false;
    }
    behavior = nds->ARM9Read32(behaviorTable + behaviorOffset);
  } else {
    out.Status = 7;
    return false;
  }

  out.Found = 1;
  out.Status = 0;
  out.StageLayout = stageLayout;
  out.ChunkPtr = chunkPtr;
  out.BehaviorTable = behaviorTable;
  out.Behavior = behavior;
  return true;
}

} // namespace

melonDS::u64 HashNDS(melonDS::NDS *nds) {
  melonDS::u64 hash = 1469598103934665603ull;
  const auto mix = [&](melonDS::u64 value) {
    for (int index = 0; index < 8; index++) {
      hash ^= (value >> (index * 8)) & 0xFF;
      hash *= 1099511628211ull;
    }
  };

  mix(nds->NumFrames);
  mix(nds->ARM9Timestamp);
  mix(nds->ARM7Timestamp);
  mix(nds->KeyInput);

  if (nds->MainRAM) {
    const melonDS::u32 length =
        std::min<melonDS::u32>(nds->MainRAMMask + 1, 0x400000);
    for (melonDS::u32 offset = 0; offset < length; offset++) {
      hash ^= nds->MainRAM[offset];
      hash *= 1099511628211ull;
    }
  }
  return hash;
}

melonDS::u64 HashFramebuffers(melonDS::NDS *nds) {
  void *topBuffer = nullptr;
  void *bottomBuffer = nullptr;
  if (!nds || !nds->GPU.GetFramebuffers(&topBuffer, &bottomBuffer) ||
      !topBuffer || !bottomBuffer)
    return 0;

  melonDS::u64 hash = 1469598103934665603ull;
  const auto mixBytes = [&](const void *data, std::size_t length) {
    const auto *bytes = reinterpret_cast<const melonDS::u8 *>(data);
    for (std::size_t index = 0; index < length; index++) {
      hash ^= bytes[index];
      hash *= 1099511628211ull;
    }
  };

  mixBytes(topBuffer, 256 * 192 * 4);
  mixBytes(bottomBuffer, 256 * 192 * 4);
  return hash;
}

melonDS::u64 HashMainRAMRange(melonDS::NDS *nds, melonDS::u32 address,
                              melonDS::u32 length) {
  if (!nds || !nds->MainRAM || address < kMainRAMBase)
    return 0;

  const melonDS::u32 offset = address - kMainRAMBase;
  const melonDS::u32 ramLength = nds->MainRAMMask + 1;
  if (offset >= ramLength)
    return 0;

  length = std::min(length, ramLength - offset);
  melonDS::u64 hash = 1469598103934665603ull;
  for (melonDS::u32 index = 0; index < length; index++) {
    hash ^= nds->MainRAM[offset + index];
    hash *= 1099511628211ull;
  }
  return hash;
}

void ReadCoreState(melonDS::NDS *nds, GameStateModel::GameStateSample &sample) {
  sample.StageID = nds->ARM9Read32(0x02085A14);
  sample.StageGroup = nds->ARM9Read32(0x02085A18);
  sample.VsMode = nds->ARM9Read32(0x02085A84);
  sample.LocalPlayerID = nds->ARM9Read32(0x02085A7C);
  sample.Arm9PC = nds->ARM9.R[15] - ((nds->ARM9.CPSR & 0x20) ? 2 : 4);
  sample.Arm9LR = nds->ARM9.R[14];
  sample.Arm9SP = nds->ARM9.R[13];
  sample.Arm9CPSR = nds->ARM9.CPSR;
  sample.AppFrameLength = nds->ARM9Read8(0x02039810);
  sample.AppUpdateTask = nds->ARM9Read32(0x02039824);
  sample.AppSleepPhase = nds->ARM9Read8(0x0208596C);
  sample.AppSleepControl = nds->ARM9Read8(0x02085974);
  sample.AppSleeping = nds->ARM9Read8(0x02085978);
  sample.AppSleepPhaseTimer = nds->ARM9Read16(0x0208597C);
  sample.AppSleepWakeUpTimer = nds->ARM9Read16(0x02085980);
  sample.AppBootParam = nds->ARM9Read32(0x0208598C);
  sample.AppBootTarget = nds->ARM9Read32(0x02085990);
  sample.AppBootScene = nds->ARM9Read32(0x02085994);
  sample.GGID = nds->ARM9Read32(0x02088858);
  sample.NetCurrentLanguage = nds->ARM9Read32(0x020887E8);
  sample.NetLocalAid = nds->ARM9Read32(0x020887F0);
  sample.NetState14 = nds->ARM9Read32(0x020887FC);
  sample.NetState1C = nds->ARM9Read32(0x02088804);
  sample.NetState20 = nds->ARM9Read32(0x02088808);
  sample.NetState24 = nds->ARM9Read32(0x0208880C);
  sample.NetExpectedConsoleCount = nds->ARM9Read32(0x0208880C);
  sample.NetMultiBootSession = nds->ARM9Read32(0x02088810);
  sample.NetSessionState = nds->ARM9Read32(0x02088814);
  sample.NetModuleState = nds->ARM9Read32(0x02088818);
  sample.NetMaxSessionChildren = nds->ARM9Read32(0x0208881C);
  sample.NetMaxConsoleCount = nds->ARM9Read32(0x0208882C);
  sample.NetState5C = nds->ARM9Read16(0x0208883C);
  sample.NetPacketTick = nds->ARM9Read16(0x020888E0);
  sample.NetPacketKeys = nds->ARM9Read16(0x020888E2);
  sample.NetPacketAction = nds->ARM9Read8(0x020888E4);
  sample.NetPacketByte5 = nds->ARM9Read8(0x020888E5);
  sample.NetPacketByte6 = nds->ARM9Read8(0x020888E6);
  sample.NetPacketByte7 = nds->ARM9Read8(0x020888E7);
  sample.NetRandomValue = nds->ARM9Read32(0x02088A68);
  sample.NetRandomCallCount = nds->ARM9Read8(0x02088A48);
  sample.NetRandomBranchAddress = nds->ARM9Read32(0x0208885C);
  sample.InputConsole0Held = nds->ARM9Read16(0x02087650);
  sample.InputConsole0Pressed = nds->ARM9Read16(0x02087652);
  sample.InputConsole1Held = nds->ARM9Read16(0x02087654);
  sample.InputConsole1Pressed = nds->ARM9Read16(0x02087656);
  sample.InputPlayer0Held = nds->ARM9Read16(0x02087660);
  sample.InputPlayer1Held = nds->ARM9Read16(0x02087662);
  sample.InputPlayer0Pressed = nds->ARM9Read16(0x02087664);
  sample.InputPlayer1Pressed = nds->ARM9Read16(0x02087666);
  sample.StageActorFreezeFlag = nds->ARM9Read8(0x020CA28C);
  sample.SceneIsSceneActive = nds->ARM9Read32(0x0203BD28);
  sample.ScenePreviousSceneID = nds->ARM9Read16(0x0203BD2C);
  sample.SceneNextSceneID = nds->ARM9Read16(0x0203BD30);
  sample.SceneCurrentSceneID = nds->ARM9Read16(0x0203BD34);
  sample.SceneNextSceneSettings = nds->ARM9Read32(0x02088F38);
}

void ReadPlayerAndCameraGlobals(melonDS::NDS *nds,
                                GameStateModel::GameStateSample &sample) {
  sample.PlayerCount = nds->ARM9Read32(0x0208B348);
  sample.PlayerTransitionStatus0 = nds->ARM9Read32(0x0208B354);
  sample.PlayerTransitionStatus1 = nds->ARM9Read32(0x0208B358);
  sample.EntranceSpawnID0 = nds->ARM9Read8(0x0208B094);
  sample.EntranceSpawnID1 = nds->ARM9Read8(0x0208B095);
  sample.EntranceTransitionFlags0 = nds->ARM9Read8(0x0208B098);
  sample.EntranceTransitionFlags1 = nds->ARM9Read8(0x0208B099);
  sample.EntranceSpawnPtr0 = nds->ARM9Read32(0x0208B0A0);
  sample.EntranceSpawnPtr1 = nds->ARM9Read32(0x0208B0A4);
  sample.Player0Powerup = nds->ARM9Read8(0x0208B324);
  sample.Player1Powerup = nds->ARM9Read8(0x0208B325);
  sample.Player0InventoryPowerup = nds->ARM9Read8(0x0208B32C);
  sample.Player1InventoryPowerup = nds->ARM9Read8(0x0208B32D);
  sample.Player0DamageGuardTimer = nds->ARM9Read16(0x0208B344);
  sample.Player1DamageGuardTimer = nds->ARM9Read16(0x0208B346);
  sample.Player0Dead = nds->ARM9Read8(0x0208B328);
  sample.Player1Dead = nds->ARM9Read8(0x0208B329);
  sample.Player0Character = nds->ARM9Read8(0x0208B330);
  sample.Player1Character = nds->ARM9Read8(0x0208B331);
  sample.Player0Lives = nds->ARM9Read32(0x0208B364);
  sample.Player1Lives = nds->ARM9Read32(0x0208B368);
  sample.Player0BattleStars = nds->ARM9Read32(0x0208B36C);
  sample.Player1BattleStars = nds->ARM9Read32(0x0208B370);
  sample.Player0Coins = nds->ARM9Read32(0x0208B37C);
  sample.Player1Coins = nds->ARM9Read32(0x0208B380);
  sample.Player0Score = nds->ARM9Read32(0x0208B384);
  sample.Player1Score = nds->ARM9Read32(0x0208B388);
  sample.Player0DisplayedStars = nds->ARM9Read32(0x0208B38C);
  sample.Player1DisplayedStars = nds->ARM9Read32(0x0208B390);
  sample.Player0Deaths = nds->ARM9Read32(0x0208B394);
  sample.Player1Deaths = nds->ARM9Read32(0x0208B398);
  sample.Player0CollectedStars = nds->ARM9Read32(0x0208B39C);
  sample.Player1CollectedStars = nds->ARM9Read32(0x0208B3A0);
  sample.VsCoinCount = nds->ARM9Read32(0x0208B37C);

  sample.StageCameraGlobalX0 = nds->ARM9Read32(0x020CAE1C);
  sample.StageCameraGlobalX1 = nds->ARM9Read32(0x020CAE20);
  sample.StageCameraGlobalY0 = nds->ARM9Read32(0x020CAD94);
  sample.StageCameraGlobalY1 = nds->ARM9Read32(0x020CAD98);
  sample.StageCameraGlobalWidth0 = nds->ARM9Read32(0x020CADA4);
  sample.StageCameraGlobalWidth1 = nds->ARM9Read32(0x020CADA8);
  sample.StageCameraGlobalHeight0 = nds->ARM9Read32(0x020CAD8C);
  sample.StageCameraGlobalHeight1 = nds->ARM9Read32(0x020CAD90);
  sample.PlayerCameraFocusPosX0 = nds->ARM9Read32(0x020CAEBC);
  sample.PlayerCameraFocusPosX1 = nds->ARM9Read32(0x020CAECC);
  sample.PlayerCameraFocusPosY0 = nds->ARM9Read32(0x020CAEC0);
  sample.PlayerCameraFocusPosY1 = nds->ARM9Read32(0x020CAED0);
  sample.PlayerCameraFocusPosZ0 = nds->ARM9Read32(0x020CAEC4);
  sample.PlayerCameraFocusPosZ1 = nds->ARM9Read32(0x020CAED4);
  sample.PlayerCameraFocusVelX0 = nds->ARM9Read32(0x020CAEEC);
  sample.PlayerCameraFocusVelX1 = nds->ARM9Read32(0x020CAEFC);
  sample.PlayerCameraFocusVelY0 = nds->ARM9Read32(0x020CAEF0);
  sample.PlayerCameraFocusVelY1 = nds->ARM9Read32(0x020CAF00);
  sample.PlayerCameraFocusVelZ0 = nds->ARM9Read32(0x020CAEF4);
  sample.PlayerCameraFocusVelZ1 = nds->ARM9Read32(0x020CAF04);
  sample.StageDisplayCameraX = nds->ARM9Read32(0x02085AB4);
  sample.CameraDbgCA880 = nds->ARM9Read32(0x020CA880);
  sample.CameraDbgCAE04 = nds->ARM9Read32(0x020CAE04);
  sample.CameraDbgCAE14 = nds->ARM9Read32(0x020CAE14);
  sample.CameraDbgCAD6C = nds->ARM9Read32(0x020CAD6C);
  sample.CameraDbgCAD8C = nds->ARM9Read32(0x020CAD8C);
  sample.CameraDbgCADB4 = nds->ARM9Read32(0x020CADB4);
  sample.CameraDbgCAE60 = nds->ARM9Read32(0x020CAE60);
  sample.CameraDbgCAE64 = nds->ARM9Read32(0x020CAE64);
}

void CopyPlayerActor(const ObjectScanSample &actor,
                     GameStateModel::GameStateSample &sample, int player) {
  if (player == 0) {
    sample.PlayerActor0Found = actor.Found;
    sample.PlayerActor0GUID = actor.GUID;
    sample.PlayerActor0Base = actor.Base;
    sample.PlayerActor0Settings = actor.Settings;
    sample.PlayerActor0StateType = actor.StateType;
    sample.PlayerActor0Flags = actor.Flags;
    sample.PlayerActor0PosX = actor.PosX;
    sample.PlayerActor0PosY = actor.PosY;
    sample.PlayerActor0PosZ = actor.PosZ;
    sample.PlayerActor0PrevX = actor.PrevX;
    sample.PlayerActor0PrevY = actor.PrevY;
    sample.PlayerActor0PrevZ = actor.PrevZ;
    sample.PlayerActor0VelX = actor.VelX;
    sample.PlayerActor0VelY = actor.VelY;
    sample.PlayerActor0VelZ = actor.VelZ;
    return;
  }
  sample.PlayerActor1Found = actor.Found;
  sample.PlayerActor1GUID = actor.GUID;
  sample.PlayerActor1Base = actor.Base;
  sample.PlayerActor1Settings = actor.Settings;
  sample.PlayerActor1StateType = actor.StateType;
  sample.PlayerActor1Flags = actor.Flags;
  sample.PlayerActor1PosX = actor.PosX;
  sample.PlayerActor1PosY = actor.PosY;
  sample.PlayerActor1PosZ = actor.PosZ;
  sample.PlayerActor1PrevX = actor.PrevX;
  sample.PlayerActor1PrevY = actor.PrevY;
  sample.PlayerActor1PrevZ = actor.PrevZ;
  sample.PlayerActor1VelX = actor.VelX;
  sample.PlayerActor1VelY = actor.VelY;
  sample.PlayerActor1VelZ = actor.VelZ;
}

void ReadPlayerTileDamage(melonDS::NDS *nds, const ObjectScanSample &actor,
                          GameStateModel::GameStateSample &sample, int player) {
  if (!actor.Found || !IsValidMainRAMRange(nds, actor.Base + 0xBB3, 1))
    return;
  const melonDS::u32 flags = nds->ARM9Read8(actor.Base + 0xBB2);
  const melonDS::u32 type = nds->ARM9Read8(actor.Base + 0xBB3);
  if (player == 0) {
    sample.PlayerActor0TileDamageFlags = flags;
    sample.PlayerActor0TileDamageType = type;
  } else {
    sample.PlayerActor1TileDamageFlags = flags;
    sample.PlayerActor1TileDamageType = type;
  }
}

void ReadPlayerTransitionState(melonDS::NDS *nds, const ObjectScanSample &actor,
                               GameStateModel::GameStateSample &sample,
                               int player) {
  if (!actor.Found || !IsARM9MainRAMAddress(actor.Base))
    return;

  const melonDS::u32 playerID = nds->ARM9Read8(actor.Base + 0x11E);
  const melonDS::u32 transitionStep = nds->ARM9Read8(actor.Base + 0xBAD);
  const melonDS::u32 signalLock = nds->ARM9Read8(actor.Base + 0x75C);
  const melonDS::u32 flag192 = nds->ARM9Read8(actor.Base + 0x192);
  const melonDS::u32 flags728 = nds->ARM9Read32(actor.Base + 0x728);
  const melonDS::u32 flags72C = nds->ARM9Read32(actor.Base + 0x72C);
  const melonDS::u32 flags730 = nds->ARM9Read32(actor.Base + 0x730);
  const melonDS::u32 actionFlag = nds->ARM9Read32(actor.Base + 0x778);
  const melonDS::u32 subActionFlag = nds->ARM9Read32(actor.Base + 0x77C);
  const melonDS::u32 physicsFlag = nds->ARM9Read32(actor.Base + 0x780);
  const melonDS::u32 damageCooldown = nds->ARM9Read16(actor.Base + 0x79C);
  const melonDS::u32 damageState = nds->ARM9Read8(actor.Base + 0x7A9);
  const melonDS::u32 powerupAuxState = nds->ARM9Read8(actor.Base + 0x7AA);
  const melonDS::u32 powerupState = nds->ARM9Read8(actor.Base + 0x7AB);
  const melonDS::u32 powerupFormState = nds->ARM9Read8(actor.Base + 0x7AC);
  const melonDS::u32 powerupSubState = nds->ARM9Read8(actor.Base + 0x7AD);
  const melonDS::u32 damageGuardFlag = nds->ARM9Read8(actor.Base + 0x7C1);
  const melonDS::u32 powerupApplyLock = nds->ARM9Read8(actor.Base + 0xBA6);
  const melonDS::u32 shellActorPtr = nds->ARM9Read32(actor.Base + 0x0B8);
  const melonDS::u32 shellState =
      (actionFlag & 0x00400000u) != 0 ? (shellActorPtr != 0 ? 2u : 1u) : 0u;
  const melonDS::u32 transitFunc = nds->ARM9Read32(actor.Base + 0x990);
  const melonDS::u32 transitArg = nds->ARM9Read32(actor.Base + 0x994);

  if (player == 0) {
    sample.PlayerActor0PlayerID = playerID;
    sample.PlayerActor0TransitionStep = transitionStep;
    sample.PlayerActor0SignalLock = signalLock;
    sample.PlayerActor0Flag192 = flag192;
    sample.PlayerActor0Flags728 = flags728;
    sample.PlayerActor0Flags72C = flags72C;
    sample.PlayerActor0Flags730 = flags730;
    sample.PlayerActor0ActionFlag = actionFlag;
    sample.PlayerActor0SubActionFlag = subActionFlag;
    sample.PlayerActor0PhysicsFlag = physicsFlag;
    sample.PlayerActor0DamageCooldown = damageCooldown;
    sample.PlayerActor0DamageState = damageState;
    sample.PlayerActor0PowerupAuxState = powerupAuxState;
    sample.PlayerActor0PowerupState = powerupState;
    sample.PlayerActor0PowerupFormState = powerupFormState;
    sample.PlayerActor0PowerupSubState = powerupSubState;
    sample.PlayerActor0DamageGuardFlag = damageGuardFlag;
    sample.PlayerActor0PowerupApplyLock = powerupApplyLock;
    sample.PlayerActor0ShellActorPtr = shellActorPtr;
    sample.PlayerActor0ShellState = shellState;
    sample.PlayerActor0TransitFunc = transitFunc;
    sample.PlayerActor0TransitArg = transitArg;
    return;
  }
  sample.PlayerActor1PlayerID = playerID;
  sample.PlayerActor1TransitionStep = transitionStep;
  sample.PlayerActor1SignalLock = signalLock;
  sample.PlayerActor1Flag192 = flag192;
  sample.PlayerActor1Flags728 = flags728;
  sample.PlayerActor1Flags72C = flags72C;
  sample.PlayerActor1Flags730 = flags730;
  sample.PlayerActor1ActionFlag = actionFlag;
  sample.PlayerActor1SubActionFlag = subActionFlag;
  sample.PlayerActor1PhysicsFlag = physicsFlag;
  sample.PlayerActor1DamageCooldown = damageCooldown;
  sample.PlayerActor1DamageState = damageState;
  sample.PlayerActor1PowerupAuxState = powerupAuxState;
  sample.PlayerActor1PowerupState = powerupState;
  sample.PlayerActor1PowerupFormState = powerupFormState;
  sample.PlayerActor1PowerupSubState = powerupSubState;
  sample.PlayerActor1DamageGuardFlag = damageGuardFlag;
  sample.PlayerActor1PowerupApplyLock = powerupApplyLock;
  sample.PlayerActor1ShellActorPtr = shellActorPtr;
  sample.PlayerActor1ShellState = shellState;
  sample.PlayerActor1TransitFunc = transitFunc;
  sample.PlayerActor1TransitArg = transitArg;
}

void ReadPlayerBaseRuntimeState(melonDS::NDS *nds,
                                const ObjectScanSample &actor,
                                GameStateModel::GameStateSample &sample,
                                int player) {
  if (!actor.Found || !IsARM9MainRAMAddress(actor.Base))
    return;

  const melonDS::u32 linkedActor = nds->ARM9Read32(actor.Base + 0x688);
  const melonDS::u32 transitionFlag = nds->ARM9Read32(actor.Base + 0x784);
  const melonDS::u32 collisionFlag = nds->ARM9Read32(actor.Base + 0x788);
  const melonDS::u32 environmentFlag = nds->ARM9Read32(actor.Base + 0x790);
  const melonDS::u32 updateLocked = nds->ARM9Read8(actor.Base + 0x7A8);
  const melonDS::u32 controlState = nds->ARM9Read8(actor.Base + 0x7A9);
  const melonDS::u32 characterID = nds->ARM9Read8(actor.Base + 0x7AA);
  const melonDS::u32 requestedPowerup = nds->ARM9Read8(actor.Base + 0x7AB);
  const melonDS::u32 currentPowerup = nds->ARM9Read8(actor.Base + 0x7AC);
  const melonDS::u32 previousPowerup = nds->ARM9Read8(actor.Base + 0x7AD);
  const melonDS::u32 transitioningFlag = nds->ARM9Read8(actor.Base + 0x7B0);
  const melonDS::u32 cameraFocusMode = nds->ARM9Read8(actor.Base + 0x7B2);
  const melonDS::u32 defeatedFlag = nds->ARM9Read8(actor.Base + 0x7B3);
  const melonDS::u32 playerBaseID = nds->ARM9Read8(actor.Base + 0x7B4);
  const melonDS::u32 visibleFlag = nds->ARM9Read8(actor.Base + 0x7B5);
  const melonDS::u32 powerupPhase = nds->ARM9Read8(actor.Base + 0xBA6);
  const melonDS::u32 powerupTimer = nds->ARM9Read8(actor.Base + 0xBA7);
  const melonDS::u32 powerupGainTimer = nds->ARM9Read8(actor.Base + 0xBA8);

  if (player == 0) {
    sample.PlayerActor0LinkedActor = linkedActor;
    sample.PlayerActor0TransitionFlag = transitionFlag;
    sample.PlayerActor0CollisionFlag = collisionFlag;
    sample.PlayerActor0EnvironmentFlag = environmentFlag;
    sample.PlayerActor0UpdateLocked = updateLocked;
    sample.PlayerActor0ControlState = controlState;
    sample.PlayerActor0CharacterIDBase = characterID;
    sample.PlayerActor0RequestedPowerup = requestedPowerup;
    sample.PlayerActor0CurrentPowerup = currentPowerup;
    sample.PlayerActor0PreviousPowerup = previousPowerup;
    sample.PlayerActor0TransitioningFlag = transitioningFlag;
    sample.PlayerActor0CameraFocusMode = cameraFocusMode;
    sample.PlayerActor0DefeatedFlag = defeatedFlag;
    sample.PlayerActor0PlayerBaseID = playerBaseID;
    sample.PlayerActor0VisibleFlag = visibleFlag;
    sample.PlayerActor0PowerupPhase = powerupPhase;
    sample.PlayerActor0PowerupTimer = powerupTimer;
    sample.PlayerActor0PowerupGainTimer = powerupGainTimer;
    return;
  }
  sample.PlayerActor1LinkedActor = linkedActor;
  sample.PlayerActor1TransitionFlag = transitionFlag;
  sample.PlayerActor1CollisionFlag = collisionFlag;
  sample.PlayerActor1EnvironmentFlag = environmentFlag;
  sample.PlayerActor1UpdateLocked = updateLocked;
  sample.PlayerActor1ControlState = controlState;
  sample.PlayerActor1CharacterIDBase = characterID;
  sample.PlayerActor1RequestedPowerup = requestedPowerup;
  sample.PlayerActor1CurrentPowerup = currentPowerup;
  sample.PlayerActor1PreviousPowerup = previousPowerup;
  sample.PlayerActor1TransitioningFlag = transitioningFlag;
  sample.PlayerActor1CameraFocusMode = cameraFocusMode;
  sample.PlayerActor1DefeatedFlag = defeatedFlag;
  sample.PlayerActor1PlayerBaseID = playerBaseID;
  sample.PlayerActor1VisibleFlag = visibleFlag;
  sample.PlayerActor1PowerupPhase = powerupPhase;
  sample.PlayerActor1PowerupTimer = powerupTimer;
  sample.PlayerActor1PowerupGainTimer = powerupGainTimer;
}

void ReadMvlGlobals(melonDS::NDS *nds,
                    GameStateModel::GameStateSample &sample) {
  sample.MvlGlobal965C = nds->ARM9Read8(0x020CA698);
  sample.MvlGlobal9670 = nds->ARM9Read8(0x020CA6AC);
  sample.MvlGlobal9674 = nds->ARM9Read8(0x020CA6B0);
  sample.MvlGlobal9694_0 = nds->ARM9Read8(0x020CA6D0);
  sample.MvlGlobal9694_1 = nds->ARM9Read8(0x020CA6D1);
  // StageLayout::onUpdate has an MvL-specific branch gated by these globals.
  // They are not named in NSMB-Code-Reference yet, so keep the address suffix.
  sample.MvlStageLayoutGateCAC6C = nds->ARM9Read8(0x020CAC6C);
  sample.MvlStageLayoutGateCAC74 = nds->ARM9Read8(0x020CAC74);
  sample.MvlStageLayoutGateCAC7C = nds->ARM9Read8(0x020CAC7C);
  sample.MvlStageLayoutGateCACDC = nds->ARM9Read8(0x020CACDC);
  sample.MvlStageLayoutGateCAE80 = nds->ARM9Read32(0x020CAE80);
  sample.MvlStageLayoutGateCAE74 = nds->ARM9Read8(0x020CAE74);
  sample.MvlStageLayoutGateCAEB8 = nds->ARM9Read32(0x020CAEB8);
  sample.MvlStageLayoutGateCAF20 = nds->ARM9Read32(0x020CAF20);
  sample.MvlStageLayoutGateCAF40 = nds->ARM9Read32(0x020CAF40);
  sample.MvlStageLayoutGateCA8C0 = nds->ARM9Read8(0x020CA8C0);
  sample.MvlStageLayoutGateCA8D0 = nds->ARM9Read8(0x020CA8D0);
  sample.MvlStageLayoutGateCAD30 = nds->ARM9Read8(0x020CAD30);
  sample.MvlManagerBase = nds->ARM9Read32(0x020CAD40);
  if (IsARM9MainRAMAddress(sample.MvlManagerBase)) {
    sample.MvlManagerVTable = nds->ARM9Read32(sample.MvlManagerBase + 0x00);
    sample.MvlManagerGUID = nds->ARM9Read32(sample.MvlManagerBase + 0x04);
    sample.MvlManagerSettings = nds->ARM9Read32(sample.MvlManagerBase + 0x08);
    sample.MvlManagerObjectID = nds->ARM9Read16(sample.MvlManagerBase + 0x0C);
    sample.MvlManagerStateType = nds->ARM9Read16(sample.MvlManagerBase + 0x0E);
    sample.MvlManagerFlags = nds->ARM9Read32(sample.MvlManagerBase + 0x10);
    sample.MvlManagerUnk54 = nds->ARM9Read32(sample.MvlManagerBase + 0x54);
    sample.MvlManagerResourcesHeap =
        nds->ARM9Read32(sample.MvlManagerBase + 0x58);
    sample.MvlManagerWordA8CC = nds->ARM9Read32(sample.MvlManagerBase + 0xA8CC);
    sample.MvlManagerWordA8D0 = nds->ARM9Read32(sample.MvlManagerBase + 0xA8D0);
    sample.MvlManagerWordA8D4 = nds->ARM9Read32(sample.MvlManagerBase + 0xA8D4);
    sample.MvlManagerWordA8D8 = nds->ARM9Read32(sample.MvlManagerBase + 0xA8D8);
    sample.MvlManagerWordA8DC = nds->ARM9Read32(sample.MvlManagerBase + 0xA8DC);
    sample.MvlManagerWordA8E0 = nds->ARM9Read32(sample.MvlManagerBase + 0xA8E0);
    sample.MvlManagerWordA8E4 = nds->ARM9Read32(sample.MvlManagerBase + 0xA8E4);
    sample.MvlManagerHalfA8E8 = nds->ARM9Read16(sample.MvlManagerBase + 0xA8E8);
    sample.MvlManagerHalfA8EA = nds->ARM9Read16(sample.MvlManagerBase + 0xA8EA);
    sample.MvlManagerByteA8EC = nds->ARM9Read8(sample.MvlManagerBase + 0xA8EC);
    sample.MvlManagerHalf494 = nds->ARM9Read16(sample.MvlManagerBase + 0x494);
    sample.MvlManagerHalf4A0 = nds->ARM9Read16(sample.MvlManagerBase + 0x4A0);
  }
}

void ReadProjectileGlobals(melonDS::NDS *nds,
                           GameStateModel::GameStateSample &sample) {
  if (IsARM9MainRAMAddress(0x02129480))
    sample.FireballsActiveCount = nds->ARM9Read32(0x02129480);
  if (IsARM9MainRAMAddress(0x02129484))
    sample.FireballsHandlerPtr = nds->ARM9Read32(0x02129484);
  for (int index = 0; index < GameStateModel::kAISpecialHandlerWordCount;
       index++) {
    const melonDS::u32 fireballAddress =
        0x02129484 + sizeof(melonDS::u32) * index;
    const melonDS::u32 projectileAddress =
        0x0212A680 + sizeof(melonDS::u32) * index;
    if (IsARM9MainRAMAddress(fireballAddress))
      sample.FireballsHandlerWords[index] = nds->ARM9Read32(fireballAddress);
    if (IsARM9MainRAMAddress(projectileAddress))
      sample.ProjectilesHandlerWords[index] =
          nds->ARM9Read32(projectileAddress);
  }
  if (!IsARM9MainRAMAddress(sample.FireballsHandlerPtr))
    return;

  for (int index = 0; index < GameStateModel::kAIFireballSlotCount; index++) {
    const melonDS::u32 slot = sample.FireballsHandlerPtr + 0x04 + 0x8C * index;
    if (!IsARM9MainRAMAddress(slot + 0x80))
      continue;
    sample.FireballSlotActive[index] = nds->ARM9Read8(slot + 0x80);
    if (sample.FireballSlotActive[index] == 0)
      continue;
    sample.FireballSlotKind[index] = nds->ARM9Read8(slot + 0x81);
    sample.FireballSlotState[index] = nds->ARM9Read8(slot + 0x83);
    sample.FireballSlotFacing[index] = nds->ARM9Read8(slot + 0x85);
    sample.FireballSlotPosX[index] = nds->ARM9Read32(slot + 0x10);
    sample.FireballSlotPosY[index] = nds->ARM9Read32(slot + 0x14);
    sample.FireballSlotPosZ[index] = nds->ARM9Read32(slot + 0x18);
    sample.FireballSlotPrevX[index] = nds->ARM9Read32(slot + 0x20);
    sample.FireballSlotPrevY[index] = nds->ARM9Read32(slot + 0x24);
    sample.FireballSlotPrevZ[index] = nds->ARM9Read32(slot + 0x28);
    sample.FireballSlotVelX[index] = nds->ARM9Read32(slot + 0x30);
    sample.FireballSlotVelY[index] = nds->ARM9Read32(slot + 0x34);
    sample.FireballSlotVelZ[index] = nds->ARM9Read32(slot + 0x38);
    for (int byteIndex = 0;
         byteIndex < GameStateModel::kAIFireballSlotStateByteCount;
         byteIndex++) {
      const melonDS::u32 address = slot + 0x80 + byteIndex;
      if (IsARM9MainRAMAddress(address))
        sample.FireballSlotStateBytes[index][byteIndex] =
            nds->ARM9Read8(address);
    }
    for (int wordIndex = 0;
         wordIndex < GameStateModel::kAIFireballSlotDebugWordCount;
         wordIndex++) {
      const melonDS::u32 address =
          slot + 0x40 + sizeof(melonDS::u32) * wordIndex;
      if (IsARM9MainRAMAddress(address))
        sample.FireballSlotDebugWords[index][wordIndex] =
            nds->ARM9Read32(address);
    }
  }
}

void ReadBattleStarState(melonDS::NDS *nds,
                         GameStateModel::GameStateSample &sample) {
  const ObjectScanSample star = FindVsBattleStarCandidate(nds);
  sample.VsStarFound = star.Found;
  sample.VsStarGUID = star.GUID;
  sample.VsStarBase = star.Base;
  sample.VsStarSettings = star.Settings;
  sample.VsStarStateType = star.StateType;
  sample.VsStarFlags = star.Flags;
  sample.VsStarPosX = star.PosX;
  sample.VsStarPosY = star.PosY;
  sample.VsStarPosZ = star.PosZ;

  const ObjectScanSample starActor = FindObjectByIDAndSettings(
      nds, kVsBattleStarActorObjectID, kVsBattleStarActorSettings);
  sample.VsStarActorFound = starActor.Found;
  sample.VsStarActorGUID = starActor.GUID;
  sample.VsStarActorBase = starActor.Base;
  sample.VsStarActorSettings = starActor.Settings;
  sample.VsStarActorStateType = starActor.StateType;
  sample.VsStarActorFlags = starActor.Flags;
  sample.VsStarActorPosX = starActor.PosX;
  sample.VsStarActorPosY = starActor.PosY;
  sample.VsStarActorPosZ = starActor.PosZ;
}

void ReadStageObjectState(melonDS::NDS *nds, melonDS::u32 stageSceneSettings,
                          GameStateModel::GameStateSample &sample) {
  ObjectScanSample stageCamera =
      FindObjectByIDAndSettings(nds, kStageCameraObjectID, 0);
  if (!stageCamera.Found)
    stageCamera = FindObjectByIDAndSettingsLoose(nds, kStageCameraObjectID, 0);
  if (stageCamera.Found) {
    sample.StageCameraFound = 1;
    sample.StageCameraBase = stageCamera.Base;
    if (IsARM9MainRAMAddress(stageCamera.Base)) {
      sample.StageCameraTargetX = nds->ARM9Read32(stageCamera.Base + 0x0CC);
      sample.StageCameraTargetY = nds->ARM9Read32(stageCamera.Base + 0x0D0);
      sample.StageCameraTargetZ = nds->ARM9Read32(stageCamera.Base + 0x0D4);
      sample.StageCameraPositionX = nds->ARM9Read32(stageCamera.Base + 0x0DC);
      sample.StageCameraPositionY = nds->ARM9Read32(stageCamera.Base + 0x0E0);
      sample.StageCameraPositionZ = nds->ARM9Read32(stageCamera.Base + 0x0E4);
      sample.StageCameraUpX = nds->ARM9Read32(stageCamera.Base + 0x0EC);
      sample.StageCameraUpY = nds->ARM9Read32(stageCamera.Base + 0x0F0);
      sample.StageCameraUpZ = nds->ARM9Read32(stageCamera.Base + 0x0F4);
      sample.StageCameraUnk114 = nds->ARM9Read32(stageCamera.Base + 0x114);
      sample.StageCameraUnk118 = nds->ARM9Read32(stageCamera.Base + 0x118);
      sample.StageCameraUnk11C = nds->ARM9Read32(stageCamera.Base + 0x11C);
      sample.StageCameraUnk128 = nds->ARM9Read32(stageCamera.Base + 0x128);
      sample.StageCameraUnk12C = nds->ARM9Read32(stageCamera.Base + 0x12C);
      sample.StageCameraRoll130 = nds->ARM9Read32(stageCamera.Base + 0x130);
      sample.StageCameraWord190 = nds->ARM9Read32(stageCamera.Base + 0x190);
      sample.StageCameraWord194 = nds->ARM9Read32(stageCamera.Base + 0x194);
      sample.StageCameraWord19C = nds->ARM9Read32(stageCamera.Base + 0x19C);
      sample.StageCameraWord1A0 = nds->ARM9Read32(stageCamera.Base + 0x1A0);
    }
  }
  ObjectScanSample stageScene =
      FindObjectByIDAndSettings(nds, kStageSceneObjectID, stageSceneSettings);
  if (!stageScene.Found)
    stageScene = FindObjectByIDAndSettingsLoose(nds, kStageSceneObjectID,
                                                stageSceneSettings);
  if (stageScene.Found) {
    sample.StageSceneFound = 1;
    sample.StageSceneBase = stageScene.Base;
    sample.StageSceneSettings = stageScene.Settings;
    sample.StageSceneStateType = stageScene.StateType;
    sample.StageSceneFlags = stageScene.Flags;
    if (IsARM9MainRAMAddress(stageScene.Base)) {
      sample.StageSceneWord154 = nds->ARM9Read32(stageScene.Base + 0x154);
      sample.StageSceneWord160 = nds->ARM9Read32(stageScene.Base + 0x160);
      sample.StageSceneWord5618 = nds->ARM9Read32(stageScene.Base + 0x5618);
      sample.StageSceneWord561C = nds->ARM9Read32(stageScene.Base + 0x561C);
      sample.StageSceneWord563C = nds->ARM9Read32(stageScene.Base + 0x563C);
      sample.StageSceneByte5643 = nds->ARM9Read8(stageScene.Base + 0x5643);
      sample.StageSceneByte5644 = nds->ARM9Read8(stageScene.Base + 0x5644);
      sample.StageSceneByte5645 = nds->ARM9Read8(stageScene.Base + 0x5645);
      sample.StageSceneByte5646 = nds->ARM9Read8(stageScene.Base + 0x5646);
      sample.StageSceneByte5648 = nds->ARM9Read8(stageScene.Base + 0x5648);
      sample.StageSceneByte5649 = nds->ARM9Read8(stageScene.Base + 0x5649);
      if (sample.StageSceneWord5618 < 16) {
        const melonDS::u32 dispatchOffset = sample.StageSceneWord5618 * 8;
        sample.StageSceneUpdateDispatchFunc = nds->ARM9Read32(
            kStageSceneUpdateDispatchTableAddr + dispatchOffset);
        sample.StageSceneUpdateDispatchArg = nds->ARM9Read32(
            kStageSceneUpdateDispatchTableAddr + dispatchOffset + 4);
        sample.StageSceneRenderDispatchFunc = nds->ARM9Read32(
            kStageSceneRenderDispatchTableAddr + dispatchOffset);
        sample.StageSceneRenderDispatchArg = nds->ARM9Read32(
            kStageSceneRenderDispatchTableAddr + dispatchOffset + 4);
      }
      sample.StageSceneGlobal9280 = nds->ARM9Read8(0x020CA2BC);
      sample.StageSceneGlobal9284 = nds->ARM9Read8(0x020CA2C0);
      sample.StageSceneGlobal928C = nds->ARM9Read8(0x020CA2C8);
      sample.StageSceneGlobal92B4 = nds->ARM9Read32(0x020CA2F0);
      sample.StageSceneGlobal92C0 = nds->ARM9Read32(0x020CA2FC);
      sample.StageSceneGlobal92C8 = nds->ARM9Read32(0x020CA304);
      sample.StageSceneGlobal92CC = nds->ARM9Read32(0x020CA308);
      sample.StageSceneGlobal92D0 = nds->ARM9Read32(0x020CA30C);
      sample.StageLiquidPlayerSlot = nds->ARM9Read32(0x02085A7C);
      sample.StageLiquidHeight0 = nds->ARM9Read32(0x020CAE0C);
      sample.StageLiquidHeight1 = nds->ARM9Read32(0x020CAE10);
    }
  }
  sample.VsConnectBase = FindObjectBaseByID(nds, kVsConnectObjectID);
  if (sample.VsConnectBase != 0) {
    sample.VsConnectFound = 1;
    sample.VsConnectWord078 = nds->ARM9Read32(sample.VsConnectBase + 0x078);
    sample.VsConnectWord07C = nds->ARM9Read32(sample.VsConnectBase + 0x07C);
    sample.VsConnectByte0E2 = nds->ARM9Read8(sample.VsConnectBase + 0x0E2);
    sample.VsConnectByte106 = nds->ARM9Read8(sample.VsConnectBase + 0x106);
    sample.VsConnectWord114 = nds->ARM9Read32(sample.VsConnectBase + 0x114);
    sample.VsConnectWord118 = nds->ARM9Read32(sample.VsConnectBase + 0x118);
    sample.VsConnectWord120 = nds->ARM9Read32(sample.VsConnectBase + 0x120);
    sample.VsConnectWord128 = nds->ARM9Read32(sample.VsConnectBase + 0x128);
    sample.VsConnectWord138 = nds->ARM9Read32(sample.VsConnectBase + 0x138);
    sample.VsConnectWord13C = nds->ARM9Read32(sample.VsConnectBase + 0x13C);
    sample.VsConnectWord140 = nds->ARM9Read32(sample.VsConnectBase + 0x140);
    sample.VsConnectWord144 = nds->ARM9Read32(sample.VsConnectBase + 0x144);
    sample.VsConnectWord148 = nds->ARM9Read32(sample.VsConnectBase + 0x148);
    sample.VsConnectByte153 = nds->ARM9Read8(sample.VsConnectBase + 0x153);
    sample.VsConnectByte154 = nds->ARM9Read8(sample.VsConnectBase + 0x154);
    sample.VsConnectByte155 = nds->ARM9Read8(sample.VsConnectBase + 0x155);
    sample.VsConnectByte156 = nds->ARM9Read8(sample.VsConnectBase + 0x156);
    sample.VsConnectByte157 = nds->ARM9Read8(sample.VsConnectBase + 0x157);
    sample.VsConnectByte158 = nds->ARM9Read8(sample.VsConnectBase + 0x158);
    sample.VsConnectWord154 = nds->ARM9Read32(sample.VsConnectBase + 0x154);
  }
  sample.CourseSelectBase = FindObjectBaseByID(nds, kCourseSelectObjectID);
  if (sample.CourseSelectBase != 0) {
    sample.CourseSelectFound = 1;
    sample.CourseSelectSettings =
        nds->ARM9Read32(sample.CourseSelectBase + 0x008);
    sample.CourseSelectWord060 =
        nds->ARM9Read32(sample.CourseSelectBase + 0x060);
    sample.CourseSelectWord064 =
        nds->ARM9Read32(sample.CourseSelectBase + 0x064);
    sample.CourseSelectWord068 =
        nds->ARM9Read32(sample.CourseSelectBase + 0x068);
    sample.CourseSelectWord06C =
        nds->ARM9Read32(sample.CourseSelectBase + 0x06C);
    sample.CourseSelectWord070 =
        nds->ARM9Read32(sample.CourseSelectBase + 0x070);
    sample.CourseSelectWord074 =
        nds->ARM9Read32(sample.CourseSelectBase + 0x074);
    sample.CourseSelectWord078 =
        nds->ARM9Read32(sample.CourseSelectBase + 0x078);
    sample.CourseSelectWord07C =
        nds->ARM9Read32(sample.CourseSelectBase + 0x07C);
    sample.CourseSelectWord080 =
        nds->ARM9Read32(sample.CourseSelectBase + 0x080);
    sample.CourseSelectWord084 =
        nds->ARM9Read32(sample.CourseSelectBase + 0x084);
    sample.CourseSelectWord088 =
        nds->ARM9Read32(sample.CourseSelectBase + 0x088);
    sample.CourseSelectWord08C =
        nds->ARM9Read32(sample.CourseSelectBase + 0x08C);
    sample.CourseSelectWord090 =
        nds->ARM9Read32(sample.CourseSelectBase + 0x090);
  }
  const ObjectScanSample stageActorManager =
      FindObjectByID(nds, kStageActorManagerObjectID);
  sample.StageActorManagerFound = stageActorManager.Found;
  sample.StageActorManagerBase = stageActorManager.Base;
  sample.StageActorManagerStateType = stageActorManager.StateType;
  const ObjectScanSample stageController =
      FindObjectByID(nds, kStageControllerObjectID);
  sample.StageControllerFound = stageController.Found;
  sample.StageControllerBase = stageController.Base;
  sample.StageControllerStateType = stageController.StateType;
  const ObjectScanSample mvlObject267 = FindObjectByID(nds, kMvlObject267ID);
  sample.MvlObject267Found = mvlObject267.Found;
  sample.MvlObject267Base = mvlObject267.Base;
  sample.MvlObject267StateType = mvlObject267.StateType;
  const ObjectPairScanSample mvlObject267Pair =
      FindObjectPairByIDSortedX(nds, kMvlObject267ID);
  sample.MvlObject267LeftFound = mvlObject267Pair.Left.Found;
  sample.MvlObject267LeftBase = mvlObject267Pair.Left.Base;
  sample.MvlObject267LeftStateType = mvlObject267Pair.Left.StateType;
  sample.MvlObject267LeftPosX = mvlObject267Pair.Left.PosX;
  sample.MvlObject267LeftPosY = mvlObject267Pair.Left.PosY;
  sample.MvlObject267LeftPosZ = mvlObject267Pair.Left.PosZ;
  sample.MvlObject267RightFound = mvlObject267Pair.Right.Found;
  sample.MvlObject267RightBase = mvlObject267Pair.Right.Base;
  sample.MvlObject267RightStateType = mvlObject267Pair.Right.StateType;
  sample.MvlObject267RightPosX = mvlObject267Pair.Right.PosX;
  sample.MvlObject267RightPosY = mvlObject267Pair.Right.PosY;
  sample.MvlObject267RightPosZ = mvlObject267Pair.Right.PosZ;
  GameStateReader::ReadMvlGlobals(nds, sample);
  const ObjectScanSample movingHazard = FindNewestActiveObjectByIDAndSettings(
      nds, kVsMovingHazardObjectID, kVsMovingHazardSettings);
  sample.MovingHazardFound = movingHazard.Found;
  sample.MovingHazardGUID = movingHazard.GUID;
  sample.MovingHazardSettings = movingHazard.Settings;
  sample.MovingHazardStateType = movingHazard.StateType;
  sample.MovingHazardFlags = movingHazard.Flags;
  sample.MovingHazardPosX = movingHazard.PosX;
  sample.MovingHazardPosY = movingHazard.PosY;
  sample.MovingHazardPosZ = movingHazard.PosZ;
  sample.MovingHazardVelX = movingHazard.VelX;
  sample.MovingHazardVelY = movingHazard.VelY;
  sample.MovingHazardLastStepX = movingHazard.LastStepX;
  sample.MovingHazardLastStepY = movingHazard.LastStepY;
  sample.MovingHazardLastStepZ = movingHazard.LastStepZ;
  sample.MovingHazardVelH = movingHazard.VelH;
  sample.MovingHazardTargetVelH = movingHazard.TargetVelH;
  sample.MovingHazardAccelV = movingHazard.AccelV;
  sample.MovingHazardTargetVelV = movingHazard.TargetVelV;
  sample.MovingHazardAccelH = movingHazard.AccelH;
  sample.MovingHazardTargetVelX = movingHazard.TargetVelX;
  sample.MovingHazardTargetVelY = movingHazard.TargetVelY;
  sample.MovingHazardTargetVelZ = movingHazard.TargetVelZ;

  GameStateReader::ReadProjectileGlobals(nds, sample);

  const ObjectLifecycleSummary objectSummary = SummarizeObjectLifecycle(nds);
  sample.ObjectScanTotal = objectSummary.Total;
  sample.ObjectNotCreatedCount = objectSummary.NotCreated;
  sample.ObjectActiveCount = objectSummary.Active;
  sample.ObjectDeadCount = objectSummary.Dead;
  sample.ObjectSkipUpdateCount = objectSummary.SkipUpdate;
  sample.ObjectSkipRenderCount = objectSummary.SkipRender;
  sample.ObjectFirstNotCreatedID = objectSummary.FirstNotCreatedID;
  sample.ObjectFirstNotCreatedBase = objectSummary.FirstNotCreatedBase;
  sample.ObjectFirstNotCreatedFlags = objectSummary.FirstNotCreatedFlags;
  sample.ObjectSecondNotCreatedID = objectSummary.SecondNotCreatedID;
  sample.ObjectSecondNotCreatedBase = objectSummary.SecondNotCreatedBase;
  sample.ObjectSecondNotCreatedFlags = objectSummary.SecondNotCreatedFlags;
  for (int i = 0; i < kObjectTraceSlots; i++) {
    sample.ObjectActiveID[i] = objectSummary.ActiveID[i];
    sample.ObjectActiveSettings[i] = objectSummary.ActiveSettings[i];
    sample.ObjectActiveBase[i] = objectSummary.ActiveBase[i];
  }
}

ScopedGameStateObjectScanCache::ScopedGameStateObjectScanCache(
    const GameStateObjectScanCache &cache)
    : Previous(ActiveGameStateObjectScanCache) {
  ActiveGameStateObjectScanCache = &cache;
}

ScopedGameStateObjectScanCache::~ScopedGameStateObjectScanCache() {
  ActiveGameStateObjectScanCache = Previous;
}

void ReadObjectTransform(melonDS::NDS *nds, melonDS::u32 off,
                         ObjectScanSample &sample) {
  // Actor embeds Vec3 objects with a 4-byte vtable followed by x/y/z.
  // position starts at 0x5C, so its numeric coordinates are 0x60/0x64/0x68.
  ReadMainRAMU32(nds, off + 0x60, sample.PosX);
  ReadMainRAMU32(nds, off + 0x64, sample.PosY);
  ReadMainRAMU32(nds, off + 0x68, sample.PosZ);
  ReadMainRAMU32(nds, off + 0x70, sample.PrevX);
  ReadMainRAMU32(nds, off + 0x74, sample.PrevY);
  ReadMainRAMU32(nds, off + 0x78, sample.PrevZ);
  ReadMainRAMU32(nds, off + 0x80, sample.LastStepX);
  ReadMainRAMU32(nds, off + 0x84, sample.LastStepY);
  ReadMainRAMU32(nds, off + 0x88, sample.LastStepZ);
  ReadMainRAMU32(nds, off + 0xB0, sample.VelH);
  ReadMainRAMU32(nds, off + 0xB4, sample.TargetVelH);
  ReadMainRAMU32(nds, off + 0xB8, sample.AccelV);
  ReadMainRAMU32(nds, off + 0xBC, sample.TargetVelV);
  ReadMainRAMU32(nds, off + 0xC0, sample.AccelH);
  ReadMainRAMU32(nds, off + 0xD0, sample.VelX);
  ReadMainRAMU32(nds, off + 0xD4, sample.VelY);
  ReadMainRAMU32(nds, off + 0xD8, sample.VelZ);
  ReadMainRAMU32(nds, off + 0xE0, sample.TargetVelX);
  ReadMainRAMU32(nds, off + 0xE4, sample.TargetVelY);
  ReadMainRAMU32(nds, off + 0xE8, sample.TargetVelZ);
}

void AddGameStateProcessObject(melonDS::NDS *nds,
                               GameStateObjectScanCache &cache,
                               std::set<melonDS::u32> &seenBases,
                               melonDS::u32 base) {
  if (!IsValidMainRAMRange(nds, base, 0x120) || !seenBases.insert(base).second)
    return;

  GameStateObjectScanEntry entry;
  melonDS::u32 guid = 0;
  melonDS::u32 settings = 0;
  melonDS::u16 stateType = 0;
  melonDS::u32 flags = 0;
  if (!ReadMainRAMAddressU32(nds, base, entry.VTable) ||
      !ReadMainRAMAddressU32(nds, base + 4, guid) ||
      !ReadMainRAMAddressU32(nds, base + 8, settings) ||
      !ReadMainRAMAddressU16(nds, base + 0x0C, entry.ObjectID) ||
      !ReadMainRAMAddressU16(nds, base + 0x0E, stateType))
    return;
  std::memcpy(&flags, nds->MainRAM + (base - kMainRAMBase) + 0x10,
              sizeof(flags));
  entry.LifecycleState = nds->MainRAM[(base - kMainRAMBase) + 0x0E];
  entry.Type = nds->MainRAM[(base - kMainRAMBase) + 0x12];
  entry.SkipFlags = nds->MainRAM[(base - kMainRAMBase) + 0x13];

  const melonDS::u32 ramLen = nds->MainRAMMask + 1;
  if (entry.VTable < kMainRAMBase || entry.VTable >= kMainRAMBase + ramLen)
    return;
  if (guid == 0 || guid >= 0x10000)
    return;
  if (entry.ObjectID == 0 || entry.ObjectID >= 0x400)
    return;

  entry.Offset = base - kMainRAMBase;
  entry.Actor.Found = 1;
  entry.Actor.GUID = guid;
  entry.Actor.Base = base;
  entry.Actor.Settings = settings;
  entry.Actor.StateType = stateType;
  entry.Actor.Flags = flags;
  ReadObjectTransform(nds, entry.Offset, entry.Actor);
  cache.Entries.push_back(entry);

  if (entry.LifecycleState > 2 || entry.Type > 2)
    return;

  auto &summary = cache.Lifecycle;
  summary.Total++;
  if (entry.LifecycleState == 0) {
    summary.NotCreated++;
    const melonDS::u32 lifecycleFlags =
        (static_cast<melonDS::u32>(entry.Type) << 16) |
        (static_cast<melonDS::u32>(entry.SkipFlags) << 24);
    if (summary.FirstNotCreatedBase == 0) {
      summary.FirstNotCreatedID = entry.ObjectID;
      summary.FirstNotCreatedBase = entry.Actor.Base;
      summary.FirstNotCreatedFlags = lifecycleFlags;
    } else if (summary.SecondNotCreatedBase == 0) {
      summary.SecondNotCreatedID = entry.ObjectID;
      summary.SecondNotCreatedBase = entry.Actor.Base;
      summary.SecondNotCreatedFlags = lifecycleFlags;
    }
  } else if (entry.LifecycleState == 1) {
    const melonDS::u32 index = summary.Active;
    if (index < static_cast<melonDS::u32>(kObjectTraceSlots)) {
      summary.ActiveID[index] = entry.ObjectID;
      summary.ActiveSettings[index] = settings;
      summary.ActiveBase[index] = entry.Actor.Base;
    }
    summary.Active++;
  } else {
    summary.Dead++;
  }

  if ((entry.SkipFlags & 0x02) != 0)
    summary.SkipUpdate++;
  if ((entry.SkipFlags & 0x08) != 0)
    summary.SkipRender++;
}

void AddGameStateProcessList(melonDS::NDS *nds, GameStateObjectScanCache &cache,
                             std::set<melonDS::u32> &seenBases,
                             melonDS::u32 listAddress) {
  melonDS::u32 node = 0;
  if (!ReadMainRAMAddressU32(nds, listAddress, node))
    return;

  std::set<melonDS::u32> seenNodes;
  for (int i = 0; i < 512 && IsValidMainRAMRange(nds, node, 0x0C); i++) {
    if (!seenNodes.insert(node).second)
      break;

    melonDS::u32 next = 0;
    melonDS::u32 base = 0;
    ReadMainRAMAddressU32(nds, node + 0x04, next);
    ReadMainRAMAddressU32(nds, node + 0x08, base);
    AddGameStateProcessObject(nds, cache, seenBases, base);
    node = next;
  }
}

GameStateObjectScanCache BuildGameStateObjectScanCache(melonDS::NDS *nds) {
  GameStateObjectScanCache cache;
  if (!nds || !nds->MainRAM)
    return cache;

  cache.Entries.reserve(128);
  std::set<melonDS::u32> seenBases;
  AddGameStateProcessList(nds, cache, seenBases, kNSMBProcessExecuteListAddr);
  AddGameStateProcessList(nds, cache, seenBases, kNSMBProcessDeleteListAddr);
  AddGameStateProcessList(nds, cache, seenBases, kNSMBProcessRenderListAddr);
  AddGameStateProcessList(nds, cache, seenBases, kNSMBProcessCreateListAddr);
  for (int i = 0; i < 8; i++) {
    AddGameStateProcessList(nds, cache, seenBases,
                            kNSMBProcessIDLookupListsAddr +
                                static_cast<melonDS::u32>(i * 8));
  }
  return cache;
}

melonDS::u32 FindObjectBaseByID(melonDS::NDS *nds, melonDS::u16 objectID) {
  if (!nds || !nds->MainRAM)
    return 0;
  if (HasActiveObjectScanCache())
    return FindCachedObjectBaseByID(objectID);

  for (melonDS::u32 off = 0x080000; off + 0x80 <= nds->MainRAMMask + 1;
       off += 4) {
    melonDS::u32 vtable = 0;
    melonDS::u16 candidateID = 0;
    melonDS::u16 stateType = 0;
    melonDS::u32 flags = 0;
    if (!ReadMainRAMU32(nds, off, vtable) ||
        !ReadMainRAMU16(nds, off + 0x0C, candidateID) ||
        !ReadMainRAMU16(nds, off + 0x0E, stateType) ||
        !ReadMainRAMU32(nds, off + 0x10, flags))
      continue;
    if (candidateID != objectID || stateType == 0 || stateType > 2)
      continue;
    if (vtable < 0x02000000 || vtable >= 0x02400000)
      continue;
    if ((flags & 0xFFFF0000u) == 0)
      continue;
    return kMainRAMBase + off;
  }
  return 0;
}

bool HasActiveObjectScanCache() {
  return ActiveGameStateObjectScanCache != nullptr;
}

bool IsCachedObjectStrict(const GameStateObjectScanEntry &entry) {
  return (entry.Actor.StateType == 1 || entry.Actor.StateType == 2 ||
          entry.Actor.StateType == 3) &&
         entry.Actor.Flags < 0x10000000;
}

ObjectScanSample FindCachedObject(melonDS::u16 expectedObjectID,
                                  bool matchSettings,
                                  melonDS::u32 expectedSettings, bool strict) {
  if (!ActiveGameStateObjectScanCache)
    return {};

  for (const auto &entry : ActiveGameStateObjectScanCache->Entries) {
    if (entry.ObjectID != expectedObjectID)
      continue;
    if (matchSettings && entry.Actor.Settings != expectedSettings)
      continue;
    if (strict && !IsCachedObjectStrict(entry))
      continue;
    return entry.Actor;
  }
  return {};
}

melonDS::u32 FindCachedObjectBaseByID(melonDS::u16 objectID) {
  if (!ActiveGameStateObjectScanCache)
    return 0;

  for (const auto &entry : ActiveGameStateObjectScanCache->Entries) {
    if (entry.Offset < 0x080000 || entry.ObjectID != objectID)
      continue;
    if (entry.Actor.StateType == 0 || entry.Actor.StateType > 2)
      continue;
    if ((entry.Actor.Flags & 0xFFFF0000u) == 0)
      continue;
    return entry.Actor.Base;
  }
  return 0;
}

ObjectScanSample FindVsBattleStarCandidate(melonDS::NDS *nds) {
  ObjectScanSample sample;
  if (!nds || !nds->MainRAM)
    return sample;
  if (ActiveGameStateObjectScanCache)
    return FindCachedObject(kVsBattleStarCandidateObjectID, false, 0, true);

  const melonDS::u32 ramLen = nds->MainRAMMask + 1;
  if (ramLen < 0x120)
    return sample;

  for (melonDS::u32 off = 0; off <= ramLen - 0x120; off += 4) {
    melonDS::u32 vtable = 0;
    melonDS::u32 guid = 0;
    melonDS::u16 objectID = 0;
    melonDS::u16 stateType = 0;
    melonDS::u32 flags = 0;
    if (!ReadMainRAMU32(nds, off, vtable) ||
        !ReadMainRAMU32(nds, off + 4, guid) ||
        !ReadMainRAMU16(nds, off + 0x0C, objectID) ||
        !ReadMainRAMU16(nds, off + 0x0E, stateType) ||
        !ReadMainRAMU32(nds, off + 0x10, flags))
      continue;

    if (vtable < kMainRAMBase || vtable >= kMainRAMBase + ramLen)
      continue;
    if (guid == 0 || guid >= 0x10000)
      continue;
    if (objectID != kVsBattleStarCandidateObjectID)
      continue;
    if (stateType != 1 && stateType != 2 && stateType != 3)
      continue;
    if (flags >= 0x10000000)
      continue;

    melonDS::u32 settings = 0;
    ReadMainRAMU32(nds, off + 8, settings);

    sample.Found = 1;
    sample.GUID = guid;
    sample.Base = kMainRAMBase + off;
    sample.Settings = settings;
    sample.StateType = stateType;
    sample.Flags = flags;
    ReadObjectTransform(nds, off, sample);
    return sample;
  }

  return sample;
}

ObjectScanSample FindObjectByIDAndSettings(melonDS::NDS *nds,
                                           melonDS::u16 expectedObjectID,
                                           melonDS::u32 expectedSettings) {
  ObjectScanSample sample;
  if (!nds || !nds->MainRAM)
    return sample;
  if (ActiveGameStateObjectScanCache)
    return FindCachedObject(expectedObjectID, true, expectedSettings, true);

  const melonDS::u32 ramLen = nds->MainRAMMask + 1;
  if (ramLen < 0x120)
    return sample;

  for (melonDS::u32 off = 0; off <= ramLen - 0x120; off += 4) {
    melonDS::u32 vtable = 0;
    melonDS::u32 guid = 0;
    melonDS::u32 settings = 0;
    melonDS::u16 objectID = 0;
    melonDS::u16 stateType = 0;
    melonDS::u32 flags = 0;
    if (!ReadMainRAMU32(nds, off, vtable) ||
        !ReadMainRAMU32(nds, off + 4, guid) ||
        !ReadMainRAMU32(nds, off + 8, settings) ||
        !ReadMainRAMU16(nds, off + 0x0C, objectID) ||
        !ReadMainRAMU16(nds, off + 0x0E, stateType) ||
        !ReadMainRAMU32(nds, off + 0x10, flags))
      continue;

    if (vtable < kMainRAMBase || vtable >= kMainRAMBase + ramLen)
      continue;
    if (guid == 0 || guid >= 0x10000)
      continue;
    if (objectID != expectedObjectID || settings != expectedSettings)
      continue;
    if (stateType != 1 && stateType != 2 && stateType != 3)
      continue;
    if (flags >= 0x10000000)
      continue;

    sample.Found = 1;
    sample.GUID = guid;
    sample.Base = kMainRAMBase + off;
    sample.Settings = settings;
    sample.StateType = stateType;
    sample.Flags = flags;
    ReadObjectTransform(nds, off, sample);
    return sample;
  }

  return sample;
}

ObjectScanSample FindObjectByID(melonDS::NDS *nds,
                                melonDS::u16 expectedObjectID) {
  ObjectScanSample sample;
  if (!nds || !nds->MainRAM)
    return sample;
  if (ActiveGameStateObjectScanCache)
    return FindCachedObject(expectedObjectID, false, 0, true);

  const melonDS::u32 ramLen = nds->MainRAMMask + 1;
  if (ramLen < 0x120)
    return sample;

  for (melonDS::u32 off = 0; off <= ramLen - 0x120; off += 4) {
    melonDS::u32 vtable = 0;
    melonDS::u32 guid = 0;
    melonDS::u32 settings = 0;
    melonDS::u16 objectID = 0;
    melonDS::u16 stateType = 0;
    melonDS::u32 flags = 0;
    if (!ReadMainRAMU32(nds, off, vtable) ||
        !ReadMainRAMU32(nds, off + 4, guid) ||
        !ReadMainRAMU32(nds, off + 8, settings) ||
        !ReadMainRAMU16(nds, off + 0x0C, objectID) ||
        !ReadMainRAMU16(nds, off + 0x0E, stateType) ||
        !ReadMainRAMU32(nds, off + 0x10, flags))
      continue;

    if (vtable < kMainRAMBase || vtable >= kMainRAMBase + ramLen)
      continue;
    if (guid == 0 || guid >= 0x10000)
      continue;
    if (objectID != expectedObjectID)
      continue;
    if (stateType != 1 && stateType != 2 && stateType != 3)
      continue;
    if (flags >= 0x10000000)
      continue;

    sample.Found = 1;
    sample.GUID = guid;
    sample.Base = kMainRAMBase + off;
    sample.Settings = settings;
    sample.StateType = stateType;
    sample.Flags = flags;
    ReadObjectTransform(nds, off, sample);
    return sample;
  }

  return sample;
}

ObjectScanSample FindObjectByIDAndSettingsLoose(melonDS::NDS *nds,
                                                melonDS::u16 expectedObjectID,
                                                melonDS::u32 expectedSettings) {
  ObjectScanSample sample;
  if (!nds || !nds->MainRAM)
    return sample;
  if (ActiveGameStateObjectScanCache)
    return FindCachedObject(expectedObjectID, true, expectedSettings, false);

  const melonDS::u32 ramLen = nds->MainRAMMask + 1;
  if (ramLen < 0x120)
    return sample;

  for (melonDS::u32 off = 0; off <= ramLen - 0x120; off += 4) {
    melonDS::u32 vtable = 0;
    melonDS::u32 guid = 0;
    melonDS::u32 settings = 0;
    melonDS::u16 objectID = 0;
    melonDS::u16 stateType = 0;
    melonDS::u32 flags = 0;
    if (!ReadMainRAMU32(nds, off, vtable) ||
        !ReadMainRAMU32(nds, off + 4, guid) ||
        !ReadMainRAMU32(nds, off + 8, settings) ||
        !ReadMainRAMU16(nds, off + 0x0C, objectID) ||
        !ReadMainRAMU16(nds, off + 0x0E, stateType) ||
        !ReadMainRAMU32(nds, off + 0x10, flags))
      continue;

    if (vtable < kMainRAMBase || vtable >= kMainRAMBase + ramLen)
      continue;
    if (guid == 0 || guid >= 0x10000)
      continue;
    if (objectID != expectedObjectID || settings != expectedSettings)
      continue;

    sample.Found = 1;
    sample.GUID = guid;
    sample.Base = kMainRAMBase + off;
    sample.Settings = settings;
    sample.StateType = stateType;
    sample.Flags = flags;
    ReadObjectTransform(nds, off, sample);
    return sample;
  }

  return sample;
}

void InsertObjectByPosX(ObjectPairScanSample &pair,
                        const ObjectScanSample &actor) {
  if (!actor.Found)
    return;
  if (!pair.Left.Found || actor.PosX < pair.Left.PosX ||
      (actor.PosX == pair.Left.PosX && actor.GUID < pair.Left.GUID)) {
    pair.Right = pair.Left;
    pair.Left = actor;
    return;
  }
  if ((!pair.Right.Found || actor.PosX < pair.Right.PosX ||
       (actor.PosX == pair.Right.PosX && actor.GUID < pair.Right.GUID)) &&
      actor.GUID != pair.Left.GUID) {
    pair.Right = actor;
  }
}

ObjectPairScanSample FindObjectPairByIDSortedX(melonDS::NDS *nds,
                                               melonDS::u16 expectedObjectID) {
  ObjectPairScanSample pair;
  if (!nds || !nds->MainRAM)
    return pair;
  if (ActiveGameStateObjectScanCache) {
    for (const auto &entry : ActiveGameStateObjectScanCache->Entries) {
      if (entry.ObjectID == expectedObjectID && entry.Actor.Flags < 0x10000000)
        InsertObjectByPosX(pair, entry.Actor);
    }
    return pair;
  }

  const melonDS::u32 ramLen = nds->MainRAMMask + 1;
  if (ramLen < 0x120)
    return pair;

  for (melonDS::u32 off = 0; off <= ramLen - 0x120; off += 4) {
    melonDS::u32 vtable = 0;
    melonDS::u32 guid = 0;
    melonDS::u32 settings = 0;
    melonDS::u16 objectID = 0;
    melonDS::u16 stateType = 0;
    melonDS::u32 flags = 0;
    if (!ReadMainRAMU32(nds, off, vtable) ||
        !ReadMainRAMU32(nds, off + 4, guid) ||
        !ReadMainRAMU32(nds, off + 8, settings) ||
        !ReadMainRAMU16(nds, off + 0x0C, objectID) ||
        !ReadMainRAMU16(nds, off + 0x0E, stateType) ||
        !ReadMainRAMU32(nds, off + 0x10, flags))
      continue;

    if (vtable < kMainRAMBase || vtable >= kMainRAMBase + ramLen)
      continue;
    if (guid == 0 || guid >= 0x10000)
      continue;
    if (objectID != expectedObjectID)
      continue;
    if (flags >= 0x10000000)
      continue;

    ObjectScanSample actor;
    actor.Found = 1;
    actor.GUID = guid;
    actor.Base = kMainRAMBase + off;
    actor.Settings = settings;
    actor.StateType = stateType;
    actor.Flags = flags;
    ReadObjectTransform(nds, off, actor);
    InsertObjectByPosX(pair, actor);
  }

  return pair;
}

void InsertPlayerActorByGUID(PlayerActorScanSample &players,
                             const ObjectScanSample &actor) {
  if (!actor.Found)
    return;
  if (!players.Actor0.Found || actor.GUID < players.Actor0.GUID) {
    players.Actor1 = players.Actor0;
    players.Actor0 = actor;
    return;
  }
  if ((!players.Actor1.Found || actor.GUID < players.Actor1.GUID) &&
      actor.GUID != players.Actor0.GUID) {
    players.Actor1 = actor;
  }
}

PlayerActorScanSample FindPlayerActors(melonDS::NDS *nds) {
  PlayerActorScanSample players;
  if (!nds || !nds->MainRAM)
    return players;
  if (ActiveGameStateObjectScanCache) {
    for (const auto &entry : ActiveGameStateObjectScanCache->Entries) {
      if (entry.ObjectID == kPlayerObjectID && IsCachedObjectStrict(entry))
        InsertPlayerActorByGUID(players, entry.Actor);
    }
    return players;
  }

  const melonDS::u32 ramLen = nds->MainRAMMask + 1;
  if (ramLen < 0x120)
    return players;

  for (melonDS::u32 off = 0; off <= ramLen - 0x120; off += 4) {
    melonDS::u32 vtable = 0;
    melonDS::u32 guid = 0;
    melonDS::u16 objectID = 0;
    melonDS::u16 stateType = 0;
    melonDS::u32 flags = 0;
    if (!ReadMainRAMU32(nds, off, vtable) ||
        !ReadMainRAMU32(nds, off + 4, guid) ||
        !ReadMainRAMU16(nds, off + 0x0C, objectID) ||
        !ReadMainRAMU16(nds, off + 0x0E, stateType) ||
        !ReadMainRAMU32(nds, off + 0x10, flags))
      continue;

    if (vtable < kMainRAMBase || vtable >= kMainRAMBase + ramLen)
      continue;
    if (guid == 0 || guid >= 0x10000)
      continue;
    if (objectID != kPlayerObjectID)
      continue;
    if (stateType != 1 && stateType != 2 && stateType != 3)
      continue;
    if (flags >= 0x10000000)
      continue;

    ObjectScanSample actor;
    actor.Found = 1;
    actor.GUID = guid;
    actor.Base = kMainRAMBase + off;
    actor.StateType = stateType;
    actor.Flags = flags;
    ReadMainRAMU32(nds, off + 8, actor.Settings);
    ReadObjectTransform(nds, off, actor);
    InsertPlayerActorByGUID(players, actor);
  }

  return players;
}

bool ReadPlayerActorByBase(melonDS::NDS *nds, melonDS::u32 base,
                           melonDS::u32 expectedGUID, ObjectScanSample &actor) {
  actor = {};
  if (!nds || !nds->MainRAM || base < kMainRAMBase)
    return false;

  const melonDS::u32 off = base - kMainRAMBase;
  const melonDS::u32 ramLen = nds->MainRAMMask + 1;
  if (off + 0x120 > ramLen)
    return false;

  melonDS::u32 vtable = 0;
  melonDS::u32 guid = 0;
  melonDS::u32 settings = 0;
  melonDS::u16 objectID = 0;
  melonDS::u16 stateType = 0;
  melonDS::u32 flags = 0;
  if (!ReadMainRAMU32(nds, off, vtable) ||
      !ReadMainRAMU32(nds, off + 4, guid) ||
      !ReadMainRAMU32(nds, off + 8, settings) ||
      !ReadMainRAMU16(nds, off + 0x0C, objectID) ||
      !ReadMainRAMU16(nds, off + 0x0E, stateType) ||
      !ReadMainRAMU32(nds, off + 0x10, flags))
    return false;

  if (vtable < kMainRAMBase || vtable >= kMainRAMBase + ramLen)
    return false;
  if (guid == 0 || guid >= 0x10000)
    return false;
  if (expectedGUID != 0 && guid != expectedGUID)
    return false;
  if (objectID != kPlayerObjectID)
    return false;
  if (stateType != 1 && stateType != 2 && stateType != 3)
    return false;
  if (flags >= 0x10000000)
    return false;

  actor.Found = 1;
  actor.GUID = guid;
  actor.Base = base;
  actor.Settings = settings;
  actor.StateType = stateType;
  actor.Flags = flags;
  ReadObjectTransform(nds, off, actor);
  return true;
}

bool ReadObjectByBase(melonDS::NDS *nds, melonDS::u32 base,
                      melonDS::u32 expectedGUID, melonDS::u16 expectedObjectID,
                      melonDS::u32 expectedSettings, ObjectScanSample &actor) {
  actor = {};
  if (!nds || !nds->MainRAM || base < kMainRAMBase)
    return false;

  const melonDS::u32 off = base - kMainRAMBase;
  const melonDS::u32 ramLen = nds->MainRAMMask + 1;
  if (off + 0x120 > ramLen)
    return false;

  melonDS::u32 vtable = 0;
  melonDS::u32 guid = 0;
  melonDS::u32 settings = 0;
  melonDS::u16 objectID = 0;
  melonDS::u16 stateType = 0;
  melonDS::u32 flags = 0;
  if (!ReadMainRAMU32(nds, off, vtable) ||
      !ReadMainRAMU32(nds, off + 4, guid) ||
      !ReadMainRAMU32(nds, off + 8, settings) ||
      !ReadMainRAMU16(nds, off + 0x0C, objectID) ||
      !ReadMainRAMU16(nds, off + 0x0E, stateType) ||
      !ReadMainRAMU32(nds, off + 0x10, flags))
    return false;

  if (vtable < kMainRAMBase || vtable >= kMainRAMBase + ramLen)
    return false;
  if (guid == 0 || guid >= 0x10000)
    return false;
  if (expectedGUID != 0 && guid != expectedGUID)
    return false;
  if (objectID != expectedObjectID || settings != expectedSettings)
    return false;
  if (stateType != 1 && stateType != 2 && stateType != 3)
    return false;
  if (flags >= 0x10000000)
    return false;

  actor.Found = 1;
  actor.GUID = guid;
  actor.Base = base;
  actor.Settings = settings;
  actor.StateType = stateType;
  actor.Flags = flags;
  ReadObjectTransform(nds, off, actor);
  return true;
}

std::vector<ObjectScanSample> FindActiveObjectsByIDAndSettings(
    melonDS::NDS *nds, melonDS::u16 expectedObjectID,
    melonDS::u32 expectedSettings, bool includeStateType2) {
  std::vector<ObjectScanSample> actors;
  if (!nds || !nds->MainRAM)
    return actors;

  std::set<melonDS::u32> seenBases;
  const auto scanList = [&](melonDS::u32 listAddress) {
    melonDS::u32 node = 0;
    if (!ReadMainRAMAddressU32(nds, listAddress, node))
      return;

    std::set<melonDS::u32> seenNodes;
    for (int i = 0; i < 512 && IsValidMainRAMRange(nds, node, 0x0C); i++) {
      if (!seenNodes.insert(node).second)
        break;

      melonDS::u32 next = 0;
      melonDS::u32 base = 0;
      ReadMainRAMAddressU32(nds, node + 0x04, next);
      ReadMainRAMAddressU32(nds, node + 0x08, base);
      node = next;
      if (!seenBases.insert(base).second)
        continue;

      ObjectScanSample actor;
      if (!ReadObjectByBase(nds, base, 0, expectedObjectID, expectedSettings,
                            actor))
        continue;
      if (actor.StateType == 1 || (includeStateType2 && actor.StateType == 2))
        actors.push_back(actor);
    }
  };

  scanList(kNSMBProcessExecuteListAddr);
  scanList(kNSMBProcessDeleteListAddr);
  scanList(kNSMBProcessRenderListAddr);
  scanList(kNSMBProcessCreateListAddr);
  for (int i = 0; i < 8; i++)
    scanList(kNSMBProcessIDLookupListsAddr + static_cast<melonDS::u32>(i * 8));

  std::sort(actors.begin(), actors.end(),
            [](const ObjectScanSample &lhs, const ObjectScanSample &rhs) {
              return lhs.GUID < rhs.GUID;
            });
  return actors;
}

ObjectScanSample FindNewestActiveObjectByIDAndSettings(
    melonDS::NDS *nds, melonDS::u16 expectedObjectID,
    melonDS::u32 expectedSettings, bool includeStateType2) {
  const std::vector<ObjectScanSample> actors = FindActiveObjectsByIDAndSettings(
      nds, expectedObjectID, expectedSettings, includeStateType2);
  if (actors.empty())
    return {};
  return actors.back();
}

ObjectLifecycleSummary SummarizeObjectLifecycle(melonDS::NDS *nds) {
  ObjectLifecycleSummary summary;
  if (!nds || !nds->MainRAM)
    return summary;
  if (ActiveGameStateObjectScanCache)
    return ActiveGameStateObjectScanCache->Lifecycle;

  const melonDS::u32 ramLen = nds->MainRAMMask + 1;
  if (ramLen < 0x5C)
    return summary;

  for (melonDS::u32 off = 0; off <= ramLen - 0x5C; off += 4) {
    melonDS::u32 vtable = 0;
    melonDS::u32 guid = 0;
    melonDS::u32 settings = 0;
    melonDS::u16 objectID = 0;
    melonDS::u8 state = 0;
    melonDS::u8 type = 0;
    melonDS::u8 skipFlags = 0;
    if (!ReadMainRAMU32(nds, off, vtable) ||
        !ReadMainRAMU32(nds, off + 4, guid) ||
        !ReadMainRAMU32(nds, off + 8, settings) ||
        !ReadMainRAMU16(nds, off + 0x0C, objectID) ||
        !ReadMainRAMU8(nds, off + 0x0E, state) ||
        !ReadMainRAMU8(nds, off + 0x12, type) ||
        !ReadMainRAMU8(nds, off + 0x13, skipFlags))
      continue;

    if (vtable < kMainRAMBase || vtable >= kMainRAMBase + ramLen)
      continue;
    if (guid == 0 || guid >= 0x10000)
      continue;
    if (objectID == 0 || objectID >= 0x400)
      continue;
    if (state > 2)
      continue;
    if (type > 2)
      continue;

    summary.Total++;
    if (state == 0) {
      summary.NotCreated++;
      const melonDS::u32 base = kMainRAMBase + off;
      const melonDS::u32 flags = (static_cast<melonDS::u32>(type) << 16) |
                                 (static_cast<melonDS::u32>(skipFlags) << 24);
      if (summary.FirstNotCreatedBase == 0) {
        summary.FirstNotCreatedID = objectID;
        summary.FirstNotCreatedBase = base;
        summary.FirstNotCreatedFlags = flags;
      } else if (summary.SecondNotCreatedBase == 0) {
        summary.SecondNotCreatedID = objectID;
        summary.SecondNotCreatedBase = base;
        summary.SecondNotCreatedFlags = flags;
      }
    } else if (state == 1) {
      const melonDS::u32 index = summary.Active;
      if (index < static_cast<melonDS::u32>(kObjectTraceSlots)) {
        summary.ActiveID[index] = objectID;
        summary.ActiveSettings[index] = settings;
        summary.ActiveBase[index] = kMainRAMBase + off;
      }
      summary.Active++;
    } else if (state == 2) {
      summary.Dead++;
    }

    if ((skipFlags & 0x02) != 0)
      summary.SkipUpdate++;
    if ((skipFlags & 0x08) != 0)
      summary.SkipRender++;
  }

  return summary;
}

PlayerCollisionMgrSample
ReadPlayerCollisionMgrSample(melonDS::NDS *nds, const ObjectScanSample &actor) {
  PlayerCollisionMgrSample sample;
  if (!nds || !actor.Found ||
      !IsValidMainRAMRange(
          nds, actor.Base + kPlayerStageActorCollisionMgrOffset, 0xB8))
    return sample;

  const melonDS::u32 base = actor.Base + kPlayerStageActorCollisionMgrOffset;
  sample.Found = 1;
  sample.Base = base;
  sample.DeltaX = nds->ARM9Read32(base + kCollisionMgrDeltaXOffset);
  sample.DeltaY = nds->ARM9Read32(base + kCollisionMgrDeltaYOffset);
  sample.CollisionResult =
      nds->ARM9Read32(base + kCollisionMgrCollisionResultOffset);
  sample.GroundCollision =
      nds->ARM9Read32(base + kCollisionMgrGroundCollisionOffset);
  sample.AttachedTileX =
      nds->ARM9Read16(base + kCollisionMgrAttachedTileXOffset);
  sample.AttachedTileY =
      nds->ARM9Read16(base + kCollisionMgrAttachedTileYOffset);
  sample.BottomModifierTileType =
      nds->ARM9Read16(base + kCollisionMgrBottomModifierTileTypeOffset);
  sample.BottomSlopeType =
      nds->ARM9Read8(base + kCollisionMgrBottomSlopeTypeOffset);
  sample.TopModifierTileType =
      nds->ARM9Read16(base + kCollisionMgrTopModifierTileTypeOffset);
  sample.TopSlopeType = nds->ARM9Read8(base + kCollisionMgrTopSlopeTypeOffset);
  sample.SideModifierTileTypeLeft =
      nds->ARM9Read16(base + kCollisionMgrSideModifierTileTypeOffset);
  sample.SideModifierTileTypeRight =
      nds->ARM9Read16(base + kCollisionMgrSideModifierTileTypeOffset + 2);
  sample.ByteA4 = nds->ARM9Read8(base + kCollisionMgrByteA4Offset);
  sample.ByteA5 = nds->ARM9Read8(base + kCollisionMgrByteA5Offset);
  sample.PreviousByteA4 =
      nds->ARM9Read8(base + kCollisionMgrPreviousByteA4Offset);
  sample.PreviousByteA5 =
      nds->ARM9Read8(base + kCollisionMgrPreviousByteA5Offset);
  sample.FlagsA8 = nds->ARM9Read8(base + kCollisionMgrFlagsA8Offset);
  sample.TileByteAB = nds->ARM9Read8(base + kCollisionMgrTileByteABOffset);
  sample.ModifierState =
      nds->ARM9Read8(base + kCollisionMgrModifierStateOffset);
  sample.UnknownB1 = nds->ARM9Read8(base + kCollisionMgrUnknownB1Offset);
  const auto readSensor = [nds](melonDS::u32 sensorBase) {
    PlayerCollisionMgrSample::Sensor sensor;
    if (!IsValidMainRAMRange(nds, sensorBase, 0x10))
      return sensor;
    sensor.Found = 1;
    sensor.Base = sensorBase;
    sensor.Type = nds->ARM9Read32(sensorBase);
    sensor.Value1 = nds->ARM9Read32(sensorBase + 4);
    sensor.Value2 = nds->ARM9Read32(sensorBase + 8);
    sensor.Value3 = nds->ARM9Read32(sensorBase + 12);
    return sensor;
  };
  sample.BottomSensor =
      readSensor(nds->ARM9Read32(base + kCollisionMgrBottomSensorPtrOffset));
  sample.TopSensor =
      readSensor(nds->ARM9Read32(base + kCollisionMgrTopSensorPtrOffset));
  sample.SideSensor =
      readSensor(nds->ARM9Read32(base + kCollisionMgrSideSensorPtrOffset));
  sample.LineSensor =
      readSensor(nds->ARM9Read32(base + kCollisionMgrLineSensorPtrOffset));
  return sample;
}

PlayerHitboxSample ReadPlayerHitboxSample(melonDS::NDS *nds,
                                          const ObjectScanSample &actor) {
  PlayerHitboxSample sample;
  if (!nds || !actor.Found ||
      !IsValidMainRAMRange(nds, actor.Base + kPlayerHitboxCenterOffsetX, 8) ||
      !IsValidMainRAMRange(nds, actor.Base + kStageActorHitboxHalfWidthOffset,
                           8))
    return sample;

  sample.Found = 1;
  sample.CenterOffsetX =
      nds->ARM9Read32(actor.Base + kPlayerHitboxCenterOffsetX);
  sample.CenterOffsetY =
      nds->ARM9Read32(actor.Base + kPlayerHitboxCenterOffsetY);
  sample.HalfWidth =
      nds->ARM9Read32(actor.Base + kStageActorHitboxHalfWidthOffset);
  sample.HalfHeight =
      nds->ARM9Read32(actor.Base + kStageActorHitboxHalfHeightOffset);
  return sample;
}

AIPlayerTileProbeSample
ReadAIPlayerTileProbeSample(melonDS::NDS *nds, const ObjectScanSample &actor) {
  AIPlayerTileProbeSample probe;
  static constexpr struct ProbeDef {
    const char *Name;
    int X;
    int Y;
    bool Directional;
  } kProbeDefs[kAITileProbeCount] = {
      {"center", 0, 0, false},         {"feet", 0, -24, false},
      {"below", 0, -48, false},        {"aheadBody", 16, 0, true},
      {"aheadFeet", 16, -24, true},    {"aheadBelow", 16, -48, true},
      {"ahead2Feet", 32, -24, true},   {"ahead2Below", 32, -48, true},
      {"above", 0, 24, false},         {"leftBody", -16, 0, false},
      {"leftFeet", -16, -24, false},   {"leftBelow", -16, -48, false},
      {"left2Below", -32, -48, false}, {"rightBody", 16, 0, false},
      {"rightFeet", 16, -24, false},   {"rightBelow", 16, -48, false},
      {"right2Below", 32, -48, false},
  };

  for (int i = 0; i < kAITileProbeCount; i++)
    probe.Samples[i].Name = kProbeDefs[i].Name;
  if (!nds || !actor.Found)
    return probe;

  const std::int32_t velX = SignedARM9U32(actor.VelX);
  const std::int32_t direction = velX < 0 ? -1 : 1;
  probe.Direction = static_cast<melonDS::u32>(direction);
  probe.StageLayout = nds->ARM9Read32(kStageLayoutPtrAddr);
  probe.WrapX = nds->ARM9Read32(kGameWrapXAddr);
  probe.Found = IsValidMainRAMRange(nds, probe.StageLayout,
                                    kStageLayoutCameraWrapAddOffset +
                                        sizeof(melonDS::u16))
                    ? 1
                    : 0;
  if (!probe.Found)
    return probe;

  const melonDS::u32 playerID =
      nds->ARM9Read8(actor.Base + kPlayerBasePlayerIDOffset) & 1u;
  const std::int32_t actorPixelX = SignedARM9U32(actor.PosX) >> 12;
  const std::int32_t actorPixelY = (-SignedARM9U32(actor.PosY)) >> 12;
  const std::int32_t anchorTileX = actorPixelX >> 4;
  const std::int32_t anchorTileY = actorPixelY >> 4;
  for (int i = 0; i < kAITileProbeCount; i++) {
    const int offsetX = kProbeDefs[i].Directional ? kProbeDefs[i].X * direction
                                                  : kProbeDefs[i].X;
    const int offsetY = kProbeDefs[i].Y;
    AITileProbeSample &sample = probe.Samples[i];
    sample.Name = kProbeDefs[i].Name;
    sample.OffsetX = static_cast<melonDS::u32>(offsetX);
    sample.OffsetY = static_cast<melonDS::u32>(offsetY);
    const melonDS::u32 worldX =
        actor.PosX + static_cast<melonDS::u32>(offsetX * 4096);
    const melonDS::u32 worldY =
        actor.PosY + static_cast<melonDS::u32>(offsetY * 4096);
    ReadStageLayoutTileBehavior(nds, worldX, worldY, playerID, sample);
  }
  for (int row = 0; row < kAITileGridHeight; row++) {
    for (int col = 0; col < kAITileGridWidth; col++) {
      const int index = row * kAITileGridWidth + col;
      const std::int32_t relTileX = kAITileGridMinRelX + col;
      const std::int32_t relTileY = kAITileGridMinRelY + row;
      const std::int32_t tileX = anchorTileX + relTileX;
      const std::int32_t tileY = anchorTileY + relTileY;
      AITileGridSample &cell = probe.Grid[index];
      cell.Row = static_cast<melonDS::u32>(row);
      cell.Col = static_cast<melonDS::u32>(col);
      cell.RelTileX = static_cast<melonDS::u32>(relTileX);
      cell.RelTileY = static_cast<melonDS::u32>(relTileY);
      cell.TileX = static_cast<melonDS::u32>(tileX);
      cell.TileY = static_cast<melonDS::u32>(tileY);
      cell.Tile.OffsetX = static_cast<melonDS::u32>(relTileX * 16);
      cell.Tile.OffsetY = static_cast<melonDS::u32>(-relTileY * 16);
      const std::int32_t pixelX = tileX * 16 + 8;
      const std::int32_t pixelY = tileY * 16 + 8;
      const melonDS::u32 worldX = static_cast<melonDS::u32>(pixelX * 4096);
      const melonDS::u32 worldY = static_cast<melonDS::u32>(-pixelY * 4096);
      ReadStageLayoutTileBehavior(nds, worldX, worldY, playerID, cell.Tile);
    }
  }
  return probe;
}

ObjectScanSample GetPlayerActorCached(
    int instanceID, int player, melonDS::NDS *nds,
    GameStateModel::StateSyncRuntime &runtime) {
  ObjectScanSample actor;
  if (instanceID < 0 || instanceID >= 16 || player < 0 || player > 1)
    return actor;

  const melonDS::u32 cachedBase =
      runtime.PlayerActorBaseCache[instanceID][player];
  const melonDS::u32 cachedGUID =
      runtime.PlayerActorGUIDCache[instanceID][player];
  if (cachedBase != 0 &&
      ReadPlayerActorByBase(nds, cachedBase, cachedGUID, actor))
    return actor;

  const PlayerActorScanSample players = FindPlayerActors(nds);
  actor = player == 0 ? players.Actor0 : players.Actor1;
  runtime.PlayerActorBaseCache[instanceID][player] =
      actor.Found ? actor.Base : 0;
  runtime.PlayerActorGUIDCache[instanceID][player] =
      actor.Found ? actor.GUID : 0;
  return actor;
}

void ReadDiagnosticFrameSnapshot(
    melonDS::NDS *nds, Diagnostics::DiagnosticFrameSnapshot &snapshot) {
  if (!nds || !nds->MainRAM)
    return;

  snapshot.StageID = nds->ARM9Read32(0x02085A14);
  snapshot.StageGroup = nds->ARM9Read32(0x02085A18);
  snapshot.VsMode = nds->ARM9Read32(0x02085A84);
  snapshot.LocalPlayerID = nds->ARM9Read32(0x02085A7C);
  snapshot.SceneCurrentSceneID = nds->ARM9Read16(0x0203BD34);
  snapshot.SceneNextSceneID = nds->ARM9Read16(0x0203BD30);
  snapshot.StageActorFreezeFlag = nds->ARM9Read8(0x020CA28C);
  snapshot.PlayerCount = nds->ARM9Read32(kGamePlayerCountAddr);
  snapshot.InputConsole0Held = nds->ARM9Read16(0x02087650);
  snapshot.InputConsole1Held = nds->ARM9Read16(0x02087654);
  snapshot.InputPlayer0Held = nds->ARM9Read16(0x02087660);
  snapshot.InputPlayer1Held = nds->ARM9Read16(0x02087662);
  snapshot.PlayerGlobalHash = HashMainRAMRange(nds, kGamePlayerPowerupAddr, 0xC0);
  snapshot.PlayerGlobalHash0 = HashMainRAMRange(nds, kGamePlayerPowerupAddr, 0x60);
  snapshot.PlayerGlobalHash1 =
      HashMainRAMRange(nds, kGamePlayerPowerupAddr + 0x60, 0x60);
  snapshot.StageCameraGlobalX0 = nds->ARM9Read32(0x020CAE1C);
  snapshot.StageCameraGlobalX1 = nds->ARM9Read32(0x020CAE20);
  snapshot.StageCameraGlobalY0 = nds->ARM9Read32(0x020CAD94);
  snapshot.StageCameraGlobalY1 = nds->ARM9Read32(0x020CAD98);
  snapshot.StageCameraGlobalWidth0 = nds->ARM9Read32(0x020CADA4);
  snapshot.StageCameraGlobalWidth1 = nds->ARM9Read32(0x020CADA8);
  snapshot.StageCameraGlobalHeight0 = nds->ARM9Read32(0x020CAD8C);
  snapshot.StageCameraGlobalHeight1 = nds->ARM9Read32(0x020CAD90);
}

void ReadDiagnosticPlayerSnapshot(
    int instanceID, melonDS::u32 frame, melonDS::NDS *nds, int player,
    GameStateModel::StateSyncRuntime &runtime,
    Diagnostics::DiagnosticPlayerSnapshot &snapshot) {
  snapshot = {};
  if (!nds || !nds->MainRAM || instanceID < 0 || instanceID >= 16 ||
      player < 0 || player > 1)
    return;

  ObjectScanSample actor;
  const melonDS::u32 cachedBase =
      runtime.PlayerActorBaseCache[instanceID][player];
  const melonDS::u32 cachedGUID =
      runtime.PlayerActorGUIDCache[instanceID][player];
  if (cachedBase != 0)
    ReadPlayerActorByBase(nds, cachedBase, cachedGUID, actor);
  if (!actor.Found && (frame % 60) == 0)
    actor = GetPlayerActorCached(instanceID, player, nds, runtime);

  snapshot.Found = actor.Found;
  snapshot.Base = actor.Base;
  snapshot.GUID = actor.GUID;
  snapshot.Settings = actor.Settings;
  snapshot.StateType = actor.StateType;
  snapshot.Flags = actor.Flags;
  snapshot.PosX = actor.PosX;
  snapshot.PosY = actor.PosY;
  snapshot.PosZ = actor.PosZ;
  snapshot.PrevX = actor.PrevX;
  snapshot.PrevY = actor.PrevY;
  snapshot.PrevZ = actor.PrevZ;
  snapshot.VelX = actor.VelX;
  snapshot.VelY = actor.VelY;
  snapshot.VelZ = actor.VelZ;

  if (actor.Found && IsValidMainRAMRange(
                         nds, actor.Base, kPlayerBaseTransitionStepOffset + 1)) {
    snapshot.ActionFlag =
        nds->ARM9Read32(actor.Base + kPlayerBaseActionFlagOffset);
    snapshot.SubActionFlag =
        nds->ARM9Read32(actor.Base + kPlayerBaseSubActionFlagOffset);
    snapshot.PhysicsFlag =
        nds->ARM9Read32(actor.Base + kPlayerBasePhysicsFlagOffset);
    snapshot.DamageCooldown =
        nds->ARM9Read16(actor.Base + kPlayerBaseDamageCooldownOffset);
    snapshot.TransitionFlag =
        nds->ARM9Read32(actor.Base + kPlayerBaseTransitionFlagOffset);
    snapshot.CollisionFlag =
        nds->ARM9Read32(actor.Base + kPlayerBaseCollisionFlagOffset);
    snapshot.EnvironmentFlag =
        nds->ARM9Read32(actor.Base + kPlayerBaseEnvironmentFlagOffset);
    snapshot.LinkedActor =
        nds->ARM9Read32(actor.Base + kPlayerBaseLinkedActorOffset);
    snapshot.TransitionStep =
        nds->ARM9Read8(actor.Base + kPlayerBaseTransitionStepOffset);
    snapshot.UpdateLocked =
        nds->ARM9Read8(actor.Base + kPlayerBaseUpdateLockedOffset);
    snapshot.CharacterIDBase =
        nds->ARM9Read8(actor.Base + kPlayerBaseCharacterIDOffset);
    snapshot.TransitioningFlag =
        nds->ARM9Read8(actor.Base + kPlayerBaseTransitioningFlagOffset);
    snapshot.CameraFocusMode =
        nds->ARM9Read8(actor.Base + kPlayerBaseCameraFocusModeOffset);
    snapshot.DefeatedFlag =
        nds->ARM9Read8(actor.Base + kPlayerBaseDefeatedFlagOffset);
    snapshot.PlayerBaseID =
        nds->ARM9Read8(actor.Base + kPlayerBasePlayerIDOffset);
    snapshot.VisibleFlag =
        nds->ARM9Read8(actor.Base + kPlayerBaseVisibleFlagOffset);
    snapshot.TransitFunc =
        nds->ARM9Read32(actor.Base + kPlayerBaseTransitFuncOffset);
    snapshot.TransitArg =
        nds->ARM9Read32(actor.Base + kPlayerBaseTransitArgOffset);
  }

  const melonDS::u32 p = static_cast<melonDS::u32>(player);
  snapshot.Powerup = nds->ARM9Read8(kGamePlayerPowerupAddr + p);
  snapshot.InventoryPowerup =
      nds->ARM9Read8(kGamePlayerInventoryPowerupAddr + p);
  snapshot.Dead = nds->ARM9Read8(kGamePlayerDeadAddr + p);
  snapshot.Character = nds->ARM9Read8(kGamePlayerCharacterAddr + p);
  snapshot.TransitionStatus = nds->ARM9Read32(
      kGamePlayerTransitionStatusAddr + sizeof(melonDS::u32) * p);
  snapshot.Lives =
      nds->ARM9Read32(kGamePlayerLivesAddr + sizeof(melonDS::u32) * p);
  snapshot.BattleStars = nds->ARM9Read32(
      kGamePlayerBattleStarsAddr + sizeof(melonDS::u32) * p);
  snapshot.Coins =
      nds->ARM9Read32(kGamePlayerCoinsAddr + sizeof(melonDS::u32) * p);
  snapshot.Score =
      nds->ARM9Read32(kGamePlayerScoreAddr + sizeof(melonDS::u32) * p);
  snapshot.DisplayedStars = nds->ARM9Read32(
      kGamePlayerDisplayedStarsAddr + sizeof(melonDS::u32) * p);
  snapshot.Deaths =
      nds->ARM9Read32(kGamePlayerDeathsAddr + sizeof(melonDS::u32) * p);
  snapshot.CollectedStars = nds->ARM9Read32(
      kGamePlayerCollectedStarsAddr + sizeof(melonDS::u32) * p);
}

std::vector<ObjectScanSample> GetWorldMovingHazardsCached(
    int instanceID, melonDS::u32 frame, melonDS::NDS *nds,
    GameStateModel::StateSyncRuntime &runtime, int actorRescanInterval) {
  std::vector<ObjectScanSample> actors;
  if (instanceID < 0 || instanceID >= 16)
    return actors;

  const bool periodicRescan =
      actorRescanInterval > 0 &&
      frame % static_cast<melonDS::u32>(actorRescanInterval) == 0;
  if (!periodicRescan) {
    const melonDS::u32 count = std::min(
        runtime.WorldMovingHazardCacheCounts[instanceID],
        static_cast<melonDS::u32>(WireProtocol::kMaxWorldMovingHazards));
    bool valid = count > 0;
    for (melonDS::u32 index = 0; index < count; index++) {
      ObjectScanSample actor;
      if (!ReadObjectByBase(
              nds, runtime.WorldMovingHazardBaseCaches[instanceID][index],
              runtime.WorldMovingHazardGUIDCaches[instanceID][index],
              kVsMovingHazardObjectID, kVsMovingHazardSettings, actor) ||
          actor.StateType != 1) {
        valid = false;
        break;
      }
      actors.push_back(actor);
    }
    if (valid)
      return actors;
    actors.clear();
  }

  actors = FindActiveObjectsByIDAndSettings(
      nds, kVsMovingHazardObjectID, kVsMovingHazardSettings);
  if (actors.size() > WireProtocol::kMaxWorldMovingHazards)
    actors.erase(actors.begin(),
                 actors.end() - WireProtocol::kMaxWorldMovingHazards);
  runtime.WorldMovingHazardCacheCounts[instanceID] =
      static_cast<melonDS::u32>(actors.size());
  for (std::size_t index = 0;
       index < WireProtocol::kMaxWorldMovingHazards; index++) {
    runtime.WorldMovingHazardBaseCaches[instanceID][index] =
        index < actors.size() ? actors[index].Base : 0;
    runtime.WorldMovingHazardGUIDCaches[instanceID][index] =
        index < actors.size() ? actors[index].GUID : 0;
  }
  return actors;
}

ObjectScanSample GetWorldActorCached(
    int instanceID, melonDS::u32 frame, melonDS::NDS *nds,
    melonDS::u16 objectID, melonDS::u32 settings, melonDS::u32 *baseCache,
    melonDS::u32 *guidCache, int actorRescanInterval) {
  ObjectScanSample actor;
  if (instanceID < 0 || instanceID >= 16)
    return actor;

  ObjectScanSample cachedActor;
  const bool periodicRescan =
      actorRescanInterval > 0 &&
      frame % static_cast<melonDS::u32>(actorRescanInterval) == 0;
  if (!periodicRescan && baseCache[instanceID] != 0 &&
      ReadObjectByBase(nds, baseCache[instanceID], guidCache[instanceID],
                       objectID, settings, cachedActor) &&
      cachedActor.StateType == 1)
    return cachedActor;

  actor = FindNewestActiveObjectByIDAndSettings(nds, objectID, settings);
  if (!actor.Found)
    actor = cachedActor;
  baseCache[instanceID] = actor.Found ? actor.Base : 0;
  guidCache[instanceID] = actor.Found ? actor.GUID : 0;
  return actor;
}

void FillWireWorldActorState(
    const ObjectScanSample &actor,
    WireProtocol::WireWorldActorState &state) {
  state.Found = actor.Found;
  state.GUID = actor.GUID;
  state.Settings = actor.Settings;
  state.StateType = actor.StateType;
  state.Flags = actor.Flags;
  state.PosX = actor.PosX;
  state.PosY = actor.PosY;
  state.PosZ = actor.PosZ;
  state.PrevX = actor.PrevX;
  state.PrevY = actor.PrevY;
  state.PrevZ = actor.PrevZ;
  state.VelX = actor.VelX;
  state.VelY = actor.VelY;
  state.VelZ = actor.VelZ;
  state.LastStepX = actor.LastStepX;
  state.LastStepY = actor.LastStepY;
  state.LastStepZ = actor.LastStepZ;
  state.VelH = actor.VelH;
  state.TargetVelH = actor.TargetVelH;
  state.AccelV = actor.AccelV;
  state.TargetVelV = actor.TargetVelV;
  state.AccelH = actor.AccelH;
  state.TargetVelX = actor.TargetVelX;
  state.TargetVelY = actor.TargetVelY;
  state.TargetVelZ = actor.TargetVelZ;
}

std::vector<WorldActorSnapshotCandidate>
CollectWorldActorSnapshotCandidates(melonDS::NDS *nds) {
  const auto isCandidate = [](const GameStateObjectScanEntry &entry) {
    if (!entry.Actor.Found || entry.Actor.StateType == 0 ||
        entry.Actor.StateType > 2 || entry.Actor.Flags >= 0x10000000)
      return false;
    switch (entry.ObjectID) {
    case kPlayerObjectID:
    case kVsBattleStarActorObjectID:
    case kVsMovingHazardObjectID:
    case kStageSceneObjectID:
    case kStageFXObjectID:
    case kStageActorManagerObjectID:
    case kStageControllerObjectID:
    case kMvlObject267ID:
    case kVsConnectObjectID:
    case kCourseSelectObjectID:
    case kStageCameraObjectID:
      return false;
    default:
      return entry.ObjectID != 0 && entry.ObjectID < 0x0300;
    }
  };

  std::vector<WorldActorSnapshotCandidate> actors;
  const GameStateObjectScanCache cache = BuildGameStateObjectScanCache(nds);
  actors.reserve(
      std::min(cache.Entries.size(), WireProtocol::kMaxWorldActorSnapshots));
  for (const GameStateObjectScanEntry &entry : cache.Entries) {
    if (isCandidate(entry))
      actors.push_back({entry.ObjectID, entry.Actor});
  }
  std::sort(actors.begin(), actors.end(),
            [](const WorldActorSnapshotCandidate &lhs,
               const WorldActorSnapshotCandidate &rhs) {
              if (lhs.ObjectID != rhs.ObjectID)
                return lhs.ObjectID < rhs.ObjectID;
              if (lhs.Actor.Settings != rhs.Actor.Settings)
                return lhs.Actor.Settings < rhs.Actor.Settings;
              if (lhs.Actor.PosX != rhs.Actor.PosX)
                return lhs.Actor.PosX < rhs.Actor.PosX;
              return lhs.Actor.GUID < rhs.Actor.GUID;
            });
  if (actors.size() > WireProtocol::kMaxWorldActorSnapshots)
    actors.resize(WireProtocol::kMaxWorldActorSnapshots);
  return actors;
}

void ReadPlayerGlobalState(melonDS::NDS *nds, melonDS::u32 player,
                           WireProtocol::WirePlayerState &state) {
  if (!nds || !nds->MainRAM || player > 1)
    return;
  state.PlayerCount = nds->ARM9Read32(kGamePlayerCountAddr);
  state.Powerup = nds->ARM9Read8(kGamePlayerPowerupAddr + player);
  state.InventoryPowerup =
      nds->ARM9Read8(kGamePlayerInventoryPowerupAddr + player);
  state.Dead = nds->ARM9Read8(kGamePlayerDeadAddr + player);
  state.Character = nds->ARM9Read8(kGamePlayerCharacterAddr + player);
  state.TransitionStatus = nds->ARM9Read32(
      kGamePlayerTransitionStatusAddr + sizeof(melonDS::u32) * player);
  state.Lives = nds->ARM9Read32(kGamePlayerLivesAddr +
                                sizeof(melonDS::u32) * player);
  state.BattleStars = nds->ARM9Read32(
      kGamePlayerBattleStarsAddr + sizeof(melonDS::u32) * player);
  state.Coins = nds->ARM9Read32(kGamePlayerCoinsAddr +
                                sizeof(melonDS::u32) * player);
  state.Score = nds->ARM9Read32(kGamePlayerScoreAddr +
                                sizeof(melonDS::u32) * player);
  state.DisplayedStars = nds->ARM9Read32(
      kGamePlayerDisplayedStarsAddr + sizeof(melonDS::u32) * player);
  state.Deaths = nds->ARM9Read32(kGamePlayerDeathsAddr +
                                 sizeof(melonDS::u32) * player);
  state.CollectedStars = nds->ARM9Read32(
      kGamePlayerCollectedStarsAddr + sizeof(melonDS::u32) * player);
}

WireProtocol::WirePlayerState BuildPlayerStatePacket(
    melonDS::NDS *nds, melonDS::u32 instance, melonDS::u32 frame,
    int player, bool includeGlobals,
    GameStateModel::StateSyncRuntime &runtime) {
  const ObjectScanSample actor =
      GetPlayerActorCached(static_cast<int>(instance), player, nds, runtime);
  const bool found = actor.Found != 0;

  WireProtocol::WirePlayerState packet{};
  packet.Magic = WireProtocol::kMagic;
  packet.Version = WireProtocol::kVersion;
  packet.Kind = WireProtocol::kWireKindPlayerState;
  packet.Frame = frame;
  packet.Instance = instance;
  packet.Player = static_cast<melonDS::u32>(player);
  packet.Found = found ? 1u : 0u;
  if (includeGlobals)
    ReadPlayerGlobalState(nds, packet.Player, packet);
  packet.GUID = actor.GUID;
  packet.Settings = actor.Settings;
  packet.StateType = actor.StateType;
  packet.Flags = actor.Flags;
  packet.PosX = actor.PosX;
  packet.PosY = actor.PosY;
  packet.PosZ = actor.PosZ;
  packet.PrevX = actor.PrevX;
  packet.PrevY = actor.PrevY;
  packet.PrevZ = actor.PrevZ;
  packet.VelX = actor.VelX;
  packet.VelY = actor.VelY;
  packet.VelZ = actor.VelZ;
  if (found && IsARM9MainRAMAddress(actor.Base)) {
    packet.ActionFlag =
        nds->ARM9Read32(actor.Base + kPlayerBaseActionFlagOffset);
    packet.SubActionFlag =
        nds->ARM9Read32(actor.Base + kPlayerBaseSubActionFlagOffset);
    packet.PhysicsFlag =
        nds->ARM9Read32(actor.Base + kPlayerBasePhysicsFlagOffset);
    packet.DamageCooldown =
        nds->ARM9Read16(actor.Base + kPlayerBaseDamageCooldownOffset);
    packet.TransitionFlag =
        nds->ARM9Read32(actor.Base + kPlayerBaseTransitionFlagOffset);
    packet.CollisionFlag =
        nds->ARM9Read32(actor.Base + kPlayerBaseCollisionFlagOffset);
    packet.EnvironmentFlag =
        nds->ARM9Read32(actor.Base + kPlayerBaseEnvironmentFlagOffset);
    packet.RuntimeFlags0 =
        (static_cast<melonDS::u32>(
             nds->ARM9Read8(actor.Base + kPlayerBaseUpdateLockedOffset)) &
         0xFFu) |
        ((static_cast<melonDS::u32>(
              nds->ARM9Read8(actor.Base + kPlayerBaseCharacterIDOffset)) &
          0xFFu)
         << 8) |
        ((static_cast<melonDS::u32>(
              nds->ARM9Read8(actor.Base +
                             kPlayerBaseTransitioningFlagOffset)) &
          0xFFu)
         << 16) |
        ((static_cast<melonDS::u32>(
              nds->ARM9Read8(actor.Base +
                             kPlayerBaseCameraFocusModeOffset)) &
          0xFFu)
         << 24);
    packet.RuntimeFlags1 =
        (static_cast<melonDS::u32>(
             nds->ARM9Read8(actor.Base + kPlayerBaseDefeatedFlagOffset)) &
         0xFFu) |
        ((static_cast<melonDS::u32>(
              nds->ARM9Read8(actor.Base + kPlayerBasePlayerIDOffset)) &
          0xFFu)
         << 8) |
        ((static_cast<melonDS::u32>(
              nds->ARM9Read8(actor.Base + kPlayerBaseVisibleFlagOffset)) &
          0xFFu)
         << 16);
  }
  return packet;
}

WireProtocol::WireWorldState BuildWorldStatePacket(
    melonDS::NDS *nds, melonDS::u32 instance, melonDS::u32 frame,
    bool includeItems, int actorRescanInterval,
    GameStateModel::StateSyncRuntime &runtime) {
  const ObjectScanSample star = GetWorldActorCached(
      static_cast<int>(instance), frame, nds, kVsBattleStarActorObjectID,
      kVsBattleStarActorSettings, runtime.WorldStarActorBaseCache,
      runtime.WorldStarActorGUIDCache, actorRescanInterval);
  ObjectScanSample neutralItem;
  ObjectScanSample item;
  ObjectScanSample droppedStarItem;
  if (includeItems) {
    neutralItem = FindNewestActiveObjectByIDAndSettings(
        nds, kVsWorldItemObjectID, kVsNeutralWorldItemSettings, true);
    item = FindNewestActiveObjectByIDAndSettings(
        nds, kVsWorldItemObjectID, kVsWorldItemSettings, true);
    droppedStarItem = FindNewestActiveObjectByIDAndSettings(
        nds, kVsWorldItemObjectID, kVsDroppedStarItemSettings, true);
  }

  WireProtocol::WireWorldState packet{};
  packet.Magic = WireProtocol::kMagic;
  packet.Version = WireProtocol::kVersion;
  packet.Kind = WireProtocol::kWireKindWorldState;
  packet.Frame = frame;
  packet.Instance = instance;
  FillWireWorldActorState(star, packet.Star);
  FillWireWorldActorState(neutralItem, packet.NeutralItem);
  FillWireWorldActorState(item, packet.Item);
  FillWireWorldActorState(droppedStarItem, packet.DroppedStarItem);
  return packet;
}

bool ReadWorldEffectSlot(melonDS::NDS *nds, melonDS::u32 base,
                         WireProtocol::WireWorldEffectSlot &slot) {
  if (base < 0x02100000u || !nds || !nds->MainRAM ||
      !IsValidMainRAMRange(nds, base,
                           kWorldEffectWordEnd + sizeof(melonDS::u32)))
    return false;

  melonDS::u32 vtable = 0;
  if (!ReadMainRAMAddressU32(nds, base, vtable) ||
      (vtable != kEffectVTablePtr && vtable != kEffectVTableStart))
    return false;
  for (melonDS::u32 relativeOffset = 0x04; relativeOffset <= 0x10C;
       relativeOffset += sizeof(melonDS::u32)) {
    melonDS::u32 value = 0;
    ReadMainRAMAddressU32(nds, base + relativeOffset, value);
    if (value == 0 || value == 0x020391F8u || value == 0x02039208u ||
        value == kEffectVTablePtr || value == kEffectVTableStart)
      continue;
    if (relativeOffset == 0xA8 && (value & 0x0000FFFFu) == 0)
      continue;

    slot.Found = 1;
    slot.Base = base;
    slot.VTable = vtable;
    for (std::size_t index = 0;
         index < WireProtocol::kWorldEffectWordCount; index++) {
      const melonDS::u32 wordOffset =
          kWorldEffectWordStart +
          static_cast<melonDS::u32>(index * sizeof(melonDS::u32));
      ReadMainRAMAddressU32(nds, base + wordOffset, slot.Words[index]);
    }
    return true;
  }
  return false;
}

bool BuildWorldEffectStatePacket(
    melonDS::NDS *nds, melonDS::u32 instance, melonDS::u32 frame,
    WireProtocol::WireWorldEffectState &packet) {
  packet = {};
  packet.Magic = WireProtocol::kMagic;
  packet.Version = WireProtocol::kVersion;
  packet.Kind = WireProtocol::kWireKindWorldEffectState;
  packet.Frame = frame;
  packet.Instance = instance;
  for (melonDS::u32 slotIndex = 0;
       slotIndex < kWorldEffectSlotCount &&
       packet.Count < WireProtocol::kMaxWorldEffects;
       slotIndex++) {
    const melonDS::u32 base =
        kWorldEffectSlotBase + slotIndex * kWorldEffectSlotStride;
    WireProtocol::WireWorldEffectSlot slot{};
    if (ReadWorldEffectSlot(nds, base, slot))
      packet.Effects[packet.Count++] = slot;
  }
  return packet.Count != 0;
}

WireProtocol::WireMovingHazardState BuildMovingHazardStatePacket(
    melonDS::NDS *nds, melonDS::u32 instance, melonDS::u32 frame,
    int actorRescanInterval, GameStateModel::StateSyncRuntime &runtime) {
  const std::vector<ObjectScanSample> actors = GetWorldMovingHazardsCached(
      static_cast<int>(instance), frame, nds, runtime, actorRescanInterval);
  WireProtocol::WireMovingHazardState packet{};
  packet.Magic = WireProtocol::kMagic;
  packet.Version = WireProtocol::kVersion;
  packet.Kind = WireProtocol::kWireKindMovingHazardState;
  packet.Frame = frame;
  packet.Instance = instance;
  packet.Count = static_cast<melonDS::u32>(
      std::min(actors.size(), WireProtocol::kMaxWorldMovingHazards));
  for (melonDS::u32 index = 0; index < packet.Count; index++)
    FillWireWorldActorState(actors[index], packet.Actors[index]);
  return packet;
}

bool BuildWorldActorSnapshotStatePacket(
    melonDS::NDS *nds, melonDS::u32 instance, melonDS::u32 frame,
    WireProtocol::WireWorldActorSnapshotState &packet) {
  const std::vector<WorldActorSnapshotCandidate> actors =
      CollectWorldActorSnapshotCandidates(nds);
  if (actors.empty())
    return false;

  packet = {};
  packet.Magic = WireProtocol::kMagic;
  packet.Version = WireProtocol::kVersion;
  packet.Kind = WireProtocol::kWireKindWorldActorSnapshot;
  packet.Frame = frame;
  packet.Instance = instance;
  packet.Count = static_cast<melonDS::u32>(
      std::min(actors.size(), WireProtocol::kMaxWorldActorSnapshots));
  for (melonDS::u32 index = 0; index < packet.Count; index++) {
    packet.Actors[index].ObjectID = actors[index].ObjectID;
    FillWireWorldActorState(actors[index].Actor,
                            packet.Actors[index].Actor);
  }
  return true;
}

} // namespace NsmbNetplayPoC::GameStateReader
