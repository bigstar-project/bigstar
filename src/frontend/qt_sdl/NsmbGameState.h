#ifndef NSMBGAMESTATE_H
#define NSMBGAMESTATE_H

#include "NsmbNetplayProtocol.h"
#include "types.h"

#include <cstddef>
#include <fstream>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>

namespace NsmbNetplayPoC::GameStateModel {

constexpr int kAITileProbeCount = 17;
constexpr int kAITileGridWidth = 33;
constexpr int kAITileGridHeight = 17;
constexpr int kAITileGridMinRelX = -16;
constexpr int kAITileGridMinRelY = -10;
constexpr int kAITileGridCount = kAITileGridWidth * kAITileGridHeight;
constexpr int kAISpecialHandlerWordCount = 4;
constexpr int kAIFireballSlotCount = 16;
constexpr int kAIFireballSlotDebugWordCount = 16;
constexpr int kAIFireballSlotStateByteCount = 12;
constexpr int kObjectTraceSlots = 16;

struct PlayerCollisionMgrSample {
  struct Sensor {
    melonDS::u32 Found = 0;
    melonDS::u32 Base = 0;
    melonDS::u32 Type = 0;
    melonDS::u32 Value1 = 0;
    melonDS::u32 Value2 = 0;
    melonDS::u32 Value3 = 0;
  };

  melonDS::u32 Found = 0;
  melonDS::u32 Base = 0;
  melonDS::u32 DeltaX = 0;
  melonDS::u32 DeltaY = 0;
  melonDS::u32 CollisionResult = 0;
  melonDS::u32 GroundCollision = 0;
  melonDS::u32 AttachedTileX = 0;
  melonDS::u32 AttachedTileY = 0;
  melonDS::u32 BottomModifierTileType = 0;
  melonDS::u32 BottomSlopeType = 0;
  melonDS::u32 TopModifierTileType = 0;
  melonDS::u32 TopSlopeType = 0;
  melonDS::u32 SideModifierTileTypeLeft = 0;
  melonDS::u32 SideModifierTileTypeRight = 0;
  melonDS::u32 ByteA4 = 0;
  melonDS::u32 ByteA5 = 0;
  melonDS::u32 PreviousByteA4 = 0;
  melonDS::u32 PreviousByteA5 = 0;
  melonDS::u32 FlagsA8 = 0;
  melonDS::u32 TileByteAB = 0;
  melonDS::u32 ModifierState = 0;
  melonDS::u32 UnknownB1 = 0;
  Sensor BottomSensor;
  Sensor TopSensor;
  Sensor SideSensor;
  Sensor LineSensor;
};

struct PlayerHitboxSample {
  melonDS::u32 Found = 0;
  melonDS::u32 CenterOffsetX = 0;
  melonDS::u32 CenterOffsetY = 0;
  melonDS::u32 HalfWidth = 0;
  melonDS::u32 HalfHeight = 0;
};

struct AITileProbeSample {
  const char *Name = "";
  melonDS::u32 Found = 0;
  melonDS::u32 Status = 0;
  melonDS::u32 StageLayout = 0;
  melonDS::u32 ChunkPtr = 0;
  melonDS::u32 BehaviorTable = 0;
  melonDS::u32 WorldX = 0;
  melonDS::u32 WorldY = 0;
  melonDS::u32 PixelX = 0;
  melonDS::u32 PixelY = 0;
  melonDS::u32 OffsetX = 0;
  melonDS::u32 OffsetY = 0;
  melonDS::u32 ChunkID = 0;
  melonDS::u32 TileID = 0;
  melonDS::u32 Behavior = 0;
};

struct AITileGridSample {
  melonDS::u32 Row = 0;
  melonDS::u32 Col = 0;
  melonDS::u32 RelTileX = 0;
  melonDS::u32 RelTileY = 0;
  melonDS::u32 TileX = 0;
  melonDS::u32 TileY = 0;
  AITileProbeSample Tile;
};

struct AIPlayerTileProbeSample {
  melonDS::u32 Found = 0;
  melonDS::u32 StageLayout = 0;
  melonDS::u32 WrapX = 0;
  melonDS::u32 Direction = 1;
  AITileProbeSample Samples[kAITileProbeCount];
  AITileGridSample Grid[kAITileGridCount];
};

struct AITerrainDerivedSummary {
  int GroundBelowSolid = 0;
  int BlockedAhead = 0;
  int BlockedLeft = 0;
  int BlockedRight = 0;
  int HoleAhead = 0;
  int HoleLeft = 0;
  int HoleRight = 0;
  int FarHoleLeft = 0;
  int FarHoleRight = 0;
  int EffectiveGroundBelowSolid = 0;
  int HoleSuppressedByContact = 0;
  int EffectiveHoleAhead = 0;
  int EffectiveHoleLeft = 0;
  int EffectiveHoleRight = 0;
};

struct GameStateSample {
  melonDS::u32 StageID = 0;
  melonDS::u32 StageGroup = 0;
  melonDS::u32 VsMode = 0;
  melonDS::u32 LocalPlayerID = 0;
  melonDS::u32 Arm9PC = 0;
  melonDS::u32 Arm9LR = 0;
  melonDS::u32 Arm9SP = 0;
  melonDS::u32 Arm9CPSR = 0;
  melonDS::u32 AppFrameLength = 0;
  melonDS::u32 AppUpdateTask = 0;
  melonDS::u32 AppSleepPhase = 0;
  melonDS::u32 AppSleepControl = 0;
  melonDS::u32 AppSleeping = 0;
  melonDS::u32 AppSleepPhaseTimer = 0;
  melonDS::u32 AppSleepWakeUpTimer = 0;
  melonDS::u32 AppBootParam = 0;
  melonDS::u32 AppBootTarget = 0;
  melonDS::u32 AppBootScene = 0;
  melonDS::u32 GGID = 0;
  melonDS::u32 NetCurrentLanguage = 0;
  melonDS::u32 NetLocalAid = 0;
  melonDS::u32 NetState14 = 0;
  melonDS::u32 NetState1C = 0;
  melonDS::u32 NetState20 = 0;
  melonDS::u32 NetState24 = 0;
  melonDS::u32 NetExpectedConsoleCount = 0;
  melonDS::u32 NetMultiBootSession = 0;
  melonDS::u32 NetSessionState = 0;
  melonDS::u32 NetModuleState = 0;
  melonDS::u32 NetMaxSessionChildren = 0;
  melonDS::u32 NetMaxConsoleCount = 0;
  melonDS::u32 NetState5C = 0;
  melonDS::u32 NetPacketTick = 0;
  melonDS::u32 NetPacketKeys = 0;
  melonDS::u32 NetPacketAction = 0;
  melonDS::u32 NetPacketByte5 = 0;
  melonDS::u32 NetPacketByte6 = 0;
  melonDS::u32 NetPacketByte7 = 0;
  melonDS::u32 NetRandomValue = 0;
  melonDS::u32 NetRandomCallCount = 0;
  melonDS::u32 NetRandomBranchAddress = 0;
  melonDS::u32 InputConsole0Held = 0;
  melonDS::u32 InputConsole0Pressed = 0;
  melonDS::u32 InputConsole1Held = 0;
  melonDS::u32 InputConsole1Pressed = 0;
  melonDS::u32 InputPlayer0Held = 0;
  melonDS::u32 InputPlayer1Held = 0;
  melonDS::u32 InputPlayer0Pressed = 0;
  melonDS::u32 InputPlayer1Pressed = 0;
  melonDS::u32 StageActorFreezeFlag = 0;
  melonDS::u32 SceneIsSceneActive = 0;
  melonDS::u32 ScenePreviousSceneID = 0;
  melonDS::u32 SceneNextSceneID = 0;
  melonDS::u32 SceneCurrentSceneID = 0;
  melonDS::u32 SceneNextSceneSettings = 0;
  melonDS::u32 VsStarFound = 0;
  melonDS::u32 VsStarGUID = 0;
  melonDS::u32 VsStarBase = 0;
  melonDS::u32 VsStarSettings = 0;
  melonDS::u32 VsStarStateType = 0;
  melonDS::u32 VsStarFlags = 0;
  melonDS::u32 VsStarPosX = 0;
  melonDS::u32 VsStarPosY = 0;
  melonDS::u32 VsStarPosZ = 0;
  melonDS::u32 VsStarActorFound = 0;
  melonDS::u32 VsStarActorGUID = 0;
  melonDS::u32 VsStarActorBase = 0;
  melonDS::u32 VsStarActorSettings = 0;
  melonDS::u32 VsStarActorStateType = 0;
  melonDS::u32 VsStarActorFlags = 0;
  melonDS::u32 VsStarActorPosX = 0;
  melonDS::u32 VsStarActorPosY = 0;
  melonDS::u32 VsStarActorPosZ = 0;
  melonDS::u32 PlayerActor0Found = 0;
  melonDS::u32 PlayerActor0GUID = 0;
  melonDS::u32 PlayerActor0Base = 0;
  melonDS::u32 PlayerActor0Settings = 0;
  melonDS::u32 PlayerActor0StateType = 0;
  melonDS::u32 PlayerActor0Flags = 0;
  melonDS::u32 PlayerActor0PosX = 0;
  melonDS::u32 PlayerActor0PosY = 0;
  melonDS::u32 PlayerActor0PosZ = 0;
  melonDS::u32 PlayerActor0PrevX = 0;
  melonDS::u32 PlayerActor0PrevY = 0;
  melonDS::u32 PlayerActor0PrevZ = 0;
  melonDS::u32 PlayerActor0VelX = 0;
  melonDS::u32 PlayerActor0VelY = 0;
  melonDS::u32 PlayerActor0VelZ = 0;
  melonDS::u32 PlayerActor0PlayerID = 0;
  melonDS::u32 PlayerActor0TransitionStep = 0;
  melonDS::u32 PlayerActor0SignalLock = 0;
  melonDS::u32 PlayerActor0Flag192 = 0;
  melonDS::u32 PlayerActor0Flags728 = 0;
  melonDS::u32 PlayerActor0Flags72C = 0;
  melonDS::u32 PlayerActor0Flags730 = 0;
  melonDS::u32 PlayerActor0LinkedActor = 0;
  melonDS::u32 PlayerActor0TransitionFlag = 0;
  melonDS::u32 PlayerActor0CollisionFlag = 0;
  melonDS::u32 PlayerActor0EnvironmentFlag = 0;
  melonDS::u32 PlayerActor0UpdateLocked = 0;
  melonDS::u32 PlayerActor0ControlState = 0;
  melonDS::u32 PlayerActor0CharacterIDBase = 0;
  melonDS::u32 PlayerActor0RequestedPowerup = 0;
  melonDS::u32 PlayerActor0CurrentPowerup = 0;
  melonDS::u32 PlayerActor0PreviousPowerup = 0;
  melonDS::u32 PlayerActor0TransitioningFlag = 0;
  melonDS::u32 PlayerActor0CameraFocusMode = 0;
  melonDS::u32 PlayerActor0DefeatedFlag = 0;
  melonDS::u32 PlayerActor0PlayerBaseID = 0;
  melonDS::u32 PlayerActor0VisibleFlag = 0;
  melonDS::u32 PlayerActor0PowerupPhase = 0;
  melonDS::u32 PlayerActor0PowerupTimer = 0;
  melonDS::u32 PlayerActor0PowerupGainTimer = 0;
  melonDS::u32 PlayerActor0ActionFlag = 0;
  melonDS::u32 PlayerActor0SubActionFlag = 0;
  melonDS::u32 PlayerActor0PhysicsFlag = 0;
  melonDS::u32 PlayerActor0DamageCooldown = 0;
  melonDS::u32 PlayerActor0DamageState = 0;
  melonDS::u32 PlayerActor0PowerupAuxState = 0;
  melonDS::u32 PlayerActor0PowerupState = 0;
  melonDS::u32 PlayerActor0PowerupFormState = 0;
  melonDS::u32 PlayerActor0PowerupSubState = 0;
  melonDS::u32 PlayerActor0DamageGuardFlag = 0;
  melonDS::u32 PlayerActor0PowerupApplyLock = 0;
  melonDS::u32 PlayerActor0ShellActorPtr = 0;
  melonDS::u32 PlayerActor0ShellState = 0;
  melonDS::u32 PlayerActor0TransitFunc = 0;
  melonDS::u32 PlayerActor0TransitArg = 0;
  PlayerCollisionMgrSample PlayerActor0CollisionMgr;
  PlayerHitboxSample PlayerActor0Hitbox;
  AIPlayerTileProbeSample PlayerActor0TileProbe;
  melonDS::u32 PlayerActor0TileDamageFlags = 0;
  melonDS::u32 PlayerActor0TileDamageType = 0;
  melonDS::u32 PlayerActor1Found = 0;
  melonDS::u32 PlayerActor1GUID = 0;
  melonDS::u32 PlayerActor1Base = 0;
  melonDS::u32 PlayerActor1Settings = 0;
  melonDS::u32 PlayerActor1StateType = 0;
  melonDS::u32 PlayerActor1Flags = 0;
  melonDS::u32 PlayerActor1PosX = 0;
  melonDS::u32 PlayerActor1PosY = 0;
  melonDS::u32 PlayerActor1PosZ = 0;
  melonDS::u32 PlayerActor1PrevX = 0;
  melonDS::u32 PlayerActor1PrevY = 0;
  melonDS::u32 PlayerActor1PrevZ = 0;
  melonDS::u32 PlayerActor1VelX = 0;
  melonDS::u32 PlayerActor1VelY = 0;
  melonDS::u32 PlayerActor1VelZ = 0;
  melonDS::u32 PlayerActor1PlayerID = 0;
  melonDS::u32 PlayerActor1TransitionStep = 0;
  melonDS::u32 PlayerActor1SignalLock = 0;
  melonDS::u32 PlayerActor1Flag192 = 0;
  melonDS::u32 PlayerActor1Flags728 = 0;
  melonDS::u32 PlayerActor1Flags72C = 0;
  melonDS::u32 PlayerActor1Flags730 = 0;
  melonDS::u32 PlayerActor1LinkedActor = 0;
  melonDS::u32 PlayerActor1TransitionFlag = 0;
  melonDS::u32 PlayerActor1CollisionFlag = 0;
  melonDS::u32 PlayerActor1EnvironmentFlag = 0;
  melonDS::u32 PlayerActor1UpdateLocked = 0;
  melonDS::u32 PlayerActor1ControlState = 0;
  melonDS::u32 PlayerActor1CharacterIDBase = 0;
  melonDS::u32 PlayerActor1RequestedPowerup = 0;
  melonDS::u32 PlayerActor1CurrentPowerup = 0;
  melonDS::u32 PlayerActor1PreviousPowerup = 0;
  melonDS::u32 PlayerActor1TransitioningFlag = 0;
  melonDS::u32 PlayerActor1CameraFocusMode = 0;
  melonDS::u32 PlayerActor1DefeatedFlag = 0;
  melonDS::u32 PlayerActor1PlayerBaseID = 0;
  melonDS::u32 PlayerActor1VisibleFlag = 0;
  melonDS::u32 PlayerActor1PowerupPhase = 0;
  melonDS::u32 PlayerActor1PowerupTimer = 0;
  melonDS::u32 PlayerActor1PowerupGainTimer = 0;
  melonDS::u32 PlayerActor1ActionFlag = 0;
  melonDS::u32 PlayerActor1SubActionFlag = 0;
  melonDS::u32 PlayerActor1PhysicsFlag = 0;
  melonDS::u32 PlayerActor1DamageCooldown = 0;
  melonDS::u32 PlayerActor1DamageState = 0;
  melonDS::u32 PlayerActor1PowerupAuxState = 0;
  melonDS::u32 PlayerActor1PowerupState = 0;
  melonDS::u32 PlayerActor1PowerupFormState = 0;
  melonDS::u32 PlayerActor1PowerupSubState = 0;
  melonDS::u32 PlayerActor1DamageGuardFlag = 0;
  melonDS::u32 PlayerActor1PowerupApplyLock = 0;
  melonDS::u32 PlayerActor1ShellActorPtr = 0;
  melonDS::u32 PlayerActor1ShellState = 0;
  melonDS::u32 PlayerActor1TransitFunc = 0;
  melonDS::u32 PlayerActor1TransitArg = 0;
  PlayerCollisionMgrSample PlayerActor1CollisionMgr;
  PlayerHitboxSample PlayerActor1Hitbox;
  AIPlayerTileProbeSample PlayerActor1TileProbe;
  melonDS::u32 PlayerActor1TileDamageFlags = 0;
  melonDS::u32 PlayerActor1TileDamageType = 0;
  melonDS::u32 PlayerCount = 0;
  melonDS::u32 PlayerTransitionStatus0 = 0;
  melonDS::u32 PlayerTransitionStatus1 = 0;
  melonDS::u32 EntranceSpawnID0 = 0;
  melonDS::u32 EntranceSpawnID1 = 0;
  melonDS::u32 EntranceTransitionFlags0 = 0;
  melonDS::u32 EntranceTransitionFlags1 = 0;
  melonDS::u32 EntranceSpawnPtr0 = 0;
  melonDS::u32 EntranceSpawnPtr1 = 0;
  melonDS::u32 Player0Powerup = 0;
  melonDS::u32 Player1Powerup = 0;
  melonDS::u32 Player0InventoryPowerup = 0;
  melonDS::u32 Player1InventoryPowerup = 0;
  melonDS::u32 Player0DamageGuardTimer = 0;
  melonDS::u32 Player1DamageGuardTimer = 0;
  melonDS::u32 Player0Dead = 0;
  melonDS::u32 Player1Dead = 0;
  melonDS::u32 Player0Character = 0;
  melonDS::u32 Player1Character = 0;
  melonDS::u32 Player0Lives = 0;
  melonDS::u32 Player1Lives = 0;
  melonDS::u32 Player0BattleStars = 0;
  melonDS::u32 Player1BattleStars = 0;
  melonDS::u32 Player0Coins = 0;
  melonDS::u32 Player1Coins = 0;
  melonDS::u32 Player0Score = 0;
  melonDS::u32 Player1Score = 0;
  melonDS::u32 Player0DisplayedStars = 0;
  melonDS::u32 Player1DisplayedStars = 0;
  melonDS::u32 Player0Deaths = 0;
  melonDS::u32 Player1Deaths = 0;
  melonDS::u32 Player0CollectedStars = 0;
  melonDS::u32 Player1CollectedStars = 0;
  melonDS::u32 VsCoinCount = 0;
  melonDS::u32 StageCameraFound = 0;
  melonDS::u32 StageCameraBase = 0;
  melonDS::u32 StageCameraTargetX = 0;
  melonDS::u32 StageCameraTargetY = 0;
  melonDS::u32 StageCameraTargetZ = 0;
  melonDS::u32 StageCameraPositionX = 0;
  melonDS::u32 StageCameraPositionY = 0;
  melonDS::u32 StageCameraPositionZ = 0;
  melonDS::u32 StageCameraUpX = 0;
  melonDS::u32 StageCameraUpY = 0;
  melonDS::u32 StageCameraUpZ = 0;
  melonDS::u32 StageCameraUnk114 = 0;
  melonDS::u32 StageCameraUnk118 = 0;
  melonDS::u32 StageCameraUnk11C = 0;
  melonDS::u32 StageCameraUnk128 = 0;
  melonDS::u32 StageCameraUnk12C = 0;
  melonDS::u32 StageCameraRoll130 = 0;
  melonDS::u32 StageCameraGlobalX0 = 0;
  melonDS::u32 StageCameraGlobalX1 = 0;
  melonDS::u32 StageCameraGlobalY0 = 0;
  melonDS::u32 StageCameraGlobalY1 = 0;
  melonDS::u32 StageCameraGlobalWidth0 = 0;
  melonDS::u32 StageCameraGlobalWidth1 = 0;
  melonDS::u32 StageCameraGlobalHeight0 = 0;
  melonDS::u32 StageCameraGlobalHeight1 = 0;
  melonDS::u32 PlayerCameraFocusPosX0 = 0;
  melonDS::u32 PlayerCameraFocusPosX1 = 0;
  melonDS::u32 PlayerCameraFocusPosY0 = 0;
  melonDS::u32 PlayerCameraFocusPosY1 = 0;
  melonDS::u32 PlayerCameraFocusPosZ0 = 0;
  melonDS::u32 PlayerCameraFocusPosZ1 = 0;
  melonDS::u32 PlayerCameraFocusVelX0 = 0;
  melonDS::u32 PlayerCameraFocusVelX1 = 0;
  melonDS::u32 PlayerCameraFocusVelY0 = 0;
  melonDS::u32 PlayerCameraFocusVelY1 = 0;
  melonDS::u32 PlayerCameraFocusVelZ0 = 0;
  melonDS::u32 PlayerCameraFocusVelZ1 = 0;
  melonDS::u32 StageDisplayCameraX = 0;
  melonDS::u32 CameraDbgCA880 = 0;
  melonDS::u32 CameraDbgCAE04 = 0;
  melonDS::u32 CameraDbgCAE14 = 0;
  melonDS::u32 CameraDbgCAD6C = 0;
  melonDS::u32 CameraDbgCAD8C = 0;
  melonDS::u32 CameraDbgCADB4 = 0;
  melonDS::u32 CameraDbgCAE60 = 0;
  melonDS::u32 CameraDbgCAE64 = 0;
  melonDS::u32 StageCameraWord190 = 0;
  melonDS::u32 StageCameraWord194 = 0;
  melonDS::u32 StageCameraWord19C = 0;
  melonDS::u32 StageCameraWord1A0 = 0;
  melonDS::u32 StageSceneFound = 0;
  melonDS::u32 StageSceneBase = 0;
  melonDS::u32 StageSceneSettings = 0;
  melonDS::u32 StageSceneStateType = 0;
  melonDS::u32 StageSceneFlags = 0;
  melonDS::u32 StageSceneWord154 = 0;
  melonDS::u32 StageSceneWord160 = 0;
  melonDS::u32 StageSceneWord5618 = 0;
  melonDS::u32 StageSceneWord561C = 0;
  melonDS::u32 StageSceneWord563C = 0;
  melonDS::u32 StageSceneByte5643 = 0;
  melonDS::u32 StageSceneByte5644 = 0;
  melonDS::u32 StageSceneByte5645 = 0;
  melonDS::u32 StageSceneByte5646 = 0;
  melonDS::u32 StageSceneByte5648 = 0;
  melonDS::u32 StageSceneByte5649 = 0;
  melonDS::u32 StageSceneUpdateDispatchFunc = 0;
  melonDS::u32 StageSceneUpdateDispatchArg = 0;
  melonDS::u32 StageSceneRenderDispatchFunc = 0;
  melonDS::u32 StageSceneRenderDispatchArg = 0;
  melonDS::u32 StageSceneGlobal9280 = 0;
  melonDS::u32 StageSceneGlobal9284 = 0;
  melonDS::u32 StageSceneGlobal928C = 0;
  melonDS::u32 StageSceneGlobal92B4 = 0;
  melonDS::u32 StageSceneGlobal92C0 = 0;
  melonDS::u32 StageSceneGlobal92C8 = 0;
  melonDS::u32 StageSceneGlobal92CC = 0;
  melonDS::u32 StageSceneGlobal92D0 = 0;
  melonDS::u32 StageLiquidPlayerSlot = 0;
  melonDS::u32 StageLiquidHeight0 = 0;
  melonDS::u32 StageLiquidHeight1 = 0;
  melonDS::u32 VsConnectFound = 0;
  melonDS::u32 VsConnectBase = 0;
  melonDS::u32 VsConnectWord078 = 0;
  melonDS::u32 VsConnectWord07C = 0;
  melonDS::u32 VsConnectByte0E2 = 0;
  melonDS::u32 VsConnectByte106 = 0;
  melonDS::u32 VsConnectWord114 = 0;
  melonDS::u32 VsConnectWord118 = 0;
  melonDS::u32 VsConnectWord120 = 0;
  melonDS::u32 VsConnectWord128 = 0;
  melonDS::u32 VsConnectWord138 = 0;
  melonDS::u32 VsConnectWord13C = 0;
  melonDS::u32 VsConnectWord140 = 0;
  melonDS::u32 VsConnectWord144 = 0;
  melonDS::u32 VsConnectWord148 = 0;
  melonDS::u32 VsConnectByte153 = 0;
  melonDS::u32 VsConnectByte154 = 0;
  melonDS::u32 VsConnectByte155 = 0;
  melonDS::u32 VsConnectByte156 = 0;
  melonDS::u32 VsConnectByte157 = 0;
  melonDS::u32 VsConnectByte158 = 0;
  melonDS::u32 VsConnectWord154 = 0;
  melonDS::u32 CourseSelectFound = 0;
  melonDS::u32 CourseSelectBase = 0;
  melonDS::u32 CourseSelectSettings = 0;
  melonDS::u32 CourseSelectWord060 = 0;
  melonDS::u32 CourseSelectWord064 = 0;
  melonDS::u32 CourseSelectWord068 = 0;
  melonDS::u32 CourseSelectWord06C = 0;
  melonDS::u32 CourseSelectWord070 = 0;
  melonDS::u32 CourseSelectWord074 = 0;
  melonDS::u32 CourseSelectWord078 = 0;
  melonDS::u32 CourseSelectWord07C = 0;
  melonDS::u32 CourseSelectWord080 = 0;
  melonDS::u32 CourseSelectWord084 = 0;
  melonDS::u32 CourseSelectWord088 = 0;
  melonDS::u32 CourseSelectWord08C = 0;
  melonDS::u32 CourseSelectWord090 = 0;
  melonDS::u32 StageActorManagerFound = 0;
  melonDS::u32 StageActorManagerBase = 0;
  melonDS::u32 StageActorManagerStateType = 0;
  melonDS::u32 StageControllerFound = 0;
  melonDS::u32 StageControllerBase = 0;
  melonDS::u32 StageControllerStateType = 0;
  melonDS::u32 MvlObject267Found = 0;
  melonDS::u32 MvlObject267Base = 0;
  melonDS::u32 MvlObject267StateType = 0;
  melonDS::u32 MvlObject267LeftFound = 0;
  melonDS::u32 MvlObject267LeftBase = 0;
  melonDS::u32 MvlObject267LeftStateType = 0;
  melonDS::u32 MvlObject267LeftPosX = 0;
  melonDS::u32 MvlObject267LeftPosY = 0;
  melonDS::u32 MvlObject267LeftPosZ = 0;
  melonDS::u32 MvlObject267RightFound = 0;
  melonDS::u32 MvlObject267RightBase = 0;
  melonDS::u32 MvlObject267RightStateType = 0;
  melonDS::u32 MvlObject267RightPosX = 0;
  melonDS::u32 MvlObject267RightPosY = 0;
  melonDS::u32 MvlObject267RightPosZ = 0;
  melonDS::u32 MvlGlobal965C = 0;
  melonDS::u32 MvlGlobal9670 = 0;
  melonDS::u32 MvlGlobal9674 = 0;
  melonDS::u32 MvlGlobal9694_0 = 0;
  melonDS::u32 MvlGlobal9694_1 = 0;
  melonDS::u32 MvlStageLayoutGateCAC6C = 0;
  melonDS::u32 MvlStageLayoutGateCAC74 = 0;
  melonDS::u32 MvlStageLayoutGateCAC7C = 0;
  melonDS::u32 MvlStageLayoutGateCACDC = 0;
  melonDS::u32 MvlStageLayoutGateCAE80 = 0;
  melonDS::u32 MvlStageLayoutGateCAE74 = 0;
  melonDS::u32 MvlStageLayoutGateCAEB8 = 0;
  melonDS::u32 MvlStageLayoutGateCAF20 = 0;
  melonDS::u32 MvlStageLayoutGateCAF40 = 0;
  melonDS::u32 MvlStageLayoutGateCA8C0 = 0;
  melonDS::u32 MvlStageLayoutGateCA8D0 = 0;
  melonDS::u32 MvlStageLayoutGateCAD30 = 0;
  melonDS::u32 MvlManagerBase = 0;
  melonDS::u32 MvlManagerVTable = 0;
  melonDS::u32 MvlManagerGUID = 0;
  melonDS::u32 MvlManagerSettings = 0;
  melonDS::u32 MvlManagerObjectID = 0;
  melonDS::u32 MvlManagerStateType = 0;
  melonDS::u32 MvlManagerFlags = 0;
  melonDS::u32 MvlManagerUnk54 = 0;
  melonDS::u32 MvlManagerResourcesHeap = 0;
  melonDS::u32 MvlManagerWordA8CC = 0;
  melonDS::u32 MvlManagerWordA8D0 = 0;
  melonDS::u32 MvlManagerWordA8D4 = 0;
  melonDS::u32 MvlManagerWordA8D8 = 0;
  melonDS::u32 MvlManagerWordA8DC = 0;
  melonDS::u32 MvlManagerWordA8E0 = 0;
  melonDS::u32 MvlManagerWordA8E4 = 0;
  melonDS::u32 MvlManagerHalfA8E8 = 0;
  melonDS::u32 MvlManagerHalfA8EA = 0;
  melonDS::u32 MvlManagerByteA8EC = 0;
  melonDS::u32 MvlManagerHalf494 = 0;
  melonDS::u32 MvlManagerHalf4A0 = 0;
  melonDS::u32 MovingHazardFound = 0;
  melonDS::u32 MovingHazardGUID = 0;
  melonDS::u32 MovingHazardSettings = 0;
  melonDS::u32 MovingHazardStateType = 0;
  melonDS::u32 MovingHazardFlags = 0;
  melonDS::u32 MovingHazardPosX = 0;
  melonDS::u32 MovingHazardPosY = 0;
  melonDS::u32 MovingHazardPosZ = 0;
  melonDS::u32 MovingHazardVelX = 0;
  melonDS::u32 MovingHazardVelY = 0;
  melonDS::u32 MovingHazardLastStepX = 0;
  melonDS::u32 MovingHazardLastStepY = 0;
  melonDS::u32 MovingHazardLastStepZ = 0;
  melonDS::u32 MovingHazardVelH = 0;
  melonDS::u32 MovingHazardTargetVelH = 0;
  melonDS::u32 MovingHazardAccelV = 0;
  melonDS::u32 MovingHazardTargetVelV = 0;
  melonDS::u32 MovingHazardAccelH = 0;
  melonDS::u32 MovingHazardTargetVelX = 0;
  melonDS::u32 MovingHazardTargetVelY = 0;
  melonDS::u32 MovingHazardTargetVelZ = 0;
  melonDS::u32 FireballsActiveCount = 0;
  melonDS::u32 FireballsHandlerPtr = 0;
  melonDS::u32 FireballsHandlerWords[kAISpecialHandlerWordCount]{};
  melonDS::u32 FireballSlotActive[kAIFireballSlotCount]{};
  melonDS::u32 FireballSlotKind[kAIFireballSlotCount]{};
  melonDS::u32 FireballSlotState[kAIFireballSlotCount]{};
  melonDS::u32 FireballSlotFacing[kAIFireballSlotCount]{};
  melonDS::u32 FireballSlotPosX[kAIFireballSlotCount]{};
  melonDS::u32 FireballSlotPosY[kAIFireballSlotCount]{};
  melonDS::u32 FireballSlotPosZ[kAIFireballSlotCount]{};
  melonDS::u32 FireballSlotPrevX[kAIFireballSlotCount]{};
  melonDS::u32 FireballSlotPrevY[kAIFireballSlotCount]{};
  melonDS::u32 FireballSlotPrevZ[kAIFireballSlotCount]{};
  melonDS::u32 FireballSlotVelX[kAIFireballSlotCount]{};
  melonDS::u32 FireballSlotVelY[kAIFireballSlotCount]{};
  melonDS::u32 FireballSlotVelZ[kAIFireballSlotCount]{};
  melonDS::u32 FireballSlotStateBytes[kAIFireballSlotCount]
                                     [kAIFireballSlotStateByteCount]{};
  melonDS::u32 FireballSlotDebugWords[kAIFireballSlotCount]
                                     [kAIFireballSlotDebugWordCount]{};
  melonDS::u32 ProjectilesHandlerWords[kAISpecialHandlerWordCount]{};
  melonDS::u32 ObjectScanTotal = 0;
  melonDS::u32 ObjectNotCreatedCount = 0;
  melonDS::u32 ObjectActiveCount = 0;
  melonDS::u32 ObjectDeadCount = 0;
  melonDS::u32 ObjectSkipUpdateCount = 0;
  melonDS::u32 ObjectSkipRenderCount = 0;
  melonDS::u32 ObjectFirstNotCreatedID = 0;
  melonDS::u32 ObjectFirstNotCreatedBase = 0;
  melonDS::u32 ObjectFirstNotCreatedFlags = 0;
  melonDS::u32 ObjectSecondNotCreatedID = 0;
  melonDS::u32 ObjectSecondNotCreatedBase = 0;
  melonDS::u32 ObjectSecondNotCreatedFlags = 0;
  melonDS::u32 ObjectActiveID[kObjectTraceSlots]{};
  melonDS::u32 ObjectActiveSettings[kObjectTraceSlots]{};
  melonDS::u32 ObjectActiveBase[kObjectTraceSlots]{};
  melonDS::u64 Hash = 0;
};

struct GameStateSyncHashes {
  melonDS::u64 Basic = 0;
  melonDS::u64 PlayerGlobal = 0;
  melonDS::u64 WifiCandidate = 0;
  melonDS::u64 RenderCandidate = 0;
};

struct GameStateHashMismatch {
  int InstanceID = 0;
  melonDS::u32 Frame = 0;
  GameStateSyncHashes Local;
  GameStateSyncHashes Remote;
};

struct GameStateTraceHashes {
  melonDS::u64 PlayerGlobal = 0;
  melonDS::u64 WifiCandidate = 0;
  melonDS::u64 RenderCandidate = 0;
  melonDS::u64 NetState = 0;
};

class GameStateTraceWriter {
public:
  bool Open(const std::string &path, bool extended);
  bool IsOpen() const;
  void ResetForRestart(int instanceID);
  bool Write(int instanceID, melonDS::u32 frame,
             const GameStateSample &sample,
             const GameStateTraceHashes *extendedHashes);
  void Close();

private:
  std::ofstream Output_;
  melonDS::u64 LastWrittenFrame_[16]{};
};

struct DecodedGameState {
  melonDS::u32 Frame = 0;
  melonDS::u32 Instance = 0;
  GameStateSample Sample;
  GameStateSyncHashes Hashes;
};

melonDS::u64 GameStateKey(int instanceID, melonDS::u32 frame);
melonDS::u64 PlayerStateKey(melonDS::u32 player, melonDS::u32 frame);

class RemoteStateStore {
public:
  static constexpr std::size_t PlayerHistoryLimit = 240;

  void ResetForRestart();
  void StoreGameState(const DecodedGameState &state);
  std::size_t StorePlayerState(
      const WireProtocol::WirePlayerState &state);
  bool StoreWorldState(const WireProtocol::WireWorldState &state);
  bool StoreMovingHazardState(
      const WireProtocol::WireMovingHazardState &state);
  bool StoreWorldEffectState(
      const WireProtocol::WireWorldEffectState &state);

  const GameStateSyncHashes *FindGameStateHashes(int instanceID,
                                                 melonDS::u32 frame) const;
  const GameStateSample *FindGameState(int instanceID,
                                      melonDS::u32 frame) const;
  bool FindLatestGameState(int instanceID, melonDS::u32 frame,
                           GameStateSample &state,
                           melonDS::u32 &stateFrame) const;
  bool FindLatestPlayerState(melonDS::u32 player, melonDS::u32 frame,
                             WireProtocol::WirePlayerState &state,
                             melonDS::u32 &stateFrame) const;

  const WireProtocol::WireWorldState *WorldState() const;
  const WireProtocol::WireMovingHazardState *MovingHazardState() const;
  const WireProtocol::WireWorldEffectState *WorldEffectState() const;
  std::size_t PlayerStateCount() const;

private:
  std::map<melonDS::u64, GameStateSyncHashes> GameStateHashes_;
  std::map<melonDS::u64, GameStateSample> GameStates_;
  std::map<melonDS::u64, WireProtocol::WirePlayerState> PlayerStates_;
  std::optional<WireProtocol::WireWorldState> WorldState_;
  std::optional<WireProtocol::WireMovingHazardState> MovingHazardState_;
  std::optional<WireProtocol::WireWorldEffectState> WorldEffectState_;
};

class StateSyncRuntime {
public:
  void ResetForRestart(int instanceID);
  bool BeginGameStateSync(int instanceID, melonDS::u32 frame);
  std::optional<GameStateHashMismatch>
  RecordLocalGameStateHashes(int instanceID, melonDS::u32 frame,
                             const GameStateSyncHashes &hashes);
  std::optional<GameStateHashMismatch>
  RecordRemoteGameState(const DecodedGameState &state);

  RemoteStateStore RemoteState;
  melonDS::u64 LastSentPlayerStateFrame[16]{};
  melonDS::u64 LastSentWorldStateFrame[16]{};
  melonDS::u32 LastAppliedPlayerGlobalsFrame[16][2]{};
  melonDS::u32 PlayerActorBaseCache[16][2]{};
  melonDS::u32 PlayerActorGUIDCache[16][2]{};
  melonDS::u32 WorldStarActorBaseCache[16]{};
  melonDS::u32 WorldStarActorGUIDCache[16]{};
  melonDS::u32 WorldMovingHazardBaseCache[16]{};
  melonDS::u32 WorldMovingHazardGUIDCache[16]{};
  melonDS::u32 WorldMovingHazardBaseCaches
      [16][WireProtocol::kMaxWorldMovingHazards]{};
  melonDS::u32 WorldMovingHazardGUIDCaches
      [16][WireProtocol::kMaxWorldMovingHazards]{};
  melonDS::u32 WorldMovingHazardRemoteGUIDMaps
      [16][WireProtocol::kMaxWorldMovingHazards]{};
  melonDS::u32 WorldMovingHazardLocalGUIDMaps
      [16][WireProtocol::kMaxWorldMovingHazards]{};
  melonDS::u32 WorldMovingHazardCacheCounts[16]{};
  melonDS::u32 LastTracedWorldMovingHazardsFrame[16]{};
  melonDS::u32 LastTracedWorldEffectsFrame[16]{};
  melonDS::u32 LastTracedWorldObjectLifecyclesFrame[16]{};

private:
  std::optional<GameStateHashMismatch>
  CompareGameStateHashes(int instanceID, melonDS::u32 frame) const;

  std::map<melonDS::u64, GameStateSyncHashes> LocalGameStateHashes_;
  melonDS::u64 LastSentGameStateFrame_[16]{};
};

melonDS::u64 ComputeBasicGameStateHash(const GameStateSample &sample);
melonDS::u64 CombinedGameStateHash(const GameStateSyncHashes &hashes);

WireProtocol::WireGameState
EncodeWireGameState(melonDS::u32 frame, melonDS::u32 instance,
                    const GameStateSample &sample,
                    const GameStateSyncHashes &hashes);
bool DecodeWireGameState(const WireProtocol::WireGameState &packet,
                         DecodedGameState &decoded);
void WriteGameStateTraceRow(std::ostream &out, int instanceID,
                            melonDS::u32 frame, const GameStateSample &sample,
                            const GameStateTraceHashes *extendedHashes);
void WriteGameStateTraceHeader(std::ostream &out, bool extended);

} // namespace NsmbNetplayPoC::GameStateModel

#endif
