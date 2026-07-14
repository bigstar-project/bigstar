#include "NsmbGameStateReader.h"

#include "NDS.h"

namespace NsmbNetplayPoC::GameStateReader {

namespace {

bool IsARM9MainRAMAddress(melonDS::u32 address) {
  return (address & 0xFF000000u) == 0x02000000u;
}

bool IsValidMainRAMRange(melonDS::NDS *nds, melonDS::u32 address,
                         melonDS::u32 length) {
  constexpr melonDS::u32 kMainRAMBase = 0x02000000;
  if (!nds || !nds->MainRAM || length == 0 || address < kMainRAMBase)
    return false;
  const melonDS::u32 offset = address - kMainRAMBase;
  const melonDS::u32 ramLength = nds->MainRAMMask + 1;
  return offset < ramLength && length <= ramLength - offset;
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

} // namespace NsmbNetplayPoC::GameStateReader
