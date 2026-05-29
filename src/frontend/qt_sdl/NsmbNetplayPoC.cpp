/*
    Experimental NSMB Mario vs Luigi input-lockstep PoC.

    Usage example:
      Host:
        MELONDS_NSML_POC=1 MELONDS_NSML_ROLE=host MELONDS_NSML_PORT=8065 MELONDS_NSML_LOCAL_INSTANCE=0 melonDS.exe
      Client:
        MELONDS_NSML_POC=1 MELONDS_NSML_ROLE=client MELONDS_NSML_PEER=HOST_IP MELONDS_NSML_PORT=8065 MELONDS_NSML_LOCAL_INSTANCE=1 melonDS.exe

    Both sides should run two melonDS instances with Local MP enabled and the
    same ROM/BIOS/firmware/savestate setup. This module exchanges only input.
*/

#include "NsmbNetplayPoC.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <filesystem>
#include <iterator>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <QImage>
#include <QString>

#include <enet/enet.h>

#include "NDS.h"
#include "ARM.h"
#include "Savestate.h"
#include "LocalMP.h"
#include "MPInterface.h"
#include "Platform.h"

namespace NsmbNetplayPoC
{

namespace
{

constexpr melonDS::u32 kMagic = 0x4C4D534E; // "NSML", little endian
constexpr melonDS::u32 kVersion = 1;
constexpr int kDefaultDelay = 6;
constexpr int kMaxPumpEvents = 64;
constexpr melonDS::u32 kNoFrameLimit = 0;
constexpr melonDS::u32 kMainRAMBase = 0x02000000;
constexpr melonDS::u32 kGameStageIDAddr = 0x02085A14;
constexpr melonDS::u32 kGameStageGroupAddr = 0x02085A18;
constexpr melonDS::u32 kGameLocalPlayerIDAddr = 0x02085A7C;
constexpr melonDS::u32 kGameVsModeAddr = 0x02085A84;
constexpr melonDS::u32 kNetStateBaseAddr = 0x020887E8;
constexpr melonDS::u32 kNetCurrentLanguageAddr = 0x020887E8;
constexpr melonDS::u32 kNetLocalAidAddr = 0x020887F0;
constexpr melonDS::u32 kWifiCommunicatingConsoleCountAddr = 0x02085200;
constexpr melonDS::u32 kWifiCommunicatingConsolesAddr = 0x0208B848;
constexpr melonDS::u32 kNetState14Addr = 0x020887FC; // Net::connectionState
constexpr melonDS::u32 kNetState1CAddr = 0x02088804; // Net::connectedConsoleCount
constexpr melonDS::u32 kNetState20Addr = 0x02088808;
constexpr melonDS::u32 kNetState24Addr = 0x0208880C; // Net::expectedConsoleCount
constexpr melonDS::u32 kNetExpectedConsoleCountAddr = 0x0208880C;
constexpr melonDS::u32 kNetMultiBootSessionAddr = 0x02088810;
constexpr melonDS::u32 kNetSessionStateAddr = 0x02088814;
constexpr melonDS::u32 kNetModuleStateAddr = 0x02088818;
constexpr melonDS::u32 kNetMaxSessionChildrenAddr = 0x0208881C;
constexpr melonDS::u32 kNetMaxConsoleCountAddr = 0x0208882C;
constexpr melonDS::u32 kNetState5CAddr = 0x0208883C; // Net::errorState
constexpr melonDS::u32 kNetGGIDAddr = 0x02088858;
constexpr melonDS::u32 kNetRandomBranchAddressAddr = 0x0208885C;
constexpr melonDS::u32 kNetPacketTickAddr = 0x020888E0;
constexpr melonDS::u32 kNetPacketKeysAddr = 0x020888E2;
constexpr melonDS::u32 kNetPacketActionAddr = 0x020888E4;
constexpr melonDS::u32 kNetPacketByte5Addr = 0x020888E5;
constexpr melonDS::u32 kNetPacketByte6Addr = 0x020888E6;
constexpr melonDS::u32 kNetPacketByte7Addr = 0x020888E7;
constexpr melonDS::u32 kPacketBridgeJitScratchBaseAddr = 0x023C1200;
constexpr melonDS::u32 kPacketBridgeJitScratchTickAddr = kPacketBridgeJitScratchBaseAddr + 0x00;
constexpr melonDS::u32 kPacketBridgeJitScratchActionAddr = kPacketBridgeJitScratchBaseAddr + 0x04;
constexpr melonDS::u32 kPacketBridgeJitScratchKeysAddr = kPacketBridgeJitScratchBaseAddr + 0x08;
constexpr melonDS::u32 kPacketBridgeJitScratchPacketsAddr = kPacketBridgeJitScratchBaseAddr + 0x40;
constexpr melonDS::u32 kNetRandomCallCountAddr = 0x02088A48;
constexpr melonDS::u32 kNetRandomValueAddr = 0x02088A68;
constexpr melonDS::u32 kInputConsoleKeysAddr = 0x02087650;
constexpr melonDS::u32 kInputPlayerKeysHeldAddr = 0x02087660;
constexpr melonDS::u32 kInputPlayerKeysPressedAddr = 0x02087664;
constexpr melonDS::u32 kStageActorFreezeFlagAddr = 0x020CA28C;
constexpr melonDS::u32 kActorCategoryMaskAddr = 0x020CA850;
constexpr melonDS::u32 kGamePlayerGlobalBlockAddr = 0x0208B324;
constexpr melonDS::u32 kGamePlayerPowerupAddr = 0x0208B324;
constexpr melonDS::u32 kGamePlayerDeadAddr = 0x0208B328;
constexpr melonDS::u32 kGamePlayerInventoryPowerupAddr = 0x0208B32C;
constexpr melonDS::u32 kGamePlayerCharacterAddr = 0x0208B330;
constexpr melonDS::u32 kGamePlayerTransitionStatusAddr = 0x0208B354; // Game::playerVSPipeState
constexpr melonDS::u32 kGamePlayerCountAddr = 0x0208B348;
constexpr melonDS::u32 kGamePlayerLivesAddr = 0x0208B364;
constexpr melonDS::u32 kGamePlayerBattleStarsAddr = 0x0208B36C;
constexpr melonDS::u32 kGamePlayerCoinsAddr = 0x0208B37C;
constexpr melonDS::u32 kGamePlayerScoreAddr = 0x0208B384;
constexpr melonDS::u32 kGamePlayerDisplayedStarsAddr = 0x0208B38C;
constexpr melonDS::u32 kGamePlayerDeathsAddr = 0x0208B394;
constexpr melonDS::u32 kGamePlayerCollectedStarsAddr = 0x0208B39C;
constexpr melonDS::u32 kGameVsCoinCountAddr = 0x0208B37C;
constexpr melonDS::u32 kGameCandidateWifiBlockAddr = 0x0208B7A0;
constexpr melonDS::u32 kGameCandidateRenderBlockAddr = 0x023F8300;
constexpr melonDS::u32 kAppFrameLengthAddr = 0x02039810;
constexpr melonDS::u32 kAppUpdateTaskAddr = 0x02039824;
constexpr melonDS::u32 kAppSleepPhaseAddr = 0x0208596C;
constexpr melonDS::u32 kAppSleepControlAddr = 0x02085974;
constexpr melonDS::u32 kAppSleepingAddr = 0x02085978;
constexpr melonDS::u32 kAppSleepPhaseTimerAddr = 0x0208597C;
constexpr melonDS::u32 kAppSleepWakeUpTimerAddr = 0x02085980;
constexpr melonDS::u32 kAppBootParamAddr = 0x0208598C;
constexpr melonDS::u32 kAppBootTargetAddr = 0x02085990;
constexpr melonDS::u32 kAppBootSceneAddr = 0x02085994;
constexpr melonDS::u16 kPlayerObjectID = 0x0015;
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
constexpr melonDS::u32 kPlayerBaseDefeatedFlagOffset = 0x7B3;
constexpr melonDS::u32 kPlayerBasePlayerIDOffset = 0x7B4;
constexpr melonDS::u32 kPlayerBaseVisibleFlagOffset = 0x7B5;
constexpr melonDS::u32 kPlayerBaseLinkedActorOffset = 0x688;
constexpr melonDS::u16 kVsBattleStarActorObjectID = 0x0022;
constexpr melonDS::u32 kVsBattleStarActorSettings = 0x00000001;
constexpr melonDS::u16 kVsBattleStarCandidateObjectID = 0x010C;
constexpr melonDS::u16 kVsMovingHazardObjectID = 0x0053;
constexpr melonDS::u32 kVsMovingHazardSettings = 0x00000000;
constexpr int kObjectTraceSlots = 16;
constexpr melonDS::u16 kStageSceneObjectID = 0x0003;
constexpr melonDS::u32 kMvlStageSceneSettings = 0x00B4FF00;
constexpr melonDS::u32 kStageSceneUpdateDispatchTableAddr = 0x020CA378;
constexpr melonDS::u32 kStageSceneRenderDispatchTableAddr = 0x020CA398;
constexpr melonDS::u16 kStageFXObjectID = 0x0012;
constexpr melonDS::u16 kStageActorManagerObjectID = 0x012F;
constexpr melonDS::u16 kStageControllerObjectID = 0x0130;
constexpr melonDS::u16 kMvlObject267ID = 0x010B;
constexpr melonDS::u16 kVsConnectObjectID = 0x0006;
constexpr melonDS::u16 kCourseSelectObjectID = 0x0005;
constexpr melonDS::u16 kStageCameraObjectID = 0x013C;
constexpr melonDS::u32 kStageCameraXAddr = 0x020CAE1C;
constexpr melonDS::u32 kStageCameraYAddr = 0x020CAD94;
constexpr melonDS::u32 kStageCameraWidthAddr = 0x020CADA4;
constexpr melonDS::u32 kStageCameraHeightAddr = 0x020CAD8C;
constexpr melonDS::u32 kStageDisplayCameraXAddr = 0x02085AB4;
constexpr melonDS::u32 kA2DJGameLoadLevelAddr = 0x020068A8;
constexpr melonDS::u32 kA2DJVSConnectCreateLoadGameSMAddr = 0x021520A0;
constexpr melonDS::u32 kA2DJVSConnectUpdateLoadGameSMAddr = 0x02151E94;
constexpr melonDS::u32 kA2DJVSConnectRenderLoadGameSMAddr = 0x02151E54;
constexpr melonDS::u32 kA2DJVSConnectUpdateStageStartSMAddr = 0x021512B8;
constexpr melonDS::u32 kA2DJVSConnectStartLoadLevelAddr = 0x0214E0C0;
constexpr melonDS::u32 kA2DJLoadMvsLFilesThreadAddr = 0x02152E04;
constexpr melonDS::u32 kA2DJCreateThreadAddr = 0x02004BFC;
constexpr melonDS::u32 kA2DJVSConnectScheduleSubMenuChangeAddr = 0x021528A0;
constexpr melonDS::u32 kA2DJVSConnectLoadGameSMSubMenuAddr = 0x02156624;
constexpr melonDS::u32 kA2DJVSConnectPostLoadGameSMSubMenuAddr = 0x02156640;
constexpr melonDS::u32 kA2DJVSConnectStageStartSMSubMenuAddr = 0x02156678;
constexpr melonDS::u32 kA2DJVSConnectClientConfirmSMSubMenuAddr = 0x02156694;
constexpr melonDS::u32 kA2DJVSConnectOnUpdateAddr = 0x021529FC;
constexpr melonDS::u32 kA2DJFSCacheLoadFileAddr = 0x02009C64;
constexpr melonDS::u32 kA2DJFSCacheLoadDataAddr = 0x02009BC8;
constexpr melonDS::u32 kA2DJVSCreateCourseSelectAddr = 0x0214F858;
constexpr melonDS::u32 kA2DJCourseSelectFactoryAddr = 0x020130A8;
constexpr melonDS::u32 kA2DJApplySceneRequestAddr = 0x02007ACC;
constexpr melonDS::u32 kA2DJStartSceneTransitionAddr = 0x02011CE8;
constexpr melonDS::u32 kA2DJCreateObjectAddr = 0x0204BF8C;
constexpr melonDS::u32 kA2DEStageLayoutMvlInitAddr = 0x020B0714;
constexpr melonDS::u32 kDirectBootTrampolineAddr = 0x023C0000;
constexpr melonDS::u32 kSceneIsSceneActiveAddr = 0x0203BD28;
constexpr melonDS::u32 kScenePreviousSceneIDAddr = 0x0203BD2C;
constexpr melonDS::u32 kSceneNextSceneIDAddr = 0x0203BD30;
constexpr melonDS::u32 kSceneCurrentSceneIDAddr = 0x0203BD34;
constexpr melonDS::u32 kSceneNextSceneSettingsAddr = 0x02088F38;
constexpr melonDS::u32 kEntranceSpawnEntranceIDAddr = 0x0208B094;
constexpr melonDS::u32 kEntranceTransitionFlagsAddr = 0x0208B098;
constexpr melonDS::u32 kEntranceSpawnEntranceAddr = 0x0208B0A0;

bool IsMarioVsLuigiGGID(melonDS::u32 value)
{
    // A2DJ diagnostics used 0x42. US A2DE keeps the MvL GGID as 0x00400150.
    return value == 0x42 || value == 0x00400150;
}

bool IsMarioVsLuigiGameplay(melonDS::NDS* nds)
{
    if (!nds) return false;
    return nds->ARM9Read32(kGameStageGroupAddr) == 9
        && nds->ARM9Read32(kGameVsModeAddr) == 1;
}

enum class Role
{
    Host,
    Client,
};

enum class RollbackBackend
{
    Savestate,
    ARM9RAM,
};

constexpr melonDS::u32 kRollbackARM9RAMMagic = 0x524D4139; // RMA9
constexpr melonDS::u32 kRollbackARM9RAMVersion = 1;

struct RollbackARM9RAMHeader
{
    melonDS::u32 Magic;
    melonDS::u32 Version;
    melonDS::u32 RamSize;
    melonDS::u32 NumFrames;
    melonDS::u32 NumLagFrames;
    melonDS::u32 LagFrameFlag;
    melonDS::u32 Reserved;
    melonDS::u64 Reserved64;
};

struct WireInput
{
    melonDS::u32 Magic;
    melonDS::u32 Version;
    melonDS::u32 Frame;
    melonDS::u32 KeyMask;
    melonDS::u16 TouchX;
    melonDS::u16 TouchY;
    melonDS::u8 Touching;
    melonDS::u8 Reserved[3];
};

static_assert(sizeof(WireInput) == 24);

struct WireInputBundleHeader
{
    melonDS::u32 Magic;
    melonDS::u32 Version;
    melonDS::u32 Kind;
    melonDS::u32 Count;
};

struct WireInputBundleEntry
{
    melonDS::u32 Frame;
    melonDS::u32 KeyMask;
    melonDS::u16 TouchX;
    melonDS::u16 TouchY;
    melonDS::u8 Touching;
    melonDS::u8 Reserved[3];
};

static_assert(sizeof(WireInputBundleHeader) == 16);
static_assert(sizeof(WireInputBundleEntry) == 16);

struct WireSeed
{
    melonDS::u32 Magic;
    melonDS::u32 Version;
    melonDS::u32 Kind;
    melonDS::u32 Seed;
};

constexpr melonDS::u32 kWireKindSeed = 0x44454553; // "SEED", little endian
constexpr melonDS::u32 kWireKindState = 0x54415453; // "STAT", little endian
constexpr melonDS::u32 kWireKindPacket = 0x4B434150; // "PACK", little endian
constexpr melonDS::u32 kWireKindInputBundle = 0x42504E49; // "INPB", little endian

static_assert(sizeof(WireSeed) == 16);

struct WireNSMLPacket
{
    melonDS::u32 Magic;
    melonDS::u32 Version;
    melonDS::u32 Kind;
    melonDS::u32 Frame;
    melonDS::u32 Player;
    melonDS::u32 Tick;
    melonDS::u8 Packet[52];
};

static_assert(sizeof(WireNSMLPacket) == 76);

struct DelayedWireNSMLPacket
{
    melonDS::u32 ReleaseFrame = 0;
    std::chrono::steady_clock::time_point ReleaseTime {};
    WireNSMLPacket Packet {};
};

struct DelayedWireInput
{
    melonDS::u32 ReleaseFrame = 0;
    std::chrono::steady_clock::time_point ReleaseTime {};
    std::vector<char> Payload {};
    melonDS::u32 Flags = ENET_PACKET_FLAG_RELIABLE;
};

struct WireGameState
{
    melonDS::u32 Magic;
    melonDS::u32 Version;
    melonDS::u32 Kind;
    melonDS::u32 Frame;
    melonDS::u32 Instance;
    melonDS::u32 StageID;
    melonDS::u32 StageGroup;
    melonDS::u32 VsMode;
    melonDS::u32 LocalPlayerID;
    melonDS::u32 GGID;
    melonDS::u32 NetRandomValue;
    melonDS::u32 NetRandomCallCount;
    melonDS::u32 NetRandomBranchAddress;
    melonDS::u32 VsStarFound;
    melonDS::u32 VsStarGUID;
    melonDS::u32 VsStarBase;
    melonDS::u32 VsStarSettings;
    melonDS::u32 VsStarStateType;
    melonDS::u32 VsStarFlags;
    melonDS::u32 VsStarPosX;
    melonDS::u32 VsStarPosY;
    melonDS::u32 VsStarPosZ;
    melonDS::u32 VsStarActorFound;
    melonDS::u32 VsStarActorGUID;
    melonDS::u32 VsStarActorBase;
    melonDS::u32 VsStarActorSettings;
    melonDS::u32 VsStarActorStateType;
    melonDS::u32 VsStarActorFlags;
    melonDS::u32 VsStarActorPosX;
    melonDS::u32 VsStarActorPosY;
    melonDS::u32 VsStarActorPosZ;
    melonDS::u32 PlayerActor0Found;
    melonDS::u32 PlayerActor0GUID;
    melonDS::u32 PlayerActor0Settings;
    melonDS::u32 PlayerActor0PosX;
    melonDS::u32 PlayerActor0PosY;
    melonDS::u32 PlayerActor0PosZ;
    melonDS::u32 PlayerActor0PrevX;
    melonDS::u32 PlayerActor0PrevY;
    melonDS::u32 PlayerActor0PrevZ;
    melonDS::u32 PlayerActor0VelX;
    melonDS::u32 PlayerActor0VelY;
    melonDS::u32 PlayerActor0VelZ;
    melonDS::u32 PlayerActor1Found;
    melonDS::u32 PlayerActor1GUID;
    melonDS::u32 PlayerActor1Settings;
    melonDS::u32 PlayerActor1PosX;
    melonDS::u32 PlayerActor1PosY;
    melonDS::u32 PlayerActor1PosZ;
    melonDS::u32 PlayerActor1PrevX;
    melonDS::u32 PlayerActor1PrevY;
    melonDS::u32 PlayerActor1PrevZ;
    melonDS::u32 PlayerActor1VelX;
    melonDS::u32 PlayerActor1VelY;
    melonDS::u32 PlayerActor1VelZ;
    melonDS::u32 PlayerCount;
    melonDS::u32 Player0BattleStars;
    melonDS::u32 Player1BattleStars;
    melonDS::u32 Player0Coins;
    melonDS::u32 Player1Coins;
    melonDS::u32 Player0Score;
    melonDS::u32 Player1Score;
    melonDS::u32 Player0DisplayedStars;
    melonDS::u32 Player1DisplayedStars;
    melonDS::u32 Player0Deaths;
    melonDS::u32 Player1Deaths;
    melonDS::u32 Player0CollectedStars;
    melonDS::u32 Player1CollectedStars;
    melonDS::u32 VsCoinCount;
    melonDS::u32 StageCameraFound;
    melonDS::u32 StageCameraWord190;
    melonDS::u32 StageCameraWord194;
    melonDS::u32 StageCameraWord19C;
    melonDS::u32 StageCameraWord1A0;
    melonDS::u32 StageSceneFound;
    melonDS::u32 StageSceneWord154;
    melonDS::u32 StageSceneWord160;
    melonDS::u32 MovingHazardFound;
    melonDS::u32 MovingHazardGUID;
    melonDS::u32 MovingHazardSettings;
    melonDS::u32 MovingHazardStateType;
    melonDS::u32 MovingHazardFlags;
    melonDS::u32 MovingHazardPosX;
    melonDS::u32 MovingHazardPosY;
    melonDS::u32 MovingHazardPosZ;
    melonDS::u32 MovingHazardVelX;
    melonDS::u32 MovingHazardVelY;
    melonDS::u32 BasicHashLo;
    melonDS::u32 BasicHashHi;
    melonDS::u32 PlayerGlobalHashLo;
    melonDS::u32 PlayerGlobalHashHi;
    melonDS::u32 WifiCandidateHashLo;
    melonDS::u32 WifiCandidateHashHi;
    melonDS::u32 RenderCandidateHashLo;
    melonDS::u32 RenderCandidateHashHi;
};

static_assert(sizeof(WireGameState) == 380);

struct GameStateSample
{
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
    melonDS::u32 PlayerActor0CharacterIDBase = 0;
    melonDS::u32 PlayerActor0TransitioningFlag = 0;
    melonDS::u32 PlayerActor0DefeatedFlag = 0;
    melonDS::u32 PlayerActor0PlayerBaseID = 0;
    melonDS::u32 PlayerActor0VisibleFlag = 0;
    melonDS::u32 PlayerActor0ActionFlag = 0;
    melonDS::u32 PlayerActor0SubActionFlag = 0;
    melonDS::u32 PlayerActor0PhysicsFlag = 0;
    melonDS::u32 PlayerActor0DamageCooldown = 0;
    melonDS::u32 PlayerActor0TransitFunc = 0;
    melonDS::u32 PlayerActor0TransitArg = 0;
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
    melonDS::u32 PlayerActor1CharacterIDBase = 0;
    melonDS::u32 PlayerActor1TransitioningFlag = 0;
    melonDS::u32 PlayerActor1DefeatedFlag = 0;
    melonDS::u32 PlayerActor1PlayerBaseID = 0;
    melonDS::u32 PlayerActor1VisibleFlag = 0;
    melonDS::u32 PlayerActor1ActionFlag = 0;
    melonDS::u32 PlayerActor1SubActionFlag = 0;
    melonDS::u32 PlayerActor1PhysicsFlag = 0;
    melonDS::u32 PlayerActor1DamageCooldown = 0;
    melonDS::u32 PlayerActor1TransitFunc = 0;
    melonDS::u32 PlayerActor1TransitArg = 0;
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
    melonDS::u32 StageDisplayCameraX = 0;
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
    melonDS::u32 ObjectActiveID[kObjectTraceSlots] {};
    melonDS::u32 ObjectActiveSettings[kObjectTraceSlots] {};
    melonDS::u32 ObjectActiveBase[kObjectTraceSlots] {};
    melonDS::u64 Hash = 0;
};

struct ObjectScanSample
{
    melonDS::u32 Found = 0;
    melonDS::u32 GUID = 0;
    melonDS::u32 Base = 0;
    melonDS::u32 Settings = 0;
    melonDS::u32 StateType = 0;
    melonDS::u32 Flags = 0;
    melonDS::u32 PosX = 0;
    melonDS::u32 PosY = 0;
    melonDS::u32 PosZ = 0;
    melonDS::u32 PrevX = 0;
    melonDS::u32 PrevY = 0;
    melonDS::u32 PrevZ = 0;
    melonDS::u32 LastStepX = 0;
    melonDS::u32 LastStepY = 0;
    melonDS::u32 LastStepZ = 0;
    melonDS::u32 VelH = 0;
    melonDS::u32 TargetVelH = 0;
    melonDS::u32 AccelV = 0;
    melonDS::u32 TargetVelV = 0;
    melonDS::u32 AccelH = 0;
    melonDS::u32 VelX = 0;
    melonDS::u32 VelY = 0;
    melonDS::u32 VelZ = 0;
    melonDS::u32 TargetVelX = 0;
    melonDS::u32 TargetVelY = 0;
    melonDS::u32 TargetVelZ = 0;
};

struct PlayerActorScanSample
{
    ObjectScanSample Actor0;
    ObjectScanSample Actor1;
};

struct ObjectLifecycleSummary
{
    melonDS::u32 Total = 0;
    melonDS::u32 NotCreated = 0;
    melonDS::u32 Active = 0;
    melonDS::u32 Dead = 0;
    melonDS::u32 SkipUpdate = 0;
    melonDS::u32 SkipRender = 0;
    melonDS::u32 FirstNotCreatedID = 0;
    melonDS::u32 FirstNotCreatedBase = 0;
    melonDS::u32 FirstNotCreatedFlags = 0;
    melonDS::u32 SecondNotCreatedID = 0;
    melonDS::u32 SecondNotCreatedBase = 0;
    melonDS::u32 SecondNotCreatedFlags = 0;
    melonDS::u32 ActiveID[kObjectTraceSlots] {};
    melonDS::u32 ActiveSettings[kObjectTraceSlots] {};
    melonDS::u32 ActiveBase[kObjectTraceSlots] {};
};

ObjectScanSample FindObjectByIDAndSettingsLoose(melonDS::NDS* nds, melonDS::u16 expectedObjectID, melonDS::u32 expectedSettings);

struct GameStateSyncHashes
{
    melonDS::u64 Basic = 0;
    melonDS::u64 PlayerGlobal = 0;
    melonDS::u64 WifiCandidate = 0;
    melonDS::u64 RenderCandidate = 0;
};

melonDS::u32 FindObjectBaseByID(melonDS::NDS* nds, melonDS::u16 objectID);
bool WriteARM9U32(melonDS::NDS* nds, melonDS::u32 addr, melonDS::u32 value);

bool IsARM9MainRAMAddress(melonDS::u32 addr)
{
    return (addr & 0xFF000000u) == 0x02000000u;
}

int CurrentPacketBridgeLocalPlayer();

struct State
{
    std::mutex Mutex;
    bool EnvChecked = false;
    bool Enabled = false;
    bool Ready = false;
    bool TestEnabled = false;
    bool TestAnnouncedQuit = false;
    bool FrameBarrierEnabled = false;
    bool SerialRunEnabled = false;
    Role NetRole = Role::Host;
    int Delay = kDefaultDelay;
    int NetplayWarmupFrames = 0;
    int LocalInstance = 0;
    int Port = 8065;
    const char* PeerHost = "127.0.0.1";
    melonDS::u32 NetplayStartFrame = 0;
    bool LocalWaitsForRemote = true;
    bool RemoteInputTimeoutFatal = false;
    melonDS::u32 TestFrames = kNoFrameLimit;
    int TestInstanceCount = 1;
    bool TestTimerStarted = false;
    std::chrono::steady_clock::time_point TestTimerStart;
    bool ActiveTimerStarted[16] {};
    melonDS::u32 ActiveTimerStartFrame[16] {};
    std::chrono::steady_clock::time_point ActiveTimerStart[16];
    melonDS::u32 ActiveFpsStartFrame = 0;
    int HashInterval = 60;
    bool HashEnabled = true;
    int TestWaitTimeoutMs = 5000;
    int TestQuitGraceMs = 0;
    bool InputTraceEnabled = false;
    int InputTraceInterval = 60;
    bool ScreenHashEnabled = false;
    bool GameStateTraceExtended = false;
    melonDS::u32 GameStateTraceStartFrame = 0;
    melonDS::u32 GameStateTraceEndFrame = 0;
    bool GameStateSyncEnabled = false;
    bool GameStateSyncExtended = false;
    bool GameStateApplyEnabled = false;
    bool GameStateApplyCriticalGlobals = true;
    bool GameStateApplyStarObjects = true;
    bool GameStateApplyStageObjects = true;
    bool GameStateApplyPlayerActors = true;
    int GameStateSyncInterval = 60;
    int SeedWaitTimeoutMs = 10000;
    bool WaitForPeerBeforeStart = false;
    bool WaitForPeerAtNetplayStart = false;
    bool WaitedForPeerAtNetplayStart = false;
    bool NetplayStartWaitArrived[16] {};
    bool NetplayStartWaitComplete = false;
    bool DeferNetworkUntilStart = false;
    bool NetplayFrameBarrierEnabled = false;
    bool PacketBridgeEnabled = false;
    bool PacketBridgeOnly = false;
    bool PacketBridgeAllowPreGame = false;
    bool PacketBridgeTraceEnabled = false;
    bool PacketBridgeSendLocalPlayerOnly = true;
    bool PacketBridgeWaitEnabled = false;
    int PacketBridgeWaitTimeoutMs = 0;
    melonDS::u32 PacketBridgeWaitStartFrame = 0;
    int PacketBridgeWaitTickAhead = 0;
    bool PacketBridgeDirectCaptureEnabled = false;
    bool PacketBridgeForceTickEnabled = false;
    melonDS::u32 PacketBridgeForceTickStartFrame = 0;
    int PacketBridgeForceTickBase = -1;
    bool PacketBridgeForceNetReady = false;
    melonDS::u32 PacketBridgeForceNetReadyStartFrame = 0;
    melonDS::u32 PacketBridgeForceNetReadyEndFrame = 0;
    bool PacketBridgeForceNetReadyHostOnly = false;
    bool PacketBridgeForceNetReadyClientOnly = false;
    bool PacketBridgeForceNetReadyState10 = false;
    bool PacketBridgeForceNetReadyState10ClientOnly = false;
    bool PacketBridgeForceLoadGameSM = false;
    melonDS::u32 PacketBridgeForceLoadGameSMStartFrame = 0;
    melonDS::u32 PacketBridgeForceLoadGameSMStep = 3;
    int PacketBridgeForceLoadGameSMTimer = -1;
    int PacketBridgeForceLoadGameSMFlags = -1;
    bool PacketBridgeForceLoadGameSMRunUpdate = false;
    bool PacketBridgeForceLoadGameSMRunUpdateClientOnly = true;
    bool PacketBridgeForceLoadGameSMPulseAction = false;
    bool PacketBridgeForceLoadGameSMBaselineFlags = false;
    bool PacketBridgeForceLoadGameSMPreload = false;
    bool PacketBridgeForceLoadGameSMAllowCourseSelect = false;
    bool PacketBridgeForceLoadGameSMCreateApplied[16] {};
    bool PacketBridgeForceStagePacketWords = false;
    melonDS::u32 PacketBridgeForceStagePacketWordsStartFrame = 0;
    melonDS::u32 PacketBridgeForceStagePacketWordsEndFrame = 0;
    bool PacketBridgeForceStageNet20OnStageScene = false;
    int PacketBridgeForceGameLocalPlayerID = -1;
    melonDS::u32 PacketBridgeForceGameLocalPlayerIDStartFrame = 0;
    bool PacketBridgeForceGameLocalPlayerIDEarly = false;
    bool PacketBridgeDummyAlloc = false;
    melonDS::u32 PacketBridgeDummyAllocFrame = 0;
    melonDS::u32 PacketBridgeDummyAllocSize = 0;
    bool PacketBridgeDummyAllocApplied[16] {};
    bool PacketBridgeScheduleLoadGameSM = false;
    bool PacketBridgeScheduleLoadGameSMApplied[16] {};
    bool PacketBridgeSubMenuDirectChange = false;
    bool PacketBridgeSubMenuCallCreate = false;
    struct PacketBridgeSubMenuSchedule
    {
        int RoleMask = 3;
        melonDS::u32 Frame = 0;
        melonDS::u32 SubMenu = 0;
        melonDS::u32 Delay = 1;
        melonDS::u32 Create = 1;
        bool Applied[16] {};
    };
    std::vector<PacketBridgeSubMenuSchedule> PacketBridgeSubMenuScheduleEntries;
    bool PacketBridgeForceStageStartSMFields = false;
    melonDS::u32 PacketBridgeForceStageStartSMFieldsStartFrame = 0;
    melonDS::u32 PacketBridgeStageStartSMBaseFrame[16] {};
    bool PacketBridgeForceStageStartSMUseLoadStep = false;
    bool PacketBridgeRunStageStartSMUpdate = false;
    melonDS::u32 PacketBridgeRunStageStartSMUpdateStartFrame = 0;
    melonDS::u32 PacketBridgeRunStageStartSMUpdateLastFrame[16] {};
    bool PacketBridgeRunVSConnectOnUpdate = false;
    melonDS::u32 PacketBridgeRunVSConnectOnUpdateStartFrame = 0;
    melonDS::u32 PacketBridgeRunVSConnectOnUpdateLastFrame[16] {};
    bool PacketBridgeForceMvlFileCache = false;
    melonDS::u32 PacketBridgeForceMvlFileCacheStartFrame = 0;
    bool PacketBridgeForceMvlFileCacheApplied[16] {};
    bool PacketBridgeForceMvlLoadThread = false;
    melonDS::u32 PacketBridgeForceMvlLoadThreadStartFrame = 0;
    bool PacketBridgeForceMvlLoadThreadApplied[16] {};
    int PacketBridgeMaxPumpEvents = kMaxPumpEvents;
    int PacketBridgeMaxTickLead = -1;
    int PacketBridgeMaxFrameLead = -1;
    int PacketBridgeThrottleTimeoutMs = 5000;
    melonDS::u32 PacketBridgeThrottleStartFrame = 0;
    int PacketBridgeLocalInputDelay = 0;
    bool PacketBridgeNeutralizeLocalInput = false;
    bool PacketBridgePreserveLocalTouch = false;
    int PacketBridgeSendDelayFrames = 0;
    int PacketBridgeSendJitterFrames = 0;
    int InputSendDelayFrames = 0;
    int InputSendJitterFrames = 0;
    bool InputUnreliable = false;
    int InputBundleHistory = 0;
    int InputDropModulo = 0;
    int InputDropOffset = 0;
    std::map<melonDS::u32, InputState> PacketBridgePacketInputs;
    std::vector<DelayedWireNSMLPacket> DelayedNSMLPackets;
    std::vector<DelayedWireInput> DelayedInputs;
    bool DirectMvlBootEnabled = false;
    bool DirectMvlBootHostOnly = false;
    bool DirectMvlBootClientOnly = false;
    melonDS::u32 DirectMvlBootFrame = 0;
    int DirectMvlBootScene = 0x0F;
    int DirectMvlBootStage = 0;
    int DirectMvlBootPlayerID = -1;
    bool DirectMvlBootUseLoadGameSM = false;
    bool DirectMvlBootPatchLoadGameSMOnly = false;
    bool DirectMvlBootCallUpdateLoadGameSM = false;
    bool DirectMvlBootCallStartLoadLevel = false;
    bool DirectMvlBootCallCreateCourseSelect = false;
    bool DirectMvlBootCallObjectCourseSelect = false;
    bool DirectMvlBootApplied[16] {};
    bool ForceCourseSelectFactory = false;
    melonDS::u32 ForceCourseSelectFactoryFrame = 0;
    int ForceCourseSelectFactoryPlayerArg = -1;
    bool ForceCourseSelectFactoryApplied[16] {};
    std::string InputScriptPath;
    std::string ScriptRemotePacketInputScriptPath;
    std::string HashLogPath;
    std::string ScreenshotDir;
    std::string StateSaveDir;
    std::string StateLoadDir;
    std::string RamDumpDir;
    std::string MemPatchFile;
    std::string GameStateTracePath;
    std::ofstream HashLog;
    std::ofstream GameStateTrace;
    int ScreenshotInterval = 0;
    int RamDumpInterval = 0;
    int GameStateTraceInterval = 60;
    int MemPatchInstance = -1;
    melonDS::u32 MemPatchFrame = 0;
    bool MemPatchFrameSet = false;
    bool MemPatchApplied[16] {};
    melonDS::u32 VsStarSnapFrame = 0;
    int VsStarSnapPlayerSlot = 0;
    bool VsStarSnapApplied[16] {};
    melonDS::u32 PlayerSnapToStarFrame = 0;
    int PlayerSnapToStarSlot = 0;
    bool PlayerSnapToStarApplied[16] {};
    melonDS::u32 PlayerStickToStarStartFrame = 0;
    melonDS::u32 PlayerStickToStarEndFrame = 0;
    int PlayerStickToStarSlot = 0;
    bool ForcePlayerCountEnabled = false;
    bool ForcePlayerCountHostOnly = false;
    bool ForcePlayerCountClientOnly = false;
    melonDS::u32 ForcePlayerCountStartFrame = 0;
    melonDS::u32 ForcePlayerCountEndFrame = 0;
    melonDS::u32 ForcePlayerCountValue = 2;
    bool ForcePlayerCountLogged[16] {};
    bool ForceStageSceneRuntimeWordsEnabled = false;
    bool ForceStageSceneRuntimeWordsHostOnly = false;
    bool ForceStageSceneRuntimeWordsClientOnly = false;
    melonDS::u32 ForceStageSceneRuntimeWordsStartFrame = 0;
    melonDS::u32 ForceStageSceneRuntimeWordsEndFrame = 0;
    melonDS::u32 ForceStageSceneWord154 = 1;
    melonDS::u32 ForceStageSceneWord160 = 0xDA;
    bool ForceStageSceneRuntimeWordsLogged[16] {};
    bool ForceStageSceneActiveEnabled = false;
    bool ForceStageSceneActiveHostOnly = false;
    bool ForceStageSceneActiveClientOnly = false;
    melonDS::u32 ForceStageSceneActiveStartFrame = 0;
    melonDS::u32 ForceStageSceneActiveEndFrame = 0;
    bool ForceStageSceneActiveLogged[16] {};
    bool ForceStageCameraSlotEnabled = false;
    bool ForceStageCameraSlotVerticalOnly = false;
    melonDS::u32 ForceStageCameraSlotStartFrame = 0;
    melonDS::u32 ForceStageCameraSlotEndFrame = 0;
    int ForceStageCameraSlotSource = 0;
    int ForceStageCameraSlotDest = 1;
    bool ForceStageCameraSlotLogged[16] {};
    bool ForceStageCameraSlotOverrideX = false;
    bool ForceStageCameraSlotOverrideY = false;
    bool ForceStageCameraSlotOverrideWidth = false;
    bool ForceStageCameraSlotOverrideHeight = false;
    melonDS::u32 ForceStageCameraSlotX = 0;
    melonDS::u32 ForceStageCameraSlotY = 0;
    melonDS::u32 ForceStageCameraSlotWidth = 0;
    melonDS::u32 ForceStageCameraSlotHeight = 0;
    bool ForceStageCameraObjectXEnabled = false;
    melonDS::u32 ForceStageCameraObjectXStartFrame = 0;
    melonDS::u32 ForceStageCameraObjectXEndFrame = 0;
    melonDS::u32 ForceStageCameraObjectX = 0;
    bool ForceStageCameraObjectXWriteDisplay = false;
    bool ForceStageCameraObjectXWriteSlot = false;
    int ForceStageCameraObjectXSlot = 1;
    bool ForceStageCameraObjectZEnabled = false;
    melonDS::u32 ForceStageCameraObjectZ = 0;
    bool ForceStageCameraObjectXLogged[16] {};
    bool ForceStageFXSettingsEnabled = false;
    bool ForceStageFXSettingsHostOnly = false;
    bool ForceStageFXSettingsClientOnly = false;
    melonDS::u32 ForceStageFXSettingsStartFrame = 0;
    melonDS::u32 ForceStageFXSettingsEndFrame = 0;
    melonDS::u32 ForceStageFXSettingsValue = 0x00008000;
    bool ForceStageFXSettingsLogged[16] {};
    bool CallStageScenePostCreateEnabled = false;
    bool CallStageScenePostCreateHostOnly = false;
    bool CallStageScenePostCreateClientOnly = false;
    melonDS::u32 CallStageScenePostCreateFrame = 0;
    bool CallStageScenePostCreateApplied[16] {};
    bool ForceStageActorFreezeFlagEnabled = false;
    bool ForceStageActorFreezeFlagHostOnly = false;
    bool ForceStageActorFreezeFlagClientOnly = false;
    melonDS::u32 ForceStageActorFreezeFlagStartFrame = 0;
    melonDS::u32 ForceStageActorFreezeFlagEndFrame = 0;
    melonDS::u32 ForceStageActorFreezeFlagValue = 0;
    bool ForceStageActorFreezeFlagLogged[16] {};
    bool ForceStageActorFreezeFlagReleased[16] {};
    bool ForcePlayerDeathCountersEnabled = false;
    bool ForcePlayerDeathCountersHostOnly = false;
    bool ForcePlayerDeathCountersClientOnly = false;
    melonDS::u32 ForcePlayerDeathCountersStartFrame = 0;
    melonDS::u32 ForcePlayerDeathCountersEndFrame = 0;
    melonDS::u32 ForcePlayerDeathCounter0 = 0;
    melonDS::u32 ForcePlayerDeathCounter1 = 0;
    bool ForcePlayerLivesEnabled = false;
    melonDS::u32 ForcePlayerLife0 = 5;
    melonDS::u32 ForcePlayerLife1 = 5;
    bool ForcePlayerDeathCountersLogged[16] {};
    bool ForcePlayerInventoryPowerupsEnabled = false;
    melonDS::u32 ForcePlayerInventoryPowerupsStartFrame = 0;
    melonDS::u32 ForcePlayerInventoryPowerupsEndFrame = 0;
    melonDS::u32 ForcePlayerInventoryPowerup0 = 0;
    melonDS::u32 ForcePlayerInventoryPowerup1 = 0;
    bool ForcePlayerInventoryPowerupsLogged[16] {};
    bool ForcePlayerStarCountersEnabled = false;
    melonDS::u32 ForcePlayerStarCountersStartFrame = 0;
    melonDS::u32 ForcePlayerStarCountersEndFrame = 0;
    melonDS::u32 ForcePlayerBattleStars0 = 0;
    melonDS::u32 ForcePlayerBattleStars1 = 0;
    melonDS::u32 ForcePlayerDisplayedStars0 = 0;
    melonDS::u32 ForcePlayerDisplayedStars1 = 0;
    melonDS::u32 ForcePlayerCollectedStars0 = 0;
    melonDS::u32 ForcePlayerCollectedStars1 = 0;
    bool ForcePlayerStarCountersLogged[16] {};
    bool TracePlayerLifeChanges = false;
    bool LastPlayerLifeSampleValid[16] {};
    GameStateSample LastPlayerLifeSample[16] {};
    bool ForceStageActorPreUpdateGateEnabled = false;
    bool ForceStageActorPreUpdateGateHostOnly = false;
    bool ForceStageActorPreUpdateGateClientOnly = false;
    melonDS::u32 ForceStageActorPreUpdateGateStartFrame = 0;
    melonDS::u32 ForceStageActorPreUpdateGateEndFrame = 0;
    bool ForceStageActorPreUpdateGateLogged[16] {};
    bool ForceActorCategoryMaskEnabled = false;
    bool ForceActorCategoryMaskHostOnly = false;
    bool ForceActorCategoryMaskClientOnly = false;
    melonDS::u32 ForceActorCategoryMaskStartFrame = 0;
    melonDS::u32 ForceActorCategoryMaskEndFrame = 0;
    melonDS::u32 ForceActorCategoryMaskValue = 0;
    bool ForceActorCategoryMaskLogged[16] {};
    bool ForcePlayerSignalUnlockEnabled = false;
    bool ForcePlayerSignalUnlockHostOnly = false;
    bool ForcePlayerSignalUnlockClientOnly = false;
    melonDS::u32 ForcePlayerSignalUnlockStartFrame = 0;
    melonDS::u32 ForcePlayerSignalUnlockEndFrame = 0;
    bool ForcePlayerSignalUnlockLogged[16] {};
    bool ForcePlayerUpdateEnableEnabled = false;
    bool ForcePlayerUpdateEnableHostOnly = false;
    bool ForcePlayerUpdateEnableClientOnly = false;
    melonDS::u32 ForcePlayerUpdateEnableStartFrame = 0;
    melonDS::u32 ForcePlayerUpdateEnableEndFrame = 0;
    bool ForcePlayerUpdateEnableLogged[16] {};
    bool ForceStageSceneStartGateEnabled = false;
    bool ForceStageSceneStartGateHostOnly = false;
    bool ForceStageSceneStartGateClientOnly = false;
    bool ForceStageSceneFadeReady = false;
    bool ForceStageSceneInputLatchEnabled = false;
    int ForceNetLocalAid = -1;
    melonDS::u32 ForceNetLocalAidStartFrame = 0;
    melonDS::u32 ForceNetLocalAidEndFrame = 0;
    bool ForceNetLocalAidLogged[16] {};
    int ForceWifiCommunicatingCount = -1;
    melonDS::u32 ForceWifiCommunicatingStartFrame = 0;
    melonDS::u32 ForceWifiCommunicatingEndFrame = 0;
    bool ForceWifiCommunicatingLogged[16] {};
    bool ScriptRemotePacketEnabled = false;
    int ScriptRemotePacketPlayer = -1;
    int ScriptRemotePacketInputInstance = -1;
    melonDS::u32 ScriptRemotePacketStartFrame = 0;
    melonDS::u32 ScriptRemotePacketEndFrame = 0;
    bool ScriptRemotePacketLogged[16] {};
    bool PacketBridgeJitHelperPatchEnabled = false;
    melonDS::u32 PacketBridgeJitHelperPatchFrame = 0;
    bool PacketBridgeJitHelperPatchApplied[16] {};
    bool InputNetplayOnly = false;
    bool InputNetplayTraceEnabled = false;
    int InputNetplayMaxFrameLead = -1;
    bool RollbackEnabled = false;
    bool RollbackResimulate = false;
    bool RollbackRestoreProbe = false;
    RollbackBackend RollbackBackendMode = RollbackBackend::Savestate;
    int RollbackWindow = 20;
    int RollbackCheckpointInterval = 1;
    int RollbackResimulateDelayFrames = 0;
    std::map<melonDS::u32, InputState> PredictedRemoteInputs;
    std::map<melonDS::u32, std::vector<char>> RollbackStates;
    bool LastConfirmedRemoteInputValid = false;
    InputState LastConfirmedRemoteInput {};
    melonDS::u32 PendingRollbackFrame = kNoFrameLimit;
    melonDS::u32 PendingRollbackObservedFrame = kNoFrameLimit;
    melonDS::u32 RollbackPredictionCount = 0;
    melonDS::u32 RollbackMismatchCount = 0;
    melonDS::u32 RollbackRestoreCount = 0;
    melonDS::u32 RollbackResimulateCount = 0;
    melonDS::u32 RollbackCheckpointSaveCount = 0;
    melonDS::u32 LastRollbackTraceFrame = kNoFrameLimit;
    melonDS::u32 ForceStageSceneStartGateStartFrame = 0;
    melonDS::u32 ForceStageSceneStartGateEndFrame = 0;
    melonDS::u32 ForceStageSceneStartGateValue = 1;
    bool ForceStageSceneStartGateLogged[16] {};
    bool ForceStageSceneContinueGateEnabled = false;
    bool ForceStageSceneContinueGateHostOnly = false;
    bool ForceStageSceneContinueGateClientOnly = false;
    melonDS::u32 ForceStageSceneContinueGateStartFrame = 0;
    melonDS::u32 ForceStageSceneContinueGateEndFrame = 0;
    melonDS::u32 ForceStageSceneContinueGateValue = 1;
    bool ForceStageSceneContinueGateLogged[16] {};
    bool ForceStageSceneState3GateEnabled = false;
    bool ForceStageSceneState3GateHostOnly = false;
    bool ForceStageSceneState3GateClientOnly = false;
    melonDS::u32 ForceStageSceneState3GateStartFrame = 0;
    melonDS::u32 ForceStageSceneState3GateEndFrame = 0;
    melonDS::u32 ForceStageSceneState3GateValue = 1;
    bool ForceStageSceneState3GateLogged[16] {};
    bool ForceStageSceneEventFlagsEnabled = false;
    bool ForceStageSceneEventFlagsHostOnly = false;
    bool ForceStageSceneEventFlagsClientOnly = false;
    melonDS::u32 ForceStageSceneEventFlagsStartFrame = 0;
    melonDS::u32 ForceStageSceneEventFlagsEndFrame = 0;
    melonDS::u32 ForceStageSceneEventFlagsValue = 0;
    bool ForceStageSceneEventFlagsLogged[16] {};
    bool ForceMvlPlayerReadyEnabled = false;
    bool ForceMvlPlayerReadyHostOnly = false;
    bool ForceMvlPlayerReadyClientOnly = false;
    melonDS::u32 ForceMvlPlayerReadyStartFrame = 0;
    melonDS::u32 ForceMvlPlayerReadyEndFrame = 0;
    melonDS::u32 ForceMvlPlayerReadyValue = 0xFF00;
    bool ForceMvlPlayerReadySetA8EC = false;
    melonDS::u32 ForceMvlPlayerReadyA8ECValue = 0xFF;
    bool ForceMvlPlayerReadyLogged[16] {};
    bool ForceMvlPlayerReadyMissingLogged[16] {};
    bool ForceMvlRuntimeStateEnabled = false;
    bool ForceMvlRuntimeStateHostOnly = false;
    bool ForceMvlRuntimeStateClientOnly = false;
    melonDS::u32 ForceMvlRuntimeStateStartFrame = 0;
    melonDS::u32 ForceMvlRuntimeStateEndFrame = 0;
    melonDS::u32 ForceMvlRuntimeStateValue = 3;
    bool ForceMvlRuntimeStateLogged[16] {};
    bool ForcePlayerActorIDsEnabled = false;
    bool ForcePlayerActorIDsHostOnly = false;
    bool ForcePlayerActorIDsClientOnly = false;
    melonDS::u32 ForcePlayerActorIDsStartFrame = 0;
    melonDS::u32 ForcePlayerActorIDsEndFrame = 0;
    bool ForcePlayerActorIDsLogged[16] {};
    bool ForcePlayerActorPositionEnabled = false;
    int ForcePlayerActorPositionSlot = 1;
    melonDS::u32 ForcePlayerActorPositionStartFrame = 0;
    melonDS::u32 ForcePlayerActorPositionEndFrame = 0;
    melonDS::u32 ForcePlayerActorPositionX = 0;
    melonDS::u32 ForcePlayerActorPositionY = 0;
    melonDS::u32 ForcePlayerActorPositionZ = 0;
    bool ForcePlayerActorPositionCharacterSet = false;
    melonDS::u16 ForcePlayerActorPositionCharacter = 0;
    bool ForcePlayerActorPositionPlayerIDSet = false;
    melonDS::u8 ForcePlayerActorPositionPlayerID = 0;
    bool ForcePlayerActorPositionLogged[16] {};
    bool ForcePlayerTransitionStatusEnabled = false;
    bool ForcePlayerTransitionStatusHostOnly = false;
    bool ForcePlayerTransitionStatusClientOnly = false;
    melonDS::u32 ForcePlayerTransitionStatusStartFrame = 0;
    melonDS::u32 ForcePlayerTransitionStatusEndFrame = 0;
    melonDS::u32 ForcePlayerTransitionStatusValue = 2;
    bool ForcePlayerTransitionStatusLogged[16] {};
    bool ForceEntranceSpawnPointersEnabled = false;
    bool ForceEntranceSpawnPointersHostOnly = false;
    bool ForceEntranceSpawnPointersClientOnly = false;
    melonDS::u32 ForceEntranceSpawnPointersStartFrame = 0;
    melonDS::u32 ForceEntranceSpawnPointersEndFrame = 0;
    melonDS::u32 ForceEntranceSpawnPtr0 = 0;
    melonDS::u32 ForceEntranceSpawnPtr1 = 0;
    melonDS::u32 ForceEntranceSpawnID0 = 0;
    melonDS::u32 ForceEntranceSpawnID1 = 1;
    bool ForceEntranceSpawnPointersLogged[16] {};
    bool ForceMvlStageLayoutGateEnabled = false;
    bool ForceMvlStageLayoutGateHostOnly = false;
    bool ForceMvlStageLayoutGateClientOnly = false;
    melonDS::u32 ForceMvlStageLayoutGateStartFrame = 0;
    melonDS::u32 ForceMvlStageLayoutGateEndFrame = 0;
    melonDS::u32 ForceMvlStageLayoutGateAddr = 0x020CAC74;
    melonDS::u32 ForceMvlStageLayoutGateValue = 5;
    bool ForceMvlStageLayoutGateLogged[16] {};
    bool ForceMvlStageLayoutBufferEnabled = false;
    bool ForceMvlStageLayoutBufferHostOnly = false;
    bool ForceMvlStageLayoutBufferClientOnly = false;
    melonDS::u32 ForceMvlStageLayoutBufferStartFrame = 0;
    melonDS::u32 ForceMvlStageLayoutBufferEndFrame = 0;
    melonDS::u32 ForceMvlStageLayoutBufferAddr = 0x023C8000;
    bool ForceMvlStageLayoutBufferApplied[16] {};
    bool CallMvlStageLayoutInitEnabled = false;
    bool CallMvlStageLayoutInitHostOnly = false;
    bool CallMvlStageLayoutInitClientOnly = false;
    melonDS::u32 CallMvlStageLayoutInitFrame = 0;
    bool CallMvlStageLayoutInitApplied[16] {};
    bool NetRandomPatchEnabled = false;
    bool NetRandomPatchAuto = false;
    melonDS::u32 NetRandomPatchFrame = 0;
    melonDS::u32 NetRandomPatchValue = 0;
    bool NetRandomPatchApplied[16] {};
    bool MatchSeedConfigured = false;
    bool MatchSeedSent = false;
    melonDS::u32 MatchSeed = 0;
    bool NetplayAnyLockstepStarted = false;
    bool NetplayLockstepStarted[16] {};
    melonDS::u32 StateSaveFrame = 0;
    melonDS::u32 StateLoadFrame = 0;
    bool StateLoadFrameSet = false;
    ENetHost* Host = nullptr;
    ENetPeer* Peer = nullptr;
    bool ENetInitialized = false;
    std::map<melonDS::u32, InputState> LocalInputs;
    std::map<melonDS::u32, InputState> RemoteInputs;
    std::map<melonDS::u64, GameStateSyncHashes> LocalGameStateHashes;
    std::map<melonDS::u64, GameStateSyncHashes> RemoteGameStateHashes;
    std::map<melonDS::u64, GameStateSample> RemoteGameStateSamples;
    std::vector<WireNSMLPacket> PendingNSMLPackets;
    bool GameStateMismatchSeen = false;
    melonDS::u32 LastTracedSentInputFrame = kNoFrameLimit;
    melonDS::u32 LastTracedReceivedInputFrame = kNoFrameLimit;
    melonDS::u32 LastReceivedInputFrame = kNoFrameLimit;
    melonDS::u32 LastInputFrameThrottleTraceFrame = kNoFrameLimit;
    melonDS::u32 LastSentNSMLPacketTick = 0xFFFFFFFF;
    melonDS::u32 LastReceivedNSMLPacketTick[2] { 0xFFFFFFFF, 0xFFFFFFFF };
    melonDS::u32 LastReceivedNSMLPacketFrame[2] { 0xFFFFFFFF, 0xFFFFFFFF };
    melonDS::u32 LastPacketBridgeWaitTimeoutTick = 0xFFFFFFFF;
    melonDS::u32 LastPacketBridgeThrottleTraceTick = 0xFFFFFFFF;
    melonDS::u32 LastPacketBridgeFrameThrottleTraceFrame = 0xFFFFFFFF;
    melonDS::u32 LastPacketBridgeForcedTickFrame[16] {};
    std::vector<std::pair<melonDS::u32, melonDS::u32>> RamDumpRanges;
    std::vector<std::pair<melonDS::u32, melonDS::u32>> MemPatchRanges;
    melonDS::u64 LastLoggedHashFrame[16] {};
    melonDS::u64 LastLoggedGameStateFrame[16] {};
    melonDS::u64 LastSentGameStateFrame[16] {};
    melonDS::u32 TestFrameCount[16] {};
    bool StateSaved[16] {};
    bool StateLoaded[16] {};
    bool LocalMPSaved = false;
    bool LocalMPLoadStarted = false;
    bool LocalMPLoadFinished = false;
    bool LocalMPLoaded = false;
    melonDS::u32 SerialFrame = 0;
    int SerialInstance = 0;
    std::condition_variable BarrierCond;
    std::condition_variable InputCond;
    bool NetworkPumpThreadEnabled = false;
    bool NetworkPumpThreadStarted = false;
    bool NetworkPumpStop = false;
    int NetworkPumpSleepUs = 250;
    int InputWaitPollUs = 100;
    std::thread NetworkPumpThread;
    unsigned long long RemoteInputWaitCount = 0;
    unsigned long long RemoteInputWaitLoops = 0;
    unsigned long long RemoteInputWaitUs = 0;
    unsigned long long RemoteInputWaitMaxUs = 0;
    unsigned long long FrameLeadThrottleCount = 0;
    unsigned long long FrameLeadThrottleLoops = 0;
    unsigned long long FrameLeadThrottleUs = 0;
    unsigned long long FrameLeadThrottleMaxUs = 0;
};

struct InputSpan
{
    int Instance = -1;
    melonDS::u32 Start = 0;
    melonDS::u32 End = 0;
    InputState Input;
};

State G;
std::vector<InputSpan> GInputScript;
std::vector<InputSpan> GScriptRemotePacketInputScript;

struct FrameBarrier
{
    bool Waiting[16] {};
    melonDS::u32 Frame[16] {};
    int Generation = 0;
};

FrameBarrier GBeforeFrameBarrier;
FrameBarrier GAfterFrameBarrier;
FrameBarrier GNetplayFrameBarrier;

bool EnvFlag(const char* name)
{
    const char* value = std::getenv(name);
    return value && value[0] && std::strcmp(value, "0") != 0;
}

const char* EnvCString(const char* name, const char* fallback)
{
    const char* value = std::getenv(name);
    return value && value[0] ? value : fallback;
}

int EnvInt(const char* name, int fallback)
{
    const char* value = std::getenv(name);
    if (!value || !value[0]) return fallback;
    return std::atoi(value);
}

melonDS::u32 GenerateMatchSeed()
{
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::random_device rd;
    return static_cast<melonDS::u32>(now) ^ (static_cast<melonDS::u32>(rd()) * 0x45D9F3Bu);
}

InputState NeutralInput()
{
    InputState input {};
    input.KeyMask = 0xFFF;
    return input;
}

bool InputsEqual(const InputState& a, const InputState& b)
{
    return a.KeyMask == b.KeyMask
        && a.Touching == b.Touching
        && a.TouchX == b.TouchX
        && a.TouchY == b.TouchY;
}

InputState NeutralInputPreservingTouch(const InputState& source)
{
    InputState input = NeutralInput();
    input.Touching = source.Touching;
    input.TouchX = source.TouchX;
    input.TouchY = source.TouchY;
    return input;
}

std::string Trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string Upper(std::string value)
{
    for (char& ch : value)
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return value;
}

bool ParseU32(const std::string& text, melonDS::u32& out)
{
    char* end = nullptr;
    unsigned long value = std::strtoul(text.c_str(), &end, 0);
    if (!end || *end != '\0') return false;
    out = static_cast<melonDS::u32>(value);
    return true;
}

int ButtonBit(const std::string& name)
{
    const std::string key = Upper(name);
    if (key == "A") return 0;
    if (key == "B") return 1;
    if (key == "SELECT") return 2;
    if (key == "START") return 3;
    if (key == "RIGHT") return 4;
    if (key == "LEFT") return 5;
    if (key == "UP") return 6;
    if (key == "DOWN") return 7;
    if (key == "R") return 8;
    if (key == "L") return 9;
    if (key == "X") return 10;
    if (key == "Y") return 11;
    return -1;
}

bool ParseInputSpec(const std::string& spec, InputState& input)
{
    input = NeutralInput();

    if (spec.empty() || Upper(spec) == "NONE" || Upper(spec) == "NEUTRAL")
        return true;

    if (spec.rfind("mask=", 0) == 0 || spec.rfind("MASK=", 0) == 0)
    {
        melonDS::u32 mask = 0;
        if (!ParseU32(spec.substr(5), mask)) return false;
        input.KeyMask = mask & 0xFFF;
        return true;
    }

    std::stringstream ss(spec);
    std::string button;
    while (std::getline(ss, button, '+'))
    {
        button = Trim(button);
        const int bit = ButtonBit(button);
        if (bit < 0) return false;
        input.KeyMask &= ~(1u << bit);
    }

    return true;
}

bool LoadInputScriptFileLocked(const std::string& path, std::vector<InputSpan>& spans)
{
    if (path.empty()) return true;

    spans.clear();
    std::ifstream file(path);
    if (!file)
    {
        std::printf("NSMB Test: failed to open input script: %s\n", path.c_str());
        return false;
    }

    std::string line;
    int lineNo = 0;
    while (std::getline(file, line))
    {
        lineNo++;

        const auto comment = line.find('#');
        if (comment != std::string::npos)
            line.resize(comment);
        line = Trim(line);
        if (line.empty()) continue;

        std::stringstream ss(line);
        std::string target;
        std::string range;
        std::string buttons;
        std::string touch;
        ss >> target >> range >> buttons >> touch;

        InputSpan span;
        if (target.find('-') != std::string::npos)
        {
            touch = buttons;
            buttons = range;
            range = target;
        }
        else
        {
            const std::string upperTarget = Upper(target);
            if (upperTarget != "ALL")
            {
                const std::string prefix = "INST";
                melonDS::u32 targetInstance = 0;
                if (upperTarget.rfind(prefix, 0) != 0 ||
                    !ParseU32(upperTarget.substr(prefix.size()), targetInstance) ||
                    targetInstance >= 16)
                {
                    std::printf("NSMB Test: invalid input target at %s:%d\n", path.c_str(), lineNo);
                    return false;
                }
                span.Instance = static_cast<int>(targetInstance);
            }
        }

        const auto dash = range.find('-');
        if (dash == std::string::npos)
        {
            std::printf("NSMB Test: invalid range at %s:%d\n", path.c_str(), lineNo);
            return false;
        }

        if (!ParseU32(range.substr(0, dash), span.Start) ||
            !ParseU32(range.substr(dash + 1), span.End) ||
            span.End < span.Start ||
            !ParseInputSpec(buttons, span.Input))
        {
            std::printf("NSMB Test: invalid input line at %s:%d\n", path.c_str(), lineNo);
            return false;
        }

        if (!touch.empty())
        {
            const auto comma = touch.find(',');
            melonDS::u32 x = 0;
            melonDS::u32 y = 0;
            if (comma == std::string::npos ||
                !ParseU32(touch.substr(0, comma), x) ||
                !ParseU32(touch.substr(comma + 1), y))
            {
                std::printf("NSMB Test: invalid touch at %s:%d\n", path.c_str(), lineNo);
                return false;
            }
            span.Input.Touching = true;
            span.Input.TouchX = static_cast<melonDS::u16>(std::min<melonDS::u32>(x, 255));
            span.Input.TouchY = static_cast<melonDS::u16>(std::min<melonDS::u32>(y, 191));
        }

        spans.push_back(span);
    }

    std::printf("NSMB Test: loaded %zu input spans from %s\n",
        spans.size(),
        path.c_str());
    return true;
}

bool LoadInputScriptLocked()
{
    return LoadInputScriptFileLocked(G.InputScriptPath, GInputScript);
}

bool ParseFrameRanges(const char* value, std::vector<std::pair<melonDS::u32, melonDS::u32>>& out)
{
    if (!value || !value[0]) return true;

    std::stringstream ss(value);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        token = Trim(token);
        if (token.empty()) continue;

        melonDS::u32 start = 0;
        melonDS::u32 end = 0;
        const auto dash = token.find('-');
        if (dash == std::string::npos)
        {
            if (!ParseU32(token, start))
                return false;
            end = start;
        }
        else
        {
            if (!ParseU32(token.substr(0, dash), start) ||
                !ParseU32(token.substr(dash + 1), end) ||
                end < start)
                return false;
        }

        out.emplace_back(start, end);
    }

    return true;
}

bool ParsePacketBridgeSubMenuSchedule(const char* value, std::vector<State::PacketBridgeSubMenuSchedule>& out)
{
    out.clear();
    if (!value || !value[0]) return true;

    std::stringstream ss(value);
    std::string token;
    while (std::getline(ss, token, ';'))
    {
        token = Trim(token);
        if (token.empty()) continue;

        std::vector<std::string> fields;
        std::stringstream entryStream(token);
        std::string field;
        while (std::getline(entryStream, field, ','))
            fields.push_back(Trim(field));

        if (fields.size() != 5)
            return false;

        State::PacketBridgeSubMenuSchedule entry {};
        const std::string role = Upper(fields[0]);
        if (role == "HOST")
            entry.RoleMask = 1;
        else if (role == "CLIENT")
            entry.RoleMask = 2;
        else if (role == "BOTH" || role == "ALL")
            entry.RoleMask = 3;
        else
            return false;

        if (!ParseU32(fields[1], entry.Frame) ||
            !ParseU32(fields[2], entry.SubMenu) ||
            !ParseU32(fields[3], entry.Delay) ||
            !ParseU32(fields[4], entry.Create))
        {
            return false;
        }

        entry.Delay &= 0xFF;
        entry.Create = entry.Create ? 1 : 0;
        out.push_back(entry);
    }

    return true;
}

void MixGameStateValue(melonDS::u64& hash, melonDS::u64 value);

melonDS::u64 GameStateKey(int instanceID, melonDS::u32 frame)
{
    return (static_cast<melonDS::u64>(static_cast<melonDS::u32>(instanceID)) << 32) | frame;
}

melonDS::u64 CombinedGameStateHash(const GameStateSyncHashes& hashes)
{
    melonDS::u64 combined = hashes.Basic;
    MixGameStateValue(combined, hashes.PlayerGlobal);
    MixGameStateValue(combined, hashes.WifiCandidate);
    MixGameStateValue(combined, hashes.RenderCandidate);
    return combined;
}

void CompareGameStateLocked(int instanceID, melonDS::u32 frame)
{
    const melonDS::u64 key = GameStateKey(instanceID, frame);
    auto local = G.LocalGameStateHashes.find(key);
    auto remote = G.RemoteGameStateHashes.find(key);
    if (local == G.LocalGameStateHashes.end() || remote == G.RemoteGameStateHashes.end())
        return;
    const GameStateSyncHashes& lhs = local->second;
    const GameStateSyncHashes& rhs = remote->second;
    if (lhs.Basic == rhs.Basic
        && lhs.PlayerGlobal == rhs.PlayerGlobal
        && lhs.WifiCandidate == rhs.WifiCandidate
        && lhs.RenderCandidate == rhs.RenderCandidate)
        return;

    G.GameStateMismatchSeen = true;
    std::printf("NSMB PoC: game state mismatch inst=%d frame=%u local=%016llX remote=%016llX basic=%d playerGlobal=%d wifiCandidate=%d renderCandidate=%d\n",
        instanceID,
        frame,
        static_cast<unsigned long long>(CombinedGameStateHash(lhs)),
        static_cast<unsigned long long>(CombinedGameStateHash(rhs)),
        lhs.Basic == rhs.Basic ? 1 : 0,
        lhs.PlayerGlobal == rhs.PlayerGlobal ? 1 : 0,
        lhs.WifiCandidate == rhs.WifiCandidate ? 1 : 0,
        lhs.RenderCandidate == rhs.RenderCandidate ? 1 : 0);
    std::printf("NSMB PoC: game state components local basic=%016llX playerGlobal=%016llX wifiCandidate=%016llX renderCandidate=%016llX\n",
        static_cast<unsigned long long>(lhs.Basic),
        static_cast<unsigned long long>(lhs.PlayerGlobal),
        static_cast<unsigned long long>(lhs.WifiCandidate),
        static_cast<unsigned long long>(lhs.RenderCandidate));
    std::printf("NSMB PoC: game state components remote basic=%016llX playerGlobal=%016llX wifiCandidate=%016llX renderCandidate=%016llX\n",
        static_cast<unsigned long long>(rhs.Basic),
        static_cast<unsigned long long>(rhs.PlayerGlobal),
        static_cast<unsigned long long>(rhs.WifiCandidate),
        static_cast<unsigned long long>(rhs.RenderCandidate));
}

InputState ApplyInputSpans(
    const std::vector<InputSpan>& spans,
    int instanceID,
    melonDS::u32 frame,
    const InputState& fallback)
{
    if (spans.empty()) return fallback;

    for (const InputSpan& span : spans)
    {
        if ((span.Instance < 0 || span.Instance == instanceID) &&
            frame >= span.Start && frame <= span.End)
            return span.Input;
    }

    return fallback;
}

InputState ApplyInputScript(int instanceID, melonDS::u32 frame, const InputState& fallback)
{
    if (!G.TestEnabled) return fallback;
    return ApplyInputSpans(GInputScript, instanceID, frame, fallback);
}

InputState ApplyScriptRemotePacketInputScript(int instanceID, melonDS::u32 frame, const InputState& fallback)
{
    if (!G.TestEnabled) return fallback;
    if (GScriptRemotePacketInputScript.empty())
        return ApplyInputScript(instanceID, frame, fallback);
    return ApplyInputSpans(GScriptRemotePacketInputScript, instanceID, frame, fallback);
}

void FlushDelayedInputsLocked(melonDS::u32 frame);

void StoreRemoteInputLocked(melonDS::u32 frame, const InputState& receivedInput, melonDS::u32 localFrame)
{
    if (G.RollbackEnabled)
    {
        auto predicted = G.PredictedRemoteInputs.find(frame);
        if (predicted != G.PredictedRemoteInputs.end()
            && !InputsEqual(predicted->second, receivedInput))
        {
            const InputState predictedInput = predicted->second;
            G.RollbackMismatchCount++;
            const melonDS::u32 observedFrame = localFrame == kNoFrameLimit
                ? frame
                : localFrame;
            if (G.PendingRollbackFrame == kNoFrameLimit
                || frame < G.PendingRollbackFrame)
            {
                G.PendingRollbackFrame = frame;
                G.PendingRollbackObservedFrame = observedFrame;
            }
            else if (G.PendingRollbackObservedFrame == kNoFrameLimit)
            {
                G.PendingRollbackObservedFrame = observedFrame;
            }
            G.PredictedRemoteInputs.erase(G.PredictedRemoteInputs.lower_bound(frame),
                G.PredictedRemoteInputs.end());
            if (G.InputNetplayTraceEnabled)
            {
                std::printf(
                    "NSMB Rollback: prediction mismatch frame=%u predicted={keys=0x%03X touch=%d x=%u y=%u} actual={keys=0x%03X touch=%d x=%u y=%u} pending=%u mismatches=%u\n",
                    frame,
                    predictedInput.KeyMask,
                    predictedInput.Touching ? 1 : 0,
                    predictedInput.TouchX,
                    predictedInput.TouchY,
                    receivedInput.KeyMask,
                    receivedInput.Touching ? 1 : 0,
                    receivedInput.TouchX,
                    receivedInput.TouchY,
                    G.PendingRollbackFrame,
                    G.RollbackMismatchCount);
                std::fflush(stdout);
            }
        }
        G.LastConfirmedRemoteInput = receivedInput;
        G.LastConfirmedRemoteInputValid = true;
    }
    G.RemoteInputs[frame] = receivedInput;
    if (G.LastReceivedInputFrame == kNoFrameLimit || frame > G.LastReceivedInputFrame)
        G.LastReceivedInputFrame = frame;
    G.InputCond.notify_all();
    if (G.InputTraceEnabled
        && frame != G.LastTracedReceivedInputFrame
        && (G.InputTraceInterval <= 1 || (frame % static_cast<melonDS::u32>(G.InputTraceInterval)) == 0))
    {
        G.LastTracedReceivedInputFrame = frame;
        std::printf("NSMB PoC: recv input frame=%u keys=0x%03X remoteQueue=%zu\n",
            frame,
            receivedInput.KeyMask,
            G.RemoteInputs.size());
    }
}

void PumpNetworkLocked(melonDS::NDS* nds = nullptr, melonDS::u32 localFrame = kNoFrameLimit)
{
    if (!G.Host) return;

    FlushDelayedInputsLocked(localFrame);

    ENetEvent event;
    const int maxEvents = std::clamp(G.PacketBridgeMaxPumpEvents, 1, kMaxPumpEvents);
    for (int i = 0; i < maxEvents; i++)
    {
        int result = enet_host_service(G.Host, &event, 0);
        if (result <= 0) break;

        switch (event.type)
        {
        case ENET_EVENT_TYPE_CONNECT:
            G.Peer = event.peer;
            G.MatchSeedSent = false;
            G.InputCond.notify_all();
            std::printf("NSMB PoC: peer connected\n");
            break;

        case ENET_EVENT_TYPE_RECEIVE:
            if (event.packet->dataLength == sizeof(WireInput))
            {
                WireInput packet;
                std::memcpy(&packet, event.packet->data, sizeof(packet));
                if (packet.Magic == kMagic && packet.Version == kVersion)
                {
                    const InputState receivedInput {
                        packet.KeyMask,
                        packet.Touching != 0,
                        packet.TouchX,
                        packet.TouchY,
                    };
                    StoreRemoteInputLocked(packet.Frame, receivedInput, localFrame);
                }
            }
            else if (event.packet->dataLength > sizeof(WireSeed))
            {
                WireInputBundleHeader header;
                std::memcpy(&header, event.packet->data, sizeof(header));
                if (header.Magic == kMagic
                    && header.Version == kVersion
                    && header.Kind == kWireKindInputBundle
                    && header.Count > 0
                    && header.Count <= 32
                    && event.packet->dataLength == sizeof(WireInputBundleHeader)
                        + sizeof(WireInputBundleEntry) * header.Count)
                {
                    const auto* entries = reinterpret_cast<const WireInputBundleEntry*>(
                        event.packet->data + sizeof(WireInputBundleHeader));
                    for (melonDS::u32 entryIndex = 0; entryIndex < header.Count; entryIndex++)
                    {
                        const WireInputBundleEntry& entry = entries[entryIndex];
                        const InputState receivedInput {
                            entry.KeyMask,
                            entry.Touching != 0,
                            entry.TouchX,
                            entry.TouchY,
                        };
                        StoreRemoteInputLocked(entry.Frame, receivedInput, localFrame);
                    }
                }
            }
            else if (event.packet->dataLength == sizeof(WireSeed))
            {
                WireSeed packet;
                std::memcpy(&packet, event.packet->data, sizeof(packet));
                if (packet.Magic == kMagic && packet.Version == kVersion && packet.Kind == kWireKindSeed)
                {
                    G.MatchSeed = packet.Seed;
                    G.MatchSeedConfigured = true;
                    G.InputCond.notify_all();
                    if (G.StateLoadDir.empty() && !G.InputNetplayOnly)
                    {
                        G.NetRandomPatchValue = packet.Seed;
                        G.NetRandomPatchEnabled = true;
                        G.NetRandomPatchAuto = true;
                    }
                    std::printf("NSMB PoC: received match seed 0x%08X\n", packet.Seed);
                }
            }
            else if (event.packet->dataLength == sizeof(WireNSMLPacket))
            {
                WireNSMLPacket packet;
                std::memcpy(&packet, event.packet->data, sizeof(packet));
                if (packet.Magic == kMagic && packet.Version == kVersion && packet.Kind == kWireKindPacket
                    && packet.Player <= 1)
                {
                    if (G.PacketBridgeEnabled && nds)
                        melonDS::NSML_PushMarioVsLuigiRemotePacket(nds, packet.Player, packet.Packet);
                    else
                        G.PendingNSMLPackets.push_back(packet);
                    const bool newTick = packet.Tick != G.LastReceivedNSMLPacketTick[packet.Player];
                    G.LastReceivedNSMLPacketTick[packet.Player] = packet.Tick;
                    G.LastReceivedNSMLPacketFrame[packet.Player] = packet.Frame;
                    if (G.PacketBridgeTraceEnabled && newTick)
                    {
                        const melonDS::u32 keys = packet.Packet[2] | (packet.Packet[3] << 8);
                        std::printf("NSMB PacketBridge: recv player=%u tick=0x%04X keys=0x%04X action=0x%02X b5=0x%02X b6=0x%02X b7=0x%02X bit=0x%02X remoteFrame=%u localFrame=%u pending=%zu\n",
                            packet.Player,
                            packet.Tick,
                            keys,
                            packet.Packet[4],
                            packet.Packet[5],
                            packet.Packet[6],
                            packet.Packet[7],
                            packet.Packet[0x29],
                            packet.Frame,
                            localFrame,
                            G.PendingNSMLPackets.size());
                    }
                }
            }
            else if (event.packet->dataLength == sizeof(WireGameState))
            {
                WireGameState packet;
                std::memcpy(&packet, event.packet->data, sizeof(packet));
                if (packet.Magic == kMagic && packet.Version == kVersion && packet.Kind == kWireKindState)
                {
                    GameStateSyncHashes hashes;
                    hashes.Basic = (static_cast<melonDS::u64>(packet.BasicHashHi) << 32) | packet.BasicHashLo;
                    hashes.PlayerGlobal = (static_cast<melonDS::u64>(packet.PlayerGlobalHashHi) << 32) | packet.PlayerGlobalHashLo;
                    hashes.WifiCandidate = (static_cast<melonDS::u64>(packet.WifiCandidateHashHi) << 32) | packet.WifiCandidateHashLo;
                    hashes.RenderCandidate = (static_cast<melonDS::u64>(packet.RenderCandidateHashHi) << 32) | packet.RenderCandidateHashLo;
                    const int packetInstance = static_cast<int>(packet.Instance);
                    const melonDS::u64 key = GameStateKey(packetInstance, packet.Frame);
                    G.RemoteGameStateHashes[key] = hashes;

                    GameStateSample sample;
                    sample.StageID = packet.StageID;
                    sample.StageGroup = packet.StageGroup;
                    sample.VsMode = packet.VsMode;
                    sample.LocalPlayerID = packet.LocalPlayerID;
                    sample.GGID = packet.GGID;
                    sample.NetRandomValue = packet.NetRandomValue;
                    sample.NetRandomCallCount = packet.NetRandomCallCount;
                    sample.NetRandomBranchAddress = packet.NetRandomBranchAddress;
                    sample.VsStarFound = packet.VsStarFound;
                    sample.VsStarGUID = packet.VsStarGUID;
                    sample.VsStarBase = packet.VsStarBase;
                    sample.VsStarSettings = packet.VsStarSettings;
                    sample.VsStarStateType = packet.VsStarStateType;
                    sample.VsStarFlags = packet.VsStarFlags;
                    sample.VsStarPosX = packet.VsStarPosX;
                    sample.VsStarPosY = packet.VsStarPosY;
                    sample.VsStarPosZ = packet.VsStarPosZ;
                    sample.VsStarActorFound = packet.VsStarActorFound;
                    sample.VsStarActorGUID = packet.VsStarActorGUID;
                    sample.VsStarActorBase = packet.VsStarActorBase;
                    sample.VsStarActorSettings = packet.VsStarActorSettings;
                    sample.VsStarActorStateType = packet.VsStarActorStateType;
                    sample.VsStarActorFlags = packet.VsStarActorFlags;
                    sample.VsStarActorPosX = packet.VsStarActorPosX;
                    sample.VsStarActorPosY = packet.VsStarActorPosY;
                    sample.VsStarActorPosZ = packet.VsStarActorPosZ;
                    sample.PlayerActor0Found = packet.PlayerActor0Found;
                    sample.PlayerActor0GUID = packet.PlayerActor0GUID;
                    sample.PlayerActor0Settings = packet.PlayerActor0Settings;
                    sample.PlayerActor0PosX = packet.PlayerActor0PosX;
                    sample.PlayerActor0PosY = packet.PlayerActor0PosY;
                    sample.PlayerActor0PosZ = packet.PlayerActor0PosZ;
                    sample.PlayerActor0PrevX = packet.PlayerActor0PrevX;
                    sample.PlayerActor0PrevY = packet.PlayerActor0PrevY;
                    sample.PlayerActor0PrevZ = packet.PlayerActor0PrevZ;
                    sample.PlayerActor0VelX = packet.PlayerActor0VelX;
                    sample.PlayerActor0VelY = packet.PlayerActor0VelY;
                    sample.PlayerActor0VelZ = packet.PlayerActor0VelZ;
                    sample.PlayerActor1Found = packet.PlayerActor1Found;
                    sample.PlayerActor1GUID = packet.PlayerActor1GUID;
                    sample.PlayerActor1Settings = packet.PlayerActor1Settings;
                    sample.PlayerActor1PosX = packet.PlayerActor1PosX;
                    sample.PlayerActor1PosY = packet.PlayerActor1PosY;
                    sample.PlayerActor1PosZ = packet.PlayerActor1PosZ;
                    sample.PlayerActor1PrevX = packet.PlayerActor1PrevX;
                    sample.PlayerActor1PrevY = packet.PlayerActor1PrevY;
                    sample.PlayerActor1PrevZ = packet.PlayerActor1PrevZ;
                    sample.PlayerActor1VelX = packet.PlayerActor1VelX;
                    sample.PlayerActor1VelY = packet.PlayerActor1VelY;
                    sample.PlayerActor1VelZ = packet.PlayerActor1VelZ;
                    sample.PlayerCount = packet.PlayerCount;
                    sample.Player0BattleStars = packet.Player0BattleStars;
                    sample.Player1BattleStars = packet.Player1BattleStars;
                    sample.Player0Coins = packet.Player0Coins;
                    sample.Player1Coins = packet.Player1Coins;
                    sample.Player0Score = packet.Player0Score;
                    sample.Player1Score = packet.Player1Score;
                    sample.Player0DisplayedStars = packet.Player0DisplayedStars;
                    sample.Player1DisplayedStars = packet.Player1DisplayedStars;
                    sample.Player0Deaths = packet.Player0Deaths;
                    sample.Player1Deaths = packet.Player1Deaths;
                    sample.Player0CollectedStars = packet.Player0CollectedStars;
                    sample.Player1CollectedStars = packet.Player1CollectedStars;
                    sample.VsCoinCount = packet.VsCoinCount;
                    sample.StageCameraFound = packet.StageCameraFound;
                    sample.StageCameraWord190 = packet.StageCameraWord190;
                    sample.StageCameraWord194 = packet.StageCameraWord194;
                    sample.StageCameraWord19C = packet.StageCameraWord19C;
                    sample.StageCameraWord1A0 = packet.StageCameraWord1A0;
                    sample.StageSceneFound = packet.StageSceneFound;
                    sample.StageSceneWord154 = packet.StageSceneWord154;
                    sample.StageSceneWord160 = packet.StageSceneWord160;
                    sample.MovingHazardFound = packet.MovingHazardFound;
                    sample.MovingHazardGUID = packet.MovingHazardGUID;
                    sample.MovingHazardSettings = packet.MovingHazardSettings;
                    sample.MovingHazardStateType = packet.MovingHazardStateType;
                    sample.MovingHazardFlags = packet.MovingHazardFlags;
                    sample.MovingHazardPosX = packet.MovingHazardPosX;
                    sample.MovingHazardPosY = packet.MovingHazardPosY;
                    sample.MovingHazardPosZ = packet.MovingHazardPosZ;
                    sample.MovingHazardVelX = packet.MovingHazardVelX;
                    sample.MovingHazardVelY = packet.MovingHazardVelY;
                    sample.Hash = hashes.Basic;
                    G.RemoteGameStateSamples[key] = sample;
                    CompareGameStateLocked(static_cast<int>(packet.Instance), packet.Frame);
                }
            }
            enet_packet_destroy(event.packet);
            break;

        case ENET_EVENT_TYPE_DISCONNECT:
            std::printf("NSMB PoC: peer disconnected\n");
            if (G.Peer == event.peer) G.Peer = nullptr;
            break;

        default:
            break;
        }
    }

    enet_host_flush(G.Host);
}

void NetworkPumpThreadMain()
{
    for (;;)
    {
        int sleepUs = 250;
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            if (G.NetworkPumpStop)
                break;
            PumpNetworkLocked();
            sleepUs = std::max(50, G.NetworkPumpSleepUs);
        }
        std::this_thread::sleep_for(std::chrono::microseconds(sleepUs));
    }
}

void StartNetworkPumpThreadIfNeeded()
{
    if (!G.NetworkPumpThreadEnabled || G.NetworkPumpThreadStarted)
        return;

    G.NetworkPumpStop = false;
    G.NetworkPumpThreadStarted = true;
    G.NetworkPumpThread = std::thread(NetworkPumpThreadMain);
    std::printf("NSMB PoC: network pump thread started sleepUs=%d inputWaitUs=%d\n",
        G.NetworkPumpSleepUs,
        G.InputWaitPollUs);
    std::fflush(stdout);
}

void StopNetworkPumpThread()
{
    std::thread thread;
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        if (!G.NetworkPumpThreadStarted)
            return;
        G.NetworkPumpStop = true;
        G.InputCond.notify_all();
        thread = std::move(G.NetworkPumpThread);
        G.NetworkPumpThreadStarted = false;
    }

    if (thread.joinable())
        thread.join();
}

void SendMatchSeedLocked()
{
    if (!G.Peer || G.NetRole != Role::Host || !G.MatchSeedConfigured || G.MatchSeedSent)
        return;

    WireSeed packet {};
    packet.Magic = kMagic;
    packet.Version = kVersion;
    packet.Kind = kWireKindSeed;
    packet.Seed = G.MatchSeed;

    ENetPacket* enetPacket = enet_packet_create(&packet, sizeof(packet), ENET_PACKET_FLAG_RELIABLE);
    if (!enetPacket) return;

    enet_peer_send(G.Peer, 0, enetPacket);
    enet_host_flush(G.Host);
    G.MatchSeedSent = true;
    std::printf("NSMB PoC: sent match seed 0x%08X\n", G.MatchSeed);
}

void SendInputPayloadNowLocked(const void* data, size_t size, melonDS::u32 flags)
{
    if (!G.Peer) return;

    ENetPacket* enetPacket = enet_packet_create(data, size, flags);
    if (!enetPacket) return;

    enet_peer_send(G.Peer, 0, enetPacket);
    enet_host_flush(G.Host);
}

void SendInputWireNowLocked(const WireInput& packet)
{
    SendInputPayloadNowLocked(&packet, sizeof(packet), ENET_PACKET_FLAG_RELIABLE);
}

void FlushDelayedInputsLocked(melonDS::u32 frame)
{
    if (!G.Peer || G.DelayedInputs.empty())
        return;

    for (auto it = G.DelayedInputs.begin(); it != G.DelayedInputs.end(); )
    {
        if (it->ReleaseFrame <= frame || std::chrono::steady_clock::now() >= it->ReleaseTime)
        {
            SendInputPayloadNowLocked(it->Payload.data(), it->Payload.size(), it->Flags);
            it = G.DelayedInputs.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

std::vector<char> BuildInputBundlePayloadLocked(melonDS::u32 frame, const InputState& input)
{
    const int history = std::clamp(G.InputBundleHistory, 0, 31);
    std::vector<WireInputBundleEntry> entries;
    entries.reserve(static_cast<size_t>(history + 1));
    for (int offset = history; offset >= 0; offset--)
    {
        if (static_cast<melonDS::u32>(offset) > frame)
            continue;
        const melonDS::u32 entryFrame = frame - static_cast<melonDS::u32>(offset);
        InputState entryInput = input;
        auto existing = G.LocalInputs.find(entryFrame);
        if (existing != G.LocalInputs.end())
            entryInput = existing->second;
        entries.push_back({
            entryFrame,
            entryInput.KeyMask,
            entryInput.TouchX,
            entryInput.TouchY,
            entryInput.Touching ? static_cast<melonDS::u8>(1) : static_cast<melonDS::u8>(0),
            {},
        });
    }

    WireInputBundleHeader header {};
    header.Magic = kMagic;
    header.Version = kVersion;
    header.Kind = kWireKindInputBundle;
    header.Count = static_cast<melonDS::u32>(entries.size());

    std::vector<char> payload(sizeof(header) + entries.size() * sizeof(WireInputBundleEntry));
    std::memcpy(payload.data(), &header, sizeof(header));
    if (!entries.empty())
        std::memcpy(payload.data() + sizeof(header), entries.data(), entries.size() * sizeof(WireInputBundleEntry));
    return payload;
}

void SendInputLocked(melonDS::u32 frame, const InputState& input)
{
    if (!G.Peer) return;

    SendMatchSeedLocked();

    if (G.InputDropModulo > 0
        && (frame % static_cast<melonDS::u32>(G.InputDropModulo))
            == static_cast<melonDS::u32>(G.InputDropOffset))
    {
        if (G.InputNetplayTraceEnabled)
            std::printf("NSMB InputNetplay: dropped local input packet frame=%u modulo=%d offset=%d\n",
                frame,
                G.InputDropModulo,
                G.InputDropOffset);
        return;
    }

    WireInput packet {};
    packet.Magic = kMagic;
    packet.Version = kVersion;
    packet.Frame = frame;
    packet.KeyMask = input.KeyMask;
    packet.TouchX = input.TouchX;
    packet.TouchY = input.TouchY;
    packet.Touching = input.Touching ? 1 : 0;

    const bool sendBundle = G.InputUnreliable && G.InputBundleHistory > 0;
    const std::vector<char> bundlePayload = sendBundle
        ? BuildInputBundlePayloadLocked(frame, input)
        : std::vector<char> {};
    const melonDS::u32 sendFlags = sendBundle ? ENET_PACKET_FLAG_UNSEQUENCED : ENET_PACKET_FLAG_RELIABLE;

    const int jitterFrames = G.InputSendJitterFrames > 0
        ? static_cast<int>(frame % static_cast<melonDS::u32>(G.InputSendJitterFrames + 1))
        : 0;
    const int sendDelayFrames = G.InputSendDelayFrames + jitterFrames;
    if (sendDelayFrames > 0)
    {
        G.DelayedInputs.push_back({
            frame + static_cast<melonDS::u32>(sendDelayFrames),
            std::chrono::steady_clock::now() + std::chrono::milliseconds(
                (sendDelayFrames * 1000 + 59) / 60),
            sendBundle
                ? bundlePayload
                : std::vector<char>(reinterpret_cast<const char*>(&packet),
                    reinterpret_cast<const char*>(&packet) + sizeof(packet)),
            sendFlags,
        });
    }
    else
    {
        if (sendBundle)
            SendInputPayloadNowLocked(bundlePayload.data(), bundlePayload.size(), sendFlags);
        else
            SendInputWireNowLocked(packet);
    }

    if (G.InputTraceEnabled
        && frame != G.LastTracedSentInputFrame
        && (G.InputTraceInterval <= 1 || (frame % static_cast<melonDS::u32>(G.InputTraceInterval)) == 0))
    {
        G.LastTracedSentInputFrame = frame;
        std::printf("NSMB PoC: sent input frame=%u keys=0x%03X localQueue=%zu\n",
            frame,
            input.KeyMask,
            G.LocalInputs.size());
    }
}

bool GetRollbackRemoteInputLocked(melonDS::u32 frame, InputState& input, bool& predicted)
{
    auto confirmed = G.RemoteInputs.find(frame);
    if (confirmed != G.RemoteInputs.end())
    {
        input = confirmed->second;
        predicted = false;
        return true;
    }

    auto existingPrediction = G.PredictedRemoteInputs.find(frame);
    if (existingPrediction != G.PredictedRemoteInputs.end())
    {
        input = existingPrediction->second;
        predicted = true;
        return true;
    }

    auto previousPrediction = frame > 0 ? G.PredictedRemoteInputs.find(frame - 1) : G.PredictedRemoteInputs.end();
    if (previousPrediction != G.PredictedRemoteInputs.end())
        input = previousPrediction->second;
    else if (G.LastConfirmedRemoteInputValid)
        input = G.LastConfirmedRemoteInput;
    else
        input = NeutralInput();

    G.PredictedRemoteInputs.emplace(frame, input);
    G.RollbackPredictionCount++;
    predicted = true;
    return true;
}

void PruneRollbackHistoryLocked(melonDS::u32 frame)
{
    const melonDS::u32 keepFrom = frame > static_cast<melonDS::u32>(G.RollbackWindow)
        ? frame - static_cast<melonDS::u32>(G.RollbackWindow)
        : 0;

    for (auto it = G.RollbackStates.begin(); it != G.RollbackStates.end(); )
    {
        if (it->first < keepFrom)
            it = G.RollbackStates.erase(it);
        else
            ++it;
    }
    for (auto it = G.PredictedRemoteInputs.begin(); it != G.PredictedRemoteInputs.end(); )
    {
        if (it->first < keepFrom && G.RemoteInputs.find(it->first) != G.RemoteInputs.end())
            it = G.PredictedRemoteInputs.erase(it);
        else
            ++it;
    }
}

bool ShouldSaveRollbackCheckpoint(melonDS::u32 frame)
{
    if (G.RollbackCheckpointInterval <= 1)
        return true;
    if (G.NetplayStartFrame != 0 && frame == G.NetplayStartFrame)
        return true;
    return (frame % static_cast<melonDS::u32>(G.RollbackCheckpointInterval)) == 0;
}

void InvalidateMainRAMJIT(melonDS::NDS* nds, melonDS::u32 len)
{
    if (!nds || len == 0)
        return;
    for (melonDS::u32 offset = 0; offset < len; offset += 0x1000)
    {
        const melonDS::u32 addr = kMainRAMBase + offset;
        nds->JIT.CheckAndInvalidate<0, melonDS::ARMJIT_Memory::memregion_MainRAM>(addr);
        nds->JIT.CheckAndInvalidate<1, melonDS::ARMJIT_Memory::memregion_MainRAM>(addr);
    }
}

bool SaveRollbackCheckpointBuffer(melonDS::NDS* nds, std::vector<char>& buffer)
{
    if (!nds)
        return false;

    if (G.RollbackBackendMode == RollbackBackend::ARM9RAM)
    {
        if (!nds->MainRAM)
            return false;
        const melonDS::u32 len = std::min<melonDS::u32>(nds->MainRAMMask + 1, 0x400000);
        RollbackARM9RAMHeader header = {};
        header.Magic = kRollbackARM9RAMMagic;
        header.Version = kRollbackARM9RAMVersion;
        header.RamSize = len;
        header.NumFrames = nds->NumFrames;
        header.NumLagFrames = nds->NumLagFrames;
        header.LagFrameFlag = nds->LagFrameFlag ? 1 : 0;
        buffer.resize(sizeof(header) + len);
        std::memcpy(buffer.data(), &header, sizeof(header));
        std::memcpy(buffer.data() + sizeof(header), nds->MainRAM, len);
        return true;
    }

    melonDS::Savestate state;
    if (state.Error || !nds->DoSavestate(&state) || state.Error)
        return false;
    buffer.assign(static_cast<const char*>(state.Buffer()),
        static_cast<const char*>(state.Buffer()) + state.Length());
    return true;
}

bool RestoreRollbackCheckpointBuffer(melonDS::NDS* nds, const std::vector<char>& buffer)
{
    if (!nds)
        return false;

    if (G.RollbackBackendMode == RollbackBackend::ARM9RAM)
    {
        if (!nds->MainRAM || buffer.empty())
            return false;
        const melonDS::u32 len = std::min<melonDS::u32>(nds->MainRAMMask + 1, 0x400000);
        if (buffer.size() != sizeof(RollbackARM9RAMHeader) + len)
            return false;
        RollbackARM9RAMHeader header = {};
        std::memcpy(&header, buffer.data(), sizeof(header));
        if (header.Magic != kRollbackARM9RAMMagic ||
            header.Version != kRollbackARM9RAMVersion ||
            header.RamSize != len)
            return false;
        std::memcpy(nds->MainRAM, buffer.data() + sizeof(header), len);
        nds->NumFrames = header.NumFrames;
        nds->NumLagFrames = header.NumLagFrames;
        nds->LagFrameFlag = header.LagFrameFlag != 0;
        InvalidateMainRAMJIT(nds, len);
        return true;
    }

    melonDS::Savestate state(const_cast<char*>(buffer.data()), static_cast<melonDS::u32>(buffer.size()), false);
    return !state.Error && nds->DoSavestate(&state) && !state.Error;
}

void SaveRollbackCheckpointIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.RollbackEnabled || !G.InputNetplayOnly || !nds)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (G.NetplayStartFrame != 0 && frame < G.NetplayStartFrame)
        return;
    if (G.RollbackWindow <= 0)
        return;
    if (!ShouldSaveRollbackCheckpoint(frame))
        return;

    std::vector<char> buffer;
    if (!SaveRollbackCheckpointBuffer(nds, buffer))
    {
        if (G.InputNetplayTraceEnabled)
            std::printf("NSMB Rollback: failed to save checkpoint inst=%d frame=%u\n", instanceID, frame);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        G.RollbackStates[frame] = std::move(buffer);
        G.RollbackCheckpointSaveCount++;
        PruneRollbackHistoryLocked(frame);
    }
}

void SaveRollbackCheckpointNowLocked(melonDS::u32 frame, melonDS::NDS* nds, bool force = false)
{
    if (!nds || G.RollbackWindow <= 0)
        return;
    if (!force && !ShouldSaveRollbackCheckpoint(frame))
        return;

    std::vector<char> buffer;
    if (!SaveRollbackCheckpointBuffer(nds, buffer))
        return;

    G.RollbackStates[frame] = std::move(buffer);
    G.RollbackCheckpointSaveCount++;
    PruneRollbackHistoryLocked(frame);
}

bool RestoreRollbackCheckpointForProbeIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.RollbackEnabled || !G.RollbackRestoreProbe || !G.InputNetplayOnly || !nds)
        return false;
    if (instanceID < 0 || instanceID >= 16)
        return false;

    melonDS::u32 restoreFrame = kNoFrameLimit;
    std::vector<char> buffer;
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        if (G.PendingRollbackFrame == kNoFrameLimit)
            return false;

        restoreFrame = G.PendingRollbackFrame;
        auto state = G.RollbackStates.find(restoreFrame);
        if (state == G.RollbackStates.end())
        {
            std::printf(
                "NSMB Rollback: cannot restore frame=%u at current=%u, checkpoint missing window=%d\n",
                restoreFrame,
                frame,
                G.RollbackWindow);
            G.PendingRollbackFrame = kNoFrameLimit;
            return false;
        }
        buffer = state->second;
        G.PendingRollbackFrame = kNoFrameLimit;
    }

    if (!RestoreRollbackCheckpointBuffer(nds, buffer))
    {
        std::printf("NSMB Rollback: restore probe failed inst=%d restoreFrame=%u current=%u\n",
            instanceID,
            restoreFrame,
            frame);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        G.RollbackRestoreCount++;
    }
    std::printf("NSMB Rollback: restore probe loaded frame=%u at current=%u bytes=%zu\n",
        restoreFrame,
        frame,
        buffer.size());
    std::fflush(stdout);
    return true;
}

melonDS::u32 LocalPlayerID(melonDS::NDS* nds)
{
    if (!nds)
        return 0;

    if (G.PacketBridgeEnabled)
    {
        static int packetBridgeLocalPlayer = -1;
        if (packetBridgeLocalPlayer < 0)
        {
            if (const char* value = std::getenv("MELONDS_NSML_PACKET_BRIDGE_LOCAL_PLAYER"))
                packetBridgeLocalPlayer = std::clamp(std::atoi(value), 0, 1);
            else
                packetBridgeLocalPlayer = 2;
        }
        if (packetBridgeLocalPlayer <= 1)
            return static_cast<melonDS::u32>(packetBridgeLocalPlayer);
    }

    const bool inGameplay = IsMarioVsLuigiGameplay(nds);
    if (G.PacketBridgeEnabled && G.PacketBridgeAllowPreGame && !inGameplay)
        return static_cast<melonDS::u32>(G.LocalInstance & 1);

    return nds->ARM9Read32(kGameLocalPlayerIDAddr) & 1;
}

void ApplyPendingNSMLPacketsLocked(melonDS::NDS* nds)
{
    if (!G.PacketBridgeEnabled || !nds || G.PendingNSMLPackets.empty())
        return;

    for (const WireNSMLPacket& packet : G.PendingNSMLPackets)
        melonDS::NSML_PushMarioVsLuigiRemotePacket(nds, packet.Player, packet.Packet);
    G.PendingNSMLPackets.clear();
}

void SendNSMLWirePacketNowLocked(const WireNSMLPacket& packet)
{
    ENetPacket* enetPacket = enet_packet_create(&packet, sizeof(packet), ENET_PACKET_FLAG_RELIABLE);
    if (!enetPacket)
        return;

    enet_peer_send(G.Peer, 0, enetPacket);
    enet_host_flush(G.Host);
}

void FlushDelayedNSMLPacketsLocked(melonDS::u32 frame)
{
    if (!G.PacketBridgeEnabled || !G.Peer || G.DelayedNSMLPackets.empty())
        return;

    for (auto it = G.DelayedNSMLPackets.begin(); it != G.DelayedNSMLPackets.end(); )
    {
        if (it->ReleaseFrame <= frame || std::chrono::steady_clock::now() >= it->ReleaseTime)
        {
            SendNSMLWirePacketNowLocked(it->Packet);
            it = G.DelayedNSMLPackets.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void SendNSMLPacketLocked(melonDS::u32 frame, melonDS::u32 player, melonDS::u32 tick, const melonDS::u8 packetBytes[52])
{
    if (!G.PacketBridgeEnabled || !G.Peer || !packetBytes || player > 1)
        return;

    SendMatchSeedLocked();

    WireNSMLPacket packet {};
    packet.Magic = kMagic;
    packet.Version = kVersion;
    packet.Kind = kWireKindPacket;
    packet.Frame = frame;
    packet.Player = player;
    packet.Tick = tick;
    std::memcpy(packet.Packet, packetBytes, sizeof(packet.Packet));

    const int jitterFrames = G.PacketBridgeSendJitterFrames > 0
        ? static_cast<int>(frame % static_cast<melonDS::u32>(G.PacketBridgeSendJitterFrames + 1))
        : 0;
    const int sendDelayFrames = G.PacketBridgeSendDelayFrames + jitterFrames;
    if (sendDelayFrames > 0)
    {
        G.DelayedNSMLPackets.push_back({
            frame + static_cast<melonDS::u32>(sendDelayFrames),
            std::chrono::steady_clock::now() + std::chrono::milliseconds(
                (sendDelayFrames * 1000 + 59) / 60),
            packet,
        });
    }
    else
    {
        SendNSMLWirePacketNowLocked(packet);
    }

    if (G.PacketBridgeTraceEnabled && tick != G.LastSentNSMLPacketTick)
    {
        G.LastSentNSMLPacketTick = tick;
        const melonDS::u32 keys = packetBytes[2] | (packetBytes[3] << 8);
        std::printf("NSMB PacketBridge: send player=%u tick=0x%04X keys=0x%04X action=0x%02X b5=0x%02X b6=0x%02X b7=0x%02X bit=0x%02X frame=%u\n",
            player,
            tick,
            keys,
            packetBytes[4],
            packetBytes[5],
            packetBytes[6],
            packetBytes[7],
            packetBytes[0x29],
            frame);
    }
}

melonDS::u32 PacketBridgeCanonicalTick(melonDS::NDS* nds, melonDS::u32 frame);

void CaptureAndSendNSMLPacketLocked(melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.PacketBridgeEnabled || !nds)
        return;

    melonDS::u8 packet[52] {};
    melonDS::u32 tick = 0;
    melonDS::u32 keys = 0;
    bool captured = melonDS::NSML_TakeMarioVsLuigiLocalPacket(nds, packet, &tick, &keys);
    if (!captured && G.PacketBridgeDirectCaptureEnabled)
        captured = melonDS::NSML_BuildMarioVsLuigiLocalPacket(nds, packet, &tick, &keys);
    if (!captured)
        return;

    auto overrideInput = G.PacketBridgePacketInputs.find(frame);
    if (overrideInput == G.PacketBridgePacketInputs.end() && frame > 0)
        overrideInput = G.PacketBridgePacketInputs.find(frame - 1);
    if (overrideInput != G.PacketBridgePacketInputs.end())
    {
        keys = (~overrideInput->second.KeyMask) & 0x0FFF;
        packet[2] = static_cast<melonDS::u8>(keys & 0xFF);
        packet[3] = static_cast<melonDS::u8>((keys >> 8) & 0xFF);
    }

    if (G.PacketBridgeForceTickEnabled
        && frame >= G.PacketBridgeForceTickStartFrame
        && G.PacketBridgeForceTickBase >= 0)
    {
        tick = PacketBridgeCanonicalTick(nds, frame);
        packet[0] = static_cast<melonDS::u8>(tick & 0xFF);
        packet[1] = static_cast<melonDS::u8>((tick >> 8) & 0xFF);
    }

    (void)keys;
    const melonDS::u32 localPlayer = LocalPlayerID(nds);
    melonDS::NSML_PushMarioVsLuigiRemotePacket(nds, localPlayer, packet);
    SendNSMLPacketLocked(frame, localPlayer, tick, packet);

    if (!G.PacketBridgePacketInputs.empty())
    {
        const melonDS::u32 keepFrom = frame > 240 ? frame - 240 : 0;
        for (auto it = G.PacketBridgePacketInputs.begin(); it != G.PacketBridgePacketInputs.end(); )
        {
            if (it->first < keepFrom)
                it = G.PacketBridgePacketInputs.erase(it);
            else
                ++it;
        }
    }
}

melonDS::u32 PacketBridgeCanonicalTick(melonDS::NDS* nds, melonDS::u32 frame)
{
    if (G.PacketBridgeForceTickEnabled
        && frame >= G.PacketBridgeForceTickStartFrame
        && G.PacketBridgeForceTickBase >= 0)
    {
        return (static_cast<melonDS::u32>(G.PacketBridgeForceTickBase)
            + (frame - G.PacketBridgeForceTickStartFrame)) & 0xFFFF;
    }

    return nds ? nds->ARM9Read16(0x020888E0) : 0;
}

void ForceNSMLPacketBridgeTickIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.PacketBridgeForceTickEnabled || !nds || instanceID < 0 || instanceID >= 16)
        return;
    if (frame < G.PacketBridgeForceTickStartFrame)
        return;
    if (G.LastPacketBridgeForcedTickFrame[instanceID] == frame)
        return;

    const melonDS::u32 tick = PacketBridgeCanonicalTick(nds, frame);
    nds->ARM9Write16(0x020888E0, static_cast<melonDS::u16>(tick));
    G.LastPacketBridgeForcedTickFrame[instanceID] = frame;

    if (G.PacketBridgeTraceEnabled && (frame % 60) == 0)
    {
        std::printf("NSMB PacketBridge: force tick=0x%04X frame=%u\n", tick, frame);
        std::fflush(stdout);
    }
}

void ForceNSMLPacketBridgeNetReadyIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.PacketBridgeForceNetReady || !nds || instanceID < 0 || instanceID >= 16)
        return;
    if (frame < G.PacketBridgeForceNetReadyStartFrame)
        return;
    if (G.PacketBridgeForceNetReadyEndFrame != 0 && frame > G.PacketBridgeForceNetReadyEndFrame)
        return;
    if (G.PacketBridgeForceNetReadyHostOnly && G.NetRole != Role::Host)
        return;
    if (G.PacketBridgeForceNetReadyClientOnly && G.NetRole != Role::Client)
        return;
    if (!IsMarioVsLuigiGGID(nds->ARM9Read32(kNetGGIDAddr)))
        return;

    nds->ARM9Write32(kNetState14Addr, 0x00000001);
    nds->ARM9Write32(kNetState1CAddr, 0x00000006);
    nds->ARM9Write32(kNetState20Addr, 0x00000002);
    nds->ARM9Write32(kNetState24Addr, 0x00000002);
    nds->ARM9Write32(kNetState5CAddr, 0x00000000);
    const bool inMvlGameplay = nds->ARM9Read32(kGameStageGroupAddr) == 9
        && nds->ARM9Read32(kGameVsModeAddr) == 1;
    nds->ARM9Write32(kGameLocalPlayerIDAddr, (inMvlGameplay && G.NetRole == Role::Client) ? 1 : 0);
    nds->ARM9Write32(kGameVsModeAddr, 0x00000001);
    nds->ARM9Write32(0x020887F4, 0x00000001);
    if (G.PacketBridgeForceNetReadyState10
        && (!G.PacketBridgeForceNetReadyState10ClientOnly || G.NetRole == Role::Client))
    {
        nds->ARM9Write32(0x020887F8, 0x00000001);
    }
    nds->ARM9Write32(0x02088878, 0x023DF000);
    nds->ARM9Write32(0x020888C0, 0x02088988);
    nds->ARM9Write32(0x020888C4, 0x02088988);
    nds->ARM9Write32(0x02088910, 0x00000100);
    nds->ARM9Write32(0x02088A00, 0x020888C0);
    nds->ARM9Write32(0x02088A4C, 0x00000001);
    nds->ARM9Write32(0x02088A58, 0x00000101);
    nds->ARM9Write32(0x02088A5C, 0x00000202);
    nds->ARM9Write32(0x02088A64, 0x00000202);
    nds->ARM9Write32(0x02088A84, 0x00000003);
    nds->ARM9Write32(0x02088A88, 0x00000003);

    if (G.PacketBridgeTraceEnabled && (frame % 60) == 0)
    {
        std::printf("NSMB PacketBridge: force net ready inst=%d frame=%u\n", instanceID, frame);
        std::fflush(stdout);
    }
}

bool InjectNSMLPacketBridgeMvlFileCache(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.PacketBridgeForceMvlFileCache || !nds || instanceID < 0 || instanceID >= 16)
        return false;
    if (frame < G.PacketBridgeForceMvlFileCacheStartFrame)
        return false;
    if (G.PacketBridgeForceMvlFileCacheApplied[instanceID])
        return false;
    if (frame < G.PacketBridgeForceLoadGameSMStartFrame)
        return false;
    if (!IsMarioVsLuigiGGID(nds->ARM9Read32(kNetGGIDAddr)))
        return false;
    if (FindObjectBaseByID(nds, kVsConnectObjectID) == 0)
        return false;

    struct FileSpec
    {
        melonDS::u32 FileID;
        bool Compressed;
    };

    static constexpr FileSpec kMvlFiles[] =
    {
        {0x07A6, false},
        {0x0615, true},
        {0x05A2, false},
        {0x05A3, false},
        {0x0529, false},
        {0x06DE, true},
        {0x06CF, true},
        {0x06D1, true},
        {0x06D0, true},
        {0x06D3, true},
        {0x06D2, true},
        {0x06D5, true},
        {0x06D4, true},
        {0x06D7, true},
        {0x06D9, true},
        {0x06D8, true},
        {0x06DB, true},
        {0x06DA, true},
        {0x06DD, true},
        {0x06E5, true},
        {0x06E2, true},
        {0x06D6, true},
        {0x06DC, true},
        {0x06E1, true},
        {0x06E3, true},
        {0x0694, true},
        {0x0697, true},
        {0x06A2, true},
        {0x06A1, true},
        {0x069F, true},
        {0x06A0, true},
        {0x0117, true},
        {0x0116, true},
        {0x0113, true},
        {0x0115, true},
        {0xC92B, false},
        {0x0798, false},
    };

    const melonDS::u32 oldPC = nds->ARM9.R[15] - ((nds->ARM9.CPSR & 0x20) ? 2 : 4);
    const melonDS::u32 returnPC = oldPC | ((nds->ARM9.CPSR & 0x20) ? 1u : 0u);
    const bool isKnownITCMCode = oldPC >= 0x01FF8000u && oldPC < nds->ARM9.ITCMSize;
    if (!IsARM9MainRAMAddress(oldPC) && !isKnownITCMCode)
        return false;
    if ((nds->ARM9.CPSR & 0x1Fu) != 0x1Fu || nds->ARM9.R[13] < 0x027E3000u)
        return false;

    std::vector<melonDS::u32> code;
    code.reserve(16 + (std::size(kMvlFiles) * 5));
    const auto emitBL = [&code](melonDS::u32 target)
    {
        const melonDS::u32 pc = kDirectBootTrampolineAddr + static_cast<melonDS::u32>(code.size() * sizeof(melonDS::u32)) + 8u;
        const melonDS::s32 offset = static_cast<melonDS::s32>(target - pc) >> 2;
        code.push_back(0xEB000000u | (static_cast<melonDS::u32>(offset) & 0x00FFFFFFu));
    };

    code.push_back(0xE92D5FFFu); // push {r0-r12, lr}
    code.push_back(0xE10F5000u); // mrs r5, cpsr
    code.push_back(0xE92D0020u); // push {r5}
    for (const FileSpec& file : kMvlFiles)
    {
        code.push_back(0xE59F0008u); // ldr r0, [pc, #8]
        code.push_back(file.Compressed ? 0xE3A01001u : 0xE3A01000u); // mov r1, #compressed
        emitBL(kA2DJFSCacheLoadFileAddr);
        code.push_back(0xEA000000u); // b over literal
        code.push_back(file.FileID);
    }
    code.push_back(0xE8BD0020u); // pop {r5}
    code.push_back(0xE128F005u); // msr apsr_nzcvq, r5
    code.push_back(0xE8BD5FFFu); // pop {r0-r12, lr}
    code.push_back(0xE59FC004u); // ldr ip, [pc, #4]
    code.push_back(0xE12FFF1Cu); // bx ip
    code.push_back(0xE1A00000u); // nop
    code.push_back(returnPC);

    bool wrote = true;
    for (size_t i = 0; i < code.size(); i++)
        wrote = WriteARM9U32(nds, kDirectBootTrampolineAddr + static_cast<melonDS::u32>(i * sizeof(melonDS::u32)), code[i]) && wrote;
    if (!wrote)
        return false;

    G.PacketBridgeForceMvlFileCacheApplied[instanceID] = true;
    std::printf("NSMB PacketBridge: force MvL file cache inst=%d frame=%u files=%zu\n",
        instanceID,
        frame,
        std::size(kMvlFiles));
    std::fflush(stdout);
    nds->ARM9.JumpTo(kDirectBootTrampolineAddr);
    return true;
}

bool InjectNSMLPacketBridgeMvlLoadThread(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.PacketBridgeForceMvlLoadThread || !nds || instanceID < 0 || instanceID >= 16)
        return false;
    if (frame < G.PacketBridgeForceMvlLoadThreadStartFrame)
        return false;
    if (G.PacketBridgeForceMvlLoadThreadApplied[instanceID])
        return false;
    if (FindObjectBaseByID(nds, kVsConnectObjectID) == 0)
        return false;

    const melonDS::u32 oldPC = nds->ARM9.R[15] - ((nds->ARM9.CPSR & 0x20) ? 2 : 4);
    const melonDS::u32 returnPC = oldPC | ((nds->ARM9.CPSR & 0x20) ? 1u : 0u);
    std::vector<melonDS::u32> code;
    code.reserve(32);
    const auto emitLoadImm = [&code](int reg, melonDS::u32 value)
    {
        if (value <= 0xFF)
        {
            code.push_back(0xE3A00000u | (static_cast<melonDS::u32>(reg & 0xF) << 12) | value);
            return;
        }
        code.push_back(0xE59F0000u | (static_cast<melonDS::u32>(reg & 0xF) << 12)); // ldr reg, [pc]
        code.push_back(0xEA000000u); // skip literal
        code.push_back(value);
    };
    const auto emitBL = [&code](melonDS::u32 target)
    {
        const melonDS::u32 pc = kDirectBootTrampolineAddr + static_cast<melonDS::u32>(code.size() * sizeof(melonDS::u32)) + 8u;
        const melonDS::s32 offset = static_cast<melonDS::s32>(target - pc) >> 2;
        code.push_back(0xEB000000u | (static_cast<melonDS::u32>(offset) & 0x00FFFFFFu));
    };

    code.push_back(0xE92D5FFFu); // push {r0-r12, lr}
    code.push_back(0xE10F5000u); // mrs r5, cpsr
    code.push_back(0xE92D0020u); // push {r5}
    code.push_back(0xE24DD004u); // sub sp, sp, #4
    code.push_back(0xE3A00001u); // mov r0, #1
    code.push_back(0xE1A00600u); // lsl r0, r0, #12
    code.push_back(0xE58D0000u); // str r0, [sp]
    emitLoadImm(0, kA2DJLoadMvsLFilesThreadAddr);
    code.push_back(0xE3A01000u); // mov r1, #0
    code.push_back(0xE3A02014u); // mov r2, #0x14
    code.push_back(0xE3A03000u); // mov r3, #0
    emitBL(kA2DJCreateThreadAddr);
    code.push_back(0xE28DD004u); // add sp, sp, #4
    code.push_back(0xE8BD0020u); // pop {r5}
    code.push_back(0xE128F005u); // msr apsr_nzcvq, r5
    code.push_back(0xE8BD5FFFu); // pop {r0-r12, lr}
    code.push_back(0xE59FC004u); // ldr ip, [pc, #4]
    code.push_back(0xE12FFF1Cu); // bx ip
    code.push_back(0xE1A00000u); // nop
    code.push_back(returnPC);

    bool wrote = true;
    for (size_t i = 0; i < code.size(); i++)
        wrote = WriteARM9U32(nds, kDirectBootTrampolineAddr + static_cast<melonDS::u32>(i * sizeof(melonDS::u32)), code[i]) && wrote;
    if (!wrote)
        return false;

    G.PacketBridgeForceMvlLoadThreadApplied[instanceID] = true;
    std::printf("NSMB PacketBridge: start early MvL file load thread inst=%d frame=%u\n",
        instanceID,
        frame);
    std::fflush(stdout);
    nds->ARM9.JumpTo(kDirectBootTrampolineAddr);
    return true;
}

bool InjectNSMLPacketBridgeScheduleSubMenu(
    int instanceID,
    melonDS::u32 frame,
    melonDS::NDS* nds,
    melonDS::u32 vsConnectBase,
    melonDS::u32 subMenu,
    melonDS::u32 delay,
    melonDS::u32 create,
    const char* label)
{
    if (!nds || instanceID < 0 || instanceID >= 16 || vsConnectBase == 0 || subMenu == 0)
        return false;

    const melonDS::u32 oldPC = nds->ARM9.R[15] - ((nds->ARM9.CPSR & 0x20) ? 2 : 4);
    const melonDS::u32 returnPC = oldPC | ((nds->ARM9.CPSR & 0x20) ? 1u : 0u);
    std::vector<melonDS::u32> code;
    code.reserve(20);
    const auto emitBL = [&code](melonDS::u32 target)
    {
        const melonDS::u32 pc = kDirectBootTrampolineAddr + static_cast<melonDS::u32>(code.size() * sizeof(melonDS::u32)) + 8u;
        const melonDS::s32 offset = static_cast<melonDS::s32>(target - pc) >> 2;
        code.push_back(0xEB000000u | (static_cast<melonDS::u32>(offset) & 0x00FFFFFFu));
    };

    code.push_back(0xE92D5FFFu); // push {r0-r12, lr}
    code.push_back(0xE10F5000u); // mrs r5, cpsr
    code.push_back(0xE92D0020u); // push {r5}
    code.push_back(0xE59F0028u); // ldr r0, [pc, #40]
    code.push_back(0xE59F1028u); // ldr r1, [pc, #40]
    code.push_back(0xE3A02000u | (delay & 0xFFu)); // mov r2, #delay
    code.push_back(0xE3A03000u | (create & 0xFFu)); // mov r3, #create
    emitBL(kA2DJVSConnectScheduleSubMenuChangeAddr);
    code.push_back(0xE8BD0020u); // pop {r5}
    code.push_back(0xE128F005u); // msr apsr_nzcvq, r5
    code.push_back(0xE8BD5FFFu); // pop {r0-r12, lr}
    code.push_back(0xE59FC004u); // ldr ip, [pc, #4]
    code.push_back(0xE12FFF1Cu); // bx ip
    code.push_back(0xE1A00000u); // nop
    code.push_back(returnPC);
    code.push_back(vsConnectBase);
    code.push_back(subMenu);

    bool wrote = true;
    for (size_t i = 0; i < code.size(); i++)
        wrote = WriteARM9U32(nds, kDirectBootTrampolineAddr + static_cast<melonDS::u32>(i * sizeof(melonDS::u32)), code[i]) && wrote;
    if (!wrote)
        return false;

    std::printf("NSMB PacketBridge: schedule %s inst=%d frame=%u vsConnect=%08X submenu=%08X delay=0x%X create=%u\n",
        label ? label : "submenu",
        instanceID,
        frame,
        vsConnectBase,
        subMenu,
        delay,
        create);
    std::fflush(stdout);
    nds->ARM9.JumpTo(kDirectBootTrampolineAddr);
    return true;
}

bool InjectNSMLPacketBridgeDirectSubMenuChange(
    int instanceID,
    melonDS::u32 frame,
    melonDS::NDS* nds,
    melonDS::u32 vsConnectBase,
    melonDS::u32 subMenu,
    bool callCreate)
{
    if (!nds || instanceID < 0 || instanceID >= 16 || vsConnectBase == 0 || subMenu == 0)
        return false;

    const melonDS::u32 createAddr = nds->ARM9Read32(subMenu + 0x00);
    const melonDS::u32 updateAddr = nds->ARM9Read32(subMenu + 0x08);
    const melonDS::u32 renderAddr = nds->ARM9Read32(subMenu + 0x10);
    if ((createAddr & 0xFF000000u) != 0x02000000u ||
        (updateAddr & 0xFF000000u) != 0x02000000u ||
        (renderAddr & 0xFF000000u) != 0x02000000u)
    {
        std::printf("NSMB PacketBridge: direct submenu rejected inst=%d frame=%u submenu=%08X create=%08X update=%08X render=%08X\n",
            instanceID,
            frame,
            subMenu,
            createAddr,
            updateAddr,
            renderAddr);
        std::fflush(stdout);
        return false;
    }

    WriteARM9U32(nds, vsConnectBase + 0x118, createAddr);
    WriteARM9U32(nds, vsConnectBase + 0x120, updateAddr);
    WriteARM9U32(nds, vsConnectBase + 0x128, renderAddr);
    WriteARM9U32(nds, vsConnectBase + 0x134, subMenu);

    std::printf("NSMB PacketBridge: direct submenu inst=%d frame=%u vsConnect=%08X submenu=%08X create=%08X update=%08X render=%08X callCreate=%d\n",
        instanceID,
        frame,
        vsConnectBase,
        subMenu,
        createAddr,
        updateAddr,
        renderAddr,
        callCreate ? 1 : 0);
    std::fflush(stdout);

    if (!callCreate)
        return true;

    const melonDS::u32 oldPC = nds->ARM9.R[15] - ((nds->ARM9.CPSR & 0x20) ? 2 : 4);
    const melonDS::u32 returnPC = oldPC | ((nds->ARM9.CPSR & 0x20) ? 1u : 0u);
    std::vector<melonDS::u32> code;
    code.reserve(16);
    const auto emitBL = [&code](melonDS::u32 target)
    {
        const melonDS::u32 pc = kDirectBootTrampolineAddr + static_cast<melonDS::u32>(code.size() * sizeof(melonDS::u32)) + 8u;
        const melonDS::s32 offset = static_cast<melonDS::s32>(target - pc) >> 2;
        code.push_back(0xEB000000u | (static_cast<melonDS::u32>(offset) & 0x00FFFFFFu));
    };

    code.push_back(0xE92D5FFFu); // push {r0-r12, lr}
    code.push_back(0xE10F5000u); // mrs r5, cpsr
    code.push_back(0xE92D0020u); // push {r5}
    code.push_back(0xE59F001Cu); // ldr r0, [pc, #28]
    emitBL(createAddr);
    code.push_back(0xE8BD0020u); // pop {r5}
    code.push_back(0xE128F005u); // msr apsr_nzcvq, r5
    code.push_back(0xE8BD5FFFu); // pop {r0-r12, lr}
    code.push_back(0xE59FC004u); // ldr ip, [pc, #4]
    code.push_back(0xE12FFF1Cu); // bx ip
    code.push_back(0xE1A00000u); // nop
    code.push_back(returnPC);
    code.push_back(vsConnectBase);

    bool wrote = true;
    for (size_t i = 0; i < code.size(); i++)
        wrote = WriteARM9U32(nds, kDirectBootTrampolineAddr + static_cast<melonDS::u32>(i * sizeof(melonDS::u32)), code[i]) && wrote;
    if (!wrote)
        return false;

    nds->ARM9.JumpTo(kDirectBootTrampolineAddr);
    return true;
}

bool InjectNSMLPacketBridgeScheduleLoadGameSM(int instanceID, melonDS::u32 frame, melonDS::NDS* nds, melonDS::u32 vsConnectBase)
{
    if (!G.PacketBridgeScheduleLoadGameSM)
        return false;
    if (G.PacketBridgeScheduleLoadGameSMApplied[instanceID])
        return false;
    if (!InjectNSMLPacketBridgeScheduleSubMenu(
            instanceID,
            frame,
            nds,
            vsConnectBase,
            kA2DJVSConnectLoadGameSMSubMenuAddr,
            0x1E,
            1,
            "loadGameSM"))
    {
        return false;
    }

    G.PacketBridgeScheduleLoadGameSMApplied[instanceID] = true;
    return true;
}

bool InjectNSMLPacketBridgeScheduledSubMenus(int instanceID, melonDS::u32 frame, melonDS::NDS* nds, melonDS::u32 vsConnectBase)
{
    if (G.PacketBridgeSubMenuScheduleEntries.empty())
        return false;

    const int roleMask = (G.NetRole == Role::Client) ? 2 : 1;
    for (State::PacketBridgeSubMenuSchedule& entry : G.PacketBridgeSubMenuScheduleEntries)
    {
        if ((entry.RoleMask & roleMask) == 0)
            continue;
        if (entry.Applied[instanceID] || frame < entry.Frame)
            continue;
        const bool injected = G.PacketBridgeSubMenuDirectChange
            ? InjectNSMLPacketBridgeDirectSubMenuChange(
                instanceID,
                frame,
                nds,
                vsConnectBase,
                entry.SubMenu,
                G.PacketBridgeSubMenuCallCreate || entry.Create != 0)
            : InjectNSMLPacketBridgeScheduleSubMenu(
                instanceID,
                frame,
                nds,
                vsConnectBase,
                entry.SubMenu,
                entry.Delay,
                entry.Create,
                "scripted-submenu");
        if (!injected)
        {
            return false;
        }
        entry.Applied[instanceID] = true;
        return true;
    }

    return false;
}

void InjectNSMLPacketBridgeScheduledSubMenusIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (G.PacketBridgeSubMenuScheduleEntries.empty() || !nds || instanceID < 0 || instanceID >= 16)
        return;
    if (nds->ARM9Read32(kGameStageGroupAddr) == 9)
        return;

    const melonDS::u32 vsConnectBase = FindObjectBaseByID(nds, kVsConnectObjectID);
    if (vsConnectBase == 0)
        return;

    InjectNSMLPacketBridgeScheduledSubMenus(instanceID, frame, nds, vsConnectBase);
}

void ForceNSMLPacketBridgeStageStartSMFieldsIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.PacketBridgeForceStageStartSMFields || !nds || instanceID < 0 || instanceID >= 16)
        return;
    if (frame < G.PacketBridgeForceStageStartSMFieldsStartFrame)
        return;
    if (nds->ARM9Read16(kSceneCurrentSceneIDAddr) != 0x0006)
        return;

    const melonDS::u32 vsConnectBase = FindObjectBaseByID(nds, kVsConnectObjectID);
    if (vsConnectBase == 0)
        return;
    if (nds->ARM9Read32(vsConnectBase + 0x120) != 0x021512B8)
        return;

    if (G.PacketBridgeStageStartSMBaseFrame[instanceID] == 0)
        G.PacketBridgeStageStartSMBaseFrame[instanceID] = frame;

    const melonDS::u32 rel = frame - G.PacketBridgeStageStartSMBaseFrame[instanceID];
    const bool client = G.NetRole == Role::Client;
    const melonDS::u32 roleBit = client ? 1u : 0u;

    WriteARM9U32(nds, kGameVsModeAddr, 1);
    WriteARM9U32(nds, kGameLocalPlayerIDAddr, roleBit);

    melonDS::u32 step = 3;
    melonDS::u32 timer = client ? 0x03u : 0x10u;
    melonDS::u32 flags = roleBit;
    if (G.PacketBridgeForceStageStartSMUseLoadStep && rel <= 35)
    {
        step = 2;
        timer = 0;
    }
    else if (!client && rel >= 60)
    {
        step = 5;
        timer = 0x21;
    }
    if (rel >= 90)
    {
        step = 7;
        timer = client ? 0x28u : 0x31u;
        flags = 0x00030000u | roleBit;
        nds->ARM9Write16(kSceneNextSceneIDAddr, 0x0005);
        WriteARM9U32(nds, kSceneNextSceneSettingsAddr, 0);
    }

    WriteARM9U32(nds, vsConnectBase + 0x144, step);
    WriteARM9U32(nds, vsConnectBase + 0x148, timer);
    WriteARM9U32(nds, vsConnectBase + 0x154, flags);

    if (G.PacketBridgeTraceEnabled && (frame % 30) == 0)
    {
        std::printf("NSMB PacketBridge: force stage-start SM fields inst=%d frame=%u rel=%u step=%u timer=0x%X flags=0x%X\n",
            instanceID,
            frame,
            rel,
            step,
            timer,
            flags);
        std::fflush(stdout);
    }
}

bool InjectNSMLPacketBridgeStageStartSMUpdateIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.PacketBridgeRunStageStartSMUpdate || !nds || instanceID < 0 || instanceID >= 16)
        return false;
    if (frame < G.PacketBridgeRunStageStartSMUpdateStartFrame)
        return false;
    if (G.PacketBridgeRunStageStartSMUpdateLastFrame[instanceID] == frame)
        return false;
    if (nds->ARM9Read32(kGameStageGroupAddr) == 9)
        return false;
    if (nds->ARM9Read16(kSceneCurrentSceneIDAddr) != 0x0006)
        return false;

    const melonDS::u32 vsConnectBase = FindObjectBaseByID(nds, kVsConnectObjectID);
    if (vsConnectBase == 0)
        return false;
    if (nds->ARM9Read32(vsConnectBase + 0x120) != kA2DJVSConnectUpdateStageStartSMAddr)
        return false;

    const melonDS::u32 oldPC = nds->ARM9.R[15] - ((nds->ARM9.CPSR & 0x20) ? 2 : 4);
    const melonDS::u32 returnPC = oldPC | ((nds->ARM9.CPSR & 0x20) ? 1u : 0u);
    std::vector<melonDS::u32> code;
    code.reserve(16);
    const auto emitBL = [&code](melonDS::u32 target)
    {
        const melonDS::u32 pc = kDirectBootTrampolineAddr + static_cast<melonDS::u32>(code.size() * sizeof(melonDS::u32)) + 8u;
        const melonDS::s32 offset = static_cast<melonDS::s32>(target - pc) >> 2;
        code.push_back(0xEB000000u | (static_cast<melonDS::u32>(offset) & 0x00FFFFFFu));
    };

    code.push_back(0xE92D5FFFu); // push {r0-r12, lr}
    code.push_back(0xE10F5000u); // mrs r5, cpsr
    code.push_back(0xE92D0020u); // push {r5}
    code.push_back(0xE59F001Cu); // ldr r0, [pc, #28]
    emitBL(kA2DJVSConnectUpdateStageStartSMAddr);
    code.push_back(0xE8BD0020u); // pop {r5}
    code.push_back(0xE128F005u); // msr apsr_nzcvq, r5
    code.push_back(0xE8BD5FFFu); // pop {r0-r12, lr}
    code.push_back(0xE59FC004u); // ldr ip, [pc, #4]
    code.push_back(0xE12FFF1Cu); // bx ip
    code.push_back(0xE1A00000u); // nop
    code.push_back(returnPC);
    code.push_back(vsConnectBase);

    bool wrote = true;
    for (size_t i = 0; i < code.size(); i++)
        wrote = WriteARM9U32(nds, kDirectBootTrampolineAddr + static_cast<melonDS::u32>(i * sizeof(melonDS::u32)), code[i]) && wrote;
    if (!wrote)
        return false;

    G.PacketBridgeRunStageStartSMUpdateLastFrame[instanceID] = frame;
    if (G.PacketBridgeTraceEnabled && (frame % 30) == 0)
    {
        std::printf("NSMB PacketBridge: run stage-start SM update inst=%d frame=%u vsConnect=%08X\n",
            instanceID,
            frame,
            vsConnectBase);
        std::fflush(stdout);
    }
    nds->ARM9.JumpTo(kDirectBootTrampolineAddr);
    return true;
}

bool InjectNSMLPacketBridgeVSConnectOnUpdateIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.PacketBridgeRunVSConnectOnUpdate || !nds || instanceID < 0 || instanceID >= 16)
        return false;
    if (frame < G.PacketBridgeRunVSConnectOnUpdateStartFrame)
        return false;
    if (G.PacketBridgeRunVSConnectOnUpdateLastFrame[instanceID] == frame)
        return false;
    if (nds->ARM9Read32(kGameStageGroupAddr) == 9)
        return false;
    if (nds->ARM9Read16(kSceneCurrentSceneIDAddr) != 0x0006)
        return false;

    const melonDS::u32 vsConnectBase = FindObjectBaseByID(nds, kVsConnectObjectID);
    if (vsConnectBase == 0)
        return false;
    if (nds->ARM9Read32(vsConnectBase + 0x120) != kA2DJVSConnectUpdateStageStartSMAddr)
        return false;

    const melonDS::u32 oldPC = nds->ARM9.R[15] - ((nds->ARM9.CPSR & 0x20) ? 2 : 4);
    const melonDS::u32 returnPC = oldPC | ((nds->ARM9.CPSR & 0x20) ? 1u : 0u);
    if ((nds->ARM9.CPSR & 0x1Fu) != 0x1Fu || nds->ARM9.R[13] < 0x02000000u || nds->ARM9.R[13] >= 0x02800000u)
        return false;

    std::vector<melonDS::u32> code;
    code.reserve(16);
    const auto emitBL = [&code](melonDS::u32 target)
    {
        const melonDS::u32 pc = kDirectBootTrampolineAddr + static_cast<melonDS::u32>(code.size() * sizeof(melonDS::u32)) + 8u;
        const melonDS::s32 offset = static_cast<melonDS::s32>(target - pc) >> 2;
        code.push_back(0xEB000000u | (static_cast<melonDS::u32>(offset) & 0x00FFFFFFu));
    };

    code.push_back(0xE92D5FFFu); // push {r0-r12, lr}
    code.push_back(0xE10F5000u); // mrs r5, cpsr
    code.push_back(0xE92D0020u); // push {r5}
    code.push_back(0xE59F001Cu); // ldr r0, [pc, #28]
    emitBL(kA2DJVSConnectOnUpdateAddr);
    code.push_back(0xE8BD0020u); // pop {r5}
    code.push_back(0xE128F005u); // msr apsr_nzcvq, r5
    code.push_back(0xE8BD5FFFu); // pop {r0-r12, lr}
    code.push_back(0xE59FC004u); // ldr ip, [pc, #4]
    code.push_back(0xE12FFF1Cu); // bx ip
    code.push_back(0xE1A00000u); // nop
    code.push_back(returnPC);
    code.push_back(vsConnectBase);

    bool wrote = true;
    for (size_t i = 0; i < code.size(); i++)
        wrote = WriteARM9U32(nds, kDirectBootTrampolineAddr + static_cast<melonDS::u32>(i * sizeof(melonDS::u32)), code[i]) && wrote;
    if (!wrote)
        return false;

    G.PacketBridgeRunVSConnectOnUpdateLastFrame[instanceID] = frame;
    if (G.PacketBridgeTraceEnabled && (frame % 30) == 0)
    {
        std::printf("NSMB PacketBridge: run VSConnect onUpdate inst=%d frame=%u vsConnect=%08X\n",
            instanceID,
            frame,
            vsConnectBase);
        std::fflush(stdout);
    }
    nds->ARM9.JumpTo(kDirectBootTrampolineAddr);
    return true;
}

bool InjectNSMLPacketBridgeDummyAlloc(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.PacketBridgeDummyAlloc || !nds || instanceID < 0 || instanceID >= 16)
        return false;
    if (G.PacketBridgeDummyAllocApplied[instanceID])
        return false;
    if (frame < G.PacketBridgeDummyAllocFrame || G.PacketBridgeDummyAllocSize == 0)
        return false;
    if (!IsMarioVsLuigiGGID(nds->ARM9Read32(kNetGGIDAddr)))
        return false;

    const melonDS::u32 oldPC = nds->ARM9.R[15] - ((nds->ARM9.CPSR & 0x20) ? 2 : 4);
    const melonDS::u32 returnPC = oldPC | ((nds->ARM9.CPSR & 0x20) ? 1u : 0u);
    std::vector<melonDS::u32> code;
    code.reserve(16);
    const auto emitBL = [&code](melonDS::u32 target)
    {
        const melonDS::u32 pc = kDirectBootTrampolineAddr + static_cast<melonDS::u32>(code.size() * sizeof(melonDS::u32)) + 8u;
        const melonDS::s32 offset = static_cast<melonDS::s32>(target - pc) >> 2;
        code.push_back(0xEB000000u | (static_cast<melonDS::u32>(offset) & 0x00FFFFFFu));
    };

    code.push_back(0xE92D5FFFu); // push {r0-r12, lr}
    code.push_back(0xE10F5000u); // mrs r5, cpsr
    code.push_back(0xE92D0020u); // push {r5}
    code.push_back(0xE59F001Cu); // ldr r0, [pc, #28]
    emitBL(kA2DJFSCacheLoadDataAddr);
    code.push_back(0xE8BD0020u); // pop {r5}
    code.push_back(0xE128F005u); // msr apsr_nzcvq, r5
    code.push_back(0xE8BD5FFFu); // pop {r0-r12, lr}
    code.push_back(0xE59FC004u); // ldr ip, [pc, #4]
    code.push_back(0xE12FFF1Cu); // bx ip
    code.push_back(0xE1A00000u); // nop
    code.push_back(returnPC);
    code.push_back(G.PacketBridgeDummyAllocSize);

    bool wrote = true;
    for (size_t i = 0; i < code.size(); i++)
        wrote = WriteARM9U32(nds, kDirectBootTrampolineAddr + static_cast<melonDS::u32>(i * sizeof(melonDS::u32)), code[i]) && wrote;
    if (!wrote)
        return false;

    G.PacketBridgeDummyAllocApplied[instanceID] = true;
    std::printf("NSMB PacketBridge: dummy alloc inst=%d frame=%u size=0x%X\n",
        instanceID,
        frame,
        G.PacketBridgeDummyAllocSize);
    std::fflush(stdout);
    nds->ARM9.JumpTo(kDirectBootTrampolineAddr);
    return true;
}

void ForceNSMLPacketBridgeLoadGameSMIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!nds || instanceID < 0 || instanceID >= 16)
        return;
    if (frame < G.PacketBridgeForceLoadGameSMStartFrame)
        return;

    if (!IsMarioVsLuigiGGID(nds->ARM9Read32(kNetGGIDAddr)))
        return;
    if (!G.PacketBridgeForceLoadGameSMAllowCourseSelect
        && FindObjectBaseByID(nds, kCourseSelectObjectID) != 0)
        return;

    const melonDS::u32 vsConnectBase = FindObjectBaseByID(nds, kVsConnectObjectID);
    if (vsConnectBase == 0)
        return;

    if (InjectNSMLPacketBridgeMvlFileCache(instanceID, frame, nds))
        return;

    if (!G.PacketBridgeForceLoadGameSM)
        return;

    const melonDS::u32 currentUpdate = nds->ARM9Read32(vsConnectBase + 0x120);
    const melonDS::u32 currentStep = nds->ARM9Read32(vsConnectBase + 0x144);
    const melonDS::u32 targetStep = std::clamp<melonDS::u32>(G.PacketBridgeForceLoadGameSMStep, 0, 7);
    const bool alreadyLoadGameSM = currentUpdate == kA2DJVSConnectUpdateLoadGameSMAddr && currentStep >= targetStep;
    if (!alreadyLoadGameSM)
    {
        if (InjectNSMLPacketBridgeScheduleLoadGameSM(instanceID, frame, nds, vsConnectBase))
            return;
        if (G.PacketBridgeScheduleLoadGameSM)
            return;

        if (G.PacketBridgeForceLoadGameSMPreload && !G.PacketBridgeForceLoadGameSMCreateApplied[instanceID])
        {
            const melonDS::u32 oldPC = nds->ARM9.R[15] - ((nds->ARM9.CPSR & 0x20) ? 2 : 4);
            const melonDS::u32 returnPC = oldPC | ((nds->ARM9.CPSR & 0x20) ? 1u : 0u);
            std::vector<melonDS::u32> code;
            code.reserve(24);
            const auto emitLoadImm = [&code](int reg, melonDS::u32 value)
            {
                if (value <= 0xFF)
                {
                    code.push_back(0xE3A00000u | (static_cast<melonDS::u32>(reg & 0xF) << 12) | value);
                    return;
                }
                code.push_back(0xE59F0000u | (static_cast<melonDS::u32>(reg & 0xF) << 12)); // ldr reg, [pc]
                code.push_back(0xEA000000u); // skip literal
                code.push_back(value);
            };
            const auto emitBL = [&code](melonDS::u32 target)
            {
                const melonDS::u32 pc = kDirectBootTrampolineAddr + static_cast<melonDS::u32>(code.size() * sizeof(melonDS::u32)) + 8u;
                const melonDS::s32 offset = static_cast<melonDS::s32>(target - pc) >> 2;
                code.push_back(0xEB000000u | (static_cast<melonDS::u32>(offset) & 0x00FFFFFFu));
            };

            code.push_back(0xE92D5FFFu); // push {r0-r12, lr}
            code.push_back(0xE10F5000u); // mrs r5, cpsr
            code.push_back(0xE92D0020u); // push {r5}
            code.push_back(0xE24DD004u); // sub sp, sp, #4
            code.push_back(0xE3A00001u); // mov r0, #1
            code.push_back(0xE1A00600u); // lsl r0, r0, #12 (0x1000 stack size)
            code.push_back(0xE58D0000u); // str r0, [sp]
            emitLoadImm(0, kA2DJLoadMvsLFilesThreadAddr);
            code.push_back(0xE3A01000u); // mov r1, #0
            code.push_back(0xE3A02014u); // mov r2, #0x14
            code.push_back(0xE3A03000u); // mov r3, #0
            emitBL(kA2DJCreateThreadAddr);
            code.push_back(0xE28DD004u); // add sp, sp, #4
            code.push_back(0xE8BD0020u); // pop {r5}
            code.push_back(0xE128F005u); // msr apsr_nzcvq, r5
            code.push_back(0xE8BD5FFFu); // pop {r0-r12, lr}
            code.push_back(0xE59FC004u); // ldr ip, [pc, #4]
            code.push_back(0xE12FFF1Cu); // bx ip
            code.push_back(0xE1A00000u); // nop
            code.push_back(returnPC);

            bool wrote = true;
            for (size_t i = 0; i < code.size(); i++)
                wrote = WriteARM9U32(nds, kDirectBootTrampolineAddr + static_cast<melonDS::u32>(i * sizeof(melonDS::u32)), code[i]) && wrote;
            if (wrote)
            {
                G.PacketBridgeForceLoadGameSMCreateApplied[instanceID] = true;
                std::printf("NSMB PacketBridge: start MvL file preload thread inst=%d frame=%u vsConnect=%08X\n",
                    instanceID,
                    frame,
                    vsConnectBase);
                std::fflush(stdout);
                nds->ARM9.JumpTo(kDirectBootTrampolineAddr);
            }
            return;
        }

        WriteARM9U32(nds, vsConnectBase + 0x078, 0x00000003);
        WriteARM9U32(nds, vsConnectBase + 0x07C, 0x00000003);
        WriteARM9U32(nds, vsConnectBase + 0x080, 0x00000107);
        WriteARM9U32(nds, vsConnectBase + 0x098, 0x00000004);
        WriteARM9U32(nds, vsConnectBase + 0x09C, 0x00000004);
        WriteARM9U32(nds, vsConnectBase + 0x10C, 0x00000000);
        WriteARM9U32(nds, vsConnectBase + 0x110, 0x00000000);
        WriteARM9U32(nds, vsConnectBase + 0x114, 0x00000000);
        WriteARM9U32(nds, vsConnectBase + 0x118, kA2DJVSConnectCreateLoadGameSMAddr);
        WriteARM9U32(nds, vsConnectBase + 0x120, kA2DJVSConnectUpdateLoadGameSMAddr);
        WriteARM9U32(nds, vsConnectBase + 0x128, kA2DJVSConnectRenderLoadGameSMAddr);
        WriteARM9U32(nds, vsConnectBase + 0x134, kA2DJVSConnectLoadGameSMSubMenuAddr);
        WriteARM9U32(nds, vsConnectBase + 0x140, (G.NetRole == Role::Client) ? 0x00000002 : 0x00000001);
        const melonDS::u32 loadGameTimer = (G.PacketBridgeForceLoadGameSMTimer >= 0)
            ? static_cast<melonDS::u32>(G.PacketBridgeForceLoadGameSMTimer)
            : ((targetStep < 7) ? 0u : ((G.NetRole == Role::Client) ? 0x00000027u : 0x00000030u));
        WriteARM9U32(nds, vsConnectBase + 0x144, targetStep);
        WriteARM9U32(nds, vsConnectBase + 0x148, loadGameTimer);
        const melonDS::u32 loadGameFlagsBase = (G.PacketBridgeForceLoadGameSMFlags >= 0)
            ? static_cast<melonDS::u32>(G.PacketBridgeForceLoadGameSMFlags)
            : ((targetStep < 7) ? 0x00000000 : 0x00030000);
        const melonDS::u32 loadGameFlags = loadGameFlagsBase
            | ((G.NetRole == Role::Client) ? 1u : 0u);
        WriteARM9U32(nds, vsConnectBase + 0x154, loadGameFlags);
        if (G.NetRole == Role::Client)
        {
            WriteARM9U32(nds, vsConnectBase + 0x13C, 0x00000258);
            WriteARM9U32(nds, vsConnectBase + 0x14C, 0x11BF0900);
            WriteARM9U32(nds, vsConnectBase + 0x150, 0x00003322);
            WriteARM9U32(nds, vsConnectBase + 0x158, 0x00000001);
        }
    }
    if (targetStep < 7 || nds->ARM9Read32(kGameStageGroupAddr) != 9)
    {
        nds->ARM9Write8(kNetPacketActionAddr, targetStep >= 3 ? 0x03 : 0x00);
        WriteARM9U32(nds, 0x02088A58, 0x00000001);
        WriteARM9U32(nds, 0x02088A5C, 0x00000002);
        WriteARM9U32(nds, 0x02088A64, 0x00000002);
    }
    if (G.PacketBridgeForceLoadGameSMPulseAction)
    {
        const melonDS::u32 rel = frame - G.PacketBridgeForceLoadGameSMStartFrame;
        if ((rel % 25u) < 5u)
            nds->ARM9Write8(kNetPacketActionAddr, 0x03);
    }
    if (G.PacketBridgeForceLoadGameSMBaselineFlags && targetStep >= 1)
    {
        const melonDS::u32 rel = frame - G.PacketBridgeForceLoadGameSMStartFrame;
        const melonDS::u32 roleBit = (G.NetRole == Role::Client) ? 1u : 0u;
        const melonDS::u32 currentTimer = nds->ARM9Read32(vsConnectBase + 0x148);
        if (G.NetRole == Role::Client)
        {
            if (rel >= 30 && nds->ARM9Read32(vsConnectBase + 0x144) < 5)
            {
                WriteARM9U32(nds, vsConnectBase + 0x144, 5);
                WriteARM9U32(nds, vsConnectBase + 0x148, std::max<melonDS::u32>(currentTimer, 0x19));
                WriteARM9U32(nds, vsConnectBase + 0x154, 0x00010000 | roleBit);
            }
            if (rel >= 48)
            {
                const melonDS::u32 beforeStep = nds->ARM9Read32(vsConnectBase + 0x144);
                WriteARM9U32(nds, vsConnectBase + 0x144, std::max<melonDS::u32>(beforeStep, 6));
                if (beforeStep < 6)
                    WriteARM9U32(nds, vsConnectBase + 0x148, 0x24);
                WriteARM9U32(nds, vsConnectBase + 0x154, 0x00030000 | roleBit);
                WriteARM9U32(nds, 0x02088A58, 0x00000101);
                WriteARM9U32(nds, 0x02088A5C, 0x00000202);
                WriteARM9U32(nds, 0x02088A64, 0x00000202);
            }
            if (rel >= 72)
            {
                WriteARM9U32(nds, vsConnectBase + 0x144, 7);
                WriteARM9U32(nds, vsConnectBase + 0x148, 0x28);
                WriteARM9U32(nds, vsConnectBase + 0x154, 0x00030000 | roleBit);
            }
        }
        else
        {
            if (rel >= 51)
                WriteARM9U32(nds, vsConnectBase + 0x154, 0x00010000 | roleBit);
            if (rel >= 66)
            {
                const melonDS::u32 beforeStep = nds->ARM9Read32(vsConnectBase + 0x144);
                WriteARM9U32(nds, vsConnectBase + 0x144, std::max<melonDS::u32>(beforeStep, 6));
                if (beforeStep < 6)
                    WriteARM9U32(nds, vsConnectBase + 0x148, 0x2D);
                WriteARM9U32(nds, vsConnectBase + 0x154, 0x00030000 | roleBit);
                WriteARM9U32(nds, 0x02088A58, 0x00000101);
                WriteARM9U32(nds, 0x02088A5C, 0x00000202);
                WriteARM9U32(nds, 0x02088A64, 0x00000202);
            }
            if (rel >= 85)
            {
                WriteARM9U32(nds, vsConnectBase + 0x144, 7);
                WriteARM9U32(nds, vsConnectBase + 0x148, 0x31);
                WriteARM9U32(nds, vsConnectBase + 0x154, 0x00030000 | roleBit);
            }
        }
    }

    if (G.PacketBridgeForceLoadGameSMRunUpdate
        && (!G.PacketBridgeForceLoadGameSMRunUpdateClientOnly || G.NetRole == Role::Client)
        && nds->ARM9Read32(vsConnectBase + 0x144) < 7
        && !(G.ForceCourseSelectFactory && frame == G.ForceCourseSelectFactoryFrame))
    {
        const melonDS::u32 oldPC = nds->ARM9.R[15] - ((nds->ARM9.CPSR & 0x20) ? 2 : 4);
        const melonDS::u32 returnPC = oldPC | ((nds->ARM9.CPSR & 0x20) ? 1u : 0u);
        std::vector<melonDS::u32> code;
        code.reserve(16);
        const auto emitBL = [&code](melonDS::u32 target)
        {
            const melonDS::u32 pc = kDirectBootTrampolineAddr + static_cast<melonDS::u32>(code.size() * sizeof(melonDS::u32)) + 8u;
            const melonDS::s32 offset = static_cast<melonDS::s32>(target - pc) >> 2;
            code.push_back(0xEB000000u | (static_cast<melonDS::u32>(offset) & 0x00FFFFFFu));
        };
        code.push_back(0xE92D5FFFu); // push {r0-r12, lr}
        code.push_back(0xE10F5000u); // mrs r5, cpsr
        code.push_back(0xE92D0020u); // push {r5}
        code.push_back(0xE59F001Cu); // ldr r0, [pc, #28]
        emitBL(kA2DJVSConnectUpdateLoadGameSMAddr);
        code.push_back(0xE8BD0020u); // pop {r5}
        code.push_back(0xE128F005u); // msr apsr_nzcvq, r5
        code.push_back(0xE8BD5FFFu); // pop {r0-r12, lr}
        code.push_back(0xE59FC004u); // ldr ip, [pc, #4]
        code.push_back(0xE12FFF1Cu); // bx ip
        code.push_back(0xE1A00000u); // nop
        code.push_back(returnPC);
        code.push_back(vsConnectBase);

        bool wrote = true;
        for (size_t i = 0; i < code.size(); i++)
            wrote = WriteARM9U32(nds, kDirectBootTrampolineAddr + static_cast<melonDS::u32>(i * sizeof(melonDS::u32)), code[i]) && wrote;
        if (wrote)
            nds->ARM9.JumpTo(kDirectBootTrampolineAddr);
    }

    if (G.PacketBridgeTraceEnabled && (frame % 60) == 0)
    {
        std::printf("NSMB PacketBridge: force load-game SM inst=%d frame=%u vsConnect=%08X\n",
            instanceID,
            frame,
            vsConnectBase);
        std::fflush(stdout);
    }
}

void ForceNSMLStagePacketWordsIfNeeded(melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.PacketBridgeForceStagePacketWords || !nds)
        return;
    if (frame < G.PacketBridgeForceStagePacketWordsStartFrame)
        return;
    if (G.PacketBridgeForceStagePacketWordsEndFrame != 0 && frame > G.PacketBridgeForceStagePacketWordsEndFrame)
        return;
    if (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1)
        return;

    WriteARM9U32(nds, 0x020888E4u, 0xFFFF0003u);
    WriteARM9U32(nds, 0x0208B044u, 0xFFFF0003u);
    WriteARM9U32(nds, 0x0208B048u, 0x00000000u);
    WriteARM9U32(nds, 0x0208B04Cu, 0x00000000u);
    WriteARM9U32(nds, 0x0208B050u, 0x00000000u);
    WriteARM9U32(nds, 0x0208B054u, 0x00000000u);
    WriteARM9U32(nds, 0x02186A88u, 0x00000303u);

    if (G.PacketBridgeForceStageNet20OnStageScene
        && nds->ARM9Read16(kSceneCurrentSceneIDAddr) == 0x0003
        && FindObjectByIDAndSettingsLoose(nds, kStageSceneObjectID, kMvlStageSceneSettings).Found)
        nds->ARM9Write16(kNetState20Addr, 2);
}

void ForceNSMLGameLocalPlayerIDIfNeeded(melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!nds || G.PacketBridgeForceGameLocalPlayerID < 0)
        return;
    if (frame < G.PacketBridgeForceGameLocalPlayerIDStartFrame)
        return;
    if (!G.PacketBridgeForceGameLocalPlayerIDEarly
        && (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1))
        return;

    nds->ARM9Write32(kGameLocalPlayerIDAddr, static_cast<melonDS::u32>(G.PacketBridgeForceGameLocalPlayerID & 1));
}

void PumpNSMLPacketBridgeLocked(melonDS::NDS* nds, melonDS::u32 frame)
{
    FlushDelayedNSMLPacketsLocked(frame);
    PumpNetworkLocked(nds, frame);
    ApplyPendingNSMLPacketsLocked(nds);
    SendMatchSeedLocked();
}

void WaitForNSMLPacketBridgeRemote(melonDS::NDS* nds, melonDS::u32 frame)
{
    if (!G.PacketBridgeWaitEnabled || !nds)
        return;
    if (frame < G.PacketBridgeWaitStartFrame)
        return;

    const melonDS::u32 remotePlayer = LocalPlayerID(nds) ^ 1;
    const melonDS::u32 currentTick = PacketBridgeCanonicalTick(nds, frame);
    const melonDS::u32 tick = (currentTick + static_cast<melonDS::u32>(G.PacketBridgeWaitTickAhead)) & 0xFFFF;
    if (melonDS::NSML_HasMarioVsLuigiRemotePacket(nds, remotePlayer, tick))
        return;
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        if (G.LastReceivedNSMLPacketTick[remotePlayer] == 0xFFFFFFFF)
            return;
    }

    const auto start = std::chrono::steady_clock::now();
    for (;;)
    {
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            PumpNSMLPacketBridgeLocked(nds, frame);
        }

        if (melonDS::NSML_HasMarioVsLuigiRemotePacket(nds, remotePlayer, tick))
            return;

        if (G.PacketBridgeWaitTimeoutMs > 0)
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= G.PacketBridgeWaitTimeoutMs)
            {
                if (G.PacketBridgeTraceEnabled && G.LastPacketBridgeWaitTimeoutTick != tick)
                {
                    G.LastPacketBridgeWaitTimeoutTick = tick;
                    std::printf("NSMB PacketBridge: wait timeout player=%u tick=0x%04X frame=%u waitedMs=%d\n",
                        remotePlayer,
                        tick,
                        frame,
                        G.PacketBridgeWaitTimeoutMs);
                }
                return;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

int NSMLPacketTickLead(melonDS::u32 localTick, melonDS::u32 remoteTick)
{
    return static_cast<int>(static_cast<melonDS::s16>((localTick - remoteTick) & 0xFFFF));
}

void ThrottleNSMLPacketBridgeLead(melonDS::NDS* nds, melonDS::u32 frame)
{
    if (G.PacketBridgeMaxTickLead < 0 || !nds)
        return;
    if (frame < G.PacketBridgeThrottleStartFrame)
        return;

    const melonDS::u32 remotePlayer = LocalPlayerID(nds) ^ 1;
    const auto start = std::chrono::steady_clock::now();

    for (;;)
    {
        melonDS::u32 remoteTick = 0xFFFFFFFF;
        melonDS::u32 remoteFrame = 0xFFFFFFFF;
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            remoteTick = G.LastReceivedNSMLPacketTick[remotePlayer];
            remoteFrame = G.LastReceivedNSMLPacketFrame[remotePlayer];
        }

        if (remoteTick == 0xFFFFFFFF)
            return;

        const melonDS::u32 localTick = PacketBridgeCanonicalTick(nds, frame);
        const int lead = NSMLPacketTickLead(localTick, remoteTick);
        if (lead <= G.PacketBridgeMaxTickLead)
            return;

        if (G.PacketBridgeTraceEnabled && G.LastPacketBridgeThrottleTraceTick != localTick)
        {
            G.LastPacketBridgeThrottleTraceTick = localTick;
            std::printf("NSMB PacketBridge: throttle localTick=0x%04X remotePlayer=%u remoteTick=0x%04X lead=%d maxLead=%d frame=%u remoteFrame=%u\n",
                localTick,
                remotePlayer,
                remoteTick,
                lead,
                G.PacketBridgeMaxTickLead,
                frame,
                remoteFrame);
            std::fflush(stdout);
        }

        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            PumpNSMLPacketBridgeLocked(nds, frame);
        }

        if (G.PacketBridgeThrottleTimeoutMs > 0)
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= G.PacketBridgeThrottleTimeoutMs)
            {
                if (G.PacketBridgeTraceEnabled)
                {
                    std::printf("NSMB PacketBridge: throttle timeout localTick=0x%04X remoteTick=0x%04X lead=%d frame=%u waitedMs=%d\n",
                        localTick,
                        remoteTick,
                        lead,
                        frame,
                        G.PacketBridgeThrottleTimeoutMs);
                    std::fflush(stdout);
                }
                return;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void ThrottleNSMLPacketBridgeFrameLead(melonDS::NDS* nds, melonDS::u32 frame)
{
    if (G.PacketBridgeMaxFrameLead < 0 || !nds)
        return;
    if (frame < G.PacketBridgeThrottleStartFrame)
        return;
    if (G.PacketBridgeForceTickEnabled && frame < G.PacketBridgeForceTickStartFrame)
        return;

    const melonDS::u32 remotePlayer = LocalPlayerID(nds) ^ 1;
    const auto start = std::chrono::steady_clock::now();

    for (;;)
    {
        melonDS::u32 remoteTick = 0xFFFFFFFF;
        melonDS::u32 remoteFrame = 0xFFFFFFFF;
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            remoteTick = G.LastReceivedNSMLPacketTick[remotePlayer];
            remoteFrame = G.LastReceivedNSMLPacketFrame[remotePlayer];
        }

        if (remoteFrame == 0xFFFFFFFF)
            return;
        if (G.PacketBridgeForceTickEnabled && remoteFrame < G.PacketBridgeForceTickStartFrame)
            return;

        const int lead = static_cast<int>(frame) - static_cast<int>(remoteFrame);
        if (lead <= G.PacketBridgeMaxFrameLead)
            return;

        if (G.PacketBridgeTraceEnabled && G.LastPacketBridgeFrameThrottleTraceFrame != frame)
        {
            G.LastPacketBridgeFrameThrottleTraceFrame = frame;
            std::printf("NSMB PacketBridge: frame throttle frame=%u remotePlayer=%u remoteFrame=%u lead=%d maxLead=%d remoteTick=0x%04X\n",
                frame,
                remotePlayer,
                remoteFrame,
                lead,
                G.PacketBridgeMaxFrameLead,
                remoteTick & 0xFFFF);
            std::fflush(stdout);
        }

        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            PumpNSMLPacketBridgeLocked(nds, frame);
        }

        if (G.PacketBridgeThrottleTimeoutMs > 0)
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= G.PacketBridgeThrottleTimeoutMs)
            {
                if (G.PacketBridgeTraceEnabled)
                {
                    std::printf("NSMB PacketBridge: frame throttle timeout frame=%u remoteFrame=%u lead=%d waitedMs=%d\n",
                        frame,
                        remoteFrame,
                        lead,
                        G.PacketBridgeThrottleTimeoutMs);
                    std::fflush(stdout);
                }
                return;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void PruneInputHistoryLocked(melonDS::u32 keepFromFrame)
{
    G.LocalInputs.erase(G.LocalInputs.begin(), G.LocalInputs.lower_bound(keepFromFrame));
    G.RemoteInputs.erase(G.RemoteInputs.begin(), G.RemoteInputs.lower_bound(keepFromFrame));
}

bool IsPastTestInputRange(melonDS::u32 targetFrame)
{
    return G.TestEnabled
        && G.TestFrames != kNoFrameLimit
        && targetFrame >= G.TestFrames;
}

void RecordRemoteInputWaitStats(unsigned long long elapsedUs, unsigned long long loops)
{
    G.RemoteInputWaitCount++;
    G.RemoteInputWaitLoops += loops;
    G.RemoteInputWaitUs += elapsedUs;
    G.RemoteInputWaitMaxUs = std::max(G.RemoteInputWaitMaxUs, elapsedUs);
}

void RecordFrameLeadThrottleStats(unsigned long long elapsedUs, unsigned long long loops)
{
    G.FrameLeadThrottleCount++;
    G.FrameLeadThrottleLoops += loops;
    G.FrameLeadThrottleUs += elapsedUs;
    G.FrameLeadThrottleMaxUs = std::max(G.FrameLeadThrottleMaxUs, elapsedUs);
}

InputState WaitForRemoteInput(melonDS::u32 targetFrame)
{
    if ((G.PacketBridgeOnly || G.InputNetplayOnly)
        && G.NetplayStartFrame != 0
        && targetFrame < G.NetplayStartFrame)
        return NeutralInput();

    const auto start = std::chrono::steady_clock::now();
    unsigned long long loops = 0;
    for (;;)
    {
        loops++;
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            PumpNetworkLocked();

            auto it = G.RemoteInputs.find(targetFrame);
            if (it != G.RemoteInputs.end())
            {
                const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start).count();
                RecordRemoteInputWaitStats(static_cast<unsigned long long>(std::max<long long>(0, elapsedUs)), loops);
                return it->second;
            }
        }

        if (G.TestEnabled && G.TestWaitTimeoutMs > 0)
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= G.TestWaitTimeoutMs)
            {
                std::printf("NSMB Test: remote input timeout frame=%u waitedMs=%d\n",
                    targetFrame,
                    G.TestWaitTimeoutMs);
                std::fflush(stdout);
                if (G.RemoteInputTimeoutFatal)
                    std::_Exit(70);
                const auto waitedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start).count();
                RecordRemoteInputWaitStats(static_cast<unsigned long long>(std::max<long long>(0, waitedUs)), loops);
                return NeutralInput();
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void WaitForMatchSeedIfNeeded()
{
    if (!G.Enabled || G.NetRole != Role::Client || G.MatchSeedConfigured)
        return;

    const auto start = std::chrono::steady_clock::now();
    for (;;)
    {
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            PumpNetworkLocked();
            if (G.MatchSeedConfigured)
                return;
        }

        if (G.SeedWaitTimeoutMs > 0)
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= G.SeedWaitTimeoutMs)
            {
                std::printf("NSMB PoC: match seed wait timeout waitedMs=%d\n", G.SeedWaitTimeoutMs);
                return;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void WaitForPeerIfNeeded(bool force = false)
{
    if (!G.Enabled || (!force && !G.WaitForPeerBeforeStart) || G.NetRole != Role::Host || G.Peer)
        return;

    const auto start = std::chrono::steady_clock::now();
    for (;;)
    {
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            PumpNetworkLocked();
            if (G.Peer)
                return;
        }

        if (G.SeedWaitTimeoutMs > 0)
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= G.SeedWaitTimeoutMs)
            {
                std::printf("NSMB PoC: peer wait timeout waitedMs=%d\n", G.SeedWaitTimeoutMs);
                return;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool ShouldPumpNetworkAtFrame(melonDS::u32 syncFrame, melonDS::u32 sendStartFrame)
{
    return !G.DeferNetworkUntilStart || G.NetplayStartFrame == 0 || syncFrame >= sendStartFrame;
}

bool AllNetplayStartWaitArrivedLocked()
{
    const int count = std::max(1, std::min(G.TestInstanceCount, 16));
    for (int i = 0; i < count; i++)
    {
        if (!G.NetplayStartWaitArrived[i])
            return false;
    }
    return true;
}

void WaitForPeerAtNetplayStartBarrier(int instanceID, melonDS::u32 syncFrame)
{
    if (!G.Enabled || !G.WaitForPeerAtNetplayStart || G.NetRole != Role::Host
        || G.NetplayStartFrame == 0 || syncFrame != G.NetplayStartFrame
        || instanceID < 0 || instanceID >= 16)
    {
        return;
    }

    const bool isLocal = (instanceID == G.LocalInstance);
    {
        std::unique_lock<std::mutex> lock(G.Mutex);
        if (G.NetplayStartWaitComplete)
            return;

        G.NetplayStartWaitArrived[instanceID] = true;
        G.BarrierCond.notify_all();

        const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(G.SeedWaitTimeoutMs);

        if (isLocal)
        {
            while (!AllNetplayStartWaitArrivedLocked())
            {
                if (G.SeedWaitTimeoutMs > 0)
                {
                    if (G.BarrierCond.wait_until(lock, deadline) == std::cv_status::timeout)
                    {
                        std::printf("NSMB PoC: netplay start local barrier timeout inst=%d frame=%u waitedMs=%d\n",
                            instanceID,
                            syncFrame,
                            G.SeedWaitTimeoutMs);
                        break;
                    }
                }
                else
                {
                    G.BarrierCond.wait(lock);
                }
            }
        }
        else
        {
            while (!G.NetplayStartWaitComplete)
            {
                if (G.SeedWaitTimeoutMs > 0)
                {
                    if (G.BarrierCond.wait_until(lock, deadline) == std::cv_status::timeout)
                    {
                        std::printf("NSMB PoC: netplay start peer wait barrier timeout inst=%d frame=%u waitedMs=%d\n",
                            instanceID,
                            syncFrame,
                            G.SeedWaitTimeoutMs);
                        return;
                    }
                }
                else
                {
                    G.BarrierCond.wait(lock);
                }
            }
            return;
        }
    }

    std::printf("NSMB PoC: waiting for peer at netplay start frame=%u\n", syncFrame);
    std::fflush(stdout);
    WaitForPeerIfNeeded(true);

    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        G.WaitedForPeerAtNetplayStart = true;
        G.NetplayStartWaitComplete = true;
        G.BarrierCond.notify_all();
    }
    std::printf("NSMB PoC: peer wait at netplay start finished frame=%u\n", syncFrame);
    std::fflush(stdout);
}

bool WaitAtFrameBarrier(FrameBarrier& barrier, int instanceID, melonDS::u32 frame, const char* name)
{
    if (!G.TestEnabled || !G.FrameBarrierEnabled || G.TestInstanceCount <= 1)
        return true;
    if (instanceID < 0 || instanceID >= G.TestInstanceCount)
        return true;

    std::unique_lock<std::mutex> lock(G.Mutex);
    const int generation = barrier.Generation;
    barrier.Waiting[instanceID] = true;
    barrier.Frame[instanceID] = frame;

    const auto allArrived = [&]() {
        for (int i = 0; i < G.TestInstanceCount; i++)
        {
            if (!barrier.Waiting[i] || barrier.Frame[i] != frame)
                return false;
        }
        return true;
    };

    const auto release = [&]() {
        for (int i = 0; i < G.TestInstanceCount; i++)
            barrier.Waiting[i] = false;
        barrier.Generation++;
        G.BarrierCond.notify_all();
    };

    if (allArrived())
    {
        release();
        return true;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(G.TestWaitTimeoutMs);
    while (barrier.Generation == generation)
    {
        if (G.TestWaitTimeoutMs > 0)
        {
            if (G.BarrierCond.wait_until(lock, deadline) == std::cv_status::timeout)
            {
                std::printf("NSMB Test: %s frame barrier timeout inst=%d frame=%u waitedMs=%d\n",
                    name,
                    instanceID,
                    frame,
                    G.TestWaitTimeoutMs);
                barrier.Waiting[instanceID] = false;
                G.BarrierCond.notify_all();
                return false;
            }
        }
        else
        {
            G.BarrierCond.wait(lock);
        }

        if (allArrived())
        {
            release();
            return true;
        }
    }

    return true;
}

bool WaitForSerialRunTurn(int instanceID, melonDS::u32 frame)
{
    if (!G.TestEnabled || !G.SerialRunEnabled || G.TestInstanceCount <= 1)
        return true;
    if (instanceID < 0 || instanceID >= G.TestInstanceCount)
        return true;

    std::unique_lock<std::mutex> lock(G.Mutex);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(G.TestWaitTimeoutMs);
    for (;;)
    {
        if (G.SerialFrame == frame && G.SerialInstance == instanceID)
            return true;

        if (G.TestWaitTimeoutMs > 0)
        {
            if (G.BarrierCond.wait_until(lock, deadline) == std::cv_status::timeout)
            {
                std::printf("NSMB Test: serial run timeout inst=%d frame=%u expectedInst=%d expectedFrame=%u waitedMs=%d\n",
                    instanceID,
                    frame,
                    G.SerialInstance,
                    G.SerialFrame,
                    G.TestWaitTimeoutMs);
                return false;
            }
        }
        else
        {
            G.BarrierCond.wait(lock);
        }
    }
}

void AdvanceSerialRunTurn(int instanceID, melonDS::u32 frame)
{
    if (!G.TestEnabled || !G.SerialRunEnabled || G.TestInstanceCount <= 1)
        return;
    if (instanceID < 0 || instanceID >= G.TestInstanceCount)
        return;

    std::lock_guard<std::mutex> lock(G.Mutex);
    if (G.SerialFrame != frame || G.SerialInstance != instanceID)
        return;

    G.SerialInstance++;
    if (G.SerialInstance >= G.TestInstanceCount)
    {
        G.SerialInstance = 0;
        G.SerialFrame++;
    }
    G.BarrierCond.notify_all();
}

melonDS::u64 HashNDS(melonDS::NDS* nds)
{
    // FNV-1a over the state that most quickly reveals gameplay divergence.
    melonDS::u64 hash = 1469598103934665603ull;
    const auto mix = [&](melonDS::u64 value) {
        for (int i = 0; i < 8; i++)
        {
            hash ^= (value >> (i * 8)) & 0xFF;
            hash *= 1099511628211ull;
        }
    };

    mix(nds->NumFrames);
    mix(nds->ARM9Timestamp);
    mix(nds->ARM7Timestamp);
    mix(nds->KeyInput);

    if (nds->MainRAM)
    {
        const melonDS::u32 len = std::min<melonDS::u32>(nds->MainRAMMask + 1, 0x400000);
        for (melonDS::u32 i = 0; i < len; i++)
        {
            hash ^= nds->MainRAM[i];
            hash *= 1099511628211ull;
        }
    }

    return hash;
}

melonDS::u64 HashFramebuffers(melonDS::NDS* nds)
{
    void* topBuffer = nullptr;
    void* bottomBuffer = nullptr;
    if (!nds || !nds->GPU.GetFramebuffers(&topBuffer, &bottomBuffer) || !topBuffer || !bottomBuffer)
        return 0;

    melonDS::u64 hash = 1469598103934665603ull;
    const auto mixBytes = [&](const void* data, std::size_t len) {
        const auto* bytes = reinterpret_cast<const melonDS::u8*>(data);
        for (std::size_t i = 0; i < len; i++)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
    };

    mixBytes(topBuffer, 256 * 192 * 4);
    mixBytes(bottomBuffer, 256 * 192 * 4);
    return hash;
}

melonDS::u64 HashMainRAMRange(melonDS::NDS* nds, melonDS::u32 addr, melonDS::u32 len)
{
    if (!nds || !nds->MainRAM || addr < kMainRAMBase)
        return 0;

    const melonDS::u32 offset = addr - kMainRAMBase;
    const melonDS::u32 ramLen = nds->MainRAMMask + 1;
    if (offset >= ramLen)
        return 0;

    len = std::min(len, ramLen - offset);
    melonDS::u64 hash = 1469598103934665603ull;
    for (melonDS::u32 i = 0; i < len; i++)
    {
        hash ^= nds->MainRAM[offset + i];
        hash *= 1099511628211ull;
    }
    return hash;
}

void MixGameStateValue(melonDS::u64& hash, melonDS::u32 value)
{
    for (int i = 0; i < 4; i++)
    {
        hash ^= (value >> (i * 8)) & 0xFF;
        hash *= 1099511628211ull;
    }
}

void MixGameStateValue(melonDS::u64& hash, melonDS::u64 value)
{
    MixGameStateValue(hash, static_cast<melonDS::u32>(value & 0xFFFFFFFFu));
    MixGameStateValue(hash, static_cast<melonDS::u32>(value >> 32));
}

bool ReadMainRAMU16(melonDS::NDS* nds, melonDS::u32 offset, melonDS::u16& value)
{
    if (!nds || !nds->MainRAM)
        return false;
    if (offset + sizeof(value) > nds->MainRAMMask + 1)
        return false;

    std::memcpy(&value, &nds->MainRAM[offset], sizeof(value));
    return true;
}

bool ReadMainRAMU8(melonDS::NDS* nds, melonDS::u32 offset, melonDS::u8& value)
{
    if (!nds || !nds->MainRAM)
        return false;
    if (offset + sizeof(value) > nds->MainRAMMask + 1)
        return false;

    value = nds->MainRAM[offset];
    return true;
}

bool ReadMainRAMU32(melonDS::NDS* nds, melonDS::u32 offset, melonDS::u32& value)
{
    if (!nds || !nds->MainRAM)
        return false;
    if (offset + sizeof(value) > nds->MainRAMMask + 1)
        return false;

    std::memcpy(&value, &nds->MainRAM[offset], sizeof(value));
    return true;
}

bool WriteMainRAMU32(melonDS::NDS* nds, melonDS::u32 offset, melonDS::u32 value)
{
    if (!nds || !nds->MainRAM)
        return false;
    if (offset + sizeof(value) > nds->MainRAMMask + 1)
        return false;

    std::memcpy(&nds->MainRAM[offset], &value, sizeof(value));
    return true;
}

bool WriteARM9U32(melonDS::NDS* nds, melonDS::u32 addr, melonDS::u32 value)
{
    if (!nds || (addr & 3) != 0)
        return false;

    nds->ARM9Write32(addr, value);
    return true;
}

melonDS::u32 FindObjectBaseByID(melonDS::NDS* nds, melonDS::u16 objectID)
{
    if (!nds || !nds->MainRAM)
        return 0;

    for (melonDS::u32 off = 0x080000; off + 0x80 <= nds->MainRAMMask + 1; off += 4)
    {
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

void EmitARM(std::vector<melonDS::u32>& code, melonDS::u32 instr)
{
    code.push_back(instr);
}

void EmitMovImm(std::vector<melonDS::u32>& code, int reg, melonDS::u32 value)
{
    EmitARM(code, 0xE3A00000u | (static_cast<melonDS::u32>(reg & 0xF) << 12) | (value & 0xFF));
}

void EmitLoadImm(std::vector<melonDS::u32>& code, int reg, melonDS::u32 value)
{
    if (value <= 0xFF)
    {
        EmitMovImm(code, reg, value);
        return;
    }

    EmitARM(code, 0xE59F0000u | (static_cast<melonDS::u32>(reg & 0xF) << 12)); // ldr reg, [pc]
    EmitARM(code, 0xEA000000u); // skip literal
    EmitARM(code, value);
}

void EmitMvnImm(std::vector<melonDS::u32>& code, int reg, melonDS::u32 value)
{
    EmitARM(code, 0xE3E00000u | (static_cast<melonDS::u32>(reg & 0xF) << 12) | (value & 0xFF));
}

void EmitStrR4SP(std::vector<melonDS::u32>& code, melonDS::u32 offset)
{
    EmitARM(code, 0xE58D4000u | (offset & 0xFFF));
}

void EmitStackArg(std::vector<melonDS::u32>& code, melonDS::u32 offset, melonDS::u32 value)
{
    if (value <= 0xFF)
        EmitMovImm(code, 4, value);
    else if (value == 0xFFFFFFFFu)
        EmitMvnImm(code, 4, 0);
    else
        EmitMovImm(code, 4, 0);
    EmitStrR4SP(code, offset);
}

bool InjectDirectMvlBootCall(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.DirectMvlBootEnabled || !nds || instanceID < 0 || instanceID >= 16)
        return false;
    if (G.DirectMvlBootHostOnly && G.NetRole != Role::Host)
        return false;
    if (G.DirectMvlBootClientOnly && G.NetRole != Role::Client)
        return false;
    if (G.DirectMvlBootApplied[instanceID] || frame != G.DirectMvlBootFrame)
        return false;

    const int playerID = std::clamp(
        G.DirectMvlBootPlayerID >= 0 ? G.DirectMvlBootPlayerID : instanceID,
        0,
        1);
    const int scene = std::clamp(G.DirectMvlBootScene, 0, 0xFFFF);
    const int stage = std::clamp(G.DirectMvlBootStage, 0, 4);
    melonDS::u32 vsConnectBase = 0;
    if (G.DirectMvlBootUseLoadGameSM)
    {
        vsConnectBase = FindObjectBaseByID(nds, 0x0006);
        if (vsConnectBase == 0)
        {
            std::printf("NSMB DirectBoot: inst=%d frame=%u loadGameSM skipped: VSConnect object not found\n", instanceID, frame);
            std::fflush(stdout);
            return false;
        }
        WriteARM9U32(nds, vsConnectBase + 0x000, 0x0208489C);
        WriteARM9U32(nds, vsConnectBase + 0x00C, 0x00020006);
        WriteARM9U32(nds, vsConnectBase + 0x078, 0x00000003);
        WriteARM9U32(nds, vsConnectBase + 0x07C, 0x00000003);
        WriteARM9U32(nds, vsConnectBase + 0x114, 0x00000000);
        WriteARM9U32(nds, vsConnectBase + 0x118, kA2DJVSConnectCreateLoadGameSMAddr);
        WriteARM9U32(nds, vsConnectBase + 0x120, kA2DJVSConnectUpdateLoadGameSMAddr);
        WriteARM9U32(nds, vsConnectBase + 0x128, kA2DJVSConnectRenderLoadGameSMAddr);
        WriteARM9U32(nds, vsConnectBase + 0x134, kA2DJVSConnectLoadGameSMSubMenuAddr);
        WriteARM9U32(nds, vsConnectBase + 0x140, playerID == 1 ? 0x00000002 : 0x00000001);
        WriteARM9U32(nds, vsConnectBase + 0x144, 0x00000007);
        WriteARM9U32(nds, vsConnectBase + 0x148, playerID == 1 ? 0x00000027 : 0x00000030);
        WriteARM9U32(nds, vsConnectBase + 0x154, 0x00030000 | static_cast<melonDS::u32>(playerID));
        if (playerID == 1)
        {
            WriteARM9U32(nds, vsConnectBase + 0x13C, 0x00000258);
            WriteARM9U32(nds, vsConnectBase + 0x14C, 0x11BF0900);
            WriteARM9U32(nds, vsConnectBase + 0x150, 0x00003322);
            WriteARM9U32(nds, vsConnectBase + 0x158, 0x00000001);
        }
        WriteARM9U32(nds, 0x020887FC, 0x00000001);
        WriteARM9U32(nds, 0x02088804, 0x00000006);
        WriteARM9U32(nds, 0x02088808, 0x00000002);
        WriteARM9U32(nds, 0x0208880C, 0x00000002);
        WriteARM9U32(nds, kGameLocalPlayerIDAddr, static_cast<melonDS::u32>(playerID));
        WriteARM9U32(nds, kGameVsModeAddr, 0x00000001);
        WriteARM9U32(nds, 0x02085A84, 0x00000001);
        WriteARM9U32(nds, 0x02085A88, 0x0000001C);
        if (G.DirectMvlBootPatchLoadGameSMOnly)
        {
            G.DirectMvlBootApplied[instanceID] = true;
            std::printf(
                "NSMB DirectBoot: inst=%d frame=%u mode=patchLoadGameSM vsConnect=%08X player=%d stage=%d\n",
                instanceID,
                frame,
                vsConnectBase,
                playerID,
                stage);
            std::fflush(stdout);
            return true;
        }
    }
    const melonDS::u32 oldPC = nds->ARM9.R[15] - ((nds->ARM9.CPSR & 0x20) ? 2 : 4);
    const melonDS::u32 returnPC = oldPC | ((nds->ARM9.CPSR & 0x20) ? 1u : 0u);

    std::vector<melonDS::u32> code;
    code.reserve(64);
    EmitARM(code, 0xE92D5FFFu); // push {r0-r12, lr}
    EmitARM(code, 0xE10F5000u); // mrs r5, cpsr
    EmitARM(code, 0xE92D0020u); // push {r5}
    if (!G.DirectMvlBootUseLoadGameSM)
    {
        EmitARM(code, 0xE24DD034u); // sub sp, sp, #0x34
        EmitMovImm(code, 0, static_cast<melonDS::u32>(scene));
        EmitMovImm(code, 1, 0x01); // vs
        EmitMovImm(code, 2, 0x09); // StageGroups::MvsL
        EmitMovImm(code, 3, static_cast<melonDS::u32>(stage));
        EmitStackArg(code, 0x00, 0x00); // act
        EmitStackArg(code, 0x04, static_cast<melonDS::u32>(playerID));
        EmitStackArg(code, 0x08, 0x03); // playerMask
        EmitStackArg(code, 0x0C, 0x00); // character1
        EmitStackArg(code, 0x10, 0x01); // character2
        EmitStackArg(code, 0x14, 0x00); // powerup
        EmitStackArg(code, 0x18, 0xFF); // entrance
        EmitStackArg(code, 0x1C, 0x01); // flag
        EmitStackArg(code, 0x20, 0x01); // unused/control flag observed in VSConnect
        EmitStackArg(code, 0x24, 0xFF);
        EmitStackArg(code, 0x28, 0x00);
        EmitStackArg(code, 0x2C, 0x00);
        EmitStackArg(code, 0x30, 0xFFFFFFFFu); // rngSeed: let NSMB keep its random path
        EmitARM(code, 0xE59FC008u); // ldr ip, [pc, #8]
        EmitARM(code, 0xE28FE008u); // add lr, pc, #8
        EmitARM(code, 0xE12FFF1Cu); // bx ip
        EmitARM(code, 0xE1A00000u); // nop
        EmitARM(code, kA2DJGameLoadLevelAddr);
    }
    else
    {
        if (G.DirectMvlBootCallObjectCourseSelect)
        {
            EmitMovImm(code, 0, 0x05);
            EmitMovImm(code, 1, 0x00);
            EmitMovImm(code, 2, 0x01);
            EmitMovImm(code, 3, 0x01);
            EmitARM(code, 0xE59FC008u); // ldr ip, [pc, #8]
            EmitARM(code, 0xE28FE008u); // add lr, pc, #8
            EmitARM(code, 0xE12FFF1Cu); // bx ip
            EmitARM(code, 0xE1A00000u); // nop
            EmitARM(code, kA2DJCreateObjectAddr);
        }
        else
        {
            if (G.DirectMvlBootCallStartLoadLevel)
            {
                WriteARM9U32(nds, vsConnectBase + 0x218 + 0x008, 0x00000001);
                WriteARM9U32(nds, vsConnectBase + 0x218 + 0x064, 0x00000409);
                EmitLoadImm(code, 0, vsConnectBase + 0x218);
                EmitLoadImm(code, 1, kA2DJVSConnectStartLoadLevelAddr);
                EmitLoadImm(code, 2, 0);
                EmitLoadImm(code, 3, 0x02156488);
                EmitARM(code, 0xE59FC008u); // ldr ip, [pc, #8]
                EmitARM(code, 0xE28FE008u); // add lr, pc, #8
                EmitARM(code, 0xE12FFF1Cu); // bx ip
                EmitARM(code, 0xE1A00000u); // nop
                EmitARM(code, kA2DJVSConnectStartLoadLevelAddr);
                EmitLoadImm(code, 4, 0x01);
                EmitLoadImm(code, 6, 0x020887FC);
                EmitARM(code, 0xE5864000u); // str r4, [r6]
                EmitLoadImm(code, 4, 0x06);
                EmitLoadImm(code, 6, 0x02088804);
                EmitARM(code, 0xE5864000u);
                EmitLoadImm(code, 4, 0x02);
                EmitLoadImm(code, 6, 0x02088808);
                EmitARM(code, 0xE5864000u);
                EmitLoadImm(code, 4, 0x02);
                EmitLoadImm(code, 6, 0x0208880C);
                EmitARM(code, 0xE5864000u);
                EmitLoadImm(code, 4, 0x00400150);
                EmitLoadImm(code, 6, 0x02088858);
                EmitARM(code, 0xE5864000u);
            }
            else
            {
                EmitARM(code, 0xE59F000Cu); // ldr r0, [pc, #12]
                EmitARM(code, 0xE59FC00Cu); // ldr ip, [pc, #12]
                EmitARM(code, 0xE28FE00Cu); // add lr, pc, #12
                EmitARM(code, 0xE12FFF1Cu); // bx ip
                EmitARM(code, 0xE1A00000u); // nop
                EmitARM(code, vsConnectBase);
                EmitARM(code, G.DirectMvlBootCallCreateCourseSelect
                    ? kA2DJVSCreateCourseSelectAddr
                    : G.DirectMvlBootCallUpdateLoadGameSM
                    ? kA2DJVSConnectUpdateLoadGameSMAddr
                    : kA2DJVSConnectCreateLoadGameSMAddr);
            }
        }
    }
    if (!G.DirectMvlBootUseLoadGameSM)
        EmitARM(code, 0xE28DD034u); // add sp, sp, #0x34
    EmitARM(code, 0xE8BD0020u); // pop {r5}
    EmitARM(code, 0xE128F005u); // msr apsr_nzcvq, r5
    EmitARM(code, 0xE8BD5FFFu); // pop {r0-r12, lr}
    EmitARM(code, 0xE59FC004u); // ldr ip, [pc, #4]
    EmitARM(code, 0xE12FFF1Cu); // bx ip
    EmitARM(code, 0xE1A00000u); // nop
    EmitARM(code, returnPC);

    for (size_t i = 0; i < code.size(); i++)
    {
        if (!WriteARM9U32(nds, kDirectBootTrampolineAddr + static_cast<melonDS::u32>(i * sizeof(melonDS::u32)), code[i]))
            return false;
    }

    G.DirectMvlBootApplied[instanceID] = true;
    std::printf(
        "NSMB DirectBoot: inst=%d frame=%u trampoline=%08X return=%08X mode=%s vsConnect=%08X scene=%d player=%d stage=%d\n",
        instanceID,
        frame,
        kDirectBootTrampolineAddr,
        returnPC,
        G.DirectMvlBootUseLoadGameSM
            ? (G.DirectMvlBootCallObjectCourseSelect
                ? "objectCourseSelect"
                : G.DirectMvlBootCallCreateCourseSelect
                ? "createCourseSelect"
                : G.DirectMvlBootCallStartLoadLevel
                ? "startLoadLevel"
                : (G.DirectMvlBootCallUpdateLoadGameSM ? "updateLoadGameSM" : "loadGameSM"))
            : "loadLevel",
        vsConnectBase,
        scene,
        playerID,
        stage);
    std::fflush(stdout);
    nds->ARM9.JumpTo(kDirectBootTrampolineAddr);
    return true;
}

bool InjectCourseSelectFactoryCall(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForceCourseSelectFactory || !nds || instanceID < 0 || instanceID >= 16)
        return false;
    if (G.ForceCourseSelectFactoryApplied[instanceID] || frame != G.ForceCourseSelectFactoryFrame)
        return false;
    if (nds->ARM9Read32(kGameVsModeAddr) != 1 || nds->ARM9Read32(kGameStageGroupAddr) == 9)
        return false;
    if (!IsMarioVsLuigiGGID(nds->ARM9Read32(kNetGGIDAddr)))
        return false;
    if (FindObjectBaseByID(nds, kCourseSelectObjectID) != 0)
        return false;

    const int playerArg = std::clamp(
        G.ForceCourseSelectFactoryPlayerArg >= 0 ? G.ForceCourseSelectFactoryPlayerArg : instanceID,
        0,
        1);
    nds->ARM9Write8(0x02088554u, 0x00u);
    nds->ARM9Write16(0x02084FB4u, 0x0005u);
    nds->ARM9Write8(0x0203B480u, nds->ARM9Read8(0x0203B480u) | 0x40u);
    WriteARM9U32(nds, 0x02088558u, static_cast<melonDS::u32>(playerArg));
    WriteARM9U32(nds, 0x02186A88u, 0x00000003u);

    const melonDS::u32 oldPC = nds->ARM9.R[15] - ((nds->ARM9.CPSR & 0x20) ? 2 : 4);
    const melonDS::u32 returnPC = oldPC | ((nds->ARM9.CPSR & 0x20) ? 1u : 0u);

    std::vector<melonDS::u32> code;
    code.reserve(32);
    const auto emitBL = [&code](melonDS::u32 target)
    {
        const melonDS::u32 pc = kDirectBootTrampolineAddr + static_cast<melonDS::u32>(code.size() * sizeof(melonDS::u32)) + 8u;
        const melonDS::s32 offset = static_cast<melonDS::s32>(target - pc) >> 2;
        EmitARM(code, 0xEB000000u | (static_cast<melonDS::u32>(offset) & 0x00FFFFFFu));
    };

    EmitARM(code, 0xE92D5FFFu); // push {r0-r12, lr}
    EmitARM(code, 0xE10F5000u); // mrs r5, cpsr
    EmitARM(code, 0xE92D0020u); // push {r5}
    EmitARM(code, 0xE59F0030u); // ldr r0, [pc, #48]
    EmitMovImm(code, 1, 0x01);
    EmitARM(code, 0xE59F202Cu); // ldr r2, [pc, #44]
    EmitARM(code, 0xE59F302Cu); // ldr r3, [pc, #44]
    emitBL(kA2DJApplySceneRequestAddr);
    EmitMovImm(code, 0, 0x1E);
    emitBL(kA2DJStartSceneTransitionAddr);
    EmitARM(code, 0xE8BD0020u); // pop {r5}
    EmitARM(code, 0xE128F005u); // msr apsr_nzcvq, r5
    EmitARM(code, 0xE8BD5FFFu); // pop {r0-r12, lr}
    EmitARM(code, 0xE59FC004u); // ldr ip, [pc, #4]
    EmitARM(code, 0xE12FFF1Cu); // bx ip
    EmitARM(code, 0xE1A00000u); // nop
    EmitARM(code, returnPC);
    EmitARM(code, 0x02088568u);
    EmitARM(code, 0x02088558u);
    EmitARM(code, 0x02084FB4u);

    for (size_t i = 0; i < code.size(); i++)
    {
        if (!WriteARM9U32(nds, kDirectBootTrampolineAddr + static_cast<melonDS::u32>(i * sizeof(melonDS::u32)), code[i]))
            return false;
    }

    G.ForceCourseSelectFactoryApplied[instanceID] = true;
    std::printf(
        "NSMB CourseSelectFactory: inst=%d frame=%u trampoline=%08X return=%08X playerArg=%d\n",
        instanceID,
        frame,
        kDirectBootTrampolineAddr,
        returnPC,
        playerArg);
    std::fflush(stdout);
    nds->ARM9.JumpTo(kDirectBootTrampolineAddr);
    return true;
}

void ReadObjectTransform(melonDS::NDS* nds, melonDS::u32 off, ObjectScanSample& sample)
{
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

ObjectScanSample FindVsBattleStarCandidate(melonDS::NDS* nds)
{
    ObjectScanSample sample;
    if (!nds || !nds->MainRAM)
        return sample;

    const melonDS::u32 ramLen = nds->MainRAMMask + 1;
    if (ramLen < 0x120)
        return sample;

    for (melonDS::u32 off = 0; off <= ramLen - 0x120; off += 4)
    {
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

ObjectScanSample FindObjectByIDAndSettings(melonDS::NDS* nds, melonDS::u16 expectedObjectID, melonDS::u32 expectedSettings)
{
    ObjectScanSample sample;
    if (!nds || !nds->MainRAM)
        return sample;

    const melonDS::u32 ramLen = nds->MainRAMMask + 1;
    if (ramLen < 0x120)
        return sample;

    for (melonDS::u32 off = 0; off <= ramLen - 0x120; off += 4)
    {
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

ObjectScanSample FindObjectByID(melonDS::NDS* nds, melonDS::u16 expectedObjectID)
{
    ObjectScanSample sample;
    if (!nds || !nds->MainRAM)
        return sample;

    const melonDS::u32 ramLen = nds->MainRAMMask + 1;
    if (ramLen < 0x120)
        return sample;

    for (melonDS::u32 off = 0; off <= ramLen - 0x120; off += 4)
    {
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

ObjectScanSample FindObjectByIDLoose(melonDS::NDS* nds, melonDS::u16 expectedObjectID)
{
    ObjectScanSample sample;
    if (!nds || !nds->MainRAM)
        return sample;

    const melonDS::u32 ramLen = nds->MainRAMMask + 1;
    if (ramLen < 0x120)
        return sample;

    for (melonDS::u32 off = 0; off <= ramLen - 0x120; off += 4)
    {
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

ObjectScanSample FindObjectByIDAndSettingsLoose(melonDS::NDS* nds, melonDS::u16 expectedObjectID, melonDS::u32 expectedSettings)
{
    ObjectScanSample sample;
    if (!nds || !nds->MainRAM)
        return sample;

    const melonDS::u32 ramLen = nds->MainRAMMask + 1;
    if (ramLen < 0x120)
        return sample;

    for (melonDS::u32 off = 0; off <= ramLen - 0x120; off += 4)
    {
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

void InsertPlayerActorByGUID(PlayerActorScanSample& players, const ObjectScanSample& actor)
{
    if (!actor.Found)
        return;
    if (!players.Actor0.Found || actor.GUID < players.Actor0.GUID)
    {
        players.Actor1 = players.Actor0;
        players.Actor0 = actor;
        return;
    }
    if ((!players.Actor1.Found || actor.GUID < players.Actor1.GUID) &&
        actor.GUID != players.Actor0.GUID)
    {
        players.Actor1 = actor;
    }
}

PlayerActorScanSample FindPlayerActors(melonDS::NDS* nds)
{
    PlayerActorScanSample players;
    if (!nds || !nds->MainRAM)
        return players;

    const melonDS::u32 ramLen = nds->MainRAMMask + 1;
    if (ramLen < 0x120)
        return players;

    for (melonDS::u32 off = 0; off <= ramLen - 0x120; off += 4)
    {
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

void ForcePlayerActorIDsIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForcePlayerActorIDsEnabled || !nds)
        return;
    if (G.ForcePlayerActorIDsHostOnly && G.NetRole != Role::Host)
        return;
    if (G.ForcePlayerActorIDsClientOnly && G.NetRole != Role::Client)
        return;
    if (frame < G.ForcePlayerActorIDsStartFrame)
        return;
    if (G.ForcePlayerActorIDsEndFrame != 0 && frame > G.ForcePlayerActorIDsEndFrame)
        return;

    const PlayerActorScanSample players = FindPlayerActors(nds);
    if (players.Actor0.Found)
        nds->ARM9Write8(players.Actor0.Base + 0x11E, 0);
    if (players.Actor1.Found)
        nds->ARM9Write8(players.Actor1.Base + 0x11E, 1);

    if (instanceID >= 0 && instanceID < 16 && !G.ForcePlayerActorIDsLogged[instanceID])
    {
        G.ForcePlayerActorIDsLogged[instanceID] = true;
        std::printf(
            "NSMB Test: force player actor IDs inst=%d frame=%u range=%u-%u p0=%08X p1=%08X\n",
            instanceID,
            frame,
            G.ForcePlayerActorIDsStartFrame,
            G.ForcePlayerActorIDsEndFrame,
            players.Actor0.Base,
            players.Actor1.Base);
    }
}

void ForcePlayerTransitionStatusIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForcePlayerTransitionStatusEnabled || !nds)
        return;
    if (G.ForcePlayerTransitionStatusHostOnly && G.NetRole != Role::Host)
        return;
    if (G.ForcePlayerTransitionStatusClientOnly && G.NetRole != Role::Client)
        return;
    if (frame < G.ForcePlayerTransitionStatusStartFrame)
        return;
    if (G.ForcePlayerTransitionStatusEndFrame != 0 && frame > G.ForcePlayerTransitionStatusEndFrame)
        return;

    const melonDS::u32 value = G.ForcePlayerTransitionStatusValue;
    nds->ARM9Write32(kGamePlayerTransitionStatusAddr, value);
    nds->ARM9Write32(kGamePlayerTransitionStatusAddr + sizeof(melonDS::u32), value);

    if (instanceID >= 0 && instanceID < 16 && !G.ForcePlayerTransitionStatusLogged[instanceID])
    {
        G.ForcePlayerTransitionStatusLogged[instanceID] = true;
        std::printf(
            "NSMB Test: force player transition status inst=%d frame=%u range=%u-%u value=0x%08X\n",
            instanceID,
            frame,
            G.ForcePlayerTransitionStatusStartFrame,
            G.ForcePlayerTransitionStatusEndFrame,
            value);
    }
}

void ForceEntranceSpawnPointersIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForceEntranceSpawnPointersEnabled || !nds)
        return;
    if (G.ForceEntranceSpawnPointersHostOnly && G.NetRole != Role::Host)
        return;
    if (G.ForceEntranceSpawnPointersClientOnly && G.NetRole != Role::Client)
        return;
    if (frame < G.ForceEntranceSpawnPointersStartFrame)
        return;
    if (G.ForceEntranceSpawnPointersEndFrame != 0 && frame > G.ForceEntranceSpawnPointersEndFrame)
        return;

    nds->ARM9Write8(kEntranceSpawnEntranceIDAddr, static_cast<melonDS::u8>(G.ForceEntranceSpawnID0 & 0xFF));
    nds->ARM9Write8(kEntranceSpawnEntranceIDAddr + 1, static_cast<melonDS::u8>(G.ForceEntranceSpawnID1 & 0xFF));
    nds->ARM9Write8(kEntranceTransitionFlagsAddr, 0);
    nds->ARM9Write8(kEntranceTransitionFlagsAddr + 1, 0);
    nds->ARM9Write32(kEntranceSpawnEntranceAddr, G.ForceEntranceSpawnPtr0);
    nds->ARM9Write32(kEntranceSpawnEntranceAddr + sizeof(melonDS::u32), G.ForceEntranceSpawnPtr1);

    if (instanceID >= 0 && instanceID < 16 && !G.ForceEntranceSpawnPointersLogged[instanceID])
    {
        G.ForceEntranceSpawnPointersLogged[instanceID] = true;
        std::printf(
            "NSMB Test: force entrance spawn pointers inst=%d frame=%u range=%u-%u ptr0=%08X ptr1=%08X id0=%u id1=%u\n",
            instanceID,
            frame,
            G.ForceEntranceSpawnPointersStartFrame,
            G.ForceEntranceSpawnPointersEndFrame,
            G.ForceEntranceSpawnPtr0,
            G.ForceEntranceSpawnPtr1,
            G.ForceEntranceSpawnID0,
            G.ForceEntranceSpawnID1);
    }
}

ObjectLifecycleSummary SummarizeObjectLifecycle(melonDS::NDS* nds)
{
    ObjectLifecycleSummary summary;
    if (!nds || !nds->MainRAM)
        return summary;

    const melonDS::u32 ramLen = nds->MainRAMMask + 1;
    if (ramLen < 0x5C)
        return summary;

    for (melonDS::u32 off = 0; off <= ramLen - 0x5C; off += 4)
    {
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
        if (state == 0)
        {
            summary.NotCreated++;
            const melonDS::u32 base = kMainRAMBase + off;
            const melonDS::u32 flags =
                (static_cast<melonDS::u32>(type) << 16) |
                (static_cast<melonDS::u32>(skipFlags) << 24);
            if (summary.FirstNotCreatedBase == 0)
            {
                summary.FirstNotCreatedID = objectID;
                summary.FirstNotCreatedBase = base;
                summary.FirstNotCreatedFlags = flags;
            }
            else if (summary.SecondNotCreatedBase == 0)
            {
                summary.SecondNotCreatedID = objectID;
                summary.SecondNotCreatedBase = base;
                summary.SecondNotCreatedFlags = flags;
            }
        }
        else if (state == 1)
        {
            const melonDS::u32 index = summary.Active;
            if (index < static_cast<melonDS::u32>(kObjectTraceSlots))
            {
                summary.ActiveID[index] = objectID;
                summary.ActiveSettings[index] = settings;
                summary.ActiveBase[index] = kMainRAMBase + off;
            }
            summary.Active++;
        }
        else if (state == 2)
        {
            summary.Dead++;
        }

        if ((skipFlags & 0x02) != 0)
            summary.SkipUpdate++;
        if ((skipFlags & 0x08) != 0)
            summary.SkipRender++;
    }

    return summary;
}

void ApplyVsStarSnap(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (G.VsStarSnapFrame == 0) return;
    if (frame != G.VsStarSnapFrame) return;
    if (instanceID < 0 || instanceID >= 16) return;
    if (G.VsStarSnapApplied[instanceID]) return;
    if (!nds || !nds->MainRAM) return;

    ObjectScanSample star = FindObjectByIDAndSettings(nds, kVsBattleStarActorObjectID, kVsBattleStarActorSettings);
    if (!star.Found)
        star = FindVsBattleStarCandidate(nds);
    const PlayerActorScanSample players = FindPlayerActors(nds);
    const ObjectScanSample& player = (G.VsStarSnapPlayerSlot == 1) ? players.Actor1 : players.Actor0;
    if (!star.Found || !player.Found)
    {
        std::printf("NSMB Test: VS star snap skipped inst=%d frame=%u star=%u player=%u\n",
            instanceID,
            frame,
            star.Found,
            player.Found);
        G.VsStarSnapApplied[instanceID] = true;
        return;
    }

    const melonDS::u32 starOffset = star.Base - kMainRAMBase;
    WriteMainRAMU32(nds, starOffset + 0x60, player.PosX);
    WriteMainRAMU32(nds, starOffset + 0x64, player.PosY);
    WriteMainRAMU32(nds, starOffset + 0x68, player.PosZ);
    G.VsStarSnapApplied[instanceID] = true;

    std::printf("NSMB Test: snapped VS star to player inst=%d frame=%u slot=%d starGuid=0x%X playerGuid=0x%X pos=0x%08X,0x%08X,0x%08X\n",
        instanceID,
        frame,
        G.VsStarSnapPlayerSlot,
        star.GUID,
        player.GUID,
        player.PosX,
        player.PosY,
        player.PosZ);
}

void WriteObjectTransform(melonDS::NDS* nds, const ObjectScanSample& actor, melonDS::u32 posX, melonDS::u32 posY, melonDS::u32 posZ, bool clearVelocity)
{
    if (!nds || !nds->MainRAM || !actor.Found || actor.Base < kMainRAMBase)
        return;

    const melonDS::u32 off = actor.Base - kMainRAMBase;
    WriteMainRAMU32(nds, off + 0x60, posX);
    WriteMainRAMU32(nds, off + 0x64, posY);
    WriteMainRAMU32(nds, off + 0x68, posZ);
    WriteMainRAMU32(nds, off + 0x70, posX);
    WriteMainRAMU32(nds, off + 0x74, posY);
    WriteMainRAMU32(nds, off + 0x78, posZ);
    if (clearVelocity)
    {
        WriteMainRAMU32(nds, off + 0x80, 0);
        WriteMainRAMU32(nds, off + 0x84, 0);
        WriteMainRAMU32(nds, off + 0x88, 0);
        WriteMainRAMU32(nds, off + 0xD0, 0);
        WriteMainRAMU32(nds, off + 0xD4, 0);
        WriteMainRAMU32(nds, off + 0xD8, 0);
    }
}

void ForcePlayerActorPositionIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForcePlayerActorPositionEnabled || !nds)
        return;
    if (frame < G.ForcePlayerActorPositionStartFrame)
        return;
    if (G.ForcePlayerActorPositionEndFrame != 0 && frame > G.ForcePlayerActorPositionEndFrame)
        return;

    const PlayerActorScanSample players = FindPlayerActors(nds);
    const int slot = std::clamp(G.ForcePlayerActorPositionSlot, 0, 1);
    const ObjectScanSample& player = slot == 1 ? players.Actor1 : players.Actor0;
    if (!player.Found)
        return;

    WriteObjectTransform(
        nds,
        player,
        G.ForcePlayerActorPositionX,
        G.ForcePlayerActorPositionY,
        G.ForcePlayerActorPositionZ,
        true);
    if (G.ForcePlayerActorPositionCharacterSet)
        nds->ARM9Write16(player.Base + kPlayerBaseCharacterIDOffset, G.ForcePlayerActorPositionCharacter);
    if (G.ForcePlayerActorPositionPlayerIDSet)
        nds->ARM9Write8(player.Base + kPlayerBasePlayerIDOffset, G.ForcePlayerActorPositionPlayerID);

    if (instanceID >= 0 && instanceID < 16 && !G.ForcePlayerActorPositionLogged[instanceID])
    {
        G.ForcePlayerActorPositionLogged[instanceID] = true;
        std::printf(
            "NSMB Test: force player actor position inst=%d frame=%u range=%u-%u slot=%d base=%08X pos=%08X,%08X,%08X characterSet=%d character=%u playerIDSet=%d playerID=%u\n",
            instanceID,
            frame,
            G.ForcePlayerActorPositionStartFrame,
            G.ForcePlayerActorPositionEndFrame,
            slot,
            player.Base,
            G.ForcePlayerActorPositionX,
            G.ForcePlayerActorPositionY,
            G.ForcePlayerActorPositionZ,
            G.ForcePlayerActorPositionCharacterSet ? 1 : 0,
            G.ForcePlayerActorPositionCharacter,
            G.ForcePlayerActorPositionPlayerIDSet ? 1 : 0,
            G.ForcePlayerActorPositionPlayerID);
    }
}

bool ReadObjectWordByIDAndSettings(
    melonDS::NDS* nds,
    melonDS::u16 expectedObjectID,
    melonDS::u32 expectedSettings,
    melonDS::u32 relativeOffset,
    melonDS::u32& value)
{
    const ObjectScanSample actor = FindObjectByIDAndSettings(nds, expectedObjectID, expectedSettings);
    if (!actor.Found || actor.Base < kMainRAMBase)
        return false;
    return ReadMainRAMU32(nds, actor.Base - kMainRAMBase + relativeOffset, value);
}

bool WriteObjectWordByIDAndSettings(
    melonDS::NDS* nds,
    melonDS::u16 expectedObjectID,
    melonDS::u32 expectedSettings,
    melonDS::u32 relativeOffset,
    melonDS::u32 value)
{
    const ObjectScanSample actor = FindObjectByIDAndSettings(nds, expectedObjectID, expectedSettings);
    if (!actor.Found || actor.Base < kMainRAMBase)
        return false;
    WriteMainRAMU32(nds, actor.Base - kMainRAMBase + relativeOffset, value);
    return true;
}

void ApplyPlayerSnapToStar(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (G.PlayerSnapToStarFrame == 0) return;
    if (frame != G.PlayerSnapToStarFrame) return;
    if (instanceID < 0 || instanceID >= 16) return;
    if (G.PlayerSnapToStarApplied[instanceID]) return;
    if (!nds || !nds->MainRAM) return;

    ObjectScanSample star = FindObjectByIDAndSettings(nds, kVsBattleStarActorObjectID, kVsBattleStarActorSettings);
    if (!star.Found)
        star = FindVsBattleStarCandidate(nds);
    const PlayerActorScanSample players = FindPlayerActors(nds);
    const ObjectScanSample& player = (G.PlayerSnapToStarSlot == 1) ? players.Actor1 : players.Actor0;
    if (!star.Found || !player.Found)
    {
        std::printf("NSMB Test: player snap to VS star skipped inst=%d frame=%u star=%u player=%u\n",
            instanceID,
            frame,
            star.Found,
            player.Found);
        G.PlayerSnapToStarApplied[instanceID] = true;
        return;
    }

    WriteObjectTransform(nds, player, star.PosX, star.PosY, star.PosZ, true);
    G.PlayerSnapToStarApplied[instanceID] = true;

    std::printf("NSMB Test: snapped player to VS star inst=%d frame=%u slot=%d playerGuid=0x%X starGuid=0x%X pos=0x%08X,0x%08X,0x%08X\n",
        instanceID,
        frame,
        G.PlayerSnapToStarSlot,
        player.GUID,
        star.GUID,
        star.PosX,
        star.PosY,
        star.PosZ);
}

void ApplyPlayerStickToStar(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (G.PlayerStickToStarStartFrame == 0 && G.PlayerStickToStarEndFrame == 0) return;
    if (frame < G.PlayerStickToStarStartFrame || frame > G.PlayerStickToStarEndFrame) return;
    if (instanceID < 0 || instanceID >= 16) return;
    if (!nds || !nds->MainRAM) return;

    ObjectScanSample star = FindObjectByIDAndSettings(nds, kVsBattleStarActorObjectID, kVsBattleStarActorSettings);
    if (!star.Found)
        star = FindVsBattleStarCandidate(nds);
    const PlayerActorScanSample players = FindPlayerActors(nds);
    const ObjectScanSample& player = (G.PlayerStickToStarSlot == 1) ? players.Actor1 : players.Actor0;
    if (!star.Found || !player.Found)
    {
        if (frame == G.PlayerStickToStarStartFrame)
        {
            std::printf("NSMB Test: player stick to VS star skipped inst=%d frame=%u star=%u player=%u\n",
                instanceID,
                frame,
                star.Found,
                player.Found);
        }
        return;
    }

    WriteObjectTransform(nds, player, star.PosX, star.PosY, star.PosZ, true);
    if (frame == G.PlayerStickToStarStartFrame)
    {
        std::printf("NSMB Test: started player stick to VS star inst=%d frame=%u-%u slot=%d playerGuid=0x%X starGuid=0x%X pos=0x%08X,0x%08X,0x%08X\n",
            instanceID,
            G.PlayerStickToStarStartFrame,
            G.PlayerStickToStarEndFrame,
            G.PlayerStickToStarSlot,
            player.GUID,
            star.GUID,
            star.PosX,
            star.PosY,
            star.PosZ);
    }
}

void ForcePlayerCountIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForcePlayerCountEnabled || !nds)
        return;
    if (frame < G.ForcePlayerCountStartFrame)
        return;
    if (G.ForcePlayerCountEndFrame != 0 && frame > G.ForcePlayerCountEndFrame)
        return;
    if (G.ForcePlayerCountHostOnly && G.NetRole != Role::Host)
        return;
    if (G.ForcePlayerCountClientOnly && G.NetRole != Role::Client)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;

    nds->ARM9Write32(kGamePlayerCountAddr, G.ForcePlayerCountValue);
    if (!G.ForcePlayerCountLogged[instanceID])
    {
        std::printf("NSMB Test: force playerCount inst=%d frame=%u value=%u range=%u-%u\n",
            instanceID,
            frame,
            G.ForcePlayerCountValue,
            G.ForcePlayerCountStartFrame,
            G.ForcePlayerCountEndFrame);
        G.ForcePlayerCountLogged[instanceID] = true;
    }
}

void ForceStageSceneRuntimeWordsIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForceStageSceneRuntimeWordsEnabled || !nds || !nds->MainRAM)
        return;
    if (frame < G.ForceStageSceneRuntimeWordsStartFrame)
        return;
    if (G.ForceStageSceneRuntimeWordsEndFrame != 0 && frame > G.ForceStageSceneRuntimeWordsEndFrame)
        return;
    if (G.ForceStageSceneRuntimeWordsHostOnly && G.NetRole != Role::Host)
        return;
    if (G.ForceStageSceneRuntimeWordsClientOnly && G.NetRole != Role::Client)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;

    const bool wrote154 = WriteObjectWordByIDAndSettings(
        nds,
        kStageSceneObjectID,
        kMvlStageSceneSettings,
        0x154,
        G.ForceStageSceneWord154);
    const bool wrote160 = WriteObjectWordByIDAndSettings(
        nds,
        kStageSceneObjectID,
        kMvlStageSceneSettings,
        0x160,
        G.ForceStageSceneWord160);
    if (!G.ForceStageSceneRuntimeWordsLogged[instanceID])
    {
        std::printf("NSMB Test: force stage scene runtime words inst=%d frame=%u range=%u-%u word154=0x%08X word160=0x%08X wrote=%d/%d\n",
            instanceID,
            frame,
            G.ForceStageSceneRuntimeWordsStartFrame,
            G.ForceStageSceneRuntimeWordsEndFrame,
            G.ForceStageSceneWord154,
            G.ForceStageSceneWord160,
            wrote154 ? 1 : 0,
            wrote160 ? 1 : 0);
        G.ForceStageSceneRuntimeWordsLogged[instanceID] = true;
    }
}

void ForceStageSceneActiveIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForceStageSceneActiveEnabled || !nds || !nds->MainRAM)
        return;
    if (frame < G.ForceStageSceneActiveStartFrame)
        return;
    if (G.ForceStageSceneActiveEndFrame != 0 && frame > G.ForceStageSceneActiveEndFrame)
        return;
    if (G.ForceStageSceneActiveHostOnly && G.NetRole != Role::Host)
        return;
    if (G.ForceStageSceneActiveClientOnly && G.NetRole != Role::Client)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1)
        return;

    const ObjectScanSample stageScene = FindObjectByIDAndSettingsLoose(nds, kStageSceneObjectID, kMvlStageSceneSettings);
    if (!stageScene.Found || !IsARM9MainRAMAddress(stageScene.Base))
    {
        if (!G.ForceStageSceneActiveLogged[instanceID])
        {
            std::printf("NSMB Test: force stage scene active skipped inst=%d frame=%u no stage scene\n",
                instanceID,
                frame);
            G.ForceStageSceneActiveLogged[instanceID] = true;
        }
        return;
    }

    nds->ARM9Write8(stageScene.Base + 0x0E, 1);
    nds->ARM9Write8(stageScene.Base + 0x13, 0);
    nds->ARM9Write32(stageScene.Base + 0x154, G.ForceStageSceneWord154);
    nds->ARM9Write32(stageScene.Base + 0x160, G.ForceStageSceneWord160);

    if (!G.ForceStageSceneActiveLogged[instanceID])
    {
        std::printf(
            "NSMB Test: force stage scene active inst=%d frame=%u range=%u-%u base=%08X word154=0x%08X word160=0x%08X\n",
            instanceID,
            frame,
            G.ForceStageSceneActiveStartFrame,
            G.ForceStageSceneActiveEndFrame,
            stageScene.Base,
            G.ForceStageSceneWord154,
            G.ForceStageSceneWord160);
        G.ForceStageSceneActiveLogged[instanceID] = true;
    }
}

void ForceStageCameraSlotIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForceStageCameraSlotEnabled || !nds || !nds->MainRAM)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (frame < G.ForceStageCameraSlotStartFrame)
        return;
    if (G.ForceStageCameraSlotEndFrame != 0 && frame > G.ForceStageCameraSlotEndFrame)
        return;

    const int src = std::clamp(G.ForceStageCameraSlotSource, 0, 1);
    const int dst = std::clamp(G.ForceStageCameraSlotDest, 0, 1);
    if (src == dst)
        return;

    melonDS::u32 x = nds->ARM9Read32(kStageCameraXAddr + sizeof(melonDS::u32) * src);
    melonDS::u32 y = nds->ARM9Read32(kStageCameraYAddr + sizeof(melonDS::u32) * src);
    melonDS::u32 width = nds->ARM9Read32(kStageCameraWidthAddr + sizeof(melonDS::u32) * src);
    melonDS::u32 height = nds->ARM9Read32(kStageCameraHeightAddr + sizeof(melonDS::u32) * src);
    if (G.ForceStageCameraSlotOverrideX) x = G.ForceStageCameraSlotX;
    if (G.ForceStageCameraSlotOverrideY) y = G.ForceStageCameraSlotY;
    if (G.ForceStageCameraSlotOverrideWidth) width = G.ForceStageCameraSlotWidth;
    if (G.ForceStageCameraSlotOverrideHeight) height = G.ForceStageCameraSlotHeight;
    if (width == 0 || height == 0)
        return;

    if (!G.ForceStageCameraSlotVerticalOnly)
        nds->ARM9Write32(kStageCameraXAddr + sizeof(melonDS::u32) * dst, x);
    nds->ARM9Write32(kStageCameraYAddr + sizeof(melonDS::u32) * dst, y);
    if (!G.ForceStageCameraSlotVerticalOnly)
        nds->ARM9Write32(kStageCameraWidthAddr + sizeof(melonDS::u32) * dst, width);
    nds->ARM9Write32(kStageCameraHeightAddr + sizeof(melonDS::u32) * dst, height);

    if (!G.ForceStageCameraSlotLogged[instanceID])
    {
        std::printf(
            "NSMB Test: mirror Stage camera slot inst=%d frame=%u range=%u-%u src=%d dst=%d "
            "x=%08X y=%08X width=%08X height=%08X verticalOnly=%d override=%d/%d/%d/%d\n",
            instanceID,
            frame,
            G.ForceStageCameraSlotStartFrame,
            G.ForceStageCameraSlotEndFrame,
            src,
            dst,
            x,
            y,
            width,
            height,
            G.ForceStageCameraSlotVerticalOnly ? 1 : 0,
            G.ForceStageCameraSlotOverrideX ? 1 : 0,
            G.ForceStageCameraSlotOverrideY ? 1 : 0,
            G.ForceStageCameraSlotOverrideWidth ? 1 : 0,
            G.ForceStageCameraSlotOverrideHeight ? 1 : 0);
        G.ForceStageCameraSlotLogged[instanceID] = true;
    }
}

void ForceStageCameraObjectXIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForceStageCameraObjectXEnabled || !nds || !nds->MainRAM)
        return;
    if (frame < G.ForceStageCameraObjectXStartFrame)
        return;
    if (G.ForceStageCameraObjectXEndFrame != 0 && frame > G.ForceStageCameraObjectXEndFrame)
        return;

    ObjectScanSample stageCamera = FindObjectByIDAndSettings(nds, kStageCameraObjectID, 0);
    if (!stageCamera.Found)
        stageCamera = FindObjectByIDAndSettingsLoose(nds, kStageCameraObjectID, 0);
    if (!stageCamera.Found || !IsARM9MainRAMAddress(stageCamera.Base + 0x0E0))
        return;

    nds->ARM9Write32(stageCamera.Base + 0x0D0, G.ForceStageCameraObjectX);
    nds->ARM9Write32(stageCamera.Base + 0x0E0, G.ForceStageCameraObjectX);
    if (G.ForceStageCameraObjectZEnabled)
    {
        nds->ARM9Write32(stageCamera.Base + 0x0D4, G.ForceStageCameraObjectZ);
        nds->ARM9Write32(stageCamera.Base + 0x0E4, G.ForceStageCameraObjectZ);
    }
    if (G.ForceStageCameraObjectXWriteDisplay)
        nds->ARM9Write32(kStageDisplayCameraXAddr, G.ForceStageCameraObjectX);
    if (G.ForceStageCameraObjectXWriteSlot)
    {
        const int slot = std::clamp(G.ForceStageCameraObjectXSlot, 0, 1);
        nds->ARM9Write32(kStageCameraXAddr + sizeof(melonDS::u32) * slot, G.ForceStageCameraObjectX);
    }
    if (instanceID >= 0 && instanceID < 16 && !G.ForceStageCameraObjectXLogged[instanceID])
    {
        std::printf(
            "NSMB Test: force StageCamera object X inst=%d frame=%u range=%u-%u base=%08X x=%08X z=%08X zWrite=%d display=%d slotWrite=%d slot=%d\n",
            instanceID,
            frame,
            G.ForceStageCameraObjectXStartFrame,
            G.ForceStageCameraObjectXEndFrame,
            stageCamera.Base,
            G.ForceStageCameraObjectX,
            G.ForceStageCameraObjectZ,
            G.ForceStageCameraObjectZEnabled ? 1 : 0,
            G.ForceStageCameraObjectXWriteDisplay ? 1 : 0,
            G.ForceStageCameraObjectXWriteSlot ? 1 : 0,
            G.ForceStageCameraObjectXSlot);
        std::fflush(stdout);
        G.ForceStageCameraObjectXLogged[instanceID] = true;
    }
}

void ForceStageFXSettingsIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForceStageFXSettingsEnabled || !nds || !nds->MainRAM)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (G.ForceStageFXSettingsHostOnly && G.NetRole != Role::Host)
        return;
    if (G.ForceStageFXSettingsClientOnly && G.NetRole != Role::Client)
        return;
    if (frame < G.ForceStageFXSettingsStartFrame)
        return;
    if (G.ForceStageFXSettingsEndFrame != 0 && frame > G.ForceStageFXSettingsEndFrame)
        return;

    const ObjectScanSample stageFX = FindObjectByID(nds, kStageFXObjectID);
    if (!stageFX.Found || !IsARM9MainRAMAddress(stageFX.Base))
        return;

    const melonDS::u32 oldSettings = nds->ARM9Read32(stageFX.Base + 0x08);
    nds->ARM9Write32(stageFX.Base + 0x08, G.ForceStageFXSettingsValue);
    if (!G.ForceStageFXSettingsLogged[instanceID] || oldSettings != G.ForceStageFXSettingsValue)
    {
        std::printf(
            "NSMB Test: force StageFX settings inst=%d frame=%u range=%u-%u base=%08X old=%08X new=%08X\n",
            instanceID,
            frame,
            G.ForceStageFXSettingsStartFrame,
            G.ForceStageFXSettingsEndFrame,
            stageFX.Base,
            oldSettings,
            G.ForceStageFXSettingsValue);
        G.ForceStageFXSettingsLogged[instanceID] = true;
    }
}

bool CallStageScenePostCreateIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.CallStageScenePostCreateEnabled || !nds || instanceID < 0 || instanceID >= 16)
        return false;
    if (G.CallStageScenePostCreateHostOnly && G.NetRole != Role::Host)
        return false;
    if (G.CallStageScenePostCreateClientOnly && G.NetRole != Role::Client)
        return false;
    if (G.CallStageScenePostCreateApplied[instanceID] || frame < G.CallStageScenePostCreateFrame)
        return false;

    const ObjectScanSample stageScene = FindObjectByIDAndSettingsLoose(
        nds,
        kStageSceneObjectID,
        kMvlStageSceneSettings);
    if (!stageScene.Found || !IsARM9MainRAMAddress(stageScene.Base))
    {
        std::printf(
            "NSMB StageScenePostCreate: inst=%d frame=%u skipped: stage scene not found\n",
            instanceID,
            frame);
        std::fflush(stdout);
        return false;
    }

    const melonDS::u32 processLink = stageScene.Base + 0x28;
    const melonDS::u32 prevLinkBase = nds->ARM9Read32(stageScene.Base + 0x2C);
    const melonDS::u32 objectID = nds->ARM9Read16(stageScene.Base + 0x0C);
    if (!IsARM9MainRAMAddress(prevLinkBase))
    {
        G.CallStageScenePostCreateApplied[instanceID] = true;
        std::printf(
            "NSMB StageScenePostCreate: inst=%d frame=%u skipped: stage scene is not linked for process callback base=%08X link=%08X prev=%08X id=%u\n",
            instanceID,
            frame,
            stageScene.Base,
            processLink,
            prevLinkBase,
            objectID);
        std::fflush(stdout);
        return false;
    }

    const melonDS::u32 oldPC = nds->ARM9.R[15] - ((nds->ARM9.CPSR & 0x20) ? 2 : 4);
    const melonDS::u32 returnPC = oldPC | ((nds->ARM9.CPSR & 0x20) ? 1u : 0u);

    std::vector<melonDS::u32> code;
    code.reserve(24);
    EmitARM(code, 0xE92D5FFFu); // push {r0-r12, lr}
    EmitARM(code, 0xE10F5000u); // mrs r5, cpsr
    EmitARM(code, 0xE92D0020u); // push {r5}
    // 0x0204D204 is the Base process-list postCreate callback, not an
    // object method. Match the register shape observed on the visible US
    // route for StageScene: r0=Success, r1=object link, r2=previous link,
    // r3=object ID.
    EmitLoadImm(code, 0, 1);
    EmitLoadImm(code, 1, processLink);
    EmitLoadImm(code, 2, prevLinkBase);
    EmitLoadImm(code, 3, objectID);
    EmitARM(code, 0xE59FC008u); // ldr ip, [pc, #8]
    EmitARM(code, 0xE28FE008u); // add lr, pc, #8
    EmitARM(code, 0xE12FFF1Cu); // bx ip
    EmitARM(code, 0xE1A00000u); // nop
    EmitARM(code, 0x0204D204u); // Base process-list postCreate callback in US/A2DE
    EmitARM(code, 0xE8BD0020u); // pop {r5}
    EmitARM(code, 0xE128F005u); // msr apsr_nzcvq, r5
    EmitARM(code, 0xE8BD5FFFu); // pop {r0-r12, lr}
    EmitARM(code, 0xE59FC004u); // ldr ip, [pc, #4]
    EmitARM(code, 0xE12FFF1Cu); // bx ip
    EmitARM(code, 0xE1A00000u); // nop
    EmitARM(code, returnPC);

    for (size_t i = 0; i < code.size(); i++)
    {
        if (!WriteARM9U32(nds, kDirectBootTrampolineAddr + static_cast<melonDS::u32>(i * sizeof(melonDS::u32)), code[i]))
            return false;
    }

    G.CallStageScenePostCreateApplied[instanceID] = true;
    std::printf(
        "NSMB StageScenePostCreate: inst=%d frame=%u base=%08X link=%08X prev=%08X id=%u trampoline=%08X return=%08X\n",
        instanceID,
        frame,
        stageScene.Base,
        processLink,
        prevLinkBase,
        objectID,
        kDirectBootTrampolineAddr,
        returnPC);
    std::fflush(stdout);
    nds->ARM9.JumpTo(kDirectBootTrampolineAddr);
    return true;
}

void ForceStageActorFreezeFlagIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForceStageActorFreezeFlagEnabled || !nds || !nds->MainRAM)
        return;
    if (G.ForceStageActorFreezeFlagHostOnly && G.NetRole != Role::Host)
        return;
    if (G.ForceStageActorFreezeFlagClientOnly && G.NetRole != Role::Client)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1)
        return;
    if (frame < G.ForceStageActorFreezeFlagStartFrame)
        return;
    if (G.ForceStageActorFreezeFlagEndFrame != 0 && frame > G.ForceStageActorFreezeFlagEndFrame)
    {
        if (!G.ForceStageActorFreezeFlagReleased[instanceID])
        {
            nds->ARM9Write8(kStageActorFreezeFlagAddr, 0);
            G.ForceStageActorFreezeFlagReleased[instanceID] = true;
            std::printf(
                "NSMB Test: released stage actor freeze flag inst=%d frame=%u end=%u\n",
                instanceID,
                frame,
                G.ForceStageActorFreezeFlagEndFrame);
        }
        return;
    }

    nds->ARM9Write8(kStageActorFreezeFlagAddr, static_cast<melonDS::u8>(G.ForceStageActorFreezeFlagValue & 0xFF));
    if (!G.ForceStageActorFreezeFlagLogged[instanceID])
    {
        std::printf(
            "NSMB Test: force stage actor freeze flag inst=%d frame=%u range=%u-%u value=0x%02X\n",
            instanceID,
            frame,
            G.ForceStageActorFreezeFlagStartFrame,
            G.ForceStageActorFreezeFlagEndFrame,
            G.ForceStageActorFreezeFlagValue & 0xFF);
        G.ForceStageActorFreezeFlagLogged[instanceID] = true;
    }
}

void ForcePlayerDeathCountersIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForcePlayerDeathCountersEnabled || !nds || !nds->MainRAM)
        return;
    if (G.ForcePlayerDeathCountersHostOnly && G.NetRole != Role::Host)
        return;
    if (G.ForcePlayerDeathCountersClientOnly && G.NetRole != Role::Client)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (frame < G.ForcePlayerDeathCountersStartFrame)
        return;
    if (G.ForcePlayerDeathCountersEndFrame != 0 && frame > G.ForcePlayerDeathCountersEndFrame)
        return;
    if (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1)
        return;

    const melonDS::u32 old0 = nds->ARM9Read32(kGamePlayerDeathsAddr);
    const melonDS::u32 old1 = nds->ARM9Read32(kGamePlayerDeathsAddr + sizeof(melonDS::u32));
    const melonDS::u32 oldLife0 = nds->ARM9Read32(kGamePlayerLivesAddr);
    const melonDS::u32 oldLife1 = nds->ARM9Read32(kGamePlayerLivesAddr + sizeof(melonDS::u32));
    nds->ARM9Write32(kGamePlayerDeathsAddr, G.ForcePlayerDeathCounter0);
    nds->ARM9Write32(kGamePlayerDeathsAddr + sizeof(melonDS::u32), G.ForcePlayerDeathCounter1);
    if (G.ForcePlayerLivesEnabled)
    {
        nds->ARM9Write32(kGamePlayerLivesAddr, G.ForcePlayerLife0);
        nds->ARM9Write32(kGamePlayerLivesAddr + sizeof(melonDS::u32), G.ForcePlayerLife1);
    }

    if (!G.ForcePlayerDeathCountersLogged[instanceID])
    {
        std::printf(
            "NSMB Test: force player death counters inst=%d frame=%u range=%u-%u old=%u/%u value=%u/%u lives=%u/%u->%u/%u enabled=%d\n",
            instanceID,
            frame,
            G.ForcePlayerDeathCountersStartFrame,
            G.ForcePlayerDeathCountersEndFrame,
            old0,
            old1,
            G.ForcePlayerDeathCounter0,
            G.ForcePlayerDeathCounter1,
            oldLife0,
            oldLife1,
            G.ForcePlayerLife0,
            G.ForcePlayerLife1,
            G.ForcePlayerLivesEnabled ? 1 : 0);
        G.ForcePlayerDeathCountersLogged[instanceID] = true;
    }
}

void ForcePlayerInventoryPowerupsIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForcePlayerInventoryPowerupsEnabled || !nds || !nds->MainRAM)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (frame < G.ForcePlayerInventoryPowerupsStartFrame)
        return;
    if (G.ForcePlayerInventoryPowerupsEndFrame != 0 && frame > G.ForcePlayerInventoryPowerupsEndFrame)
        return;
    if (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1)
        return;

    const melonDS::u8 old0 = nds->ARM9Read8(kGamePlayerInventoryPowerupAddr);
    const melonDS::u8 old1 = nds->ARM9Read8(kGamePlayerInventoryPowerupAddr + 1);
    nds->ARM9Write8(kGamePlayerInventoryPowerupAddr, static_cast<melonDS::u8>(G.ForcePlayerInventoryPowerup0 & 0xFF));
    nds->ARM9Write8(kGamePlayerInventoryPowerupAddr + 1, static_cast<melonDS::u8>(G.ForcePlayerInventoryPowerup1 & 0xFF));

    if (!G.ForcePlayerInventoryPowerupsLogged[instanceID])
    {
        std::printf(
            "NSMB Test: force player inventory powerups inst=%d frame=%u range=%u-%u old=%u/%u value=%u/%u\n",
            instanceID,
            frame,
            G.ForcePlayerInventoryPowerupsStartFrame,
            G.ForcePlayerInventoryPowerupsEndFrame,
            old0,
            old1,
            G.ForcePlayerInventoryPowerup0 & 0xFF,
            G.ForcePlayerInventoryPowerup1 & 0xFF);
        G.ForcePlayerInventoryPowerupsLogged[instanceID] = true;
    }
}

void ForcePlayerStarCountersIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForcePlayerStarCountersEnabled || !nds || !nds->MainRAM)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (frame < G.ForcePlayerStarCountersStartFrame)
        return;
    if (G.ForcePlayerStarCountersEndFrame != 0 && frame > G.ForcePlayerStarCountersEndFrame)
        return;
    if (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1)
        return;

    const melonDS::u32 oldBattle0 = nds->ARM9Read32(kGamePlayerBattleStarsAddr);
    const melonDS::u32 oldBattle1 = nds->ARM9Read32(kGamePlayerBattleStarsAddr + sizeof(melonDS::u32));
    const melonDS::u32 oldDisplayed0 = nds->ARM9Read32(kGamePlayerDisplayedStarsAddr);
    const melonDS::u32 oldDisplayed1 = nds->ARM9Read32(kGamePlayerDisplayedStarsAddr + sizeof(melonDS::u32));
    const melonDS::u32 oldCollected0 = nds->ARM9Read32(kGamePlayerCollectedStarsAddr);
    const melonDS::u32 oldCollected1 = nds->ARM9Read32(kGamePlayerCollectedStarsAddr + sizeof(melonDS::u32));

    nds->ARM9Write32(kGamePlayerBattleStarsAddr, G.ForcePlayerBattleStars0);
    nds->ARM9Write32(kGamePlayerBattleStarsAddr + sizeof(melonDS::u32), G.ForcePlayerBattleStars1);
    nds->ARM9Write32(kGamePlayerDisplayedStarsAddr, G.ForcePlayerDisplayedStars0);
    nds->ARM9Write32(kGamePlayerDisplayedStarsAddr + sizeof(melonDS::u32), G.ForcePlayerDisplayedStars1);
    nds->ARM9Write32(kGamePlayerCollectedStarsAddr, G.ForcePlayerCollectedStars0);
    nds->ARM9Write32(kGamePlayerCollectedStarsAddr + sizeof(melonDS::u32), G.ForcePlayerCollectedStars1);

    if (!G.ForcePlayerStarCountersLogged[instanceID])
    {
        std::printf(
            "NSMB Test: force player star counters inst=%d frame=%u range=%u-%u "
            "battle=%u/%u->%u/%u displayed=%u/%u->%u/%u collected=%u/%u->%u/%u\n",
            instanceID,
            frame,
            G.ForcePlayerStarCountersStartFrame,
            G.ForcePlayerStarCountersEndFrame,
            oldBattle0,
            oldBattle1,
            G.ForcePlayerBattleStars0,
            G.ForcePlayerBattleStars1,
            oldDisplayed0,
            oldDisplayed1,
            G.ForcePlayerDisplayedStars0,
            G.ForcePlayerDisplayedStars1,
            oldCollected0,
            oldCollected1,
            G.ForcePlayerCollectedStars0,
            G.ForcePlayerCollectedStars1);
        G.ForcePlayerStarCountersLogged[instanceID] = true;
    }
}

void ForceStageActorPreUpdateGateIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForceStageActorPreUpdateGateEnabled || !nds || !nds->MainRAM)
        return;
    if (frame < G.ForceStageActorPreUpdateGateStartFrame)
        return;
    if (G.ForceStageActorPreUpdateGateEndFrame != 0 && frame > G.ForceStageActorPreUpdateGateEndFrame)
        return;
    if (G.ForceStageActorPreUpdateGateHostOnly && G.NetRole != Role::Host)
        return;
    if (G.ForceStageActorPreUpdateGateClientOnly && G.NetRole != Role::Client)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1)
        return;

    // StageActor::preUpdate calls 02007CB0(0x02088F48) and returns false when
    // both per-player gate bytes at +0x5BE/+0x5BF have bit0 set. Clear them
    // for diagnosis so the normal onUpdate dispatch can be tested.
    constexpr melonDS::u32 kStageActorPreUpdateGate0 = 0x02089506u;
    constexpr melonDS::u32 kStageActorPreUpdateGate1 = 0x02089507u;
    const melonDS::u8 old0 = nds->ARM9Read8(kStageActorPreUpdateGate0);
    const melonDS::u8 old1 = nds->ARM9Read8(kStageActorPreUpdateGate1);
    nds->ARM9Write8(kStageActorPreUpdateGate0, static_cast<melonDS::u8>(old0 & ~0x01));
    nds->ARM9Write8(kStageActorPreUpdateGate1, static_cast<melonDS::u8>(old1 & ~0x01));

    if (!G.ForceStageActorPreUpdateGateLogged[instanceID])
    {
        std::printf(
            "NSMB Test: force stage actor preupdate gate inst=%d frame=%u range=%u-%u gate0=%02X->%02X gate1=%02X->%02X\n",
            instanceID,
            frame,
            G.ForceStageActorPreUpdateGateStartFrame,
            G.ForceStageActorPreUpdateGateEndFrame,
            old0,
            nds->ARM9Read8(kStageActorPreUpdateGate0),
            old1,
            nds->ARM9Read8(kStageActorPreUpdateGate1));
        G.ForceStageActorPreUpdateGateLogged[instanceID] = true;
    }
}

void ForceActorCategoryMaskIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForceActorCategoryMaskEnabled || !nds || !nds->MainRAM)
        return;
    if (frame < G.ForceActorCategoryMaskStartFrame)
        return;
    if (G.ForceActorCategoryMaskEndFrame != 0 && frame > G.ForceActorCategoryMaskEndFrame)
        return;
    if (G.ForceActorCategoryMaskHostOnly && G.NetRole != Role::Host)
        return;
    if (G.ForceActorCategoryMaskClientOnly && G.NetRole != Role::Client)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1)
        return;

    const melonDS::u8 oldValue = nds->ARM9Read8(kActorCategoryMaskAddr);
    nds->ARM9Write8(kActorCategoryMaskAddr, static_cast<melonDS::u8>(G.ForceActorCategoryMaskValue & 0xFF));

    if (!G.ForceActorCategoryMaskLogged[instanceID])
    {
        std::printf(
            "NSMB Test: force actor category mask inst=%d frame=%u range=%u-%u old=0x%02X value=0x%02X\n",
            instanceID,
            frame,
            G.ForceActorCategoryMaskStartFrame,
            G.ForceActorCategoryMaskEndFrame,
            oldValue,
            G.ForceActorCategoryMaskValue & 0xFF);
        G.ForceActorCategoryMaskLogged[instanceID] = true;
    }
}

void ForcePlayerSignalUnlockIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForcePlayerSignalUnlockEnabled || !nds || !nds->MainRAM)
        return;
    if (frame < G.ForcePlayerSignalUnlockStartFrame)
        return;
    if (G.ForcePlayerSignalUnlockEndFrame != 0 && frame > G.ForcePlayerSignalUnlockEndFrame)
        return;
    if (G.ForcePlayerSignalUnlockHostOnly && G.NetRole != Role::Host)
        return;
    if (G.ForcePlayerSignalUnlockClientOnly && G.NetRole != Role::Client)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1)
        return;

    // Diagnostic equivalent of PlayerBase::signalUnlocked(): clear the global
    // player lock bits and each Player actor's local lock flags.
    const melonDS::u8 global9280 = nds->ARM9Read8(0x020C9280);
    const melonDS::u8 global9298 = nds->ARM9Read8(0x020C9298);
    nds->ARM9Write8(0x020C9280, static_cast<melonDS::u8>(global9280 & ~0x10));
    nds->ARM9Write8(0x020C9298, static_cast<melonDS::u8>(global9298 & ~0x40));

    const melonDS::u32 ramLen = std::min<melonDS::u32>(nds->MainRAMMask + 1, 0x400000);
    int clearedPlayers = 0;
    for (melonDS::u32 off = 0; off + 0x7C4 <= ramLen; off += 4)
    {
        melonDS::u32 vtable = 0;
        melonDS::u16 objectID = 0;
        melonDS::u16 stateType = 0;
        melonDS::u32 flags = 0;
        if (!ReadMainRAMU32(nds, off, vtable) ||
            !ReadMainRAMU16(nds, off + 0x0C, objectID) ||
            !ReadMainRAMU16(nds, off + 0x0E, stateType) ||
            !ReadMainRAMU32(nds, off + 0x10, flags))
            continue;
        if (objectID != kPlayerObjectID)
            continue;
        if (vtable < kMainRAMBase || vtable >= kMainRAMBase + ramLen)
            continue;
        if (stateType != 1 && stateType != 2 && stateType != 3)
            continue;
        if (flags >= 0x10000000)
            continue;

        const melonDS::u32 base = kMainRAMBase + off;
        nds->ARM9Write8(base + 0x75C, 0);
        const melonDS::u8 flag192 = nds->ARM9Read8(base + 0x192);
        nds->ARM9Write8(base + 0x192, static_cast<melonDS::u8>(flag192 & ~0x01));
        clearedPlayers++;
    }

    if (!G.ForcePlayerSignalUnlockLogged[instanceID])
    {
        std::printf(
            "NSMB Test: force player signal unlock inst=%d frame=%u range=%u-%u players=%d global9280=%02X->%02X global9298=%02X->%02X\n",
            instanceID,
            frame,
            G.ForcePlayerSignalUnlockStartFrame,
            G.ForcePlayerSignalUnlockEndFrame,
            clearedPlayers,
            global9280,
            nds->ARM9Read8(0x020C9280),
            global9298,
            nds->ARM9Read8(0x020C9298));
        G.ForcePlayerSignalUnlockLogged[instanceID] = true;
    }
}

void ForcePlayerUpdateEnableIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForcePlayerUpdateEnableEnabled || !nds || !nds->MainRAM)
        return;
    if (frame < G.ForcePlayerUpdateEnableStartFrame)
        return;
    if (G.ForcePlayerUpdateEnableEndFrame != 0 && frame > G.ForcePlayerUpdateEnableEndFrame)
        return;
    if (G.ForcePlayerUpdateEnableHostOnly && G.NetRole != Role::Host)
        return;
    if (G.ForcePlayerUpdateEnableClientOnly && G.NetRole != Role::Client)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1)
        return;

    const PlayerActorScanSample players = FindPlayerActors(nds);
    melonDS::u32 playerBases[2] {};
    int playerCount = 0;
    if (players.Actor0.Found && IsARM9MainRAMAddress(players.Actor0.Base))
        playerBases[playerCount++] = players.Actor0.Base;
    if (players.Actor1.Found && IsARM9MainRAMAddress(players.Actor1.Base))
        playerBases[playerCount++] = players.Actor1.Base;

    int changed = 0;
    melonDS::u8 oldFlags[2] {};
    melonDS::u8 newFlags[2] {};
    for (int i = 0; i < playerCount; i++)
    {
        oldFlags[i] = nds->ARM9Read8(playerBases[i] + 0x13);
        newFlags[i] = static_cast<melonDS::u8>(oldFlags[i] & ~0x02);
        if (newFlags[i] != oldFlags[i])
        {
            nds->ARM9Write8(playerBases[i] + 0x13, newFlags[i]);
            changed++;
        }
    }

    if (!G.ForcePlayerUpdateEnableLogged[instanceID])
    {
        std::printf(
            "NSMB Test: force player update enable inst=%d frame=%u range=%u-%u players=%d changed=%d p0=%08X %02X->%02X p1=%08X %02X->%02X\n",
            instanceID,
            frame,
            G.ForcePlayerUpdateEnableStartFrame,
            G.ForcePlayerUpdateEnableEndFrame,
            playerCount,
            changed,
            playerCount > 0 ? playerBases[0] : 0,
            playerCount > 0 ? oldFlags[0] : 0,
            playerCount > 0 ? newFlags[0] : 0,
            playerCount > 1 ? playerBases[1] : 0,
            playerCount > 1 ? oldFlags[1] : 0,
            playerCount > 1 ? newFlags[1] : 0);
        G.ForcePlayerUpdateEnableLogged[instanceID] = true;
    }
}

void ForceStageSceneStartGateIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForceStageSceneStartGateEnabled || !nds || !nds->MainRAM)
        return;
    if (frame < G.ForceStageSceneStartGateStartFrame)
        return;
    if (G.ForceStageSceneStartGateEndFrame != 0 && frame > G.ForceStageSceneStartGateEndFrame)
        return;
    if (G.ForceStageSceneStartGateHostOnly && G.NetRole != Role::Host)
        return;
    if (G.ForceStageSceneStartGateClientOnly && G.NetRole != Role::Client)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1)
        return;

    const ObjectScanSample stageScene = FindObjectByIDAndSettingsLoose(nds, kStageSceneObjectID, kMvlStageSceneSettings);
    if (!stageScene.Found || !IsARM9MainRAMAddress(stageScene.Base))
    {
        if (!G.ForceStageSceneStartGateLogged[instanceID])
        {
            std::printf("NSMB Test: force stage scene start gate skipped inst=%d frame=%u no stage scene\n",
                instanceID,
                frame);
            G.ForceStageSceneStartGateLogged[instanceID] = true;
        }
        return;
    }

    if (G.ForceStageSceneFadeReady)
    {
        nds->ARM9Write32(stageScene.Base + 0x561C, 1);
        nds->ARM9Write32(stageScene.Base + 0x563C, 0x1000);
    }
    if (G.ForceStageSceneInputLatchEnabled)
    {
        nds->ARM9Write8(0x020C9280, 0);
        nds->ARM9Write8(stageScene.Base + 0x5645, 1);
        nds->ARM9Write8(0x020C928C, 0);
    }
    nds->ARM9Write8(stageScene.Base + 0x5649, static_cast<melonDS::u8>(G.ForceStageSceneStartGateValue & 0xFF));

    if (!G.ForceStageSceneStartGateLogged[instanceID])
    {
        std::printf(
            "NSMB Test: force stage scene start gate inst=%d frame=%u range=%u-%u base=%08X value=0x%02X fadeReady=%d inputLatch=%d\n",
            instanceID,
            frame,
            G.ForceStageSceneStartGateStartFrame,
            G.ForceStageSceneStartGateEndFrame,
            stageScene.Base,
            G.ForceStageSceneStartGateValue & 0xFF,
            G.ForceStageSceneFadeReady ? 1 : 0,
            G.ForceStageSceneInputLatchEnabled ? 1 : 0);
        G.ForceStageSceneStartGateLogged[instanceID] = true;
    }
}

void ForceNetLocalAidIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (G.ForceNetLocalAid < 0 || !nds || !nds->MainRAM)
        return;
    if (G.ForceNetLocalAid > 3)
        return;
    if (frame < G.ForceNetLocalAidStartFrame)
        return;
    if (G.ForceNetLocalAidEndFrame != 0 && frame > G.ForceNetLocalAidEndFrame)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;

    nds->ARM9Write32(kNetLocalAidAddr, static_cast<melonDS::u32>(G.ForceNetLocalAid));

    if (!G.ForceNetLocalAidLogged[instanceID])
    {
        std::printf("NSMB Test: force net local AID inst=%d frame=%u range=%u-%u value=%d\n",
            instanceID,
            frame,
            G.ForceNetLocalAidStartFrame,
            G.ForceNetLocalAidEndFrame,
            G.ForceNetLocalAid);
        G.ForceNetLocalAidLogged[instanceID] = true;
    }
}

void ForceWifiCommunicatingIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (G.ForceWifiCommunicatingCount < 0 || !nds || !nds->MainRAM)
        return;
    if (G.ForceWifiCommunicatingCount < 1 || G.ForceWifiCommunicatingCount > 4)
        return;
    if (frame < G.ForceWifiCommunicatingStartFrame)
        return;
    if (G.ForceWifiCommunicatingEndFrame != 0 && frame > G.ForceWifiCommunicatingEndFrame)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;

    nds->ARM9Write16(kWifiCommunicatingConsoleCountAddr,
        static_cast<melonDS::u16>(G.ForceWifiCommunicatingCount));
    for (int i = 0; i < 4; i++)
    {
        nds->ARM9Write32(kWifiCommunicatingConsolesAddr + static_cast<melonDS::u32>(i * sizeof(melonDS::u32)),
            i < G.ForceWifiCommunicatingCount ? 1u : 0u);
    }

    if (!G.ForceWifiCommunicatingLogged[instanceID])
    {
        std::printf("NSMB Test: force wifi communicating inst=%d frame=%u range=%u-%u count=%d\n",
            instanceID,
            frame,
            G.ForceWifiCommunicatingStartFrame,
            G.ForceWifiCommunicatingEndFrame,
            G.ForceWifiCommunicatingCount);
        G.ForceWifiCommunicatingLogged[instanceID] = true;
    }
}

void PushScriptRemotePacketIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ScriptRemotePacketEnabled || !nds || !nds->MainRAM)
        return;
    if (G.ScriptRemotePacketPlayer < 0 || G.ScriptRemotePacketPlayer > 1)
        return;
    if (G.ScriptRemotePacketInputInstance < 0 || G.ScriptRemotePacketInputInstance >= 16)
        return;
    const melonDS::u32 startFrame = G.ScriptRemotePacketEnabled
        ? G.ScriptRemotePacketStartFrame
        : G.PacketBridgeJitHelperPatchFrame;
    const melonDS::u32 endFrame = G.ScriptRemotePacketEnabled ? G.ScriptRemotePacketEndFrame : 0;
    if (frame < startFrame)
        return;
    if (endFrame != 0 && frame > endFrame)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;

    const InputState input = ApplyScriptRemotePacketInputScript(
        G.ScriptRemotePacketInputInstance,
        frame,
        NeutralInput());
    melonDS::u8 packet[52] {};
    const melonDS::u32 tick = nds->ARM9Read16(kNetPacketTickAddr);
    const melonDS::u32 keys = (~input.KeyMask) & 0x0FFF;
    packet[0] = static_cast<melonDS::u8>(tick & 0xFF);
    packet[1] = static_cast<melonDS::u8>((tick >> 8) & 0xFF);
    packet[2] = static_cast<melonDS::u8>(keys & 0xFF);
    packet[3] = static_cast<melonDS::u8>((keys >> 8) & 0xFF);
    packet[4] = nds->ARM9Read8(kNetPacketActionAddr);
    packet[5] = nds->ARM9Read8(kNetPacketByte5Addr);
    packet[6] = nds->ARM9Read8(kNetPacketByte6Addr);
    packet[7] = nds->ARM9Read8(kNetPacketByte7Addr);
    for (melonDS::u32 i = 0; i < 44; i++)
        packet[8 + i] = nds->ARM9Read8(0x020888E8 + i);
    packet[0x29] = nds->ARM9Read8(0x02088A4C);

    melonDS::NSML_PushMarioVsLuigiRemotePacket(
        nds,
        static_cast<melonDS::u32>(G.ScriptRemotePacketPlayer),
        packet);

    if (!G.ScriptRemotePacketLogged[instanceID])
    {
        std::printf("NSMB Test: script remote packet inst=%d frame=%u range=%u-%u player=%d inputInstance=%d tick=0x%04X keys=0x%04X\n",
            instanceID,
            frame,
            G.ScriptRemotePacketStartFrame,
            G.ScriptRemotePacketEndFrame,
            G.ScriptRemotePacketPlayer,
            G.ScriptRemotePacketInputInstance,
            tick,
            keys);
        G.ScriptRemotePacketLogged[instanceID] = true;
    }
}

int CurrentPacketBridgeLocalPlayer()
{
    if (G.ForceNetLocalAid >= 0 && G.ForceNetLocalAid <= 1)
        return G.ForceNetLocalAid;
    if (G.NetRole == Role::Client)
        return 1;
    return 0;
}

InputState PacketBridgeInputForPlayer(
    int player,
    int localPlayer,
    int instanceID,
    melonDS::u32 frame,
    const InputState& localInput,
    const InputState& remoteInput,
    bool hasRemoteInput)
{
    if (player == localPlayer)
        return localInput;
    if (hasRemoteInput)
        return remoteInput;
    return ApplyScriptRemotePacketInputScript(G.ScriptRemotePacketInputInstance, frame, NeutralInput());
}

void WritePacketBridgeJitScratchInputs(
    int instanceID,
    melonDS::u32 frame,
    melonDS::NDS* nds,
    int localPlayer,
    const InputState& localInput,
    const InputState& remoteInput,
    bool hasRemoteInput,
    bool predictedRemoteInput)
{
    if (!nds || !nds->MainRAM)
        return;

    melonDS::u32 tick = nds->ARM9Read16(kNetPacketTickAddr);
    if (G.InputNetplayOnly && G.NetplayStartFrame != 0 && frame >= G.NetplayStartFrame)
    {
        tick = (frame - G.NetplayStartFrame) & 0xFFFF;
        nds->ARM9Write16(kNetPacketTickAddr, static_cast<melonDS::u16>(tick));
    }
    const melonDS::u8 action = nds->ARM9Read8(kNetPacketActionAddr);
    nds->ARM9Write16(kPacketBridgeJitScratchTickAddr, static_cast<melonDS::u16>(tick));
    nds->ARM9Write8(kPacketBridgeJitScratchActionAddr, action);

    for (int player = 0; player < 2; player++)
    {
        const InputState input = PacketBridgeInputForPlayer(
            player,
            localPlayer,
            instanceID,
            frame,
            localInput,
            remoteInput,
            hasRemoteInput);
        const melonDS::u32 keys = (~input.KeyMask) & 0x0FFF;
        nds->ARM9Write16(kPacketBridgeJitScratchKeysAddr + static_cast<melonDS::u32>(player * 2),
            static_cast<melonDS::u16>(keys));

        melonDS::u8 packet[52] {};
        packet[0] = static_cast<melonDS::u8>(tick & 0xFF);
        packet[1] = static_cast<melonDS::u8>((tick >> 8) & 0xFF);
        packet[2] = static_cast<melonDS::u8>(keys & 0xFF);
        packet[3] = static_cast<melonDS::u8>((keys >> 8) & 0xFF);
        packet[4] = action;
        packet[5] = input.Touching ? 1 : 0;
        packet[6] = static_cast<melonDS::u8>(std::min<int>(input.TouchX, 255));
        packet[7] = static_cast<melonDS::u8>(std::min<int>(input.TouchY, 191));
        for (melonDS::u32 i = 0; i < 44; i++)
            packet[8 + i] = nds->ARM9Read8(0x020888E8 + i);
        packet[0x29] = nds->ARM9Read8(0x02088A4C);

        const melonDS::u32 packetAddr = kPacketBridgeJitScratchPacketsAddr + static_cast<melonDS::u32>(player * 0x40);
        for (melonDS::u32 i = 0; i < sizeof(packet); i++)
            nds->ARM9Write8(packetAddr + i, packet[i]);

        // The JIT helper path only replaces Net::getConsoleKeys()/TouchPad.
        // Feeding these synthetic packets back into NSMB's lower packet queue can
        // corrupt the game's stage state, so keep the packet copy as trace data.
    }

    if (G.InputNetplayTraceEnabled && (frame % 60) == 0)
    {
        const melonDS::u16 scratchKeys0 = nds->ARM9Read16(kPacketBridgeJitScratchKeysAddr);
        const melonDS::u16 scratchKeys1 = nds->ARM9Read16(kPacketBridgeJitScratchKeysAddr + 2);
        std::printf(
            "NSMB InputNetplay: inst=%d frame=%u localPlayer=%d hasRemote=%d predictedRemote=%d tick=0x%04X action=0x%02X keys0=0x%03X keys1=0x%03X\n",
            instanceID,
            frame,
            localPlayer,
            hasRemoteInput ? 1 : 0,
            predictedRemoteInput ? 1 : 0,
            static_cast<unsigned>(tick),
            static_cast<unsigned>(action),
            static_cast<unsigned>(scratchKeys0),
            static_cast<unsigned>(scratchKeys1));
    }
}

bool RollbackResimulateIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.RollbackEnabled || !G.RollbackResimulate || !G.InputNetplayOnly || !nds)
        return false;
    if (instanceID < 0 || instanceID >= 16)
        return false;

    melonDS::u32 mismatchFrame = kNoFrameLimit;
    melonDS::u32 restoreFrame = kNoFrameLimit;
    std::vector<char> buffer;
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        if (G.PendingRollbackFrame == kNoFrameLimit)
            return false;
        mismatchFrame = G.PendingRollbackFrame;
        if (mismatchFrame >= frame)
            return false;
        if (G.RollbackResimulateDelayFrames > 0
            && G.PendingRollbackObservedFrame != kNoFrameLimit
            && frame < G.PendingRollbackObservedFrame + static_cast<melonDS::u32>(G.RollbackResimulateDelayFrames))
        {
            return false;
        }

        auto state = G.RollbackStates.upper_bound(mismatchFrame);
        if (state == G.RollbackStates.begin())
        {
            std::printf(
                "NSMB Rollback: cannot resimulate mismatch=%u at current=%u, checkpoint missing window=%d interval=%d\n",
                mismatchFrame,
                frame,
                G.RollbackWindow,
                G.RollbackCheckpointInterval);
            G.PendingRollbackFrame = kNoFrameLimit;
            G.PendingRollbackObservedFrame = kNoFrameLimit;
            return false;
        }
        --state;
        restoreFrame = state->first;

        buffer = state->second;
        G.PendingRollbackFrame = kNoFrameLimit;
        G.PendingRollbackObservedFrame = kNoFrameLimit;

        for (auto it = G.RollbackStates.upper_bound(restoreFrame); it != G.RollbackStates.end(); )
            it = G.RollbackStates.erase(it);
    }

    if (!RestoreRollbackCheckpointBuffer(nds, buffer))
    {
        std::printf("NSMB Rollback: resim restore failed inst=%d restoreFrame=%u current=%u\n",
            instanceID,
            restoreFrame,
            frame);
        return false;
    }

    const int localPlayer = CurrentPacketBridgeLocalPlayer();
    melonDS::u32 resimulated = 0;
    for (melonDS::u32 f = restoreFrame; f < frame; f++)
    {
        InputState localInput = NeutralInput();
        InputState remoteInput = NeutralInput();
        bool predictedRemote = false;
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            auto localIt = G.LocalInputs.find(f);
            if (localIt != G.LocalInputs.end())
                localInput = localIt->second;
            GetRollbackRemoteInputLocked(f, remoteInput, predictedRemote);
        }

        WritePacketBridgeJitScratchInputs(
            instanceID,
            f,
            nds,
            localPlayer,
            localInput,
            remoteInput,
            true,
            predictedRemote);

        nds->SetKeyMask(localInput.KeyMask);
        if (localInput.Touching)
            nds->TouchScreen(localInput.TouchX, localInput.TouchY);
        else
            nds->ReleaseScreen();

        nds->RunFrame();
        resimulated++;

        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            SaveRollbackCheckpointNowLocked(f + 1, nds);
        }

        if (nds->NumFrames != f + 1 && G.InputNetplayTraceEnabled)
        {
            std::printf("NSMB Rollback: resim frame counter drift expected=%u actual=%u\n",
                f + 1,
                nds->NumFrames);
        }
    }

    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        G.RollbackResimulateCount++;
    }
    if (G.InputNetplayTraceEnabled)
    {
        std::printf("NSMB Rollback: resimulated from checkpoint=%u mismatch=%u to current=%u frames=%u bytes=%zu\n",
            restoreFrame,
            mismatchFrame,
            frame,
            resimulated,
            buffer.size());
        std::fflush(stdout);
    }
    return true;
}

void ThrottleInputNetplayFrameLead(melonDS::NDS* nds, melonDS::u32 frame, melonDS::u32 sendFrame)
{
    if (!G.InputNetplayOnly || G.InputNetplayMaxFrameLead < 0 || !G.Enabled || !G.Ready)
        return;
    if (G.NetplayStartFrame != 0 && frame < G.NetplayStartFrame)
        return;
    if (IsPastTestInputRange(sendFrame))
        return;

    const auto start = std::chrono::steady_clock::now();
    bool blocked = false;
    unsigned long long loops = 0;
    for (;;)
    {
        loops++;
        melonDS::u32 remoteFrame = kNoFrameLimit;
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            PumpNetworkLocked(nds, frame);
            remoteFrame = G.LastReceivedInputFrame;
        }

        if (remoteFrame == kNoFrameLimit)
            return;

        const int lead = static_cast<int>(sendFrame) - static_cast<int>(remoteFrame);
        if (lead <= G.InputNetplayMaxFrameLead)
        {
            if (blocked)
            {
                const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start).count();
                RecordFrameLeadThrottleStats(static_cast<unsigned long long>(std::max<long long>(0, elapsedUs)), loops);
            }
            return;
        }
        blocked = true;

        if (G.InputNetplayTraceEnabled && G.LastInputFrameThrottleTraceFrame != frame)
        {
            G.LastInputFrameThrottleTraceFrame = frame;
            std::printf("NSMB InputNetplay: frame throttle frame=%u sendFrame=%u remoteInputFrame=%u lead=%d maxLead=%d\n",
                frame,
                sendFrame,
                remoteFrame,
                lead,
                G.InputNetplayMaxFrameLead);
            std::fflush(stdout);
        }

        if (G.TestEnabled && G.TestWaitTimeoutMs > 0)
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= G.TestWaitTimeoutMs)
            {
                std::printf("NSMB Test: input frame throttle timeout frame=%u sendFrame=%u remoteInputFrame=%u lead=%d waitedMs=%d\n",
                    frame,
                    sendFrame,
                    remoteFrame,
                    lead,
                    G.TestWaitTimeoutMs);
                std::fflush(stdout);
                if (G.RemoteInputTimeoutFatal)
                    std::_Exit(71);
                if (blocked)
                {
                    const auto waitedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - start).count();
                    RecordFrameLeadThrottleStats(static_cast<unsigned long long>(std::max<long long>(0, waitedUs)), loops);
                }
                return;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void WritePacketBridgeJitScratchIfNeeded(
    int instanceID,
    melonDS::u32 frame,
    melonDS::NDS* nds,
    const InputState& localInput)
{
    if (!G.PacketBridgeJitHelperPatchEnabled || !nds || !nds->MainRAM)
        return;
    if (!G.ScriptRemotePacketEnabled && !G.Enabled)
        return;
    if (G.ScriptRemotePacketEnabled &&
        (G.ScriptRemotePacketInputInstance < 0 || G.ScriptRemotePacketInputInstance >= 16))
        return;
    melonDS::u32 startFrame = G.ScriptRemotePacketStartFrame;
    if (G.InputNetplayOnly)
    {
        startFrame = std::max(startFrame, G.PacketBridgeJitHelperPatchFrame);
        startFrame = std::max(startFrame, G.NetplayStartFrame);
    }
    if (G.ScriptRemotePacketEndFrame != 0 && frame > G.ScriptRemotePacketEndFrame)
        return;

    const int localPlayer = CurrentPacketBridgeLocalPlayer();
    if (G.InputNetplayOnly && G.WaitForPeerBeforeStart && G.NetplayStartFrame > 0)
    {
        const melonDS::u32 delay = static_cast<melonDS::u32>(std::max(0, G.Delay));
        const melonDS::u32 sendStartFrame = (G.NetplayStartFrame > delay)
            ? G.NetplayStartFrame - delay
            : 0;
        if (frame == sendStartFrame)
        {
            std::printf("NSMB InputNetplay: waiting for peer before send start frame=%u applyStart=%u\n",
                sendStartFrame,
                G.NetplayStartFrame);
            std::fflush(stdout);
            WaitForPeerIfNeeded(true);
        }
    }

    InputState effectiveLocalInput = localInput;
    InputState remoteInput = NeutralInput();
    bool hasRemoteInput = false;
    bool predictedRemoteInput = false;
    if (G.Enabled && G.Ready)
    {
        const melonDS::u32 sendFrame = G.InputNetplayOnly
            ? frame + static_cast<melonDS::u32>(std::max(0, G.Delay))
            : frame;
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            PumpNetworkLocked(nds, frame);
            SendMatchSeedLocked();
            G.LocalInputs[sendFrame] = localInput;
            SendInputLocked(sendFrame, localInput);
            if (G.InputNetplayOnly)
            {
                auto localIt = G.LocalInputs.find(frame);
                effectiveLocalInput = localIt != G.LocalInputs.end() ? localIt->second : NeutralInput();
            }
            auto it = G.RemoteInputs.find(frame);
            if (it != G.RemoteInputs.end())
            {
                remoteInput = it->second;
                hasRemoteInput = true;
            }
            else if (G.RollbackEnabled && G.InputNetplayOnly
                && (G.NetplayStartFrame == 0 || frame >= G.NetplayStartFrame))
            {
                hasRemoteInput = GetRollbackRemoteInputLocked(frame, remoteInput, predictedRemoteInput);
            }
        }

        ThrottleInputNetplayFrameLead(nds, frame, sendFrame);

        if (!hasRemoteInput
            && !G.RollbackEnabled
            && G.LocalWaitsForRemote
            && (!G.InputNetplayOnly || G.NetplayStartFrame == 0 || frame >= G.NetplayStartFrame))
        {
            remoteInput = WaitForRemoteInput(frame);
            hasRemoteInput = true;
        }
    }

    if (frame < startFrame)
        return;

    WritePacketBridgeJitScratchInputs(
        instanceID,
        frame,
        nds,
        localPlayer,
        effectiveLocalInput,
        remoteInput,
        hasRemoteInput,
        predictedRemoteInput);
}

void ApplyPacketBridgeJitHelperPatchIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.PacketBridgeJitHelperPatchEnabled || !nds || !nds->MainRAM)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (G.PacketBridgeJitHelperPatchApplied[instanceID] || frame < G.PacketBridgeJitHelperPatchFrame)
        return;

    auto invalidateMainRAM = [&](melonDS::u32 start, melonDS::u32 end)
    {
        for (melonDS::u32 addr = start; addr <= end; addr += sizeof(melonDS::u32))
        {
            nds->JIT.CheckAndInvalidate<0, melonDS::ARMJIT_Memory::memregion_MainRAM>(addr);
            nds->JIT.CheckAndInvalidate<1, melonDS::ARMJIT_Memory::memregion_MainRAM>(addr);
        }
    };

    // Net::getConsoleKeys(u16): return scratchKeys[player].
    WriteARM9U32(nds, 0x0200E854, 0xE59F1008); // ldr r1, [pc, #8]
    WriteARM9U32(nds, 0x0200E858, 0xE0811080); // add r1, r1, r0, lsl #1
    WriteARM9U32(nds, 0x0200E85C, 0xE1D100B0); // ldrh r0, [r1]
    WriteARM9U32(nds, 0x0200E860, 0xE12FFF1E); // bx lr
    WriteARM9U32(nds, 0x0200E864, kPacketBridgeJitScratchKeysAddr);

    // Net::getConsoleTouchPad(u16): write TPData{x,y,touch,0} from scratch packet[player].
    WriteARM9U32(nds, 0x0200E7D0, 0xE59F2024); // ldr r2, [pc, #36]
    WriteARM9U32(nds, 0x0200E7D4, 0xE0822301); // add r2, r2, r1, lsl #6
    WriteARM9U32(nds, 0x0200E7D8, 0xE5D23006); // ldrb r3, [r2, #6]
    WriteARM9U32(nds, 0x0200E7DC, 0xE1C030B0); // strh r3, [r0]
    WriteARM9U32(nds, 0x0200E7E0, 0xE5D23007); // ldrb r3, [r2, #7]
    WriteARM9U32(nds, 0x0200E7E4, 0xE1C030B2); // strh r3, [r0, #2]
    WriteARM9U32(nds, 0x0200E7E8, 0xE5D23005); // ldrb r3, [r2, #5]
    WriteARM9U32(nds, 0x0200E7EC, 0xE1C030B4); // strh r3, [r0, #4]
    WriteARM9U32(nds, 0x0200E7F0, 0xE3A03000); // mov r3, #0
    WriteARM9U32(nds, 0x0200E7F4, 0xE1C030B6); // strh r3, [r0, #6]
    WriteARM9U32(nds, 0x0200E7F8, 0xE12FFF1E); // bx lr
    WriteARM9U32(nds, 0x0200E7FC, kPacketBridgeJitScratchPacketsAddr);

    invalidateMainRAM(0x0200E854, 0x0200E864);
    invalidateMainRAM(0x0200E7D0, 0x0200E7FC);

    G.PacketBridgeJitHelperPatchApplied[instanceID] = true;
    std::printf(
        "NSMB Test: packet bridge JIT keys/touch helper patch inst=%d frame=%u scratch=0x%08X\n",
        instanceID,
        frame,
        kPacketBridgeJitScratchBaseAddr);
}

void ForceStageSceneContinueGateIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForceStageSceneContinueGateEnabled || !nds || !nds->MainRAM)
        return;
    if (frame < G.ForceStageSceneContinueGateStartFrame)
        return;
    if (G.ForceStageSceneContinueGateEndFrame != 0 && frame > G.ForceStageSceneContinueGateEndFrame)
        return;
    if (G.ForceStageSceneContinueGateHostOnly && G.NetRole != Role::Host)
        return;
    if (G.ForceStageSceneContinueGateClientOnly && G.NetRole != Role::Client)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1)
        return;

    const ObjectScanSample stageScene = FindObjectByIDAndSettingsLoose(nds, kStageSceneObjectID, kMvlStageSceneSettings);
    if (!stageScene.Found || !IsARM9MainRAMAddress(stageScene.Base))
    {
        if (!G.ForceStageSceneContinueGateLogged[instanceID])
        {
            std::printf("NSMB Test: force stage scene continue gate skipped inst=%d frame=%u no stage scene\n",
                instanceID,
                frame);
            G.ForceStageSceneContinueGateLogged[instanceID] = true;
        }
        return;
    }

    nds->ARM9Write8(stageScene.Base + 0x5649, static_cast<melonDS::u8>(G.ForceStageSceneContinueGateValue & 0xFF));
    if (!G.ForceStageSceneContinueGateLogged[instanceID])
    {
        std::printf(
            "NSMB Test: force stage scene continue gate inst=%d frame=%u range=%u-%u base=%08X state=%u value=0x%02X\n",
            instanceID,
            frame,
            G.ForceStageSceneContinueGateStartFrame,
            G.ForceStageSceneContinueGateEndFrame,
            stageScene.Base,
            nds->ARM9Read32(stageScene.Base + 0x5618),
            G.ForceStageSceneContinueGateValue & 0xFF);
        G.ForceStageSceneContinueGateLogged[instanceID] = true;
    }
}

void ForceStageSceneState3GateIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForceStageSceneState3GateEnabled || !nds || !nds->MainRAM)
        return;
    if (frame < G.ForceStageSceneState3GateStartFrame)
        return;
    if (G.ForceStageSceneState3GateEndFrame != 0 && frame > G.ForceStageSceneState3GateEndFrame)
        return;
    if (G.ForceStageSceneState3GateHostOnly && G.NetRole != Role::Host)
        return;
    if (G.ForceStageSceneState3GateClientOnly && G.NetRole != Role::Client)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1)
        return;

    const melonDS::u32 oldValue = nds->ARM9Read32(0x020C92C0);
    nds->ARM9Write32(0x020C92C0, G.ForceStageSceneState3GateValue);

    if (!G.ForceStageSceneState3GateLogged[instanceID])
    {
        std::printf(
            "NSMB Test: force stage scene state3 gate inst=%d frame=%u range=%u-%u old=0x%08X value=0x%08X\n",
            instanceID,
            frame,
            G.ForceStageSceneState3GateStartFrame,
            G.ForceStageSceneState3GateEndFrame,
            oldValue,
            G.ForceStageSceneState3GateValue);
        G.ForceStageSceneState3GateLogged[instanceID] = true;
    }
}

void ForceStageSceneEventFlagsIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForceStageSceneEventFlagsEnabled || !nds || !nds->MainRAM)
        return;
    if (frame < G.ForceStageSceneEventFlagsStartFrame)
        return;
    if (G.ForceStageSceneEventFlagsEndFrame != 0 && frame > G.ForceStageSceneEventFlagsEndFrame)
        return;
    if (G.ForceStageSceneEventFlagsHostOnly && G.NetRole != Role::Host)
        return;
    if (G.ForceStageSceneEventFlagsClientOnly && G.NetRole != Role::Client)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1)
        return;

    const melonDS::u32 oldValue = nds->ARM9Read32(0x020C92D0);
    const melonDS::u32 newValue = oldValue | G.ForceStageSceneEventFlagsValue;
    nds->ARM9Write32(0x020C92D0, newValue);

    if (!G.ForceStageSceneEventFlagsLogged[instanceID])
    {
        std::printf(
            "NSMB Test: force stage scene event flags inst=%d frame=%u range=%u-%u old=0x%08X or=0x%08X new=0x%08X\n",
            instanceID,
            frame,
            G.ForceStageSceneEventFlagsStartFrame,
            G.ForceStageSceneEventFlagsEndFrame,
            oldValue,
            G.ForceStageSceneEventFlagsValue,
            newValue);
        G.ForceStageSceneEventFlagsLogged[instanceID] = true;
    }
}

void ForceMvlPlayerReadyIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForceMvlPlayerReadyEnabled || !nds || !nds->MainRAM)
        return;
    if (frame < G.ForceMvlPlayerReadyStartFrame)
        return;
    if (G.ForceMvlPlayerReadyEndFrame != 0 && frame > G.ForceMvlPlayerReadyEndFrame)
        return;
    if (G.ForceMvlPlayerReadyHostOnly && G.NetRole != Role::Host)
        return;
    if (G.ForceMvlPlayerReadyClientOnly && G.NetRole != Role::Client)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1)
        return;

    const melonDS::u32 managerBase = nds->ARM9Read32(0x020CAD40);
    if (!IsARM9MainRAMAddress(managerBase))
    {
        if (!G.ForceMvlPlayerReadyMissingLogged[instanceID])
        {
            std::printf("NSMB Test: force MvL player ready skipped inst=%d frame=%u invalid manager=%08X\n",
                instanceID,
                frame,
                managerBase);
            G.ForceMvlPlayerReadyMissingLogged[instanceID] = true;
        }
        return;
    }

    const melonDS::u16 value = static_cast<melonDS::u16>(G.ForceMvlPlayerReadyValue & 0xFFFF);
    const melonDS::u16 old0 = nds->ARM9Read16(managerBase + 0x494);
    const melonDS::u16 old1 = nds->ARM9Read16(managerBase + 0x4A0);
    const melonDS::u8 oldA8EC = nds->ARM9Read8(managerBase + 0xA8EC);
    nds->ARM9Write16(managerBase + 0x494, value);
    nds->ARM9Write16(managerBase + 0x4A0, value);
    if (G.ForceMvlPlayerReadySetA8EC)
        nds->ARM9Write8(managerBase + 0xA8EC, static_cast<melonDS::u8>(G.ForceMvlPlayerReadyA8ECValue & 0xFF));

    if (!G.ForceMvlPlayerReadyLogged[instanceID])
    {
        std::printf(
            "NSMB Test: force MvL player ready inst=%d frame=%u range=%u-%u manager=%08X old0=0x%04X old1=0x%04X value=0x%04X oldA8EC=0x%02X setA8EC=%u a8ecValue=0x%02X state=%u\n",
            instanceID,
            frame,
            G.ForceMvlPlayerReadyStartFrame,
            G.ForceMvlPlayerReadyEndFrame,
            managerBase,
            old0,
            old1,
            value,
            oldA8EC,
            G.ForceMvlPlayerReadySetA8EC ? 1 : 0,
            G.ForceMvlPlayerReadyA8ECValue & 0xFF,
            nds->ARM9Read8(0x020C9670));
        G.ForceMvlPlayerReadyLogged[instanceID] = true;
    }
}

void ForceMvlRuntimeStateIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForceMvlRuntimeStateEnabled || !nds || !nds->MainRAM)
        return;
    if (frame < G.ForceMvlRuntimeStateStartFrame)
        return;
    if (G.ForceMvlRuntimeStateEndFrame != 0 && frame > G.ForceMvlRuntimeStateEndFrame)
        return;
    if (G.ForceMvlRuntimeStateHostOnly && G.NetRole != Role::Host)
        return;
    if (G.ForceMvlRuntimeStateClientOnly && G.NetRole != Role::Client)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1)
        return;

    constexpr melonDS::u32 kMvlRuntimeStateAddr = 0x020CA6AC;
    const melonDS::u8 oldValue = nds->ARM9Read8(kMvlRuntimeStateAddr);
    const melonDS::u8 newValue = static_cast<melonDS::u8>(G.ForceMvlRuntimeStateValue & 0xFF);
    nds->ARM9Write8(kMvlRuntimeStateAddr, newValue);

    if (!G.ForceMvlRuntimeStateLogged[instanceID])
    {
        std::printf(
            "NSMB Test: force MvL runtime state inst=%d frame=%u range=%u-%u old=0x%02X value=0x%02X\n",
            instanceID,
            frame,
            G.ForceMvlRuntimeStateStartFrame,
            G.ForceMvlRuntimeStateEndFrame,
            oldValue,
            newValue);
        G.ForceMvlRuntimeStateLogged[instanceID] = true;
    }
}

void ForceMvlStageLayoutGateIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForceMvlStageLayoutGateEnabled || !nds || !nds->MainRAM)
        return;
    if (frame < G.ForceMvlStageLayoutGateStartFrame)
        return;
    if (G.ForceMvlStageLayoutGateEndFrame != 0 && frame > G.ForceMvlStageLayoutGateEndFrame)
        return;
    if (G.ForceMvlStageLayoutGateHostOnly && G.NetRole != Role::Host)
        return;
    if (G.ForceMvlStageLayoutGateClientOnly && G.NetRole != Role::Client)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1)
        return;

    const melonDS::u32 addr = G.ForceMvlStageLayoutGateAddr;
    if (!IsARM9MainRAMAddress(addr))
        return;

    const melonDS::u8 oldValue = nds->ARM9Read8(addr);
    const melonDS::u8 newValue = static_cast<melonDS::u8>(G.ForceMvlStageLayoutGateValue & 0xFF);
    nds->ARM9Write8(addr, newValue);

    if (!G.ForceMvlStageLayoutGateLogged[instanceID])
    {
        std::printf(
            "NSMB Test: force MvL StageLayout gate inst=%d frame=%u range=%u-%u addr=%08X old=0x%02X value=0x%02X\n",
            instanceID,
            frame,
            G.ForceMvlStageLayoutGateStartFrame,
            G.ForceMvlStageLayoutGateEndFrame,
            addr,
            oldValue,
            newValue);
        std::fflush(stdout);
        G.ForceMvlStageLayoutGateLogged[instanceID] = true;
    }
}

void ForceMvlStageLayoutBufferIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ForceMvlStageLayoutBufferEnabled || !nds || !nds->MainRAM)
        return;
    if (frame < G.ForceMvlStageLayoutBufferStartFrame)
        return;
    if (G.ForceMvlStageLayoutBufferEndFrame != 0 && frame > G.ForceMvlStageLayoutBufferEndFrame)
        return;
    if (G.ForceMvlStageLayoutBufferHostOnly && G.NetRole != Role::Host)
        return;
    if (G.ForceMvlStageLayoutBufferClientOnly && G.NetRole != Role::Client)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1)
        return;

    const melonDS::u32 stageLayout = nds->ARM9Read32(0x020CAD40);
    const melonDS::u32 buffer = G.ForceMvlStageLayoutBufferAddr;
    if (!IsARM9MainRAMAddress(stageLayout) || !IsARM9MainRAMAddress(buffer) || !IsARM9MainRAMAddress(buffer + 0x1FFC))
        return;

    if (!G.ForceMvlStageLayoutBufferApplied[instanceID])
    {
        for (melonDS::u32 off = 0; off < 0x2000; off += 4)
            nds->ARM9Write32(buffer + off, 0);
        G.ForceMvlStageLayoutBufferApplied[instanceID] = true;
    }

    const melonDS::u32 oldValue = nds->ARM9Read32(stageLayout + 0xA8CC);
    nds->ARM9Write32(stageLayout + 0xA8CC, buffer);

    if (G.ForceMvlStageLayoutBufferApplied[instanceID])
    {
        static bool logged[16] {};
        if (!logged[instanceID])
        {
            std::printf(
                "NSMB Test: force MvL StageLayout buffer inst=%d frame=%u range=%u-%u stageLayout=%08X old=0x%08X buffer=%08X\n",
                instanceID,
                frame,
                G.ForceMvlStageLayoutBufferStartFrame,
                G.ForceMvlStageLayoutBufferEndFrame,
                stageLayout,
                oldValue,
                buffer);
            std::fflush(stdout);
            logged[instanceID] = true;
        }
    }
}

bool CallMvlStageLayoutInitIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.CallMvlStageLayoutInitEnabled || !nds || !nds->MainRAM)
        return false;
    if (instanceID < 0 || instanceID >= 16)
        return false;
    if (G.CallMvlStageLayoutInitApplied[instanceID] || frame < G.CallMvlStageLayoutInitFrame)
        return false;
    if (G.CallMvlStageLayoutInitHostOnly && G.NetRole != Role::Host)
        return false;
    if (G.CallMvlStageLayoutInitClientOnly && G.NetRole != Role::Client)
        return false;
    if (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1)
        return false;

    const melonDS::u32 stageLayout = nds->ARM9Read32(0x020CAD40);
    if (!IsARM9MainRAMAddress(stageLayout))
        return false;

    const melonDS::u32 oldPC = nds->ARM9.R[15] - ((nds->ARM9.CPSR & 0x20) ? 2 : 4);
    const melonDS::u32 returnPC = oldPC | ((nds->ARM9.CPSR & 0x20) ? 1u : 0u);
    const melonDS::u32 playerID = nds->ARM9Read32(kGameLocalPlayerIDAddr) & 1u;

    std::vector<melonDS::u32> code;
    code.reserve(32);
    EmitARM(code, 0xE92D5FFFu); // push {r0-r12, lr}
    EmitARM(code, 0xE10F5000u); // mrs r5, cpsr
    EmitARM(code, 0xE92D0020u); // push {r5}
    EmitLoadImm(code, 0, stageLayout);
    EmitLoadImm(code, 1, playerID);
    EmitARM(code, 0xE59FC008u); // ldr ip, [pc, #8]
    EmitARM(code, 0xE28FE008u); // add lr, pc, #8
    EmitARM(code, 0xE12FFF1Cu); // bx ip
    EmitARM(code, 0xE1A00000u); // nop
    EmitARM(code, kA2DEStageLayoutMvlInitAddr);
    EmitARM(code, 0xE8BD0020u); // pop {r5}
    EmitARM(code, 0xE128F005u); // msr apsr_nzcvq, r5
    EmitARM(code, 0xE8BD5FFFu); // pop {r0-r12, lr}
    EmitARM(code, 0xE59FC004u); // ldr ip, [pc, #4]
    EmitARM(code, 0xE12FFF1Cu); // bx ip
    EmitARM(code, 0xE1A00000u); // nop
    EmitARM(code, returnPC);

    for (size_t i = 0; i < code.size(); i++)
    {
        if (!WriteARM9U32(nds, kDirectBootTrampolineAddr + static_cast<melonDS::u32>(i * sizeof(melonDS::u32)), code[i]))
            return false;
    }

    G.CallMvlStageLayoutInitApplied[instanceID] = true;
    std::printf(
        "NSMB Test: call MvL StageLayout init inst=%d frame=%u trampoline=%08X return=%08X stageLayout=%08X player=%u\n",
        instanceID,
        frame,
        kDirectBootTrampolineAddr,
        returnPC,
        stageLayout,
        playerID);
    std::fflush(stdout);
    nds->ARM9.JumpTo(kDirectBootTrampolineAddr);
    return true;
}

bool WriteObjectPositionByGUID(melonDS::NDS* nds, melonDS::u32 guid, melonDS::u32 posX, melonDS::u32 posY, melonDS::u32 posZ)
{
    if (!nds || !nds->MainRAM || guid == 0)
        return false;

    const melonDS::u32 ramLen = nds->MainRAMMask + 1;
    if (ramLen < 0x120)
        return false;

    for (melonDS::u32 off = 0; off <= ramLen - 0x120; off += 4)
    {
        melonDS::u32 candidateGUID = 0;
        if (!ReadMainRAMU32(nds, off + 4, candidateGUID) || candidateGUID != guid)
            continue;

        melonDS::u32 vtable = 0;
        melonDS::u16 objectID = 0;
        melonDS::u16 stateType = 0;
        melonDS::u32 flags = 0;
        if (!ReadMainRAMU32(nds, off, vtable) ||
            !ReadMainRAMU16(nds, off + 0x0C, objectID) ||
            !ReadMainRAMU16(nds, off + 0x0E, stateType) ||
            !ReadMainRAMU32(nds, off + 0x10, flags))
            continue;

        if (vtable < kMainRAMBase || vtable >= kMainRAMBase + ramLen)
            continue;
        if (objectID == 0 || objectID >= 0x400)
            continue;
        if (stateType != 1 && stateType != 2 && stateType != 3)
            continue;
        if (flags >= 0x10000000)
            continue;

        WriteMainRAMU32(nds, off + 0x60, posX);
        WriteMainRAMU32(nds, off + 0x64, posY);
        WriteMainRAMU32(nds, off + 0x68, posZ);
        return true;
    }

    return false;
}

bool WriteObjectPositionByIDAndSettings(
    melonDS::NDS* nds,
    melonDS::u16 expectedObjectID,
    melonDS::u32 expectedSettings,
    melonDS::u32 posX,
    melonDS::u32 posY,
    melonDS::u32 posZ)
{
    ObjectScanSample actor = FindObjectByIDAndSettings(nds, expectedObjectID, expectedSettings);
    if (!actor.Found || actor.Base < kMainRAMBase)
        return false;

    const melonDS::u32 off = actor.Base - kMainRAMBase;
    WriteMainRAMU32(nds, off + 0x60, posX);
    WriteMainRAMU32(nds, off + 0x64, posY);
    WriteMainRAMU32(nds, off + 0x68, posZ);
    WriteMainRAMU32(nds, off + 0x70, posX);
    WriteMainRAMU32(nds, off + 0x74, posY);
    WriteMainRAMU32(nds, off + 0x78, posZ);
    return true;
}

bool WriteVsBattleStarCandidatePosition(melonDS::NDS* nds, melonDS::u32 posX, melonDS::u32 posY, melonDS::u32 posZ)
{
    ObjectScanSample actor = FindVsBattleStarCandidate(nds);
    if (!actor.Found || actor.Base < kMainRAMBase)
        return false;

    const melonDS::u32 off = actor.Base - kMainRAMBase;
    WriteMainRAMU32(nds, off + 0x60, posX);
    WriteMainRAMU32(nds, off + 0x64, posY);
    WriteMainRAMU32(nds, off + 0x68, posZ);
    WriteMainRAMU32(nds, off + 0x70, posX);
    WriteMainRAMU32(nds, off + 0x74, posY);
    WriteMainRAMU32(nds, off + 0x78, posZ);
    return true;
}

bool WriteObjectTransformByGUID(
    melonDS::NDS* nds,
    melonDS::u32 guid,
    melonDS::u32 posX,
    melonDS::u32 posY,
    melonDS::u32 posZ,
    melonDS::u32 prevX,
    melonDS::u32 prevY,
    melonDS::u32 prevZ,
    melonDS::u32 velX,
    melonDS::u32 velY,
    melonDS::u32 velZ)
{
    if (!nds || !nds->MainRAM || guid == 0)
        return false;

    const melonDS::u32 ramLen = nds->MainRAMMask + 1;
    if (ramLen < 0x120)
        return false;

    for (melonDS::u32 off = 0; off <= ramLen - 0x120; off += 4)
    {
        melonDS::u32 candidateGUID = 0;
        if (!ReadMainRAMU32(nds, off + 4, candidateGUID) || candidateGUID != guid)
            continue;

        melonDS::u32 vtable = 0;
        melonDS::u16 objectID = 0;
        melonDS::u16 stateType = 0;
        melonDS::u32 flags = 0;
        if (!ReadMainRAMU32(nds, off, vtable) ||
            !ReadMainRAMU16(nds, off + 0x0C, objectID) ||
            !ReadMainRAMU16(nds, off + 0x0E, stateType) ||
            !ReadMainRAMU32(nds, off + 0x10, flags))
            continue;

        if (vtable < kMainRAMBase || vtable >= kMainRAMBase + ramLen)
            continue;
        if (objectID == 0 || objectID >= 0x400)
            continue;
        if (stateType != 1 && stateType != 2 && stateType != 3)
            continue;
        if (flags >= 0x10000000)
            continue;

        WriteMainRAMU32(nds, off + 0x60, posX);
        WriteMainRAMU32(nds, off + 0x64, posY);
        WriteMainRAMU32(nds, off + 0x68, posZ);
        WriteMainRAMU32(nds, off + 0x70, prevX);
        WriteMainRAMU32(nds, off + 0x74, prevY);
        WriteMainRAMU32(nds, off + 0x78, prevZ);
        WriteMainRAMU32(nds, off + 0xD0, velX);
        WriteMainRAMU32(nds, off + 0xD4, velY);
        WriteMainRAMU32(nds, off + 0xD8, velZ);
        return true;
    }

    return false;
}

bool FindLatestRemoteGameStateLocked(int instanceID, melonDS::u32 frame, GameStateSample& sample, melonDS::u32& sampleFrame)
{
    bool found = false;
    melonDS::u32 bestFrame = 0;
    const melonDS::u64 instancePrefix = static_cast<melonDS::u64>(static_cast<melonDS::u32>(instanceID)) << 32;
    for (const auto& [key, value] : G.RemoteGameStateSamples)
    {
        if ((key & 0xFFFFFFFF00000000ull) != instancePrefix)
            continue;
        const melonDS::u32 candidateFrame = static_cast<melonDS::u32>(key & 0xFFFFFFFFu);
        if (candidateFrame > frame)
            continue;
        if (found && candidateFrame <= bestFrame)
            continue;
        found = true;
        bestFrame = candidateFrame;
        sample = value;
    }

    sampleFrame = bestFrame;
    return found;
}

void ApplyRemoteGameState(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.Enabled || !G.GameStateApplyEnabled || G.NetRole != Role::Client) return;
    if (instanceID < 0 || instanceID >= 16 || !nds || !nds->MainRAM) return;
    if (frame < G.NetplayStartFrame) return;

    GameStateSample sample;
    melonDS::u32 sampleFrame = 0;
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        PumpNetworkLocked();
        if (!FindLatestRemoteGameStateLocked(instanceID, frame, sample, sampleFrame))
            return;
    }

    if (G.GameStateApplyCriticalGlobals)
    {
        nds->ARM9Write32(kNetRandomValueAddr, sample.NetRandomValue);
        nds->ARM9Write8(kNetRandomCallCountAddr, static_cast<melonDS::u8>(sample.NetRandomCallCount & 0xFF));
        nds->ARM9Write32(kNetRandomBranchAddressAddr, sample.NetRandomBranchAddress);
        nds->ARM9Write32(kGamePlayerCountAddr, sample.PlayerCount);
        nds->ARM9Write32(kGamePlayerBattleStarsAddr, sample.Player0BattleStars);
        nds->ARM9Write32(kGamePlayerBattleStarsAddr + sizeof(melonDS::u32), sample.Player1BattleStars);
        nds->ARM9Write32(kGamePlayerCoinsAddr, sample.Player0Coins);
        nds->ARM9Write32(kGamePlayerCoinsAddr + sizeof(melonDS::u32), sample.Player1Coins);
        nds->ARM9Write32(kGamePlayerScoreAddr, sample.Player0Score);
        nds->ARM9Write32(kGamePlayerScoreAddr + sizeof(melonDS::u32), sample.Player1Score);
        nds->ARM9Write32(kGamePlayerDisplayedStarsAddr, sample.Player0DisplayedStars);
        nds->ARM9Write32(kGamePlayerDisplayedStarsAddr + sizeof(melonDS::u32), sample.Player1DisplayedStars);
        nds->ARM9Write32(kGamePlayerDeathsAddr, sample.Player0Deaths);
        nds->ARM9Write32(kGamePlayerDeathsAddr + sizeof(melonDS::u32), sample.Player1Deaths);
        nds->ARM9Write32(kGamePlayerCollectedStarsAddr, sample.Player0CollectedStars);
        nds->ARM9Write32(kGamePlayerCollectedStarsAddr + sizeof(melonDS::u32), sample.Player1CollectedStars);
        nds->ARM9Write32(kGameVsCoinCountAddr, sample.VsCoinCount);
    }

    if (G.GameStateApplyStageObjects && sample.StageCameraFound)
    {
        WriteObjectWordByIDAndSettings(nds, kStageCameraObjectID, 0, 0x190, sample.StageCameraWord190);
        WriteObjectWordByIDAndSettings(nds, kStageCameraObjectID, 0, 0x194, sample.StageCameraWord194);
        WriteObjectWordByIDAndSettings(nds, kStageCameraObjectID, 0, 0x19C, sample.StageCameraWord19C);
        WriteObjectWordByIDAndSettings(nds, kStageCameraObjectID, 0, 0x1A0, sample.StageCameraWord1A0);
    }
    if (G.GameStateApplyStageObjects && sample.StageSceneFound)
    {
        WriteObjectWordByIDAndSettings(nds, kStageSceneObjectID, kMvlStageSceneSettings, 0x154, sample.StageSceneWord154);
        WriteObjectWordByIDAndSettings(nds, kStageSceneObjectID, kMvlStageSceneSettings, 0x160, sample.StageSceneWord160);
    }
    if (G.GameStateApplyStageObjects && sample.MovingHazardFound)
    {
        WriteObjectTransformByGUID(
            nds,
            sample.MovingHazardGUID,
            sample.MovingHazardPosX,
            sample.MovingHazardPosY,
            sample.MovingHazardPosZ,
            sample.MovingHazardPosX,
            sample.MovingHazardPosY,
            sample.MovingHazardPosZ,
            sample.MovingHazardVelX,
            sample.MovingHazardVelY,
            0);
    }
    if (G.GameStateApplyStarObjects && sample.VsStarFound)
    {
        if (!WriteObjectPositionByGUID(nds, sample.VsStarGUID, sample.VsStarPosX, sample.VsStarPosY, sample.VsStarPosZ))
            WriteVsBattleStarCandidatePosition(nds, sample.VsStarPosX, sample.VsStarPosY, sample.VsStarPosZ);
    }
    else if (G.GameStateApplyStarObjects)
    {
        WriteVsBattleStarCandidatePosition(nds, 0, 0, 0);
    }
    if (G.GameStateApplyStarObjects && sample.VsStarActorFound)
    {
        if (!WriteObjectPositionByGUID(nds, sample.VsStarActorGUID, sample.VsStarActorPosX, sample.VsStarActorPosY, sample.VsStarActorPosZ))
        {
            WriteObjectPositionByIDAndSettings(
                nds,
                kVsBattleStarActorObjectID,
                kVsBattleStarActorSettings,
                sample.VsStarActorPosX,
                sample.VsStarActorPosY,
                sample.VsStarActorPosZ);
        }
    }
    else if (G.GameStateApplyStarObjects)
    {
        WriteObjectPositionByIDAndSettings(nds, kVsBattleStarActorObjectID, kVsBattleStarActorSettings, 0, 0, 0);
    }
    if (G.GameStateApplyPlayerActors && sample.PlayerActor0Found)
        WriteObjectTransformByGUID(
            nds,
            sample.PlayerActor0GUID,
            sample.PlayerActor0PosX,
            sample.PlayerActor0PosY,
            sample.PlayerActor0PosZ,
            sample.PlayerActor0PrevX,
            sample.PlayerActor0PrevY,
            sample.PlayerActor0PrevZ,
            sample.PlayerActor0VelX,
            sample.PlayerActor0VelY,
            sample.PlayerActor0VelZ);
    if (G.GameStateApplyPlayerActors && sample.PlayerActor1Found)
        WriteObjectTransformByGUID(
            nds,
            sample.PlayerActor1GUID,
            sample.PlayerActor1PosX,
            sample.PlayerActor1PosY,
            sample.PlayerActor1PosZ,
            sample.PlayerActor1PrevX,
            sample.PlayerActor1PrevY,
            sample.PlayerActor1PrevZ,
            sample.PlayerActor1VelX,
            sample.PlayerActor1VelY,
            sample.PlayerActor1VelZ);

    if (G.InputTraceEnabled &&
        (G.InputTraceInterval <= 1 || (frame % static_cast<melonDS::u32>(G.InputTraceInterval)) == 0))
    {
        std::printf("NSMB PoC: applied remote game state inst=%d frame=%u sampleFrame=%u\n",
            instanceID,
            frame,
            sampleFrame);
    }
}

GameStateSample ReadGameStateSample(melonDS::NDS* nds)
{
    GameStateSample sample;
    if (!nds || !nds->MainRAM)
        return sample;

    sample.StageID = nds->ARM9Read32(kGameStageIDAddr);
    sample.StageGroup = nds->ARM9Read32(kGameStageGroupAddr);
    sample.VsMode = nds->ARM9Read32(kGameVsModeAddr);
    sample.LocalPlayerID = nds->ARM9Read32(kGameLocalPlayerIDAddr);
    sample.Arm9PC = nds->ARM9.R[15] - ((nds->ARM9.CPSR & 0x20) ? 2 : 4);
    sample.Arm9LR = nds->ARM9.R[14];
    sample.Arm9SP = nds->ARM9.R[13];
    sample.Arm9CPSR = nds->ARM9.CPSR;
    sample.AppFrameLength = nds->ARM9Read8(kAppFrameLengthAddr);
    sample.AppUpdateTask = nds->ARM9Read32(kAppUpdateTaskAddr);
    sample.AppSleepPhase = nds->ARM9Read8(kAppSleepPhaseAddr);
    sample.AppSleepControl = nds->ARM9Read8(kAppSleepControlAddr);
    sample.AppSleeping = nds->ARM9Read8(kAppSleepingAddr);
    sample.AppSleepPhaseTimer = nds->ARM9Read16(kAppSleepPhaseTimerAddr);
    sample.AppSleepWakeUpTimer = nds->ARM9Read16(kAppSleepWakeUpTimerAddr);
    sample.AppBootParam = nds->ARM9Read32(kAppBootParamAddr);
    sample.AppBootTarget = nds->ARM9Read32(kAppBootTargetAddr);
    sample.AppBootScene = nds->ARM9Read32(kAppBootSceneAddr);
    sample.GGID = nds->ARM9Read32(kNetGGIDAddr);
    sample.NetCurrentLanguage = nds->ARM9Read32(kNetCurrentLanguageAddr);
    sample.NetLocalAid = nds->ARM9Read32(kNetLocalAidAddr);
    sample.NetState14 = nds->ARM9Read32(kNetState14Addr);
    sample.NetState1C = nds->ARM9Read32(kNetState1CAddr);
    sample.NetState20 = nds->ARM9Read32(kNetState20Addr);
    sample.NetState24 = nds->ARM9Read32(kNetState24Addr);
    sample.NetExpectedConsoleCount = nds->ARM9Read32(kNetExpectedConsoleCountAddr);
    sample.NetMultiBootSession = nds->ARM9Read32(kNetMultiBootSessionAddr);
    sample.NetSessionState = nds->ARM9Read32(kNetSessionStateAddr);
    sample.NetModuleState = nds->ARM9Read32(kNetModuleStateAddr);
    sample.NetMaxSessionChildren = nds->ARM9Read32(kNetMaxSessionChildrenAddr);
    sample.NetMaxConsoleCount = nds->ARM9Read32(kNetMaxConsoleCountAddr);
    sample.NetState5C = nds->ARM9Read16(kNetState5CAddr);
    sample.NetPacketTick = nds->ARM9Read16(kNetPacketTickAddr);
    sample.NetPacketKeys = nds->ARM9Read16(kNetPacketKeysAddr);
    sample.NetPacketAction = nds->ARM9Read8(kNetPacketActionAddr);
    sample.NetPacketByte5 = nds->ARM9Read8(kNetPacketByte5Addr);
    sample.NetPacketByte6 = nds->ARM9Read8(kNetPacketByte6Addr);
    sample.NetPacketByte7 = nds->ARM9Read8(kNetPacketByte7Addr);
    sample.NetRandomValue = nds->ARM9Read32(kNetRandomValueAddr);
    sample.NetRandomCallCount = nds->ARM9Read8(kNetRandomCallCountAddr);
    sample.NetRandomBranchAddress = nds->ARM9Read32(kNetRandomBranchAddressAddr);
    sample.InputConsole0Held = nds->ARM9Read16(kInputConsoleKeysAddr + 0x0);
    sample.InputConsole0Pressed = nds->ARM9Read16(kInputConsoleKeysAddr + 0x2);
    sample.InputConsole1Held = nds->ARM9Read16(kInputConsoleKeysAddr + 0x4);
    sample.InputConsole1Pressed = nds->ARM9Read16(kInputConsoleKeysAddr + 0x6);
    sample.InputPlayer0Held = nds->ARM9Read16(kInputPlayerKeysHeldAddr + 0x0);
    sample.InputPlayer1Held = nds->ARM9Read16(kInputPlayerKeysHeldAddr + 0x2);
    sample.InputPlayer0Pressed = nds->ARM9Read16(kInputPlayerKeysPressedAddr + 0x0);
    sample.InputPlayer1Pressed = nds->ARM9Read16(kInputPlayerKeysPressedAddr + 0x2);
    sample.StageActorFreezeFlag = nds->ARM9Read8(kStageActorFreezeFlagAddr);
    sample.SceneIsSceneActive = nds->ARM9Read32(kSceneIsSceneActiveAddr);
    sample.ScenePreviousSceneID = nds->ARM9Read16(kScenePreviousSceneIDAddr);
    sample.SceneNextSceneID = nds->ARM9Read16(kSceneNextSceneIDAddr);
    sample.SceneCurrentSceneID = nds->ARM9Read16(kSceneCurrentSceneIDAddr);
    sample.SceneNextSceneSettings = nds->ARM9Read32(kSceneNextSceneSettingsAddr);

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

    const ObjectScanSample starActor = FindObjectByIDAndSettings(nds, kVsBattleStarActorObjectID, kVsBattleStarActorSettings);
    sample.VsStarActorFound = starActor.Found;
    sample.VsStarActorGUID = starActor.GUID;
    sample.VsStarActorBase = starActor.Base;
    sample.VsStarActorSettings = starActor.Settings;
    sample.VsStarActorStateType = starActor.StateType;
    sample.VsStarActorFlags = starActor.Flags;
    sample.VsStarActorPosX = starActor.PosX;
    sample.VsStarActorPosY = starActor.PosY;
    sample.VsStarActorPosZ = starActor.PosZ;

    const PlayerActorScanSample players = FindPlayerActors(nds);
    sample.PlayerActor0Found = players.Actor0.Found;
    sample.PlayerActor0GUID = players.Actor0.GUID;
    sample.PlayerActor0Base = players.Actor0.Base;
    sample.PlayerActor0Settings = players.Actor0.Settings;
    sample.PlayerActor0StateType = players.Actor0.StateType;
    sample.PlayerActor0Flags = players.Actor0.Flags;
    sample.PlayerActor0PosX = players.Actor0.PosX;
    sample.PlayerActor0PosY = players.Actor0.PosY;
    sample.PlayerActor0PosZ = players.Actor0.PosZ;
    sample.PlayerActor0PrevX = players.Actor0.PrevX;
    sample.PlayerActor0PrevY = players.Actor0.PrevY;
    sample.PlayerActor0PrevZ = players.Actor0.PrevZ;
    sample.PlayerActor0VelX = players.Actor0.VelX;
    sample.PlayerActor0VelY = players.Actor0.VelY;
    sample.PlayerActor0VelZ = players.Actor0.VelZ;
    sample.PlayerActor1Found = players.Actor1.Found;
    sample.PlayerActor1GUID = players.Actor1.GUID;
    sample.PlayerActor1Base = players.Actor1.Base;
    sample.PlayerActor1Settings = players.Actor1.Settings;
    sample.PlayerActor1StateType = players.Actor1.StateType;
    sample.PlayerActor1Flags = players.Actor1.Flags;
    sample.PlayerActor1PosX = players.Actor1.PosX;
    sample.PlayerActor1PosY = players.Actor1.PosY;
    sample.PlayerActor1PosZ = players.Actor1.PosZ;
    sample.PlayerActor1PrevX = players.Actor1.PrevX;
    sample.PlayerActor1PrevY = players.Actor1.PrevY;
    sample.PlayerActor1PrevZ = players.Actor1.PrevZ;
    sample.PlayerActor1VelX = players.Actor1.VelX;
    sample.PlayerActor1VelY = players.Actor1.VelY;
    sample.PlayerActor1VelZ = players.Actor1.VelZ;
    auto readPlayerTransitionFields = [nds](const ObjectScanSample& actor,
                                            melonDS::u32& playerID,
                                            melonDS::u32& transitionStep,
                                            melonDS::u32& signalLock,
                                            melonDS::u32& flag192,
                                            melonDS::u32& flags728,
                                            melonDS::u32& flags72C,
                                            melonDS::u32& flags730,
                                            melonDS::u32& actionFlag,
                                            melonDS::u32& subActionFlag,
                                            melonDS::u32& physicsFlag,
                                            melonDS::u32& damageCooldown,
                                            melonDS::u32& transitFunc,
                                            melonDS::u32& transitArg)
    {
        if (!actor.Found || !IsARM9MainRAMAddress(actor.Base))
            return;
        playerID = nds->ARM9Read8(actor.Base + 0x11E);
        transitionStep = nds->ARM9Read8(actor.Base + 0xBAD);
        signalLock = nds->ARM9Read8(actor.Base + 0x75C);
        flag192 = nds->ARM9Read8(actor.Base + 0x192);
        flags728 = nds->ARM9Read32(actor.Base + 0x728);
        flags72C = nds->ARM9Read32(actor.Base + 0x72C);
        flags730 = nds->ARM9Read32(actor.Base + 0x730);
        actionFlag = nds->ARM9Read32(actor.Base + kPlayerBaseActionFlagOffset);
        subActionFlag = nds->ARM9Read32(actor.Base + kPlayerBaseSubActionFlagOffset);
        physicsFlag = nds->ARM9Read32(actor.Base + kPlayerBasePhysicsFlagOffset);
        damageCooldown = nds->ARM9Read16(actor.Base + kPlayerBaseDamageCooldownOffset);
        transitFunc = nds->ARM9Read32(actor.Base + 0x990);
        transitArg = nds->ARM9Read32(actor.Base + 0x994);
    };
    readPlayerTransitionFields(
        players.Actor0,
        sample.PlayerActor0PlayerID,
        sample.PlayerActor0TransitionStep,
        sample.PlayerActor0SignalLock,
        sample.PlayerActor0Flag192,
        sample.PlayerActor0Flags728,
        sample.PlayerActor0Flags72C,
        sample.PlayerActor0Flags730,
        sample.PlayerActor0ActionFlag,
        sample.PlayerActor0SubActionFlag,
        sample.PlayerActor0PhysicsFlag,
        sample.PlayerActor0DamageCooldown,
        sample.PlayerActor0TransitFunc,
        sample.PlayerActor0TransitArg);
    readPlayerTransitionFields(
        players.Actor1,
        sample.PlayerActor1PlayerID,
        sample.PlayerActor1TransitionStep,
        sample.PlayerActor1SignalLock,
        sample.PlayerActor1Flag192,
        sample.PlayerActor1Flags728,
        sample.PlayerActor1Flags72C,
        sample.PlayerActor1Flags730,
        sample.PlayerActor1ActionFlag,
        sample.PlayerActor1SubActionFlag,
        sample.PlayerActor1PhysicsFlag,
        sample.PlayerActor1DamageCooldown,
        sample.PlayerActor1TransitFunc,
        sample.PlayerActor1TransitArg);

    auto readPlayerBaseRuntimeFields = [nds](const ObjectScanSample& actor,
                                             melonDS::u32& linkedActor,
                                             melonDS::u32& transitionFlag,
                                             melonDS::u32& collisionFlag,
                                             melonDS::u32& environmentFlag,
                                             melonDS::u32& updateLocked,
                                             melonDS::u32& characterID,
                                             melonDS::u32& transitioningFlag,
                                             melonDS::u32& defeatedFlag,
                                             melonDS::u32& playerBaseID,
                                             melonDS::u32& visibleFlag)
    {
        if (!actor.Found || !IsARM9MainRAMAddress(actor.Base))
            return;
        linkedActor = nds->ARM9Read32(actor.Base + kPlayerBaseLinkedActorOffset);
        transitionFlag = nds->ARM9Read32(actor.Base + kPlayerBaseTransitionFlagOffset);
        collisionFlag = nds->ARM9Read32(actor.Base + kPlayerBaseCollisionFlagOffset);
        environmentFlag = nds->ARM9Read32(actor.Base + kPlayerBaseEnvironmentFlagOffset);
        updateLocked = nds->ARM9Read8(actor.Base + kPlayerBaseUpdateLockedOffset);
        characterID = nds->ARM9Read8(actor.Base + kPlayerBaseCharacterIDOffset);
        transitioningFlag = nds->ARM9Read8(actor.Base + kPlayerBaseTransitioningFlagOffset);
        defeatedFlag = nds->ARM9Read8(actor.Base + kPlayerBaseDefeatedFlagOffset);
        playerBaseID = nds->ARM9Read8(actor.Base + kPlayerBasePlayerIDOffset);
        visibleFlag = nds->ARM9Read8(actor.Base + kPlayerBaseVisibleFlagOffset);
    };
    readPlayerBaseRuntimeFields(
        players.Actor0,
        sample.PlayerActor0LinkedActor,
        sample.PlayerActor0TransitionFlag,
        sample.PlayerActor0CollisionFlag,
        sample.PlayerActor0EnvironmentFlag,
        sample.PlayerActor0UpdateLocked,
        sample.PlayerActor0CharacterIDBase,
        sample.PlayerActor0TransitioningFlag,
        sample.PlayerActor0DefeatedFlag,
        sample.PlayerActor0PlayerBaseID,
        sample.PlayerActor0VisibleFlag);
    readPlayerBaseRuntimeFields(
        players.Actor1,
        sample.PlayerActor1LinkedActor,
        sample.PlayerActor1TransitionFlag,
        sample.PlayerActor1CollisionFlag,
        sample.PlayerActor1EnvironmentFlag,
        sample.PlayerActor1UpdateLocked,
        sample.PlayerActor1CharacterIDBase,
        sample.PlayerActor1TransitioningFlag,
        sample.PlayerActor1DefeatedFlag,
        sample.PlayerActor1PlayerBaseID,
        sample.PlayerActor1VisibleFlag);

    sample.PlayerCount = nds->ARM9Read32(kGamePlayerCountAddr);
    sample.PlayerTransitionStatus0 = nds->ARM9Read32(kGamePlayerTransitionStatusAddr);
    sample.PlayerTransitionStatus1 = nds->ARM9Read32(kGamePlayerTransitionStatusAddr + sizeof(melonDS::u32));
    sample.EntranceSpawnID0 = nds->ARM9Read8(kEntranceSpawnEntranceIDAddr);
    sample.EntranceSpawnID1 = nds->ARM9Read8(kEntranceSpawnEntranceIDAddr + 1);
    sample.EntranceTransitionFlags0 = nds->ARM9Read8(kEntranceTransitionFlagsAddr);
    sample.EntranceTransitionFlags1 = nds->ARM9Read8(kEntranceTransitionFlagsAddr + 1);
    sample.EntranceSpawnPtr0 = nds->ARM9Read32(kEntranceSpawnEntranceAddr);
    sample.EntranceSpawnPtr1 = nds->ARM9Read32(kEntranceSpawnEntranceAddr + sizeof(melonDS::u32));
    sample.Player0Powerup = nds->ARM9Read8(kGamePlayerPowerupAddr);
    sample.Player1Powerup = nds->ARM9Read8(kGamePlayerPowerupAddr + 1);
    sample.Player0InventoryPowerup = nds->ARM9Read8(kGamePlayerInventoryPowerupAddr);
    sample.Player1InventoryPowerup = nds->ARM9Read8(kGamePlayerInventoryPowerupAddr + 1);
    sample.Player0Dead = nds->ARM9Read8(kGamePlayerDeadAddr);
    sample.Player1Dead = nds->ARM9Read8(kGamePlayerDeadAddr + 1);
    sample.Player0Character = nds->ARM9Read8(kGamePlayerCharacterAddr);
    sample.Player1Character = nds->ARM9Read8(kGamePlayerCharacterAddr + 1);
    sample.Player0Lives = nds->ARM9Read32(kGamePlayerLivesAddr);
    sample.Player1Lives = nds->ARM9Read32(kGamePlayerLivesAddr + sizeof(melonDS::u32));
    sample.Player0BattleStars = nds->ARM9Read32(kGamePlayerBattleStarsAddr);
    sample.Player1BattleStars = nds->ARM9Read32(kGamePlayerBattleStarsAddr + sizeof(melonDS::u32));
    sample.Player0Coins = nds->ARM9Read32(kGamePlayerCoinsAddr);
    sample.Player1Coins = nds->ARM9Read32(kGamePlayerCoinsAddr + sizeof(melonDS::u32));
    sample.Player0Score = nds->ARM9Read32(kGamePlayerScoreAddr);
    sample.Player1Score = nds->ARM9Read32(kGamePlayerScoreAddr + sizeof(melonDS::u32));
    sample.Player0DisplayedStars = nds->ARM9Read32(kGamePlayerDisplayedStarsAddr);
    sample.Player1DisplayedStars = nds->ARM9Read32(kGamePlayerDisplayedStarsAddr + sizeof(melonDS::u32));
    sample.Player0Deaths = nds->ARM9Read32(kGamePlayerDeathsAddr);
    sample.Player1Deaths = nds->ARM9Read32(kGamePlayerDeathsAddr + sizeof(melonDS::u32));
    sample.Player0CollectedStars = nds->ARM9Read32(kGamePlayerCollectedStarsAddr);
    sample.Player1CollectedStars = nds->ARM9Read32(kGamePlayerCollectedStarsAddr + sizeof(melonDS::u32));
    sample.VsCoinCount = nds->ARM9Read32(kGameVsCoinCountAddr);

    sample.StageCameraGlobalX0 = nds->ARM9Read32(kStageCameraXAddr);
    sample.StageCameraGlobalX1 = nds->ARM9Read32(kStageCameraXAddr + sizeof(melonDS::u32));
    sample.StageCameraGlobalY0 = nds->ARM9Read32(kStageCameraYAddr);
    sample.StageCameraGlobalY1 = nds->ARM9Read32(kStageCameraYAddr + sizeof(melonDS::u32));
    sample.StageCameraGlobalWidth0 = nds->ARM9Read32(kStageCameraWidthAddr);
    sample.StageCameraGlobalWidth1 = nds->ARM9Read32(kStageCameraWidthAddr + sizeof(melonDS::u32));
    sample.StageCameraGlobalHeight0 = nds->ARM9Read32(kStageCameraHeightAddr);
    sample.StageCameraGlobalHeight1 = nds->ARM9Read32(kStageCameraHeightAddr + sizeof(melonDS::u32));
    sample.StageDisplayCameraX = nds->ARM9Read32(kStageDisplayCameraXAddr);

    ObjectScanSample stageCamera = FindObjectByIDAndSettings(nds, kStageCameraObjectID, 0);
    if (!stageCamera.Found)
        stageCamera = FindObjectByIDAndSettingsLoose(nds, kStageCameraObjectID, 0);
    if (stageCamera.Found)
    {
        sample.StageCameraFound = 1;
        sample.StageCameraBase = stageCamera.Base;
        if (IsARM9MainRAMAddress(stageCamera.Base))
        {
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
    ObjectScanSample stageScene = FindObjectByIDAndSettings(nds, kStageSceneObjectID, kMvlStageSceneSettings);
    if (!stageScene.Found)
        stageScene = FindObjectByIDAndSettingsLoose(nds, kStageSceneObjectID, kMvlStageSceneSettings);
    if (stageScene.Found)
    {
        sample.StageSceneFound = 1;
        sample.StageSceneBase = stageScene.Base;
        sample.StageSceneSettings = stageScene.Settings;
        sample.StageSceneStateType = stageScene.StateType;
        sample.StageSceneFlags = stageScene.Flags;
        if (IsARM9MainRAMAddress(stageScene.Base))
        {
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
            if (sample.StageSceneWord5618 < 16)
            {
                const melonDS::u32 dispatchOffset = sample.StageSceneWord5618 * 8;
                sample.StageSceneUpdateDispatchFunc = nds->ARM9Read32(kStageSceneUpdateDispatchTableAddr + dispatchOffset);
                sample.StageSceneUpdateDispatchArg = nds->ARM9Read32(kStageSceneUpdateDispatchTableAddr + dispatchOffset + 4);
                sample.StageSceneRenderDispatchFunc = nds->ARM9Read32(kStageSceneRenderDispatchTableAddr + dispatchOffset);
                sample.StageSceneRenderDispatchArg = nds->ARM9Read32(kStageSceneRenderDispatchTableAddr + dispatchOffset + 4);
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
    if (sample.VsConnectBase != 0)
    {
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
    if (sample.CourseSelectBase != 0)
    {
        sample.CourseSelectFound = 1;
        sample.CourseSelectSettings = nds->ARM9Read32(sample.CourseSelectBase + 0x008);
        sample.CourseSelectWord060 = nds->ARM9Read32(sample.CourseSelectBase + 0x060);
        sample.CourseSelectWord064 = nds->ARM9Read32(sample.CourseSelectBase + 0x064);
        sample.CourseSelectWord068 = nds->ARM9Read32(sample.CourseSelectBase + 0x068);
        sample.CourseSelectWord06C = nds->ARM9Read32(sample.CourseSelectBase + 0x06C);
        sample.CourseSelectWord070 = nds->ARM9Read32(sample.CourseSelectBase + 0x070);
        sample.CourseSelectWord074 = nds->ARM9Read32(sample.CourseSelectBase + 0x074);
        sample.CourseSelectWord078 = nds->ARM9Read32(sample.CourseSelectBase + 0x078);
        sample.CourseSelectWord07C = nds->ARM9Read32(sample.CourseSelectBase + 0x07C);
        sample.CourseSelectWord080 = nds->ARM9Read32(sample.CourseSelectBase + 0x080);
        sample.CourseSelectWord084 = nds->ARM9Read32(sample.CourseSelectBase + 0x084);
        sample.CourseSelectWord088 = nds->ARM9Read32(sample.CourseSelectBase + 0x088);
        sample.CourseSelectWord08C = nds->ARM9Read32(sample.CourseSelectBase + 0x08C);
        sample.CourseSelectWord090 = nds->ARM9Read32(sample.CourseSelectBase + 0x090);
    }
    const ObjectScanSample stageActorManager = FindObjectByID(nds, kStageActorManagerObjectID);
    sample.StageActorManagerFound = stageActorManager.Found;
    sample.StageActorManagerBase = stageActorManager.Base;
    sample.StageActorManagerStateType = stageActorManager.StateType;
    const ObjectScanSample stageController = FindObjectByID(nds, kStageControllerObjectID);
    sample.StageControllerFound = stageController.Found;
    sample.StageControllerBase = stageController.Base;
    sample.StageControllerStateType = stageController.StateType;
    const ObjectScanSample mvlObject267 = FindObjectByID(nds, kMvlObject267ID);
    sample.MvlObject267Found = mvlObject267.Found;
    sample.MvlObject267Base = mvlObject267.Base;
    sample.MvlObject267StateType = mvlObject267.StateType;
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
    if (IsARM9MainRAMAddress(sample.MvlManagerBase))
    {
        sample.MvlManagerVTable = nds->ARM9Read32(sample.MvlManagerBase + 0x00);
        sample.MvlManagerGUID = nds->ARM9Read32(sample.MvlManagerBase + 0x04);
        sample.MvlManagerSettings = nds->ARM9Read32(sample.MvlManagerBase + 0x08);
        sample.MvlManagerObjectID = nds->ARM9Read16(sample.MvlManagerBase + 0x0C);
        sample.MvlManagerStateType = nds->ARM9Read16(sample.MvlManagerBase + 0x0E);
        sample.MvlManagerFlags = nds->ARM9Read32(sample.MvlManagerBase + 0x10);
        sample.MvlManagerUnk54 = nds->ARM9Read32(sample.MvlManagerBase + 0x54);
        sample.MvlManagerResourcesHeap = nds->ARM9Read32(sample.MvlManagerBase + 0x58);
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
    const ObjectScanSample movingHazard = FindObjectByIDAndSettings(nds, kVsMovingHazardObjectID, kVsMovingHazardSettings);
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
    for (int i = 0; i < kObjectTraceSlots; i++)
    {
        sample.ObjectActiveID[i] = objectSummary.ActiveID[i];
        sample.ObjectActiveSettings[i] = objectSummary.ActiveSettings[i];
        sample.ObjectActiveBase[i] = objectSummary.ActiveBase[i];
    }

    sample.Hash = 1469598103934665603ull;
    MixGameStateValue(sample.Hash, sample.StageID);
    MixGameStateValue(sample.Hash, sample.StageGroup);
    MixGameStateValue(sample.Hash, sample.VsMode);
    MixGameStateValue(sample.Hash, sample.LocalPlayerID);
    MixGameStateValue(sample.Hash, sample.GGID);
    MixGameStateValue(sample.Hash, sample.NetState14);
    MixGameStateValue(sample.Hash, sample.NetState1C);
    MixGameStateValue(sample.Hash, sample.NetState20);
    MixGameStateValue(sample.Hash, sample.NetState24);
    MixGameStateValue(sample.Hash, sample.NetState5C);
    MixGameStateValue(sample.Hash, sample.NetPacketTick);
    MixGameStateValue(sample.Hash, sample.NetPacketKeys);
    MixGameStateValue(sample.Hash, sample.NetPacketAction);
    MixGameStateValue(sample.Hash, sample.NetPacketByte5);
    MixGameStateValue(sample.Hash, sample.NetPacketByte6);
    MixGameStateValue(sample.Hash, sample.NetPacketByte7);
    MixGameStateValue(sample.Hash, sample.NetRandomValue);
    MixGameStateValue(sample.Hash, sample.NetRandomCallCount);
    MixGameStateValue(sample.Hash, sample.NetRandomBranchAddress);
    MixGameStateValue(sample.Hash, sample.InputConsole0Held);
    MixGameStateValue(sample.Hash, sample.InputConsole0Pressed);
    MixGameStateValue(sample.Hash, sample.InputConsole1Held);
    MixGameStateValue(sample.Hash, sample.InputConsole1Pressed);
    MixGameStateValue(sample.Hash, sample.InputPlayer0Held);
    MixGameStateValue(sample.Hash, sample.InputPlayer1Held);
    MixGameStateValue(sample.Hash, sample.InputPlayer0Pressed);
    MixGameStateValue(sample.Hash, sample.InputPlayer1Pressed);
    MixGameStateValue(sample.Hash, sample.StageActorFreezeFlag);
    MixGameStateValue(sample.Hash, sample.VsStarFound);
    MixGameStateValue(sample.Hash, sample.VsStarGUID);
    MixGameStateValue(sample.Hash, sample.VsStarSettings);
    MixGameStateValue(sample.Hash, sample.VsStarStateType);
    MixGameStateValue(sample.Hash, sample.VsStarFlags);
    MixGameStateValue(sample.Hash, sample.VsStarPosX);
    MixGameStateValue(sample.Hash, sample.VsStarPosY);
    MixGameStateValue(sample.Hash, sample.VsStarPosZ);
    MixGameStateValue(sample.Hash, sample.VsStarActorFound);
    MixGameStateValue(sample.Hash, sample.VsStarActorGUID);
    MixGameStateValue(sample.Hash, sample.VsStarActorSettings);
    MixGameStateValue(sample.Hash, sample.VsStarActorStateType);
    MixGameStateValue(sample.Hash, sample.VsStarActorFlags);
    MixGameStateValue(sample.Hash, sample.VsStarActorPosX);
    MixGameStateValue(sample.Hash, sample.VsStarActorPosY);
    MixGameStateValue(sample.Hash, sample.VsStarActorPosZ);
    MixGameStateValue(sample.Hash, sample.PlayerActor0Found);
    MixGameStateValue(sample.Hash, sample.PlayerActor0GUID);
    MixGameStateValue(sample.Hash, sample.PlayerActor0Settings);
    MixGameStateValue(sample.Hash, sample.PlayerActor0PosX);
    MixGameStateValue(sample.Hash, sample.PlayerActor0PosY);
    MixGameStateValue(sample.Hash, sample.PlayerActor0PosZ);
    MixGameStateValue(sample.Hash, sample.PlayerActor0PrevX);
    MixGameStateValue(sample.Hash, sample.PlayerActor0PrevY);
    MixGameStateValue(sample.Hash, sample.PlayerActor0PrevZ);
    MixGameStateValue(sample.Hash, sample.PlayerActor0VelX);
    MixGameStateValue(sample.Hash, sample.PlayerActor0VelY);
    MixGameStateValue(sample.Hash, sample.PlayerActor0VelZ);
    MixGameStateValue(sample.Hash, sample.PlayerActor1Found);
    MixGameStateValue(sample.Hash, sample.PlayerActor1GUID);
    MixGameStateValue(sample.Hash, sample.PlayerActor1Settings);
    MixGameStateValue(sample.Hash, sample.PlayerActor1PosX);
    MixGameStateValue(sample.Hash, sample.PlayerActor1PosY);
    MixGameStateValue(sample.Hash, sample.PlayerActor1PosZ);
    MixGameStateValue(sample.Hash, sample.PlayerActor1PrevX);
    MixGameStateValue(sample.Hash, sample.PlayerActor1PrevY);
    MixGameStateValue(sample.Hash, sample.PlayerActor1PrevZ);
    MixGameStateValue(sample.Hash, sample.PlayerActor1VelX);
    MixGameStateValue(sample.Hash, sample.PlayerActor1VelY);
    MixGameStateValue(sample.Hash, sample.PlayerActor1VelZ);
    MixGameStateValue(sample.Hash, sample.PlayerCount);
    MixGameStateValue(sample.Hash, sample.Player0Powerup);
    MixGameStateValue(sample.Hash, sample.Player1Powerup);
    MixGameStateValue(sample.Hash, sample.Player0InventoryPowerup);
    MixGameStateValue(sample.Hash, sample.Player1InventoryPowerup);
    MixGameStateValue(sample.Hash, sample.Player0Dead);
    MixGameStateValue(sample.Hash, sample.Player1Dead);
    MixGameStateValue(sample.Hash, sample.Player0Character);
    MixGameStateValue(sample.Hash, sample.Player1Character);
    MixGameStateValue(sample.Hash, sample.Player0Lives);
    MixGameStateValue(sample.Hash, sample.Player1Lives);
    MixGameStateValue(sample.Hash, sample.Player0BattleStars);
    MixGameStateValue(sample.Hash, sample.Player1BattleStars);
    MixGameStateValue(sample.Hash, sample.Player0Coins);
    MixGameStateValue(sample.Hash, sample.Player1Coins);
    MixGameStateValue(sample.Hash, sample.Player0Score);
    MixGameStateValue(sample.Hash, sample.Player1Score);
    MixGameStateValue(sample.Hash, sample.Player0DisplayedStars);
    MixGameStateValue(sample.Hash, sample.Player1DisplayedStars);
    MixGameStateValue(sample.Hash, sample.Player0Deaths);
    MixGameStateValue(sample.Hash, sample.Player1Deaths);
    MixGameStateValue(sample.Hash, sample.Player0CollectedStars);
    MixGameStateValue(sample.Hash, sample.Player1CollectedStars);
    MixGameStateValue(sample.Hash, sample.VsCoinCount);
    MixGameStateValue(sample.Hash, sample.StageCameraFound);
    MixGameStateValue(sample.Hash, sample.StageCameraWord190);
    MixGameStateValue(sample.Hash, sample.StageCameraWord194);
    MixGameStateValue(sample.Hash, sample.StageCameraWord19C);
    MixGameStateValue(sample.Hash, sample.StageCameraWord1A0);
    MixGameStateValue(sample.Hash, sample.StageSceneFound);
    MixGameStateValue(sample.Hash, sample.StageSceneWord154);
    MixGameStateValue(sample.Hash, sample.StageSceneWord160);
    MixGameStateValue(sample.Hash, sample.VsConnectFound);
    MixGameStateValue(sample.Hash, sample.VsConnectWord078);
    MixGameStateValue(sample.Hash, sample.VsConnectWord07C);
    MixGameStateValue(sample.Hash, sample.VsConnectWord114);
    MixGameStateValue(sample.Hash, sample.VsConnectWord118);
    MixGameStateValue(sample.Hash, sample.VsConnectWord120);
    MixGameStateValue(sample.Hash, sample.VsConnectWord128);
    MixGameStateValue(sample.Hash, sample.VsConnectWord144);
    MixGameStateValue(sample.Hash, sample.VsConnectWord148);
    MixGameStateValue(sample.Hash, sample.VsConnectWord154);
    MixGameStateValue(sample.Hash, sample.CourseSelectFound);
    MixGameStateValue(sample.Hash, sample.CourseSelectSettings);
    MixGameStateValue(sample.Hash, sample.CourseSelectWord060);
    MixGameStateValue(sample.Hash, sample.CourseSelectWord064);
    MixGameStateValue(sample.Hash, sample.CourseSelectWord068);
    MixGameStateValue(sample.Hash, sample.CourseSelectWord06C);
    MixGameStateValue(sample.Hash, sample.CourseSelectWord070);
    MixGameStateValue(sample.Hash, sample.CourseSelectWord074);
    MixGameStateValue(sample.Hash, sample.CourseSelectWord078);
    MixGameStateValue(sample.Hash, sample.CourseSelectWord07C);
    MixGameStateValue(sample.Hash, sample.CourseSelectWord080);
    MixGameStateValue(sample.Hash, sample.CourseSelectWord084);
    MixGameStateValue(sample.Hash, sample.CourseSelectWord088);
    MixGameStateValue(sample.Hash, sample.CourseSelectWord08C);
    MixGameStateValue(sample.Hash, sample.CourseSelectWord090);
    MixGameStateValue(sample.Hash, sample.StageActorManagerFound);
    MixGameStateValue(sample.Hash, sample.StageActorManagerStateType);
    MixGameStateValue(sample.Hash, sample.StageControllerFound);
    MixGameStateValue(sample.Hash, sample.StageControllerStateType);
    MixGameStateValue(sample.Hash, sample.MvlObject267Found);
    MixGameStateValue(sample.Hash, sample.MvlObject267StateType);
    MixGameStateValue(sample.Hash, sample.MovingHazardFound);
    MixGameStateValue(sample.Hash, sample.MovingHazardGUID);
    MixGameStateValue(sample.Hash, sample.MovingHazardSettings);
    MixGameStateValue(sample.Hash, sample.MovingHazardStateType);
    MixGameStateValue(sample.Hash, sample.MovingHazardFlags);
    MixGameStateValue(sample.Hash, sample.MovingHazardPosX);
    MixGameStateValue(sample.Hash, sample.MovingHazardPosY);
    MixGameStateValue(sample.Hash, sample.MovingHazardPosZ);
    MixGameStateValue(sample.Hash, sample.MovingHazardVelX);
    MixGameStateValue(sample.Hash, sample.MovingHazardVelY);
    return sample;
}

void SaveScreenshot(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (G.ScreenshotDir.empty() || G.ScreenshotInterval <= 0) return;
    if ((frame % static_cast<melonDS::u32>(G.ScreenshotInterval)) != 0) return;

    void* topBuffer = nullptr;
    void* bottomBuffer = nullptr;
    if (!nds->GPU.GetFramebuffers(&topBuffer, &bottomBuffer)) return;
    if (!topBuffer || !bottomBuffer) return;

    std::error_code ec;
    std::filesystem::create_directories(G.ScreenshotDir, ec);
    if (ec)
    {
        std::printf("NSMB Test: failed to create screenshot dir: %s (%s)\n",
            G.ScreenshotDir.c_str(),
            ec.message().c_str());
        return;
    }

    QImage image(256, 384, QImage::Format_RGB32);
    std::memcpy(image.scanLine(0), topBuffer, 256 * 192 * 4);
    std::memcpy(image.scanLine(192), bottomBuffer, 256 * 192 * 4);

    int blackPixels = 0;
    int brightPixels = 0;
    for (int y = 0; y < image.height(); y += 4)
    {
        const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); x += 4)
        {
            const QRgb pixel = row[x];
            const int r = qRed(pixel);
            const int g = qGreen(pixel);
            const int b = qBlue(pixel);
            if (r <= 2 && g <= 2 && b <= 2)
                blackPixels++;
            if (r >= 24 || g >= 24 || b >= 24)
                brightPixels++;
        }
    }
    if (blackPixels > 6100 && brightPixels == 0)
    {
        std::printf(
            "NSMB Test: black framebuffer inst=%d frame=%u dispcntA=0x%08X dispcntB=0x%08X dispstat=0x%04X powcnt1=0x%04X bldcntA=0x%04X bldyA=0x%04X bldcntB=0x%04X bldyB=0x%04X netState=0x%02X netFlags=0x%04X\n",
            instanceID,
            frame,
            nds->ARM9Read32(0x04000000),
            nds->ARM9Read32(0x04001000),
            nds->ARM9Read16(0x04000004),
            nds->ARM9Read16(0x04000304),
            nds->ARM9Read16(0x04000050),
            nds->ARM9Read16(0x04000054),
            nds->ARM9Read16(0x04001050),
            nds->ARM9Read16(0x04001054),
            nds->ARM9Read8(0x02088804),
            nds->ARM9Read16(0x0208883C));
        std::fflush(stdout);
    }
    else if (EnvFlag("MELONDS_NSML_SCREENSHOT_REG_TRACE"))
    {
        std::printf(
            "NSMB Test: screenshot regs inst=%d frame=%u dispcntA=0x%08X dispcntB=0x%08X bldcntA=0x%04X bldyA=0x%04X bldcntB=0x%04X bldyB=0x%04X netState=0x%02X netFlags=0x%04X blackSample=%d brightSample=%d\n",
            instanceID,
            frame,
            nds->ARM9Read32(0x04000000),
            nds->ARM9Read32(0x04001000),
            nds->ARM9Read16(0x04000050),
            nds->ARM9Read16(0x04000054),
            nds->ARM9Read16(0x04001050),
            nds->ARM9Read16(0x04001054),
            nds->ARM9Read8(0x02088804),
            nds->ARM9Read16(0x0208883C),
            blackPixels,
            brightPixels);
        std::fflush(stdout);
    }

    char filename[256];
    std::snprintf(filename, sizeof(filename), "inst%d_frame%06u.png", instanceID, frame);
    const std::filesystem::path path = std::filesystem::path(G.ScreenshotDir) / filename;
    if (!image.save(QString::fromStdWString(path.wstring())))
        std::printf("NSMB Test: failed to save screenshot: %ls\n", path.c_str());
}

void SaveRamDump(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (G.RamDumpDir.empty()) return;

    bool shouldDump = false;
    if (G.RamDumpInterval > 0 &&
        (frame % static_cast<melonDS::u32>(G.RamDumpInterval)) == 0)
        shouldDump = true;

    for (const auto& [start, end] : G.RamDumpRanges)
    {
        if (frame >= start && frame <= end)
        {
            shouldDump = true;
            break;
        }
    }

    if (!shouldDump) return;
    if (!nds->MainRAM) return;

    std::error_code ec;
    std::filesystem::create_directories(G.RamDumpDir, ec);
    if (ec)
    {
        std::printf("NSMB Test: failed to create RAM dump dir: %s (%s)\n",
            G.RamDumpDir.c_str(),
            ec.message().c_str());
        return;
    }

    char filename[256];
    std::snprintf(filename, sizeof(filename), "inst%d_frame%06u_mainram.bin", instanceID, frame);
    const std::filesystem::path path = std::filesystem::path(G.RamDumpDir) / filename;

    const melonDS::u32 len = std::min<melonDS::u32>(nds->MainRAMMask + 1, 0x400000);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        std::printf("NSMB Test: failed to open RAM dump for write: %ls\n", path.c_str());
        return;
    }

    file.write(reinterpret_cast<const char*>(nds->MainRAM), len);
    if (!file)
        std::printf("NSMB Test: failed to write RAM dump: %ls\n", path.c_str());
}

void ApplyMemPatch(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!nds || !nds->MainRAM || G.MemPatchFile.empty() || !G.MemPatchFrameSet) return;
    if (frame != G.MemPatchFrame || G.MemPatchApplied[instanceID]) return;
    if (G.MemPatchInstance >= 0 && G.MemPatchInstance != instanceID) return;
    if (G.MemPatchRanges.empty()) return;

    std::string patchFile = G.MemPatchFile;
    const std::string instToken = "{inst}";
    if (const auto pos = patchFile.find(instToken); pos != std::string::npos)
        patchFile.replace(pos, instToken.size(), std::to_string(instanceID));

    std::ifstream file(patchFile, std::ios::binary);
    if (!file)
    {
        std::printf("NSMB Test: failed to open memory patch source: %s\n", patchFile.c_str());
        return;
    }

    std::vector<char> source(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    if (source.empty())
    {
        std::printf("NSMB Test: memory patch source is empty: %s\n", patchFile.c_str());
        return;
    }

    const melonDS::u32 ramLen = std::min<melonDS::u32>(nds->MainRAMMask + 1, 0x400000);
    for (const auto& [start, end] : G.MemPatchRanges)
    {
        if (end < start || start >= ramLen)
            continue;

        const melonDS::u32 clampedEnd = std::min(end, ramLen - 1);
        const melonDS::u32 len = clampedEnd - start + 1;
        if (static_cast<size_t>(clampedEnd) >= source.size())
        {
            std::printf("NSMB Test: memory patch range outside source: 0x%06X-0x%06X sourceBytes=%zu\n",
                start,
                clampedEnd,
                source.size());
            continue;
        }

        std::memcpy(&nds->MainRAM[start], &source[start], len);
        nds->JIT.CheckAndInvalidate<0, melonDS::ARMJIT_Memory::memregion_MainRAM>(0x02000000 + start);
        nds->JIT.CheckAndInvalidate<1, melonDS::ARMJIT_Memory::memregion_MainRAM>(0x02000000 + start);
        std::printf("NSMB Test: patched memory inst=%d frame=%u range=0x%06X-0x%06X source=%s\n",
            instanceID,
            frame,
            start,
            clampedEnd,
            patchFile.c_str());
    }

    G.MemPatchApplied[instanceID] = true;
}

void ApplyNetRandomPatch(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    constexpr melonDS::u32 kNetRandomValueOffset = kNetRandomValueAddr - kMainRAMBase;
    constexpr melonDS::u32 kNetRandomCallCountOffset = kNetRandomCallCountAddr - kMainRAMBase;

    if (!nds || !nds->MainRAM || !G.NetRandomPatchEnabled) return;
    if (instanceID < 0 || instanceID >= 16) return;
    if (G.NetRandomPatchApplied[instanceID]) return;
    if (kNetRandomValueOffset + sizeof(melonDS::u32) > nds->MainRAMMask + 1) return;

    bool shouldPatch = frame == G.NetRandomPatchFrame;
    melonDS::u8 randomCallCountBeforePatch = 0;
    if (G.NetRandomPatchAuto)
    {
        const melonDS::u32 ggid = nds->ARM9Read32(kNetGGIDAddr);
        randomCallCountBeforePatch = nds->ARM9Read8(kNetRandomCallCountAddr);
        shouldPatch = IsMarioVsLuigiGameplay(nds) || IsMarioVsLuigiGGID(ggid);
    }
    if (!shouldPatch) return;

    std::memcpy(&nds->MainRAM[kNetRandomValueOffset], &G.NetRandomPatchValue, sizeof(G.NetRandomPatchValue));
    nds->MainRAM[kNetRandomCallCountOffset] = 0;
    G.NetRandomPatchApplied[instanceID] = true;

    std::printf("NSMB Test: patched Net::random.value inst=%d frame=%u value=0x%08X auto=%d oldCount=0x%02X resetCount=1\n",
        instanceID,
        frame,
        G.NetRandomPatchValue,
        G.NetRandomPatchAuto ? 1 : 0,
        randomCallCountBeforePatch);
}

void TraceGameState(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (G.GameStateTracePath.empty()) return;
    if (!nds || !nds->MainRAM) return;
    if (instanceID < 0 || instanceID >= 16) return;
    if (frame < G.GameStateTraceStartFrame) return;
    if (G.GameStateTraceEndFrame != 0 && frame > G.GameStateTraceEndFrame) return;
    if ((frame % static_cast<melonDS::u32>(G.GameStateTraceInterval)) != 0) return;
    if ((kNetRandomValueAddr - kMainRAMBase) + sizeof(melonDS::u32) > nds->MainRAMMask + 1) return;

    const GameStateSample sample = ReadGameStateSample(nds);

    std::lock_guard<std::mutex> lock(G.Mutex);
    if (!G.GameStateTrace) return;
    if (G.LastLoggedGameStateFrame[instanceID] == frame) return;
    G.LastLoggedGameStateFrame[instanceID] = frame;

    G.GameStateTrace << std::dec << instanceID << ',' << frame
                     << ",0x" << std::hex << sample.StageID
                     << ",0x" << sample.StageGroup
                     << ",0x" << sample.VsMode
                     << ",0x" << sample.LocalPlayerID
                     << ",0x" << sample.Arm9PC
                     << ",0x" << sample.Arm9LR
                     << ",0x" << sample.Arm9SP
                     << ",0x" << sample.Arm9CPSR
                     << ",0x" << sample.AppFrameLength
                     << ",0x" << sample.AppUpdateTask
                     << ",0x" << sample.AppSleepPhase
                     << ",0x" << sample.AppSleepControl
                     << ",0x" << sample.AppSleeping
                     << ",0x" << sample.AppSleepPhaseTimer
                     << ",0x" << sample.AppSleepWakeUpTimer
                     << ",0x" << sample.AppBootParam
                     << ",0x" << sample.AppBootTarget
                     << ",0x" << sample.AppBootScene
                     << ",0x" << sample.GGID
                     << ",0x" << sample.NetCurrentLanguage
                     << ",0x" << sample.NetLocalAid
                     << ",0x" << sample.NetState14
                     << ",0x" << sample.NetState1C
                     << ",0x" << sample.NetState20
                     << ",0x" << sample.NetState24
                     << ",0x" << sample.NetExpectedConsoleCount
                     << ",0x" << sample.NetMultiBootSession
                     << ",0x" << sample.NetSessionState
                     << ",0x" << sample.NetModuleState
                     << ",0x" << sample.NetMaxSessionChildren
                     << ",0x" << sample.NetMaxConsoleCount
                     << ",0x" << sample.NetState5C
                     << ",0x" << sample.NetPacketTick
                     << ",0x" << sample.NetPacketKeys
                     << ",0x" << sample.NetPacketAction
                     << ",0x" << sample.NetPacketByte5
                     << ",0x" << sample.NetPacketByte6
                     << ",0x" << sample.NetPacketByte7
                     << ",0x" << sample.NetRandomValue
                     << ",0x" << sample.NetRandomCallCount
                     << ",0x" << sample.NetRandomBranchAddress
                     << ",0x" << sample.InputConsole0Held
                     << ",0x" << sample.InputConsole0Pressed
                     << ",0x" << sample.InputConsole1Held
                     << ",0x" << sample.InputConsole1Pressed
                     << ",0x" << sample.InputPlayer0Held
                     << ",0x" << sample.InputPlayer1Held
                     << ",0x" << sample.InputPlayer0Pressed
                     << ",0x" << sample.InputPlayer1Pressed
                     << ",0x" << sample.StageActorFreezeFlag
                     << ",0x" << sample.SceneIsSceneActive
                     << ",0x" << sample.ScenePreviousSceneID
                     << ",0x" << sample.SceneNextSceneID
                     << ",0x" << sample.SceneCurrentSceneID
                     << ",0x" << sample.SceneNextSceneSettings
                     << ",0x" << sample.VsStarFound
                     << ",0x" << sample.VsStarGUID
                     << ",0x" << sample.VsStarBase
                     << ",0x" << sample.VsStarSettings
                     << ",0x" << sample.VsStarStateType
                     << ",0x" << sample.VsStarFlags
                     << ",0x" << sample.VsStarPosX
                     << ",0x" << sample.VsStarPosY
                     << ",0x" << sample.VsStarPosZ
                     << ",0x" << sample.VsStarActorFound
                     << ",0x" << sample.VsStarActorGUID
                     << ",0x" << sample.VsStarActorBase
                     << ",0x" << sample.VsStarActorSettings
                     << ",0x" << sample.VsStarActorStateType
                     << ",0x" << sample.VsStarActorFlags
                     << ",0x" << sample.VsStarActorPosX
                     << ",0x" << sample.VsStarActorPosY
                     << ",0x" << sample.VsStarActorPosZ
                     << ",0x" << sample.PlayerActor0Found
                     << ",0x" << sample.PlayerActor0GUID
                     << ",0x" << sample.PlayerActor0Base
                     << ",0x" << sample.PlayerActor0Settings
                     << ",0x" << sample.PlayerActor0StateType
                     << ",0x" << sample.PlayerActor0Flags
                     << ",0x" << sample.PlayerActor0PosX
                     << ",0x" << sample.PlayerActor0PosY
                     << ",0x" << sample.PlayerActor0PosZ
                     << ",0x" << sample.PlayerActor0PrevX
                     << ",0x" << sample.PlayerActor0PrevY
                     << ",0x" << sample.PlayerActor0PrevZ
                     << ",0x" << sample.PlayerActor0VelX
                     << ",0x" << sample.PlayerActor0VelY
                     << ",0x" << sample.PlayerActor0VelZ
                     << ",0x" << sample.PlayerActor0PlayerID
                     << ",0x" << sample.PlayerActor0TransitionStep
                     << ",0x" << sample.PlayerActor0SignalLock
                     << ",0x" << sample.PlayerActor0Flag192
                     << ",0x" << sample.PlayerActor0Flags728
                     << ",0x" << sample.PlayerActor0Flags72C
                     << ",0x" << sample.PlayerActor0Flags730
                     << ",0x" << sample.PlayerActor0TransitFunc
                     << ",0x" << sample.PlayerActor0TransitArg
                     << ",0x" << sample.PlayerActor1Found
                     << ",0x" << sample.PlayerActor1GUID
                     << ",0x" << sample.PlayerActor1Base
                     << ",0x" << sample.PlayerActor1Settings
                     << ",0x" << sample.PlayerActor1StateType
                     << ",0x" << sample.PlayerActor1Flags
                     << ",0x" << sample.PlayerActor1PosX
                     << ",0x" << sample.PlayerActor1PosY
                     << ",0x" << sample.PlayerActor1PosZ
                     << ",0x" << sample.PlayerActor1PrevX
                     << ",0x" << sample.PlayerActor1PrevY
                     << ",0x" << sample.PlayerActor1PrevZ
                     << ",0x" << sample.PlayerActor1VelX
                     << ",0x" << sample.PlayerActor1VelY
                     << ",0x" << sample.PlayerActor1VelZ
                     << ",0x" << sample.PlayerActor1PlayerID
                     << ",0x" << sample.PlayerActor1TransitionStep
                     << ",0x" << sample.PlayerActor1SignalLock
                     << ",0x" << sample.PlayerActor1Flag192
                     << ",0x" << sample.PlayerActor1Flags728
                     << ",0x" << sample.PlayerActor1Flags72C
                     << ",0x" << sample.PlayerActor1Flags730
                     << ",0x" << sample.PlayerActor1TransitFunc
                     << ",0x" << sample.PlayerActor1TransitArg
                     << ",0x" << sample.PlayerTransitionStatus0
                     << ",0x" << sample.PlayerTransitionStatus1
                     << ",0x" << sample.VsConnectFound
                     << ",0x" << sample.VsConnectBase
                     << ",0x" << sample.VsConnectWord078
                     << ",0x" << sample.VsConnectWord07C
                     << ",0x" << sample.VsConnectByte0E2
                     << ",0x" << sample.VsConnectByte106
                     << ",0x" << sample.VsConnectWord114
                     << ",0x" << sample.VsConnectWord118
                     << ",0x" << sample.VsConnectWord120
                     << ",0x" << sample.VsConnectWord128
                     << ",0x" << sample.VsConnectWord138
                     << ",0x" << sample.VsConnectWord13C
                     << ",0x" << sample.VsConnectWord140
                     << ",0x" << sample.VsConnectWord144
                     << ",0x" << sample.VsConnectWord148
                     << ",0x" << sample.VsConnectByte153
                     << ",0x" << sample.VsConnectByte154
                     << ",0x" << sample.VsConnectByte155
                     << ",0x" << sample.VsConnectByte156
                     << ",0x" << sample.VsConnectByte157
                     << ",0x" << sample.VsConnectByte158
                     << ",0x" << sample.VsConnectWord154
                     << ",0x" << sample.CourseSelectFound
                     << ",0x" << sample.CourseSelectBase
                     << ",0x" << sample.CourseSelectSettings
                     << ",0x" << sample.CourseSelectWord060
                     << ",0x" << sample.CourseSelectWord064
                     << ",0x" << sample.CourseSelectWord068
                     << ",0x" << sample.CourseSelectWord06C
                     << ",0x" << sample.CourseSelectWord070
                     << ",0x" << sample.CourseSelectWord074
                     << ",0x" << sample.CourseSelectWord078
                     << ",0x" << sample.CourseSelectWord07C
                     << ",0x" << sample.CourseSelectWord080
                     << ",0x" << sample.CourseSelectWord084
                     << ",0x" << sample.CourseSelectWord088
                     << ",0x" << sample.CourseSelectWord08C
                     << ",0x" << sample.CourseSelectWord090
                     << ",0x" << sample.StageCameraFound
                     << ",0x" << sample.StageCameraWord190
                     << ",0x" << sample.StageCameraWord194
                     << ",0x" << sample.StageCameraWord19C
                     << ",0x" << sample.StageCameraWord1A0
                     << ",0x" << sample.StageActorManagerFound
                     << ",0x" << sample.StageActorManagerBase
                     << ",0x" << sample.StageActorManagerStateType
                     << ",0x" << sample.StageControllerFound
                     << ",0x" << sample.StageControllerBase
                     << ",0x" << sample.StageControllerStateType
                     << ",0x" << sample.MvlObject267Found
                     << ",0x" << sample.MvlObject267Base
                     << ",0x" << sample.MvlObject267StateType
                     << ",0x" << sample.MvlGlobal965C
                     << ",0x" << sample.MvlGlobal9670
                     << ",0x" << sample.MvlGlobal9674
                     << ",0x" << sample.MvlGlobal9694_0
                     << ",0x" << sample.MvlGlobal9694_1
                     << ",0x" << sample.MvlStageLayoutGateCAC6C
                     << ",0x" << sample.MvlStageLayoutGateCAC74
                     << ",0x" << sample.MvlStageLayoutGateCAC7C
                     << ",0x" << sample.MvlStageLayoutGateCACDC
                     << ",0x" << sample.MvlStageLayoutGateCAE80
                     << ",0x" << sample.MvlStageLayoutGateCAE74
                     << ",0x" << sample.MvlStageLayoutGateCAEB8
                     << ",0x" << sample.MvlStageLayoutGateCAF20
                     << ",0x" << sample.MvlStageLayoutGateCAF40
                     << ",0x" << sample.MvlStageLayoutGateCA8C0
                     << ",0x" << sample.MvlStageLayoutGateCA8D0
                     << ",0x" << sample.MvlStageLayoutGateCAD30
                     << ",0x" << sample.MvlManagerBase
                     << ",0x" << sample.MvlManagerVTable
                     << ",0x" << sample.MvlManagerGUID
                     << ",0x" << sample.MvlManagerSettings
                     << ",0x" << sample.MvlManagerObjectID
                     << ",0x" << sample.MvlManagerStateType
                     << ",0x" << sample.MvlManagerFlags
                     << ",0x" << sample.MvlManagerUnk54
                     << ",0x" << sample.MvlManagerResourcesHeap
                     << ",0x" << sample.MvlManagerWordA8CC
                     << ",0x" << sample.MvlManagerWordA8D0
                     << ",0x" << sample.MvlManagerWordA8D4
                     << ",0x" << sample.MvlManagerWordA8D8
                     << ",0x" << sample.MvlManagerWordA8DC
                     << ",0x" << sample.MvlManagerWordA8E0
                     << ",0x" << sample.MvlManagerWordA8E4
                     << ",0x" << sample.MvlManagerHalfA8E8
                     << ",0x" << sample.MvlManagerHalfA8EA
                     << ",0x" << sample.MvlManagerByteA8EC
                     << ",0x" << sample.MvlManagerHalf494
                     << ",0x" << sample.MvlManagerHalf4A0
                     << ",0x" << sample.StageSceneFound
                     << ",0x" << sample.StageSceneBase
                     << ",0x" << sample.StageSceneSettings
                     << ",0x" << sample.StageSceneStateType
                     << ",0x" << sample.StageSceneFlags
                     << ",0x" << sample.StageSceneWord154
                     << ",0x" << sample.StageSceneWord160
                     << ",0x" << sample.StageSceneWord5618
                     << ",0x" << sample.StageSceneWord561C
                     << ",0x" << sample.StageSceneWord563C
                     << ",0x" << sample.StageSceneByte5643
                     << ",0x" << sample.StageSceneByte5644
                     << ",0x" << sample.StageSceneByte5645
                     << ",0x" << sample.StageSceneByte5646
                     << ",0x" << sample.StageSceneByte5648
                     << ",0x" << sample.StageSceneByte5649
                     << ",0x" << sample.StageSceneUpdateDispatchFunc
                     << ",0x" << sample.StageSceneUpdateDispatchArg
                     << ",0x" << sample.StageSceneRenderDispatchFunc
                     << ",0x" << sample.StageSceneRenderDispatchArg
                     << ",0x" << sample.StageSceneGlobal9280
                     << ",0x" << sample.StageSceneGlobal9284
                     << ",0x" << sample.StageSceneGlobal928C
                     << ",0x" << sample.StageSceneGlobal92B4
                     << ",0x" << sample.StageSceneGlobal92C0
                     << ",0x" << sample.StageSceneGlobal92C8
                     << ",0x" << sample.StageSceneGlobal92CC
                     << ",0x" << sample.StageSceneGlobal92D0
                     << ",0x" << sample.StageLiquidPlayerSlot
                     << ",0x" << sample.StageLiquidHeight0
                     << ",0x" << sample.StageLiquidHeight1
                     << ",0x" << sample.MovingHazardFound
                     << ",0x" << sample.MovingHazardGUID
                     << ",0x" << sample.MovingHazardSettings
                     << ",0x" << sample.MovingHazardStateType
                     << ",0x" << sample.MovingHazardFlags
                     << ",0x" << sample.MovingHazardPosX
                     << ",0x" << sample.MovingHazardPosY
                     << ",0x" << sample.MovingHazardPosZ
                     << ",0x" << sample.MovingHazardVelX
                     << ",0x" << sample.MovingHazardVelY
                     << ",0x" << sample.MovingHazardLastStepX
                     << ",0x" << sample.MovingHazardLastStepY
                     << ",0x" << sample.MovingHazardLastStepZ
                     << ",0x" << sample.MovingHazardVelH
                     << ",0x" << sample.MovingHazardTargetVelH
                     << ",0x" << sample.MovingHazardAccelV
                     << ",0x" << sample.MovingHazardTargetVelV
                     << ",0x" << sample.MovingHazardAccelH
                     << ",0x" << sample.MovingHazardTargetVelX
                     << ",0x" << sample.MovingHazardTargetVelY
                     << ",0x" << sample.MovingHazardTargetVelZ
                     << ",0x" << sample.ObjectScanTotal
                     << ",0x" << sample.ObjectNotCreatedCount
                     << ",0x" << sample.ObjectActiveCount
                     << ",0x" << sample.ObjectDeadCount
                     << ",0x" << sample.ObjectSkipUpdateCount
                     << ",0x" << sample.ObjectSkipRenderCount
                     << ",0x" << sample.ObjectFirstNotCreatedID
                     << ",0x" << sample.ObjectFirstNotCreatedBase
                     << ",0x" << sample.ObjectFirstNotCreatedFlags
                     << ",0x" << sample.ObjectSecondNotCreatedID
                     << ",0x" << sample.ObjectSecondNotCreatedBase
                     << ",0x" << sample.ObjectSecondNotCreatedFlags;
    for (int i = 0; i < kObjectTraceSlots; i++)
    {
        G.GameStateTrace << ",0x" << sample.ObjectActiveID[i]
                         << ",0x" << sample.ObjectActiveSettings[i]
                         << ",0x" << sample.ObjectActiveBase[i];
    }

    if (G.GameStateTraceExtended)
    {
        const melonDS::u64 playerGlobalHash = HashMainRAMRange(nds, kGamePlayerGlobalBlockAddr, 0xC0);
        const melonDS::u64 wifiCandidateHash = HashMainRAMRange(nds, kGameCandidateWifiBlockAddr, 0x2200);
        const melonDS::u64 renderCandidateHash = HashMainRAMRange(nds, kGameCandidateRenderBlockAddr, 0x240);
        const melonDS::u64 netStateHash = HashMainRAMRange(nds, kNetStateBaseAddr, 0x180);

        G.GameStateTrace << ",0x" << sample.PlayerCount
                         << ",0x" << sample.Player0Powerup
                         << ",0x" << sample.Player1Powerup
                         << ",0x" << sample.Player0InventoryPowerup
                         << ",0x" << sample.Player1InventoryPowerup
                         << ",0x" << sample.Player0Dead
                         << ",0x" << sample.Player1Dead
                         << ",0x" << sample.Player0Character
                         << ",0x" << sample.Player1Character
                         << ",0x" << sample.Player0Lives
                         << ",0x" << sample.Player1Lives
                         << ",0x" << sample.Player0BattleStars
                         << ",0x" << sample.Player1BattleStars
                         << ",0x" << sample.Player0Coins
                         << ",0x" << sample.Player1Coins
                         << ",0x" << sample.Player0Score
                         << ",0x" << sample.Player1Score
                         << ",0x" << sample.Player0DisplayedStars
                         << ",0x" << sample.Player1DisplayedStars
                         << ",0x" << sample.Player0Deaths
                         << ",0x" << sample.Player1Deaths
                         << ",0x" << sample.Player0CollectedStars
                         << ",0x" << sample.Player1CollectedStars
                         << ",0x" << sample.VsCoinCount
                         << ",0x" << sample.EntranceSpawnID0
                         << ",0x" << sample.EntranceSpawnID1
                         << ",0x" << sample.EntranceTransitionFlags0
                         << ",0x" << sample.EntranceTransitionFlags1
                         << ",0x" << sample.EntranceSpawnPtr0
                         << ",0x" << sample.EntranceSpawnPtr1
                         << ",0x" << sample.StageCameraBase
                         << ",0x" << sample.StageCameraTargetX
                         << ",0x" << sample.StageCameraTargetY
                         << ",0x" << sample.StageCameraTargetZ
                         << ",0x" << sample.StageCameraPositionX
                         << ",0x" << sample.StageCameraPositionY
                         << ",0x" << sample.StageCameraPositionZ
                         << ",0x" << sample.StageCameraUpX
                         << ",0x" << sample.StageCameraUpY
                         << ",0x" << sample.StageCameraUpZ
                         << ",0x" << sample.StageCameraUnk114
                         << ",0x" << sample.StageCameraUnk118
                         << ",0x" << sample.StageCameraUnk11C
                         << ",0x" << sample.StageCameraUnk128
                         << ",0x" << sample.StageCameraUnk12C
                         << ",0x" << sample.StageCameraRoll130
                         << ",0x" << sample.StageCameraGlobalX0
                         << ",0x" << sample.StageCameraGlobalX1
                         << ",0x" << sample.StageCameraGlobalY0
                         << ",0x" << sample.StageCameraGlobalY1
                         << ",0x" << sample.StageCameraGlobalWidth0
                         << ",0x" << sample.StageCameraGlobalWidth1
                         << ",0x" << sample.StageCameraGlobalHeight0
                         << ",0x" << sample.StageCameraGlobalHeight1
                         << ",0x" << sample.StageDisplayCameraX
                         << ",0x" << playerGlobalHash
                         << ",0x" << wifiCandidateHash
                         << ",0x" << renderCandidateHash
                         << ",0x" << netStateHash
                         << ",0x" << sample.PlayerActor0ActionFlag
                         << ",0x" << sample.PlayerActor0SubActionFlag
                         << ",0x" << sample.PlayerActor0PhysicsFlag
                         << ",0x" << sample.PlayerActor0DamageCooldown
                         << ",0x" << sample.PlayerActor1ActionFlag
                         << ",0x" << sample.PlayerActor1SubActionFlag
                         << ",0x" << sample.PlayerActor1PhysicsFlag
                         << ",0x" << sample.PlayerActor1DamageCooldown
                         << ",0x" << sample.PlayerActor0LinkedActor
                         << ",0x" << sample.PlayerActor0TransitionFlag
                         << ",0x" << sample.PlayerActor0CollisionFlag
                         << ",0x" << sample.PlayerActor0EnvironmentFlag
                         << ",0x" << sample.PlayerActor0UpdateLocked
                         << ",0x" << sample.PlayerActor0CharacterIDBase
                         << ",0x" << sample.PlayerActor0TransitioningFlag
                         << ",0x" << sample.PlayerActor0DefeatedFlag
                         << ",0x" << sample.PlayerActor0PlayerBaseID
                         << ",0x" << sample.PlayerActor0VisibleFlag
                         << ",0x" << sample.PlayerActor1LinkedActor
                         << ",0x" << sample.PlayerActor1TransitionFlag
                         << ",0x" << sample.PlayerActor1CollisionFlag
                         << ",0x" << sample.PlayerActor1EnvironmentFlag
                         << ",0x" << sample.PlayerActor1UpdateLocked
                         << ",0x" << sample.PlayerActor1CharacterIDBase
                         << ",0x" << sample.PlayerActor1TransitioningFlag
                         << ",0x" << sample.PlayerActor1DefeatedFlag
                         << ",0x" << sample.PlayerActor1PlayerBaseID
                         << ",0x" << sample.PlayerActor1VisibleFlag;
    }

    G.GameStateTrace << std::dec << '\n';
    G.GameStateTrace.flush();
}

void SyncGameState(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.Enabled || !G.GameStateSyncEnabled || !nds) return;
    if (instanceID < 0 || instanceID >= 16) return;
    if (frame < G.NetplayStartFrame) return;
    if ((frame % static_cast<melonDS::u32>(G.GameStateSyncInterval)) != 0) return;

    const GameStateSample sample = ReadGameStateSample(nds);
    GameStateSyncHashes hashes;
    hashes.Basic = sample.Hash;
    if (G.GameStateSyncExtended)
    {
        hashes.PlayerGlobal = HashMainRAMRange(nds, kGamePlayerGlobalBlockAddr, 0xC0);
        hashes.WifiCandidate = HashMainRAMRange(nds, kGameCandidateWifiBlockAddr, 0x2200);
        hashes.RenderCandidate = HashMainRAMRange(nds, kGameCandidateRenderBlockAddr, 0x240);
    }

    std::lock_guard<std::mutex> lock(G.Mutex);
    if (G.LastSentGameStateFrame[instanceID] == frame) return;
    G.LastSentGameStateFrame[instanceID] = frame;

    G.LocalGameStateHashes[GameStateKey(instanceID, frame)] = hashes;
    CompareGameStateLocked(instanceID, frame);

    if (!G.Peer) return;

    WireGameState packet {};
    packet.Magic = kMagic;
    packet.Version = kVersion;
    packet.Kind = kWireKindState;
    packet.Frame = frame;
    packet.Instance = static_cast<melonDS::u32>(instanceID);
    packet.StageID = sample.StageID;
    packet.StageGroup = sample.StageGroup;
    packet.VsMode = sample.VsMode;
    packet.LocalPlayerID = sample.LocalPlayerID;
    packet.GGID = sample.GGID;
    packet.NetRandomValue = sample.NetRandomValue;
    packet.NetRandomCallCount = sample.NetRandomCallCount;
    packet.NetRandomBranchAddress = sample.NetRandomBranchAddress;
    packet.VsStarFound = sample.VsStarFound;
    packet.VsStarGUID = sample.VsStarGUID;
    packet.VsStarBase = sample.VsStarBase;
    packet.VsStarSettings = sample.VsStarSettings;
    packet.VsStarStateType = sample.VsStarStateType;
    packet.VsStarFlags = sample.VsStarFlags;
    packet.VsStarPosX = sample.VsStarPosX;
    packet.VsStarPosY = sample.VsStarPosY;
    packet.VsStarPosZ = sample.VsStarPosZ;
    packet.VsStarActorFound = sample.VsStarActorFound;
    packet.VsStarActorGUID = sample.VsStarActorGUID;
    packet.VsStarActorBase = sample.VsStarActorBase;
    packet.VsStarActorSettings = sample.VsStarActorSettings;
    packet.VsStarActorStateType = sample.VsStarActorStateType;
    packet.VsStarActorFlags = sample.VsStarActorFlags;
    packet.VsStarActorPosX = sample.VsStarActorPosX;
    packet.VsStarActorPosY = sample.VsStarActorPosY;
    packet.VsStarActorPosZ = sample.VsStarActorPosZ;
    packet.PlayerActor0Found = sample.PlayerActor0Found;
    packet.PlayerActor0GUID = sample.PlayerActor0GUID;
    packet.PlayerActor0Settings = sample.PlayerActor0Settings;
    packet.PlayerActor0PosX = sample.PlayerActor0PosX;
    packet.PlayerActor0PosY = sample.PlayerActor0PosY;
    packet.PlayerActor0PosZ = sample.PlayerActor0PosZ;
    packet.PlayerActor0PrevX = sample.PlayerActor0PrevX;
    packet.PlayerActor0PrevY = sample.PlayerActor0PrevY;
    packet.PlayerActor0PrevZ = sample.PlayerActor0PrevZ;
    packet.PlayerActor0VelX = sample.PlayerActor0VelX;
    packet.PlayerActor0VelY = sample.PlayerActor0VelY;
    packet.PlayerActor0VelZ = sample.PlayerActor0VelZ;
    packet.PlayerActor1Found = sample.PlayerActor1Found;
    packet.PlayerActor1GUID = sample.PlayerActor1GUID;
    packet.PlayerActor1Settings = sample.PlayerActor1Settings;
    packet.PlayerActor1PosX = sample.PlayerActor1PosX;
    packet.PlayerActor1PosY = sample.PlayerActor1PosY;
    packet.PlayerActor1PosZ = sample.PlayerActor1PosZ;
    packet.PlayerActor1PrevX = sample.PlayerActor1PrevX;
    packet.PlayerActor1PrevY = sample.PlayerActor1PrevY;
    packet.PlayerActor1PrevZ = sample.PlayerActor1PrevZ;
    packet.PlayerActor1VelX = sample.PlayerActor1VelX;
    packet.PlayerActor1VelY = sample.PlayerActor1VelY;
    packet.PlayerActor1VelZ = sample.PlayerActor1VelZ;
    packet.PlayerCount = sample.PlayerCount;
    packet.Player0BattleStars = sample.Player0BattleStars;
    packet.Player1BattleStars = sample.Player1BattleStars;
    packet.Player0Coins = sample.Player0Coins;
    packet.Player1Coins = sample.Player1Coins;
    packet.Player0Score = sample.Player0Score;
    packet.Player1Score = sample.Player1Score;
    packet.Player0DisplayedStars = sample.Player0DisplayedStars;
    packet.Player1DisplayedStars = sample.Player1DisplayedStars;
    packet.Player0Deaths = sample.Player0Deaths;
    packet.Player1Deaths = sample.Player1Deaths;
    packet.Player0CollectedStars = sample.Player0CollectedStars;
    packet.Player1CollectedStars = sample.Player1CollectedStars;
    packet.VsCoinCount = sample.VsCoinCount;
    packet.StageCameraFound = sample.StageCameraFound;
    packet.StageCameraWord190 = sample.StageCameraWord190;
    packet.StageCameraWord194 = sample.StageCameraWord194;
    packet.StageCameraWord19C = sample.StageCameraWord19C;
    packet.StageCameraWord1A0 = sample.StageCameraWord1A0;
    packet.StageSceneFound = sample.StageSceneFound;
    packet.StageSceneWord154 = sample.StageSceneWord154;
    packet.StageSceneWord160 = sample.StageSceneWord160;
    packet.MovingHazardFound = sample.MovingHazardFound;
    packet.MovingHazardGUID = sample.MovingHazardGUID;
    packet.MovingHazardSettings = sample.MovingHazardSettings;
    packet.MovingHazardStateType = sample.MovingHazardStateType;
    packet.MovingHazardFlags = sample.MovingHazardFlags;
    packet.MovingHazardPosX = sample.MovingHazardPosX;
    packet.MovingHazardPosY = sample.MovingHazardPosY;
    packet.MovingHazardPosZ = sample.MovingHazardPosZ;
    packet.MovingHazardVelX = sample.MovingHazardVelX;
    packet.MovingHazardVelY = sample.MovingHazardVelY;
    packet.BasicHashLo = static_cast<melonDS::u32>(hashes.Basic & 0xFFFFFFFFu);
    packet.BasicHashHi = static_cast<melonDS::u32>(hashes.Basic >> 32);
    packet.PlayerGlobalHashLo = static_cast<melonDS::u32>(hashes.PlayerGlobal & 0xFFFFFFFFu);
    packet.PlayerGlobalHashHi = static_cast<melonDS::u32>(hashes.PlayerGlobal >> 32);
    packet.WifiCandidateHashLo = static_cast<melonDS::u32>(hashes.WifiCandidate & 0xFFFFFFFFu);
    packet.WifiCandidateHashHi = static_cast<melonDS::u32>(hashes.WifiCandidate >> 32);
    packet.RenderCandidateHashLo = static_cast<melonDS::u32>(hashes.RenderCandidate & 0xFFFFFFFFu);
    packet.RenderCandidateHashHi = static_cast<melonDS::u32>(hashes.RenderCandidate >> 32);

    ENetPacket* enetPacket = enet_packet_create(&packet, sizeof(packet), ENET_PACKET_FLAG_RELIABLE);
    if (enetPacket)
        enet_peer_send(G.Peer, 0, enetPacket);
}

std::filesystem::path StatePath(const std::string& dir, int instanceID)
{
    char filename[64];
    std::snprintf(filename, sizeof(filename), "inst%d.mln", instanceID);
    return std::filesystem::path(dir) / filename;
}

std::filesystem::path LocalMPStatePath(const std::string& dir)
{
    return std::filesystem::path(dir) / "localmp.bin";
}

bool SaveState(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (G.StateSaveDir.empty() || G.StateSaveFrame == 0) return false;
    if (frame != G.StateSaveFrame || G.StateSaved[instanceID]) return false;

    std::error_code ec;
    std::filesystem::create_directories(G.StateSaveDir, ec);
    if (ec)
    {
        std::printf("NSMB Test: failed to create state save dir: %s (%s)\n",
            G.StateSaveDir.c_str(),
            ec.message().c_str());
        return false;
    }

    melonDS::Savestate state;
    if (state.Error || !nds->DoSavestate(&state) || state.Error)
    {
        std::printf("NSMB Test: failed to create savestate inst=%d frame=%u\n", instanceID, frame);
        return false;
    }

    const std::filesystem::path path = StatePath(G.StateSaveDir, instanceID);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        std::printf("NSMB Test: failed to open savestate for write: %ls\n", path.c_str());
        return false;
    }

    file.write(reinterpret_cast<const char*>(state.Buffer()), state.Length());
    if (!file)
    {
        std::printf("NSMB Test: failed to write savestate: %ls\n", path.c_str());
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        G.StateSaved[instanceID] = true;
    }
    std::printf("NSMB Test: saved state inst=%d frame=%u path=%ls bytes=%u\n",
        instanceID,
        frame,
        path.c_str(),
        state.Length());
    return true;
}

bool AllStatesSavedLocked()
{
    for (int i = 0; i < G.TestInstanceCount; i++)
    {
        if (!G.StateSaved[i])
            return false;
    }
    return true;
}

bool SaveLocalMPState(melonDS::u32 frame)
{
    std::string stateSaveDir;
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        if (G.StateSaveDir.empty() || G.StateSaveFrame == 0) return false;
        if (frame != G.StateSaveFrame || G.LocalMPSaved) return false;
        if (!AllStatesSavedLocked()) return false;
        G.LocalMPSaved = true;
        stateSaveDir = G.StateSaveDir;
    }

    if (melonDS::MPInterface::GetType() != melonDS::MPInterface_Local)
    {
        std::printf("NSMB Test: LocalMP snapshot skipped because MPInterface is not Local\n");
        return false;
    }

    auto* localMP = dynamic_cast<melonDS::LocalMP*>(&melonDS::MPInterface::Get());
    if (!localMP)
    {
        std::printf("NSMB Test: LocalMP snapshot failed because LocalMP cast failed\n");
        return false;
    }

    std::vector<melonDS::u8> buffer;
    if (!localMP->SnapshotForTest(buffer) || buffer.empty())
    {
        std::printf("NSMB Test: LocalMP snapshot failed\n");
        return false;
    }

    const std::filesystem::path path = LocalMPStatePath(stateSaveDir);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        std::printf("NSMB Test: failed to open LocalMP state for write: %ls\n", path.c_str());
        return false;
    }

    file.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    if (!file)
    {
        std::printf("NSMB Test: failed to write LocalMP state: %ls\n", path.c_str());
        return false;
    }

    std::printf("NSMB Test: saved LocalMP state frame=%u path=%ls bytes=%zu\n",
        frame,
        path.c_str(),
        buffer.size());
    return true;
}

bool WaitForStateLoadBarrier(int instanceID)
{
    if (G.TestInstanceCount <= 1) return true;

    const auto start = std::chrono::steady_clock::now();
    for (;;)
    {
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            bool allLoaded = true;
            for (int i = 0; i < G.TestInstanceCount; i++)
            {
                if (!G.StateLoaded[i])
                {
                    allLoaded = false;
                    break;
                }
            }
            if (allLoaded) return true;
        }

        if (G.TestWaitTimeoutMs > 0)
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= G.TestWaitTimeoutMs)
            {
                std::printf("NSMB Test: state load barrier timeout inst=%d waitedMs=%d\n",
                    instanceID,
                    G.TestWaitTimeoutMs);
                return false;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool WaitForLocalMPLoadFinished(int instanceID)
{
    const auto start = std::chrono::steady_clock::now();
    for (;;)
    {
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            if (G.LocalMPLoadFinished)
                return G.LocalMPLoaded;
        }

        if (G.TestWaitTimeoutMs > 0)
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= G.TestWaitTimeoutMs)
            {
                std::printf("NSMB Test: LocalMP load barrier timeout inst=%d waitedMs=%d\n",
                    instanceID,
                    G.TestWaitTimeoutMs);
                return false;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool LoadLocalMPStateOnce(int instanceID)
{
    std::string stateLoadDir;
    bool shouldLoad = false;
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        if (G.StateLoadDir.empty() || !G.StateLoadFrameSet) return false;
        if (!G.LocalMPLoadStarted)
        {
            G.LocalMPLoadStarted = true;
            shouldLoad = true;
            stateLoadDir = G.StateLoadDir;
        }
    }

    if (!shouldLoad)
        return WaitForLocalMPLoadFinished(instanceID);

    bool loaded = false;
    const std::filesystem::path path = LocalMPStatePath(stateLoadDir);
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        std::printf("NSMB Test: failed to open LocalMP state for read: %ls\n", path.c_str());
    }
    else
    {
        std::vector<melonDS::u8> buffer(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());

        if (melonDS::MPInterface::GetType() != melonDS::MPInterface_Local)
        {
            std::printf("NSMB Test: LocalMP restore skipped because MPInterface is not Local\n");
        }
        else if (auto* localMP = dynamic_cast<melonDS::LocalMP*>(&melonDS::MPInterface::Get()))
        {
            loaded = localMP->RestoreForTest(buffer.data(), buffer.size());
            std::printf("NSMB Test: loaded LocalMP state path=%ls bytes=%zu ok=%d\n",
                path.c_str(),
                buffer.size(),
                loaded ? 1 : 0);
        }
        else
        {
            std::printf("NSMB Test: LocalMP restore failed because LocalMP cast failed\n");
        }
    }

    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        G.LocalMPLoaded = loaded;
        G.LocalMPLoadFinished = true;
    }
    return loaded;
}

bool LoadState(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    std::string stateLoadDir;
    melonDS::u32 stateLoadFrame = 0;
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        if (G.StateLoadDir.empty() || !G.StateLoadFrameSet) return false;
        if (frame != G.StateLoadFrame || G.StateLoaded[instanceID]) return false;
        stateLoadDir = G.StateLoadDir;
        stateLoadFrame = G.StateLoadFrame;
    }

    const std::filesystem::path path = StatePath(stateLoadDir, instanceID);
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        std::printf("NSMB Test: failed to open savestate for read: %ls\n", path.c_str());
        return false;
    }

    std::vector<char> buffer(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    if (buffer.empty())
    {
        std::printf("NSMB Test: savestate is empty: %ls\n", path.c_str());
        return false;
    }

    melonDS::Savestate state(buffer.data(), static_cast<melonDS::u32>(buffer.size()), false);
    if (state.Error || !nds->DoSavestate(&state) || state.Error)
    {
        std::printf("NSMB Test: failed to load savestate inst=%d frame=%u path=%ls\n",
            instanceID,
            stateLoadFrame,
            path.c_str());
        return false;
    }

    // NDS savestate loading restores Wifi::PowerOn before Wifi::SetPowerCnt()
    // runs, so the normal power-on side effect can be skipped. Re-register the
    // instance with LocalMP before restoring the shared LocalMP queue snapshot.
    melonDS::Platform::MP_Begin(nds->UserData);

    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        G.StateLoaded[instanceID] = true;
    }
    std::printf("NSMB Test: loaded state inst=%d frame=%u path=%ls bytes=%zu\n",
        instanceID,
        stateLoadFrame,
        path.c_str(),
        buffer.size());
    WaitForStateLoadBarrier(instanceID);
    LoadLocalMPStateOnce(instanceID);
    return true;
}

}

bool IsEnabled()
{
    InitFromEnvironment();
    return G.Enabled;
}

void InitFromEnvironment()
{
    std::lock_guard<std::mutex> lock(G.Mutex);
    if (G.EnvChecked) return;
    G.EnvChecked = true;

    G.Enabled = EnvFlag("MELONDS_NSML_POC");
    G.TestEnabled = EnvFlag("MELONDS_NSML_TEST");
    G.TestFrames = static_cast<melonDS::u32>(std::max(0, EnvInt("MELONDS_NSML_TEST_FRAMES", 0)));
    G.TestInstanceCount = std::clamp(EnvInt("MELONDS_NSML_TEST_INSTANCES", 1), 1, 16);
    G.ActiveFpsStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_ACTIVE_FPS_START_FRAME", 0)));
    G.FrameBarrierEnabled = EnvFlag("MELONDS_NSML_FRAME_BARRIER");
    G.SerialRunEnabled = EnvFlag("MELONDS_NSML_SERIAL_RUN");
    G.HashEnabled = !EnvFlag("MELONDS_NSML_DISABLE_HASH");
    G.HashInterval = std::max(1, EnvInt("MELONDS_NSML_HASH_INTERVAL", 60));
    G.TestWaitTimeoutMs = std::max(0, EnvInt("MELONDS_NSML_WAIT_TIMEOUT_MS", 5000));
    G.TestQuitGraceMs = std::max(0, EnvInt("MELONDS_NSML_QUIT_GRACE_MS", 0));
    G.InputTraceEnabled = EnvFlag("MELONDS_NSML_INPUT_TRACE");
    G.InputTraceInterval = std::max(1, EnvInt("MELONDS_NSML_INPUT_TRACE_INTERVAL", 60));
    G.ScreenHashEnabled = EnvFlag("MELONDS_NSML_SCREEN_HASH");
    G.SeedWaitTimeoutMs = std::max(0, EnvInt("MELONDS_NSML_SEED_WAIT_TIMEOUT_MS", 10000));
    G.WaitForPeerBeforeStart = EnvFlag("MELONDS_NSML_WAIT_FOR_PEER");
    G.WaitForPeerAtNetplayStart = EnvFlag("MELONDS_NSML_WAIT_FOR_PEER_AT_NETPLAY_START");
    G.DeferNetworkUntilStart = EnvFlag("MELONDS_NSML_DEFER_NETWORK_UNTIL_START");
    G.NetplayFrameBarrierEnabled = EnvFlag("MELONDS_NSML_NETPLAY_FRAME_BARRIER");
    G.PacketBridgeEnabled = EnvFlag("MELONDS_NSML_PACKET_BRIDGE");
    G.PacketBridgeOnly = EnvFlag("MELONDS_NSML_PACKET_BRIDGE_ONLY");
    G.PacketBridgeAllowPreGame = EnvFlag("MELONDS_NSML_PACKET_BRIDGE_ALLOW_PRE_GAME");
    G.PacketBridgeTraceEnabled = EnvFlag("MELONDS_NSML_PACKET_BRIDGE_TRACE");
    G.PacketBridgeSendLocalPlayerOnly = !EnvFlag("MELONDS_NSML_PACKET_BRIDGE_SEND_ALL");
    G.PacketBridgeWaitEnabled = EnvFlag("MELONDS_NSML_PACKET_BRIDGE_WAIT");
    G.PacketBridgeWaitTimeoutMs = std::max(0, EnvInt("MELONDS_NSML_PACKET_BRIDGE_WAIT_TIMEOUT_MS", 0));
    G.PacketBridgeWaitStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_PACKET_BRIDGE_WAIT_START_FRAME", 0)));
    G.PacketBridgeWaitTickAhead = std::clamp(EnvInt("MELONDS_NSML_PACKET_BRIDGE_WAIT_TICK_AHEAD", 0), 0, 32);
    G.PacketBridgeDirectCaptureEnabled = EnvFlag("MELONDS_NSML_PACKET_BRIDGE_DIRECT_CAPTURE");
    G.PacketBridgeForceTickEnabled = EnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK");
    G.PacketBridgeForceTickStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK_START_FRAME", 0)));
    G.PacketBridgeForceTickBase = EnvInt("MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK_BASE", -1);
    G.PacketBridgeForceNetReady = EnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY");
    G.PacketBridgeForceNetReadyStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_START_FRAME", 0)));
    G.PacketBridgeForceNetReadyEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_END_FRAME", 0)));
    G.PacketBridgeForceNetReadyHostOnly =
        EnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_HOST_ONLY");
    G.PacketBridgeForceNetReadyClientOnly =
        EnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_CLIENT_ONLY");
    G.PacketBridgeForceNetReadyState10 =
        EnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_STATE10");
    G.PacketBridgeForceNetReadyState10ClientOnly =
        EnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_STATE10_CLIENT_ONLY");
    G.PacketBridgeForceLoadGameSM = EnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM");
    G.PacketBridgeForceLoadGameSMStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_START_FRAME", 0)));
    G.PacketBridgeForceLoadGameSMStep = static_cast<melonDS::u32>(
        std::clamp(EnvInt("MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_STEP", 3), 0, 7));
    G.PacketBridgeForceLoadGameSMTimer =
        EnvInt("MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_TIMER", -1);
    G.PacketBridgeForceLoadGameSMFlags =
        EnvInt("MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_FLAGS", -1);
    G.PacketBridgeForceLoadGameSMRunUpdate = EnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_RUN_UPDATE");
    G.PacketBridgeForceLoadGameSMRunUpdateClientOnly =
        !EnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_RUN_UPDATE_ALL");
    G.PacketBridgeForceLoadGameSMPulseAction =
        EnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_PULSE_ACTION");
    G.PacketBridgeForceLoadGameSMBaselineFlags =
        EnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_BASELINE_FLAGS");
    G.PacketBridgeForceLoadGameSMPreload =
        EnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_PRELOAD");
    G.PacketBridgeForceLoadGameSMAllowCourseSelect =
        EnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_LOAD_GAME_SM_ALLOW_COURSE_SELECT");
    G.PacketBridgeForceStagePacketWords =
        EnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS");
    G.PacketBridgeForceStagePacketWordsStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS_START_FRAME", 0)));
    G.PacketBridgeForceStagePacketWordsEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_PACKET_WORDS_END_FRAME", 0)));
    G.PacketBridgeForceStageNet20OnStageScene =
        EnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_NET20_ON_STAGE_SCENE");
    G.PacketBridgeForceGameLocalPlayerID =
        EnvInt("MELONDS_NSML_PACKET_BRIDGE_FORCE_GAME_LOCAL_PLAYER_ID", -1);
    G.PacketBridgeForceGameLocalPlayerIDStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_PACKET_BRIDGE_FORCE_GAME_LOCAL_PLAYER_ID_START_FRAME", 0)));
    G.PacketBridgeForceGameLocalPlayerIDEarly =
        EnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_GAME_LOCAL_PLAYER_ID_EARLY");
    G.PacketBridgeDummyAlloc =
        EnvFlag("MELONDS_NSML_PACKET_BRIDGE_DUMMY_ALLOC");
    G.PacketBridgeDummyAllocFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_PACKET_BRIDGE_DUMMY_ALLOC_FRAME", 0)));
    G.PacketBridgeDummyAllocSize = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_PACKET_BRIDGE_DUMMY_ALLOC_SIZE", 0)));
    G.PacketBridgeScheduleLoadGameSM =
        EnvFlag("MELONDS_NSML_PACKET_BRIDGE_SCHEDULE_LOAD_GAME_SM");
    G.PacketBridgeSubMenuDirectChange =
        EnvFlag("MELONDS_NSML_PACKET_BRIDGE_SUBMENU_DIRECT");
    G.PacketBridgeSubMenuCallCreate =
        EnvFlag("MELONDS_NSML_PACKET_BRIDGE_SUBMENU_CALL_CREATE");
    if (!ParsePacketBridgeSubMenuSchedule(
            std::getenv("MELONDS_NSML_PACKET_BRIDGE_SUBMENU_SCHEDULE"),
            G.PacketBridgeSubMenuScheduleEntries))
    {
        std::printf("NSMB PacketBridge: invalid submenu schedule\n");
        G.PacketBridgeSubMenuScheduleEntries.clear();
    }
    G.PacketBridgeForceStageStartSMFields =
        EnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_START_SM_FIELDS");
    G.PacketBridgeForceStageStartSMFieldsStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_START_SM_FIELDS_START_FRAME", 0)));
    G.PacketBridgeForceStageStartSMUseLoadStep =
        EnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_START_SM_USE_LOAD_STEP");
    G.PacketBridgeRunStageStartSMUpdate =
        EnvFlag("MELONDS_NSML_PACKET_BRIDGE_RUN_STAGE_START_SM_UPDATE");
    G.PacketBridgeRunStageStartSMUpdateStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_PACKET_BRIDGE_RUN_STAGE_START_SM_UPDATE_START_FRAME", 0)));
    G.PacketBridgeRunVSConnectOnUpdate =
        EnvFlag("MELONDS_NSML_PACKET_BRIDGE_RUN_VSCONNECT_ON_UPDATE");
    G.PacketBridgeRunVSConnectOnUpdateStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_PACKET_BRIDGE_RUN_VSCONNECT_ON_UPDATE_START_FRAME", 0)));
    G.PacketBridgeForceMvlFileCache =
        EnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_FILE_CACHE");
    G.PacketBridgeForceMvlFileCacheStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_FILE_CACHE_START_FRAME", 0)));
    G.PacketBridgeForceMvlLoadThread =
        EnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_LOAD_THREAD");
    G.PacketBridgeForceMvlLoadThreadStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_PACKET_BRIDGE_FORCE_MVL_LOAD_THREAD_START_FRAME", 0)));
    G.PacketBridgeMaxPumpEvents = std::clamp(
        EnvInt("MELONDS_NSML_PACKET_BRIDGE_MAX_PUMP_EVENTS", kMaxPumpEvents), 1, kMaxPumpEvents);
    G.PacketBridgeMaxTickLead = EnvInt("MELONDS_NSML_PACKET_BRIDGE_MAX_TICK_LEAD", -1);
    G.PacketBridgeMaxFrameLead = EnvInt("MELONDS_NSML_PACKET_BRIDGE_MAX_FRAME_LEAD", -1);
    G.PacketBridgeThrottleTimeoutMs = std::max(
        0, EnvInt("MELONDS_NSML_PACKET_BRIDGE_THROTTLE_TIMEOUT_MS", 5000));
    G.PacketBridgeThrottleStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_PACKET_BRIDGE_THROTTLE_START_FRAME", 0)));
    G.PacketBridgeLocalInputDelay = std::max(
        0, EnvInt("MELONDS_NSML_PACKET_BRIDGE_LOCAL_INPUT_DELAY", 0));
    G.PacketBridgeNeutralizeLocalInput =
        EnvFlag("MELONDS_NSML_PACKET_BRIDGE_NEUTRALIZE_LOCAL_INPUT");
    G.PacketBridgePreserveLocalTouch =
        EnvFlag("MELONDS_NSML_PACKET_BRIDGE_PRESERVE_LOCAL_TOUCH");
    G.PacketBridgeSendDelayFrames = std::max(
        0, EnvInt("MELONDS_NSML_PACKET_BRIDGE_SEND_DELAY_FRAMES", 0));
    G.PacketBridgeSendJitterFrames = std::max(
        0, EnvInt("MELONDS_NSML_PACKET_BRIDGE_SEND_JITTER_FRAMES", 0));
    G.InputSendDelayFrames = std::max(
        0, EnvInt("MELONDS_NSML_INPUT_SEND_DELAY_FRAMES", 0));
    G.InputSendJitterFrames = std::max(
        0, EnvInt("MELONDS_NSML_INPUT_SEND_JITTER_FRAMES", 0));
    G.InputUnreliable = EnvFlag("MELONDS_NSML_INPUT_UNRELIABLE");
    G.InputBundleHistory = std::clamp(
        EnvInt("MELONDS_NSML_INPUT_BUNDLE_HISTORY", 0), 0, 31);
    G.InputDropModulo = std::max(0, EnvInt("MELONDS_NSML_INPUT_DROP_MODULO", 0));
    G.InputDropOffset = std::max(0, EnvInt("MELONDS_NSML_INPUT_DROP_OFFSET", 0));
    if (G.InputDropModulo > 0)
        G.InputDropOffset %= G.InputDropModulo;
    G.InputNetplayMaxFrameLead =
        EnvInt("MELONDS_NSML_INPUT_MAX_FRAME_LEAD", G.InputNetplayOnly ? 2 : -1);
    G.DirectMvlBootEnabled = EnvFlag("MELONDS_NSML_DIRECT_MVL_BOOT");
    G.DirectMvlBootHostOnly = EnvFlag("MELONDS_NSML_DIRECT_MVL_BOOT_HOST_ONLY");
    G.DirectMvlBootClientOnly = EnvFlag("MELONDS_NSML_DIRECT_MVL_BOOT_CLIENT_ONLY");
    G.DirectMvlBootFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_DIRECT_MVL_BOOT_FRAME", 900)));
    G.DirectMvlBootScene = std::clamp(EnvInt("MELONDS_NSML_DIRECT_MVL_BOOT_SCENE", 0x0F), 0, 0xFFFF);
    G.DirectMvlBootStage = std::clamp(EnvInt("MELONDS_NSML_DIRECT_MVL_BOOT_STAGE", 0), 0, 4);
    G.DirectMvlBootPlayerID = EnvInt("MELONDS_NSML_DIRECT_MVL_BOOT_PLAYER_ID", -1);
    G.DirectMvlBootUseLoadGameSM = EnvFlag("MELONDS_NSML_DIRECT_MVL_BOOT_LOAD_SM");
    G.DirectMvlBootPatchLoadGameSMOnly = EnvFlag("MELONDS_NSML_DIRECT_MVL_BOOT_PATCH_LOAD_SM_ONLY");
    G.DirectMvlBootCallUpdateLoadGameSM = EnvFlag("MELONDS_NSML_DIRECT_MVL_BOOT_CALL_UPDATE_SM");
    G.DirectMvlBootCallStartLoadLevel = EnvFlag("MELONDS_NSML_DIRECT_MVL_BOOT_CALL_START_LOAD");
    G.DirectMvlBootCallCreateCourseSelect = EnvFlag("MELONDS_NSML_DIRECT_MVL_BOOT_CALL_COURSE_SELECT");
    G.DirectMvlBootCallObjectCourseSelect = EnvFlag("MELONDS_NSML_DIRECT_MVL_BOOT_CALL_OBJECT_COURSE_SELECT");
    G.ForceCourseSelectFactory = EnvFlag("MELONDS_NSML_FORCE_COURSE_SELECT_FACTORY");
    G.ForceCourseSelectFactoryFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_COURSE_SELECT_FACTORY_FRAME", 0)));
    G.ForceCourseSelectFactoryPlayerArg = EnvInt("MELONDS_NSML_FORCE_COURSE_SELECT_FACTORY_PLAYER_ARG", -1);

    const char* inputScript = std::getenv("MELONDS_NSML_INPUT_SCRIPT");
    if (inputScript && inputScript[0]) G.InputScriptPath = inputScript;

    const char* scriptRemotePacketInputScript = std::getenv("MELONDS_NSML_SCRIPT_REMOTE_PACKET_INPUT_SCRIPT");
    if (scriptRemotePacketInputScript && scriptRemotePacketInputScript[0])
        G.ScriptRemotePacketInputScriptPath = scriptRemotePacketInputScript;

    const char* hashLog = std::getenv("MELONDS_NSML_HASH_LOG");
    if (hashLog && hashLog[0]) G.HashLogPath = hashLog;

    const char* screenshotDir = std::getenv("MELONDS_NSML_SCREENSHOT_DIR");
    if (screenshotDir && screenshotDir[0]) G.ScreenshotDir = screenshotDir;
    G.ScreenshotInterval = std::max(0, EnvInt("MELONDS_NSML_SCREENSHOT_INTERVAL", 0));

    const char* ramDumpDir = std::getenv("MELONDS_NSML_RAM_DUMP_DIR");
    if (ramDumpDir && ramDumpDir[0]) G.RamDumpDir = ramDumpDir;
    G.RamDumpInterval = std::max(0, EnvInt("MELONDS_NSML_RAM_DUMP_INTERVAL", 0));
    if (!ParseFrameRanges(std::getenv("MELONDS_NSML_RAM_DUMP_FRAMES"), G.RamDumpRanges))
    {
        std::printf("NSMB Test: invalid RAM dump frame list\n");
        G.RamDumpRanges.clear();
    }

    const char* gameStateTrace = std::getenv("MELONDS_NSML_GAME_STATE_TRACE");
    if (gameStateTrace && gameStateTrace[0]) G.GameStateTracePath = gameStateTrace;
    G.GameStateTraceInterval = std::max(1, EnvInt("MELONDS_NSML_GAME_STATE_TRACE_INTERVAL", 60));
    G.GameStateTraceStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_GAME_STATE_TRACE_START_FRAME", 0)));
    G.GameStateTraceEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_GAME_STATE_TRACE_END_FRAME", 0)));
    G.GameStateTraceExtended = EnvFlag("MELONDS_NSML_GAME_STATE_TRACE_EXTENDED");
    G.GameStateSyncEnabled = EnvFlag("MELONDS_NSML_STATE_SYNC");
    G.GameStateSyncExtended = EnvFlag("MELONDS_NSML_STATE_SYNC_EXTENDED");
    G.GameStateApplyEnabled = EnvFlag("MELONDS_NSML_STATE_APPLY");
    G.GameStateApplyCriticalGlobals = true;
    G.GameStateApplyStarObjects = true;
    G.GameStateApplyStageObjects = true;
    G.GameStateApplyPlayerActors = true;
    if (const char* applyMode = std::getenv("MELONDS_NSML_STATE_APPLY_MODE"))
    {
        if (!std::strcmp(applyMode, "critical"))
        {
            G.GameStateApplyStageObjects = false;
            G.GameStateApplyPlayerActors = false;
        }
        else if (!std::strcmp(applyMode, "globals"))
        {
            G.GameStateApplyStarObjects = false;
            G.GameStateApplyStageObjects = false;
            G.GameStateApplyPlayerActors = false;
        }
        else if (!std::strcmp(applyMode, "objects"))
        {
            G.GameStateApplyCriticalGlobals = false;
        }
    }
    G.GameStateSyncInterval = std::max(1, EnvInt("MELONDS_NSML_STATE_SYNC_INTERVAL", 60));

    const char* memPatchFile = std::getenv("MELONDS_NSML_MEM_PATCH_FILE");
    if (memPatchFile && memPatchFile[0]) G.MemPatchFile = memPatchFile;
    const char* memPatchFrame = std::getenv("MELONDS_NSML_MEM_PATCH_FRAME");
    if (memPatchFrame && memPatchFrame[0])
    {
        G.MemPatchFrame = static_cast<melonDS::u32>(std::max(0, std::atoi(memPatchFrame)));
        G.MemPatchFrameSet = true;
    }
    G.MemPatchInstance = EnvInt("MELONDS_NSML_MEM_PATCH_INSTANCE", -1);
    if (!ParseFrameRanges(std::getenv("MELONDS_NSML_MEM_PATCH_RANGES"), G.MemPatchRanges))
    {
        std::printf("NSMB Test: invalid memory patch range list\n");
        G.MemPatchRanges.clear();
    }
    G.VsStarSnapFrame = static_cast<melonDS::u32>(std::max(0, EnvInt("MELONDS_NSML_VS_STAR_SNAP_FRAME", 0)));
    G.VsStarSnapPlayerSlot = std::clamp(EnvInt("MELONDS_NSML_VS_STAR_SNAP_PLAYER_SLOT", 0), 0, 1);
    G.PlayerSnapToStarFrame = static_cast<melonDS::u32>(std::max(0, EnvInt("MELONDS_NSML_PLAYER_SNAP_TO_STAR_FRAME", 0)));
    G.PlayerSnapToStarSlot = std::clamp(EnvInt("MELONDS_NSML_PLAYER_SNAP_TO_STAR_SLOT", 0), 0, 1);
    G.PlayerStickToStarStartFrame = static_cast<melonDS::u32>(std::max(0, EnvInt("MELONDS_NSML_PLAYER_STICK_TO_STAR_START_FRAME", 0)));
    G.PlayerStickToStarEndFrame = static_cast<melonDS::u32>(std::max(0, EnvInt("MELONDS_NSML_PLAYER_STICK_TO_STAR_END_FRAME", 0)));
    if (G.PlayerStickToStarEndFrame == 0)
        G.PlayerStickToStarEndFrame = G.PlayerStickToStarStartFrame;
    if (G.PlayerStickToStarEndFrame < G.PlayerStickToStarStartFrame)
        std::swap(G.PlayerStickToStarStartFrame, G.PlayerStickToStarEndFrame);
    G.PlayerStickToStarSlot = std::clamp(EnvInt("MELONDS_NSML_PLAYER_STICK_TO_STAR_SLOT", 0), 0, 1);
    G.ForcePlayerCountEnabled = EnvFlag("MELONDS_NSML_FORCE_PLAYER_COUNT");
    G.ForcePlayerCountHostOnly = EnvFlag("MELONDS_NSML_FORCE_PLAYER_COUNT_HOST_ONLY");
    G.ForcePlayerCountClientOnly = EnvFlag("MELONDS_NSML_FORCE_PLAYER_COUNT_CLIENT_ONLY");
    G.ForcePlayerCountStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_COUNT_START_FRAME", 0)));
    G.ForcePlayerCountEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_COUNT_END_FRAME", 0)));
    G.ForcePlayerCountValue = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_COUNT_VALUE", 2)));
    G.ForceStageSceneRuntimeWordsEnabled = EnvFlag("MELONDS_NSML_FORCE_STAGE_SCENE_RUNTIME_WORDS");
    G.ForceStageSceneRuntimeWordsHostOnly = EnvFlag("MELONDS_NSML_FORCE_STAGE_SCENE_RUNTIME_WORDS_HOST_ONLY");
    G.ForceStageSceneRuntimeWordsClientOnly = EnvFlag("MELONDS_NSML_FORCE_STAGE_SCENE_RUNTIME_WORDS_CLIENT_ONLY");
    G.ForceStageSceneRuntimeWordsStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_STAGE_SCENE_RUNTIME_WORDS_START_FRAME", 0)));
    G.ForceStageSceneRuntimeWordsEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_STAGE_SCENE_RUNTIME_WORDS_END_FRAME", 0)));
    G.ForceStageSceneWord154 = static_cast<melonDS::u32>(
        std::strtoul(std::getenv("MELONDS_NSML_FORCE_STAGE_SCENE_WORD154")
            ? std::getenv("MELONDS_NSML_FORCE_STAGE_SCENE_WORD154") : "1", nullptr, 0));
    G.ForceStageSceneWord160 = static_cast<melonDS::u32>(
        std::strtoul(std::getenv("MELONDS_NSML_FORCE_STAGE_SCENE_WORD160")
            ? std::getenv("MELONDS_NSML_FORCE_STAGE_SCENE_WORD160") : "0xDA", nullptr, 0));
    G.ForceStageSceneActiveEnabled = EnvFlag("MELONDS_NSML_FORCE_STAGE_SCENE_ACTIVE");
    G.ForceStageSceneActiveHostOnly = EnvFlag("MELONDS_NSML_FORCE_STAGE_SCENE_ACTIVE_HOST_ONLY");
    G.ForceStageSceneActiveClientOnly = EnvFlag("MELONDS_NSML_FORCE_STAGE_SCENE_ACTIVE_CLIENT_ONLY");
    G.ForceStageSceneActiveStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_STAGE_SCENE_ACTIVE_START_FRAME", 0)));
    G.ForceStageSceneActiveEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_STAGE_SCENE_ACTIVE_END_FRAME", 0)));
    G.ForceStageCameraSlotEnabled = EnvFlag("MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT");
    G.ForceStageCameraSlotVerticalOnly = EnvFlag("MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT_VERTICAL_ONLY");
    G.ForceStageCameraSlotStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT_START_FRAME", 0)));
    G.ForceStageCameraSlotEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT_END_FRAME", 0)));
    G.ForceStageCameraSlotSource = std::clamp(EnvInt("MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT_SOURCE", 0), 0, 1);
    G.ForceStageCameraSlotDest = std::clamp(EnvInt("MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT_DEST", 1), 0, 1);
    if (const char* x = std::getenv("MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT_X"))
    {
        G.ForceStageCameraSlotOverrideX = true;
        G.ForceStageCameraSlotX = static_cast<melonDS::u32>(std::strtoul(x, nullptr, 0));
    }
    if (const char* y = std::getenv("MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT_Y"))
    {
        G.ForceStageCameraSlotOverrideY = true;
        G.ForceStageCameraSlotY = static_cast<melonDS::u32>(std::strtoul(y, nullptr, 0));
    }
    if (const char* width = std::getenv("MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT_WIDTH"))
    {
        G.ForceStageCameraSlotOverrideWidth = true;
        G.ForceStageCameraSlotWidth = static_cast<melonDS::u32>(std::strtoul(width, nullptr, 0));
    }
    if (const char* height = std::getenv("MELONDS_NSML_FORCE_STAGE_CAMERA_SLOT_HEIGHT"))
    {
        G.ForceStageCameraSlotOverrideHeight = true;
        G.ForceStageCameraSlotHeight = static_cast<melonDS::u32>(std::strtoul(height, nullptr, 0));
    }
    G.ForceStageCameraObjectXEnabled = EnvFlag("MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X");
    G.ForceStageCameraObjectXStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_START_FRAME", 0)));
    G.ForceStageCameraObjectXEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_END_FRAME", 0)));
    G.ForceStageCameraObjectX = static_cast<melonDS::u32>(
        std::strtoul(std::getenv("MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_VALUE")
            ? std::getenv("MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_VALUE") : "0", nullptr, 0));
    G.ForceStageCameraObjectXWriteDisplay =
        EnvFlag("MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_WRITE_DISPLAY");
    G.ForceStageCameraObjectXWriteSlot =
        EnvFlag("MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_WRITE_SLOT");
    G.ForceStageCameraObjectXSlot =
        std::clamp(EnvInt("MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_X_SLOT", 1), 0, 1);
    if (const char* z = std::getenv("MELONDS_NSML_FORCE_STAGE_CAMERA_OBJECT_Z_VALUE"))
    {
        G.ForceStageCameraObjectZEnabled = true;
        G.ForceStageCameraObjectZ = static_cast<melonDS::u32>(std::strtoul(z, nullptr, 0));
    }
    G.ForceStageFXSettingsEnabled = EnvFlag("MELONDS_NSML_FORCE_STAGEFX_SETTINGS");
    G.ForceStageFXSettingsHostOnly = EnvFlag("MELONDS_NSML_FORCE_STAGEFX_SETTINGS_HOST_ONLY");
    G.ForceStageFXSettingsClientOnly = EnvFlag("MELONDS_NSML_FORCE_STAGEFX_SETTINGS_CLIENT_ONLY");
    G.ForceStageFXSettingsStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_STAGEFX_SETTINGS_START_FRAME", 0)));
    G.ForceStageFXSettingsEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_STAGEFX_SETTINGS_END_FRAME", 0)));
    G.ForceStageFXSettingsValue = static_cast<melonDS::u32>(
        std::strtoul(std::getenv("MELONDS_NSML_FORCE_STAGEFX_SETTINGS_VALUE")
            ? std::getenv("MELONDS_NSML_FORCE_STAGEFX_SETTINGS_VALUE") : "0x8000", nullptr, 0));
    G.CallStageScenePostCreateEnabled = EnvFlag("MELONDS_NSML_CALL_STAGE_SCENE_POST_CREATE");
    G.CallStageScenePostCreateHostOnly = EnvFlag("MELONDS_NSML_CALL_STAGE_SCENE_POST_CREATE_HOST_ONLY");
    G.CallStageScenePostCreateClientOnly = EnvFlag("MELONDS_NSML_CALL_STAGE_SCENE_POST_CREATE_CLIENT_ONLY");
    G.CallStageScenePostCreateFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_CALL_STAGE_SCENE_POST_CREATE_FRAME", 0)));
    G.ForceStageActorFreezeFlagEnabled = EnvFlag("MELONDS_NSML_FORCE_STAGE_ACTOR_FREEZE_FLAG");
    G.ForceStageActorFreezeFlagHostOnly = EnvFlag("MELONDS_NSML_FORCE_STAGE_ACTOR_FREEZE_FLAG_HOST_ONLY");
    G.ForceStageActorFreezeFlagClientOnly = EnvFlag("MELONDS_NSML_FORCE_STAGE_ACTOR_FREEZE_FLAG_CLIENT_ONLY");
    G.ForceStageActorFreezeFlagStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_STAGE_ACTOR_FREEZE_FLAG_START_FRAME", 0)));
    G.ForceStageActorFreezeFlagEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_STAGE_ACTOR_FREEZE_FLAG_END_FRAME", 0)));
    G.ForceStageActorFreezeFlagValue = static_cast<melonDS::u32>(
        std::strtoul(std::getenv("MELONDS_NSML_FORCE_STAGE_ACTOR_FREEZE_FLAG_VALUE")
            ? std::getenv("MELONDS_NSML_FORCE_STAGE_ACTOR_FREEZE_FLAG_VALUE") : "0", nullptr, 0));
    G.ForcePlayerDeathCountersEnabled = EnvFlag("MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTERS");
    G.ForcePlayerDeathCountersHostOnly = EnvFlag("MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTERS_HOST_ONLY");
    G.ForcePlayerDeathCountersClientOnly = EnvFlag("MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTERS_CLIENT_ONLY");
    G.ForcePlayerDeathCountersStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTERS_START_FRAME", 0)));
    G.ForcePlayerDeathCountersEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTERS_END_FRAME", 0)));
    G.ForcePlayerDeathCounter0 = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTER0", 0)));
    G.ForcePlayerDeathCounter1 = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_DEATH_COUNTER1", 0)));
    G.ForcePlayerLivesEnabled = EnvFlag("MELONDS_NSML_FORCE_PLAYER_LIVES");
    G.ForcePlayerLife0 = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_LIFE0", 5)));
    G.ForcePlayerLife1 = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_LIFE1", 5)));
    G.ForcePlayerInventoryPowerupsEnabled = EnvFlag("MELONDS_NSML_FORCE_PLAYER_INVENTORY_POWERUPS");
    G.ForcePlayerInventoryPowerupsStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_INVENTORY_POWERUPS_START_FRAME", 0)));
    G.ForcePlayerInventoryPowerupsEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_INVENTORY_POWERUPS_END_FRAME", 0)));
    G.ForcePlayerInventoryPowerup0 = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_INVENTORY_POWERUP0", 0)));
    G.ForcePlayerInventoryPowerup1 = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_INVENTORY_POWERUP1", 0)));
    G.ForcePlayerStarCountersEnabled = EnvFlag("MELONDS_NSML_FORCE_PLAYER_STAR_COUNTERS");
    G.ForcePlayerStarCountersStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_STAR_COUNTERS_START_FRAME", 0)));
    G.ForcePlayerStarCountersEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_STAR_COUNTERS_END_FRAME", 0)));
    G.ForcePlayerBattleStars0 = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_BATTLE_STARS0", 0)));
    G.ForcePlayerBattleStars1 = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_BATTLE_STARS1", 0)));
    G.ForcePlayerDisplayedStars0 = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_DISPLAYED_STARS0", 0)));
    G.ForcePlayerDisplayedStars1 = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_DISPLAYED_STARS1", 0)));
    G.ForcePlayerCollectedStars0 = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_COLLECTED_STARS0", 0)));
    G.ForcePlayerCollectedStars1 = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_COLLECTED_STARS1", 0)));
    G.TracePlayerLifeChanges = EnvFlag("MELONDS_NSML_TRACE_PLAYER_LIFE_CHANGES");
    G.ForceStageActorPreUpdateGateEnabled = EnvFlag("MELONDS_NSML_FORCE_STAGE_ACTOR_PREUPDATE_GATE");
    G.ForceStageActorPreUpdateGateHostOnly = EnvFlag("MELONDS_NSML_FORCE_STAGE_ACTOR_PREUPDATE_GATE_HOST_ONLY");
    G.ForceStageActorPreUpdateGateClientOnly = EnvFlag("MELONDS_NSML_FORCE_STAGE_ACTOR_PREUPDATE_GATE_CLIENT_ONLY");
    G.ForceStageActorPreUpdateGateStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_STAGE_ACTOR_PREUPDATE_GATE_START_FRAME", 0)));
    G.ForceStageActorPreUpdateGateEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_STAGE_ACTOR_PREUPDATE_GATE_END_FRAME", 0)));
    G.ForceActorCategoryMaskEnabled = EnvFlag("MELONDS_NSML_FORCE_ACTOR_CATEGORY_MASK");
    G.ForceActorCategoryMaskHostOnly = EnvFlag("MELONDS_NSML_FORCE_ACTOR_CATEGORY_MASK_HOST_ONLY");
    G.ForceActorCategoryMaskClientOnly = EnvFlag("MELONDS_NSML_FORCE_ACTOR_CATEGORY_MASK_CLIENT_ONLY");
    G.ForceActorCategoryMaskStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_ACTOR_CATEGORY_MASK_START_FRAME", 0)));
    G.ForceActorCategoryMaskEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_ACTOR_CATEGORY_MASK_END_FRAME", 0)));
    G.ForceActorCategoryMaskValue = static_cast<melonDS::u32>(
        std::strtoul(std::getenv("MELONDS_NSML_FORCE_ACTOR_CATEGORY_MASK_VALUE")
            ? std::getenv("MELONDS_NSML_FORCE_ACTOR_CATEGORY_MASK_VALUE") : "0", nullptr, 0));
    G.ForcePlayerSignalUnlockEnabled = EnvFlag("MELONDS_NSML_FORCE_PLAYER_SIGNAL_UNLOCK");
    G.ForcePlayerSignalUnlockHostOnly = EnvFlag("MELONDS_NSML_FORCE_PLAYER_SIGNAL_UNLOCK_HOST_ONLY");
    G.ForcePlayerSignalUnlockClientOnly = EnvFlag("MELONDS_NSML_FORCE_PLAYER_SIGNAL_UNLOCK_CLIENT_ONLY");
    G.ForcePlayerSignalUnlockStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_SIGNAL_UNLOCK_START_FRAME", 0)));
    G.ForcePlayerSignalUnlockEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_SIGNAL_UNLOCK_END_FRAME", 0)));
    G.ForcePlayerUpdateEnableEnabled = EnvFlag("MELONDS_NSML_FORCE_PLAYER_UPDATE_ENABLE");
    G.ForcePlayerUpdateEnableHostOnly = EnvFlag("MELONDS_NSML_FORCE_PLAYER_UPDATE_ENABLE_HOST_ONLY");
    G.ForcePlayerUpdateEnableClientOnly = EnvFlag("MELONDS_NSML_FORCE_PLAYER_UPDATE_ENABLE_CLIENT_ONLY");
    G.ForcePlayerUpdateEnableStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_UPDATE_ENABLE_START_FRAME", 0)));
    G.ForcePlayerUpdateEnableEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_UPDATE_ENABLE_END_FRAME", 0)));
    G.ForceStageSceneStartGateEnabled = EnvFlag("MELONDS_NSML_FORCE_STAGE_SCENE_START_GATE");
    G.ForceStageSceneStartGateHostOnly = EnvFlag("MELONDS_NSML_FORCE_STAGE_SCENE_START_GATE_HOST_ONLY");
    G.ForceStageSceneStartGateClientOnly = EnvFlag("MELONDS_NSML_FORCE_STAGE_SCENE_START_GATE_CLIENT_ONLY");
    G.ForceStageSceneFadeReady = EnvFlag("MELONDS_NSML_FORCE_STAGE_SCENE_FADE_READY");
    G.ForceStageSceneInputLatchEnabled = EnvFlag("MELONDS_NSML_FORCE_STAGE_SCENE_INPUT_LATCH");
    G.ForceNetLocalAid = EnvInt("MELONDS_NSML_FORCE_NET_LOCAL_AID", -1);
    G.ForceNetLocalAidStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_NET_LOCAL_AID_START_FRAME", 0)));
    G.ForceNetLocalAidEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_NET_LOCAL_AID_END_FRAME", 0)));
    G.ForceWifiCommunicatingCount = EnvInt("MELONDS_NSML_FORCE_WIFI_COMMUNICATING_COUNT", -1);
    G.ForceWifiCommunicatingStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_WIFI_COMMUNICATING_START_FRAME", 0)));
    G.ForceWifiCommunicatingEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_WIFI_COMMUNICATING_END_FRAME", 0)));
    G.ScriptRemotePacketEnabled = EnvFlag("MELONDS_NSML_SCRIPT_REMOTE_PACKET");
    G.ScriptRemotePacketPlayer = EnvInt("MELONDS_NSML_SCRIPT_REMOTE_PACKET_PLAYER", -1);
    G.ScriptRemotePacketInputInstance = EnvInt("MELONDS_NSML_SCRIPT_REMOTE_PACKET_INPUT_INSTANCE", -1);
    G.ScriptRemotePacketStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_SCRIPT_REMOTE_PACKET_START_FRAME", 0)));
    G.ScriptRemotePacketEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_SCRIPT_REMOTE_PACKET_END_FRAME", 0)));
    G.PacketBridgeJitHelperPatchEnabled = EnvFlag("MELONDS_NSML_PACKET_BRIDGE_JIT_HELPER_PATCH");
    G.PacketBridgeJitHelperPatchFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_PACKET_BRIDGE_JIT_HELPER_PATCH_FRAME", 0)));
    G.InputNetplayOnly = EnvFlag("MELONDS_NSML_INPUT_NETPLAY_ONLY");
    G.InputNetplayTraceEnabled = EnvFlag("MELONDS_NSML_INPUT_NETPLAY_TRACE");
    G.NetworkPumpThreadEnabled = EnvFlag("MELONDS_NSML_NET_PUMP_THREAD");
    G.NetworkPumpSleepUs = std::clamp(EnvInt("MELONDS_NSML_NET_PUMP_SLEEP_US", 250), 50, 5000);
    G.InputWaitPollUs = std::clamp(EnvInt("MELONDS_NSML_INPUT_WAIT_POLL_US", 100), 50, 5000);
    G.RollbackEnabled = EnvFlag("MELONDS_NSML_ROLLBACK");
    G.RollbackResimulate = EnvFlag("MELONDS_NSML_ROLLBACK_RESIMULATE");
    G.RollbackRestoreProbe = EnvFlag("MELONDS_NSML_ROLLBACK_RESTORE_PROBE");
    const char* rollbackBackend = EnvCString("MELONDS_NSML_ROLLBACK_BACKEND", "savestate");
    if (!std::strcmp(rollbackBackend, "arm9ram") || !std::strcmp(rollbackBackend, "ram"))
        G.RollbackBackendMode = RollbackBackend::ARM9RAM;
    else
        G.RollbackBackendMode = RollbackBackend::Savestate;
    G.RollbackWindow = std::clamp(EnvInt("MELONDS_NSML_ROLLBACK_WINDOW", 20), 1, 180);
    G.RollbackCheckpointInterval = std::clamp(
        EnvInt("MELONDS_NSML_ROLLBACK_CHECKPOINT_INTERVAL", 1), 1, 30);
    G.RollbackResimulateDelayFrames = std::clamp(
        EnvInt("MELONDS_NSML_ROLLBACK_RESIMULATE_DELAY_FRAMES", 0), 0, 30);
    if (G.RollbackEnabled && G.InputNetplayOnly)
    {
        G.LocalWaitsForRemote = false;
        if (G.Delay > 2)
            G.Delay = std::min(G.Delay, 2);
    }
    G.ForceStageSceneStartGateStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_STAGE_SCENE_START_GATE_START_FRAME", 0)));
    G.ForceStageSceneStartGateEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_STAGE_SCENE_START_GATE_END_FRAME", 0)));
    G.ForceStageSceneStartGateValue = static_cast<melonDS::u32>(
        std::strtoul(std::getenv("MELONDS_NSML_FORCE_STAGE_SCENE_START_GATE_VALUE")
            ? std::getenv("MELONDS_NSML_FORCE_STAGE_SCENE_START_GATE_VALUE") : "1", nullptr, 0));
    G.ForceStageSceneContinueGateEnabled = EnvFlag("MELONDS_NSML_FORCE_STAGE_SCENE_CONTINUE_GATE");
    G.ForceStageSceneContinueGateHostOnly = EnvFlag("MELONDS_NSML_FORCE_STAGE_SCENE_CONTINUE_GATE_HOST_ONLY");
    G.ForceStageSceneContinueGateClientOnly = EnvFlag("MELONDS_NSML_FORCE_STAGE_SCENE_CONTINUE_GATE_CLIENT_ONLY");
    G.ForceStageSceneContinueGateStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_STAGE_SCENE_CONTINUE_GATE_START_FRAME", 0)));
    G.ForceStageSceneContinueGateEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_STAGE_SCENE_CONTINUE_GATE_END_FRAME", 0)));
    G.ForceStageSceneContinueGateValue = static_cast<melonDS::u32>(
        std::strtoul(std::getenv("MELONDS_NSML_FORCE_STAGE_SCENE_CONTINUE_GATE_VALUE")
            ? std::getenv("MELONDS_NSML_FORCE_STAGE_SCENE_CONTINUE_GATE_VALUE") : "1", nullptr, 0));
    G.ForceStageSceneState3GateEnabled = EnvFlag("MELONDS_NSML_FORCE_STAGE_SCENE_STATE3_GATE");
    G.ForceStageSceneState3GateHostOnly = EnvFlag("MELONDS_NSML_FORCE_STAGE_SCENE_STATE3_GATE_HOST_ONLY");
    G.ForceStageSceneState3GateClientOnly = EnvFlag("MELONDS_NSML_FORCE_STAGE_SCENE_STATE3_GATE_CLIENT_ONLY");
    G.ForceStageSceneState3GateStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_STAGE_SCENE_STATE3_GATE_START_FRAME", 0)));
    G.ForceStageSceneState3GateEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_STAGE_SCENE_STATE3_GATE_END_FRAME", 0)));
    G.ForceStageSceneState3GateValue = static_cast<melonDS::u32>(
        std::strtoul(std::getenv("MELONDS_NSML_FORCE_STAGE_SCENE_STATE3_GATE_VALUE")
            ? std::getenv("MELONDS_NSML_FORCE_STAGE_SCENE_STATE3_GATE_VALUE") : "1", nullptr, 0));
    G.ForceStageSceneEventFlagsEnabled = EnvFlag("MELONDS_NSML_FORCE_STAGE_SCENE_EVENT_FLAGS");
    G.ForceStageSceneEventFlagsHostOnly = EnvFlag("MELONDS_NSML_FORCE_STAGE_SCENE_EVENT_FLAGS_HOST_ONLY");
    G.ForceStageSceneEventFlagsClientOnly = EnvFlag("MELONDS_NSML_FORCE_STAGE_SCENE_EVENT_FLAGS_CLIENT_ONLY");
    G.ForceStageSceneEventFlagsStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_STAGE_SCENE_EVENT_FLAGS_START_FRAME", 0)));
    G.ForceStageSceneEventFlagsEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_STAGE_SCENE_EVENT_FLAGS_END_FRAME", 0)));
    G.ForceStageSceneEventFlagsValue = static_cast<melonDS::u32>(
        std::strtoul(std::getenv("MELONDS_NSML_FORCE_STAGE_SCENE_EVENT_FLAGS_VALUE")
            ? std::getenv("MELONDS_NSML_FORCE_STAGE_SCENE_EVENT_FLAGS_VALUE") : "0", nullptr, 0));
    G.ForceMvlPlayerReadyEnabled = EnvFlag("MELONDS_NSML_FORCE_MVL_PLAYER_READY");
    G.ForceMvlPlayerReadyHostOnly = EnvFlag("MELONDS_NSML_FORCE_MVL_PLAYER_READY_HOST_ONLY");
    G.ForceMvlPlayerReadyClientOnly = EnvFlag("MELONDS_NSML_FORCE_MVL_PLAYER_READY_CLIENT_ONLY");
    G.ForceMvlPlayerReadyStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_MVL_PLAYER_READY_START_FRAME", 0)));
    G.ForceMvlPlayerReadyEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_MVL_PLAYER_READY_END_FRAME", 0)));
    G.ForceMvlPlayerReadyValue = static_cast<melonDS::u32>(
        std::strtoul(std::getenv("MELONDS_NSML_FORCE_MVL_PLAYER_READY_VALUE")
            ? std::getenv("MELONDS_NSML_FORCE_MVL_PLAYER_READY_VALUE") : "0xFF00", nullptr, 0));
    G.ForceMvlPlayerReadySetA8EC = EnvFlag("MELONDS_NSML_FORCE_MVL_PLAYER_READY_SET_A8EC");
    G.ForceMvlPlayerReadyA8ECValue = static_cast<melonDS::u32>(
        std::strtoul(std::getenv("MELONDS_NSML_FORCE_MVL_PLAYER_READY_A8EC_VALUE")
            ? std::getenv("MELONDS_NSML_FORCE_MVL_PLAYER_READY_A8EC_VALUE") : "0xFF", nullptr, 0));
    G.ForceMvlRuntimeStateEnabled = EnvFlag("MELONDS_NSML_FORCE_MVL_RUNTIME_STATE");
    G.ForceMvlRuntimeStateHostOnly = EnvFlag("MELONDS_NSML_FORCE_MVL_RUNTIME_STATE_HOST_ONLY");
    G.ForceMvlRuntimeStateClientOnly = EnvFlag("MELONDS_NSML_FORCE_MVL_RUNTIME_STATE_CLIENT_ONLY");
    G.ForceMvlRuntimeStateStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_MVL_RUNTIME_STATE_START_FRAME", 0)));
    G.ForceMvlRuntimeStateEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_MVL_RUNTIME_STATE_END_FRAME", 0)));
    G.ForceMvlRuntimeStateValue = static_cast<melonDS::u32>(
        std::strtoul(std::getenv("MELONDS_NSML_FORCE_MVL_RUNTIME_STATE_VALUE")
            ? std::getenv("MELONDS_NSML_FORCE_MVL_RUNTIME_STATE_VALUE") : "3", nullptr, 0));
    G.ForcePlayerActorIDsEnabled = EnvFlag("MELONDS_NSML_FORCE_PLAYER_ACTOR_IDS");
    G.ForcePlayerActorIDsHostOnly = EnvFlag("MELONDS_NSML_FORCE_PLAYER_ACTOR_IDS_HOST_ONLY");
    G.ForcePlayerActorIDsClientOnly = EnvFlag("MELONDS_NSML_FORCE_PLAYER_ACTOR_IDS_CLIENT_ONLY");
    G.ForcePlayerActorIDsStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_ACTOR_IDS_START_FRAME", 0)));
    G.ForcePlayerActorIDsEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_ACTOR_IDS_END_FRAME", 0)));
    G.ForcePlayerActorPositionEnabled = EnvFlag("MELONDS_NSML_FORCE_PLAYER_ACTOR_POSITION");
    G.ForcePlayerActorPositionSlot =
        std::clamp(EnvInt("MELONDS_NSML_FORCE_PLAYER_ACTOR_POSITION_SLOT", 1), 0, 1);
    G.ForcePlayerActorPositionStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_ACTOR_POSITION_START_FRAME", 0)));
    G.ForcePlayerActorPositionEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_ACTOR_POSITION_END_FRAME", 0)));
    G.ForcePlayerActorPositionX = static_cast<melonDS::u32>(
        std::strtoul(std::getenv("MELONDS_NSML_FORCE_PLAYER_ACTOR_POSITION_X")
            ? std::getenv("MELONDS_NSML_FORCE_PLAYER_ACTOR_POSITION_X") : "0", nullptr, 0));
    G.ForcePlayerActorPositionY = static_cast<melonDS::u32>(
        std::strtoul(std::getenv("MELONDS_NSML_FORCE_PLAYER_ACTOR_POSITION_Y")
            ? std::getenv("MELONDS_NSML_FORCE_PLAYER_ACTOR_POSITION_Y") : "0", nullptr, 0));
    G.ForcePlayerActorPositionZ = static_cast<melonDS::u32>(
        std::strtoul(std::getenv("MELONDS_NSML_FORCE_PLAYER_ACTOR_POSITION_Z")
            ? std::getenv("MELONDS_NSML_FORCE_PLAYER_ACTOR_POSITION_Z") : "0", nullptr, 0));
    if (const char* character = std::getenv("MELONDS_NSML_FORCE_PLAYER_ACTOR_POSITION_CHARACTER"))
    {
        G.ForcePlayerActorPositionCharacterSet = true;
        G.ForcePlayerActorPositionCharacter = static_cast<melonDS::u16>(std::strtoul(character, nullptr, 0) & 0xFFFF);
    }
    if (const char* playerID = std::getenv("MELONDS_NSML_FORCE_PLAYER_ACTOR_POSITION_PLAYER_ID"))
    {
        G.ForcePlayerActorPositionPlayerIDSet = true;
        G.ForcePlayerActorPositionPlayerID = static_cast<melonDS::u8>(std::strtoul(playerID, nullptr, 0) & 0xFF);
    }
    G.ForcePlayerTransitionStatusEnabled = EnvFlag("MELONDS_NSML_FORCE_PLAYER_TRANSITION_STATUS");
    G.ForcePlayerTransitionStatusHostOnly = EnvFlag("MELONDS_NSML_FORCE_PLAYER_TRANSITION_STATUS_HOST_ONLY");
    G.ForcePlayerTransitionStatusClientOnly = EnvFlag("MELONDS_NSML_FORCE_PLAYER_TRANSITION_STATUS_CLIENT_ONLY");
    G.ForcePlayerTransitionStatusStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_TRANSITION_STATUS_START_FRAME", 0)));
    G.ForcePlayerTransitionStatusEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_PLAYER_TRANSITION_STATUS_END_FRAME", 0)));
    G.ForcePlayerTransitionStatusValue = static_cast<melonDS::u32>(
        std::strtoul(std::getenv("MELONDS_NSML_FORCE_PLAYER_TRANSITION_STATUS_VALUE")
            ? std::getenv("MELONDS_NSML_FORCE_PLAYER_TRANSITION_STATUS_VALUE") : "2", nullptr, 0));
    G.ForceEntranceSpawnPointersEnabled = EnvFlag("MELONDS_NSML_FORCE_ENTRANCE_SPAWN_POINTERS");
    G.ForceEntranceSpawnPointersHostOnly = EnvFlag("MELONDS_NSML_FORCE_ENTRANCE_SPAWN_POINTERS_HOST_ONLY");
    G.ForceEntranceSpawnPointersClientOnly = EnvFlag("MELONDS_NSML_FORCE_ENTRANCE_SPAWN_POINTERS_CLIENT_ONLY");
    G.ForceEntranceSpawnPointersStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_ENTRANCE_SPAWN_POINTERS_START_FRAME", 0)));
    G.ForceEntranceSpawnPointersEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_ENTRANCE_SPAWN_POINTERS_END_FRAME", 0)));
    G.ForceEntranceSpawnPtr0 = static_cast<melonDS::u32>(
        std::strtoul(std::getenv("MELONDS_NSML_FORCE_ENTRANCE_SPAWN_PTR0")
            ? std::getenv("MELONDS_NSML_FORCE_ENTRANCE_SPAWN_PTR0") : "0", nullptr, 0));
    G.ForceEntranceSpawnPtr1 = static_cast<melonDS::u32>(
        std::strtoul(std::getenv("MELONDS_NSML_FORCE_ENTRANCE_SPAWN_PTR1")
            ? std::getenv("MELONDS_NSML_FORCE_ENTRANCE_SPAWN_PTR1") : "0", nullptr, 0));
    G.ForceEntranceSpawnID0 = static_cast<melonDS::u32>(
        std::strtoul(std::getenv("MELONDS_NSML_FORCE_ENTRANCE_SPAWN_ID0")
            ? std::getenv("MELONDS_NSML_FORCE_ENTRANCE_SPAWN_ID0") : "0", nullptr, 0));
    G.ForceEntranceSpawnID1 = static_cast<melonDS::u32>(
        std::strtoul(std::getenv("MELONDS_NSML_FORCE_ENTRANCE_SPAWN_ID1")
            ? std::getenv("MELONDS_NSML_FORCE_ENTRANCE_SPAWN_ID1") : "1", nullptr, 0));
    G.ForceMvlStageLayoutGateEnabled = EnvFlag("MELONDS_NSML_FORCE_MVL_STAGE_LAYOUT_GATE");
    G.ForceMvlStageLayoutGateHostOnly = EnvFlag("MELONDS_NSML_FORCE_MVL_STAGE_LAYOUT_GATE_HOST_ONLY");
    G.ForceMvlStageLayoutGateClientOnly = EnvFlag("MELONDS_NSML_FORCE_MVL_STAGE_LAYOUT_GATE_CLIENT_ONLY");
    G.ForceMvlStageLayoutGateStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_MVL_STAGE_LAYOUT_GATE_START_FRAME", 0)));
    G.ForceMvlStageLayoutGateEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_MVL_STAGE_LAYOUT_GATE_END_FRAME", 0)));
    G.ForceMvlStageLayoutGateAddr = static_cast<melonDS::u32>(
        std::strtoul(std::getenv("MELONDS_NSML_FORCE_MVL_STAGE_LAYOUT_GATE_ADDR")
            ? std::getenv("MELONDS_NSML_FORCE_MVL_STAGE_LAYOUT_GATE_ADDR") : "0x020CAC74", nullptr, 0));
    G.ForceMvlStageLayoutGateValue = static_cast<melonDS::u32>(
        std::strtoul(std::getenv("MELONDS_NSML_FORCE_MVL_STAGE_LAYOUT_GATE_VALUE")
            ? std::getenv("MELONDS_NSML_FORCE_MVL_STAGE_LAYOUT_GATE_VALUE") : "5", nullptr, 0));
    G.ForceMvlStageLayoutBufferEnabled = EnvFlag("MELONDS_NSML_FORCE_MVL_STAGE_LAYOUT_BUFFER");
    G.ForceMvlStageLayoutBufferHostOnly = EnvFlag("MELONDS_NSML_FORCE_MVL_STAGE_LAYOUT_BUFFER_HOST_ONLY");
    G.ForceMvlStageLayoutBufferClientOnly = EnvFlag("MELONDS_NSML_FORCE_MVL_STAGE_LAYOUT_BUFFER_CLIENT_ONLY");
    G.ForceMvlStageLayoutBufferStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_MVL_STAGE_LAYOUT_BUFFER_START_FRAME", 0)));
    G.ForceMvlStageLayoutBufferEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_FORCE_MVL_STAGE_LAYOUT_BUFFER_END_FRAME", 0)));
    G.ForceMvlStageLayoutBufferAddr = static_cast<melonDS::u32>(
        std::strtoul(std::getenv("MELONDS_NSML_FORCE_MVL_STAGE_LAYOUT_BUFFER_ADDR")
            ? std::getenv("MELONDS_NSML_FORCE_MVL_STAGE_LAYOUT_BUFFER_ADDR") : "0x023C8000", nullptr, 0));
    G.CallMvlStageLayoutInitEnabled = EnvFlag("MELONDS_NSML_CALL_MVL_STAGE_LAYOUT_INIT");
    G.CallMvlStageLayoutInitHostOnly = EnvFlag("MELONDS_NSML_CALL_MVL_STAGE_LAYOUT_INIT_HOST_ONLY");
    G.CallMvlStageLayoutInitClientOnly = EnvFlag("MELONDS_NSML_CALL_MVL_STAGE_LAYOUT_INIT_CLIENT_ONLY");
    G.CallMvlStageLayoutInitFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_CALL_MVL_STAGE_LAYOUT_INIT_FRAME", 0)));

    const char* netRandomValue = std::getenv("MELONDS_NSML_NET_RANDOM_VALUE");
    if (netRandomValue && netRandomValue[0])
    {
        G.NetRandomPatchEnabled = true;
        G.NetRandomPatchAuto = EnvFlag("MELONDS_NSML_NET_RANDOM_AUTO");
        G.NetRandomPatchValue = static_cast<melonDS::u32>(std::strtoul(netRandomValue, nullptr, 0));
        G.NetRandomPatchFrame = static_cast<melonDS::u32>(
            std::max(0, EnvInt("MELONDS_NSML_NET_RANDOM_FRAME", 0)));
        G.MatchSeed = G.NetRandomPatchValue;
        G.MatchSeedConfigured = true;
    }

    const char* matchSeed = std::getenv("MELONDS_NSML_MATCH_SEED");
    if (matchSeed && matchSeed[0])
    {
        G.MatchSeed = static_cast<melonDS::u32>(std::strtoul(matchSeed, nullptr, 0));
        G.MatchSeedConfigured = true;
    }

    const char* stateSaveDir = std::getenv("MELONDS_NSML_STATE_SAVE_DIR");
    if (stateSaveDir && stateSaveDir[0]) G.StateSaveDir = stateSaveDir;
    G.StateSaveFrame = static_cast<melonDS::u32>(std::max(0, EnvInt("MELONDS_NSML_STATE_SAVE_FRAME", 0)));

    const char* stateLoadDir = std::getenv("MELONDS_NSML_STATE_LOAD_DIR");
    if (stateLoadDir && stateLoadDir[0]) G.StateLoadDir = stateLoadDir;
    const char* stateLoadFrame = std::getenv("MELONDS_NSML_STATE_LOAD_FRAME");
    if (stateLoadFrame && stateLoadFrame[0])
    {
        G.StateLoadFrame = static_cast<melonDS::u32>(std::max(0, std::atoi(stateLoadFrame)));
        G.StateLoadFrameSet = true;
    }

    if ((G.TestEnabled || G.Enabled) && !G.GameStateTracePath.empty())
    {
        G.GameStateTrace.open(G.GameStateTracePath, std::ios::out | std::ios::trunc);
        if (!G.GameStateTrace)
        {
            std::printf("NSMB Test: failed to open game state trace: %s\n", G.GameStateTracePath.c_str());
        }
        else
        {
            G.GameStateTrace << "instance,frame,stageID,stageGroup,vsMode,localPlayerID,arm9PC,arm9LR,arm9SP,arm9CPSR,appFrameLength,appUpdateTask,appSleepPhase,appSleepControl,appSleeping,appSleepPhaseTimer,appSleepWakeUpTimer,appBootParam,appBootTarget,appBootScene,ggid,netCurrentLanguage,netLocalAid,netState14,netState1C,netState20,netState24,netExpectedConsoleCount,netMultiBootSession,netSessionState,netModuleState,netMaxSessionChildren,netMaxConsoleCount,netState5C,netPacketTick,netPacketKeys,netPacketAction,netPacketByte5,netPacketByte6,netPacketByte7,netRandomValue,netRandomCallCount,netRandomBranchAddress,inputConsole0Held,inputConsole0Pressed,inputConsole1Held,inputConsole1Pressed,inputPlayer0Held,inputPlayer1Held,inputPlayer0Pressed,inputPlayer1Pressed,stageActorFreezeFlag,sceneIsSceneActive,scenePreviousSceneID,sceneNextSceneID,sceneCurrentSceneID,sceneNextSceneSettings,vsStarFound,vsStarGuid,vsStarBase,vsStarSettings,vsStarStateType,vsStarFlags,vsStarX,vsStarY,vsStarZ,vsStarActorFound,vsStarActorGuid,vsStarActorBase,vsStarActorSettings,vsStarActorStateType,vsStarActorFlags,vsStarActorX,vsStarActorY,vsStarActorZ,playerActor0Found,playerActor0Guid,playerActor0Base,playerActor0Settings,playerActor0StateType,playerActor0Flags,playerActor0X,playerActor0Y,playerActor0Z,playerActor0PrevX,playerActor0PrevY,playerActor0PrevZ,playerActor0VelX,playerActor0VelY,playerActor0VelZ,playerActor0PlayerID,playerActor0TransitionStep,playerActor0SignalLock,playerActor0Flag192,playerActor0Flags728,playerActor0Flags72C,playerActor0Flags730,playerActor0TransitFunc,playerActor0TransitArg,playerActor1Found,playerActor1Guid,playerActor1Base,playerActor1Settings,playerActor1StateType,playerActor1Flags,playerActor1X,playerActor1Y,playerActor1Z,playerActor1PrevX,playerActor1PrevY,playerActor1PrevZ,playerActor1VelX,playerActor1VelY,playerActor1VelZ,playerActor1PlayerID,playerActor1TransitionStep,playerActor1SignalLock,playerActor1Flag192,playerActor1Flags728,playerActor1Flags72C,playerActor1Flags730,playerActor1TransitFunc,playerActor1TransitArg,playerTransitionStatus0,playerTransitionStatus1,vsConnectFound,vsConnectBase,vsConnectWord078,vsConnectWord07C,vsConnectByte0E2,vsConnectByte106,vsConnectWord114,vsConnectWord118,vsConnectWord120,vsConnectWord128,vsConnectWord138,vsConnectWord13C,vsConnectWord140,vsConnectWord144,vsConnectWord148,vsConnectByte153,vsConnectByte154,vsConnectByte155,vsConnectByte156,vsConnectByte157,vsConnectByte158,vsConnectWord154,courseSelectFound,courseSelectBase,courseSelectSettings,courseSelectWord060,courseSelectWord064,courseSelectWord068,courseSelectWord06C,courseSelectWord070,courseSelectWord074,courseSelectWord078,courseSelectWord07C,courseSelectWord080,courseSelectWord084,courseSelectWord088,courseSelectWord08C,courseSelectWord090,stageCameraFound,stageCameraWord190,stageCameraWord194,stageCameraWord19C,stageCameraWord1A0,stageActorManagerFound,stageActorManagerBase,stageActorManagerStateType,stageControllerFound,stageControllerBase,stageControllerStateType,mvlObject267Found,mvlObject267Base,mvlObject267StateType,mvlGlobal965C,mvlGlobal9670,mvlGlobal9674,mvlGlobal9694_0,mvlGlobal9694_1,mvlStageLayoutGateCAC6C,mvlStageLayoutGateCAC74,mvlStageLayoutGateCAC7C,mvlStageLayoutGateCACDC,mvlStageLayoutGateCAE80,mvlStageLayoutGateCAE74,mvlStageLayoutGateCAEB8,mvlStageLayoutGateCAF20,mvlStageLayoutGateCAF40,mvlStageLayoutGateCA8C0,mvlStageLayoutGateCA8D0,mvlStageLayoutGateCAD30,mvlManagerBase,mvlManagerVTable,mvlManagerGuid,mvlManagerSettings,mvlManagerObjectId,mvlManagerStateType,mvlManagerFlags,mvlManagerUnk54,mvlManagerResourcesHeap,mvlManagerWordA8CC,mvlManagerWordA8D0,mvlManagerWordA8D4,mvlManagerWordA8D8,mvlManagerWordA8DC,mvlManagerWordA8E0,mvlManagerWordA8E4,mvlManagerHalfA8E8,mvlManagerHalfA8EA,mvlManagerByteA8EC,mvlManagerHalf494,mvlManagerHalf4A0,stageSceneFound,stageSceneBase,stageSceneSettings,stageSceneStateType,stageSceneFlags,stageSceneWord154,stageSceneWord160,stageSceneWord5618,stageSceneWord561C,stageSceneWord563C,stageSceneByte5643,stageSceneByte5644,stageSceneByte5645,stageSceneByte5646,stageSceneByte5648,stageSceneByte5649,stageSceneUpdateDispatchFunc,stageSceneUpdateDispatchArg,stageSceneRenderDispatchFunc,stageSceneRenderDispatchArg,stageSceneGlobal9280,stageSceneGlobal9284,stageSceneGlobal928C,stageSceneGlobal92B4,stageSceneGlobal92C0,stageSceneGlobal92C8,stageSceneGlobal92CC,stageSceneGlobal92D0,stageLiquidPlayerSlot,stageLiquidHeight0,stageLiquidHeight1,movingHazardFound,movingHazardGuid,movingHazardSettings,movingHazardStateType,movingHazardFlags,movingHazardX,movingHazardY,movingHazardZ,movingHazardVelX,movingHazardVelY,movingHazardLastStepX,movingHazardLastStepY,movingHazardLastStepZ,movingHazardVelH,movingHazardTargetVelH,movingHazardAccelV,movingHazardTargetVelV,movingHazardAccelH,movingHazardTargetVelX,movingHazardTargetVelY,movingHazardTargetVelZ,objectScanTotal,objectNotCreatedCount,objectActiveCount,objectDeadCount,objectSkipUpdateCount,objectSkipRenderCount,objectFirstNotCreatedId,objectFirstNotCreatedBase,objectFirstNotCreatedFlags,objectSecondNotCreatedId,objectSecondNotCreatedBase,objectSecondNotCreatedFlags,objectActiveId0,objectActiveSettings0,objectActiveBase0,objectActiveId1,objectActiveSettings1,objectActiveBase1,objectActiveId2,objectActiveSettings2,objectActiveBase2,objectActiveId3,objectActiveSettings3,objectActiveBase3,objectActiveId4,objectActiveSettings4,objectActiveBase4,objectActiveId5,objectActiveSettings5,objectActiveBase5,objectActiveId6,objectActiveSettings6,objectActiveBase6,objectActiveId7,objectActiveSettings7,objectActiveBase7,objectActiveId8,objectActiveSettings8,objectActiveBase8,objectActiveId9,objectActiveSettings9,objectActiveBase9,objectActiveId10,objectActiveSettings10,objectActiveBase10,objectActiveId11,objectActiveSettings11,objectActiveBase11,objectActiveId12,objectActiveSettings12,objectActiveBase12,objectActiveId13,objectActiveSettings13,objectActiveBase13,objectActiveId14,objectActiveSettings14,objectActiveBase14,objectActiveId15,objectActiveSettings15,objectActiveBase15";
            if (G.GameStateTraceExtended)
                G.GameStateTrace << ",playerCount,player0Powerup,player1Powerup,player0InventoryPowerup,player1InventoryPowerup,player0Dead,player1Dead,player0Character,player1Character,player0Lives,player1Lives,player0BattleStars,player1BattleStars,player0Coins,player1Coins,player0Score,player1Score,player0DisplayedStars,player1DisplayedStars,player0Deaths,player1Deaths,player0CollectedStars,player1CollectedStars,vsCoinCount,entranceSpawnID0,entranceSpawnID1,entranceTransitionFlags0,entranceTransitionFlags1,entranceSpawnPtr0,entranceSpawnPtr1,stageCameraBase,stageCameraTargetX,stageCameraTargetY,stageCameraTargetZ,stageCameraPositionX,stageCameraPositionY,stageCameraPositionZ,stageCameraUpX,stageCameraUpY,stageCameraUpZ,stageCameraUnk114,stageCameraUnk118,stageCameraUnk11C,stageCameraUnk128,stageCameraUnk12C,stageCameraRoll130,stageCameraGlobalX0,stageCameraGlobalX1,stageCameraGlobalY0,stageCameraGlobalY1,stageCameraGlobalWidth0,stageCameraGlobalWidth1,stageCameraGlobalHeight0,stageCameraGlobalHeight1,stageDisplayCameraX,playerGlobalHash,wifiCandidateHash,renderCandidateHash,netStateHash,playerActor0ActionFlag,playerActor0SubActionFlag,playerActor0PhysicsFlag,playerActor0DamageCooldown,playerActor1ActionFlag,playerActor1SubActionFlag,playerActor1PhysicsFlag,playerActor1DamageCooldown,playerActor0LinkedActor,playerActor0TransitionFlag,playerActor0CollisionFlag,playerActor0EnvironmentFlag,playerActor0UpdateLocked,playerActor0CharacterIDBase,playerActor0TransitioningFlag,playerActor0DefeatedFlag,playerActor0PlayerBaseID,playerActor0VisibleFlag,playerActor1LinkedActor,playerActor1TransitionFlag,playerActor1CollisionFlag,playerActor1EnvironmentFlag,playerActor1UpdateLocked,playerActor1CharacterIDBase,playerActor1TransitioningFlag,playerActor1DefeatedFlag,playerActor1PlayerBaseID,playerActor1VisibleFlag";
            G.GameStateTrace << '\n';
        }
    }

    if (G.TestEnabled)
    {
        if (!LoadInputScriptLocked())
            G.TestEnabled = false;
        if (!G.ScriptRemotePacketInputScriptPath.empty() &&
            !LoadInputScriptFileLocked(G.ScriptRemotePacketInputScriptPath, GScriptRemotePacketInputScript))
        {
            G.TestEnabled = false;
        }

        if (!G.HashLogPath.empty())
        {
            G.HashLog.open(G.HashLogPath, std::ios::out | std::ios::trunc);
            if (!G.HashLog)
            {
                std::printf("NSMB Test: failed to open hash log: %s\n", G.HashLogPath.c_str());
            }
            else
            {
                if (G.ScreenHashEnabled)
                    G.HashLog << "instance,frame,hash,screenHash\n";
                else
                    G.HashLog << "instance,frame,hash\n";
            }
        }

        std::printf("NSMB Test: enabled frames=%u instances=%d frameBarrier=%d serialRun=%d input=%s hashLog=%s interval=%d screenshotDir=%s screenshotInterval=%d ramDumpDir=%s ramDumpInterval=%d ramDumpRanges=%zu gameStateTrace=%s gameStateTraceInterval=%d stateSync=%d stateApply=%d stateSyncInterval=%d memPatchFile=%s memPatchFrame=%u memPatchRanges=%zu netRandomEnabled=%d netRandomAuto=%d netRandomFrame=%u netRandomValue=0x%08X stateSaveDir=%s stateSaveFrame=%u stateLoadDir=%s stateLoadFrame=%u waitTimeoutMs=%d quitGraceMs=%d inputTrace=%d inputTraceInterval=%d seedWaitMs=%d waitForPeer=%d waitForPeerAtStart=%d deferNetworkUntilStart=%d netplayFrameBarrier=%d packetBridge=%d packetBridgeOnly=%d packetBridgePreGame=%d packetBridgeTrace=%d packetBridgeWait=%d packetBridgeWaitMs=%d packetBridgeWaitStart=%u packetBridgeWaitAhead=%d packetBridgeDirect=%d packetBridgeForceTick=%d packetBridgeForceTickStart=%u packetBridgeMaxTickLead=%d packetBridgeMaxFrameLead=%d packetBridgeThrottleMs=%d packetBridgeThrottleStart=%u directBoot=%d directBootFrame=%u directBootScene=%d directBootStage=%d directBootPlayerID=%d directBootLoadSM=%d directBootPatchLoadSMOnly=%d directBootCallUpdateSM=%d\n",
            G.TestFrames,
            G.TestInstanceCount,
            G.FrameBarrierEnabled ? 1 : 0,
            G.SerialRunEnabled ? 1 : 0,
            G.InputScriptPath.empty() ? "<none>" : G.InputScriptPath.c_str(),
            G.HashLogPath.empty() ? "<none>" : G.HashLogPath.c_str(),
            G.HashInterval,
            G.ScreenshotDir.empty() ? "<none>" : G.ScreenshotDir.c_str(),
            G.ScreenshotInterval,
            G.RamDumpDir.empty() ? "<none>" : G.RamDumpDir.c_str(),
            G.RamDumpInterval,
            G.RamDumpRanges.size(),
            G.GameStateTracePath.empty() ? "<none>" : G.GameStateTracePath.c_str(),
            G.GameStateTraceInterval,
            G.GameStateSyncEnabled ? 1 : 0,
            G.GameStateApplyEnabled ? 1 : 0,
            G.GameStateSyncInterval,
            G.MemPatchFile.empty() ? "<none>" : G.MemPatchFile.c_str(),
            G.MemPatchFrameSet ? G.MemPatchFrame : 0,
            G.MemPatchRanges.size(),
            G.NetRandomPatchEnabled ? 1 : 0,
            G.NetRandomPatchAuto ? 1 : 0,
            G.NetRandomPatchFrame,
            G.NetRandomPatchValue,
            G.StateSaveDir.empty() ? "<none>" : G.StateSaveDir.c_str(),
            G.StateSaveFrame,
            G.StateLoadDir.empty() ? "<none>" : G.StateLoadDir.c_str(),
            G.StateLoadFrameSet ? G.StateLoadFrame : 0,
            G.TestWaitTimeoutMs,
            G.TestQuitGraceMs,
            G.InputTraceEnabled ? 1 : 0,
            G.InputTraceInterval,
            G.SeedWaitTimeoutMs,
            G.WaitForPeerBeforeStart ? 1 : 0,
            G.WaitForPeerAtNetplayStart ? 1 : 0,
            G.DeferNetworkUntilStart ? 1 : 0,
            G.NetplayFrameBarrierEnabled ? 1 : 0,
            G.PacketBridgeEnabled ? 1 : 0,
            G.PacketBridgeOnly ? 1 : 0,
            G.PacketBridgeAllowPreGame ? 1 : 0,
            G.PacketBridgeTraceEnabled ? 1 : 0,
            G.PacketBridgeWaitEnabled ? 1 : 0,
            G.PacketBridgeWaitTimeoutMs,
            G.PacketBridgeWaitStartFrame,
            G.PacketBridgeWaitTickAhead,
            G.PacketBridgeDirectCaptureEnabled ? 1 : 0,
            G.PacketBridgeForceTickEnabled ? 1 : 0,
            G.PacketBridgeForceTickStartFrame,
            G.PacketBridgeMaxTickLead,
            G.PacketBridgeMaxFrameLead,
            G.PacketBridgeThrottleTimeoutMs,
            G.PacketBridgeThrottleStartFrame,
            G.DirectMvlBootEnabled ? 1 : 0,
            G.DirectMvlBootFrame,
            G.DirectMvlBootScene,
            G.DirectMvlBootStage,
            G.DirectMvlBootPlayerID,
            G.DirectMvlBootUseLoadGameSM ? 1 : 0,
            G.DirectMvlBootPatchLoadGameSMOnly ? 1 : 0,
            G.DirectMvlBootCallUpdateLoadGameSM ? 1 : 0);
        std::fflush(stdout);
    }

    const char* role = std::getenv("MELONDS_NSML_ROLE");
    if (!role || !role[0])
        role = std::getenv("MELONDS_NSML_LAN_ROLE");
    G.NetRole = (role && std::strcmp(role, "client") == 0) ? Role::Client : Role::Host;

    if (!G.Enabled) return;

    G.Delay = std::max(0, EnvInt("MELONDS_NSML_DELAY", kDefaultDelay));
    G.NetplayWarmupFrames = std::max(0, EnvInt("MELONDS_NSML_NETPLAY_WARMUP_FRAMES", G.TestEnabled ? G.Delay * 2 : 0));
    G.Port = EnvInt("MELONDS_NSML_PORT", 8065);
    G.LocalInstance = EnvInt("MELONDS_NSML_LOCAL_INSTANCE", G.NetRole == Role::Host ? 0 : 1);
    G.NetplayStartFrame = static_cast<melonDS::u32>(std::max(0, EnvInt("MELONDS_NSML_NETPLAY_START_FRAME", 0)));
    G.LocalWaitsForRemote = !EnvFlag("MELONDS_NSML_NO_LOCAL_WAIT");
    G.RemoteInputTimeoutFatal = EnvFlag("MELONDS_NSML_REMOTE_INPUT_TIMEOUT_FATAL");

    const char* peer = std::getenv("MELONDS_NSML_PEER");
    if (peer && peer[0]) G.PeerHost = peer;

    if (G.NetRole == Role::Host && !G.MatchSeedConfigured)
    {
        G.MatchSeed = GenerateMatchSeed();
        G.MatchSeedConfigured = true;
    }

    if (G.NetRole == Role::Host
        && G.MatchSeedConfigured
        && G.StateLoadDir.empty()
        && !G.PacketBridgeOnly
        && !G.InputNetplayOnly)
    {
        G.NetRandomPatchEnabled = true;
        G.NetRandomPatchAuto = true;
        G.NetRandomPatchValue = G.MatchSeed;
    }

    if (enet_initialize() != 0)
    {
        std::printf("NSMB PoC: ENet initialization failed\n");
        G.Enabled = false;
        return;
    }
    G.ENetInitialized = true;

    if (G.NetRole == Role::Host)
    {
        ENetAddress address {};
        address.host = ENET_HOST_ANY;
        address.port = G.Port;
        G.Host = enet_host_create(&address, 1, 1, 0, 0);
    }
    else
    {
        G.Host = enet_host_create(nullptr, 1, 1, 0, 0);
        if (G.Host)
        {
            ENetAddress address {};
            enet_address_set_host(&address, G.PeerHost);
            address.port = G.Port;
            G.Peer = enet_host_connect(G.Host, &address, 1, 0);
        }
    }

    if (!G.Host)
    {
        std::printf("NSMB PoC: failed to create ENet host\n");
        G.Enabled = false;
        return;
    }

    G.Ready = true;
    StartNetworkPumpThreadIfNeeded();
    std::printf("NSMB PoC: enabled role=%s port=%d peer=%s delay=%d warmup=%d localInstance=%d netplayStartFrame=%u localWait=%d remoteTimeoutFatal=%d waitForPeer=%d waitForPeerAtStart=%d deferNetworkUntilStart=%d netplayFrameBarrier=%d packetBridge=%d packetBridgeOnly=%d packetBridgePreGame=%d packetBridgeTrace=%d packetBridgeWait=%d packetBridgeWaitMs=%d packetBridgeWaitStart=%u packetBridgeWaitAhead=%d packetBridgeDirect=%d packetBridgeForceTick=%d packetBridgeForceTickStart=%u packetBridgeMaxTickLead=%d packetBridgeMaxFrameLead=%d packetBridgeThrottleMs=%d packetBridgeThrottleStart=%u inputNetplayOnly=%d inputNetplayTrace=%d inputMaxFrameLead=%d inputUnreliable=%d inputBundleHistory=%d inputDropModulo=%d inputDropOffset=%d netPumpThread=%d netPumpSleepUs=%d inputWaitUs=%d rollback=%d rollbackBackend=%s rollbackWindow=%d rollbackCheckpointInterval=%d rollbackResimDelay=%d rollbackResimulate=%d rollbackRestoreProbe=%d matchSeed=0x%08X seedConfigured=%d directBoot=%d directBootFrame=%u directBootScene=%d directBootStage=%d directBootPlayerID=%d directBootLoadSM=%d directBootPatchLoadSMOnly=%d directBootCallUpdateSM=%d\n",
        G.NetRole == Role::Host ? "host" : "client",
        G.Port,
        G.PeerHost,
        G.Delay,
        G.NetplayWarmupFrames,
        G.LocalInstance,
        G.NetplayStartFrame,
        G.LocalWaitsForRemote ? 1 : 0,
        G.RemoteInputTimeoutFatal ? 1 : 0,
        G.WaitForPeerBeforeStart ? 1 : 0,
        G.WaitForPeerAtNetplayStart ? 1 : 0,
        G.DeferNetworkUntilStart ? 1 : 0,
        G.NetplayFrameBarrierEnabled ? 1 : 0,
        G.PacketBridgeEnabled ? 1 : 0,
        G.PacketBridgeOnly ? 1 : 0,
        G.PacketBridgeAllowPreGame ? 1 : 0,
        G.PacketBridgeTraceEnabled ? 1 : 0,
        G.PacketBridgeWaitEnabled ? 1 : 0,
        G.PacketBridgeWaitTimeoutMs,
        G.PacketBridgeWaitStartFrame,
        G.PacketBridgeWaitTickAhead,
        G.PacketBridgeDirectCaptureEnabled ? 1 : 0,
        G.PacketBridgeForceTickEnabled ? 1 : 0,
        G.PacketBridgeForceTickStartFrame,
        G.PacketBridgeMaxTickLead,
        G.PacketBridgeMaxFrameLead,
        G.PacketBridgeThrottleTimeoutMs,
        G.PacketBridgeThrottleStartFrame,
        G.InputNetplayOnly ? 1 : 0,
        G.InputNetplayTraceEnabled ? 1 : 0,
        G.InputNetplayMaxFrameLead,
        G.InputUnreliable ? 1 : 0,
        G.InputBundleHistory,
        G.InputDropModulo,
        G.InputDropOffset,
        G.NetworkPumpThreadEnabled ? 1 : 0,
        G.NetworkPumpSleepUs,
        G.InputWaitPollUs,
        G.RollbackEnabled ? 1 : 0,
        G.RollbackBackendMode == RollbackBackend::ARM9RAM ? "arm9ram" : "savestate",
        G.RollbackWindow,
        G.RollbackCheckpointInterval,
        G.RollbackResimulateDelayFrames,
        G.RollbackResimulate ? 1 : 0,
        G.RollbackRestoreProbe ? 1 : 0,
        G.MatchSeed,
        G.MatchSeedConfigured ? 1 : 0,
        G.DirectMvlBootEnabled ? 1 : 0,
        G.DirectMvlBootFrame,
        G.DirectMvlBootScene,
        G.DirectMvlBootStage,
        G.DirectMvlBootPlayerID,
        G.DirectMvlBootUseLoadGameSM ? 1 : 0,
        G.DirectMvlBootPatchLoadGameSMOnly ? 1 : 0,
        G.DirectMvlBootCallUpdateLoadGameSM ? 1 : 0);
    std::fflush(stdout);
}

InputState BeforeRunFrame(int instanceID, melonDS::u32 frame, melonDS::NDS* nds, const InputState& polledInput)
{
    InitFromEnvironment();
    melonDS::u32 inputFrame = frame;
    if (G.TestEnabled && instanceID >= 0 && instanceID < 16)
        inputFrame = G.TestFrameCount[instanceID];

    if (G.Enabled && G.InputNetplayOnly && G.WaitForPeerBeforeStart && inputFrame == 0)
    {
        WaitForPeerIfNeeded(true);
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            PumpNetworkLocked(nds, inputFrame);
            SendMatchSeedLocked();
        }
        WaitForMatchSeedIfNeeded();
    }

    if (G.TestEnabled && instanceID >= 0 && instanceID < 16 && nds)
        LoadState(instanceID, inputFrame, nds);

    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        RestoreRollbackCheckpointForProbeIfNeeded(instanceID, inputFrame, nds);

    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ApplyPacketBridgeJitHelperPatchIfNeeded(instanceID, inputFrame, nds);

    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        RollbackResimulateIfNeeded(instanceID, inputFrame, nds);

    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        InjectDirectMvlBootCall(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        InjectCourseSelectFactoryCall(instanceID, inputFrame, nds);

    if (G.TestEnabled && instanceID >= 0 && instanceID < 16 && nds)
        ApplyMemPatch(instanceID, inputFrame, nds);

    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ApplyNetRandomPatch(instanceID, inputFrame, nds);

    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
    {
        if (InjectNSMLPacketBridgeDummyAlloc(instanceID, inputFrame, nds))
            return polledInput;
        ForceNSMLPacketBridgeNetReadyIfNeeded(instanceID, inputFrame, nds);
        InjectNSMLPacketBridgeScheduledSubMenusIfNeeded(instanceID, inputFrame, nds);
        if (InjectNSMLPacketBridgeMvlLoadThread(instanceID, inputFrame, nds))
            return polledInput;
        ForceNSMLPacketBridgeStageStartSMFieldsIfNeeded(instanceID, inputFrame, nds);
        if (InjectNSMLPacketBridgeVSConnectOnUpdateIfNeeded(instanceID, inputFrame, nds))
            return polledInput;
        if (InjectNSMLPacketBridgeStageStartSMUpdateIfNeeded(instanceID, inputFrame, nds))
            return polledInput;
        ForceNSMLPacketBridgeLoadGameSMIfNeeded(instanceID, inputFrame, nds);
        ForceNSMLStagePacketWordsIfNeeded(inputFrame, nds);
        ForceNSMLGameLocalPlayerIDIfNeeded(inputFrame, nds);
    }

    if (G.TestEnabled && instanceID >= 0 && instanceID < 16 && nds)
        ApplyVsStarSnap(instanceID, inputFrame, nds);

    if (G.TestEnabled && instanceID >= 0 && instanceID < 16 && nds)
        ApplyPlayerSnapToStar(instanceID, inputFrame, nds);

    if (G.TestEnabled && instanceID >= 0 && instanceID < 16 && nds)
        ApplyPlayerStickToStar(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForcePlayerCountIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForceStageSceneRuntimeWordsIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForceStageSceneActiveIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
    {
        if (CallStageScenePostCreateIfNeeded(instanceID, inputFrame, nds))
            return polledInput;
    }
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForceStageSceneStartGateIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForceWifiCommunicatingIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForceNetLocalAidIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        PushScriptRemotePacketIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForceStageSceneContinueGateIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForceMvlPlayerReadyIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForceMvlRuntimeStateIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForcePlayerTransitionStatusIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForceEntranceSpawnPointersIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForcePlayerActorIDsIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForcePlayerActorPositionIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
    {
        if (CallMvlStageLayoutInitIfNeeded(instanceID, inputFrame, nds))
            return polledInput;
    }
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForceMvlStageLayoutBufferIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForceMvlStageLayoutGateIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForceStageSceneEventFlagsIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForceStageCameraSlotIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForceStageCameraObjectXIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForceStageSceneState3GateIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForceStageActorFreezeFlagIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForcePlayerDeathCountersIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForcePlayerInventoryPowerupsIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForcePlayerStarCountersIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForceStageFXSettingsIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForceStageActorPreUpdateGateIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForceActorCategoryMaskIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForcePlayerSignalUnlockIfNeeded(instanceID, inputFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForcePlayerUpdateEnableIfNeeded(instanceID, inputFrame, nds);

    if (G.Enabled && instanceID >= 0 && instanceID < 16 && nds)
        ApplyRemoteGameState(instanceID, inputFrame, nds);

    WaitForSerialRunTurn(instanceID, inputFrame);
    WaitAtFrameBarrier(GBeforeFrameBarrier, instanceID, inputFrame, "before");

    const InputState testInput = ApplyInputScript(instanceID, inputFrame, polledInput);
    const melonDS::u32 syncFrame = G.TestEnabled ? inputFrame : frame;

    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        SaveRollbackCheckpointIfNeeded(instanceID, syncFrame, nds);

    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        WritePacketBridgeJitScratchIfNeeded(instanceID, syncFrame, nds, testInput);

    if (G.Enabled && G.InputNetplayOnly)
        return testInput;

    if (!G.Enabled || !G.Ready) return testInput;
    if (syncFrame == 0 && G.PacketBridgeOnly)
    {
        WaitForPeerIfNeeded();
    }
    else if (syncFrame == 0)
    {
        WaitForPeerIfNeeded();
        WaitForMatchSeedIfNeeded();
    }

    if (G.PacketBridgeEnabled && G.PacketBridgeOnly)
    {
        const bool bridgeNetworkActive =
            !G.DeferNetworkUntilStart || G.NetplayStartFrame == 0 || syncFrame >= G.NetplayStartFrame;
        InputState packetBridgeInput = testInput;
        if (bridgeNetworkActive && G.PacketBridgeLocalInputDelay > 0)
        {
            const melonDS::u32 inputDelay = static_cast<melonDS::u32>(G.PacketBridgeLocalInputDelay);
            std::lock_guard<std::mutex> lock(G.Mutex);
            G.LocalInputs.emplace(syncFrame + inputDelay, testInput);
            auto delayed = G.LocalInputs.find(syncFrame);
            packetBridgeInput = delayed != G.LocalInputs.end() ? delayed->second : NeutralInput();

            const melonDS::u32 keepFrom = syncFrame > 180 ? syncFrame - 180 : 0;
            for (auto it = G.LocalInputs.begin(); it != G.LocalInputs.end(); )
            {
                if (it->first < keepFrom)
                    it = G.LocalInputs.erase(it);
                else
                    ++it;
            }
        }
        if (bridgeNetworkActive && G.PacketBridgeNeutralizeLocalInput)
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            G.PacketBridgePacketInputs[syncFrame] = testInput;
            packetBridgeInput = G.PacketBridgePreserveLocalTouch
                ? NeutralInputPreservingTouch(testInput)
                : NeutralInput();
        }
        if (bridgeNetworkActive)
        {
            {
                std::lock_guard<std::mutex> lock(G.Mutex);
                PumpNSMLPacketBridgeLocked(nds, syncFrame);
                ForceNSMLPacketBridgeTickIfNeeded(instanceID, syncFrame, nds);
            }
            ForceNSMLPacketBridgeNetReadyIfNeeded(instanceID, syncFrame, nds);
            if (InjectNSMLPacketBridgeDummyAlloc(instanceID, syncFrame, nds))
                return testInput;
            InjectNSMLPacketBridgeScheduledSubMenusIfNeeded(instanceID, syncFrame, nds);
            if (InjectNSMLPacketBridgeMvlLoadThread(instanceID, syncFrame, nds))
                return testInput;
            ForceNSMLPacketBridgeStageStartSMFieldsIfNeeded(instanceID, syncFrame, nds);
            if (InjectNSMLPacketBridgeVSConnectOnUpdateIfNeeded(instanceID, syncFrame, nds))
                return testInput;
            if (InjectNSMLPacketBridgeStageStartSMUpdateIfNeeded(instanceID, syncFrame, nds))
                return testInput;
            ForceNSMLPacketBridgeLoadGameSMIfNeeded(instanceID, syncFrame, nds);
            ForceNSMLStagePacketWordsIfNeeded(syncFrame, nds);
            ForceNSMLGameLocalPlayerIDIfNeeded(syncFrame, nds);
            melonDS::NSML_RefreshMarioVsLuigiPacketSlots(nds);
            ForceNSMLStagePacketWordsIfNeeded(syncFrame, nds);
            ForceNSMLGameLocalPlayerIDIfNeeded(syncFrame, nds);
            ThrottleNSMLPacketBridgeLead(nds, syncFrame);
            WaitForNSMLPacketBridgeRemote(nds, syncFrame);
        }
        return packetBridgeInput;
    }

    const bool isLocal = (instanceID == G.LocalInstance);
    const melonDS::u32 delay = static_cast<melonDS::u32>(G.Delay);
    const melonDS::u32 sendStartFrame = (G.NetplayStartFrame > delay)
        ? G.NetplayStartFrame - delay
        : 0;
    const bool netplaySendActive = (G.NetplayStartFrame == 0 || syncFrame >= sendStartFrame);
    const bool netplayApplyActive = (G.NetplayStartFrame == 0 || syncFrame >= G.NetplayStartFrame);
    const bool networkPumpActive = ShouldPumpNetworkAtFrame(syncFrame, sendStartFrame);

    if ((isLocal || G.TestEnabled) && networkPumpActive)
    {
        InputState localInput = testInput;
        if (!isLocal && G.TestEnabled)
            localInput = ApplyInputScript(G.LocalInstance, syncFrame, NeutralInput());

        std::lock_guard<std::mutex> lock(G.Mutex);
        PumpNetworkLocked(nds, syncFrame);
        ApplyPendingNSMLPacketsLocked(nds);
        SendMatchSeedLocked();
        if (netplaySendActive)
        {
            const melonDS::u32 effectiveFrame = syncFrame + delay;
            G.LocalInputs.emplace(effectiveFrame, localInput);
            for (const auto& [storedFrame, input] : G.LocalInputs)
                SendInputLocked(storedFrame, input);
        }
    }

    if (!netplayApplyActive)
        return testInput;

    if (G.NetplayStartFrame != 0
        && G.NetplayWarmupFrames > 0
        && syncFrame < G.NetplayStartFrame + static_cast<melonDS::u32>(G.NetplayWarmupFrames))
    {
        return testInput;
    }

    const melonDS::u32 targetFrame = syncFrame;

    if (G.NetplayFrameBarrierEnabled)
        WaitAtFrameBarrier(GNetplayFrameBarrier, instanceID, targetFrame, "netplay");

    if (G.TestEnabled && instanceID >= 0 && instanceID < 16 && !G.NetplayLockstepStarted[instanceID])
    {
        bool needsInitialRemoteInput = false;
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            PumpNetworkLocked(nds, targetFrame);
            ApplyPendingNSMLPacketsLocked(nds);
            needsInitialRemoteInput =
                !G.NetplayAnyLockstepStarted && G.RemoteInputs.find(targetFrame) == G.RemoteInputs.end();
        }

        if (needsInitialRemoteInput)
            (void)WaitForRemoteInput(targetFrame);

        std::lock_guard<std::mutex> lock(G.Mutex);

        G.NetplayLockstepStarted[instanceID] = true;
        G.NetplayAnyLockstepStarted = true;
        std::printf("NSMB PoC: lockstep started inst=%d frame=%u\n", instanceID, targetFrame);
    }

    if (isLocal)
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        if (targetFrame > 120)
            PruneInputHistoryLocked(targetFrame - 120);

        auto it = G.LocalInputs.find(targetFrame);
        const InputState delayedLocalInput = it != G.LocalInputs.end() ? it->second : NeutralInput();
        if (!G.LocalWaitsForRemote)
            return delayedLocalInput;
        if (IsPastTestInputRange(targetFrame))
            return delayedLocalInput;
    }
    else if (IsPastTestInputRange(targetFrame))
    {
        return NeutralInput();
    }

    const InputState remoteInput = WaitForRemoteInput(targetFrame);

    if (isLocal)
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        auto it = G.LocalInputs.find(targetFrame);
        return it != G.LocalInputs.end() ? it->second : NeutralInput();
    }

    return remoteInput;
}

void TracePlayerLifeChanges(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.TracePlayerLifeChanges || !nds || !nds->MainRAM) return;
    if (instanceID < 0 || instanceID >= 16) return;

    GameStateSample& last = G.LastPlayerLifeSample[instanceID];
    const bool valid = G.LastPlayerLifeSampleValid[instanceID];
    const melonDS::u32 player0Lives = nds->ARM9Read32(kGamePlayerLivesAddr);
    const melonDS::u32 player1Lives = nds->ARM9Read32(kGamePlayerLivesAddr + sizeof(melonDS::u32));
    const melonDS::u32 player0Deaths = nds->ARM9Read32(kGamePlayerDeathsAddr);
    const melonDS::u32 player1Deaths = nds->ARM9Read32(kGamePlayerDeathsAddr + sizeof(melonDS::u32));
    const melonDS::u32 player0Dead = nds->ARM9Read8(kGamePlayerDeadAddr);
    const melonDS::u32 player1Dead = nds->ARM9Read8(kGamePlayerDeadAddr + 1);
    const melonDS::u32 transition0 = nds->ARM9Read32(kGamePlayerTransitionStatusAddr);
    const melonDS::u32 transition1 = nds->ARM9Read32(kGamePlayerTransitionStatusAddr + sizeof(melonDS::u32));
    const bool changed =
        !valid ||
        player0Lives != last.Player0Lives ||
        player1Lives != last.Player1Lives ||
        player0Deaths != last.Player0Deaths ||
        player1Deaths != last.Player1Deaths ||
        player0Dead != last.Player0Dead ||
        player1Dead != last.Player1Dead ||
        transition0 != last.PlayerTransitionStatus0 ||
        transition1 != last.PlayerTransitionStatus1;

    last.Player0Lives = player0Lives;
    last.Player1Lives = player1Lives;
    last.Player0Deaths = player0Deaths;
    last.Player1Deaths = player1Deaths;
    last.Player0Dead = player0Dead;
    last.Player1Dead = player1Dead;
    last.PlayerTransitionStatus0 = transition0;
    last.PlayerTransitionStatus1 = transition1;

    if (changed)
    {
        if (!IsMarioVsLuigiGameplay(nds) && frame < 800)
        {
            G.LastPlayerLifeSampleValid[instanceID] = true;
            return;
        }

        const GameStateSample sample = ReadGameStateSample(nds);
        std::printf(
            "NSMB LifeDelta: inst=%d frame=%u lives=%u/%u deaths=%u/%u dead=%u/%u trans=%u/%u "
            "cam={x=%08X/%08X y=%08X/%08X w=%08X/%08X h=%08X/%08X} "
            "p0={found=%u base=%08X pid11E=%u pid7B4=%u def=%u tring=%u updLock=%u vis=%u x=%08X y=%08X vel=%08X/%08X flags=%08X act=%08X sub=%08X phy=%08X transFlag=%08X coll=%08X env=%08X linked=%08X transitFunc=%08X transitArg=%08X} "
            "p1={found=%u base=%08X pid11E=%u pid7B4=%u def=%u tring=%u updLock=%u vis=%u x=%08X y=%08X vel=%08X/%08X flags=%08X act=%08X sub=%08X phy=%08X transFlag=%08X coll=%08X env=%08X linked=%08X transitFunc=%08X transitArg=%08X}\n",
            instanceID,
            frame,
            sample.Player0Lives,
            sample.Player1Lives,
            sample.Player0Deaths,
            sample.Player1Deaths,
            sample.Player0Dead,
            sample.Player1Dead,
            sample.PlayerTransitionStatus0,
            sample.PlayerTransitionStatus1,
            sample.StageCameraGlobalX0,
            sample.StageCameraGlobalX1,
            sample.StageCameraGlobalY0,
            sample.StageCameraGlobalY1,
            sample.StageCameraGlobalWidth0,
            sample.StageCameraGlobalWidth1,
            sample.StageCameraGlobalHeight0,
            sample.StageCameraGlobalHeight1,
            sample.PlayerActor0Found,
            sample.PlayerActor0Base,
            sample.PlayerActor0PlayerID,
            sample.PlayerActor0PlayerBaseID,
            sample.PlayerActor0DefeatedFlag,
            sample.PlayerActor0TransitioningFlag,
            sample.PlayerActor0UpdateLocked,
            sample.PlayerActor0VisibleFlag,
            sample.PlayerActor0PosX,
            sample.PlayerActor0PosY,
            sample.PlayerActor0VelX,
            sample.PlayerActor0VelY,
            sample.PlayerActor0Flags,
            sample.PlayerActor0ActionFlag,
            sample.PlayerActor0SubActionFlag,
            sample.PlayerActor0PhysicsFlag,
            sample.PlayerActor0TransitionFlag,
            sample.PlayerActor0CollisionFlag,
            sample.PlayerActor0EnvironmentFlag,
            sample.PlayerActor0LinkedActor,
            sample.PlayerActor0TransitFunc,
            sample.PlayerActor0TransitArg,
            sample.PlayerActor1Found,
            sample.PlayerActor1Base,
            sample.PlayerActor1PlayerID,
            sample.PlayerActor1PlayerBaseID,
            sample.PlayerActor1DefeatedFlag,
            sample.PlayerActor1TransitioningFlag,
            sample.PlayerActor1UpdateLocked,
            sample.PlayerActor1VisibleFlag,
            sample.PlayerActor1PosX,
            sample.PlayerActor1PosY,
            sample.PlayerActor1VelX,
            sample.PlayerActor1VelY,
            sample.PlayerActor1Flags,
            sample.PlayerActor1ActionFlag,
            sample.PlayerActor1SubActionFlag,
            sample.PlayerActor1PhysicsFlag,
            sample.PlayerActor1TransitionFlag,
            sample.PlayerActor1CollisionFlag,
            sample.PlayerActor1EnvironmentFlag,
            sample.PlayerActor1LinkedActor,
            sample.PlayerActor1TransitFunc,
            sample.PlayerActor1TransitArg);
    }
    G.LastPlayerLifeSampleValid[instanceID] = true;
}

void AfterRunFrame(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    InitFromEnvironment();
    if ((!G.Enabled && !G.TestEnabled) || !nds) return;

    if (instanceID < 0 || instanceID >= 16) return;

    melonDS::u32 logFrame = frame;
    if (G.TestEnabled)
    {
        logFrame = ++G.TestFrameCount[instanceID];
        if (logFrame == 1)
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            if (!G.TestTimerStarted)
            {
                G.TestTimerStarted = true;
                G.TestTimerStart = std::chrono::steady_clock::now();
            }
        }
        const melonDS::u32 activeStartFrame = G.ActiveFpsStartFrame != 0
            ? G.ActiveFpsStartFrame
            : (G.NetplayStartFrame != 0
                ? G.NetplayStartFrame + 120
                : 120);
        if (!G.ActiveTimerStarted[instanceID] && logFrame >= activeStartFrame)
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            if (!G.ActiveTimerStarted[instanceID])
            {
                G.ActiveTimerStarted[instanceID] = true;
                G.ActiveTimerStartFrame[instanceID] = logFrame;
                G.ActiveTimerStart[instanceID] = std::chrono::steady_clock::now();
            }
        }
    }

    WaitAtFrameBarrier(GAfterFrameBarrier, instanceID, logFrame, "after");
    AdvanceSerialRunTurn(instanceID, logFrame - 1);
    WaitForPeerAtNetplayStartBarrier(instanceID, logFrame);

    if (G.Enabled)
        ApplyRemoteGameState(instanceID, logFrame, nds);

    const bool bridgeNetworkActive =
        !G.DeferNetworkUntilStart || G.NetplayStartFrame == 0 || logFrame >= G.NetplayStartFrame;
    if (G.Enabled && G.PacketBridgeEnabled && bridgeNetworkActive)
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        PumpNSMLPacketBridgeLocked(nds, logFrame);
        ForceNSMLStagePacketWordsIfNeeded(logFrame, nds);
        ForceNSMLGameLocalPlayerIDIfNeeded(logFrame, nds);
        CaptureAndSendNSMLPacketLocked(logFrame, nds);
    }
    if (G.Enabled && G.PacketBridgeEnabled && bridgeNetworkActive)
    {
        ForceNSMLStagePacketWordsIfNeeded(logFrame, nds);
        ForceNSMLGameLocalPlayerIDIfNeeded(logFrame, nds);
        melonDS::NSML_RefreshMarioVsLuigiPacketSlots(nds);
        ForceNSMLStagePacketWordsIfNeeded(logFrame, nds);
        ForceNSMLGameLocalPlayerIDIfNeeded(logFrame, nds);
    }
    if (G.Enabled && G.PacketBridgeEnabled && bridgeNetworkActive)
        ThrottleNSMLPacketBridgeFrameLead(nds, logFrame);

    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        TracePlayerLifeChanges(instanceID, logFrame, nds);

    if (G.RollbackEnabled
        && G.InputNetplayTraceEnabled
        && logFrame != G.LastRollbackTraceFrame
        && (logFrame % 120) == 0)
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        G.LastRollbackTraceFrame = logFrame;
        std::printf(
            "NSMB Rollback: frame=%u checkpoints=%zu checkpointSaves=%u predicted=%zu predictions=%u mismatches=%u restores=%u resims=%u pending=%u observed=%u\n",
            logFrame,
            G.RollbackStates.size(),
            G.RollbackCheckpointSaveCount,
            G.PredictedRemoteInputs.size(),
            G.RollbackPredictionCount,
            G.RollbackMismatchCount,
            G.RollbackRestoreCount,
            G.RollbackResimulateCount,
            G.PendingRollbackFrame,
            G.PendingRollbackObservedFrame);
        std::fflush(stdout);
    }

    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForceStageCameraSlotIfNeeded(instanceID, logFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForceStageCameraObjectXIfNeeded(instanceID, logFrame, nds);

    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForceStageFXSettingsIfNeeded(instanceID, logFrame, nds);

    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForcePlayerDeathCountersIfNeeded(instanceID, logFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForcePlayerActorPositionIfNeeded(instanceID, logFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForcePlayerInventoryPowerupsIfNeeded(instanceID, logFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForcePlayerStarCountersIfNeeded(instanceID, logFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForceWifiCommunicatingIfNeeded(instanceID, logFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ForceNetLocalAidIfNeeded(instanceID, logFrame, nds);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        PushScriptRemotePacketIfNeeded(instanceID, logFrame, nds);

    SaveState(instanceID, logFrame, nds);
    SaveLocalMPState(logFrame);
    SaveScreenshot(instanceID, logFrame, nds);
    SaveRamDump(instanceID, logFrame, nds);
    TraceGameState(instanceID, logFrame, nds);
    SyncGameState(instanceID, logFrame, nds);

    if (!G.HashEnabled) return;
    if ((logFrame % static_cast<melonDS::u32>(G.HashInterval)) != 0) return;

    const melonDS::u64 hash = HashNDS(nds);
    const melonDS::u64 screenHash = G.ScreenHashEnabled ? HashFramebuffers(nds) : 0;
    if (G.LastLoggedHashFrame[instanceID] == logFrame) return;
    G.LastLoggedHashFrame[instanceID] = logFrame;

    if (G.ScreenHashEnabled)
    {
        std::printf("NSMB PoC: inst=%d frame=%u hash=%016llX screen=%016llX\n",
            instanceID,
            logFrame,
            static_cast<unsigned long long>(hash),
            static_cast<unsigned long long>(screenHash));
    }
    else
    {
        std::printf("NSMB PoC: inst=%d frame=%u hash=%016llX\n",
            instanceID,
            logFrame,
            static_cast<unsigned long long>(hash));
    }

    std::lock_guard<std::mutex> lock(G.Mutex);
    if (G.HashLog)
    {
        G.HashLog << instanceID << ',' << logFrame << ','
                  << std::hex << hash;
        if (G.ScreenHashEnabled)
            G.HashLog << ',' << screenHash;
        G.HashLog << std::dec << '\n';
        G.HashLog.flush();
    }
}

bool ShouldQuitAfterFrame(int instanceID, melonDS::u32 frame)
{
    InitFromEnvironment();
    if (!G.TestEnabled || G.TestFrames == kNoFrameLimit) return false;
    if (instanceID != G.TestInstanceCount - 1) return false;
    if (G.TestFrameCount[instanceID] < G.TestFrames) return false;

    std::lock_guard<std::mutex> lock(G.Mutex);
    for (int i = 0; i < G.TestInstanceCount; i++)
    {
        if (G.TestFrameCount[i] < G.TestFrames)
            return false;
    }

    if (!G.TestAnnouncedQuit)
    {
        G.TestAnnouncedQuit = true;
        const auto elapsedMs = G.TestTimerStarted
            ? std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - G.TestTimerStart).count()
            : 0;
        const double fps = elapsedMs > 0
            ? (static_cast<double>(G.TestFrames) * 1000.0) / static_cast<double>(elapsedMs)
            : 0.0;
        std::printf("NSMB Test: frame limit reached at frame=%u instances=%d elapsedMs=%lld fps=%.2f\n",
            G.TestFrames,
            G.TestInstanceCount,
            static_cast<long long>(elapsedMs),
            fps);
        if (G.ActiveTimerStarted[instanceID] && G.TestFrames > G.ActiveTimerStartFrame[instanceID])
        {
            const auto activeElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - G.ActiveTimerStart[instanceID]).count();
            const melonDS::u32 activeFrames = G.TestFrames - G.ActiveTimerStartFrame[instanceID];
            const double activeFps = activeElapsedMs > 0
                ? (static_cast<double>(activeFrames) * 1000.0) / static_cast<double>(activeElapsedMs)
                : 0.0;
            std::printf("NSMB Test: active fps startFrame=%u frames=%u elapsedMs=%lld fps=%.2f\n",
                G.ActiveTimerStartFrame[instanceID],
                activeFrames,
                static_cast<long long>(activeElapsedMs),
                activeFps);
        }
        if (G.Enabled && G.InputNetplayOnly)
        {
            const double remoteAvgUs = G.RemoteInputWaitCount > 0
                ? static_cast<double>(G.RemoteInputWaitUs) / static_cast<double>(G.RemoteInputWaitCount)
                : 0.0;
            const double throttleAvgUs = G.FrameLeadThrottleCount > 0
                ? static_cast<double>(G.FrameLeadThrottleUs) / static_cast<double>(G.FrameLeadThrottleCount)
                : 0.0;
            std::printf(
                "NSMB Test: input wait stats remoteWaitCount=%llu remoteWaitAvgUs=%.1f remoteWaitMaxUs=%llu remoteWaitLoops=%llu throttleCount=%llu throttleAvgUs=%.1f throttleMaxUs=%llu throttleLoops=%llu\n",
                G.RemoteInputWaitCount,
                remoteAvgUs,
                G.RemoteInputWaitMaxUs,
                G.RemoteInputWaitLoops,
                G.FrameLeadThrottleCount,
                throttleAvgUs,
                G.FrameLeadThrottleMaxUs,
                G.FrameLeadThrottleLoops);
        }
        std::fflush(nullptr);
        if (G.Enabled && G.TestQuitGraceMs > 0)
        {
            const auto end = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(G.TestQuitGraceMs);
            while (std::chrono::steady_clock::now() < end)
            {
                PumpNetworkLocked();
                if (G.Host)
                    enet_host_flush(G.Host);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        std::_Exit(0);
    }
    return true;
}

void Shutdown()
{
    StopNetworkPumpThread();

    std::lock_guard<std::mutex> lock(G.Mutex);

    if (G.Host)
    {
        enet_host_destroy(G.Host);
        G.Host = nullptr;
        G.Peer = nullptr;
    }

    if (G.ENetInitialized)
    {
        enet_deinitialize();
        G.ENetInitialized = false;
    }

    if (G.HashLog)
        G.HashLog.close();

    if (G.GameStateTrace)
        G.GameStateTrace.close();
}

}
