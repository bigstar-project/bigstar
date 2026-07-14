#include "NsmbGameStateReader.h"

#include "NDS.h"

namespace NsmbNetplayPoC::GameStateReader {

namespace {

bool IsARM9MainRAMAddress(melonDS::u32 address) {
  return (address & 0xFF000000u) == 0x02000000u;
}

} // namespace

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

} // namespace NsmbNetplayPoC::GameStateReader
