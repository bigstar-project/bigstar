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
#include "NsmbNetplayConfig.h"
#include "NsmbInputDelivery.h"
#include "NsmbInputProtocol.h"
#include "NsmbNetplayProtocol.h"
#include "NsmbNetplayTransport.h"
#include "NsmbGameState.h"
#include "NsmbGameStateReader.h"
#include "NsmbRollbackStore.h"
#include "NsmbInputTimeline.h"
#include "NsmbImitationAI.h"
#include "NsmbRuleAI.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <filesystem>
#include <iterator>
#include <limits>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <QImage>
#include <QString>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#endif

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
using WireProtocol::WireGameState;
using WireProtocol::WireMovingHazardState;
using WireProtocol::WireNSMLPacket;
using WireProtocol::WirePlayerState;
using WireProtocol::WireWorldActorSnapshotState;
using WireProtocol::WireWorldActorState;
using WireProtocol::WireWorldEffectSlot;
using WireProtocol::WireWorldEffectState;
using WireProtocol::WireWorldObjectActorState;
using WireProtocol::WireWorldState;
using WireProtocol::kMaxWorldActorSnapshots;
using WireProtocol::kMaxWorldEffects;
using WireProtocol::kMaxWorldMovingHazards;
using WireProtocol::kWireKindMovingHazardState;
using WireProtocol::kWireKindPacket;
using WireProtocol::kWireKindPlayerState;
using WireProtocol::kWireKindState;
using WireProtocol::kWireKindWorldActorSnapshot;
using WireProtocol::kWireKindWorldEffectState;
using WireProtocol::kWireKindWorldState;
using WireProtocol::kWorldEffectWordCount;
using GameStateModel::AITerrainDerivedSummary;
using GameStateModel::AITileGridSample;
using GameStateModel::AITileProbeSample;
using GameStateModel::AIPlayerTileProbeSample;
using GameStateModel::GameStateSample;
using GameStateModel::GameStateSyncHashes;
using GameStateModel::GameStateTraceHashes;
using GameStateModel::CombinedGameStateHash;
using GameStateModel::ComputeBasicGameStateHash;
using GameStateModel::DecodedGameState;
using GameStateModel::DecodeWireGameState;
using GameStateModel::EncodeWireGameState;
using GameStateModel::WriteGameStateTraceRow;
using GameStateModel::WriteGameStateTraceHeader;
using GameStateModel::PlayerCollisionMgrSample;
using GameStateModel::PlayerHitboxSample;
using GameStateModel::kAITileGridCount;
using GameStateModel::kAITileGridHeight;
using GameStateModel::kAITileGridWidth;
using GameStateModel::kAITileProbeCount;
using GameStateModel::kAIFireballSlotCount;
using GameStateModel::kAIFireballSlotDebugWordCount;
using GameStateModel::kAIFireballSlotStateByteCount;
using GameStateModel::kAISpecialHandlerWordCount;
using GameStateModel::kObjectTraceSlots;
using GameStateReader::ObjectPairScanSample;
using GameStateReader::ObjectScanSample;
using GameStateReader::PlayerActorScanSample;
using GameStateReader::ObjectLifecycleSummary;
using GameStateReader::GameStateObjectScanCache;
using GameStateReader::GameStateObjectScanEntry;
using GameStateReader::ScopedGameStateObjectScanCache;
using GameStateReader::BuildGameStateObjectScanCache;
using GameStateReader::FindActiveObjectsByIDAndSettings;
using GameStateReader::FindCachedObjectBaseByID;
using GameStateReader::FindNewestActiveObjectByIDAndSettings;
using GameStateReader::FindObjectByID;
using GameStateReader::FindObjectBaseByID;
using GameStateReader::FindObjectByIDAndSettings;
using GameStateReader::FindObjectByIDAndSettingsLoose;
using GameStateReader::FindObjectPairByIDSortedX;
using GameStateReader::FindPlayerActors;
using GameStateReader::FindVsBattleStarCandidate;
using GameStateReader::HasActiveObjectScanCache;
using GameStateReader::ReadObjectByBase;
using GameStateReader::ReadPlayerActorByBase;
using GameStateReader::SummarizeObjectLifecycle;
using RollbackStoredState = RollbackStorage::StoredState;
using RollbackStorage::CheckpointBytes;
constexpr melonDS::u32 kNoFrameLimit = 0;
constexpr melonDS::u32 kMainRAMBase = 0x02000000;
constexpr melonDS::u32 kGameStageIDAddr = 0x02085A14;
constexpr melonDS::u32 kGameStageGroupAddr = 0x02085A18;
constexpr melonDS::u32 kGameLocalPlayerIDAddr = 0x02085A7C;
constexpr melonDS::u32 kGameVsModeAddr = 0x02085A84;
constexpr melonDS::u32 kGameWrapXAddr = 0x02085AA4;
constexpr melonDS::u32 kNetStateBaseAddr = 0x020887E8;
constexpr melonDS::u32 kNetLocalAidAddr = 0x020887F0;
constexpr melonDS::u32 kNetState14Addr = 0x020887FC; // Net::connectionState
constexpr melonDS::u32 kNetState1CAddr = 0x02088804; // Net::connectedConsoleCount
constexpr melonDS::u32 kNetState20Addr = 0x02088808;
constexpr melonDS::u32 kNetState24Addr = 0x0208880C; // Net::expectedConsoleCount

unsigned long long NowUnixMs()
{
    return static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}
constexpr melonDS::u32 kNetState5CAddr = 0x0208883C; // Net::errorState
constexpr melonDS::u32 kNetGGIDAddr = 0x02088858;
constexpr melonDS::u32 kNetRandomBranchAddressAddr = 0x0208885C;
constexpr melonDS::u32 kNetPacketTickAddr = 0x020888E0;
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
constexpr melonDS::u32 kGameRandomCallCountAddr = 0x02085A54;
constexpr melonDS::u32 kGameRandomValueAddr = 0x02085A70;
constexpr melonDS::u32 kInputConsoleKeysAddr = 0x02087650;
constexpr melonDS::u32 kInputPlayerKeysHeldAddr = 0x02087660;
constexpr melonDS::u32 kInputKeyXMask = 1u << 10;
constexpr melonDS::u16 kMvlStockItemTouchX = 217;
constexpr melonDS::u16 kMvlStockItemTouchY = 153;
constexpr melonDS::u32 kStageActorFreezeFlagAddr = 0x020CA28C;
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
constexpr melonDS::u16 kPlayerObjectID = 0x0015;
constexpr melonDS::u32 kPlayerBaseActionFlagOffset = 0x778;
constexpr melonDS::u32 kPlayerBaseSubActionFlagOffset = 0x77C;
constexpr melonDS::u32 kPlayerBasePhysicsFlagOffset = 0x780;
constexpr melonDS::u32 kPlayerBaseTransitionFlagOffset = 0x784;
constexpr melonDS::u32 kPlayerBaseCollisionFlagOffset = 0x788;
constexpr melonDS::u32 kPlayerBaseEnvironmentFlagOffset = 0x790;
constexpr melonDS::u32 kPlayerBaseDamageCooldownOffset = 0x79C;
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
constexpr melonDS::u32 kPlayerBaseUpdateLockedOffset = 0x7A8;
constexpr melonDS::u32 kPlayerActorPlayerIDOffset = 0x11E;
constexpr melonDS::u32 kPlayerBaseCharacterIDOffset = 0x7AA;
constexpr melonDS::u32 kPlayerBasePowerupStateOffset = 0x7AB;
constexpr melonDS::u32 kPlayerBasePowerupFormStateOffset = 0x7AC;
constexpr melonDS::u32 kPlayerBasePowerupSubStateOffset = 0x7AD;
constexpr melonDS::u32 kPlayerBaseTransitioningFlagOffset = 0x7B0;
constexpr melonDS::u32 kPlayerBaseCameraFocusModeOffset = 0x7B2;
constexpr melonDS::u32 kPlayerBaseDefeatedFlagOffset = 0x7B3;
constexpr melonDS::u32 kPlayerBasePlayerIDOffset = 0x7B4;
constexpr melonDS::u32 kPlayerBaseVisibleFlagOffset = 0x7B5;
constexpr melonDS::u32 kPlayerBaseTransitionStepOffset = 0xBAD;
constexpr melonDS::u32 kPlayerBaseLinkedActorOffset = 0x688;
constexpr melonDS::u16 kVsBattleStarActorObjectID = 0x0022;
constexpr melonDS::u32 kVsBattleStarActorSettings = 0x00000001;
constexpr melonDS::u16 kVsBattleStarRelatedObjectID = 0x0021;
constexpr melonDS::u16 kVsBattleStarCandidateObjectID = 0x010C;
constexpr melonDS::u16 kVsMovingHazardObjectID = 0x0053;
constexpr melonDS::u32 kVsMovingHazardSettings = 0x00000000;
constexpr melonDS::u16 kVsKoopaTroopaObjectID = 0x005E;
constexpr melonDS::u16 kVsWorldItemObjectID = 0x001F;
constexpr melonDS::u32 kVsNeutralWorldItemSettings = 0x00080000;
constexpr melonDS::u32 kVsWorldItemSettings = 0x00080002;
constexpr melonDS::u32 kVsDroppedStarItemSettings = 0x00090002;
constexpr melonDS::u32 kVsWorldItemNaturalSpawnGraceFrames = 4;
constexpr melonDS::u32 kEffectVTableStart = 0x02126A24;
constexpr melonDS::u32 kEffectVTablePtr = 0x02126A2C;
constexpr melonDS::u32 kWorldEffectSlotBase = 0x021C3268;
constexpr melonDS::u32 kWorldEffectSlotStride = 0x1D4;
constexpr melonDS::u32 kWorldEffectSlotCount = 32;
constexpr melonDS::u32 kWorldEffectWordStart = 0x04;
constexpr melonDS::u32 kWorldEffectWordEnd = 0xAC;
static_assert(kWorldEffectWordCount ==
    ((kWorldEffectWordEnd - kWorldEffectWordStart) / sizeof(melonDS::u32)) + 1);
constexpr melonDS::u32 kFireballsHandlerAddr = 0x02129484;
constexpr melonDS::u32 kProjectilesHandlerAddr = 0x0212A680;

bool IsVsDroppedStarActorSettings(melonDS::u32 settings)
{
    const melonDS::u32 normalized = settings & 0x7FFFFFFFu;
    return normalized == 0x00001002u ||
        normalized == 0x00001012u ||
        normalized == 0x00001102u ||
        normalized == 0x00001112u;
}
constexpr melonDS::u32 kAIFireballSlotActiveOffset = 0x80;
// Fireball::create stores the spawn kind at +0x81. For player fireballs, kind 0/1 is the owner player id.
constexpr melonDS::u32 kAIFireballSlotDebugWordOffset = 0x40;
constexpr int kAITileGridMinRelX = -16;
constexpr int kAITileGridMinRelY = -10;
constexpr melonDS::u16 kStageSceneObjectID = 0x0003;
constexpr melonDS::u32 kMvlStageSceneDefaultSettings = 0x00B4FF00;
constexpr melonDS::u16 kStageFXObjectID = 0x0012;
constexpr melonDS::u16 kStageActorManagerObjectID = 0x012F;
constexpr melonDS::u16 kStageControllerObjectID = 0x0130;
constexpr melonDS::u16 kMvlObject267ID = 0x010B;
constexpr melonDS::u16 kVsConnectObjectID = 0x0006;
constexpr melonDS::u16 kCourseSelectObjectID = 0x0005;
constexpr melonDS::u16 kStageCameraObjectID = 0x013C;
constexpr melonDS::u16 kStageLayoutObjectID = 0x0145;
constexpr melonDS::u16 kCoinObjectID = 0x0042;
constexpr melonDS::u16 kGoombaObjectID = 0x0053;
constexpr melonDS::u16 kGoombaBigObjectID = 0x0054;
constexpr melonDS::u16 kGoombaMegaObjectID = 0x0055;
constexpr melonDS::u16 kKoopaTroopaAltObjectID = 0x005F;
constexpr melonDS::u16 kWarpEntranceObjectID = 0x0057;
constexpr melonDS::u16 kDonutLiftObjectID = 0x0047;
constexpr melonDS::u16 kTrampolineObjectID = 0x00ED;
constexpr melonDS::u16 kSpinBlockObjectID = 0x00FE;
constexpr melonDS::u16 kSpinBlockAltObjectID = 0x00FF;
constexpr melonDS::u16 kSpinBlockFinalObjectID = 0x0100;
constexpr melonDS::u16 kBulletBillObjectID = 0x001B;
constexpr melonDS::u16 kBulletBillAltObjectID = 0x00EE;
constexpr melonDS::u16 kBulletBillBlasterObjectID = 0x00F8;
constexpr melonDS::u16 kBulletBillBlasterAltObjectID = 0x00F9;
constexpr melonDS::u16 kThwompObjectID = 0x0025;
constexpr melonDS::u16 kThwompAltObjectID = 0x0026;
constexpr melonDS::u16 kFirebarObjectID = 0x0041;
constexpr melonDS::u16 kBobOmbObjectID = 0x0023;
constexpr melonDS::u16 kItemSpawnEffectObjectID = 0x00F0;
constexpr melonDS::u32 kStageCameraXAddr = 0x020CAE1C;
constexpr melonDS::u32 kStageCameraYAddr = 0x020CAD94;
constexpr melonDS::u32 kStageCameraWidthAddr = 0x020CADA4;
constexpr melonDS::u32 kStageCameraHeightAddr = 0x020CAD8C;
constexpr melonDS::u32 kA2DJGameLoadLevelAddr = 0x020068A8;
constexpr melonDS::u32 kA2DEActorSpawnActorAddr = 0x020A0B64;
constexpr melonDS::u32 kDirectBootTrampolineAddr = 0x023C0000;
constexpr melonDS::u32 kDirectBootTrampolineDataAddr = 0x023C0400;
// Overlay0 padding cave. Keep runtime settings out of high Main RAM, which NSMB
// can use for stage graphics/model buffers during MvL gameplay.
constexpr melonDS::u32 kMvlRuntimeConfigAddr = 0x020C5360;
constexpr melonDS::u32 kMvlRuntimeConfigMagic = 0x434C564D; // "MVLC", little endian
constexpr melonDS::u32 kMvlRuntimeConfigStageOffset = 0x04;
constexpr melonDS::u32 kMvlRuntimeConfigSceneSettingsOffset = 0x08;
constexpr melonDS::u32 kMvlRuntimeConfigInitialLivesOffset = 0x0C;
constexpr melonDS::u32 kMvlRuntimeConfigLifeModeSelectorOffset = 0x10;
constexpr melonDS::u32 kMvlRuntimeConfigBigStarSelectorOffset = 0x14;
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

using RollbackBackend = Config::RollbackBackend;

constexpr melonDS::u32 kRollbackMainRAMModeFull = 0;
constexpr melonDS::u32 kRollbackMainRAMModeSparse = 1;
constexpr melonDS::u32 kRollbackMainRAMModeDelta = 2;
constexpr melonDS::u32 kRollbackMainRAMModeSkip = 3;

constexpr std::size_t kDiagnosticRingCapacity = 720;
constexpr melonDS::u32 kDiagnosticPostTriggerFrames = 120;
constexpr melonDS::u32 kDiagnosticRepeatedAnomalyFrames = 120;
constexpr melonDS::u32 kPlayerPitDeathTransitStateAddr = 0x021196B0;
constexpr melonDS::s32 kDiagnosticFixedOne = 0x1000;
constexpr melonDS::s32 kDiagnosticOffscreenMargin = 512 * kDiagnosticFixedOne;
constexpr melonDS::s32 kDiagnosticLargePositionDelta = 256 * kDiagnosticFixedOne;


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








AITerrainDerivedSummary DeriveAITerrainSummaryFromGrid(
    const AIPlayerTileProbeSample& probe,
    bool contactGround,
    bool contactWallLeft,
    bool contactWallRight);

bool AITerrainTargetHasFloorBelow(
    const AIPlayerTileProbeSample& probe,
    melonDS::u32 selfX,
    melonDS::u32 selfY,
    melonDS::u32 targetX,
    melonDS::u32 targetY);

const char* AIObjectCategory(melonDS::u16 objectID, melonDS::u32 settings);


GameStateSample ReadGameStateSample(melonDS::NDS* nds);

struct DiagnosticPlayerSnapshot
{
    melonDS::u32 Found = 0;
    melonDS::u32 Base = 0;
    melonDS::u32 GUID = 0;
    melonDS::u32 Settings = 0;
    melonDS::u32 StateType = 0;
    melonDS::u32 Flags = 0;
    melonDS::u32 PosX = 0;
    melonDS::u32 PosY = 0;
    melonDS::u32 PosZ = 0;
    melonDS::u32 PrevX = 0;
    melonDS::u32 PrevY = 0;
    melonDS::u32 PrevZ = 0;
    melonDS::u32 VelX = 0;
    melonDS::u32 VelY = 0;
    melonDS::u32 VelZ = 0;
    melonDS::u32 ActionFlag = 0;
    melonDS::u32 SubActionFlag = 0;
    melonDS::u32 PhysicsFlag = 0;
    melonDS::u32 DamageCooldown = 0;
    melonDS::u32 TransitionFlag = 0;
    melonDS::u32 CollisionFlag = 0;
    melonDS::u32 EnvironmentFlag = 0;
    melonDS::u32 LinkedActor = 0;
    melonDS::u32 TransitionStep = 0;
    melonDS::u32 UpdateLocked = 0;
    melonDS::u32 CharacterIDBase = 0;
    melonDS::u32 TransitioningFlag = 0;
    melonDS::u32 CameraFocusMode = 0;
    melonDS::u32 DefeatedFlag = 0;
    melonDS::u32 PlayerBaseID = 0;
    melonDS::u32 VisibleFlag = 0;
    melonDS::u32 TransitFunc = 0;
    melonDS::u32 TransitArg = 0;
    melonDS::u32 Powerup = 0;
    melonDS::u32 InventoryPowerup = 0;
    melonDS::u32 Dead = 0;
    melonDS::u32 Character = 0;
    melonDS::u32 TransitionStatus = 0;
    melonDS::u32 Lives = 0;
    melonDS::u32 BattleStars = 0;
    melonDS::u32 Coins = 0;
    melonDS::u32 Score = 0;
    melonDS::u32 DisplayedStars = 0;
    melonDS::u32 Deaths = 0;
    melonDS::u32 CollectedStars = 0;
};

struct DiagnosticFrameSnapshot
{
    bool Valid = false;
    melonDS::u32 Frame = 0;
    melonDS::u32 Instance = 0;
    melonDS::u32 StageID = 0;
    melonDS::u32 StageGroup = 0;
    melonDS::u32 VsMode = 0;
    melonDS::u32 LocalPlayerID = 0;
    melonDS::u32 SceneCurrentSceneID = 0;
    melonDS::u32 SceneNextSceneID = 0;
    melonDS::u32 StageActorFreezeFlag = 0;
    melonDS::u32 PlayerCount = 0;
    melonDS::u32 InputConsole0Held = 0;
    melonDS::u32 InputConsole1Held = 0;
    melonDS::u32 InputPlayer0Held = 0;
    melonDS::u32 InputPlayer1Held = 0;
    melonDS::u32 LastSentInputFrame = 0;
    melonDS::u32 LastReceivedInputFrame = 0;
    melonDS::u64 PlayerGlobalHash = 0;
    melonDS::u64 PlayerGlobalHash0 = 0;
    melonDS::u64 PlayerGlobalHash1 = 0;
    melonDS::u64 PlayerActorHash0 = 0;
    melonDS::u64 PlayerActorHash1 = 0;
    melonDS::u32 StageCameraGlobalX0 = 0;
    melonDS::u32 StageCameraGlobalX1 = 0;
    melonDS::u32 StageCameraGlobalY0 = 0;
    melonDS::u32 StageCameraGlobalY1 = 0;
    melonDS::u32 StageCameraGlobalWidth0 = 0;
    melonDS::u32 StageCameraGlobalWidth1 = 0;
    melonDS::u32 StageCameraGlobalHeight0 = 0;
    melonDS::u32 StageCameraGlobalHeight1 = 0;
    DiagnosticPlayerSnapshot Player[2];
};

void RecordDiagnosticSnapshotIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds);


bool WriteARM9U32(melonDS::NDS* nds, melonDS::u32 addr, melonDS::u32 value);
void EmitGameStateMismatchEventLocked(
    int instanceID,
    melonDS::u32 frame,
    const GameStateSyncHashes& local,
    const GameStateSyncHashes& remote);
void EmitPlayerLifeEvent(
    int instanceID,
    melonDS::u32 frame,
    int player,
    const char* reason,
    const GameStateSample& sample,
    melonDS::NDS* nds);
void EmitStartReadyEventLocked(const char* direction, melonDS::u32 localFrame, melonDS::u32 remoteFrame);

bool IsARM9MainRAMAddress(melonDS::u32 addr)
{
    return (addr & 0xFF000000u) == 0x02000000u;
}

int CurrentPacketBridgeLocalPlayer();

struct State
{
    std::mutex Mutex;
    std::mutex PerfMutex;
    std::atomic<bool> EnvChecked { false };
    Config::BootstrapConfig Bootstrap;
    bool Enabled = false;
    bool Ready = false;
    bool TestEnabled = false;
    bool TestAnnouncedQuit = false;
    Config::ConnectionConfig Connection;
    Role NetRole = Role::Host;
    melonDS::u32 MvlAutoRestartStartupFrameBase = 0;
    bool TestTimerStarted = false;
    std::chrono::steady_clock::time_point TestTimerStart;
    bool ActiveTimerStarted[16] {};
    melonDS::u32 ActiveTimerStartFrame[16] {};
    std::chrono::steady_clock::time_point ActiveTimerStart[16];
    melonDS::u32 ActiveFpsStartFrame = 0;
    bool ActiveFrameTimingStarted[16] {};
    std::chrono::steady_clock::time_point ActiveFrameLastTime[16];
    melonDS::u32 ActiveFrameSamples[16] {};
    unsigned long long ActiveFrameTotalUs[16] {};
    unsigned long long ActiveFrameMaxUs[16] {};
    melonDS::u32 ActiveFrameMaxFrame[16] {};
    melonDS::u32 ActiveFrameOver16ms[16] {};
    melonDS::u32 ActiveFrameOver25ms[16] {};
    melonDS::u32 ActiveFrameOver33ms[16] {};
    int ActiveFrameSpikeThresholdUs = 25000;
    bool ActiveFrameSpikeTrace = false;
    int FrameHeartbeatInterval = 0;
    melonDS::u32 LastFrameHeartbeat[16] {};
    int GameplayHeartbeatInterval = 0;
    melonDS::u32 LastGameplayHeartbeat[16] {};
    std::string FrameHeartbeatPath;
    std::ofstream FrameHeartbeat;
    std::atomic<melonDS::u32> PendingFrameHeartbeat { 0 };
    std::atomic<bool> FrameHeartbeatStop { false };
    bool FrameHeartbeatThreadStarted = false;
    std::thread FrameHeartbeatThread;
    bool InputRecordEnabled = false;
    std::string InputRecordPath;
    std::ofstream InputRecord;
    std::mutex InputRecordMutex;
    melonDS::u32 InputRecordStartFrame = 0;
    melonDS::u32 InputRecordEndFrame = kNoFrameLimit;
    int InputRecordInstance = -1;
    bool InputRecordHasSpan = false;
    melonDS::u32 InputRecordSpanStart = 0;
    melonDS::u32 InputRecordSpanEnd = 0;
    InputState InputRecordSpanInput;
    int InputRecordFlushPendingSpans = 0;
    bool ScreenHashEnabled = false;
    bool GameStateTraceExtended = false;
    melonDS::u32 GameStateTraceStartFrame = 0;
    melonDS::u32 GameStateTraceEndFrame = 0;
    melonDS::u32 AIPlayLogStartFrame = 0;
    melonDS::u32 AIPlayLogEndFrame = 0;
    Config::StateSyncConfig StateSync;
    bool WaitedForPeerAtNetplayStart = false;
    bool NetplayStartReadySent = false;
    int NetplayStartReadySendCount = 0;
    std::chrono::steady_clock::time_point LastNetplayStartReadySentAt;
    std::chrono::steady_clock::time_point LastInputFrameLeadResendAt;
    int InputFrameLeadResendCount = 0;
    melonDS::u32 LocalNetplayStartReadyFrame = kNoFrameLimit;
    melonDS::u32 RemoteNetplayStartReadyFrame = kNoFrameLimit;
    bool RemoteNetplayStartReadyAfterLocal = false;
    melonDS::u32 InputEpochPrimedStartFrame = kNoFrameLimit;
    bool NetplayStartWaitArrived[16] {};
    bool NetplayStartWaitComplete = false;
    Config::PacketBridgeConfig PacketBridge;
    Config::InputConfig Input;
    std::map<melonDS::u32, InputState> PacketBridgePacketInputs;
    std::vector<DelayedWireNSMLPacket> DelayedNSMLPackets;
    std::vector<DelayedWireInput> DelayedInputs;
    Config::MvlConfig Mvl;
    int MvlCurrentStage = 0;
    melonDS::u32 MvlCurrentStageSceneSettings = kMvlStageSceneDefaultSettings;
    bool MvlEntranceSpawnNormalizedLogged[16] {};
    bool MvlAutoRestartInResult[16] {};
    bool MvlAutoRestartResultScored[16] {};
    bool MvlAutoRestartResultUnresolvedLogged[16] {};
    melonDS::u32 MvlAutoRestartResultFrame[16] {};
    melonDS::u32 MvlAutoRestartLastRestartFrame[16] {};
    int MvlAutoRestartCount[16] {};
    int MvlAutoRestartWins[16][2] {};
    std::vector<char> MvlAutoRestartBootstrapCheckpoint[16];
    melonDS::u32 MvlAutoRestartBootstrapCheckpointFrame[16] {};
    bool MvlAutoRestartBootstrapCheckpointLogged[16] {};
    std::vector<char> MvlAutoRestartCheckpoint[16];
    melonDS::u32 MvlAutoRestartCheckpointFrame[16] {};
    int MvlAutoRestartCheckpointStage[16] {};
    bool MvlAutoRestartCheckpointLogged[16] {};
    Config::AIConfig AI;
    bool ImitationAIEnabled = false;
    bool ImitationAIFireTapPressNext[16][2] {};
    bool ImitationAILastHeldValid[16][2] {};
    melonDS::u32 ImitationAILastHeld[16][2] {};
    bool ImitationAICachedHeldValid[16][2] {};
    melonDS::u32 ImitationAICachedHeld[16][2] {};
    melonDS::u32 ImitationAICachedFrame[16][2] {};
    bool ImitationAILastNonZeroHeldValid[16][2] {};
    melonDS::u32 ImitationAILastNonZeroHeld[16][2] {};
    melonDS::u32 ImitationAILastNonZeroFrame[16][2] {};
    NsmbImitationAI::LinearPolicyModel ImitationAIModel;
    NsmbImitationAI::CompactActionPolicyModel ImitationAICompactModel;
    NsmbImitationAI::TorchCompactPolicyModel ImitationAITorchCompactModel;
    bool ImitationAIModelLoaded = false;
    bool ImitationAICompactModelLoaded = false;
    bool ImitationAITorchCompactModelLoaded = false;
    int ImitationAIFeaturesFilled = 0;
    int ImitationAIFeaturesMissing = 0;
    bool DirectMvlBootApplied[16] {};
    std::string InputScriptPath;
    Config::RuntimePatchConfig RuntimePatch;
    Config::HarnessConfig Harness;
    std::string HashLogPath;
    std::string ScreenshotDir;
    std::string RamDumpDir;
    std::string GameStateTracePath;
    std::string DiagnosticsPath;
    std::string DiagnosticEventsPath;
    std::string AIPlayLogPath;
    std::string AIObservationV2Path;
    std::string AIObservationV3Path;
    std::ofstream HashLog;
    std::ofstream GameStateTrace;
    std::ofstream DiagnosticEvents;
    std::ofstream AIPlayLog;
    std::ofstream AIObservationV2Log;
    std::ofstream AIObservationV3Log;
    bool DiagnosticEventsEnabled = false;
    int DiagnosticRingFrames = 360;
    melonDS::u32 DiagnosticPostTriggerUntilFrame[16] {};
    melonDS::u32 LastDiagnosticMismatchFrame[16] {};
    melonDS::u32 LastDiagnosticLifeEventFrame[16][2] {};
    melonDS::u32 LastDiagnosticPitTransitionFrame[16][2] {};
    melonDS::u32 LastDiagnosticPositionAnomalyFrame[16][2] {};
    std::array<DiagnosticFrameSnapshot, kDiagnosticRingCapacity> DiagnosticRing[16];
    std::size_t DiagnosticRingNext[16] {};
    InputState AIPlayLogLastAppliedInput[16][2] {};
    melonDS::u32 AIPlayLogLastAppliedInputFrame[16][2] {};
    bool AIPlayLogLastAppliedInputValid[16][2] {};
    melonDS::u32 AIPlayLogFireballOwnerHandlerPtr[16] {};
    bool AIPlayLogFireballOwnerValid[16][kAIFireballSlotCount] {};
    int AIPlayLogFireballOwner[16][kAIFireballSlotCount] {};
    int AIPlayLogFireballOwnerConfidence[16][kAIFireballSlotCount] {};
    int AIPlayLogFireballOwnerHeuristic[16][kAIFireballSlotCount] {};
    int ScreenshotInterval = 0;
    int RamDumpInterval = 0;
    int GameStateTraceInterval = 60;
    int AIPlayLogInterval = 1;
    int AIPlayLogFlushInterval = 60;
    int AIPlayLogLinesSinceFlush = 0;
    int AIObservationV2LinesSinceFlush = 0;
    int AIObservationV3LinesSinceFlush = 0;
    int AIPlayLogMaxObjects = 32;
    int AIObservationV2StageFilter = -1;
    int AIObservationV3StageFilter = -1;
    bool AIPlayLogGameplayOnly = true;
    bool MemPatchApplied[16] {};
    bool ForcePlayerDeathCountersLogged[16] {};
    bool ForcePlayerPowerupsLogged[16] {};
    bool ForcePlayerInventoryPowerupsLogged[16] {};
    bool ForcePlayerStarCountersLogged[16] {};
    bool LastPlayerLifeSampleValid[16] {};
    GameStateSample LastPlayerLifeSample[16] {};
    bool ScriptRemotePacketLogged[16] {};
    bool PacketBridgeJitHelperPatchApplied[16] {};
    melonDS::u32 PacketBridgeJitHelperPatchResumeFrame[16] {};
    Config::RollbackConfig Rollback;
    std::map<melonDS::u32, InputState> PredictedRemoteInputs;
    RollbackStorage::Store RollbackStore;
    bool LastConfirmedRemoteInputValid = false;
    InputState LastConfirmedRemoteInput {};
    melonDS::u32 PendingRollbackFrame = kNoFrameLimit;
    melonDS::u32 PendingRollbackObservedFrame = kNoFrameLimit;
    melonDS::u32 RollbackPredictionCount = 0;
    melonDS::u32 RollbackMismatchCount = 0;
    melonDS::u32 RollbackRestoreCount = 0;
    melonDS::u32 RollbackResimulateCount = 0;
    melonDS::u32 LastPerfSpikeRollbackRestoreCount[16] {};
    melonDS::u32 LastPerfSpikeRollbackResimulateCount[16] {};
    melonDS::u32 RollbackPredictionProbeCount = 0;
    melonDS::u32 RollbackCheckpointSaveCount = 0;
    size_t RollbackCheckpointLastBytes = 0;
    size_t RollbackCheckpointMinBytes = 0;
    size_t RollbackCheckpointMaxBytes = 0;
    unsigned long long RollbackCheckpointTotalBytes = 0;
    unsigned long long RollbackCheckpointSaveTotalUs = 0;
    unsigned long long RollbackCheckpointSaveMaxUs = 0;
    unsigned long long RollbackCheckpointRestoreTotalUs = 0;
    unsigned long long RollbackCheckpointRestoreMaxUs = 0;
    melonDS::u32 RollbackCheckpointRestoreOpCount = 0;
    melonDS::u32 RollbackMeasuredResimOpCount = 0;
    unsigned long long RollbackMeasuredResimFrameCount = 0;
    unsigned long long RollbackResimRunFrameTotalUs = 0;
    unsigned long long RollbackResimRunFrameMaxUs = 0;
    unsigned long long RollbackResimCheckpointSaveTotalUs = 0;
    unsigned long long RollbackResimCheckpointSaveMaxUs = 0;
    unsigned long long RollbackResimCorrectionTotalUs = 0;
    unsigned long long RollbackResimCorrectionMaxUs = 0;
    melonDS::u32 LastRollbackTraceFrame = kNoFrameLimit;
    bool ClearMvlCameraInitHoldEnabled = false;
    bool ClearMvlCameraInitHoldHostOnly = false;
    bool ClearMvlCameraInitHoldClientOnly = false;
    melonDS::u32 ClearMvlCameraInitHoldStartFrame = 840;
    melonDS::u32 ClearMvlCameraInitHoldEndFrame = 0;
    bool ClearMvlCameraInitHoldApplied[16] {};
    bool NetRandomPatchEnabled = false;
    bool NetRandomPatchAuto = false;
    melonDS::u32 NetRandomPatchFrame = 0;
    melonDS::u32 NetRandomPatchValue = 0;
    bool NetRandomPatchApplied[16] {};
    bool MatchSeedConfigured = false;
    bool MatchSeedSent = false;
    melonDS::u32 MatchSeed = 0;
    std::vector<melonDS::u32> MatchSeedSequence;
    bool NetplayAnyLockstepStarted = false;
    bool NetplayLockstepStarted[16] {};
    NsmbNetplayTransport::Transport Transport;
    std::map<melonDS::u32, InputState> LocalInputs;
    std::map<melonDS::u32, InputState> RemoteInputs;
    std::map<melonDS::u64, GameStateSyncHashes> LocalGameStateHashes;
    std::map<melonDS::u64, GameStateSyncHashes> RemoteGameStateHashes;
    std::map<melonDS::u64, GameStateSample> RemoteGameStateSamples;
    std::map<melonDS::u64, WirePlayerState> RemotePlayerStateSamples;
    WireWorldState RemoteWorldStateSample {};
    bool RemoteWorldStateSampleValid = false;
    WireMovingHazardState RemoteMovingHazardStateSample {};
    bool RemoteMovingHazardStateSampleValid = false;
    WireWorldActorSnapshotState RemoteWorldActorSnapshotSample {};
    bool RemoteWorldActorSnapshotSampleValid = false;
    WireWorldEffectState RemoteWorldEffectStateSample {};
    bool RemoteWorldEffectStateSampleValid = false;
    std::vector<WireNSMLPacket> PendingNSMLPackets;
    bool GameStateMismatchSeen = false;
    melonDS::u32 LastTracedSentInputFrame = kNoFrameLimit;
    melonDS::u32 LastTracedReceivedInputFrame = kNoFrameLimit;
    melonDS::u32 LastReceivedInputFrame = kNoFrameLimit;
    melonDS::u32 LastSentInputFrame = kNoFrameLimit;
    melonDS::u32 LastInputHealthSummaryFrame = kNoFrameLimit;
    melonDS::u32 LastInputHealthRemoteWaitFrame = kNoFrameLimit;
    melonDS::u32 LastInputHealthThrottleFrame = kNoFrameLimit;
    melonDS::u32 LastInputHealthThrottleResolvedFrame = kNoFrameLimit;
    melonDS::u32 LastInputHealthReceiveGapFrame = kNoFrameLimit;
    melonDS::u32 LastInputHealthSendGapFrame = kNoFrameLimit;
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
    melonDS::u64 LastSentPlayerStateFrame[16] {};
    melonDS::u64 LastSentWorldStateFrame[16] {};
    melonDS::u32 LastAppliedPlayerGlobalsFrame[16][2] {};
    melonDS::u32 PlayerActorBaseCache[16][2] {};
    melonDS::u32 PlayerActorGUIDCache[16][2] {};
    melonDS::u32 WorldStarActorBaseCache[16] {};
    melonDS::u32 WorldStarActorGUIDCache[16] {};
    melonDS::u32 LastSpawnedWorldItemRemoteGUID[16] {};
    melonDS::u32 LastConfirmedWorldItemRemoteGUID[16] {};
    melonDS::u32 PendingWorldItemRemoteGUID[16] {};
    melonDS::u32 PendingWorldItemFirstMissingFrame[16] {};
    melonDS::u32 LastSpawnedNeutralWorldItemRemoteGUID[16] {};
    melonDS::u32 LastConfirmedNeutralWorldItemRemoteGUID[16] {};
    melonDS::u32 PendingNeutralWorldItemRemoteGUID[16] {};
    melonDS::u32 PendingNeutralWorldItemFirstMissingFrame[16] {};
    melonDS::u32 LastSpawnedDroppedStarItemRemoteGUID[16] {};
    melonDS::u32 LastConfirmedDroppedStarItemRemoteGUID[16] {};
    melonDS::u32 PendingDroppedStarItemRemoteGUID[16] {};
    melonDS::u32 PendingDroppedStarItemFirstMissingFrame[16] {};
    melonDS::u32 WorldMovingHazardBaseCache[16] {};
    melonDS::u32 WorldMovingHazardGUIDCache[16] {};
    melonDS::u32 WorldMovingHazardBaseCaches[16][kMaxWorldMovingHazards] {};
    melonDS::u32 WorldMovingHazardGUIDCaches[16][kMaxWorldMovingHazards] {};
    melonDS::u32 WorldMovingHazardRemoteGUIDMaps[16][kMaxWorldMovingHazards] {};
    melonDS::u32 WorldMovingHazardLocalGUIDMaps[16][kMaxWorldMovingHazards] {};
    melonDS::u32 WorldActorSnapshotRemoteGUIDMaps[16][kMaxWorldActorSnapshots] {};
    melonDS::u32 WorldActorSnapshotLocalGUIDMaps[16][kMaxWorldActorSnapshots] {};
    melonDS::u32 WorldMovingHazardCacheCounts[16] {};
    melonDS::u32 LastTracedWorldMovingHazardsFrame[16] {};
    melonDS::u32 LastTracedWorldEffectsFrame[16] {};
    melonDS::u32 LastTracedWorldObjectLifecyclesFrame[16] {};
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
    bool NetworkPumpThreadStarted = false;
    bool NetworkPumpStop = false;
    int InputWaitPollUs = 100;
    std::thread NetworkPumpThread;
    bool HangDiagnosticsEnabled = false;
    int HangWatchdogIntervalMs = 1000;
    int HangThresholdMs = 8000;
    std::string HangWatchdogPath;
    std::string HangPhaseEventsPath;
    std::string HangDumpPath;
    std::ofstream HangWatchdogLog;
    std::ofstream HangPhaseEventsLog;
    std::mutex HangLogMutex;
    std::atomic<bool> HangWatchdogStop { false };
    bool HangWatchdogThreadStarted = false;
    std::thread HangWatchdogThread;
    std::atomic<const char*> HangPhase { "startup" };
    std::atomic<const char*> HangEvent { "startup" };
    std::atomic<unsigned long long> HangPhaseUnixMs { 0 };
    std::atomic<unsigned long long> HangProgressUnixMs { 0 };
    std::atomic<unsigned long long> HangLastDumpUnixMs { 0 };
    std::atomic<int> HangInstance { -1 };
    std::atomic<melonDS::u32> HangFrame { 0 };
    std::atomic<melonDS::u32> HangLogicalFrame { 0 };
    std::atomic<melonDS::u32> HangSendFrame { 0 };
    std::atomic<melonDS::u32> HangRemoteWaitTarget { 0 };
    std::atomic<int> HangRemoteWaitActive { 0 };
    std::atomic<unsigned long long> HangRemoteWaitStartUnixMs { 0 };
    std::atomic<unsigned long long> HangRemoteWaitProgressUnixMs { 0 };
    std::atomic<melonDS::u32> HangLastSentFrame { kNoFrameLimit };
    std::atomic<melonDS::u32> HangLastRecvFrame { kNoFrameLimit };
    std::atomic<int> HangLead { 0 };
    std::atomic<std::size_t> HangLocalQueue { 0 };
    std::atomic<std::size_t> HangRemoteQueue { 0 };
    std::atomic<std::size_t> HangDelayedQueue { 0 };
    std::atomic<int> HangPeerState { -1 };
    std::atomic<int> HangConnectingPeerState { -1 };
    std::atomic<unsigned long long> HangLastENetSendUnixMs { 0 };
    std::atomic<unsigned long long> HangLastENetRecvUnixMs { 0 };
    std::atomic<int> HangLastENetSendResult { 0 };
    std::atomic<int> HangLastENetServiceResult { 0 };
    std::atomic<int> HangLastENetEventType { 0 };
    std::atomic<melonDS::u32> HangLastENetEventData { 0 };
    std::atomic<std::size_t> HangLastENetSendBytes { 0 };
    std::atomic<melonDS::u32> HangArm9PC { 0 };
    std::atomic<melonDS::u32> HangArm9LR { 0 };
    std::atomic<melonDS::u32> HangArm9SP { 0 };
    std::atomic<melonDS::u32> HangArm9CPSR { 0 };
    std::atomic<melonDS::u32> HangStageID { 0 };
    std::atomic<melonDS::u32> HangStageGroup { 0 };
    std::atomic<melonDS::u32> HangVsMode { 0 };
    std::atomic<melonDS::u32> HangNetState14 { 0 };
    std::atomic<melonDS::u32> HangNetState1C { 0 };
    std::atomic<melonDS::u32> HangNetState20 { 0 };
    std::atomic<melonDS::u32> HangNetState24 { 0 };
    std::atomic<melonDS::u32> HangNetState5C { 0 };
    std::atomic<melonDS::u32> HangNetPacketTick { 0 };
    std::atomic<melonDS::u32> HangAppFrameLength { 0 };
    std::atomic<melonDS::u32> HangAppUpdateTask { 0 };
    std::atomic<melonDS::u32> HangAppSleeping { 0 };
    std::atomic<melonDS::u32> HangStageSceneState { 0 };
    std::atomic<melonDS::u32> HangPlayer0Transition { 0 };
    std::atomic<melonDS::u32> HangPlayer1Transition { 0 };
    std::atomic<unsigned long long> HangGameSnapshotUnixMs { 0 };
    unsigned long long RemoteInputWaitCount = 0;
    unsigned long long RemoteInputWaitLoops = 0;
    unsigned long long RemoteInputWaitUs = 0;
    unsigned long long RemoteInputWaitMaxUs = 0;
    unsigned long long FrameLeadThrottleCount = 0;
    unsigned long long FrameLeadThrottleLoops = 0;
    unsigned long long FrameLeadThrottleUs = 0;
    unsigned long long FrameLeadThrottleMaxUs = 0;
};

State G;
std::vector<InputTimeline::InputSpan> GInputScript;
std::vector<InputTimeline::InputSpan> GScriptRemotePacketInputScript;

void TraceHangPhase(const char* event, const char* phase, int instanceID = -1,
    melonDS::u32 frame = 0, melonDS::u32 logicalFrame = 0, melonDS::u32 sendFrame = 0);
void UpdateHangGameSnapshot(int instanceID, melonDS::u32 frame, melonDS::NDS* nds);
void StartHangWatchdogIfNeeded();
void StopHangWatchdog();

struct FrameBarrier
{
    bool Waiting[16] {};
    melonDS::u32 Frame[16] {};
    int Generation = 0;
};

FrameBarrier GBeforeFrameBarrier;
FrameBarrier GAfterFrameBarrier;
FrameBarrier GNetplayFrameBarrier;

using Config::EnvCString;
using Config::EnvDouble;
using Config::EnvFlag;
using Config::EnvHasValue;
using Config::EnvInt;
using Config::EnvU32;

bool EnsureHangLogOpenLocked(std::ofstream& file, const std::string& path)
{
    if (path.empty())
        return false;
    if (file.is_open())
        return true;

    const std::filesystem::path logPath(path);
    std::error_code ec;
    if (logPath.has_parent_path())
        std::filesystem::create_directories(logPath.parent_path(), ec);
    file.open(logPath, std::ios::out | std::ios::app | std::ios::binary);
    if (!file)
    {
        std::printf("NSMB HangDiagnostics: failed to open log: %s\n", logPath.string().c_str());
        std::fflush(stdout);
        return false;
    }
    return true;
}

void UpdateHangNetplaySnapshotLocked(melonDS::u32 frameForLead)
{
    if (!G.HangDiagnosticsEnabled)
        return;

    G.HangLastSentFrame.store(G.LastSentInputFrame, std::memory_order_release);
    G.HangLastRecvFrame.store(G.LastReceivedInputFrame, std::memory_order_release);
    const int lead = (frameForLead == kNoFrameLimit || G.LastReceivedInputFrame == kNoFrameLimit)
        ? 0
        : static_cast<int>(frameForLead) - static_cast<int>(G.LastReceivedInputFrame);
    G.HangLead.store(lead, std::memory_order_release);
    G.HangLocalQueue.store(G.LocalInputs.size(), std::memory_order_release);
    G.HangRemoteQueue.store(G.RemoteInputs.size(), std::memory_order_release);
    G.HangDelayedQueue.store(G.DelayedInputs.size(), std::memory_order_release);
    G.HangPeerState.store(G.Transport.PeerState(), std::memory_order_release);
    G.HangConnectingPeerState.store(G.Transport.ConnectingPeerState(), std::memory_order_release);
}

void WriteHangPhaseEventLocked(
    unsigned long long now,
    const char* event,
    const char* phase,
    int instanceID,
    melonDS::u32 frame,
    melonDS::u32 logicalFrame,
    melonDS::u32 sendFrame)
{
    if (!EnsureHangLogOpenLocked(G.HangPhaseEventsLog, G.HangPhaseEventsPath))
        return;

    G.HangPhaseEventsLog
        << "{\"tUnixMs\":" << now
        << ",\"event\":\"" << (event ? event : "phase") << "\""
        << ",\"phase\":\"" << (phase ? phase : "unknown") << "\""
        << ",\"instance\":" << instanceID
        << ",\"frame\":" << frame
        << ",\"logicalFrame\":" << logicalFrame
        << ",\"sendFrame\":" << sendFrame
        << ",\"lastSent\":" << G.HangLastSentFrame.load(std::memory_order_acquire)
        << ",\"lastRecv\":" << G.HangLastRecvFrame.load(std::memory_order_acquire)
        << ",\"lead\":" << G.HangLead.load(std::memory_order_acquire)
        << ",\"remoteWaitActive\":" << G.HangRemoteWaitActive.load(std::memory_order_acquire)
        << ",\"remoteWaitTarget\":" << G.HangRemoteWaitTarget.load(std::memory_order_acquire)
        << "}\n";
    G.HangPhaseEventsLog.flush();
}

void TraceHangPhase(
    const char* event,
    const char* phase,
    int instanceID,
    melonDS::u32 frame,
    melonDS::u32 logicalFrame,
    melonDS::u32 sendFrame)
{
    if (!G.HangDiagnosticsEnabled)
        return;

    const unsigned long long now = NowUnixMs();
    G.HangEvent.store(event ? event : "phase", std::memory_order_release);
    G.HangPhase.store(phase ? phase : "unknown", std::memory_order_release);
    G.HangPhaseUnixMs.store(now, std::memory_order_release);
    G.HangProgressUnixMs.store(now, std::memory_order_release);
    G.HangInstance.store(instanceID, std::memory_order_release);
    G.HangFrame.store(frame, std::memory_order_release);
    G.HangLogicalFrame.store(logicalFrame, std::memory_order_release);
    G.HangSendFrame.store(sendFrame, std::memory_order_release);

    std::lock_guard<std::mutex> lock(G.HangLogMutex);
    WriteHangPhaseEventLocked(now, event, phase, instanceID, frame, logicalFrame, sendFrame);
}

#ifdef _WIN32
bool WriteHangMiniDump(const std::string& path)
{
    if (path.empty())
        return false;

    const std::filesystem::path dumpPath(path);
    std::error_code ec;
    if (dumpPath.has_parent_path())
        std::filesystem::create_directories(dumpPath.parent_path(), ec);

    HMODULE dbghelp = LoadLibraryA("Dbghelp.dll");
    if (!dbghelp)
        return false;

    using MiniDumpWriteDumpFn = BOOL (WINAPI*)(
        HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
        PMINIDUMP_EXCEPTION_INFORMATION,
        PMINIDUMP_USER_STREAM_INFORMATION,
        PMINIDUMP_CALLBACK_INFORMATION);
    auto miniDumpWriteDump = reinterpret_cast<MiniDumpWriteDumpFn>(
        GetProcAddress(dbghelp, "MiniDumpWriteDump"));
    if (!miniDumpWriteDump)
    {
        FreeLibrary(dbghelp);
        return false;
    }

    HANDLE file = CreateFileA(
        dumpPath.string().c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        FreeLibrary(dbghelp);
        return false;
    }

    const BOOL ok = miniDumpWriteDump(
        GetCurrentProcess(),
        GetCurrentProcessId(),
        file,
        MiniDumpNormal,
        nullptr,
        nullptr,
        nullptr);
    CloseHandle(file);
    FreeLibrary(dbghelp);
    return ok != FALSE;
}
#else
bool WriteHangMiniDump(const std::string&)
{
    return false;
}
#endif

void HangWatchdogThreadMain()
{
    while (!G.HangWatchdogStop.load(std::memory_order_acquire))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(std::max(100, G.HangWatchdogIntervalMs)));

        const unsigned long long now = NowUnixMs();
        const unsigned long long phaseUnixMs = G.HangPhaseUnixMs.load(std::memory_order_acquire);
        const unsigned long long phaseAgeMs = phaseUnixMs == 0 || now < phaseUnixMs ? 0 : now - phaseUnixMs;
        const bool stalled = G.HangThresholdMs > 0 && phaseAgeMs >= static_cast<unsigned long long>(G.HangThresholdMs);
        bool dumpWritten = false;
        if (stalled && !G.HangDumpPath.empty() && G.HangLastDumpUnixMs.load(std::memory_order_acquire) == 0)
        {
            G.HangLastDumpUnixMs.store(now, std::memory_order_release);
            dumpWritten = WriteHangMiniDump(G.HangDumpPath);
        }

        std::lock_guard<std::mutex> lock(G.HangLogMutex);
        if (!EnsureHangLogOpenLocked(G.HangWatchdogLog, G.HangWatchdogPath))
            continue;

        G.HangWatchdogLog
            << "{\"tUnixMs\":" << now
            << ",\"event\":\"watchdog\""
            << ",\"role\":\"" << (G.NetRole == Role::Host ? "host" : "client") << "\""
            << ",\"phase\":\"" << G.HangPhase.load(std::memory_order_acquire) << "\""
            << ",\"phaseEvent\":\"" << G.HangEvent.load(std::memory_order_acquire) << "\""
            << ",\"phaseAgeMs\":" << phaseAgeMs
            << ",\"stalled\":" << (stalled ? 1 : 0)
            << ",\"dumpWritten\":" << (dumpWritten ? 1 : 0)
            << ",\"instance\":" << G.HangInstance.load(std::memory_order_acquire)
            << ",\"frame\":" << G.HangFrame.load(std::memory_order_acquire)
            << ",\"logicalFrame\":" << G.HangLogicalFrame.load(std::memory_order_acquire)
            << ",\"sendFrame\":" << G.HangSendFrame.load(std::memory_order_acquire)
            << ",\"lastSent\":" << G.HangLastSentFrame.load(std::memory_order_acquire)
            << ",\"lastRecv\":" << G.HangLastRecvFrame.load(std::memory_order_acquire)
            << ",\"lead\":" << G.HangLead.load(std::memory_order_acquire)
            << ",\"localQueue\":" << G.HangLocalQueue.load(std::memory_order_acquire)
            << ",\"remoteQueue\":" << G.HangRemoteQueue.load(std::memory_order_acquire)
            << ",\"delayedQueue\":" << G.HangDelayedQueue.load(std::memory_order_acquire)
            << ",\"remoteWaitActive\":" << G.HangRemoteWaitActive.load(std::memory_order_acquire)
            << ",\"remoteWaitTarget\":" << G.HangRemoteWaitTarget.load(std::memory_order_acquire)
            << ",\"remoteWaitStartUnixMs\":" << G.HangRemoteWaitStartUnixMs.load(std::memory_order_acquire)
            << ",\"remoteWaitProgressUnixMs\":" << G.HangRemoteWaitProgressUnixMs.load(std::memory_order_acquire)
            << ",\"peerState\":" << G.HangPeerState.load(std::memory_order_acquire)
            << ",\"connectingPeerState\":" << G.HangConnectingPeerState.load(std::memory_order_acquire)
            << ",\"lastENetSendUnixMs\":" << G.HangLastENetSendUnixMs.load(std::memory_order_acquire)
            << ",\"lastENetRecvUnixMs\":" << G.HangLastENetRecvUnixMs.load(std::memory_order_acquire)
            << ",\"lastENetSendResult\":" << G.HangLastENetSendResult.load(std::memory_order_acquire)
            << ",\"lastENetServiceResult\":" << G.HangLastENetServiceResult.load(std::memory_order_acquire)
            << ",\"lastENetEventType\":" << G.HangLastENetEventType.load(std::memory_order_acquire)
            << ",\"lastENetEventData\":" << G.HangLastENetEventData.load(std::memory_order_acquire)
            << ",\"lastENetSendBytes\":" << G.HangLastENetSendBytes.load(std::memory_order_acquire)
            << ",\"arm9PC\":\"0x" << std::hex << G.HangArm9PC.load(std::memory_order_acquire)
            << "\",\"arm9LR\":\"0x" << G.HangArm9LR.load(std::memory_order_acquire)
            << "\",\"arm9SP\":\"0x" << G.HangArm9SP.load(std::memory_order_acquire)
            << "\",\"arm9CPSR\":\"0x" << G.HangArm9CPSR.load(std::memory_order_acquire)
            << "\",\"stageID\":\"0x" << G.HangStageID.load(std::memory_order_acquire)
            << "\",\"stageGroup\":\"0x" << G.HangStageGroup.load(std::memory_order_acquire)
            << "\",\"vsMode\":\"0x" << G.HangVsMode.load(std::memory_order_acquire)
            << "\",\"netState14\":\"0x" << G.HangNetState14.load(std::memory_order_acquire)
            << "\",\"netState1C\":\"0x" << G.HangNetState1C.load(std::memory_order_acquire)
            << "\",\"netState20\":\"0x" << G.HangNetState20.load(std::memory_order_acquire)
            << "\",\"netState24\":\"0x" << G.HangNetState24.load(std::memory_order_acquire)
            << "\",\"netState5C\":\"0x" << G.HangNetState5C.load(std::memory_order_acquire)
            << "\",\"netPacketTick\":\"0x" << G.HangNetPacketTick.load(std::memory_order_acquire)
            << "\",\"appFrameLength\":\"0x" << G.HangAppFrameLength.load(std::memory_order_acquire)
            << "\",\"appUpdateTask\":\"0x" << G.HangAppUpdateTask.load(std::memory_order_acquire)
            << "\",\"appSleeping\":\"0x" << G.HangAppSleeping.load(std::memory_order_acquire)
            << "\",\"stageSceneState\":\"0x" << G.HangStageSceneState.load(std::memory_order_acquire)
            << "\",\"player0Transition\":\"0x" << G.HangPlayer0Transition.load(std::memory_order_acquire)
            << "\",\"player1Transition\":\"0x" << G.HangPlayer1Transition.load(std::memory_order_acquire)
            << "\",\"gameSnapshotUnixMs\":" << std::dec << G.HangGameSnapshotUnixMs.load(std::memory_order_acquire)
            << "}\n";
        G.HangWatchdogLog.flush();
    }
}

void StartHangWatchdogIfNeeded()
{
    if (!G.HangDiagnosticsEnabled || G.HangWatchdogThreadStarted)
        return;
    G.HangWatchdogStop.store(false, std::memory_order_release);
    G.HangWatchdogThreadStarted = true;
    G.HangWatchdogThread = std::thread(HangWatchdogThreadMain);
}

void StopHangWatchdog()
{
    if (!G.HangWatchdogThreadStarted)
        return;
    G.HangWatchdogStop.store(true, std::memory_order_release);
    if (G.HangWatchdogThread.joinable())
        G.HangWatchdogThread.join();
    G.HangWatchdogThreadStarted = false;

    std::lock_guard<std::mutex> lock(G.HangLogMutex);
    if (G.HangWatchdogLog)
        G.HangWatchdogLog.close();
    if (G.HangPhaseEventsLog)
        G.HangPhaseEventsLog.close();
}

melonDS::u32 ComposeMvlSceneSettingsForStage(int stage)
{
    const melonDS::u32 clampedStage = static_cast<melonDS::u32>(std::clamp(stage, 0, 4));
    return ((0xB4u + clampedStage) << 16) | 0xFF00u;
}

int GameIndexForInstance(int instanceID)
{
    if (instanceID < 0 || instanceID >= 16)
        return 0;
    return std::max(0, G.MvlAutoRestartCount[instanceID]);
}

int MvlStageForGame(int instanceID)
{
    const int index = GameIndexForInstance(instanceID);
    if (!G.Mvl.StageSequence.empty())
        return G.Mvl.StageSequence[std::min(index, static_cast<int>(G.Mvl.StageSequence.size()) - 1)];
    if (G.Mvl.CourseMode == "random" && G.MatchSeedConfigured)
        return static_cast<int>((G.MatchSeed + static_cast<melonDS::u32>(index)) % 5u);
    return std::clamp(G.MvlCurrentStage, 0, 4);
}

void RefreshMvlGameSelectionForInstance(int instanceID)
{
    if (instanceID < 0 || instanceID >= 16)
        return;
    const int stage = std::clamp(MvlStageForGame(instanceID), 0, 4);
    G.MvlCurrentStage = stage;
    G.MvlCurrentStageSceneSettings = ComposeMvlSceneSettingsForStage(stage);
}

melonDS::u32 MatchSeedForGame(int instanceID)
{
    const int index = GameIndexForInstance(instanceID);
    if (!G.MatchSeedSequence.empty())
        return G.MatchSeedSequence[std::min(index, static_cast<int>(G.MatchSeedSequence.size()) - 1)];
    return G.MatchSeed + static_cast<melonDS::u32>(index);
}

melonDS::u32 MvlRestartPacketCutoffFrame()
{
    melonDS::u32 cutoff = 0;
    for (melonDS::u32 frame : G.MvlAutoRestartLastRestartFrame)
        cutoff = std::max(cutoff, frame);
    return cutoff;
}

void ResetMvlRuntimeSyncStateForRestart(int instanceID, melonDS::u32 frame)
{
    if (instanceID < 0 || instanceID >= 16)
        return;

    std::lock_guard<std::mutex> lock(G.Mutex);

    G.LocalGameStateHashes.clear();
    G.RemoteGameStateHashes.clear();
    G.RemoteGameStateSamples.clear();
    G.RemotePlayerStateSamples.clear();
    G.RemoteWorldStateSampleValid = false;
    G.RemoteMovingHazardStateSampleValid = false;
    G.RemoteWorldActorSnapshotSampleValid = false;
    G.RemoteWorldEffectStateSampleValid = false;
    G.PendingNSMLPackets.clear();
    G.PacketBridgePacketInputs.clear();
    G.DelayedNSMLPackets.clear();
    G.DelayedInputs.clear();
    G.LocalInputs.clear();
    G.RemoteInputs.clear();
    G.PredictedRemoteInputs.clear();
    G.LastReceivedInputFrame = kNoFrameLimit;
    G.LastSentInputFrame = kNoFrameLimit;
    G.LastTracedSentInputFrame = kNoFrameLimit;
    G.LastTracedReceivedInputFrame = kNoFrameLimit;
    G.LastInputHealthSummaryFrame = kNoFrameLimit;
    G.LastInputHealthRemoteWaitFrame = kNoFrameLimit;
    G.LastInputHealthThrottleFrame = kNoFrameLimit;
    G.LastInputHealthThrottleResolvedFrame = kNoFrameLimit;
    G.LastInputHealthReceiveGapFrame = kNoFrameLimit;
    G.LastInputHealthSendGapFrame = kNoFrameLimit;
    G.LastInputFrameThrottleTraceFrame = kNoFrameLimit;
    G.HangRemoteWaitActive.store(0, std::memory_order_release);
    G.HangRemoteWaitTarget.store(0, std::memory_order_release);
    G.HangLastSentFrame.store(kNoFrameLimit, std::memory_order_release);
    G.HangLastRecvFrame.store(kNoFrameLimit, std::memory_order_release);
    G.HangLocalQueue.store(0, std::memory_order_release);
    G.HangRemoteQueue.store(0, std::memory_order_release);
    G.HangDelayedQueue.store(0, std::memory_order_release);
    G.NetplayStartReadySent = false;
    G.NetplayStartReadySendCount = 0;
    G.LastNetplayStartReadySentAt = {};
    G.LastInputFrameLeadResendAt = {};
    G.InputFrameLeadResendCount = 0;
    G.WaitedForPeerAtNetplayStart = false;
    G.LocalNetplayStartReadyFrame = kNoFrameLimit;
    G.RemoteNetplayStartReadyFrame = kNoFrameLimit;
    G.RemoteNetplayStartReadyAfterLocal = false;
    G.InputEpochPrimedStartFrame = kNoFrameLimit;

    G.LastLoggedGameStateFrame[instanceID] = 0;
    G.LastSentGameStateFrame[instanceID] = 0;
    G.LastSentPlayerStateFrame[instanceID] = 0;
    G.LastSentWorldStateFrame[instanceID] = 0;
    for (int player = 0; player < 2; player++)
    {
        G.LastAppliedPlayerGlobalsFrame[instanceID][player] = 0;
        G.PlayerActorBaseCache[instanceID][player] = 0;
        G.PlayerActorGUIDCache[instanceID][player] = 0;
    }

    G.WorldStarActorBaseCache[instanceID] = 0;
    G.WorldStarActorGUIDCache[instanceID] = 0;
    G.LastSpawnedWorldItemRemoteGUID[instanceID] = 0;
    G.LastConfirmedWorldItemRemoteGUID[instanceID] = 0;
    G.PendingWorldItemRemoteGUID[instanceID] = 0;
    G.PendingWorldItemFirstMissingFrame[instanceID] = 0;
    G.LastSpawnedNeutralWorldItemRemoteGUID[instanceID] = 0;
    G.LastConfirmedNeutralWorldItemRemoteGUID[instanceID] = 0;
    G.PendingNeutralWorldItemRemoteGUID[instanceID] = 0;
    G.PendingNeutralWorldItemFirstMissingFrame[instanceID] = 0;
    G.LastSpawnedDroppedStarItemRemoteGUID[instanceID] = 0;
    G.LastConfirmedDroppedStarItemRemoteGUID[instanceID] = 0;
    G.PendingDroppedStarItemRemoteGUID[instanceID] = 0;
    G.PendingDroppedStarItemFirstMissingFrame[instanceID] = 0;
    G.WorldMovingHazardBaseCache[instanceID] = 0;
    G.WorldMovingHazardGUIDCache[instanceID] = 0;
    G.WorldMovingHazardCacheCounts[instanceID] = 0;
    std::fill(std::begin(G.WorldMovingHazardBaseCaches[instanceID]), std::end(G.WorldMovingHazardBaseCaches[instanceID]), 0);
    std::fill(std::begin(G.WorldMovingHazardGUIDCaches[instanceID]), std::end(G.WorldMovingHazardGUIDCaches[instanceID]), 0);
    std::fill(std::begin(G.WorldMovingHazardRemoteGUIDMaps[instanceID]), std::end(G.WorldMovingHazardRemoteGUIDMaps[instanceID]), 0);
    std::fill(std::begin(G.WorldMovingHazardLocalGUIDMaps[instanceID]), std::end(G.WorldMovingHazardLocalGUIDMaps[instanceID]), 0);
    std::fill(std::begin(G.WorldActorSnapshotRemoteGUIDMaps[instanceID]), std::end(G.WorldActorSnapshotRemoteGUIDMaps[instanceID]), 0);
    std::fill(std::begin(G.WorldActorSnapshotLocalGUIDMaps[instanceID]), std::end(G.WorldActorSnapshotLocalGUIDMaps[instanceID]), 0);

    std::printf("NSMB MvL auto restart: reset sync caches inst=%d frame=%u cutoff=%u\n",
        instanceID,
        frame,
        MvlRestartPacketCutoffFrame());
    std::fflush(stdout);
}

melonDS::u32 MvlAutoRestartRelativeStartupFrame(melonDS::u32 value)
{
    if (value == 0 || value == kNoFrameLimit)
        return value;
    if (G.MvlAutoRestartStartupFrameBase != 0 && value >= G.MvlAutoRestartStartupFrameBase)
        return value - G.MvlAutoRestartStartupFrameBase;
    return value;
}

void RebaseMvlAutoRestartStartupFrame(melonDS::u32 restartFrame, melonDS::u32& value)
{
    if (value == 0 || value == kNoFrameLimit)
        return;
    value = restartFrame + MvlAutoRestartRelativeStartupFrame(value);
}

void RebaseMvlAutoRestartStartupFrames(int instanceID, melonDS::u32 restartFrame)
{
    if (instanceID < 0 || instanceID >= 16)
        return;

    RebaseMvlAutoRestartStartupFrame(restartFrame, G.Connection.StartFrame);
    RebaseMvlAutoRestartStartupFrame(restartFrame, G.PacketBridge.WaitStartFrame);
    RebaseMvlAutoRestartStartupFrame(restartFrame, G.PacketBridge.ForceTickStartFrame);
    RebaseMvlAutoRestartStartupFrame(restartFrame, G.PacketBridge.ForceNetReadyStartFrame);
    RebaseMvlAutoRestartStartupFrame(restartFrame, G.PacketBridge.ForceNetReadyEndFrame);
    RebaseMvlAutoRestartStartupFrame(restartFrame, G.PacketBridge.ForceGameLocalPlayerIDStartFrame);
    RebaseMvlAutoRestartStartupFrame(restartFrame, G.PacketBridge.ThrottleStartFrame);
    RebaseMvlAutoRestartStartupFrame(restartFrame, G.Input.SendDelayStartFrame);
    RebaseMvlAutoRestartStartupFrame(restartFrame, G.Input.SendDelayEndFrame);
    RebaseMvlAutoRestartStartupFrame(restartFrame, G.Mvl.DirectBootFrame);
    RebaseMvlAutoRestartStartupFrame(restartFrame, G.RuntimePatch.ScriptRemotePacketStartFrame);
    RebaseMvlAutoRestartStartupFrame(restartFrame, G.RuntimePatch.ScriptRemotePacketEndFrame);
    RebaseMvlAutoRestartStartupFrame(restartFrame, G.RuntimePatch.PacketBridgeJitHelperPatchFrame);
    RebaseMvlAutoRestartStartupFrame(restartFrame, G.ClearMvlCameraInitHoldStartFrame);
    RebaseMvlAutoRestartStartupFrame(restartFrame, G.ClearMvlCameraInitHoldEndFrame);

    G.MvlAutoRestartStartupFrameBase = restartFrame;
    std::printf(
        "NSMB MvL auto restart: rebased startup frames inst=%d restartFrame=%u directBoot=%u netplayStart=%u packetJit=%u\n",
        instanceID,
        restartFrame,
        G.Mvl.DirectBootFrame,
        G.Connection.StartFrame,
        G.RuntimePatch.PacketBridgeJitHelperPatchFrame);
    std::fflush(stdout);
}

void RebaseMvlAutoRestartStartupFrameFromCheckpoint(
    melonDS::u32 restoreFrame,
    melonDS::u32 checkpointFrame,
    melonDS::u32& value)
{
    if (value == 0 || value == kNoFrameLimit)
        return;

    const melonDS::u32 relative = MvlAutoRestartRelativeStartupFrame(value);
    if (relative == 0 || relative == kNoFrameLimit)
    {
        value = relative;
        return;
    }

    value = restoreFrame + (relative > checkpointFrame ? relative - checkpointFrame : 1u);
}

void RebaseMvlAutoRestartStartupFramesFromCheckpoint(
    int instanceID,
    melonDS::u32 restoreFrame,
    melonDS::u32 checkpointFrame)
{
    if (instanceID < 0 || instanceID >= 16)
        return;

    RebaseMvlAutoRestartStartupFrameFromCheckpoint(restoreFrame, checkpointFrame, G.Connection.StartFrame);
    RebaseMvlAutoRestartStartupFrameFromCheckpoint(restoreFrame, checkpointFrame, G.PacketBridge.WaitStartFrame);
    RebaseMvlAutoRestartStartupFrameFromCheckpoint(restoreFrame, checkpointFrame, G.PacketBridge.ForceTickStartFrame);
    RebaseMvlAutoRestartStartupFrameFromCheckpoint(restoreFrame, checkpointFrame, G.PacketBridge.ForceNetReadyStartFrame);
    RebaseMvlAutoRestartStartupFrameFromCheckpoint(restoreFrame, checkpointFrame, G.PacketBridge.ForceNetReadyEndFrame);
    RebaseMvlAutoRestartStartupFrameFromCheckpoint(restoreFrame, checkpointFrame, G.PacketBridge.ForceGameLocalPlayerIDStartFrame);
    RebaseMvlAutoRestartStartupFrameFromCheckpoint(restoreFrame, checkpointFrame, G.PacketBridge.ThrottleStartFrame);
    RebaseMvlAutoRestartStartupFrameFromCheckpoint(restoreFrame, checkpointFrame, G.Input.SendDelayStartFrame);
    RebaseMvlAutoRestartStartupFrameFromCheckpoint(restoreFrame, checkpointFrame, G.Input.SendDelayEndFrame);
    RebaseMvlAutoRestartStartupFrameFromCheckpoint(restoreFrame, checkpointFrame, G.Mvl.DirectBootFrame);
    RebaseMvlAutoRestartStartupFrameFromCheckpoint(restoreFrame, checkpointFrame, G.RuntimePatch.ScriptRemotePacketStartFrame);
    RebaseMvlAutoRestartStartupFrameFromCheckpoint(restoreFrame, checkpointFrame, G.RuntimePatch.ScriptRemotePacketEndFrame);
    RebaseMvlAutoRestartStartupFrameFromCheckpoint(restoreFrame, checkpointFrame, G.RuntimePatch.PacketBridgeJitHelperPatchFrame);
    RebaseMvlAutoRestartStartupFrameFromCheckpoint(restoreFrame, checkpointFrame, G.ClearMvlCameraInitHoldStartFrame);
    RebaseMvlAutoRestartStartupFrameFromCheckpoint(restoreFrame, checkpointFrame, G.ClearMvlCameraInitHoldEndFrame);

    G.MvlAutoRestartStartupFrameBase = restoreFrame > checkpointFrame ? restoreFrame - checkpointFrame : restoreFrame;
    std::printf(
        "NSMB MvL auto restart: rebased startup frames from checkpoint inst=%d restoreFrame=%u checkpointFrame=%u directBoot=%u netplayStart=%u packetJit=%u\n",
        instanceID,
        restoreFrame,
        checkpointFrame,
        G.Mvl.DirectBootFrame,
        G.Connection.StartFrame,
        G.RuntimePatch.PacketBridgeJitHelperPatchFrame);
    std::fflush(stdout);
}

void ResetMvlAutoRestartStartupHookState(int instanceID)
{
    if (instanceID < 0 || instanceID >= 16)
        return;

    G.DirectMvlBootApplied[instanceID] = false;
    G.PacketBridgeJitHelperPatchApplied[instanceID] = false;
    G.PacketBridgeJitHelperPatchResumeFrame[instanceID] = 0;
    G.LastPacketBridgeForcedTickFrame[instanceID] = 0;
    G.ScriptRemotePacketLogged[instanceID] = false;
    G.ClearMvlCameraInitHoldApplied[instanceID] = false;
    G.LocalNetplayStartReadyFrame = kNoFrameLimit;
    G.RemoteNetplayStartReadyFrame = kNoFrameLimit;
    G.RemoteNetplayStartReadyAfterLocal = false;
    G.InputEpochPrimedStartFrame = kNoFrameLimit;
    std::fill(std::begin(G.NetplayStartWaitArrived), std::end(G.NetplayStartWaitArrived), false);
    G.NetplayStartWaitComplete = false;
    G.WaitedForPeerAtNetplayStart = false;
    G.NetplayStartReadySent = false;
    G.NetplayStartReadySendCount = 0;
    G.LastNetplayStartReadySentAt = {};
    G.LastInputFrameLeadResendAt = {};
    G.InputFrameLeadResendCount = 0;
    G.NetplayLockstepStarted[instanceID] = false;
    G.NetplayAnyLockstepStarted = false;
}

void ApplyMvlRuntimeConfigIfNeeded(melonDS::NDS* nds)
{
    if (!G.Mvl.RuntimeConfigEnabled || !nds)
        return;

    nds->ARM9Write32(kMvlRuntimeConfigAddr, kMvlRuntimeConfigMagic);
    nds->ARM9Write32(kMvlRuntimeConfigAddr + kMvlRuntimeConfigStageOffset,
        static_cast<melonDS::u32>(G.MvlCurrentStage));
    nds->ARM9Write32(kMvlRuntimeConfigAddr + kMvlRuntimeConfigSceneSettingsOffset,
        G.MvlCurrentStageSceneSettings);
    nds->ARM9Write32(kMvlRuntimeConfigAddr + kMvlRuntimeConfigInitialLivesOffset,
        G.Mvl.InitialLives);
    nds->ARM9Write32(kMvlRuntimeConfigAddr + kMvlRuntimeConfigLifeModeSelectorOffset,
        G.Mvl.LifeModeSelector);
    nds->ARM9Write32(kMvlRuntimeConfigAddr + kMvlRuntimeConfigBigStarSelectorOffset,
        G.Mvl.BigStarSelector);
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

InputState ConvertStockXToTouch(InputState input)
{
    if ((input.KeyMask & kInputKeyXMask) != 0)
        return input;

    input.KeyMask |= kInputKeyXMask;
    if (!input.Touching)
    {
        input.Touching = true;
        input.TouchX = kMvlStockItemTouchX;
        input.TouchY = kMvlStockItemTouchY;
    }
    return input;
}

bool InputsEqual(const InputState& a, const InputState& b)
{
    return a.KeyMask == b.KeyMask
        && a.Touching == b.Touching
        && a.TouchX == b.TouchX
        && a.TouchY == b.TouchY;
}

std::string InputSpecForScript(const InputState& input)
{
    static constexpr const char* kButtonNames[12] {
        "A",
        "B",
        "SELECT",
        "START",
        "RIGHT",
        "LEFT",
        "UP",
        "DOWN",
        "R",
        "L",
        "X",
        "Y",
    };

    std::string spec;
    for (int bit = 0; bit < 12; bit++)
    {
        if ((input.KeyMask & (1u << bit)) != 0)
            continue;
        if (!spec.empty())
            spec += "+";
        spec += kButtonNames[bit];
    }
    if (spec.empty())
        spec = "NONE";
    if (input.Touching)
    {
        spec += " ";
        spec += std::to_string(input.TouchX);
        spec += ",";
        spec += std::to_string(input.TouchY);
    }
    return spec;
}

InputState CanonicalInputForRecord(const InputState& input)
{
    InputState canonical = input;
    canonical.KeyMask &= 0xFFF;
    return canonical;
}

void FlushInputRecordSpanLocked()
{
    if (!G.InputRecord || !G.InputRecordHasSpan)
        return;

    G.InputRecord << G.InputRecordSpanStart << "-" << G.InputRecordSpanEnd << " "
        << InputSpecForScript(G.InputRecordSpanInput) << "\n";
    G.InputRecordHasSpan = false;
    G.InputRecordFlushPendingSpans++;
    if (G.InputRecordFlushPendingSpans >= 64)
    {
        G.InputRecord.flush();
        G.InputRecordFlushPendingSpans = 0;
    }
}

void RecordInputIfNeeded(int instanceID, melonDS::u32 frame, const InputState& input)
{
    if (!G.InputRecordEnabled)
        return;
    if (G.InputRecordInstance >= 0 && G.InputRecordInstance != instanceID)
        return;
    if (frame < G.InputRecordStartFrame)
        return;
    if (G.InputRecordEndFrame != kNoFrameLimit && frame > G.InputRecordEndFrame)
        return;

    std::lock_guard<std::mutex> lock(G.InputRecordMutex);
    if (!G.InputRecord)
        return;

    const InputState recordInput = CanonicalInputForRecord(input);
    if (!G.InputRecordHasSpan)
    {
        G.InputRecordSpanStart = frame;
        G.InputRecordSpanEnd = frame;
        G.InputRecordSpanInput = recordInput;
        G.InputRecordHasSpan = true;
        return;
    }

    if (G.InputRecordSpanEnd + 1 == frame && InputsEqual(G.InputRecordSpanInput, recordInput))
    {
        G.InputRecordSpanEnd = frame;
        return;
    }
    if (frame <= G.InputRecordSpanEnd)
    {
        if (!InputsEqual(G.InputRecordSpanInput, recordInput)
            && G.InputRecordSpanStart == frame
            && G.InputRecordSpanEnd == frame)
        {
            G.InputRecordSpanInput = recordInput;
        }
        return;
    }

    FlushInputRecordSpanLocked();
    G.InputRecordSpanStart = frame;
    G.InputRecordSpanEnd = frame;
    G.InputRecordSpanInput = recordInput;
    G.InputRecordHasSpan = true;
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

bool ParseU32(const std::string& text, melonDS::u32& out)
{
    char* end = nullptr;
    unsigned long value = std::strtoul(text.c_str(), &end, 0);
    if (!end || *end != '\0') return false;
    out = static_cast<melonDS::u32>(value);
    return true;
}

bool LoadInputScriptFileLocked(
    const std::string& path,
    std::vector<InputTimeline::InputSpan>& spans)
{
    if (path.empty()) return true;

    InputTimeline::ParseError error;
    if (!InputTimeline::LoadInputScriptFile(path, spans, error))
    {
        const char* message = "invalid input line";
        switch (error.Kind)
        {
        case InputTimeline::ParseErrorKind::Open:
            std::printf("NSMB Test: failed to open input script: %s\n", path.c_str());
            return false;
        case InputTimeline::ParseErrorKind::Target:
            message = "invalid input target";
            break;
        case InputTimeline::ParseErrorKind::Range:
            message = "invalid range";
            break;
        case InputTimeline::ParseErrorKind::Touch:
            message = "invalid touch";
            break;
        case InputTimeline::ParseErrorKind::Input:
        case InputTimeline::ParseErrorKind::None:
            break;
        }
        std::printf("NSMB Test: %s at %s:%d\n", message, path.c_str(), error.Line);
        return false;
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

melonDS::u64 GameStateKey(int instanceID, melonDS::u32 frame)
{
    return (static_cast<melonDS::u64>(static_cast<melonDS::u32>(instanceID)) << 32) | frame;
}

melonDS::u64 PlayerStateKey(melonDS::u32 player, melonDS::u32 frame)
{
    return (static_cast<melonDS::u64>(player) << 32) | frame;
}

std::string JsonEscape(const std::string& value)
{
    std::ostringstream out;
    for (char ch : value)
    {
        switch (ch)
        {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20)
            {
                out << "\\u"
                    << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
                    << static_cast<int>(static_cast<unsigned char>(ch))
                    << std::dec << std::nouppercase << std::setfill(' ');
            }
            else
            {
                out << ch;
            }
            break;
        }
    }
    return out.str();
}

std::string Hex64(melonDS::u64 value)
{
    std::ostringstream out;
    out << std::hex << std::uppercase << std::setw(16) << std::setfill('0')
        << static_cast<unsigned long long>(value);
    return out.str();
}

void WriteDiagnosticsJson(const std::string& json)
{
    if (G.DiagnosticsPath.empty())
        return;

    const std::filesystem::path path(G.DiagnosticsPath);
    const std::filesystem::path tmp = path.string() + ".tmp";
    std::error_code ec;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), ec);

    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            std::printf("NSMB PoC: failed to open diagnostics file: %s\n", tmp.string().c_str());
            return;
        }
        file << json;
        file.flush();
        if (!file)
        {
            std::printf("NSMB PoC: failed to write diagnostics file: %s\n", tmp.string().c_str());
            return;
        }
    }

    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tmp, path, ec);
    if (ec)
    {
        std::printf("NSMB PoC: failed to publish diagnostics file: %s error=%s\n",
            path.string().c_str(),
            ec.message().c_str());
    }
}

void WriteGameStateMismatchDiagnostics(
    int instanceID,
    melonDS::u32 frame,
    const GameStateSyncHashes& local,
    const GameStateSyncHashes& remote)
{
    const melonDS::u64 localHash = CombinedGameStateHash(local);
    const melonDS::u64 remoteHash = CombinedGameStateHash(remote);
    const bool basicMatches = local.Basic == remote.Basic;
    const bool playerGlobalMatches = local.PlayerGlobal == remote.PlayerGlobal;
    const bool wifiCandidateMatches = local.WifiCandidate == remote.WifiCandidate;
    const bool renderCandidateMatches = local.RenderCandidate == remote.RenderCandidate;

    std::ostringstream line;
    line << "NSMB PoC: game state mismatch inst=" << instanceID
         << " frame=" << frame
         << " local=" << Hex64(localHash)
         << " remote=" << Hex64(remoteHash)
         << " basic=" << (basicMatches ? 1 : 0)
         << " playerGlobal=" << (playerGlobalMatches ? 1 : 0)
         << " wifiCandidate=" << (wifiCandidateMatches ? 1 : 0)
         << " renderCandidate=" << (renderCandidateMatches ? 1 : 0);

    std::ostringstream json;
    json << "{\n"
         << "  \"game_state_mismatch\": {\n"
         << "    \"instance\": " << instanceID << ",\n"
         << "    \"frame\": " << frame << ",\n"
         << "    \"local_hash\": \"" << Hex64(localHash) << "\",\n"
         << "    \"remote_hash\": \"" << Hex64(remoteHash) << "\",\n"
         << "    \"basic_matches\": " << (basicMatches ? "true" : "false") << ",\n"
         << "    \"player_global_matches\": " << (playerGlobalMatches ? "true" : "false") << ",\n"
         << "    \"wifi_candidate_matches\": " << (wifiCandidateMatches ? "true" : "false") << ",\n"
         << "    \"render_candidate_matches\": " << (renderCandidateMatches ? "true" : "false") << ",\n"
         << "    \"line\": \"" << JsonEscape(line.str()) << "\"\n"
         << "  }\n"
         << "}\n";
    WriteDiagnosticsJson(json.str());
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
    WriteGameStateMismatchDiagnostics(instanceID, frame, lhs, rhs);
    EmitGameStateMismatchEventLocked(instanceID, frame, lhs, rhs);
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

InputState ApplyInputScript(int instanceID, melonDS::u32 frame, const InputState& fallback)
{
    if (!G.TestEnabled) return fallback;
    return InputTimeline::Apply(GInputScript, instanceID, frame, fallback);
}

InputState ApplyScriptRemotePacketInputScript(int instanceID, melonDS::u32 frame, const InputState& fallback)
{
    if (!G.TestEnabled) return fallback;
    if (GScriptRemotePacketInputScript.empty())
        return ApplyInputScript(instanceID, frame, fallback);
    return InputTimeline::Apply(GScriptRemotePacketInputScript, instanceID, frame, fallback);
}

NsmbRuleAI::Config RuleAIConfig()
{
    NsmbRuleAI::Config config {};
    config.Enabled = G.AI.Rule.Enabled;
    config.PlayerSpec = G.AI.Rule.PlayerSpec;
    config.StartFrame = G.AI.Rule.StartFrame;
    config.HorizontalDeadzone = G.AI.Rule.HorizontalDeadzone;
    config.HorizontalWrapWidth = G.AI.Rule.HorizontalWrapWidth;
    config.CloseRange = G.AI.Rule.CloseRange;
    config.HazardHorizontalRange = G.AI.Rule.HazardHorizontalRange;
    config.HazardVerticalRange = G.AI.Rule.HazardVerticalRange;
    config.JumpInterval = G.AI.Rule.JumpInterval;
    config.JumpFrames = G.AI.Rule.JumpFrames;
    config.WallEscapeFrames = 36;
    config.StuckFrames = 24;
    config.TraceEnabled = G.AI.Rule.TraceEnabled;
    config.TraceInterval = G.AI.Rule.TraceInterval;
    return config;
}

bool RuleAIProvidesInputForPlayer(int player)
{
    if (!G.AI.Rule.Enabled)
        return false;
    if (G.AI.Rule.HostOnly && G.NetRole != Role::Host)
        return false;
    if (G.AI.Rule.ClientOnly && G.NetRole != Role::Client)
        return false;
    return NsmbRuleAI::ControlsPlayer(
        RuleAIConfig(),
        player,
        CurrentPacketBridgeLocalPlayer());
}

bool ImitationAIProvidesInputForPlayer(int player)
{
    if (!G.ImitationAIEnabled || !G.ImitationAIModelLoaded)
        return false;
    if (G.AI.Imitation.HostOnly && G.NetRole != Role::Host)
        return false;
    if (G.AI.Imitation.ClientOnly && G.NetRole != Role::Client)
        return false;

    NsmbRuleAI::Config config {};
    config.Enabled = true;
    config.PlayerSpec = G.AI.Imitation.PlayerSpec;
    return NsmbRuleAI::ControlsPlayer(
        config,
        player,
        CurrentPacketBridgeLocalPlayer());
}

void RecordAIPlayLogAppliedInput(int instanceID, melonDS::u32 frame, int player, const InputState& input)
{
    if (instanceID < 0 || instanceID >= 16 || player < 0 || player >= 2)
        return;
    G.AIPlayLogLastAppliedInput[instanceID][player] = input;
    G.AIPlayLogLastAppliedInputFrame[instanceID][player] = frame;
    G.AIPlayLogLastAppliedInputValid[instanceID][player] = true;
}

InputState ApplyRuleBasedAIInput(
    int instanceID,
    melonDS::u32 frame,
    melonDS::NDS* nds,
    int player,
    const InputState& fallback)
{
    if (!G.AI.Rule.Enabled || frame < G.AI.Rule.StartFrame || !nds || !nds->MainRAM)
        return fallback;
    if (G.AI.Rule.HostOnly && G.NetRole != Role::Host)
        return fallback;
    if (G.AI.Rule.ClientOnly && G.NetRole != Role::Client)
        return fallback;
    const NsmbRuleAI::Config config = RuleAIConfig();
    const int localPlayer = CurrentPacketBridgeLocalPlayer();
    if (!NsmbRuleAI::ControlsPlayer(config, player, localPlayer))
        return fallback;
    const bool inGameplay = IsMarioVsLuigiGameplay(nds);
    if (!inGameplay)
        return fallback;
    const GameStateSample sample = ReadGameStateSample(nds);
    const GameStateObjectScanCache objectScanCache = BuildGameStateObjectScanCache(nds);
    NsmbRuleAI::FrameStateServices frameStateServices {};
    frameStateServices.ObjectCategory = AIObjectCategory;
    frameStateServices.DeriveTerrainSummary = DeriveAITerrainSummaryFromGrid;
    frameStateServices.TargetHasFloorBelow = AITerrainTargetHasFloorBelow;
    return NsmbRuleAI::DecideInput(
        config,
        NsmbRuleAI::BuildFrameState(
            config,
            sample,
            objectScanCache,
            inGameplay,
            frameStateServices),
        instanceID,
        frame,
        player,
        localPlayer,
        fallback);
}

void FlushDelayedInputsLocked(melonDS::u32 frame);
void PrintInputHealthLineLocked(
    const char* event,
    melonDS::u32 frame,
    melonDS::u32 logicalFrame,
    melonDS::u32 sendFrame,
    unsigned long long waitedUs,
    unsigned long long throttleUs,
    unsigned long long networkUs,
    int lead,
    bool hasRemoteInput,
    bool predictedRemoteInput);
int CurrentInputLeadLocked(melonDS::u32 sendFrame);

void StoreRemoteInputLocked(melonDS::u32 frame, const InputState& receivedInput, melonDS::u32 localFrame)
{
    if (G.Input.NetplayOnly && G.Connection.StartFrame != 0 && frame < G.Connection.StartFrame)
    {
        if (G.Input.NetplayTrace && G.LastTracedReceivedInputFrame != frame)
        {
            G.LastTracedReceivedInputFrame = frame;
            std::printf("NSMB InputNetplay: ignored old input frame=%u currentStart=%u\n",
                frame,
                G.Connection.StartFrame);
        }
        return;
    }

    if (G.Rollback.Enabled)
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
            const bool frameAlreadySimulated = localFrame == kNoFrameLimit || frame < localFrame;
            if (frameAlreadySimulated
                && (G.PendingRollbackFrame == kNoFrameLimit
                    || frame < G.PendingRollbackFrame))
            {
                G.PendingRollbackFrame = frame;
                G.PendingRollbackObservedFrame = observedFrame;
            }
            else if (frameAlreadySimulated && G.PendingRollbackObservedFrame == kNoFrameLimit)
            {
                G.PendingRollbackObservedFrame = observedFrame;
            }
            G.PredictedRemoteInputs.erase(G.PredictedRemoteInputs.lower_bound(frame),
                G.PredictedRemoteInputs.end());
            if (G.Input.NetplayTrace)
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
                if (!frameAlreadySimulated)
                {
                    std::printf(
                        "NSMB Rollback: current/future mismatch applied without rollback frame=%u localFrame=%u\n",
                        frame,
                        localFrame);
                }
                std::fflush(stdout);
            }
        }
        G.LastConfirmedRemoteInput = receivedInput;
        G.LastConfirmedRemoteInputValid = true;
    }
    const melonDS::u32 previousLastReceived = G.LastReceivedInputFrame;
    G.RemoteInputs[frame] = receivedInput;
    if (G.LastReceivedInputFrame == kNoFrameLimit || frame > G.LastReceivedInputFrame)
        G.LastReceivedInputFrame = frame;
    UpdateHangNetplaySnapshotLocked(G.LastSentInputFrame == kNoFrameLimit ? frame : G.LastSentInputFrame);
    if (G.Input.HealthTrace
        && previousLastReceived != kNoFrameLimit
        && frame > previousLastReceived + 1
        && G.LastInputHealthReceiveGapFrame != frame)
    {
        G.LastInputHealthReceiveGapFrame = frame;
        PrintInputHealthLineLocked(
            "recv-gap",
            localFrame,
            frame,
            G.LastSentInputFrame,
            0,
            0,
            0,
            CurrentInputLeadLocked(G.LastSentInputFrame == kNoFrameLimit ? frame : G.LastSentInputFrame),
            true,
            false);
    }
    G.InputCond.notify_all();
    if ((G.Bootstrap.InputTraceEnabled || G.Input.NetplayTrace)
        && frame != G.LastTracedReceivedInputFrame
        && (G.Bootstrap.InputTraceInterval <= 1 || (frame % static_cast<melonDS::u32>(G.Bootstrap.InputTraceInterval)) == 0))
    {
        G.LastTracedReceivedInputFrame = frame;
        std::printf("NSMB PoC: recv input tUnixMs=%llu frame=%u keys=0x%03X remoteQueue=%zu lastSent=%u lead=%d localFrame=%u\n",
            NowUnixMs(),
            frame,
            receivedInput.KeyMask,
            G.RemoteInputs.size(),
            G.LastSentInputFrame,
            CurrentInputLeadLocked(G.LastSentInputFrame == kNoFrameLimit ? frame : G.LastSentInputFrame),
            localFrame);
        std::fflush(stdout);
    }
}

void HandleReceivedInputLocked(const void* data, std::size_t size, melonDS::u32 localFrame)
{
    InputProtocol::FramedInput packet;
    if (InputProtocol::DecodeInput(data, size, packet))
        StoreRemoteInputLocked(packet.Frame, packet.Input, localFrame);
}

void HandleReceivedInputBundleLocked(const void* data, std::size_t size, melonDS::u32 localFrame)
{
    std::vector<InputProtocol::FramedInput> entries;
    if (!InputProtocol::DecodeInputBundle(data, size, entries))
        return;
    for (const InputProtocol::FramedInput& entry : entries)
        StoreRemoteInputLocked(entry.Frame, entry.Input, localFrame);
}

void HandleReceivedSessionLocked(const void* data, std::size_t size, melonDS::u32 localFrame)
{
    SessionProtocol::Message message;
    if (!SessionProtocol::Decode(data, size, message))
        return;

    if (message.Kind == SessionProtocol::MessageKind::MatchSeed)
    {
        G.MatchSeed = message.Value;
        G.MatchSeedConfigured = true;
        G.InputCond.notify_all();
        if (G.Harness.StateLoadDir.empty() && !G.PacketBridge.Only)
        {
            G.NetRandomPatchValue = message.Value;
            G.NetRandomPatchEnabled = true;
            G.NetRandomPatchAuto = true;
        }
        std::printf("NSMB PoC: received match seed 0x%08X\n", message.Value);
        return;
    }

    if (SessionPolicy::IsOldStartReady(
            G.Input.NetplayOnly,
            G.Connection.StartFrame,
            message.Value))
    {
        std::printf("NSMB InputNetplay: ignored old start ready frame=%u currentStart=%u\n",
            message.Value,
            G.Connection.StartFrame);
        return;
    }

    G.RemoteNetplayStartReadyFrame = message.Value;
    if (G.LocalNetplayStartReadyFrame != kNoFrameLimit)
        G.RemoteNetplayStartReadyAfterLocal = true;
    EmitStartReadyEventLocked("recv", localFrame, message.Value);
    G.InputCond.notify_all();
    std::printf("NSMB InputNetplay: received start ready frame=%u\n", message.Value);
}

enum class ReceiveDisposition
{
    CleanupPacket,
    SkipPacketCleanup,
};

void HandleReceivedNSMLPacketLocked(
    const void* data,
    std::size_t size,
    melonDS::NDS* nds,
    melonDS::u32 localFrame)
{
    WireNSMLPacket packet;
    std::memcpy(&packet, data, size);
    if (packet.Magic != kMagic || packet.Version != kVersion
        || packet.Kind != kWireKindPacket || packet.Player > 1)
    {
        return;
    }

    const melonDS::u32 restartCutoff = MvlRestartPacketCutoffFrame();
    if (restartCutoff != 0 && packet.Frame <= restartCutoff)
        return;
    if (G.PacketBridge.Enabled && nds)
        melonDS::NSML_PushMarioVsLuigiRemotePacket(nds, packet.Player, packet.Packet);
    else
        G.PendingNSMLPackets.push_back(packet);
    const bool newTick = packet.Tick != G.LastReceivedNSMLPacketTick[packet.Player];
    G.LastReceivedNSMLPacketTick[packet.Player] = packet.Tick;
    G.LastReceivedNSMLPacketFrame[packet.Player] = packet.Frame;
    if (G.PacketBridge.TraceEnabled && newTick)
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

ReceiveDisposition HandleReceivedPlayerStateLocked(
    const void* data,
    std::size_t size,
    melonDS::u32 localFrame)
{
    WirePlayerState packet;
    std::memcpy(&packet, data, size);
    if (packet.Magic != kMagic || packet.Version != kVersion
        || packet.Kind != kWireKindPlayerState || packet.Player > 1)
    {
        return ReceiveDisposition::CleanupPacket;
    }

    const melonDS::u32 restartCutoff = MvlRestartPacketCutoffFrame();
    if (restartCutoff != 0 && packet.Frame <= restartCutoff)
        return ReceiveDisposition::SkipPacketCleanup;
    G.RemotePlayerStateSamples[PlayerStateKey(packet.Player, packet.Frame)] = packet;
    while (G.RemotePlayerStateSamples.size() > 240)
        G.RemotePlayerStateSamples.erase(G.RemotePlayerStateSamples.begin());
    if ((G.Bootstrap.InputTraceEnabled || G.Input.NetplayTrace)
        && (G.Bootstrap.InputTraceInterval <= 1
            || (localFrame != kNoFrameLimit
                && (localFrame % static_cast<melonDS::u32>(G.Bootstrap.InputTraceInterval)) == 0)))
    {
        std::printf("NSMB PlayerState: recv localFrame=%u packetFrame=%u player=%u found=%u pos=%08X/%08X vel=%08X/%08X stored=%zu\n",
            localFrame,
            packet.Frame,
            packet.Player,
            packet.Found,
            packet.PosX,
            packet.PosY,
            packet.VelX,
            packet.VelY,
            G.RemotePlayerStateSamples.size());
    }
    return ReceiveDisposition::CleanupPacket;
}

ReceiveDisposition HandleReceivedWorldStateLocked(
    const void* data,
    std::size_t size,
    melonDS::u32 localFrame)
{
    WireWorldState packet;
    std::memcpy(&packet, data, size);
    if (packet.Magic != kMagic || packet.Version != kVersion || packet.Kind != kWireKindWorldState)
        return ReceiveDisposition::CleanupPacket;

    const melonDS::u32 restartCutoff = MvlRestartPacketCutoffFrame();
    if (restartCutoff != 0 && packet.Frame <= restartCutoff)
        return ReceiveDisposition::SkipPacketCleanup;
    if (!G.RemoteWorldStateSampleValid || packet.Frame >= G.RemoteWorldStateSample.Frame)
    {
        G.RemoteWorldStateSample = packet;
        G.RemoteWorldStateSampleValid = true;
    }
    if ((G.Bootstrap.InputTraceEnabled || G.Input.NetplayTrace)
        && (G.Bootstrap.InputTraceInterval <= 1
            || (localFrame != kNoFrameLimit
                && (localFrame % static_cast<melonDS::u32>(G.Bootstrap.InputTraceInterval)) == 0)))
    {
        std::printf("NSMB WorldState: recv localFrame=%u packetFrame=%u star=%u neutralItem=%u item=%u droppedItem=%u hazard=%u hazardPos=%08X/%08X\n",
            localFrame,
            packet.Frame,
            packet.Star.Found,
            packet.NeutralItem.Found,
            packet.Item.Found,
            packet.DroppedStarItem.Found,
            packet.MovingHazard.Found,
            packet.MovingHazard.PosX,
            packet.MovingHazard.PosY);
    }
    return ReceiveDisposition::CleanupPacket;
}

ReceiveDisposition HandleReceivedMovingHazardStateLocked(const void* data, std::size_t size)
{
    WireMovingHazardState packet;
    std::memcpy(&packet, data, size);
    if (packet.Magic != kMagic || packet.Version != kVersion
        || packet.Kind != kWireKindMovingHazardState || packet.Count > kMaxWorldMovingHazards)
    {
        return ReceiveDisposition::CleanupPacket;
    }

    const melonDS::u32 restartCutoff = MvlRestartPacketCutoffFrame();
    if (restartCutoff != 0 && packet.Frame <= restartCutoff)
        return ReceiveDisposition::SkipPacketCleanup;
    if (!G.RemoteMovingHazardStateSampleValid || packet.Frame >= G.RemoteMovingHazardStateSample.Frame)
    {
        G.RemoteMovingHazardStateSample = packet;
        G.RemoteMovingHazardStateSampleValid = true;
    }
    return ReceiveDisposition::CleanupPacket;
}

ReceiveDisposition HandleReceivedWorldActorSnapshotStateLocked(const void* data, std::size_t size)
{
    WireWorldActorSnapshotState packet;
    std::memcpy(&packet, data, size);
    if (packet.Magic != kMagic || packet.Version != kVersion
        || packet.Kind != kWireKindWorldActorSnapshot || packet.Count > kMaxWorldActorSnapshots)
    {
        return ReceiveDisposition::CleanupPacket;
    }

    const melonDS::u32 restartCutoff = MvlRestartPacketCutoffFrame();
    if (restartCutoff != 0 && packet.Frame <= restartCutoff)
        return ReceiveDisposition::SkipPacketCleanup;
    if (!G.RemoteWorldActorSnapshotSampleValid || packet.Frame >= G.RemoteWorldActorSnapshotSample.Frame)
    {
        G.RemoteWorldActorSnapshotSample = packet;
        G.RemoteWorldActorSnapshotSampleValid = true;
    }
    return ReceiveDisposition::CleanupPacket;
}

ReceiveDisposition HandleReceivedWorldEffectStateLocked(const void* data, std::size_t size)
{
    WireWorldEffectState packet;
    std::memcpy(&packet, data, size);
    if (packet.Magic != kMagic || packet.Version != kVersion
        || packet.Kind != kWireKindWorldEffectState || packet.Count > kMaxWorldEffects)
    {
        return ReceiveDisposition::CleanupPacket;
    }

    const melonDS::u32 restartCutoff = MvlRestartPacketCutoffFrame();
    if (restartCutoff != 0 && packet.Frame <= restartCutoff)
        return ReceiveDisposition::SkipPacketCleanup;
    if (!G.RemoteWorldEffectStateSampleValid || packet.Frame >= G.RemoteWorldEffectStateSample.Frame)
    {
        G.RemoteWorldEffectStateSample = packet;
        G.RemoteWorldEffectStateSampleValid = true;
    }
    return ReceiveDisposition::CleanupPacket;
}
void HandleReceivedGameStateLocked(const void* data, std::size_t size)
{
    WireGameState packet;
    std::memcpy(&packet, data, size);
    DecodedGameState decoded;
    if (!DecodeWireGameState(packet, decoded))
        return;

    const int packetInstance = static_cast<int>(decoded.Instance);
    const melonDS::u64 key = GameStateKey(packetInstance, decoded.Frame);
    G.RemoteGameStateHashes[key] = decoded.Hashes;
    G.RemoteGameStateSamples[key] = decoded.Sample;
    CompareGameStateLocked(packetInstance, decoded.Frame);
}

void PumpNetworkLocked(melonDS::NDS* nds = nullptr, melonDS::u32 localFrame = kNoFrameLimit)
{
    if (!G.Transport.HasHost()) return;

    TraceHangPhase("begin", "enet-service", -1, localFrame, localFrame, localFrame);
    FlushDelayedInputsLocked(localFrame);

    ENetEvent event;
    const int maxEvents = std::clamp(
        G.PacketBridge.MaxPumpEvents, 1, Config::PacketBridgePumpEventLimit);
    for (int i = 0; i < maxEvents; i++)
    {
        int result = G.Transport.Service(event);
        G.HangLastENetServiceResult.store(result, std::memory_order_release);
        if (result <= 0)
        {
            UpdateHangNetplaySnapshotLocked(localFrame);
            break;
        }

        G.HangLastENetEventType.store(static_cast<int>(event.type), std::memory_order_release);
        G.HangLastENetEventData.store(event.data, std::memory_order_release);

        switch (event.type)
        {
        case ENET_EVENT_TYPE_CONNECT:
            G.Transport.HandleConnected(event.peer);
            G.MatchSeedSent = false;
            G.NetplayStartReadySent = false;
            G.RemoteNetplayStartReadyFrame = kNoFrameLimit;
            G.RemoteNetplayStartReadyAfterLocal = false;
            UpdateHangNetplaySnapshotLocked(localFrame);
            G.InputCond.notify_all();
            std::printf("NSMB PoC: peer connected tUnixMs=%llu localFrame=%u peer=%d connectingPeer=%d lastSent=%u lastRecv=%u localQueue=%zu remoteQueue=%zu\n",
                NowUnixMs(),
                localFrame,
                G.Transport.IsConnected() ? 1 : 0,
                G.Transport.IsConnecting() ? 1 : 0,
                G.LastSentInputFrame,
                G.LastReceivedInputFrame,
                G.LocalInputs.size(),
                G.RemoteInputs.size());
            std::fflush(stdout);
            break;

        case ENET_EVENT_TYPE_RECEIVE:
        {
            G.HangLastENetRecvUnixMs.store(NowUnixMs(), std::memory_order_release);
            const PacketClassifier::PacketClass packetClass = PacketClassifier::Classify(
                event.packet->dataLength,
                {
                    InputProtocol::kInputPacketSize,
                    SessionProtocol::kSessionPacketSize,
                    sizeof(WireNSMLPacket),
                    sizeof(WirePlayerState),
                    sizeof(WireWorldState),
                    sizeof(WireMovingHazardState),
                    sizeof(WireWorldActorSnapshotState),
                    sizeof(WireWorldEffectState),
                    sizeof(WireGameState),
                });
            if (packetClass == PacketClassifier::PacketClass::Input)
            {
                HandleReceivedInputLocked(event.packet->data, event.packet->dataLength, localFrame);
            }
            else if (packetClass == PacketClassifier::PacketClass::InputBundleCandidate)
            {
                HandleReceivedInputBundleLocked(event.packet->data, event.packet->dataLength, localFrame);
            }
            else if (packetClass == PacketClassifier::PacketClass::Session)
            {
                HandleReceivedSessionLocked(event.packet->data, event.packet->dataLength, localFrame);
            }
            else if (packetClass == PacketClassifier::PacketClass::NSMLPacket)
            {
                HandleReceivedNSMLPacketLocked(
                    event.packet->data,
                    event.packet->dataLength,
                    nds,
                    localFrame);
            }
            else if (packetClass == PacketClassifier::PacketClass::PlayerState)
            {
                if (HandleReceivedPlayerStateLocked(
                        event.packet->data,
                        event.packet->dataLength,
                        localFrame)
                    == ReceiveDisposition::SkipPacketCleanup)
                {
                    break;
                }
            }
            else if (packetClass == PacketClassifier::PacketClass::WorldState)
            {
                if (HandleReceivedWorldStateLocked(
                        event.packet->data,
                        event.packet->dataLength,
                        localFrame)
                    == ReceiveDisposition::SkipPacketCleanup)
                {
                    break;
                }
            }
            else if (packetClass == PacketClassifier::PacketClass::MovingHazardState)
            {
                if (HandleReceivedMovingHazardStateLocked(
                        event.packet->data,
                        event.packet->dataLength)
                    == ReceiveDisposition::SkipPacketCleanup)
                {
                    break;
                }
            }
            else if (packetClass == PacketClassifier::PacketClass::WorldActorSnapshotState)
            {
                if (HandleReceivedWorldActorSnapshotStateLocked(
                        event.packet->data,
                        event.packet->dataLength)
                    == ReceiveDisposition::SkipPacketCleanup)
                {
                    break;
                }
            }
            else if (packetClass == PacketClassifier::PacketClass::WorldEffectState)
            {
                if (HandleReceivedWorldEffectStateLocked(
                        event.packet->data,
                        event.packet->dataLength)
                    == ReceiveDisposition::SkipPacketCleanup)
                {
                    break;
                }
            }
            else if (packetClass == PacketClassifier::PacketClass::GameState)
            {
                HandleReceivedGameStateLocked(event.packet->data, event.packet->dataLength);
            }
            enet_packet_destroy(event.packet);
            UpdateHangNetplaySnapshotLocked(localFrame);
            break;
        }

        case ENET_EVENT_TYPE_DISCONNECT:
            std::printf("NSMB PoC: peer disconnected tUnixMs=%llu localFrame=%u eventPeerMatches=%d peerBefore=%d connectingPeer=%d lastSent=%u lastRecv=%u lead=%d localQueue=%zu remoteQueue=%zu delayed=%zu resendCount=%d netplayStart=%u localReady=%u remoteReady=%u remoteReadyAfterLocal=%d eventData=%u\n",
                NowUnixMs(),
                localFrame,
                G.Transport.IsPeer(event.peer) ? 1 : 0,
                G.Transport.IsConnected() ? 1 : 0,
                G.Transport.IsConnecting() ? 1 : 0,
                G.LastSentInputFrame,
                G.LastReceivedInputFrame,
                CurrentInputLeadLocked(G.LastSentInputFrame == kNoFrameLimit ? localFrame : G.LastSentInputFrame),
                G.LocalInputs.size(),
                G.RemoteInputs.size(),
                G.DelayedInputs.size(),
                G.InputFrameLeadResendCount,
                G.Connection.StartFrame,
                G.LocalNetplayStartReadyFrame,
                G.RemoteNetplayStartReadyFrame,
                G.RemoteNetplayStartReadyAfterLocal ? 1 : 0,
                event.data);
            G.Transport.HandleDisconnected(event.peer);
            UpdateHangNetplaySnapshotLocked(localFrame);
            std::fflush(stdout);
            break;

        default:
            break;
        }
    }

    G.Transport.Flush();
    TraceHangPhase("end", "enet-service", -1, localFrame, localFrame, localFrame);
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
            sleepUs = std::max(50, G.Harness.NetworkPumpSleepUs);
        }
        std::this_thread::sleep_for(std::chrono::microseconds(sleepUs));
    }
}

void FrameHeartbeatThreadMain()
{
    melonDS::u32 writtenFrame = 0;
    while (!G.FrameHeartbeatStop.load(std::memory_order_acquire))
    {
        const melonDS::u32 frame = G.PendingFrameHeartbeat.load(std::memory_order_acquire);
        if (frame != 0 && frame != writtenFrame && G.FrameHeartbeat)
        {
            G.FrameHeartbeat << frame << '\n';
            G.FrameHeartbeat.flush();
            writtenFrame = frame;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void StartFrameHeartbeatThreadIfNeeded()
{
    if (!G.FrameHeartbeat || G.FrameHeartbeatThreadStarted)
        return;

    G.FrameHeartbeatStop.store(false, std::memory_order_release);
    G.FrameHeartbeatThreadStarted = true;
    G.FrameHeartbeatThread = std::thread(FrameHeartbeatThreadMain);
}

void StopFrameHeartbeatThread()
{
    if (!G.FrameHeartbeatThreadStarted)
        return;

    G.FrameHeartbeatStop.store(true, std::memory_order_release);
    if (G.FrameHeartbeatThread.joinable())
        G.FrameHeartbeatThread.join();
    G.FrameHeartbeatThreadStarted = false;
}

void StartNetworkPumpThreadIfNeeded()
{
    if (!G.Harness.NetworkPumpThreadEnabled || G.NetworkPumpThreadStarted)
        return;

    G.NetworkPumpStop = false;
    G.NetworkPumpThreadStarted = true;
    G.NetworkPumpThread = std::thread(NetworkPumpThreadMain);
    std::printf("NSMB PoC: network pump thread started sleepUs=%d inputWaitPollUs=%d rollbackInputWaitUs=%d\n",
        G.Harness.NetworkPumpSleepUs,
        G.Input.WaitPollUs,
        G.Rollback.InputWaitUs);
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
    if (!G.Transport.IsConnected() || G.NetRole != Role::Host || !G.MatchSeedConfigured || G.MatchSeedSent)
        return;

    const std::vector<char> payload = SessionProtocol::Encode({
        SessionProtocol::MessageKind::MatchSeed,
        G.MatchSeed,
    });
    if (G.Transport.Send(payload.data(), payload.size(), ENET_PACKET_FLAG_RELIABLE, true)
        == NsmbNetplayTransport::SendUnavailable)
        return;
    G.MatchSeedSent = true;
    std::printf("NSMB PoC: sent match seed 0x%08X\n", G.MatchSeed);
}

void SendNetplayStartReadyLocked(melonDS::u32 frame, bool force = false)
{
    if (!G.Transport.IsConnected() || (G.NetplayStartReadySent && !force))
        return;

    const std::vector<char> payload = SessionProtocol::Encode({
        SessionProtocol::MessageKind::StartReady,
        frame,
    });
    if (G.Transport.Send(payload.data(), payload.size(), ENET_PACKET_FLAG_RELIABLE, true)
        == NsmbNetplayTransport::SendUnavailable)
        return;
    G.NetplayStartReadySent = true;
    G.NetplayStartReadySendCount++;
    G.LastNetplayStartReadySentAt = std::chrono::steady_clock::now();
    EmitStartReadyEventLocked(force ? "resend" : "send", frame, G.RemoteNetplayStartReadyFrame);
    std::printf("NSMB InputNetplay: %s start ready frame=%u count=%d\n",
        force ? "resent" : "sent",
        frame,
        G.NetplayStartReadySendCount);
    std::fflush(stdout);
}

void MaybeResendNetplayStartReadyLocked(bool allowBeforeAccepted = false)
{
    const auto now = std::chrono::steady_clock::now();
    const auto elapsedSinceLastSend = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - G.LastNetplayStartReadySentAt).count();
    if (!SessionPolicy::ShouldResendStartReady({
            G.Transport.IsConnected(),
            G.Input.NetplayOnly,
            allowBeforeAccepted,
            G.WaitedForPeerAtNetplayStart,
            G.NetplayStartReadySent,
            G.LocalNetplayStartReadyFrame != kNoFrameLimit,
            G.LastReceivedInputFrame != kNoFrameLimit,
            G.LastReceivedInputFrame,
            G.Connection.StartFrame,
            G.Connection.Delay,
            G.NetplayStartReadySendCount,
            elapsedSinceLastSend,
        }))
    {
        return;
    }

    SendNetplayStartReadyLocked(G.LocalNetplayStartReadyFrame, true);
}

void SendInputPayloadNowLocked(const void* data, size_t size, melonDS::u32 flags)
{
    if (!G.Transport.IsConnected()) return;

    TraceHangPhase("begin", "enet-send-input", -1, G.LastSentInputFrame, G.LastSentInputFrame, G.LastSentInputFrame);
    const int result = G.Transport.Send(data, size, flags, true);
    if (result == NsmbNetplayTransport::SendUnavailable)
        return;
    G.HangLastENetSendResult.store(result, std::memory_order_release);
    G.HangLastENetSendBytes.store(size, std::memory_order_release);
    G.HangLastENetSendUnixMs.store(NowUnixMs(), std::memory_order_release);
    UpdateHangNetplaySnapshotLocked(G.LastSentInputFrame);
    TraceHangPhase("end", "enet-send-input", -1, G.LastSentInputFrame, G.LastSentInputFrame, G.LastSentInputFrame);
}

void FlushDelayedInputsLocked(melonDS::u32 frame)
{
    if (!G.Transport.IsConnected() || G.DelayedInputs.empty())
        return;

    for (auto it = G.DelayedInputs.begin(); it != G.DelayedInputs.end(); )
    {
        if (InputDelivery::ShouldReleaseDelayedInput(
                frame,
                std::chrono::steady_clock::now(),
                it->ReleaseFrame,
                it->ReleaseTime))
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
    const std::vector<InputProtocol::FramedInput> entries = InputDelivery::SelectBundleInputs(
        frame,
        input,
        G.Input.BundleHistory,
        G.LocalInputs);
    return InputProtocol::EncodeInputBundle(entries);
}

void SendInputLocked(melonDS::u32 frame, const InputState& input)
{
    if (!G.Transport.IsConnected()) return;

    SendMatchSeedLocked();
    MaybeResendNetplayStartReadyLocked();

    const melonDS::u32 previousLastSent = G.LastSentInputFrame;
    G.LastSentInputFrame = frame;
    UpdateHangNetplaySnapshotLocked(frame);
    if (G.Input.HealthTrace
        && previousLastSent != kNoFrameLimit
        && frame > previousLastSent + 1
        && G.LastInputHealthSendGapFrame != frame)
    {
        G.LastInputHealthSendGapFrame = frame;
        PrintInputHealthLineLocked(
            "send-gap",
            frame,
            frame,
            frame,
            0,
            0,
            0,
            CurrentInputLeadLocked(frame),
            false,
            false);
    }

    const InputDelivery::SendDecision sendDecision = InputDelivery::DecideSend(
        frame,
        {
            G.Input.UseHistoryBundle,
            G.Input.BundleHistory,
            G.Input.DropModulo,
            G.Input.DropOffset,
            G.Input.DropStartFrame,
            G.Input.DropEndFrame,
            G.Input.SendDelayFrames,
            G.Input.SendJitterFrames,
            G.Input.SendDelayStartFrame,
            G.Input.SendDelayEndFrame,
        });
    if (sendDecision.Drop)
    {
        if (G.Input.NetplayTrace)
            std::printf("NSMB InputNetplay: dropped local input packet frame=%u modulo=%d offset=%d range=%u-%u\n",
                frame,
                G.Input.DropModulo,
                G.Input.DropOffset,
                G.Input.DropStartFrame,
                G.Input.DropEndFrame);
        return;
    }

    const std::vector<char> inputPayload = InputProtocol::EncodeInput({ frame, input });

    const bool sendBundle = sendDecision.Bundle;
    const std::vector<char> bundlePayload = sendBundle
        ? BuildInputBundlePayloadLocked(frame, input)
        : std::vector<char> {};
    const melonDS::u32 sendFlags = ENET_PACKET_FLAG_RELIABLE;

    const int sendDelayFrames = sendDecision.DelayFrames;
    if (sendDelayFrames > 0)
    {
        G.DelayedInputs.push_back({
            frame + static_cast<melonDS::u32>(sendDelayFrames),
            std::chrono::steady_clock::now() + std::chrono::milliseconds(
                (sendDelayFrames * 1000 + 59) / 60),
            sendBundle
                ? bundlePayload
                : inputPayload,
            sendFlags,
        });
    }
    else
    {
        if (sendBundle)
            SendInputPayloadNowLocked(bundlePayload.data(), bundlePayload.size(), sendFlags);
        else
            SendInputPayloadNowLocked(
                inputPayload.data(),
                inputPayload.size(),
                ENET_PACKET_FLAG_RELIABLE);
    }

    if ((G.Bootstrap.InputTraceEnabled || G.Input.NetplayTrace)
        && frame != G.LastTracedSentInputFrame
        && (G.Bootstrap.InputTraceInterval <= 1 || (frame % static_cast<melonDS::u32>(G.Bootstrap.InputTraceInterval)) == 0))
    {
        G.LastTracedSentInputFrame = frame;
        std::printf("NSMB PoC: sent input tUnixMs=%llu frame=%u keys=0x%03X localQueue=%zu lastRecv=%u lead=%d bundle=%d delayedFrames=%d peer=%d\n",
            NowUnixMs(),
            frame,
            input.KeyMask,
            G.LocalInputs.size(),
            G.LastReceivedInputFrame,
            CurrentInputLeadLocked(frame),
            sendBundle ? 1 : 0,
            sendDelayFrames,
            G.Transport.IsConnected() ? 1 : 0);
        std::fflush(stdout);
    }
}

void MaybeResendLatestInputForFrameLeadLocked()
{
    if (!G.Transport.IsConnected() || !G.Input.NetplayOnly || G.LastSentInputFrame == kNoFrameLimit)
        return;

    const auto now = std::chrono::steady_clock::now();
    if (G.InputFrameLeadResendCount > 0
        && now - G.LastInputFrameLeadResendAt < std::chrono::milliseconds(50))
    {
        return;
    }

    auto it = G.LocalInputs.find(G.LastSentInputFrame);
    if (it == G.LocalInputs.end())
        return;

    const InputState& input = it->second;
    size_t payloadBytes = 0;
    if (G.Input.BundleHistory > 0)
    {
        const std::vector<char> payload = BuildInputBundlePayloadLocked(G.LastSentInputFrame, input);
        payloadBytes = payload.size();
        SendInputPayloadNowLocked(payload.data(), payload.size(), ENET_PACKET_FLAG_RELIABLE);
    }
    else
    {
        const std::vector<char> payload = InputProtocol::EncodeInput({ G.LastSentInputFrame, input });
        payloadBytes = payload.size();
        SendInputPayloadNowLocked(payload.data(), payload.size(), ENET_PACKET_FLAG_RELIABLE);
    }

    G.LastInputFrameLeadResendAt = now;
    G.InputFrameLeadResendCount++;
    if (G.Input.NetplayTrace)
    {
        std::printf("NSMB InputNetplay: resent latest input tUnixMs=%llu frame=%u count=%d payloadBytes=%zu bundleHistory=%d lastRecv=%u lead=%d localQueue=%zu remoteQueue=%zu delayed=%zu peer=%d\n",
            NowUnixMs(),
            G.LastSentInputFrame,
            G.InputFrameLeadResendCount,
            payloadBytes,
            G.Input.BundleHistory,
            G.LastReceivedInputFrame,
            CurrentInputLeadLocked(G.LastSentInputFrame),
            G.LocalInputs.size(),
            G.RemoteInputs.size(),
            G.DelayedInputs.size(),
            G.Transport.IsConnected() ? 1 : 0);
        std::fflush(stdout);
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

    if (G.Rollback.PredictionProbeModulo > 0
        && (G.Rollback.PredictionProbeLimit < 0
            || G.RollbackPredictionProbeCount < static_cast<melonDS::u32>(G.Rollback.PredictionProbeLimit))
        && frame >= G.Rollback.PredictionProbeStartFrame
        && (G.Rollback.PredictionProbeEndFrame == kNoFrameLimit
            || frame <= G.Rollback.PredictionProbeEndFrame)
        && (frame % static_cast<melonDS::u32>(G.Rollback.PredictionProbeModulo))
            == static_cast<melonDS::u32>(G.Rollback.PredictionProbeOffset))
    {
        input.KeyMask ^= G.Rollback.PredictionProbeKeyMask & 0xFFFu;
        G.RollbackPredictionProbeCount++;
    }

    G.PredictedRemoteInputs.emplace(frame, input);
    G.RollbackPredictionCount++;
    predicted = true;
    return true;
}

void PruneRollbackHistoryLocked(melonDS::u32 frame)
{
    const melonDS::u32 window = G.Rollback.Window > 0
        ? static_cast<melonDS::u32>(G.Rollback.Window)
        : 0;
    G.RollbackStore.Prune(frame, window);
    const melonDS::u32 keepFrom = frame > window ? frame - window : 0;
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
    if (G.Rollback.CheckpointInterval <= 1)
        return true;
    if (G.Connection.StartFrame != 0 && frame == G.Connection.StartFrame)
        return true;
    return (frame % static_cast<melonDS::u32>(G.Rollback.CheckpointInterval)) == 0;
}

const char* RollbackBackendName()
{
    switch (G.Rollback.Backend)
    {
    case RollbackBackend::CoreLite:
        return "corelite";
    case RollbackBackend::CoreSparse:
        return "coresparse";
    case RollbackBackend::CoreDelta:
        return "coredelta";
    case RollbackBackend::CoreFrameDelta:
        return "coreframedelta";
    case RollbackBackend::CorePreimage:
        return "corepreimage";
    case RollbackBackend::TinyCorePreimage:
        return "tinycorepreimage";
    case RollbackBackend::Savestate:
    default:
        return "savestate";
    }
}

bool IsRollbackPreimageBackend()
{
    return G.Rollback.Backend == RollbackBackend::CorePreimage
        || G.Rollback.Backend == RollbackBackend::TinyCorePreimage;
}

unsigned long long ElapsedUs(std::chrono::steady_clock::time_point start)
{
    return static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count());
}

void RecordRollbackCheckpointSaveLocked(size_t bytes, unsigned long long elapsedUs)
{
    G.RollbackCheckpointLastBytes = bytes;
    if (G.RollbackCheckpointMinBytes == 0 || bytes < G.RollbackCheckpointMinBytes)
        G.RollbackCheckpointMinBytes = bytes;
    if (bytes > G.RollbackCheckpointMaxBytes)
        G.RollbackCheckpointMaxBytes = bytes;
    G.RollbackCheckpointTotalBytes += static_cast<unsigned long long>(bytes);
    G.RollbackCheckpointSaveTotalUs += elapsedUs;
    if (elapsedUs > G.RollbackCheckpointSaveMaxUs)
        G.RollbackCheckpointSaveMaxUs = elapsedUs;
}

void RecordRollbackCheckpointRestoreLocked(unsigned long long elapsedUs)
{
    G.RollbackCheckpointRestoreOpCount++;
    G.RollbackCheckpointRestoreTotalUs += elapsedUs;
    if (elapsedUs > G.RollbackCheckpointRestoreMaxUs)
        G.RollbackCheckpointRestoreMaxUs = elapsedUs;
}

void RecordRollbackResimTimingLocked(
    melonDS::u32 frames,
    unsigned long long runFrameTotalUs,
    unsigned long long runFrameMaxUs,
    unsigned long long checkpointSaveTotalUs,
    unsigned long long checkpointSaveMaxUs,
    unsigned long long correctionTotalUs)
{
    G.RollbackMeasuredResimOpCount++;
    G.RollbackMeasuredResimFrameCount += frames;
    G.RollbackResimRunFrameTotalUs += runFrameTotalUs;
    if (runFrameMaxUs > G.RollbackResimRunFrameMaxUs)
        G.RollbackResimRunFrameMaxUs = runFrameMaxUs;
    G.RollbackResimCheckpointSaveTotalUs += checkpointSaveTotalUs;
    if (checkpointSaveMaxUs > G.RollbackResimCheckpointSaveMaxUs)
        G.RollbackResimCheckpointSaveMaxUs = checkpointSaveMaxUs;
    G.RollbackResimCorrectionTotalUs += correctionTotalUs;
    if (correctionTotalUs > G.RollbackResimCorrectionMaxUs)
        G.RollbackResimCorrectionMaxUs = correctionTotalUs;
}

void RecordActiveFrameTiming(int instanceID, melonDS::u32 frame)
{
    if (instanceID < 0 || instanceID >= 16 || !G.ActiveTimerStarted[instanceID])
        return;

    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(G.PerfMutex);
    if (!G.ActiveFrameTimingStarted[instanceID])
    {
        G.ActiveFrameTimingStarted[instanceID] = true;
        G.ActiveFrameLastTime[instanceID] = now;
        return;
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        now - G.ActiveFrameLastTime[instanceID]).count();
    G.ActiveFrameLastTime[instanceID] = now;
    if (elapsed <= 0)
        return;

    const unsigned long long elapsedUs =
        static_cast<unsigned long long>(elapsed);
    G.ActiveFrameSamples[instanceID]++;
    G.ActiveFrameTotalUs[instanceID] += elapsedUs;
    if (elapsedUs > G.ActiveFrameMaxUs[instanceID])
    {
        G.ActiveFrameMaxUs[instanceID] = elapsedUs;
        G.ActiveFrameMaxFrame[instanceID] = frame;
    }
    if (elapsedUs > 16667)
        G.ActiveFrameOver16ms[instanceID]++;
    if (elapsedUs > 25000)
        G.ActiveFrameOver25ms[instanceID]++;
    if (elapsedUs > 33334)
        G.ActiveFrameOver33ms[instanceID]++;

    if (G.ActiveFrameSpikeTrace && elapsedUs >= static_cast<unsigned long long>(G.ActiveFrameSpikeThresholdUs))
    {
        const melonDS::u32 restoreDelta =
            G.RollbackRestoreCount - G.LastPerfSpikeRollbackRestoreCount[instanceID];
        const melonDS::u32 resimDelta =
            G.RollbackResimulateCount - G.LastPerfSpikeRollbackResimulateCount[instanceID];
        G.LastPerfSpikeRollbackRestoreCount[instanceID] = G.RollbackRestoreCount;
        G.LastPerfSpikeRollbackResimulateCount[instanceID] = G.RollbackResimulateCount;
        std::printf(
            "NSMB PerfSpike: inst=%d frame=%u frameTimeUs=%llu thresholdUs=%d rollbackRestores=%u rollbackResims=%u rollbackRestoreDelta=%u rollbackResimDelta=%u saveMaxUs=%llu restoreMaxUs=%llu resimRunMaxUs=%llu resimSaveMaxUs=%llu resimTotalMaxUs=%llu\n",
            instanceID,
            frame,
            elapsedUs,
            G.ActiveFrameSpikeThresholdUs,
            G.RollbackRestoreCount,
            G.RollbackResimulateCount,
            restoreDelta,
            resimDelta,
            G.RollbackCheckpointSaveMaxUs,
            G.RollbackCheckpointRestoreMaxUs,
            G.RollbackResimRunFrameMaxUs,
            G.RollbackResimCheckpointSaveMaxUs,
            G.RollbackResimCorrectionMaxUs);
    }
}

void InvalidateMainRAMJIT(melonDS::NDS* nds, melonDS::u32 len)
{
    static const bool skipInvalidation = std::getenv("MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET") != nullptr;
    if (skipInvalidation || !nds || len == 0)
        return;
    for (melonDS::u32 offset = 0; offset < len; offset += 0x1000)
    {
        const melonDS::u32 addr = kMainRAMBase + offset;
        nds->JIT.CheckAndInvalidate<0, melonDS::ARMJIT_Memory::memregion_MainRAM>(addr);
        nds->JIT.CheckAndInvalidate<1, melonDS::ARMJIT_Memory::memregion_MainRAM>(addr);
    }
}

bool IsValidMainRAMRange(melonDS::NDS* nds, melonDS::u32 address, melonDS::u32 length)
{
    if (!nds || !nds->MainRAM || length == 0 || address < kMainRAMBase)
        return false;
    const melonDS::u32 offset = address - kMainRAMBase;
    const melonDS::u32 ramLen = nds->MainRAMMask + 1;
    return offset < ramLen && length <= ramLen - offset;
}

bool ReadMainRAMAddressU32(melonDS::NDS* nds, melonDS::u32 address, melonDS::u32& value)
{
    if (!IsValidMainRAMRange(nds, address, sizeof(value)))
        return false;
    std::memcpy(&value, nds->MainRAM + (address - kMainRAMBase), sizeof(value));
    return true;
}

bool PrepareRollbackDeltaSaveLocked(melonDS::u32 frame, RollbackStoredState& checkpoint, std::vector<melonDS::u8>& baseMainRAM)
{
    RollbackStorage::DeltaMode mode = RollbackStorage::DeltaMode::None;
    if (IsRollbackPreimageBackend())
        mode = RollbackStorage::DeltaMode::Preimage;
    else if (G.Rollback.Backend == RollbackBackend::CoreFrameDelta)
        mode = RollbackStorage::DeltaMode::FrameDelta;
    else if (G.Rollback.Backend == RollbackBackend::CoreDelta)
        mode = RollbackStorage::DeltaMode::KeyframeDelta;
    const melonDS::u32 keyframeInterval = G.Rollback.DeltaKeyframeInterval > 0
        ? static_cast<melonDS::u32>(G.Rollback.DeltaKeyframeInterval)
        : 0;
    G.RollbackStore.PrepareSave(
        frame,
        mode,
        keyframeInterval,
        G.Connection.StartFrame,
        checkpoint,
        baseMainRAM);
    return true;
}

bool CaptureRollbackFramePreimage(
    RollbackStoredState& checkpoint,
    melonDS::NDS* nds,
    const std::vector<melonDS::u8>& baseMainRAM)
{
    if (!checkpoint.MainRAMFramePreimage)
        return true;
    if (!nds || !nds->MainRAM || baseMainRAM.size() != nds->MainRAMMask + 1)
        return false;

    const melonDS::u32 len = nds->MainRAMMask + 1;
    const melonDS::u32 pageSize = static_cast<melonDS::u32>(G.Rollback.MainRAMPageSize);
    for (melonDS::u32 offset = 0; offset < len; offset += pageSize)
    {
        const melonDS::u32 pageBytes = std::min(pageSize, len - offset);
        if (std::memcmp(nds->MainRAM + offset, baseMainRAM.data() + offset, pageBytes) == 0)
            continue;
        checkpoint.MainRAMPreimagePages.push_back(offset / pageSize);
        checkpoint.MainRAMPreimage.insert(
            checkpoint.MainRAMPreimage.end(),
            baseMainRAM.begin() + offset,
            baseMainRAM.begin() + offset + pageBytes);
    }
    return true;
}

bool SaveRollbackCheckpointBuffer(
    melonDS::NDS* nds,
    std::vector<char>& buffer,
    melonDS::u32 mainRAMMode = kRollbackMainRAMModeFull,
    const melonDS::u8* deltaBaseMainRAM = nullptr)
{
    if (!nds)
        return false;

    if (G.Rollback.Backend == RollbackBackend::TinyCorePreimage)
    {
        melonDS::Savestate state;
        const bool saved = nds->DoRollbackTinyCoreSavestate(
            &state,
            static_cast<melonDS::u32>(G.Rollback.TinyCoreFlags));
        if (state.Error || !saved || state.Error)
            return false;
        buffer.assign(static_cast<const char*>(state.Buffer()),
            static_cast<const char*>(state.Buffer()) + state.Length());
        return true;
    }

    melonDS::Savestate state;
    const bool saved = (G.Rollback.Backend == RollbackBackend::CoreLite
        || G.Rollback.Backend == RollbackBackend::CoreSparse
        || G.Rollback.Backend == RollbackBackend::CoreDelta
        || G.Rollback.Backend == RollbackBackend::CoreFrameDelta
        || G.Rollback.Backend == RollbackBackend::CorePreimage)
        ? nds->DoRollbackSavestate(&state, mainRAMMode, deltaBaseMainRAM,
            static_cast<melonDS::u32>(G.Rollback.MainRAMPageSize),
            static_cast<melonDS::u32>(G.Rollback.CoreSkipMask))
        : nds->DoSavestate(&state);
    if (state.Error || !saved || state.Error)
        return false;
    buffer.assign(static_cast<const char*>(state.Buffer()),
        static_cast<const char*>(state.Buffer()) + state.Length());
    return true;
}

bool RestoreRollbackCheckpointBuffer(
    melonDS::NDS* nds,
    const std::vector<char>& buffer,
    const melonDS::u8* deltaBaseMainRAM = nullptr)
{
    if (!nds)
        return false;

    if (G.Rollback.Backend == RollbackBackend::TinyCorePreimage)
    {
        melonDS::Savestate state(const_cast<char*>(buffer.data()), static_cast<melonDS::u32>(buffer.size()), false);
        const bool restored = nds->DoRollbackTinyCoreSavestate(
            &state,
            static_cast<melonDS::u32>(G.Rollback.TinyCoreFlags));
        return !state.Error && restored && !state.Error;
    }

    melonDS::Savestate state(const_cast<char*>(buffer.data()), static_cast<melonDS::u32>(buffer.size()), false);
    const bool restored = (G.Rollback.Backend == RollbackBackend::CoreLite
        || G.Rollback.Backend == RollbackBackend::CoreSparse
        || G.Rollback.Backend == RollbackBackend::CoreDelta
        || G.Rollback.Backend == RollbackBackend::CoreFrameDelta
        || G.Rollback.Backend == RollbackBackend::CorePreimage)
        ? nds->DoRollbackSavestate(&state, kRollbackMainRAMModeFull, deltaBaseMainRAM,
            static_cast<melonDS::u32>(G.Rollback.MainRAMPageSize),
            static_cast<melonDS::u32>(G.Rollback.CoreSkipMask))
        : nds->DoSavestate(&state);
    return !state.Error && restored && !state.Error;
}

bool BuildRollbackRestoreChainLocked(
    melonDS::u32 frame,
    std::vector<RollbackStoredState>& chain)
{
    return G.RollbackStore.BuildRestoreChain(frame, chain);
}

bool RestoreRollbackStoredStates(
    melonDS::NDS* nds,
    const std::vector<RollbackStoredState>& chain)
{
    if (chain.empty() || chain.front().MainRAMDelta)
        return false;
    if (!RestoreRollbackCheckpointBuffer(nds, chain.front().Buffer))
        return false;
    for (size_t i = 1; i < chain.size(); i++)
    {
        if (!chain[i].MainRAMDelta
            || !RestoreRollbackCheckpointBuffer(nds, chain[i].Buffer, nds->MainRAM))
            return false;
    }
    return true;
}

bool BuildRollbackPreimageRestoreLocked(
    melonDS::u32 frame,
    std::vector<RollbackStoredState>& reverseStates,
    std::vector<melonDS::u8>& latestMainRAM)
{
    return G.RollbackStore.BuildPreimageRestore(frame, reverseStates, latestMainRAM);
}

bool RestoreRollbackPreimageState(
    melonDS::NDS* nds,
    const RollbackStoredState& checkpoint,
    const std::vector<RollbackStoredState>& reverseStates,
    const std::vector<melonDS::u8>& latestMainRAM)
{
    if (!nds || !nds->MainRAM || latestMainRAM.size() != nds->MainRAMMask + 1)
        return false;
    const melonDS::u32 len = nds->MainRAMMask + 1;
    const melonDS::u32 pageSize = static_cast<melonDS::u32>(G.Rollback.MainRAMPageSize);
    std::memcpy(nds->MainRAM, latestMainRAM.data(), len);
    for (const auto& state : reverseStates)
    {
        size_t inOffset = 0;
        for (const melonDS::u32 page : state.MainRAMPreimagePages)
        {
            const melonDS::u32 offset = page * pageSize;
            if (offset >= len)
                return false;
            const melonDS::u32 pageBytes = std::min(pageSize, len - offset);
            if (inOffset + pageBytes > state.MainRAMPreimage.size())
                return false;
            std::memcpy(nds->MainRAM + offset, state.MainRAMPreimage.data() + inOffset, pageBytes);
            inOffset += pageBytes;
        }
        if (inOffset != state.MainRAMPreimage.size())
            return false;
    }
    if (!RestoreRollbackCheckpointBuffer(nds, checkpoint.Buffer))
        return false;
    InvalidateMainRAMJIT(nds, len);
    return true;
}

void RefreshRollbackFrameDeltaShadowLocked(melonDS::u32 frame, melonDS::NDS* nds)
{
    if ((G.Rollback.Backend != RollbackBackend::CoreFrameDelta
        && !IsRollbackPreimageBackend())
        || !nds || !nds->MainRAM)
        return;
    const melonDS::u32 len = nds->MainRAMMask + 1;
    G.RollbackStore.UpdateFrameShadow(frame, nds->MainRAM, len);
}

void SaveRollbackCheckpointIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.Rollback.Enabled || !G.Input.NetplayOnly || !nds)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (G.Connection.StartFrame != 0 && frame < G.Connection.StartFrame)
        return;
    if (G.Rollback.Window <= 0)
        return;
    if (!ShouldSaveRollbackCheckpoint(frame))
        return;

    RollbackStoredState checkpoint;
    std::vector<melonDS::u8> deltaBaseMainRAM;
    const auto saveStart = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        PrepareRollbackDeltaSaveLocked(frame, checkpoint, deltaBaseMainRAM);
    }
    const melonDS::u32 mainRAMMode = IsRollbackPreimageBackend()
        ? kRollbackMainRAMModeSkip
        : (checkpoint.MainRAMDelta
            ? kRollbackMainRAMModeDelta
            : (G.Rollback.Backend == RollbackBackend::CoreSparse ? kRollbackMainRAMModeSparse : kRollbackMainRAMModeFull));
    if (checkpoint.MainRAMDelta && (!nds->MainRAM || deltaBaseMainRAM.size() != nds->MainRAMMask + 1))
        return;
    if (!CaptureRollbackFramePreimage(checkpoint, nds, deltaBaseMainRAM))
        return;

    if (!SaveRollbackCheckpointBuffer(nds, checkpoint.Buffer, mainRAMMode,
        checkpoint.MainRAMDelta ? deltaBaseMainRAM.data() : nullptr))
    {
        if (G.Input.NetplayTrace)
            std::printf("NSMB Rollback: failed to save checkpoint inst=%d frame=%u\n", instanceID, frame);
        return;
    }
    if (G.Rollback.Backend == RollbackBackend::CoreDelta && !checkpoint.MainRAMDelta && nds->MainRAM)
    {
        const melonDS::u32 len = nds->MainRAMMask + 1;
        checkpoint.MainRAMCopy.resize(len);
        std::memcpy(checkpoint.MainRAMCopy.data(), nds->MainRAM, len);
    }
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        const size_t checkpointBytes = G.RollbackStore.Put(frame, std::move(checkpoint));
        RefreshRollbackFrameDeltaShadowLocked(frame, nds);
        RecordRollbackCheckpointSaveLocked(
            checkpointBytes,
            ElapsedUs(saveStart));
        G.RollbackCheckpointSaveCount++;
        PruneRollbackHistoryLocked(frame);
    }
}

void SaveRollbackCheckpointNowLocked(melonDS::u32 frame, melonDS::NDS* nds, bool force = false)
{
    if (!nds || G.Rollback.Window <= 0)
        return;
    if (!force && !ShouldSaveRollbackCheckpoint(frame))
        return;

    RollbackStoredState checkpoint;
    std::vector<melonDS::u8> deltaBaseMainRAM;
    const auto saveStart = std::chrono::steady_clock::now();
    PrepareRollbackDeltaSaveLocked(frame, checkpoint, deltaBaseMainRAM);
    const melonDS::u32 mainRAMMode = IsRollbackPreimageBackend()
        ? kRollbackMainRAMModeSkip
        : (checkpoint.MainRAMDelta
            ? kRollbackMainRAMModeDelta
            : (G.Rollback.Backend == RollbackBackend::CoreSparse ? kRollbackMainRAMModeSparse : kRollbackMainRAMModeFull));
    if (checkpoint.MainRAMDelta && (!nds->MainRAM || deltaBaseMainRAM.size() != nds->MainRAMMask + 1))
        return;
    if (!CaptureRollbackFramePreimage(checkpoint, nds, deltaBaseMainRAM))
        return;

    if (!SaveRollbackCheckpointBuffer(nds, checkpoint.Buffer, mainRAMMode,
        checkpoint.MainRAMDelta ? deltaBaseMainRAM.data() : nullptr))
        return;
    if (G.Rollback.Backend == RollbackBackend::CoreDelta && !checkpoint.MainRAMDelta && nds->MainRAM)
    {
        const melonDS::u32 len = nds->MainRAMMask + 1;
        checkpoint.MainRAMCopy.resize(len);
        std::memcpy(checkpoint.MainRAMCopy.data(), nds->MainRAM, len);
    }
    const size_t checkpointBytes = G.RollbackStore.Put(frame, std::move(checkpoint));
    RefreshRollbackFrameDeltaShadowLocked(frame, nds);
    RecordRollbackCheckpointSaveLocked(
        checkpointBytes,
        ElapsedUs(saveStart));
    G.RollbackCheckpointSaveCount++;
    PruneRollbackHistoryLocked(frame);
}

bool RestoreRollbackCheckpointForProbeIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.Rollback.Enabled || !G.Rollback.RestoreProbe || !G.Input.NetplayOnly || !nds)
        return false;
    if (instanceID < 0 || instanceID >= 16)
        return false;

    melonDS::u32 restoreFrame = kNoFrameLimit;
    RollbackStoredState checkpoint;
    std::vector<RollbackStoredState> restoreChain;
    std::vector<RollbackStoredState> reverseStates;
    std::vector<melonDS::u8> latestMainRAM;
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        if (G.PendingRollbackFrame == kNoFrameLimit)
            return false;

        restoreFrame = G.PendingRollbackFrame;
        if (!G.RollbackStore.Copy(restoreFrame, checkpoint))
        {
            std::printf(
                "NSMB Rollback: cannot restore frame=%u at current=%u, checkpoint missing window=%d\n",
                restoreFrame,
                frame,
                G.Rollback.Window);
            G.PendingRollbackFrame = kNoFrameLimit;
            return false;
        }
        const bool restoreReady = IsRollbackPreimageBackend()
            ? BuildRollbackPreimageRestoreLocked(restoreFrame, reverseStates, latestMainRAM)
            : BuildRollbackRestoreChainLocked(restoreFrame, restoreChain);
        if (!restoreReady)
        {
            std::printf("NSMB Rollback: cannot restore delta chain frame=%u base=%u missing\n",
                restoreFrame,
                checkpoint.BaseFrame);
            G.PendingRollbackFrame = kNoFrameLimit;
            return false;
        }
        G.PendingRollbackFrame = kNoFrameLimit;
    }

    const auto restoreStart = std::chrono::steady_clock::now();
    const bool restored = IsRollbackPreimageBackend()
        ? RestoreRollbackPreimageState(nds, checkpoint, reverseStates, latestMainRAM)
        : RestoreRollbackStoredStates(nds, restoreChain);
    if (!restored)
    {
        std::printf("NSMB Rollback: restore probe failed inst=%d restoreFrame=%u current=%u\n",
            instanceID,
            restoreFrame,
            frame);
        return false;
    }
    const unsigned long long restoreUs = ElapsedUs(restoreStart);

    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        RefreshRollbackFrameDeltaShadowLocked(restoreFrame, nds);
        RecordRollbackCheckpointRestoreLocked(restoreUs);
        G.RollbackRestoreCount++;
    }
    std::printf("NSMB Rollback: restore probe loaded frame=%u at current=%u bytes=%zu\n",
        restoreFrame,
        frame,
        CheckpointBytes(checkpoint));
    std::fflush(stdout);
    return true;
}

melonDS::u32 LocalPlayerID(melonDS::NDS* nds)
{
    if (!nds)
        return 0;

    if (G.PacketBridge.Enabled)
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
    if (G.PacketBridge.Enabled && G.PacketBridge.AllowPreGame && !inGameplay)
        return static_cast<melonDS::u32>(G.Connection.LocalInstance & 1);

    return nds->ARM9Read32(kGameLocalPlayerIDAddr) & 1;
}

void ApplyPendingNSMLPacketsLocked(melonDS::NDS* nds)
{
    if (!G.PacketBridge.Enabled || !nds || G.PendingNSMLPackets.empty())
        return;

    for (const WireNSMLPacket& packet : G.PendingNSMLPackets)
        melonDS::NSML_PushMarioVsLuigiRemotePacket(nds, packet.Player, packet.Packet);
    G.PendingNSMLPackets.clear();
}

void SendNSMLWirePacketNowLocked(const WireNSMLPacket& packet)
{
    G.Transport.Send(&packet, sizeof(packet), ENET_PACKET_FLAG_RELIABLE, true);
}

void FlushDelayedNSMLPacketsLocked(melonDS::u32 frame)
{
    if (!G.PacketBridge.Enabled || !G.Transport.IsConnected() || G.DelayedNSMLPackets.empty())
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
    if (!G.PacketBridge.Enabled || !G.Transport.IsConnected() || !packetBytes || player > 1)
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

    const int jitterFrames = G.PacketBridge.SendJitterFrames > 0
        ? static_cast<int>(frame % static_cast<melonDS::u32>(G.PacketBridge.SendJitterFrames + 1))
        : 0;
    const int sendDelayFrames = G.PacketBridge.SendDelayFrames + jitterFrames;
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

    if (G.PacketBridge.TraceEnabled && tick != G.LastSentNSMLPacketTick)
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
    if (!G.PacketBridge.Enabled || !nds)
        return;

    melonDS::u8 packet[52] {};
    melonDS::u32 tick = 0;
    melonDS::u32 keys = 0;
    bool captured = melonDS::NSML_TakeMarioVsLuigiLocalPacket(nds, packet, &tick, &keys);
    if (!captured && G.PacketBridge.DirectCaptureEnabled)
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

    if (G.PacketBridge.ForceTickEnabled
        && frame >= G.PacketBridge.ForceTickStartFrame
        && G.PacketBridge.ForceTickBase >= 0)
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
    if (G.PacketBridge.ForceTickEnabled
        && frame >= G.PacketBridge.ForceTickStartFrame
        && G.PacketBridge.ForceTickBase >= 0)
    {
        return (static_cast<melonDS::u32>(G.PacketBridge.ForceTickBase)
            + (frame - G.PacketBridge.ForceTickStartFrame)) & 0xFFFF;
    }

    return nds ? nds->ARM9Read16(0x020888E0) : 0;
}

void ForceNSMLPacketBridgeTickIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.PacketBridge.ForceTickEnabled || !nds || instanceID < 0 || instanceID >= 16)
        return;
    if (frame < G.PacketBridge.ForceTickStartFrame)
        return;
    if (G.LastPacketBridgeForcedTickFrame[instanceID] == frame)
        return;

    const melonDS::u32 tick = PacketBridgeCanonicalTick(nds, frame);
    nds->ARM9Write16(0x020888E0, static_cast<melonDS::u16>(tick));
    G.LastPacketBridgeForcedTickFrame[instanceID] = frame;

    if (G.PacketBridge.TraceEnabled && (frame % 60) == 0)
    {
        std::printf("NSMB PacketBridge: force tick=0x%04X frame=%u\n", tick, frame);
        std::fflush(stdout);
    }
}

void ForceNSMLPacketBridgeNetReadyIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.PacketBridge.ForceNetReady || !nds || instanceID < 0 || instanceID >= 16)
        return;
    if (frame < G.PacketBridge.ForceNetReadyStartFrame)
        return;
    if (G.PacketBridge.ForceNetReadyEndFrame != 0 && frame > G.PacketBridge.ForceNetReadyEndFrame)
        return;
    if (G.PacketBridge.ForceNetReadyHostOnly && G.NetRole != Role::Host)
        return;
    if (G.PacketBridge.ForceNetReadyClientOnly && G.NetRole != Role::Client)
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
    if (G.PacketBridge.ForceNetReadyState10
        && (!G.PacketBridge.ForceNetReadyState10ClientOnly || G.NetRole == Role::Client))
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

    if (G.PacketBridge.TraceEnabled && (frame % 60) == 0)
    {
        std::printf("NSMB PacketBridge: force net ready inst=%d frame=%u\n", instanceID, frame);
        std::fflush(stdout);
    }
}

void ForceNSMLGameLocalPlayerIDIfNeeded(melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!nds || G.PacketBridge.ForceGameLocalPlayerID < 0)
        return;
    if (frame < G.PacketBridge.ForceGameLocalPlayerIDStartFrame)
        return;
    if (!G.PacketBridge.ForceGameLocalPlayerIDEarly
        && (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1))
        return;

    nds->ARM9Write32(kGameLocalPlayerIDAddr, static_cast<melonDS::u32>(G.PacketBridge.ForceGameLocalPlayerID & 1));
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
    if (!G.PacketBridge.WaitEnabled || !nds)
        return;
    if (frame < G.PacketBridge.WaitStartFrame)
        return;

    const melonDS::u32 remotePlayer = LocalPlayerID(nds) ^ 1;
    const melonDS::u32 currentTick = PacketBridgeCanonicalTick(nds, frame);
    const melonDS::u32 tick = (currentTick + static_cast<melonDS::u32>(G.PacketBridge.WaitTickAhead)) & 0xFFFF;
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

        if (G.PacketBridge.WaitTimeoutMs > 0)
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= G.PacketBridge.WaitTimeoutMs)
            {
                if (G.PacketBridge.TraceEnabled && G.LastPacketBridgeWaitTimeoutTick != tick)
                {
                    G.LastPacketBridgeWaitTimeoutTick = tick;
                    std::printf("NSMB PacketBridge: wait timeout player=%u tick=0x%04X frame=%u waitedMs=%d\n",
                        remotePlayer,
                        tick,
                        frame,
                        G.PacketBridge.WaitTimeoutMs);
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
    if (G.PacketBridge.MaxTickLead < 0 || !nds)
        return;
    if (frame < G.PacketBridge.ThrottleStartFrame)
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
        if (lead <= G.PacketBridge.MaxTickLead)
            return;

        if (G.PacketBridge.TraceEnabled && G.LastPacketBridgeThrottleTraceTick != localTick)
        {
            G.LastPacketBridgeThrottleTraceTick = localTick;
            std::printf("NSMB PacketBridge: throttle localTick=0x%04X remotePlayer=%u remoteTick=0x%04X lead=%d maxLead=%d frame=%u remoteFrame=%u\n",
                localTick,
                remotePlayer,
                remoteTick,
                lead,
                G.PacketBridge.MaxTickLead,
                frame,
                remoteFrame);
            std::fflush(stdout);
        }

        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            PumpNSMLPacketBridgeLocked(nds, frame);
        }

        if (G.PacketBridge.ThrottleTimeoutMs > 0)
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= G.PacketBridge.ThrottleTimeoutMs)
            {
                if (G.PacketBridge.TraceEnabled)
                {
                    std::printf("NSMB PacketBridge: throttle timeout localTick=0x%04X remoteTick=0x%04X lead=%d frame=%u waitedMs=%d\n",
                        localTick,
                        remoteTick,
                        lead,
                        frame,
                        G.PacketBridge.ThrottleTimeoutMs);
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
    if (G.PacketBridge.MaxFrameLead < 0 || !nds)
        return;
    if (frame < G.PacketBridge.ThrottleStartFrame)
        return;
    if (G.PacketBridge.ForceTickEnabled && frame < G.PacketBridge.ForceTickStartFrame)
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
        if (G.PacketBridge.ForceTickEnabled && remoteFrame < G.PacketBridge.ForceTickStartFrame)
            return;

        const int lead = static_cast<int>(frame) - static_cast<int>(remoteFrame);
        if (lead <= G.PacketBridge.MaxFrameLead)
            return;

        if (G.PacketBridge.TraceEnabled && G.LastPacketBridgeFrameThrottleTraceFrame != frame)
        {
            G.LastPacketBridgeFrameThrottleTraceFrame = frame;
            std::printf("NSMB PacketBridge: frame throttle frame=%u remotePlayer=%u remoteFrame=%u lead=%d maxLead=%d remoteTick=0x%04X\n",
                frame,
                remotePlayer,
                remoteFrame,
                lead,
                G.PacketBridge.MaxFrameLead,
                remoteTick & 0xFFFF);
            std::fflush(stdout);
        }

        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            PumpNSMLPacketBridgeLocked(nds, frame);
        }

        if (G.PacketBridge.ThrottleTimeoutMs > 0)
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= G.PacketBridge.ThrottleTimeoutMs)
            {
                if (G.PacketBridge.TraceEnabled)
                {
                    std::printf("NSMB PacketBridge: frame throttle timeout frame=%u remoteFrame=%u lead=%d waitedMs=%d\n",
                        frame,
                        remoteFrame,
                        lead,
                        G.PacketBridge.ThrottleTimeoutMs);
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

void PrintInputHealthLineLocked(
    const char* event,
    melonDS::u32 frame,
    melonDS::u32 logicalFrame,
    melonDS::u32 sendFrame,
    unsigned long long waitedUs,
    unsigned long long throttleUs,
    unsigned long long networkUs,
    int lead,
    bool hasRemoteInput,
    bool predictedRemoteInput)
{
    if (!G.Input.HealthTrace)
        return;

    const melonDS::u32 lastSent = G.LastSentInputFrame;
    const melonDS::u32 lastRecv = G.LastReceivedInputFrame;
    UpdateHangNetplaySnapshotLocked(sendFrame);
    std::printf(
        "NSMB InputHealth: tUnixMs=%llu event=%s frame=%u logicalFrame=%u sendFrame=%u lastSent=%u lastRecv=%u lead=%d localQueue=%zu remoteQueue=%zu delayed=%zu waitMs=%.3f throttleMs=%.3f networkMs=%.3f hasRemote=%d predicted=%d rollback=%d peer=%d connectingPeer=%d resendCount=%d netplayStart=%u localReady=%u remoteReady=%u remoteReadyAfterLocal=%d\n",
        NowUnixMs(),
        event,
        frame,
        logicalFrame,
        sendFrame,
        lastSent,
        lastRecv,
        lead,
        G.LocalInputs.size(),
        G.RemoteInputs.size(),
        G.DelayedInputs.size(),
        static_cast<double>(waitedUs) / 1000.0,
        static_cast<double>(throttleUs) / 1000.0,
        static_cast<double>(networkUs) / 1000.0,
        hasRemoteInput ? 1 : 0,
        predictedRemoteInput ? 1 : 0,
        G.Rollback.Enabled ? 1 : 0,
        G.Transport.IsConnected() ? 1 : 0,
        G.Transport.IsConnecting() ? 1 : 0,
        G.InputFrameLeadResendCount,
        G.Connection.StartFrame,
        G.LocalNetplayStartReadyFrame,
        G.RemoteNetplayStartReadyFrame,
        G.RemoteNetplayStartReadyAfterLocal ? 1 : 0);
    std::fflush(stdout);
}

int CurrentInputLeadLocked(melonDS::u32 sendFrame)
{
    if (G.LastReceivedInputFrame == kNoFrameLimit)
        return 0;
    return static_cast<int>(sendFrame) - static_cast<int>(G.LastReceivedInputFrame);
}

void PrimeInputNetplayEpochStartLocked(melonDS::u32 localFrame)
{
    if (!G.Input.NetplayOnly || G.Connection.StartFrame == 0
        || G.InputEpochPrimedStartFrame == G.Connection.StartFrame)
    {
        return;
    }

    PruneInputHistoryLocked(G.Connection.StartFrame);

    const melonDS::u32 delay = static_cast<melonDS::u32>(std::max(0, G.Connection.Delay));
    const melonDS::u32 firstInputFrame = G.Connection.StartFrame + delay;
    for (melonDS::u32 frame = G.Connection.StartFrame; frame < firstInputFrame; frame++)
    {
        G.LocalInputs.emplace(frame, NeutralInput());
        G.RemoteInputs.emplace(frame, NeutralInput());
    }
    if (delay > 0)
    {
        const melonDS::u32 primedThrough = firstInputFrame - 1;
        if (G.LastReceivedInputFrame == kNoFrameLimit || G.LastReceivedInputFrame < primedThrough)
            G.LastReceivedInputFrame = primedThrough;
    }
    G.InputEpochPrimedStartFrame = G.Connection.StartFrame;
    G.InputCond.notify_all();

    std::printf("NSMB InputNetplay: primed epoch start localFrame=%u logicalStart=%u firstInput=%u delay=%u\n",
        localFrame,
        G.Connection.StartFrame,
        firstInputFrame,
        delay);
    std::fflush(stdout);
}

bool IsPastTestInputRange(melonDS::u32 targetFrame)
{
    return G.TestEnabled
        && G.Bootstrap.TestFrames != kNoFrameLimit
        && targetFrame >= G.Bootstrap.TestFrames;
}

void RecordRemoteInputWaitStats(unsigned long long elapsedUs, unsigned long long loops)
{
    G.RemoteInputWaitCount++;
    G.RemoteInputWaitLoops += loops;
    G.RemoteInputWaitUs += elapsedUs;
    G.RemoteInputWaitMaxUs = std::max(G.RemoteInputWaitMaxUs, elapsedUs);
}

void TraceRemoteInputWaitSpike(melonDS::u32 targetFrame, unsigned long long elapsedUs, unsigned long long loops)
{
    if (!G.ActiveFrameSpikeTrace
        || elapsedUs < static_cast<unsigned long long>(std::min(G.ActiveFrameSpikeThresholdUs, 10000)))
    {
        return;
    }

    std::printf(
        "NSMB RemoteInputWaitSpike: frame=%u waitedMs=%.3f loops=%llu\n",
        targetFrame,
        static_cast<double>(elapsedUs) / 1000.0,
        loops);
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
    if ((G.PacketBridge.Only || G.Input.NetplayOnly)
        && G.Connection.StartFrame != 0
        && targetFrame < G.Connection.StartFrame)
        return NeutralInput();

    const auto start = std::chrono::steady_clock::now();
    unsigned long long loops = 0;
    bool waitTraceStarted = false;
    long long lastProgressSecond = -1;
    if (G.HangDiagnosticsEnabled)
    {
        const unsigned long long now = NowUnixMs();
        G.HangRemoteWaitActive.store(1, std::memory_order_release);
        G.HangRemoteWaitTarget.store(targetFrame, std::memory_order_release);
        G.HangRemoteWaitStartUnixMs.store(now, std::memory_order_release);
        G.HangRemoteWaitProgressUnixMs.store(now, std::memory_order_release);
        TraceHangPhase("begin", "remote-input-wait", -1, targetFrame, targetFrame, targetFrame);
    }
    for (;;)
    {
        loops++;
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            PumpNetworkLocked();
            MaybeResendNetplayStartReadyLocked();
            MaybeResendLatestInputForFrameLeadLocked();

            auto it = G.RemoteInputs.find(targetFrame);
            if (it != G.RemoteInputs.end())
            {
                const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start).count();
                const auto waitedUs = static_cast<unsigned long long>(std::max<long long>(0, elapsedUs));
                RecordRemoteInputWaitStats(waitedUs, loops);
                TraceRemoteInputWaitSpike(targetFrame, waitedUs, loops);
                if (G.Input.HealthTrace
                    && waitedUs >= static_cast<unsigned long long>(G.Input.HealthTraceWaitThresholdMs) * 1000ULL
                    && G.LastInputHealthRemoteWaitFrame != targetFrame)
                {
                    G.LastInputHealthRemoteWaitFrame = targetFrame;
                    PrintInputHealthLineLocked(
                        "remote-wait-resolved",
                        targetFrame,
                        targetFrame,
                        G.LastSentInputFrame,
                        waitedUs,
                        0,
                        0,
                        CurrentInputLeadLocked(
                            G.LastSentInputFrame == kNoFrameLimit ? targetFrame : G.LastSentInputFrame),
                        true,
                        false);
                }
                G.HangRemoteWaitActive.store(0, std::memory_order_release);
                TraceHangPhase("end", "remote-input-wait", -1, targetFrame, targetFrame, G.LastSentInputFrame);
                return it->second;
            }

            if (G.Input.HealthTrace)
            {
                const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start).count();
                const auto waitedUs = static_cast<unsigned long long>(std::max<long long>(0, elapsedUs));
                const long long progressSecond = std::max<long long>(0, elapsedUs) / 1000000LL;
                if (!waitTraceStarted || progressSecond > lastProgressSecond)
                {
                    G.HangRemoteWaitProgressUnixMs.store(NowUnixMs(), std::memory_order_release);
                    TraceHangPhase(
                        progressSecond == 0 ? "wait-start" : "wait-progress",
                        "remote-input-wait",
                        -1,
                        targetFrame,
                        targetFrame,
                        G.LastSentInputFrame);
                    waitTraceStarted = true;
                    lastProgressSecond = progressSecond;
                    PrintInputHealthLineLocked(
                        progressSecond == 0 ? "remote-wait-start" : "remote-wait-progress",
                        targetFrame,
                        targetFrame,
                        G.LastSentInputFrame,
                        waitedUs,
                        0,
                        0,
                        CurrentInputLeadLocked(
                            G.LastSentInputFrame == kNoFrameLimit ? targetFrame : G.LastSentInputFrame),
                        false,
                        false);
                }
            }
        }

        if (G.TestEnabled && G.Bootstrap.WaitTimeoutMs > 0)
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= G.Bootstrap.WaitTimeoutMs)
            {
                std::lock_guard<std::mutex> lock(G.Mutex);
                std::printf("NSMB Test: remote input timeout tUnixMs=%llu frame=%u waitedMs=%d actualElapsedMs=%lld loops=%llu peer=%d connectingPeer=%d lastSent=%u lastRecv=%u lead=%d localQueue=%zu remoteQueue=%zu delayed=%zu resendCount=%d netplayStart=%u localReady=%u remoteReady=%u\n",
                    NowUnixMs(),
                    targetFrame,
                    G.Bootstrap.WaitTimeoutMs,
                    static_cast<long long>(elapsed),
                    loops,
                    G.Transport.IsConnected() ? 1 : 0,
                    G.Transport.IsConnecting() ? 1 : 0,
                    G.LastSentInputFrame,
                    G.LastReceivedInputFrame,
                    CurrentInputLeadLocked(G.LastSentInputFrame == kNoFrameLimit ? targetFrame : G.LastSentInputFrame),
                    G.LocalInputs.size(),
                    G.RemoteInputs.size(),
                    G.DelayedInputs.size(),
                    G.InputFrameLeadResendCount,
                    G.Connection.StartFrame,
                    G.LocalNetplayStartReadyFrame,
                    G.RemoteNetplayStartReadyFrame);
                std::fflush(stdout);
                G.HangRemoteWaitActive.store(0, std::memory_order_release);
                TraceHangPhase("timeout", "remote-input-wait", -1, targetFrame, targetFrame, G.LastSentInputFrame);
                if (G.Connection.RemoteInputTimeoutFatal)
                    std::_Exit(70);
                const auto waitedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start).count();
                const auto recordedWaitedUs = static_cast<unsigned long long>(std::max<long long>(0, waitedUs));
                RecordRemoteInputWaitStats(recordedWaitedUs, loops);
                TraceRemoteInputWaitSpike(targetFrame, recordedWaitedUs, loops);
                return NeutralInput();
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool TryWaitForRollbackRemoteInputLocked(
    std::unique_lock<std::mutex>& lock,
    melonDS::NDS* nds,
    melonDS::u32 localFrame,
    melonDS::u32 targetFrame,
    InputState& input)
{
    if (G.Rollback.InputWaitUs <= 0)
        return false;
    if (!lock.owns_lock())
        return false;
    if ((G.PacketBridge.Only || G.Input.NetplayOnly)
        && G.Connection.StartFrame != 0
        && targetFrame < G.Connection.StartFrame)
        return false;

    const auto start = std::chrono::steady_clock::now();
    unsigned long long loops = 0;
    for (;;)
    {
        loops++;
        PumpNetworkLocked(nds, localFrame);

        auto it = G.RemoteInputs.find(targetFrame);
        if (it != G.RemoteInputs.end())
        {
            input = it->second;
            const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start).count();
            RecordRemoteInputWaitStats(static_cast<unsigned long long>(std::max<long long>(0, elapsedUs)), loops);
            return true;
        }

        const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
        const long long remainingUs = static_cast<long long>(G.Rollback.InputWaitUs) - elapsedUs;
        if (remainingUs <= 0)
        {
            RecordRemoteInputWaitStats(static_cast<unsigned long long>(std::max<long long>(0, elapsedUs)), loops);
            if (G.Input.NetplayTrace)
            {
                std::printf("NSMB Rollback: input wait timeout frame=%u waitedUs=%lld\n",
                    targetFrame,
                    static_cast<long long>(std::max<long long>(0, elapsedUs)));
                std::fflush(stdout);
            }
            return false;
        }

        const int pollUs = std::clamp(
            static_cast<int>(std::min<long long>(remainingUs, G.Input.WaitPollUs)),
            50,
            5000);
        G.InputCond.wait_for(lock, std::chrono::microseconds(pollUs));
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

        if (G.Harness.SeedWaitTimeoutMs > 0)
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= G.Harness.SeedWaitTimeoutMs)
            {
                std::printf("NSMB PoC: match seed wait timeout waitedMs=%d\n", G.Harness.SeedWaitTimeoutMs);
                return;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void WaitForPeerIfNeeded(bool force = false)
{
    if (!G.Enabled || (!force && !G.Harness.WaitForPeerBeforeStart) ||
        G.NetRole != Role::Host || G.Transport.IsConnected())
        return;

    const auto start = std::chrono::steady_clock::now();
    for (;;)
    {
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            PumpNetworkLocked();
            if (G.Transport.IsConnected())
                return;
        }

        if (G.Harness.SeedWaitTimeoutMs > 0)
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= G.Harness.SeedWaitTimeoutMs)
            {
                std::printf("NSMB PoC: peer wait timeout waitedMs=%d\n", G.Harness.SeedWaitTimeoutMs);
                return;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool ShouldPumpNetworkAtFrame(melonDS::u32 syncFrame, melonDS::u32 sendStartFrame)
{
    return !G.Harness.DeferNetworkUntilStart || G.Connection.StartFrame == 0 || syncFrame >= sendStartFrame;
}

bool IsInputNetplayGameplayStartReady(melonDS::NDS* nds)
{
    if (!nds || !nds->MainRAM)
        return false;
    if (!IsMarioVsLuigiGameplay(nds))
        return false;

    const ObjectScanSample stageScene = FindObjectByIDAndSettingsLoose(
        nds,
        kStageSceneObjectID,
        G.MvlCurrentStageSceneSettings);
    if (!stageScene.Found || stageScene.StateType == 0)
        return false;

    const ObjectScanSample stageController = FindObjectByID(nds, kStageControllerObjectID);
    if (!stageController.Found)
        return false;

    const PlayerActorScanSample players = FindPlayerActors(nds);
    if (!players.Actor0.Found || !players.Actor1.Found)
        return false;
    return true;
}

melonDS::u32 InputNetplayLogicalFrame(melonDS::u32 rawFrame)
{
    if (!G.Input.NetplayOnly || G.LocalNetplayStartReadyFrame == kNoFrameLimit)
        return rawFrame;
    if (rawFrame < G.LocalNetplayStartReadyFrame)
        return rawFrame;
    return G.Connection.StartFrame + (rawFrame - G.LocalNetplayStartReadyFrame);
}

bool AllNetplayStartWaitArrivedLocked()
{
    const int count = std::max(1, std::min(G.Bootstrap.TestInstanceCount, 16));
    for (int i = 0; i < count; i++)
    {
        if (!G.NetplayStartWaitArrived[i])
            return false;
    }
    return true;
}

void WaitForPeerAtNetplayStartBarrier(int instanceID, melonDS::u32 syncFrame)
{
    if (!G.Enabled || !G.Harness.WaitForPeerAtNetplayStart || G.NetRole != Role::Host
        || G.Input.NetplayOnly
        || G.Connection.StartFrame == 0 || syncFrame != G.Connection.StartFrame
        || instanceID < 0 || instanceID >= 16 || G.WaitedForPeerAtNetplayStart)
    {
        return;
    }

    const bool isLocal = (instanceID == G.Connection.LocalInstance);
    {
        std::unique_lock<std::mutex> lock(G.Mutex);
        if (G.NetplayStartWaitComplete)
            return;

        G.NetplayStartWaitArrived[instanceID] = true;
        G.BarrierCond.notify_all();

        const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(G.Harness.SeedWaitTimeoutMs);

        if (isLocal)
        {
            while (!AllNetplayStartWaitArrivedLocked())
            {
                if (G.Harness.SeedWaitTimeoutMs > 0)
                {
                    if (G.BarrierCond.wait_until(lock, deadline) == std::cv_status::timeout)
                    {
                        std::printf("NSMB PoC: netplay start local barrier timeout inst=%d frame=%u waitedMs=%d\n",
                            instanceID,
                            syncFrame,
                            G.Harness.SeedWaitTimeoutMs);
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
                if (G.Harness.SeedWaitTimeoutMs > 0)
                {
                    if (G.BarrierCond.wait_until(lock, deadline) == std::cv_status::timeout)
                    {
                        std::printf("NSMB PoC: netplay start peer wait barrier timeout inst=%d frame=%u waitedMs=%d\n",
                            instanceID,
                            syncFrame,
                            G.Harness.SeedWaitTimeoutMs);
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

void WaitForRemoteNetplayStartReadyIfNeeded(melonDS::NDS* nds, melonDS::u32 syncFrame)
{
    if (!G.Enabled || !G.Input.NetplayOnly || !G.Harness.WaitForPeerAtNetplayStart
        || G.Connection.StartFrame == 0 || syncFrame < G.Connection.StartFrame
        || G.WaitedForPeerAtNetplayStart)
    {
        return;
    }
    if (!IsInputNetplayGameplayStartReady(nds))
        return;

    if (G.LocalNetplayStartReadyFrame == kNoFrameLimit)
    {
        G.LocalNetplayStartReadyFrame = syncFrame;
        G.RemoteNetplayStartReadyAfterLocal = false;
    }

    std::printf("NSMB InputNetplay: waiting for remote gameplay start ready localFrame=%u logicalStart=%u\n",
        syncFrame,
        G.Connection.StartFrame);
    std::fflush(stdout);

    const auto start = std::chrono::steady_clock::now();
    for (;;)
    {
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            PumpNetworkLocked(nds, syncFrame);
            SendMatchSeedLocked();
            SendNetplayStartReadyLocked(syncFrame);
            MaybeResendNetplayStartReadyLocked(true);
            const bool hasPostStartRemoteInput = SessionPolicy::HasPostStartRemoteInput(
                G.LastReceivedInputFrame != kNoFrameLimit,
                G.LastReceivedInputFrame,
                G.Connection.StartFrame,
                G.Connection.Delay);
            if (SessionPolicy::ShouldAcceptStartReady(
                    G.RemoteNetplayStartReadyFrame != kNoFrameLimit,
                    G.RemoteNetplayStartReadyAfterLocal,
                    hasPostStartRemoteInput))
            {
                G.WaitedForPeerAtNetplayStart = true;
                PrimeInputNetplayEpochStartLocked(syncFrame);
                EmitStartReadyEventLocked("accept", syncFrame, G.RemoteNetplayStartReadyFrame);
                std::printf("NSMB InputNetplay: remote gameplay start ready accepted remoteFrame=%u localFrame=%u logicalStart=%u\n",
                    G.RemoteNetplayStartReadyFrame,
                    syncFrame,
                    G.Connection.StartFrame);
                std::fflush(stdout);
                return;
            }
        }

        if (G.Harness.SeedWaitTimeoutMs > 0)
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= G.Harness.SeedWaitTimeoutMs)
            {
                std::printf("NSMB InputNetplay: remote start ready wait timeout frame=%u waitedMs=%d\n",
                    syncFrame,
                    G.Harness.SeedWaitTimeoutMs);
                std::fflush(stdout);
                G.LocalNetplayStartReadyFrame = kNoFrameLimit;
                G.RemoteNetplayStartReadyAfterLocal = false;
                G.NetplayStartReadySent = false;
                return;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool WaitAtFrameBarrier(FrameBarrier& barrier, int instanceID, melonDS::u32 frame, const char* name)
{
    if (!G.TestEnabled || !G.Harness.FrameBarrierEnabled || G.Bootstrap.TestInstanceCount <= 1)
        return true;
    if (instanceID < 0 || instanceID >= G.Bootstrap.TestInstanceCount)
        return true;

    std::unique_lock<std::mutex> lock(G.Mutex);
    const int generation = barrier.Generation;
    barrier.Waiting[instanceID] = true;
    barrier.Frame[instanceID] = frame;

    const auto allArrived = [&]() {
        for (int i = 0; i < G.Bootstrap.TestInstanceCount; i++)
        {
            if (!barrier.Waiting[i] || barrier.Frame[i] != frame)
                return false;
        }
        return true;
    };

    const auto release = [&]() {
        for (int i = 0; i < G.Bootstrap.TestInstanceCount; i++)
            barrier.Waiting[i] = false;
        barrier.Generation++;
        G.BarrierCond.notify_all();
    };

    if (allArrived())
    {
        release();
        return true;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(G.Bootstrap.WaitTimeoutMs);
    while (barrier.Generation == generation)
    {
        if (G.Bootstrap.WaitTimeoutMs > 0)
        {
            if (G.BarrierCond.wait_until(lock, deadline) == std::cv_status::timeout)
            {
                std::printf("NSMB Test: %s frame barrier timeout inst=%d frame=%u waitedMs=%d\n",
                    name,
                    instanceID,
                    frame,
                    G.Bootstrap.WaitTimeoutMs);
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
    if (!G.TestEnabled || !G.Harness.SerialRunEnabled || G.Bootstrap.TestInstanceCount <= 1)
        return true;
    if (instanceID < 0 || instanceID >= G.Bootstrap.TestInstanceCount)
        return true;

    std::unique_lock<std::mutex> lock(G.Mutex);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(G.Bootstrap.WaitTimeoutMs);
    for (;;)
    {
        if (G.SerialFrame == frame && G.SerialInstance == instanceID)
            return true;

        if (G.Bootstrap.WaitTimeoutMs > 0)
        {
            if (G.BarrierCond.wait_until(lock, deadline) == std::cv_status::timeout)
            {
                std::printf("NSMB Test: serial run timeout inst=%d frame=%u expectedInst=%d expectedFrame=%u waitedMs=%d\n",
                    instanceID,
                    frame,
                    G.SerialInstance,
                    G.SerialFrame,
                    G.Bootstrap.WaitTimeoutMs);
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
    if (!G.TestEnabled || !G.Harness.SerialRunEnabled || G.Bootstrap.TestInstanceCount <= 1)
        return;
    if (instanceID < 0 || instanceID >= G.Bootstrap.TestInstanceCount)
        return;

    std::lock_guard<std::mutex> lock(G.Mutex);
    if (G.SerialFrame != frame || G.SerialInstance != instanceID)
        return;

    G.SerialInstance++;
    if (G.SerialInstance >= G.Bootstrap.TestInstanceCount)
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

bool IsMainRAMAddress(melonDS::NDS* nds, melonDS::u32 addr, melonDS::u32 size = 1)
{
    if (!nds || !nds->MainRAM || addr < kMainRAMBase)
        return false;

    const melonDS::u32 offset = addr - kMainRAMBase;
    const melonDS::u32 ramLen = nds->MainRAMMask + 1;
    return offset < ramLen && size <= ramLen - offset;
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

bool WriteMainRAMU8(melonDS::NDS* nds, melonDS::u32 offset, melonDS::u8 value)
{
    if (!nds || !nds->MainRAM)
        return false;
    if (offset + sizeof(value) > nds->MainRAMMask + 1)
        return false;

    nds->MainRAM[offset] = value;
    return true;
}

bool MainRAMOffsetFromAddr(melonDS::NDS* nds, melonDS::u32 addr, melonDS::u32 size, melonDS::u32& offset)
{
    if (!nds || !nds->MainRAM || addr < kMainRAMBase)
        return false;

    offset = addr - kMainRAMBase;
    return offset + size <= nds->MainRAMMask + 1;
}

bool WriteMainRAMAddrU8IfChanged(melonDS::NDS* nds, melonDS::u32 addr, melonDS::u8 value)
{
    melonDS::u32 off = 0;
    if (!MainRAMOffsetFromAddr(nds, addr, sizeof(value), off))
        return false;

    if (nds->MainRAM[off] == value)
        return true;
    return WriteMainRAMU8(nds, off, value);
}

bool WriteMainRAMAddrU32IfChanged(melonDS::NDS* nds, melonDS::u32 addr, melonDS::u32 value)
{
    melonDS::u32 off = 0;
    melonDS::u32 current = 0;
    if (!MainRAMOffsetFromAddr(nds, addr, sizeof(value), off))
        return false;
    if (!ReadMainRAMU32(nds, off, current))
        return false;

    if (current == value)
        return true;
    return WriteMainRAMU32(nds, off, value);
}

bool WriteARM9U32(melonDS::NDS* nds, melonDS::u32 addr, melonDS::u32 value)
{
    if (!nds || (addr & 3) != 0)
        return false;

    nds->ARM9Write32(addr, value);
    return true;
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

void EmitStrImm(std::vector<melonDS::u32>& code, int rd, int rn, melonDS::u32 offset)
{
    EmitARM(code, 0xE5800000u
        | (static_cast<melonDS::u32>(rn & 0xF) << 16)
        | (static_cast<melonDS::u32>(rd & 0xF) << 12)
        | (offset & 0xFFF));
}

void EmitStrbImm(std::vector<melonDS::u32>& code, int rd, int rn, melonDS::u32 offset)
{
    EmitARM(code, 0xE5C00000u
        | (static_cast<melonDS::u32>(rn & 0xF) << 16)
        | (static_cast<melonDS::u32>(rd & 0xF) << 12)
        | (offset & 0xFFF));
}

void EmitLdrImm(std::vector<melonDS::u32>& code, int rd, int rn, melonDS::u32 offset)
{
    EmitARM(code, 0xE5900000u
        | (static_cast<melonDS::u32>(rn & 0xF) << 16)
        | (static_cast<melonDS::u32>(rd & 0xF) << 12)
        | (offset & 0xFFF));
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

void EmitNormalizeMvlEntranceSpawnState(std::vector<melonDS::u32>& code)
{
    EmitLoadImm(code, 0, kEntranceSpawnEntranceIDAddr);
    EmitMovImm(code, 1, 0);
    EmitStrbImm(code, 1, 0, 0);
    EmitMovImm(code, 1, 1);
    EmitStrbImm(code, 1, 0, 1);
    EmitLoadImm(code, 0, kEntranceTransitionFlagsAddr);
    EmitMovImm(code, 1, 0);
    EmitStrbImm(code, 1, 0, 0);
    EmitStrbImm(code, 1, 0, 1);
    EmitLoadImm(code, 0, kEntranceSpawnEntranceAddr);
    EmitLdrImm(code, 1, 0, 0);
    EmitARM(code, 0xE2812014u); // add r2, r1, #0x14
    EmitStrImm(code, 2, 0, sizeof(melonDS::u32));
}

void EmitClearInitialPlayerInventoryPowerups(std::vector<melonDS::u32>& code)
{
    EmitLoadImm(code, 0, kGamePlayerInventoryPowerupAddr);
    EmitMovImm(code, 1, 0);
    EmitStrbImm(code, 1, 0, 0);
    EmitStrbImm(code, 1, 0, 1);
}

bool InjectDirectMvlBootCall(int instanceID, melonDS::u32 frame, melonDS::NDS* nds, bool autoRestart = false)
{
    if ((!G.Mvl.DirectBootEnabled && !autoRestart) || !nds || instanceID < 0 || instanceID >= 16)
        return false;
    if (G.Mvl.DirectBootHostOnly && G.NetRole != Role::Host)
        return false;
    if (G.Mvl.DirectBootClientOnly && G.NetRole != Role::Client)
        return false;
    if (G.DirectMvlBootApplied[instanceID] || (!autoRestart && frame < G.Mvl.DirectBootFrame))
        return false;

    int defaultPlayerID = instanceID;
    if (G.NetRole == Role::Host)
        defaultPlayerID = 0;
    else if (G.NetRole == Role::Client)
        defaultPlayerID = 1;
    const int playerID = std::clamp(
        G.Mvl.DirectBootPlayerID >= 0 ? G.Mvl.DirectBootPlayerID : defaultPlayerID,
        0,
        1);
    const int scene = std::clamp(G.Mvl.DirectBootScene, 0, 0xFFFF);
    if (G.Mvl.CourseMode == "random" && !G.MatchSeedConfigured && G.Mvl.StageSequence.empty())
        return false;
    const int stage = std::clamp(MvlStageForGame(instanceID), 0, 4);
    G.MvlCurrentStage = stage;
    G.MvlCurrentStageSceneSettings = ComposeMvlSceneSettingsForStage(stage);
    const melonDS::u32 oldPC = nds->ARM9.R[15] - ((nds->ARM9.CPSR & 0x20) ? 2 : 4);
    const melonDS::u32 returnPC = oldPC | ((nds->ARM9.CPSR & 0x20) ? 1u : 0u);

    std::vector<melonDS::u32> code;
    code.reserve(64);
    EmitARM(code, 0xE92D5FFFu); // push {r0-r12, lr}
    EmitARM(code, 0xE10F5000u); // mrs r5, cpsr
    EmitARM(code, 0xE92D0020u); // push {r5}
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
    EmitNormalizeMvlEntranceSpawnState(code);
    EmitClearInitialPlayerInventoryPowerups(code);
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
        "NSMB DirectBoot: inst=%d frame=%u trampoline=%08X return=%08X mode=loadLevel scene=%d player=%d stage=%d\n",
        instanceID,
        frame,
        kDirectBootTrampolineAddr,
        returnPC,
        scene,
        playerID,
        stage);
    std::fflush(stdout);
    nds->ARM9.JumpTo(kDirectBootTrampolineAddr);
    return true;
}

bool WriteNetAndGameRandomSeed(melonDS::NDS* nds, melonDS::u32 seed);

void SaveMvlAutoRestartBootstrapCheckpointIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.Mvl.AutoRestartAfterResult || G.Mvl.TargetWins <= 1 || !nds)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (!G.MvlAutoRestartBootstrapCheckpoint[instanceID].empty())
        return;
    if (G.MvlAutoRestartCount[instanceID] != 0 || G.MvlAutoRestartInResult[instanceID])
        return;
    const bool directBootReady = G.Mvl.DirectBootEnabled
        && !G.DirectMvlBootApplied[instanceID]
        && frame >= G.Mvl.DirectBootFrame;
    const melonDS::u32 generatedBootstrapFrame = G.Mvl.AutoRestartBootstrapFrame;
    const melonDS::u32 stageGroup = nds->ARM9Read32(kGameStageGroupAddr);
    const melonDS::u16 currentScene = nds->ARM9Read16(kSceneCurrentSceneIDAddr);
    const melonDS::u16 nextScene = nds->ARM9Read16(kSceneNextSceneIDAddr);
    const bool generatedRomReady = !G.Mvl.DirectBootEnabled
        && frame >= generatedBootstrapFrame
        && stageGroup != 9
        && currentScene == 0x0004
        && nextScene == 0x0006
        && nds->ARM9Read16(kSceneIsSceneActiveAddr) != 0;
    if (!directBootReady && !generatedRomReady)
        return;

    melonDS::Savestate state;
    if (state.Error || !nds->DoSavestate(&state) || state.Error)
    {
        if (!G.MvlAutoRestartBootstrapCheckpointLogged[instanceID])
        {
            std::printf("NSMB MvL auto restart: failed to save bootstrap checkpoint inst=%d frame=%u\n",
                instanceID,
                frame);
            std::fflush(stdout);
            G.MvlAutoRestartBootstrapCheckpointLogged[instanceID] = true;
        }
        return;
    }

    G.MvlAutoRestartBootstrapCheckpoint[instanceID].assign(
        reinterpret_cast<const char*>(state.Buffer()),
        reinterpret_cast<const char*>(state.Buffer()) + state.Length());
    G.MvlAutoRestartBootstrapCheckpointFrame[instanceID] = frame;
    G.MvlAutoRestartBootstrapCheckpointLogged[instanceID] = true;
    std::printf("NSMB MvL auto restart: saved bootstrap checkpoint inst=%d frame=%u bytes=%u scene=%04X stageGroup=%u\n",
        instanceID,
        frame,
        state.Length(),
        currentScene,
        stageGroup);
    std::fflush(stdout);
}

void SchedulePacketBridgeJitHelperPatchAfterRestore(
    int instanceID,
    melonDS::u32 restoreFrame,
    melonDS::u32 checkpointFrame)
{
    if (instanceID < 0 || instanceID >= 16)
        return;

    G.PacketBridgeJitHelperPatchResumeFrame[instanceID] = 0;
    if (!G.RuntimePatch.PacketBridgeJitHelperPatchEnabled)
    {
        G.PacketBridgeJitHelperPatchApplied[instanceID] = false;
        return;
    }

    if (checkpointFrame >= G.RuntimePatch.PacketBridgeJitHelperPatchFrame)
    {
        G.PacketBridgeJitHelperPatchApplied[instanceID] = true;
        std::printf(
            "NSMB MvL auto restart: keeping packet bridge JIT helper patch inst=%d restoreFrame=%u checkpointFrame=%u patchFrame=%u\n",
            instanceID,
            restoreFrame,
            checkpointFrame,
            G.RuntimePatch.PacketBridgeJitHelperPatchFrame);
        std::fflush(stdout);
        return;
    }

    melonDS::u32 delay = G.RuntimePatch.PacketBridgeJitHelperPatchFrame - checkpointFrame;
    if (delay > 6)
        delay -= 6;
    G.PacketBridgeJitHelperPatchApplied[instanceID] = false;
    G.PacketBridgeJitHelperPatchResumeFrame[instanceID] = restoreFrame + delay;
    std::printf(
        "NSMB MvL auto restart: scheduled packet bridge JIT helper patch inst=%d restoreFrame=%u checkpointFrame=%u patchFrame=%u resumeFrame=%u\n",
        instanceID,
        restoreFrame,
        checkpointFrame,
        G.RuntimePatch.PacketBridgeJitHelperPatchFrame,
        G.PacketBridgeJitHelperPatchResumeFrame[instanceID]);
    std::fflush(stdout);
}

bool RestoreMvlAutoRestartBootstrapCheckpoint(int instanceID, melonDS::u32 frame, melonDS::NDS* nds, melonDS::u32 requestedSeed)
{
    if (!nds || instanceID < 0 || instanceID >= 16)
        return false;
    if (G.MvlAutoRestartBootstrapCheckpoint[instanceID].empty())
        return false;

    ResetMvlAutoRestartStartupHookState(instanceID);
    RebaseMvlAutoRestartStartupFramesFromCheckpoint(
        instanceID,
        frame,
        G.MvlAutoRestartBootstrapCheckpointFrame[instanceID]);
    melonDS::Savestate state(
        G.MvlAutoRestartBootstrapCheckpoint[instanceID].data(),
        static_cast<melonDS::u32>(G.MvlAutoRestartBootstrapCheckpoint[instanceID].size()),
        false);
    if (state.Error || !nds->DoSavestate(&state) || state.Error)
    {
        std::printf("NSMB MvL auto restart: failed to restore bootstrap checkpoint inst=%d frame=%u bytes=%zu\n",
            instanceID,
            frame,
            G.MvlAutoRestartBootstrapCheckpoint[instanceID].size());
        std::fflush(stdout);
        return false;
    }

    InvalidateMainRAMJIT(nds, nds->MainRAMMask + 1);
    melonDS::Platform::MP_Begin(nds->UserData);
    G.NetRandomPatchApplied[instanceID] = false;
    G.NetRandomPatchValue = requestedSeed;
    G.NetRandomPatchEnabled = true;
    G.NetRandomPatchAuto = true;
    if (G.NetRole == Role::Host)
        WriteARM9U32(nds, kNetLocalAidAddr, 0);
    else if (G.NetRole == Role::Client)
        WriteARM9U32(nds, kNetLocalAidAddr, 1);
    WriteNetAndGameRandomSeed(nds, requestedSeed);
    ApplyMvlRuntimeConfigIfNeeded(nds);
    std::printf("NSMB MvL auto restart: restored bootstrap checkpoint inst=%d frame=%u seed=0x%08X bytes=%zu\n",
        instanceID,
        frame,
        requestedSeed,
        G.MvlAutoRestartBootstrapCheckpoint[instanceID].size());
    std::fflush(stdout);
    return true;
}

bool ResetMvlAutoRestartConsoleForNextMatch(int instanceID, melonDS::u32 frame, melonDS::NDS* nds, melonDS::u32 requestedSeed)
{
    if (!nds || instanceID < 0 || instanceID >= 16 || !nds->GetNDSCart())
        return false;

    ResetMvlAutoRestartStartupHookState(instanceID);
    RebaseMvlAutoRestartStartupFrames(instanceID, frame);
    nds->Reset();
    ApplyMvlRuntimeConfigIfNeeded(nds);
    WriteNetAndGameRandomSeed(nds, requestedSeed);
    nds->SetupDirectBoot(std::string {});
    nds->Start();
    melonDS::Platform::MP_Begin(nds->UserData);
    ApplyMvlRuntimeConfigIfNeeded(nds);
    WriteNetAndGameRandomSeed(nds, requestedSeed);
    G.NetRandomPatchEnabled = true;
    G.NetRandomPatchAuto = true;
    G.NetRandomPatchApplied[instanceID] = false;
    G.NetRandomPatchValue = requestedSeed;
    std::printf(
        "NSMB MvL auto restart: hard reset console for next match inst=%d frame=%u seed=0x%08X\n",
        instanceID,
        frame,
        requestedSeed);
    std::fflush(stdout);
    return true;
}

struct MvlResultSnapshot
{
    melonDS::u32 BattleStars[2] {};
    melonDS::u32 DisplayedStars[2] {};
    melonDS::u32 CollectedStars[2] {};
    melonDS::u32 Lives[2] {};
    melonDS::u32 Deaths[2] {};
    melonDS::u32 Dead[2] {};
};

MvlResultSnapshot ReadMvlResultSnapshot(melonDS::NDS* nds)
{
    MvlResultSnapshot result {};
    if (!nds)
        return result;

    for (melonDS::u32 player = 0; player < 2; player++)
    {
        const melonDS::u32 wordOffset = sizeof(melonDS::u32) * player;
        result.BattleStars[player] = nds->ARM9Read32(kGamePlayerBattleStarsAddr + wordOffset);
        result.DisplayedStars[player] = nds->ARM9Read32(kGamePlayerDisplayedStarsAddr + wordOffset);
        result.CollectedStars[player] = nds->ARM9Read32(kGamePlayerCollectedStarsAddr + wordOffset);
        result.Lives[player] = nds->ARM9Read32(kGamePlayerLivesAddr + wordOffset);
        result.Deaths[player] = nds->ARM9Read32(kGamePlayerDeathsAddr + wordOffset);
        result.Dead[player] = nds->ARM9Read8(kGamePlayerDeadAddr + player);
        if (G.Mvl.LifeModeSelector != 2 && result.Dead[player] != 0)
        {
            result.Lives[player] = 0;
            result.Deaths[player] = std::max(result.Deaths[player], G.Mvl.InitialLives);
        }
    }
    return result;
}

int ResolveMvlResultWinner(const MvlResultSnapshot& result)
{
    auto higherWins = [](melonDS::u32 a, melonDS::u32 b) -> int {
        if (a == b)
            return -1;
        return a > b ? 0 : 1;
    };
    auto lowerWins = [](melonDS::u32 a, melonDS::u32 b) -> int {
        if (a == b)
            return -1;
        return a < b ? 0 : 1;
    };

    const bool player0Dead = result.Dead[0] != 0;
    const bool player1Dead = result.Dead[1] != 0;
    if (player0Dead != player1Dead)
        return player0Dead ? 1 : 0;

    if (int winner = higherWins(result.BattleStars[0], result.BattleStars[1]); winner >= 0)
        return winner;
    if (int winner = higherWins(result.DisplayedStars[0], result.DisplayedStars[1]); winner >= 0)
        return winner;
    if (int winner = higherWins(result.CollectedStars[0], result.CollectedStars[1]); winner >= 0)
        return winner;

    if (int winner = higherWins(result.Lives[0], result.Lives[1]); winner >= 0)
        return winner;
    if (int winner = lowerWins(result.Deaths[0], result.Deaths[1]); winner >= 0)
        return winner;

    return -1;
}

bool RestartMvlAfterResultIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.Mvl.AutoRestartAfterResult || G.Mvl.TargetWins < 1 || !nds || instanceID < 0 || instanceID >= 16)
        return false;

    constexpr melonDS::u16 kResultsScene = 0x000A;
    const melonDS::u16 currentScene = nds->ARM9Read16(kSceneCurrentSceneIDAddr);
    if (currentScene != kResultsScene)
    {
        if (G.MvlAutoRestartCount[instanceID] > 0
            && currentScene == 0x0003
            && nds->ARM9Read16(kScenePreviousSceneIDAddr) == kResultsScene
            && nds->ARM9Read16(kSceneNextSceneIDAddr) == 0x0003
            && frame - G.MvlAutoRestartLastRestartFrame[instanceID] >= 30)
        {
            nds->ARM9Write16(kSceneNextSceneIDAddr, 0x0181);
        }
        G.MvlAutoRestartInResult[instanceID] = false;
        G.MvlAutoRestartResultScored[instanceID] = false;
        G.MvlAutoRestartResultUnresolvedLogged[instanceID] = false;
        return false;
    }

    if (!G.MvlAutoRestartInResult[instanceID])
    {
        G.MvlAutoRestartInResult[instanceID] = true;
        G.MvlAutoRestartResultUnresolvedLogged[instanceID] = false;
        G.MvlAutoRestartResultFrame[instanceID] = frame;
        return false;
    }

    if (!G.MvlAutoRestartResultScored[instanceID])
    {
        const MvlResultSnapshot result = ReadMvlResultSnapshot(nds);
        const int winner = ResolveMvlResultWinner(result);
        if (winner < 0)
        {
            if (!G.MvlAutoRestartResultUnresolvedLogged[instanceID]
                && frame - G.MvlAutoRestartResultFrame[instanceID] >= G.Mvl.AutoRestartDelayFrames)
            {
                std::printf(
                    "NSMB MvL auto restart: result unresolved inst=%d frame=%u stars=%u/%u displayed=%u/%u collected=%u/%u lives=%u/%u deaths=%u/%u dead=%u/%u matchWins=%d/%d target=%d\n",
                    instanceID,
                    frame,
                    result.BattleStars[0],
                    result.BattleStars[1],
                    result.DisplayedStars[0],
                    result.DisplayedStars[1],
                    result.CollectedStars[0],
                    result.CollectedStars[1],
                    result.Lives[0],
                    result.Lives[1],
                    result.Deaths[0],
                    result.Deaths[1],
                    result.Dead[0],
                    result.Dead[1],
                    G.MvlAutoRestartWins[instanceID][0],
                    G.MvlAutoRestartWins[instanceID][1],
                    G.Mvl.TargetWins);
                std::fflush(stdout);
                G.MvlAutoRestartResultUnresolvedLogged[instanceID] = true;
            }
            return false;
        }
        G.MvlAutoRestartWins[instanceID][winner]++;
        G.MvlAutoRestartResultScored[instanceID] = true;
        std::printf(
            "NSMB MvL auto restart: result inst=%d frame=%u winner=%d stars=%u/%u displayed=%u/%u collected=%u/%u lives=%u/%u deaths=%u/%u dead=%u/%u matchWins=%d/%d target=%d\n",
            instanceID,
            frame,
            winner,
            result.BattleStars[0],
            result.BattleStars[1],
            result.DisplayedStars[0],
            result.DisplayedStars[1],
            result.CollectedStars[0],
            result.CollectedStars[1],
            result.Lives[0],
            result.Lives[1],
            result.Deaths[0],
            result.Deaths[1],
            result.Dead[0],
            result.Dead[1],
            G.MvlAutoRestartWins[instanceID][0],
            G.MvlAutoRestartWins[instanceID][1],
            G.Mvl.TargetWins);
        std::fflush(stdout);
    }

    const int leadingWins = std::max(G.MvlAutoRestartWins[instanceID][0], G.MvlAutoRestartWins[instanceID][1]);
    if (leadingWins >= G.Mvl.TargetWins)
        return false;
    if (frame - G.MvlAutoRestartResultFrame[instanceID] < G.Mvl.AutoRestartDelayFrames)
        return false;

    const int nextRestartCount = G.MvlAutoRestartCount[instanceID] + 1;
    G.MvlAutoRestartCount[instanceID] = nextRestartCount;
    const int requestedStage = std::clamp(MvlStageForGame(instanceID), 0, 4);
    const melonDS::u32 requestedSeed = MatchSeedForGame(instanceID);
    G.MvlCurrentStage = requestedStage;
    G.MvlCurrentStageSceneSettings = ComposeMvlSceneSettingsForStage(requestedStage);
    WriteNetAndGameRandomSeed(nds, requestedSeed);
    int restartPath = 0;
    if (RestoreMvlAutoRestartBootstrapCheckpoint(instanceID, frame, nds, requestedSeed))
    {
        restartPath = 3;
    }
    else if (!G.MvlAutoRestartCheckpoint[instanceID].empty()
        && G.MvlAutoRestartCheckpointStage[instanceID] == requestedStage)
    {
        melonDS::Savestate state(
            G.MvlAutoRestartCheckpoint[instanceID].data(),
            static_cast<melonDS::u32>(G.MvlAutoRestartCheckpoint[instanceID].size()),
            false);
        if (!state.Error && nds->DoSavestate(&state) && !state.Error)
        {
            restartPath = 1;
            InvalidateMainRAMJIT(nds, nds->MainRAMMask + 1);
            melonDS::Platform::MP_Begin(nds->UserData);
            SchedulePacketBridgeJitHelperPatchAfterRestore(
                instanceID,
                frame,
                G.MvlAutoRestartCheckpointFrame[instanceID]);
            WriteARM9U32(nds, kGameStageGroupAddr, 0x00000009);
            WriteARM9U32(nds, kGameVsModeAddr, 0x00000001);
            WriteARM9U32(nds, kSceneNextSceneSettingsAddr, G.MvlCurrentStageSceneSettings);
            ApplyMvlRuntimeConfigIfNeeded(nds);
            WriteNetAndGameRandomSeed(nds, requestedSeed);
        }
    }
    else if (ResetMvlAutoRestartConsoleForNextMatch(instanceID, frame, nds, requestedSeed))
    {
        restartPath = 4;
    }

    if (restartPath == 0)
    {
        G.MvlAutoRestartCount[instanceID] = nextRestartCount - 1;
        std::printf(
            "NSMB MvL auto restart: failed inst=%d frame=%u nextGame=%d requestedStage=%d seed=0x%08X reason=no-compatible-checkpoint\n",
            instanceID,
            frame,
            nextRestartCount + 1,
            requestedStage,
            requestedSeed);
        std::fflush(stdout);
        return false;
    }
    G.MvlAutoRestartLastRestartFrame[instanceID] = frame;
    ResetMvlRuntimeSyncStateForRestart(instanceID, frame);
    G.MvlAutoRestartInResult[instanceID] = false;
    G.MvlAutoRestartResultScored[instanceID] = false;
    G.MvlAutoRestartResultUnresolvedLogged[instanceID] = false;
    const int actualStage = static_cast<int>(nds->ARM9Read32(kGameStageIDAddr));
    std::printf(
        "NSMB MvL auto restart: inst=%d frame=%u nextGame=%d stage=%d requestedStage=%d seed=0x%08X matchWins=%d/%d target=%d checkpoint=%d\n",
        instanceID,
        frame,
        G.MvlAutoRestartCount[instanceID] + 1,
        actualStage,
        requestedStage,
        requestedSeed,
        G.MvlAutoRestartWins[instanceID][0],
        G.MvlAutoRestartWins[instanceID][1],
        G.Mvl.TargetWins,
        restartPath);
    std::fflush(stdout);
    return true;
}

bool ShouldPauseInputNetplayForMvlAutoRestart(int instanceID, melonDS::NDS* nds)
{
    if (!G.Input.NetplayOnly || !G.Mvl.AutoRestartAfterResult || G.Mvl.TargetWins <= 1
        || !nds || instanceID < 0 || instanceID >= 16)
    {
        return false;
    }

    constexpr melonDS::u16 kResultsScene = 0x000A;
    if (nds->ARM9Read16(kSceneCurrentSceneIDAddr) != kResultsScene)
        return false;
    if (!G.MvlAutoRestartInResult[instanceID])
        return false;

    const int leadingWins = std::max(G.MvlAutoRestartWins[instanceID][0], G.MvlAutoRestartWins[instanceID][1]);
    return leadingWins < G.Mvl.TargetWins;
}

void SaveMvlAutoRestartCheckpointIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.Mvl.AutoRestartAfterResult || G.Mvl.TargetWins <= 1 || !nds || !nds->MainRAM)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (!G.MvlAutoRestartCheckpoint[instanceID].empty())
        return;
    if (G.MvlAutoRestartInResult[instanceID] || G.MvlAutoRestartCount[instanceID] != 0)
        return;
    if (nds->ARM9Read16(kSceneCurrentSceneIDAddr) != 0x0003)
        return;
    if (nds->ARM9Read16(kSceneIsSceneActiveAddr) == 0)
        return;
    if (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1)
        return;

    const GameStateSample sample = ReadGameStateSample(nds);
    if (!sample.PlayerActor0Found || !sample.PlayerActor1Found || !sample.VsStarActorFound || !sample.StageSceneFound)
        return;
    if (sample.StageSceneStateType != 1)
        return;

    melonDS::Savestate state;
    if (state.Error || !nds->DoSavestate(&state) || state.Error)
    {
        if (!G.MvlAutoRestartCheckpointLogged[instanceID])
        {
            std::printf("NSMB MvL auto restart: failed to save checkpoint inst=%d frame=%u\n", instanceID, frame);
            std::fflush(stdout);
            G.MvlAutoRestartCheckpointLogged[instanceID] = true;
        }
        return;
    }

    G.MvlAutoRestartCheckpoint[instanceID].assign(
        reinterpret_cast<const char*>(state.Buffer()),
        reinterpret_cast<const char*>(state.Buffer()) + state.Length());
    G.MvlAutoRestartCheckpointFrame[instanceID] = frame;
    G.MvlAutoRestartCheckpointStage[instanceID] = static_cast<int>(sample.StageID);
    G.MvlAutoRestartCheckpointLogged[instanceID] = true;
    std::printf(
        "NSMB MvL auto restart: saved checkpoint inst=%d frame=%u bytes=%u stage=%u settings=0x%08X\n",
        instanceID,
        frame,
        state.Length(),
        sample.StageID,
        sample.StageSceneSettings);
    std::fflush(stdout);
}

ObjectScanSample GetPlayerActorCached(int instanceID, int player, melonDS::NDS* nds)
{
    ObjectScanSample actor;
    if (instanceID < 0 || instanceID >= 16 || player < 0 || player > 1)
        return actor;

    const melonDS::u32 cachedBase = G.PlayerActorBaseCache[instanceID][player];
    const melonDS::u32 cachedGUID = G.PlayerActorGUIDCache[instanceID][player];
    if (cachedBase != 0 && ReadPlayerActorByBase(nds, cachedBase, cachedGUID, actor))
        return actor;

    const PlayerActorScanSample players = FindPlayerActors(nds);
    actor = (player == 0) ? players.Actor0 : players.Actor1;
    if (actor.Found)
    {
        G.PlayerActorBaseCache[instanceID][player] = actor.Base;
        G.PlayerActorGUIDCache[instanceID][player] = actor.GUID;
    }
    else
    {
        G.PlayerActorBaseCache[instanceID][player] = 0;
        G.PlayerActorGUIDCache[instanceID][player] = 0;
    }
    return actor;
}

std::string Hex32(melonDS::u32 value)
{
    std::ostringstream out;
    out << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

bool EnsureDiagnosticEventsOpenLocked()
{
    if (!G.DiagnosticEventsEnabled || G.DiagnosticEventsPath.empty())
        return false;
    if (G.DiagnosticEvents.is_open())
        return true;

    const std::filesystem::path path(G.DiagnosticEventsPath);
    std::error_code ec;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), ec);
    G.DiagnosticEvents.open(path, std::ios::out | std::ios::app | std::ios::binary);
    if (!G.DiagnosticEvents)
    {
        std::printf("NSMB Diagnostics: failed to open event log: %s\n", path.string().c_str());
        G.DiagnosticEventsEnabled = false;
        return false;
    }
    return true;
}

void WriteDiagnosticEventLocked(const std::string& json)
{
    if (!EnsureDiagnosticEventsOpenLocked())
        return;
    G.DiagnosticEvents << json << '\n';
    G.DiagnosticEvents.flush();
}

void EmitStartReadyEventLocked(const char* direction, melonDS::u32 localFrame, melonDS::u32 remoteFrame)
{
    if (!G.DiagnosticEventsEnabled)
        return;

    const long long delta = (localFrame == kNoFrameLimit || remoteFrame == kNoFrameLimit)
        ? 0
        : static_cast<long long>(remoteFrame) - static_cast<long long>(localFrame);
    std::ostringstream json;
    json << "{\"event\":\"start_ready\","
         << "\"role\":\"" << (G.NetRole == Role::Host ? "host" : "client") << "\","
         << "\"direction\":\"" << (direction ? direction : "unknown") << "\","
         << "\"localFrame\":" << localFrame << ","
         << "\"remoteFrame\":" << remoteFrame << ","
         << "\"delta\":" << delta << ","
         << "\"logicalStart\":" << G.Connection.StartFrame << ","
         << "\"lastSentInputFrame\":" << G.LastSentInputFrame << ","
         << "\"lastReceivedInputFrame\":" << G.LastReceivedInputFrame << ","
         << "\"localQueue\":" << G.LocalInputs.size() << ","
         << "\"remoteQueue\":" << G.RemoteInputs.size() << ","
         << "\"delayedInputs\":" << G.DelayedInputs.size()
         << "}";
    WriteDiagnosticEventLocked(json.str());
}

void EmitDiagnosticStartupEvent()
{
    if (!G.DiagnosticEventsEnabled)
        return;

    std::ostringstream json;
    json << "{\"event\":\"diagnostic_started\","
         << "\"role\":\"" << (G.NetRole == Role::Host ? "host" : "client") << "\","
         << "\"ringFrames\":" << G.DiagnosticRingFrames << ","
         << "\"stateSync\":" << (G.StateSync.GameEnabled ? "true" : "false") << ","
         << "\"stateSyncExtended\":" << (G.StateSync.GameExtended ? "true" : "false") << ","
         << "\"stateSyncInterval\":" << G.StateSync.GameInterval << ","
         << "\"diagnosticsFile\":\"" << JsonEscape(G.DiagnosticsPath) << "\","
         << "\"eventsFile\":\"" << JsonEscape(G.DiagnosticEventsPath) << "\""
         << "}";
    WriteDiagnosticEventLocked(json.str());
}

void AppendJsonHex32(std::ostream& out, const char* key, melonDS::u32 value)
{
    out << "\"" << key << "\":\"0x" << Hex32(value) << "\"";
}

void AppendJsonHex64(std::ostream& out, const char* key, melonDS::u64 value)
{
    out << "\"" << key << "\":\"0x" << Hex64(value) << "\"";
}

void AppendDiagnosticPlayerJson(std::ostream& out, const DiagnosticPlayerSnapshot& player)
{
    out << "{";
    out << "\"found\":" << player.Found << ",";
    AppendJsonHex32(out, "base", player.Base); out << ",";
    out << "\"guid\":" << player.GUID << ",";
    AppendJsonHex32(out, "settings", player.Settings); out << ",";
    out << "\"stateType\":" << player.StateType << ",";
    AppendJsonHex32(out, "flags", player.Flags); out << ",";
    AppendJsonHex32(out, "x", player.PosX); out << ",";
    AppendJsonHex32(out, "y", player.PosY); out << ",";
    AppendJsonHex32(out, "z", player.PosZ); out << ",";
    AppendJsonHex32(out, "prevX", player.PrevX); out << ",";
    AppendJsonHex32(out, "prevY", player.PrevY); out << ",";
    AppendJsonHex32(out, "velX", player.VelX); out << ",";
    AppendJsonHex32(out, "velY", player.VelY); out << ",";
    AppendJsonHex32(out, "action", player.ActionFlag); out << ",";
    AppendJsonHex32(out, "subAction", player.SubActionFlag); out << ",";
    AppendJsonHex32(out, "physics", player.PhysicsFlag); out << ",";
    AppendJsonHex32(out, "damageCooldown", player.DamageCooldown); out << ",";
    AppendJsonHex32(out, "transitionFlag", player.TransitionFlag); out << ",";
    AppendJsonHex32(out, "collisionFlag", player.CollisionFlag); out << ",";
    AppendJsonHex32(out, "environmentFlag", player.EnvironmentFlag); out << ",";
    AppendJsonHex32(out, "linkedActor", player.LinkedActor); out << ",";
    out << "\"transitionStep\":" << player.TransitionStep << ",";
    out << "\"updateLocked\":" << player.UpdateLocked << ",";
    out << "\"characterIDBase\":" << player.CharacterIDBase << ",";
    out << "\"transitioningFlag\":" << player.TransitioningFlag << ",";
    out << "\"cameraFocusMode\":" << player.CameraFocusMode << ",";
    out << "\"defeatedFlag\":" << player.DefeatedFlag << ",";
    out << "\"playerBaseID\":" << player.PlayerBaseID << ",";
    out << "\"visibleFlag\":" << player.VisibleFlag << ",";
    AppendJsonHex32(out, "transitFunc", player.TransitFunc); out << ",";
    AppendJsonHex32(out, "transitArg", player.TransitArg); out << ",";
    out << "\"powerup\":" << player.Powerup << ",";
    out << "\"inventoryPowerup\":" << player.InventoryPowerup << ",";
    out << "\"dead\":" << player.Dead << ",";
    out << "\"character\":" << player.Character << ",";
    out << "\"transitionStatus\":" << player.TransitionStatus << ",";
    out << "\"lives\":" << player.Lives << ",";
    out << "\"battleStars\":" << player.BattleStars << ",";
    out << "\"coins\":" << player.Coins << ",";
    out << "\"score\":" << player.Score << ",";
    out << "\"displayedStars\":" << player.DisplayedStars << ",";
    out << "\"deaths\":" << player.Deaths << ",";
    out << "\"collectedStars\":" << player.CollectedStars;
    out << "}";
}

void AppendDiagnosticFrameJson(std::ostream& out, const DiagnosticFrameSnapshot& snap)
{
    out << "{";
    out << "\"frame\":" << snap.Frame << ",";
    out << "\"instance\":" << snap.Instance << ",";
    out << "\"stageID\":" << snap.StageID << ",";
    out << "\"stageGroup\":" << snap.StageGroup << ",";
    out << "\"vsMode\":" << snap.VsMode << ",";
    out << "\"localPlayerID\":" << snap.LocalPlayerID << ",";
    out << "\"scene\":" << snap.SceneCurrentSceneID << ",";
    out << "\"nextScene\":" << snap.SceneNextSceneID << ",";
    out << "\"freeze\":" << snap.StageActorFreezeFlag << ",";
    out << "\"playerCount\":" << snap.PlayerCount << ",";
    AppendJsonHex32(out, "inputConsole0", snap.InputConsole0Held); out << ",";
    AppendJsonHex32(out, "inputConsole1", snap.InputConsole1Held); out << ",";
    AppendJsonHex32(out, "inputPlayer0", snap.InputPlayer0Held); out << ",";
    AppendJsonHex32(out, "inputPlayer1", snap.InputPlayer1Held); out << ",";
    out << "\"lastSentInputFrame\":" << snap.LastSentInputFrame << ",";
    out << "\"lastReceivedInputFrame\":" << snap.LastReceivedInputFrame << ",";
    AppendJsonHex64(out, "playerGlobalHash", snap.PlayerGlobalHash); out << ",";
    AppendJsonHex64(out, "playerGlobalHash0", snap.PlayerGlobalHash0); out << ",";
    AppendJsonHex64(out, "playerGlobalHash1", snap.PlayerGlobalHash1); out << ",";
    AppendJsonHex64(out, "playerActorHash0", snap.PlayerActorHash0); out << ",";
    AppendJsonHex64(out, "playerActorHash1", snap.PlayerActorHash1); out << ",";
    AppendJsonHex32(out, "cameraX0", snap.StageCameraGlobalX0); out << ",";
    AppendJsonHex32(out, "cameraX1", snap.StageCameraGlobalX1); out << ",";
    AppendJsonHex32(out, "cameraY0", snap.StageCameraGlobalY0); out << ",";
    AppendJsonHex32(out, "cameraY1", snap.StageCameraGlobalY1); out << ",";
    AppendJsonHex32(out, "cameraWidth0", snap.StageCameraGlobalWidth0); out << ",";
    AppendJsonHex32(out, "cameraWidth1", snap.StageCameraGlobalWidth1); out << ",";
    AppendJsonHex32(out, "cameraHeight0", snap.StageCameraGlobalHeight0); out << ",";
    AppendJsonHex32(out, "cameraHeight1", snap.StageCameraGlobalHeight1); out << ",";
    out << "\"players\":[";
    AppendDiagnosticPlayerJson(out, snap.Player[0]);
    out << ",";
    AppendDiagnosticPlayerJson(out, snap.Player[1]);
    out << "]}";
}

void AppendGameStatePlayerJson(std::ostream& out, const GameStateSample& sample, int player)
{
    const bool p0 = player == 0;
    DiagnosticPlayerSnapshot snap;
    snap.Found = p0 ? sample.PlayerActor0Found : sample.PlayerActor1Found;
    snap.Base = p0 ? sample.PlayerActor0Base : sample.PlayerActor1Base;
    snap.GUID = p0 ? sample.PlayerActor0GUID : sample.PlayerActor1GUID;
    snap.Settings = p0 ? sample.PlayerActor0Settings : sample.PlayerActor1Settings;
    snap.StateType = p0 ? sample.PlayerActor0StateType : sample.PlayerActor1StateType;
    snap.Flags = p0 ? sample.PlayerActor0Flags : sample.PlayerActor1Flags;
    snap.PosX = p0 ? sample.PlayerActor0PosX : sample.PlayerActor1PosX;
    snap.PosY = p0 ? sample.PlayerActor0PosY : sample.PlayerActor1PosY;
    snap.PosZ = p0 ? sample.PlayerActor0PosZ : sample.PlayerActor1PosZ;
    snap.PrevX = p0 ? sample.PlayerActor0PrevX : sample.PlayerActor1PrevX;
    snap.PrevY = p0 ? sample.PlayerActor0PrevY : sample.PlayerActor1PrevY;
    snap.PrevZ = p0 ? sample.PlayerActor0PrevZ : sample.PlayerActor1PrevZ;
    snap.VelX = p0 ? sample.PlayerActor0VelX : sample.PlayerActor1VelX;
    snap.VelY = p0 ? sample.PlayerActor0VelY : sample.PlayerActor1VelY;
    snap.VelZ = p0 ? sample.PlayerActor0VelZ : sample.PlayerActor1VelZ;
    snap.ActionFlag = p0 ? sample.PlayerActor0ActionFlag : sample.PlayerActor1ActionFlag;
    snap.SubActionFlag = p0 ? sample.PlayerActor0SubActionFlag : sample.PlayerActor1SubActionFlag;
    snap.PhysicsFlag = p0 ? sample.PlayerActor0PhysicsFlag : sample.PlayerActor1PhysicsFlag;
    snap.DamageCooldown = p0 ? sample.PlayerActor0DamageCooldown : sample.PlayerActor1DamageCooldown;
    snap.TransitionFlag = p0 ? sample.PlayerActor0TransitionFlag : sample.PlayerActor1TransitionFlag;
    snap.CollisionFlag = p0 ? sample.PlayerActor0CollisionFlag : sample.PlayerActor1CollisionFlag;
    snap.EnvironmentFlag = p0 ? sample.PlayerActor0EnvironmentFlag : sample.PlayerActor1EnvironmentFlag;
    snap.LinkedActor = p0 ? sample.PlayerActor0LinkedActor : sample.PlayerActor1LinkedActor;
    snap.TransitionStep = p0 ? sample.PlayerActor0TransitionStep : sample.PlayerActor1TransitionStep;
    snap.UpdateLocked = p0 ? sample.PlayerActor0UpdateLocked : sample.PlayerActor1UpdateLocked;
    snap.CharacterIDBase = p0 ? sample.PlayerActor0CharacterIDBase : sample.PlayerActor1CharacterIDBase;
    snap.TransitioningFlag = p0 ? sample.PlayerActor0TransitioningFlag : sample.PlayerActor1TransitioningFlag;
    snap.CameraFocusMode = p0 ? sample.PlayerActor0CameraFocusMode : sample.PlayerActor1CameraFocusMode;
    snap.DefeatedFlag = p0 ? sample.PlayerActor0DefeatedFlag : sample.PlayerActor1DefeatedFlag;
    snap.PlayerBaseID = p0 ? sample.PlayerActor0PlayerBaseID : sample.PlayerActor1PlayerBaseID;
    snap.VisibleFlag = p0 ? sample.PlayerActor0VisibleFlag : sample.PlayerActor1VisibleFlag;
    snap.TransitFunc = p0 ? sample.PlayerActor0TransitFunc : sample.PlayerActor1TransitFunc;
    snap.TransitArg = p0 ? sample.PlayerActor0TransitArg : sample.PlayerActor1TransitArg;
    snap.Powerup = p0 ? sample.Player0Powerup : sample.Player1Powerup;
    snap.InventoryPowerup = p0 ? sample.Player0InventoryPowerup : sample.Player1InventoryPowerup;
    snap.Dead = p0 ? sample.Player0Dead : sample.Player1Dead;
    snap.Character = p0 ? sample.Player0Character : sample.Player1Character;
    snap.TransitionStatus = p0 ? sample.PlayerTransitionStatus0 : sample.PlayerTransitionStatus1;
    snap.Lives = p0 ? sample.Player0Lives : sample.Player1Lives;
    snap.BattleStars = p0 ? sample.Player0BattleStars : sample.Player1BattleStars;
    snap.Coins = p0 ? sample.Player0Coins : sample.Player1Coins;
    snap.Score = p0 ? sample.Player0Score : sample.Player1Score;
    snap.DisplayedStars = p0 ? sample.Player0DisplayedStars : sample.Player1DisplayedStars;
    snap.Deaths = p0 ? sample.Player0Deaths : sample.Player1Deaths;
    snap.CollectedStars = p0 ? sample.Player0CollectedStars : sample.Player1CollectedStars;
    AppendDiagnosticPlayerJson(out, snap);
}

void AppendDiagnosticRingJson(std::ostream& out, int instanceID)
{
    out << "\"ring\":[";
    if (instanceID >= 0 && instanceID < 16)
    {
        bool first = true;
        const std::size_t ringFrames = static_cast<std::size_t>(
            std::clamp(G.DiagnosticRingFrames, 1, static_cast<int>(kDiagnosticRingCapacity)));
        const std::size_t next = G.DiagnosticRingNext[instanceID] % kDiagnosticRingCapacity;
        for (std::size_t i = 0; i < ringFrames; i++)
        {
            const std::size_t idx = (next + kDiagnosticRingCapacity - ringFrames + i) % kDiagnosticRingCapacity;
            const DiagnosticFrameSnapshot& snap = G.DiagnosticRing[instanceID][idx];
            if (!snap.Valid)
                continue;
            if (!first)
                out << ",";
            first = false;
            AppendDiagnosticFrameJson(out, snap);
        }
    }
    out << "]";
}

melonDS::s32 DiagnosticSignedFixed(melonDS::u32 value)
{
    return static_cast<melonDS::s32>(value);
}

melonDS::s64 DiagnosticFixedDelta(melonDS::u32 lhs, melonDS::u32 rhs)
{
    return static_cast<melonDS::s64>(DiagnosticSignedFixed(lhs))
        - static_cast<melonDS::s64>(DiagnosticSignedFixed(rhs));
}

melonDS::u32 DiagnosticCameraX(const DiagnosticFrameSnapshot& snap, int player)
{
    return player == 0 ? snap.StageCameraGlobalX0 : snap.StageCameraGlobalX1;
}

melonDS::u32 DiagnosticCameraY(const DiagnosticFrameSnapshot& snap, int player)
{
    return player == 0 ? snap.StageCameraGlobalY0 : snap.StageCameraGlobalY1;
}

melonDS::u32 DiagnosticCameraWidth(const DiagnosticFrameSnapshot& snap, int player)
{
    const melonDS::u32 value = player == 0 ? snap.StageCameraGlobalWidth0 : snap.StageCameraGlobalWidth1;
    return value != 0 ? value : static_cast<melonDS::u32>(256 * kDiagnosticFixedOne);
}

melonDS::u32 DiagnosticCameraHeight(const DiagnosticFrameSnapshot& snap, int player)
{
    const melonDS::u32 value = player == 0 ? snap.StageCameraGlobalHeight0 : snap.StageCameraGlobalHeight1;
    return value != 0 ? value : static_cast<melonDS::u32>(192 * kDiagnosticFixedOne);
}

void AppendDiagnosticPlayerContextJson(
    std::ostream& out,
    const DiagnosticFrameSnapshot& snap,
    const DiagnosticFrameSnapshot* previous,
    int player)
{
    const DiagnosticPlayerSnapshot& current = snap.Player[player];
    const DiagnosticPlayerSnapshot* prev = previous ? &previous->Player[player] : nullptr;
    const melonDS::u32 cameraX = DiagnosticCameraX(snap, player);
    const melonDS::u32 cameraY = DiagnosticCameraY(snap, player);
    const melonDS::u32 cameraWidth = DiagnosticCameraWidth(snap, player);
    const melonDS::u32 cameraHeight = DiagnosticCameraHeight(snap, player);
    const melonDS::s64 screenX = DiagnosticFixedDelta(current.PosX, cameraX);
    const melonDS::s64 screenY = DiagnosticFixedDelta(current.PosY, cameraY);
    const melonDS::s64 deltaX = prev ? DiagnosticFixedDelta(current.PosX, prev->PosX) : 0;
    const melonDS::s64 deltaY = prev ? DiagnosticFixedDelta(current.PosY, prev->PosY) : 0;

    out << "\"player\":" << player << ",";
    AppendJsonHex32(out, "cameraX", cameraX); out << ",";
    AppendJsonHex32(out, "cameraY", cameraY); out << ",";
    AppendJsonHex32(out, "cameraWidth", cameraWidth); out << ",";
    AppendJsonHex32(out, "cameraHeight", cameraHeight); out << ",";
    out << "\"screenX\":" << screenX << ",";
    out << "\"screenY\":" << screenY << ",";
    out << "\"screenXPx\":" << (screenX / kDiagnosticFixedOne) << ",";
    out << "\"screenYPx\":" << (screenY / kDiagnosticFixedOne) << ",";
    out << "\"deltaX\":" << deltaX << ",";
    out << "\"deltaY\":" << deltaY << ",";
    out << "\"deltaXPx\":" << (deltaX / kDiagnosticFixedOne) << ",";
    out << "\"deltaYPx\":" << (deltaY / kDiagnosticFixedOne) << ",";
    out << "\"current\":";
    AppendDiagnosticPlayerJson(out, current);
    if (prev)
    {
        out << ",\"previous\":";
        AppendDiagnosticPlayerJson(out, *prev);
    }
}

bool DiagnosticPlayerIsLiveForPositionCheck(const DiagnosticPlayerSnapshot& player)
{
    return player.Found != 0
        && player.Dead == 0
        && player.VisibleFlag != 0
        && player.TransitioningFlag == 0
        && player.DefeatedFlag == 0;
}

bool DiagnosticPlayerScreenPositionAnomalous(
    const DiagnosticFrameSnapshot& snap,
    const DiagnosticFrameSnapshot* previous,
    int player)
{
    const DiagnosticPlayerSnapshot& current = snap.Player[player];
    if (!DiagnosticPlayerIsLiveForPositionCheck(current))
        return false;

    const melonDS::s64 screenX = DiagnosticFixedDelta(current.PosX, DiagnosticCameraX(snap, player));
    const melonDS::s64 cameraWidth = static_cast<melonDS::s64>(DiagnosticCameraWidth(snap, player));
    if (screenX < -static_cast<melonDS::s64>(kDiagnosticOffscreenMargin)
        || screenX > cameraWidth + static_cast<melonDS::s64>(kDiagnosticOffscreenMargin))
    {
        return true;
    }

    if (previous && previous->Valid && DiagnosticPlayerIsLiveForPositionCheck(previous->Player[player]))
    {
        const melonDS::s64 deltaX = DiagnosticFixedDelta(current.PosX, previous->Player[player].PosX);
        const melonDS::s64 deltaY = DiagnosticFixedDelta(current.PosY, previous->Player[player].PosY);
        if (std::llabs(deltaX) > kDiagnosticLargePositionDelta
            || std::llabs(deltaY) > kDiagnosticLargePositionDelta)
        {
            return true;
        }
    }

    return false;
}

void EmitDiagnosticPitTransitionEvent(
    int instanceID,
    const DiagnosticFrameSnapshot& snap,
    const DiagnosticFrameSnapshot* previous,
    int player)
{
    if (!G.DiagnosticEventsEnabled || instanceID < 0 || instanceID >= 16 || player < 0 || player > 1)
        return;
    if (snap.Player[player].TransitFunc != kPlayerPitDeathTransitStateAddr)
        return;
    if (previous && previous->Valid && previous->Player[player].TransitFunc == kPlayerPitDeathTransitStateAddr)
        return;
    if (G.LastDiagnosticPitTransitionFrame[instanceID][player] != 0
        && snap.Frame < G.LastDiagnosticPitTransitionFrame[instanceID][player] + kDiagnosticRepeatedAnomalyFrames)
    {
        return;
    }

    G.LastDiagnosticPitTransitionFrame[instanceID][player] = snap.Frame;
    G.DiagnosticPostTriggerUntilFrame[instanceID] = snap.Frame + kDiagnosticPostTriggerFrames;

    std::ostringstream json;
    json << "{\"event\":\"player_pit_transition\","
         << "\"role\":\"" << (G.NetRole == Role::Host ? "host" : "client") << "\","
         << "\"instance\":" << instanceID << ","
         << "\"frame\":" << snap.Frame << ","
         << "\"stageID\":" << snap.StageID << ","
         << "\"stageGroup\":" << snap.StageGroup << ","
         << "\"scene\":" << snap.SceneCurrentSceneID << ","
         << "\"nextScene\":" << snap.SceneNextSceneID << ",";
    AppendDiagnosticPlayerContextJson(json, snap, previous, player);
    json << ",";
    AppendDiagnosticRingJson(json, instanceID);
    json << "}";

    std::lock_guard<std::mutex> lock(G.Mutex);
    WriteDiagnosticEventLocked(json.str());
}

void EmitDiagnosticPositionAnomalyEvent(
    int instanceID,
    const DiagnosticFrameSnapshot& snap,
    const DiagnosticFrameSnapshot* previous,
    int player)
{
    if (!G.DiagnosticEventsEnabled || instanceID < 0 || instanceID >= 16 || player < 0 || player > 1)
        return;
    if (!DiagnosticPlayerScreenPositionAnomalous(snap, previous, player))
        return;
    if (G.LastDiagnosticPositionAnomalyFrame[instanceID][player] != 0
        && snap.Frame < G.LastDiagnosticPositionAnomalyFrame[instanceID][player] + kDiagnosticRepeatedAnomalyFrames)
    {
        return;
    }

    G.LastDiagnosticPositionAnomalyFrame[instanceID][player] = snap.Frame;

    std::ostringstream json;
    json << "{\"event\":\"player_position_anomaly\","
         << "\"role\":\"" << (G.NetRole == Role::Host ? "host" : "client") << "\","
         << "\"instance\":" << instanceID << ","
         << "\"frame\":" << snap.Frame << ","
         << "\"stageID\":" << snap.StageID << ","
         << "\"stageGroup\":" << snap.StageGroup << ","
         << "\"scene\":" << snap.SceneCurrentSceneID << ","
         << "\"nextScene\":" << snap.SceneNextSceneID << ",";
    AppendDiagnosticPlayerContextJson(json, snap, previous, player);
    json << ",";
    AppendDiagnosticRingJson(json, instanceID);
    json << "}";

    std::lock_guard<std::mutex> lock(G.Mutex);
    WriteDiagnosticEventLocked(json.str());
}

void ReadDiagnosticPlayerSnapshot(
    int instanceID,
    melonDS::u32 frame,
    melonDS::NDS* nds,
    int player,
    DiagnosticPlayerSnapshot& out)
{
    out = {};
    if (!nds || !nds->MainRAM || player < 0 || player > 1)
        return;

    ObjectScanSample actor;
    const melonDS::u32 cachedBase = G.PlayerActorBaseCache[instanceID][player];
    const melonDS::u32 cachedGUID = G.PlayerActorGUIDCache[instanceID][player];
    if (cachedBase != 0)
        ReadPlayerActorByBase(nds, cachedBase, cachedGUID, actor);
    if (!actor.Found && (frame % 60) == 0)
        actor = GetPlayerActorCached(instanceID, player, nds);

    out.Found = actor.Found;
    out.Base = actor.Base;
    out.GUID = actor.GUID;
    out.Settings = actor.Settings;
    out.StateType = actor.StateType;
    out.Flags = actor.Flags;
    out.PosX = actor.PosX;
    out.PosY = actor.PosY;
    out.PosZ = actor.PosZ;
    out.PrevX = actor.PrevX;
    out.PrevY = actor.PrevY;
    out.PrevZ = actor.PrevZ;
    out.VelX = actor.VelX;
    out.VelY = actor.VelY;
    out.VelZ = actor.VelZ;

    if (actor.Found && IsValidMainRAMRange(nds, actor.Base, kPlayerBaseTransitionStepOffset + 1))
    {
        out.ActionFlag = nds->ARM9Read32(actor.Base + kPlayerBaseActionFlagOffset);
        out.SubActionFlag = nds->ARM9Read32(actor.Base + kPlayerBaseSubActionFlagOffset);
        out.PhysicsFlag = nds->ARM9Read32(actor.Base + kPlayerBasePhysicsFlagOffset);
        out.DamageCooldown = nds->ARM9Read16(actor.Base + kPlayerBaseDamageCooldownOffset);
        out.TransitionFlag = nds->ARM9Read32(actor.Base + kPlayerBaseTransitionFlagOffset);
        out.CollisionFlag = nds->ARM9Read32(actor.Base + kPlayerBaseCollisionFlagOffset);
        out.EnvironmentFlag = nds->ARM9Read32(actor.Base + kPlayerBaseEnvironmentFlagOffset);
        out.LinkedActor = nds->ARM9Read32(actor.Base + kPlayerBaseLinkedActorOffset);
        out.TransitionStep = nds->ARM9Read8(actor.Base + kPlayerBaseTransitionStepOffset);
        out.UpdateLocked = nds->ARM9Read8(actor.Base + kPlayerBaseUpdateLockedOffset);
        out.CharacterIDBase = nds->ARM9Read8(actor.Base + kPlayerBaseCharacterIDOffset);
        out.TransitioningFlag = nds->ARM9Read8(actor.Base + kPlayerBaseTransitioningFlagOffset);
        out.CameraFocusMode = nds->ARM9Read8(actor.Base + kPlayerBaseCameraFocusModeOffset);
        out.DefeatedFlag = nds->ARM9Read8(actor.Base + kPlayerBaseDefeatedFlagOffset);
        out.PlayerBaseID = nds->ARM9Read8(actor.Base + kPlayerBasePlayerIDOffset);
        out.VisibleFlag = nds->ARM9Read8(actor.Base + kPlayerBaseVisibleFlagOffset);
        out.TransitFunc = nds->ARM9Read32(actor.Base + 0x990);
        out.TransitArg = nds->ARM9Read32(actor.Base + 0x994);
    }

    const melonDS::u32 p = static_cast<melonDS::u32>(player);
    out.Powerup = nds->ARM9Read8(kGamePlayerPowerupAddr + p);
    out.InventoryPowerup = nds->ARM9Read8(kGamePlayerInventoryPowerupAddr + p);
    out.Dead = nds->ARM9Read8(kGamePlayerDeadAddr + p);
    out.Character = nds->ARM9Read8(kGamePlayerCharacterAddr + p);
    out.TransitionStatus = nds->ARM9Read32(kGamePlayerTransitionStatusAddr + sizeof(melonDS::u32) * p);
    out.Lives = nds->ARM9Read32(kGamePlayerLivesAddr + sizeof(melonDS::u32) * p);
    out.BattleStars = nds->ARM9Read32(kGamePlayerBattleStarsAddr + sizeof(melonDS::u32) * p);
    out.Coins = nds->ARM9Read32(kGamePlayerCoinsAddr + sizeof(melonDS::u32) * p);
    out.Score = nds->ARM9Read32(kGamePlayerScoreAddr + sizeof(melonDS::u32) * p);
    out.DisplayedStars = nds->ARM9Read32(kGamePlayerDisplayedStarsAddr + sizeof(melonDS::u32) * p);
    out.Deaths = nds->ARM9Read32(kGamePlayerDeathsAddr + sizeof(melonDS::u32) * p);
    out.CollectedStars = nds->ARM9Read32(kGamePlayerCollectedStarsAddr + sizeof(melonDS::u32) * p);
}

void RecordDiagnosticSnapshotIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.DiagnosticEventsEnabled || !nds || !nds->MainRAM)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (frame < G.Connection.StartFrame)
        return;

    DiagnosticFrameSnapshot snap;
    snap.Valid = true;
    snap.Frame = frame;
    snap.Instance = static_cast<melonDS::u32>(instanceID);
    snap.StageID = nds->ARM9Read32(kGameStageIDAddr);
    snap.StageGroup = nds->ARM9Read32(kGameStageGroupAddr);
    snap.VsMode = nds->ARM9Read32(kGameVsModeAddr);
    snap.LocalPlayerID = nds->ARM9Read32(kGameLocalPlayerIDAddr);
    snap.SceneCurrentSceneID = nds->ARM9Read16(kSceneCurrentSceneIDAddr);
    snap.SceneNextSceneID = nds->ARM9Read16(kSceneNextSceneIDAddr);
    snap.StageActorFreezeFlag = nds->ARM9Read8(kStageActorFreezeFlagAddr);
    snap.PlayerCount = nds->ARM9Read32(kGamePlayerCountAddr);
    snap.InputConsole0Held = nds->ARM9Read16(kInputConsoleKeysAddr + 0x0);
    snap.InputConsole1Held = nds->ARM9Read16(kInputConsoleKeysAddr + 0x4);
    snap.InputPlayer0Held = nds->ARM9Read16(kInputPlayerKeysHeldAddr + 0x0);
    snap.InputPlayer1Held = nds->ARM9Read16(kInputPlayerKeysHeldAddr + 0x2);
    snap.LastSentInputFrame = G.LastSentInputFrame;
    snap.LastReceivedInputFrame = G.LastReceivedInputFrame;
    snap.PlayerGlobalHash = HashMainRAMRange(nds, kGamePlayerGlobalBlockAddr, 0xC0);
    snap.PlayerGlobalHash0 = HashMainRAMRange(nds, kGamePlayerGlobalBlockAddr, 0x60);
    snap.PlayerGlobalHash1 = HashMainRAMRange(nds, kGamePlayerGlobalBlockAddr + 0x60, 0x60);
    snap.StageCameraGlobalX0 = nds->ARM9Read32(kStageCameraXAddr);
    snap.StageCameraGlobalX1 = nds->ARM9Read32(kStageCameraXAddr + sizeof(melonDS::u32));
    snap.StageCameraGlobalY0 = nds->ARM9Read32(kStageCameraYAddr);
    snap.StageCameraGlobalY1 = nds->ARM9Read32(kStageCameraYAddr + sizeof(melonDS::u32));
    snap.StageCameraGlobalWidth0 = nds->ARM9Read32(kStageCameraWidthAddr);
    snap.StageCameraGlobalWidth1 = nds->ARM9Read32(kStageCameraWidthAddr + sizeof(melonDS::u32));
    snap.StageCameraGlobalHeight0 = nds->ARM9Read32(kStageCameraHeightAddr);
    snap.StageCameraGlobalHeight1 = nds->ARM9Read32(kStageCameraHeightAddr + sizeof(melonDS::u32));
    ReadDiagnosticPlayerSnapshot(instanceID, frame, nds, 0, snap.Player[0]);
    ReadDiagnosticPlayerSnapshot(instanceID, frame, nds, 1, snap.Player[1]);
    if (snap.Player[0].Found && IsValidMainRAMRange(nds, snap.Player[0].Base, 0xC00))
        snap.PlayerActorHash0 = HashMainRAMRange(nds, snap.Player[0].Base, 0xC00);
    if (snap.Player[1].Found && IsValidMainRAMRange(nds, snap.Player[1].Base, 0xC00))
        snap.PlayerActorHash1 = HashMainRAMRange(nds, snap.Player[1].Base, 0xC00);

    const DiagnosticFrameSnapshot* previous = nullptr;
    if (G.DiagnosticRingNext[instanceID] != 0)
    {
        const std::size_t prevIdx =
            (G.DiagnosticRingNext[instanceID] + kDiagnosticRingCapacity - 1) % kDiagnosticRingCapacity;
        if (G.DiagnosticRing[instanceID][prevIdx].Valid)
            previous = &G.DiagnosticRing[instanceID][prevIdx];
    }
    for (int player = 0; player < 2; player++)
    {
        EmitDiagnosticPositionAnomalyEvent(instanceID, snap, previous, player);
        EmitDiagnosticPitTransitionEvent(instanceID, snap, previous, player);
    }

    G.DiagnosticRing[instanceID][G.DiagnosticRingNext[instanceID] % kDiagnosticRingCapacity] = snap;
    G.DiagnosticRingNext[instanceID] = (G.DiagnosticRingNext[instanceID] + 1) % kDiagnosticRingCapacity;

    if (G.DiagnosticPostTriggerUntilFrame[instanceID] != 0
        && frame >= G.DiagnosticPostTriggerUntilFrame[instanceID])
    {
        const melonDS::u32 triggerFrame = G.DiagnosticPostTriggerUntilFrame[instanceID];
        G.DiagnosticPostTriggerUntilFrame[instanceID] = 0;
        std::ostringstream json;
        json << "{\"event\":\"diagnostic_post_window\","
             << "\"role\":\"" << (G.NetRole == Role::Host ? "host" : "client") << "\","
             << "\"instance\":" << instanceID << ","
             << "\"frame\":" << frame << ","
             << "\"triggerUntilFrame\":" << triggerFrame << ",";
        AppendDiagnosticRingJson(json, instanceID);
        json << "}";
        std::lock_guard<std::mutex> lock(G.Mutex);
        WriteDiagnosticEventLocked(json.str());
    }
}

void AppendRemoteSampleDiffJson(std::ostream& out, const GameStateSample& local, const GameStateSample& remote)
{
    auto diffHex = [&out](const char* name, melonDS::u32 a, melonDS::u32 b, bool& first) {
        if (a == b)
            return;
        if (!first)
            out << ",";
        first = false;
        out << "{\"field\":\"" << name << "\",\"local\":\"0x" << Hex32(a)
            << "\",\"remote\":\"0x" << Hex32(b) << "\"}";
    };

    bool first = true;
    out << "\"remoteSampleDiffs\":[";
    diffHex("stageID", local.StageID, remote.StageID, first);
    diffHex("stageGroup", local.StageGroup, remote.StageGroup, first);
    diffHex("player0PosX", local.PlayerActor0PosX, remote.PlayerActor0PosX, first);
    diffHex("player0PosY", local.PlayerActor0PosY, remote.PlayerActor0PosY, first);
    diffHex("player0VelX", local.PlayerActor0VelX, remote.PlayerActor0VelX, first);
    diffHex("player0VelY", local.PlayerActor0VelY, remote.PlayerActor0VelY, first);
    diffHex("player1PosX", local.PlayerActor1PosX, remote.PlayerActor1PosX, first);
    diffHex("player1PosY", local.PlayerActor1PosY, remote.PlayerActor1PosY, first);
    diffHex("player1VelX", local.PlayerActor1VelX, remote.PlayerActor1VelX, first);
    diffHex("player1VelY", local.PlayerActor1VelY, remote.PlayerActor1VelY, first);
    diffHex("player0Deaths", local.Player0Deaths, remote.Player0Deaths, first);
    diffHex("player1Deaths", local.Player1Deaths, remote.Player1Deaths, first);
    diffHex("player0BattleStars", local.Player0BattleStars, remote.Player0BattleStars, first);
    diffHex("player1BattleStars", local.Player1BattleStars, remote.Player1BattleStars, first);
    diffHex("vsCoinCount", local.VsCoinCount, remote.VsCoinCount, first);
    out << "]";
}

void EmitGameStateMismatchEventLocked(
    int instanceID,
    melonDS::u32 frame,
    const GameStateSyncHashes& local,
    const GameStateSyncHashes& remote)
{
    if (!G.DiagnosticEventsEnabled || instanceID < 0 || instanceID >= 16)
        return;
    if (local.PlayerGlobal == remote.PlayerGlobal)
        return;
    if (G.LastDiagnosticMismatchFrame[instanceID] != 0
        && frame < G.LastDiagnosticMismatchFrame[instanceID] + 300)
        return;

    G.LastDiagnosticMismatchFrame[instanceID] = frame;
    G.DiagnosticPostTriggerUntilFrame[instanceID] = frame + kDiagnosticPostTriggerFrames;

    GameStateSample remoteSample;
    bool hasRemoteSample = false;
    auto remoteIt = G.RemoteGameStateSamples.find(GameStateKey(instanceID, frame));
    if (remoteIt != G.RemoteGameStateSamples.end())
    {
        remoteSample = remoteIt->second;
        hasRemoteSample = true;
    }

    GameStateSample localSample;
    bool hasLocalSample = false;
    const DiagnosticFrameSnapshot* latest = nullptr;
    if (G.DiagnosticRingNext[instanceID] != 0)
    {
        const std::size_t idx =
            (G.DiagnosticRingNext[instanceID] + kDiagnosticRingCapacity - 1) % kDiagnosticRingCapacity;
        if (G.DiagnosticRing[instanceID][idx].Valid)
            latest = &G.DiagnosticRing[instanceID][idx];
    }

    std::ostringstream json;
    json << "{\"event\":\"player_global_mismatch\","
         << "\"role\":\"" << (G.NetRole == Role::Host ? "host" : "client") << "\","
         << "\"instance\":" << instanceID << ","
         << "\"frame\":" << frame << ",";
    AppendJsonHex64(json, "localBasic", local.Basic); json << ",";
    AppendJsonHex64(json, "remoteBasic", remote.Basic); json << ",";
    AppendJsonHex64(json, "localPlayerGlobal", local.PlayerGlobal); json << ",";
    AppendJsonHex64(json, "remotePlayerGlobal", remote.PlayerGlobal); json << ",";
    AppendJsonHex64(json, "localWifiCandidate", local.WifiCandidate); json << ",";
    AppendJsonHex64(json, "remoteWifiCandidate", remote.WifiCandidate); json << ",";
    AppendJsonHex64(json, "localRenderCandidate", local.RenderCandidate); json << ",";
    AppendJsonHex64(json, "remoteRenderCandidate", remote.RenderCandidate); json << ",";
    if (latest)
    {
        AppendJsonHex64(json, "localPlayerGlobalHash0", latest->PlayerGlobalHash0); json << ",";
        AppendJsonHex64(json, "localPlayerGlobalHash1", latest->PlayerGlobalHash1); json << ",";
        AppendJsonHex64(json, "localPlayerActorHash0", latest->PlayerActorHash0); json << ",";
        AppendJsonHex64(json, "localPlayerActorHash1", latest->PlayerActorHash1); json << ",";
        json << "\"latestLocal\":";
        AppendDiagnosticFrameJson(json, *latest);
        json << ",";
    }
    if (hasRemoteSample)
    {
        json << "\"remotePlayers\":[";
        AppendGameStatePlayerJson(json, remoteSample, 0);
        json << ",";
        AppendGameStatePlayerJson(json, remoteSample, 1);
        json << "],";
        if (latest)
        {
            localSample.StageID = latest->StageID;
            localSample.StageGroup = latest->StageGroup;
            localSample.PlayerActor0PosX = latest->Player[0].PosX;
            localSample.PlayerActor0PosY = latest->Player[0].PosY;
            localSample.PlayerActor0VelX = latest->Player[0].VelX;
            localSample.PlayerActor0VelY = latest->Player[0].VelY;
            localSample.PlayerActor1PosX = latest->Player[1].PosX;
            localSample.PlayerActor1PosY = latest->Player[1].PosY;
            localSample.PlayerActor1VelX = latest->Player[1].VelX;
            localSample.PlayerActor1VelY = latest->Player[1].VelY;
            localSample.Player0Deaths = latest->Player[0].Deaths;
            localSample.Player1Deaths = latest->Player[1].Deaths;
            localSample.Player0BattleStars = latest->Player[0].BattleStars;
            localSample.Player1BattleStars = latest->Player[1].BattleStars;
            localSample.VsCoinCount = latest->Player[0].Coins + latest->Player[1].Coins;
            hasLocalSample = true;
        }
        if (hasLocalSample)
        {
            AppendRemoteSampleDiffJson(json, localSample, remoteSample);
            json << ",";
        }
    }
    AppendDiagnosticRingJson(json, instanceID);
    json << "}";
    WriteDiagnosticEventLocked(json.str());
}

void AppendNearbyHazardsJson(std::ostream& out, melonDS::NDS* nds)
{
    out << "\"nearbyMovingHazards\":[";
    const std::vector<ObjectScanSample> hazards =
        FindActiveObjectsByIDAndSettings(nds, kVsMovingHazardObjectID, kVsMovingHazardSettings);
    const std::size_t count = std::min<std::size_t>(hazards.size(), kMaxWorldMovingHazards);
    for (std::size_t i = 0; i < count; i++)
    {
        if (i != 0)
            out << ",";
        out << "{";
        out << "\"guid\":" << hazards[i].GUID << ",";
        AppendJsonHex32(out, "base", hazards[i].Base); out << ",";
        AppendJsonHex32(out, "x", hazards[i].PosX); out << ",";
        AppendJsonHex32(out, "y", hazards[i].PosY); out << ",";
        AppendJsonHex32(out, "velX", hazards[i].VelX); out << ",";
        AppendJsonHex32(out, "velY", hazards[i].VelY); out << ",";
        out << "\"stateType\":" << hazards[i].StateType << ",";
        AppendJsonHex32(out, "flags", hazards[i].Flags);
        out << "}";
    }
    out << "]";
}

void EmitPlayerLifeEvent(
    int instanceID,
    melonDS::u32 frame,
    int player,
    const char* reason,
    const GameStateSample& sample,
    melonDS::NDS* nds)
{
    if (!G.DiagnosticEventsEnabled || instanceID < 0 || instanceID >= 16 || player < 0 || player > 1)
        return;
    const bool transitionOnly = reason && std::strcmp(reason, "death-transition") == 0;
    if (G.LastDiagnosticLifeEventFrame[instanceID][player] == frame)
        return;
    if (transitionOnly
        && G.LastDiagnosticLifeEventFrame[instanceID][player] != 0
        && frame < G.LastDiagnosticLifeEventFrame[instanceID][player] + 300)
    {
        return;
    }

    G.LastDiagnosticLifeEventFrame[instanceID][player] = frame;
    if (!transitionOnly)
        G.DiagnosticPostTriggerUntilFrame[instanceID] = frame + kDiagnosticPostTriggerFrames;

    std::ostringstream json;
    json << "{\"event\":\"player_life_change\","
         << "\"role\":\"" << (G.NetRole == Role::Host ? "host" : "client") << "\","
         << "\"reason\":\"" << (reason ? reason : "change") << "\","
         << "\"instance\":" << instanceID << ","
         << "\"frame\":" << frame << ","
         << "\"player\":" << player << ","
         << "\"stageID\":" << sample.StageID << ","
         << "\"stageGroup\":" << sample.StageGroup << ","
         << "\"scene\":" << sample.SceneCurrentSceneID << ","
         << "\"nextScene\":" << sample.SceneNextSceneID << ","
         << "\"players\":[";
    AppendGameStatePlayerJson(json, sample, 0);
    json << ",";
    AppendGameStatePlayerJson(json, sample, 1);
    json << "],";
    AppendNearbyHazardsJson(json, nds);
    if (!transitionOnly)
    {
        json << ",";
        AppendDiagnosticRingJson(json, instanceID);
    }
    json << "}";

    std::lock_guard<std::mutex> lock(G.Mutex);
    WriteDiagnosticEventLocked(json.str());
}

void TraceWorldMovingHazardsIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.StateSync.WorldTraceMovingHazards || instanceID < 0 || instanceID >= 16)
        return;
    if ((frame % 60) != 0 || G.LastTracedWorldMovingHazardsFrame[instanceID] == frame)
        return;
    G.LastTracedWorldMovingHazardsFrame[instanceID] = frame;

    const std::vector<ObjectScanSample> actors =
        FindActiveObjectsByIDAndSettings(nds, kVsMovingHazardObjectID, kVsMovingHazardSettings);
    std::printf("NSMB WorldHazards: role=%s inst=%d frame=%u count=%zu",
        G.NetRole == Role::Host ? "host" : "client",
        instanceID,
        frame,
        actors.size());
    for (std::size_t i = 0; i < actors.size(); i++)
    {
        std::printf(" slot%zu=%u/%08X/%08X",
            i,
            actors[i].GUID,
            actors[i].PosX,
            actors[i].PosY);
    }
    std::printf("\n");
}

bool ShouldTraceWorldActorInternals(melonDS::u16 objectID, melonDS::u32 vtable)
{
    return objectID == kVsMovingHazardObjectID ||
        objectID == kVsKoopaTroopaObjectID ||
        objectID == kVsBattleStarCandidateObjectID ||
        vtable == kEffectVTablePtr ||
        vtable == kEffectVTableStart;
}

void PrintWorldActorInternalWords(
    const char* prefix,
    int instanceID,
    melonDS::u32 frame,
    melonDS::NDS* nds,
    melonDS::u32 base,
    melonDS::u32 guid,
    melonDS::u16 objectID,
    melonDS::u32 settings,
    melonDS::u32 vtable)
{
    std::printf(
        "%s: role=%s inst=%d frame=%u guid=%u object=%03X settings=%08X vtable=%08X base=%08X words=",
        prefix,
        G.NetRole == Role::Host ? "host" : "client",
        instanceID,
        frame,
        guid,
        objectID,
        settings,
        vtable,
        base);

    for (melonDS::u32 relativeOffset = 0; relativeOffset <= 0x1FC; relativeOffset += sizeof(melonDS::u32))
    {
        melonDS::u32 value = 0;
        ReadMainRAMAddressU32(nds, base + relativeOffset, value);
        std::printf("%s%02X:%08X", relativeOffset == 0 ? "" : "/", relativeOffset, value);
    }
    std::printf("\n");
}

bool IsInterestingEffectCandidate(melonDS::NDS* nds, melonDS::u32 base)
{
    if (base < 0x02100000u)
        return false;

    for (melonDS::u32 relativeOffset = 0x04; relativeOffset <= 0x10C; relativeOffset += sizeof(melonDS::u32))
    {
        melonDS::u32 value = 0;
        ReadMainRAMAddressU32(nds, base + relativeOffset, value);
        if (value == 0 ||
            value == 0x020391F8u ||
            value == 0x02039208u ||
            value == kEffectVTablePtr ||
            value == kEffectVTableStart)
            continue;
        if (relativeOffset == 0xA8 && (value & 0x0000FFFFu) == 0)
            continue;
        return true;
    }
    return false;
}

bool ReadWorldEffectSlot(melonDS::NDS* nds, melonDS::u32 base, WireWorldEffectSlot& slot)
{
    if (!nds || !nds->MainRAM || !IsValidMainRAMRange(nds, base, kWorldEffectWordEnd + sizeof(melonDS::u32)))
        return false;

    melonDS::u32 vtable = 0;
    if (!ReadMainRAMAddressU32(nds, base, vtable))
        return false;
    if (vtable != kEffectVTablePtr && vtable != kEffectVTableStart)
        return false;
    if (!IsInterestingEffectCandidate(nds, base))
        return false;

    slot.Found = 1;
    slot.Base = base;
    slot.VTable = vtable;
    for (std::size_t i = 0; i < kWorldEffectWordCount; i++)
    {
        const melonDS::u32 relativeOffset =
            kWorldEffectWordStart + static_cast<melonDS::u32>(i * sizeof(melonDS::u32));
        ReadMainRAMAddressU32(nds, base + relativeOffset, slot.Words[i]);
    }
    return true;
}

void TraceWorldEffectsIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.StateSync.WorldTraceEffects || !nds || !nds->MainRAM || instanceID < 0 || instanceID >= 16)
        return;
    if (frame < G.StateSync.WorldTraceObjectLifecyclesStartFrame ||
        (G.StateSync.WorldTraceObjectLifecyclesEndFrame != kNoFrameLimit &&
            frame > G.StateSync.WorldTraceObjectLifecyclesEndFrame))
        return;
    if ((frame % static_cast<melonDS::u32>(G.StateSync.WorldTraceObjectLifecyclesInterval)) != 0 ||
        G.LastTracedWorldEffectsFrame[instanceID] == frame)
        return;
    G.LastTracedWorldEffectsFrame[instanceID] = frame;

    melonDS::u32 count = 0;
    for (melonDS::u32 slotIndex = 0; slotIndex < kWorldEffectSlotCount; slotIndex++)
    {
        const melonDS::u32 base = kWorldEffectSlotBase + slotIndex * kWorldEffectSlotStride;
        WireWorldEffectSlot slot {};
        if (!ReadWorldEffectSlot(nds, base, slot))
            continue;

        const melonDS::u32 guid = 0;
        PrintWorldActorInternalWords(
            "NSMB WorldEffectInternals",
            instanceID,
            frame,
            nds,
            base,
            guid,
            0,
            0,
            slot.VTable);
        count++;
        if (count >= 16)
            break;
    }

    if (count == 0)
    {
        std::printf(
            "NSMB WorldEffectInternals: role=%s inst=%d frame=%u count=0\n",
            G.NetRole == Role::Host ? "host" : "client",
            instanceID,
            frame);
    }
}

void TraceWorldObjectLifecyclesIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.StateSync.WorldTraceObjectLifecycles || !nds || !nds->MainRAM || instanceID < 0 || instanceID >= 16)
        return;
    if (frame < G.StateSync.WorldTraceObjectLifecyclesStartFrame ||
        (G.StateSync.WorldTraceObjectLifecyclesEndFrame != kNoFrameLimit &&
            frame > G.StateSync.WorldTraceObjectLifecyclesEndFrame))
        return;
    if ((frame % static_cast<melonDS::u32>(G.StateSync.WorldTraceObjectLifecyclesInterval)) != 0 ||
        G.LastTracedWorldObjectLifecyclesFrame[instanceID] == frame)
        return;
    G.LastTracedWorldObjectLifecyclesFrame[instanceID] = frame;

    struct LifecycleActor
    {
        melonDS::u32 VTable = 0;
        melonDS::u32 Base = 0;
        melonDS::u32 GUID = 0;
        melonDS::u32 Settings = 0;
        melonDS::u32 PosX = 0;
        melonDS::u32 PosY = 0;
        melonDS::u32 PosZ = 0;
        melonDS::u16 ObjectID = 0;
        melonDS::u8 State = 0;
        melonDS::u8 Type = 0;
        melonDS::u8 SkipFlags = 0;
    };

    std::vector<LifecycleActor> actors;
    const GameStateObjectScanCache cache = BuildGameStateObjectScanCache(nds);
    actors.reserve(cache.Entries.size());
    for (const GameStateObjectScanEntry& entry : cache.Entries)
    {
        LifecycleActor actor;
        actor.VTable = entry.VTable;
        actor.Base = entry.Actor.Base;
        actor.GUID = entry.Actor.GUID;
        actor.Settings = entry.Actor.Settings;
        actor.PosX = entry.Actor.PosX;
        actor.PosY = entry.Actor.PosY;
        actor.PosZ = entry.Actor.PosZ;
        actor.ObjectID = entry.ObjectID;
        actor.State = entry.LifecycleState;
        actor.Type = entry.Type;
        actor.SkipFlags = entry.SkipFlags;
        if (G.StateSync.WorldTraceActorInternals &&
            ShouldTraceWorldActorInternals(entry.ObjectID, entry.VTable))
        {
            PrintWorldActorInternalWords(
                "NSMB WorldActorInternals",
                instanceID,
                frame,
                nds,
                actor.Base,
                actor.GUID,
                actor.ObjectID,
                actor.Settings,
                actor.VTable);
        }
        if (actor.State == 0 || actor.State > 2 || actor.Type > 2)
            continue;
        actors.push_back(actor);
    }

    std::sort(actors.begin(), actors.end(), [](const LifecycleActor& lhs, const LifecycleActor& rhs) {
        if (lhs.State != rhs.State)
            return lhs.State < rhs.State;
        if (lhs.ObjectID != rhs.ObjectID)
            return lhs.ObjectID < rhs.ObjectID;
        return lhs.GUID < rhs.GUID;
    });
    std::printf("NSMB WorldObjects: role=%s inst=%d frame=%u count=%zu",
        G.NetRole == Role::Host ? "host" : "client",
        instanceID,
        frame,
        actors.size());
    for (const LifecycleActor& actor : actors)
    {
        std::printf(" actor=%u/%03X/%08X/%u/%u/%02X/%08X/%08X/%08X/%08X/%08X",
            actor.GUID,
            actor.ObjectID,
            actor.Settings,
            actor.State,
            actor.Type,
            actor.SkipFlags,
            actor.VTable,
            actor.Base,
            actor.PosX,
            actor.PosY,
            actor.PosZ);
    }
    std::printf("\n");
}

std::vector<ObjectScanSample> GetWorldMovingHazardsCached(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    std::vector<ObjectScanSample> actors;
    if (instanceID < 0 || instanceID >= 16)
        return actors;

    const bool periodicRescan =
        G.StateSync.WorldActorRescanInterval > 0 &&
        (frame % static_cast<melonDS::u32>(G.StateSync.WorldActorRescanInterval)) == 0;
    if (!periodicRescan)
    {
        const melonDS::u32 count = std::min(
            G.WorldMovingHazardCacheCounts[instanceID],
            static_cast<melonDS::u32>(kMaxWorldMovingHazards));
        bool valid = count > 0;
        for (melonDS::u32 i = 0; i < count; i++)
        {
            ObjectScanSample actor;
            if (!ReadObjectByBase(
                    nds,
                    G.WorldMovingHazardBaseCaches[instanceID][i],
                    G.WorldMovingHazardGUIDCaches[instanceID][i],
                    kVsMovingHazardObjectID,
                    kVsMovingHazardSettings,
                    actor) ||
                actor.StateType != 1)
            {
                valid = false;
                break;
            }
            actors.push_back(actor);
        }
        if (valid)
            return actors;
        actors.clear();
    }

    actors = FindActiveObjectsByIDAndSettings(nds, kVsMovingHazardObjectID, kVsMovingHazardSettings);
    if (actors.size() > kMaxWorldMovingHazards)
        actors.erase(actors.begin(), actors.end() - kMaxWorldMovingHazards);
    G.WorldMovingHazardCacheCounts[instanceID] = static_cast<melonDS::u32>(actors.size());
    for (std::size_t i = 0; i < kMaxWorldMovingHazards; i++)
    {
        G.WorldMovingHazardBaseCaches[instanceID][i] = i < actors.size() ? actors[i].Base : 0;
        G.WorldMovingHazardGUIDCaches[instanceID][i] = i < actors.size() ? actors[i].GUID : 0;
    }
    return actors;
}

ObjectScanSample GetWorldActorCached(
    int instanceID,
    melonDS::u32 frame,
    melonDS::NDS* nds,
    melonDS::u16 objectID,
    melonDS::u32 settings,
    melonDS::u32* baseCache,
    melonDS::u32* guidCache)
{
    ObjectScanSample actor;
    if (instanceID < 0 || instanceID >= 16)
        return actor;

    ObjectScanSample cachedActor;
    const bool periodicRescan =
        G.StateSync.WorldActorRescanInterval > 0 &&
        (frame % static_cast<melonDS::u32>(G.StateSync.WorldActorRescanInterval)) == 0;
    if (!periodicRescan &&
        baseCache[instanceID] != 0 &&
        ReadObjectByBase(nds, baseCache[instanceID], guidCache[instanceID], objectID, settings, cachedActor) &&
        cachedActor.StateType == 1)
        return cachedActor;

    actor = FindNewestActiveObjectByIDAndSettings(nds, objectID, settings);
    if (!actor.Found)
        actor = cachedActor;
    baseCache[instanceID] = actor.Found ? actor.Base : 0;
    guidCache[instanceID] = actor.Found ? actor.GUID : 0;
    return actor;
}

void WriteObjectTransform(melonDS::NDS* nds, const ObjectScanSample& actor, melonDS::u32 posX, melonDS::u32 posY, melonDS::u32 posZ, bool clearVelocity);

void NormalizeMvlEntranceSpawnStateIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.Mvl.NormalizeEntranceSpawnWrites)
        return;
    if (!nds || !nds->MainRAM || instanceID < 0 || instanceID >= 16)
        return;
    if (!IsMarioVsLuigiGameplay(nds))
        return;

    const melonDS::u32 oldID0 = nds->ARM9Read8(kEntranceSpawnEntranceIDAddr);
    const melonDS::u32 oldID1 = nds->ARM9Read8(kEntranceSpawnEntranceIDAddr + 1);
    const melonDS::u32 oldPtr0 = nds->ARM9Read32(kEntranceSpawnEntranceAddr);
    const melonDS::u32 oldPtr1 = nds->ARM9Read32(kEntranceSpawnEntranceAddr + sizeof(melonDS::u32));

    const bool idsNeedFix = oldID0 != 0 || oldID1 != 1;
    const bool ptr0Valid = IsMainRAMAddress(nds, oldPtr0, 0x14);
    const bool ptr1Valid = IsMainRAMAddress(nds, oldPtr1, 0x14);
    const bool fallbackPtr1Valid = IsMainRAMAddress(nds, oldPtr0 + 0x14, 0x14);
    const bool ptrsNeedFix = ptr0Valid && fallbackPtr1Valid && (!ptr1Valid || oldPtr1 == oldPtr0);
    if (!idsNeedFix && !ptrsNeedFix)
        return;

    nds->ARM9Write8(kEntranceSpawnEntranceIDAddr, 0);
    nds->ARM9Write8(kEntranceSpawnEntranceIDAddr + 1, 1);
    nds->ARM9Write8(kEntranceTransitionFlagsAddr, 0);
    nds->ARM9Write8(kEntranceTransitionFlagsAddr + 1, 0);
    if (ptrsNeedFix)
    {
        nds->ARM9Write32(kEntranceSpawnEntranceAddr, oldPtr0);
        nds->ARM9Write32(kEntranceSpawnEntranceAddr + sizeof(melonDS::u32), oldPtr0 + 0x14);
    }

    if (!G.MvlEntranceSpawnNormalizedLogged[instanceID])
    {
        G.MvlEntranceSpawnNormalizedLogged[instanceID] = true;
        std::printf(
            "NSMB MvL: normalized entrance spawn state inst=%d frame=%u ids=%u/%u->0/1 ptr=%08X/%08X->%08X/%08X\n",
            instanceID,
            frame,
            oldID0,
            oldID1,
            oldPtr0,
            oldPtr1,
            oldPtr0,
            ptrsNeedFix ? oldPtr0 + 0x14 : oldPtr1);
        std::fflush(stdout);
    }
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

void ApplyPlayerStickToStar(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (G.RuntimePatch.PlayerStickToStarStartFrame == 0 && G.RuntimePatch.PlayerStickToStarEndFrame == 0) return;
    if (frame < G.RuntimePatch.PlayerStickToStarStartFrame || frame > G.RuntimePatch.PlayerStickToStarEndFrame) return;
    if (instanceID < 0 || instanceID >= 16) return;
    if (!nds || !nds->MainRAM) return;

    ObjectScanSample star = FindObjectByIDAndSettings(nds, kVsBattleStarActorObjectID, kVsBattleStarActorSettings);
    if (!star.Found)
        star = FindVsBattleStarCandidate(nds);
    const PlayerActorScanSample players = FindPlayerActors(nds);
    const ObjectScanSample& player = (G.RuntimePatch.PlayerStickToStarSlot == 1) ? players.Actor1 : players.Actor0;
    if (!star.Found || !player.Found)
    {
        if (frame == G.RuntimePatch.PlayerStickToStarStartFrame)
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
    if (frame == G.RuntimePatch.PlayerStickToStarStartFrame)
    {
        std::printf("NSMB Test: started player stick to VS star inst=%d frame=%u-%u slot=%d playerGuid=0x%X starGuid=0x%X pos=0x%08X,0x%08X,0x%08X\n",
            instanceID,
            G.RuntimePatch.PlayerStickToStarStartFrame,
            G.RuntimePatch.PlayerStickToStarEndFrame,
            G.RuntimePatch.PlayerStickToStarSlot,
            player.GUID,
            star.GUID,
            star.PosX,
            star.PosY,
            star.PosZ);
    }
}

void ForcePlayerDeathCountersIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.RuntimePatch.ForcePlayerDeathCountersEnabled || !nds || !nds->MainRAM)
        return;
    if (G.RuntimePatch.ForcePlayerDeathCountersHostOnly && G.NetRole != Role::Host)
        return;
    if (G.RuntimePatch.ForcePlayerDeathCountersClientOnly && G.NetRole != Role::Client)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (frame < G.RuntimePatch.ForcePlayerDeathCountersStartFrame)
        return;
    if (G.RuntimePatch.ForcePlayerDeathCountersEndFrame != 0 && frame > G.RuntimePatch.ForcePlayerDeathCountersEndFrame)
        return;
    if (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1)
        return;

    const melonDS::u32 old0 = nds->ARM9Read32(kGamePlayerDeathsAddr);
    const melonDS::u32 old1 = nds->ARM9Read32(kGamePlayerDeathsAddr + sizeof(melonDS::u32));
    const melonDS::u32 oldLife0 = nds->ARM9Read32(kGamePlayerLivesAddr);
    const melonDS::u32 oldLife1 = nds->ARM9Read32(kGamePlayerLivesAddr + sizeof(melonDS::u32));
    nds->ARM9Write32(kGamePlayerDeathsAddr, G.RuntimePatch.ForcePlayerDeathCounter0);
    nds->ARM9Write32(kGamePlayerDeathsAddr + sizeof(melonDS::u32), G.RuntimePatch.ForcePlayerDeathCounter1);
    if (G.RuntimePatch.ForcePlayerLivesEnabled)
    {
        nds->ARM9Write32(kGamePlayerLivesAddr, G.RuntimePatch.ForcePlayerLife0);
        nds->ARM9Write32(kGamePlayerLivesAddr + sizeof(melonDS::u32), G.RuntimePatch.ForcePlayerLife1);
    }

    if (!G.ForcePlayerDeathCountersLogged[instanceID])
    {
        std::printf(
            "NSMB Test: force player death counters inst=%d frame=%u range=%u-%u old=%u/%u value=%u/%u lives=%u/%u->%u/%u enabled=%d\n",
            instanceID,
            frame,
            G.RuntimePatch.ForcePlayerDeathCountersStartFrame,
            G.RuntimePatch.ForcePlayerDeathCountersEndFrame,
            old0,
            old1,
            G.RuntimePatch.ForcePlayerDeathCounter0,
            G.RuntimePatch.ForcePlayerDeathCounter1,
            oldLife0,
            oldLife1,
            G.RuntimePatch.ForcePlayerLife0,
            G.RuntimePatch.ForcePlayerLife1,
            G.RuntimePatch.ForcePlayerLivesEnabled ? 1 : 0);
        G.ForcePlayerDeathCountersLogged[instanceID] = true;
    }
}

void ForcePlayerInventoryPowerupsIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.RuntimePatch.ForcePlayerInventoryPowerupsEnabled || !nds || !nds->MainRAM)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (frame < G.RuntimePatch.ForcePlayerInventoryPowerupsStartFrame)
        return;
    if (G.RuntimePatch.ForcePlayerInventoryPowerupsEndFrame != 0 && frame > G.RuntimePatch.ForcePlayerInventoryPowerupsEndFrame)
        return;
    if (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1)
        return;

    const melonDS::u8 old0 = nds->ARM9Read8(kGamePlayerInventoryPowerupAddr);
    const melonDS::u8 old1 = nds->ARM9Read8(kGamePlayerInventoryPowerupAddr + 1);
    nds->ARM9Write8(kGamePlayerInventoryPowerupAddr, static_cast<melonDS::u8>(G.RuntimePatch.ForcePlayerInventoryPowerup0 & 0xFF));
    nds->ARM9Write8(kGamePlayerInventoryPowerupAddr + 1, static_cast<melonDS::u8>(G.RuntimePatch.ForcePlayerInventoryPowerup1 & 0xFF));

    if (!G.ForcePlayerInventoryPowerupsLogged[instanceID])
    {
        std::printf(
            "NSMB Test: force player inventory powerups inst=%d frame=%u range=%u-%u old=%u/%u value=%u/%u\n",
            instanceID,
            frame,
            G.RuntimePatch.ForcePlayerInventoryPowerupsStartFrame,
            G.RuntimePatch.ForcePlayerInventoryPowerupsEndFrame,
            old0,
            old1,
            G.RuntimePatch.ForcePlayerInventoryPowerup0 & 0xFF,
            G.RuntimePatch.ForcePlayerInventoryPowerup1 & 0xFF);
        G.ForcePlayerInventoryPowerupsLogged[instanceID] = true;
    }
}

void ForcePlayerPowerupsIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.RuntimePatch.ForcePlayerPowerupsEnabled || !nds || !nds->MainRAM)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (frame < G.RuntimePatch.ForcePlayerPowerupsStartFrame)
        return;
    if (G.RuntimePatch.ForcePlayerPowerupsEndFrame != 0 && frame > G.RuntimePatch.ForcePlayerPowerupsEndFrame)
        return;
    if (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1)
        return;

    const melonDS::u8 value0 = static_cast<melonDS::u8>(G.RuntimePatch.ForcePlayerPowerup0 & 0xFF);
    const melonDS::u8 value1 = static_cast<melonDS::u8>(G.RuntimePatch.ForcePlayerPowerup1 & 0xFF);
    const melonDS::u8 old0 = nds->ARM9Read8(kGamePlayerPowerupAddr);
    const melonDS::u8 old1 = nds->ARM9Read8(kGamePlayerPowerupAddr + 1);
    nds->ARM9Write8(kGamePlayerPowerupAddr, value0);
    nds->ARM9Write8(kGamePlayerPowerupAddr + 1, value1);

    melonDS::u8 actorOldState[2] { 0xFF, 0xFF };
    melonDS::u8 actorOldForm[2] { 0xFF, 0xFF };
    melonDS::u32 actorBase[2] {};
    const PlayerActorScanSample players = FindPlayerActors(nds);
    const ObjectScanSample actors[2] { players.Actor0, players.Actor1 };
    for (const ObjectScanSample& actor : actors)
    {
        if (!actor.Found || !IsARM9MainRAMAddress(actor.Base))
            continue;
        const melonDS::u32 player = nds->ARM9Read8(actor.Base + kPlayerActorPlayerIDOffset) & 1u;
        const melonDS::u8 value = player == 0 ? value0 : value1;
        actorBase[player] = actor.Base;
        actorOldState[player] = nds->ARM9Read8(actor.Base + kPlayerBasePowerupStateOffset);
        actorOldForm[player] = nds->ARM9Read8(actor.Base + kPlayerBasePowerupFormStateOffset);
        nds->ARM9Write8(actor.Base + kPlayerBasePowerupStateOffset, value);
        nds->ARM9Write8(actor.Base + kPlayerBasePowerupFormStateOffset, value);
        nds->ARM9Write8(actor.Base + kPlayerBasePowerupSubStateOffset, 0);
    }

    if (!G.ForcePlayerPowerupsLogged[instanceID])
    {
        std::printf(
            "NSMB Test: force player active powerups inst=%d frame=%u range=%u-%u "
            "globalOld=%u/%u value=%u/%u actorBase=0x%08X/0x%08X actorStateOld=%u/%u actorFormOld=%u/%u\n",
            instanceID,
            frame,
            G.RuntimePatch.ForcePlayerPowerupsStartFrame,
            G.RuntimePatch.ForcePlayerPowerupsEndFrame,
            old0,
            old1,
            value0,
            value1,
            actorBase[0],
            actorBase[1],
            actorOldState[0],
            actorOldState[1],
            actorOldForm[0],
            actorOldForm[1]);
        G.ForcePlayerPowerupsLogged[instanceID] = true;
    }
}

void ForcePlayerStarCountersIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.RuntimePatch.ForcePlayerStarCountersEnabled || !nds || !nds->MainRAM)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (frame < G.RuntimePatch.ForcePlayerStarCountersStartFrame)
        return;
    if (G.RuntimePatch.ForcePlayerStarCountersEndFrame != 0 && frame > G.RuntimePatch.ForcePlayerStarCountersEndFrame)
        return;
    if (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1)
        return;

    const melonDS::u32 oldBattle0 = nds->ARM9Read32(kGamePlayerBattleStarsAddr);
    const melonDS::u32 oldBattle1 = nds->ARM9Read32(kGamePlayerBattleStarsAddr + sizeof(melonDS::u32));
    const melonDS::u32 oldDisplayed0 = nds->ARM9Read32(kGamePlayerDisplayedStarsAddr);
    const melonDS::u32 oldDisplayed1 = nds->ARM9Read32(kGamePlayerDisplayedStarsAddr + sizeof(melonDS::u32));
    const melonDS::u32 oldCollected0 = nds->ARM9Read32(kGamePlayerCollectedStarsAddr);
    const melonDS::u32 oldCollected1 = nds->ARM9Read32(kGamePlayerCollectedStarsAddr + sizeof(melonDS::u32));

    nds->ARM9Write32(kGamePlayerBattleStarsAddr, G.RuntimePatch.ForcePlayerBattleStars0);
    nds->ARM9Write32(kGamePlayerBattleStarsAddr + sizeof(melonDS::u32), G.RuntimePatch.ForcePlayerBattleStars1);
    nds->ARM9Write32(kGamePlayerDisplayedStarsAddr, G.RuntimePatch.ForcePlayerDisplayedStars0);
    nds->ARM9Write32(kGamePlayerDisplayedStarsAddr + sizeof(melonDS::u32), G.RuntimePatch.ForcePlayerDisplayedStars1);
    nds->ARM9Write32(kGamePlayerCollectedStarsAddr, G.RuntimePatch.ForcePlayerCollectedStars0);
    nds->ARM9Write32(kGamePlayerCollectedStarsAddr + sizeof(melonDS::u32), G.RuntimePatch.ForcePlayerCollectedStars1);

    if (!G.ForcePlayerStarCountersLogged[instanceID])
    {
        std::printf(
            "NSMB Test: force player star counters inst=%d frame=%u range=%u-%u "
            "battle=%u/%u->%u/%u displayed=%u/%u->%u/%u collected=%u/%u->%u/%u\n",
            instanceID,
            frame,
            G.RuntimePatch.ForcePlayerStarCountersStartFrame,
            G.RuntimePatch.ForcePlayerStarCountersEndFrame,
            oldBattle0,
            oldBattle1,
            G.RuntimePatch.ForcePlayerBattleStars0,
            G.RuntimePatch.ForcePlayerBattleStars1,
            oldDisplayed0,
            oldDisplayed1,
            G.RuntimePatch.ForcePlayerDisplayedStars0,
            G.RuntimePatch.ForcePlayerDisplayedStars1,
            oldCollected0,
            oldCollected1,
            G.RuntimePatch.ForcePlayerCollectedStars0,
            G.RuntimePatch.ForcePlayerCollectedStars1);
        G.ForcePlayerStarCountersLogged[instanceID] = true;
    }
}

void PushScriptRemotePacketIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.RuntimePatch.ScriptRemotePacketEnabled || !nds || !nds->MainRAM)
        return;
    if (G.RuntimePatch.ScriptRemotePacketPlayer < 0 || G.RuntimePatch.ScriptRemotePacketPlayer > 1)
        return;
    if (G.RuntimePatch.ScriptRemotePacketInputInstance < 0 || G.RuntimePatch.ScriptRemotePacketInputInstance >= 16)
        return;
    const melonDS::u32 startFrame = G.RuntimePatch.ScriptRemotePacketEnabled
        ? G.RuntimePatch.ScriptRemotePacketStartFrame
        : G.RuntimePatch.PacketBridgeJitHelperPatchFrame;
    const melonDS::u32 endFrame = G.RuntimePatch.ScriptRemotePacketEnabled ? G.RuntimePatch.ScriptRemotePacketEndFrame : 0;
    if (frame < startFrame)
        return;
    if (endFrame != 0 && frame > endFrame)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;

    const InputState input = ConvertStockXToTouch(ApplyScriptRemotePacketInputScript(
        G.RuntimePatch.ScriptRemotePacketInputInstance,
        frame,
        NeutralInput()));
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
        static_cast<melonDS::u32>(G.RuntimePatch.ScriptRemotePacketPlayer),
        packet);

    if (!G.ScriptRemotePacketLogged[instanceID])
    {
        std::printf("NSMB Test: script remote packet inst=%d frame=%u range=%u-%u player=%d inputInstance=%d tick=0x%04X keys=0x%04X\n",
            instanceID,
            frame,
            G.RuntimePatch.ScriptRemotePacketStartFrame,
            G.RuntimePatch.ScriptRemotePacketEndFrame,
            G.RuntimePatch.ScriptRemotePacketPlayer,
            G.RuntimePatch.ScriptRemotePacketInputInstance,
            tick,
            keys);
        G.ScriptRemotePacketLogged[instanceID] = true;
    }
}

int CurrentPacketBridgeLocalPlayer()
{
    if (G.NetRole == Role::Client)
        return 1;
    return 0;
}

InputState ApplyImitationAIInput(
    int instanceID,
    melonDS::u32 frame,
    melonDS::NDS* nds,
    int player,
    const InputState& fallback);

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
        return ConvertStockXToTouch(localInput);
    if (hasRemoteInput)
        return ConvertStockXToTouch(remoteInput);
    return ConvertStockXToTouch(
        ApplyScriptRemotePacketInputScript(G.RuntimePatch.ScriptRemotePacketInputInstance, frame, NeutralInput()));
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
    if (G.Input.NetplayOnly && G.Connection.StartFrame != 0 && frame >= G.Connection.StartFrame)
    {
        tick = (frame - G.Connection.StartFrame) & 0xFFFF;
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
        InputState effectiveInput = ApplyRuleBasedAIInput(instanceID, frame, nds, player, input);
        effectiveInput = ApplyImitationAIInput(instanceID, frame, nds, player, effectiveInput);
        RecordAIPlayLogAppliedInput(instanceID, frame, player, effectiveInput);
        const melonDS::u32 keys = (~effectiveInput.KeyMask) & 0x0FFF;
        nds->ARM9Write16(kPacketBridgeJitScratchKeysAddr + static_cast<melonDS::u32>(player * 2),
            static_cast<melonDS::u16>(keys));

        melonDS::u8 packet[52] {};
        packet[0] = static_cast<melonDS::u8>(tick & 0xFF);
        packet[1] = static_cast<melonDS::u8>((tick >> 8) & 0xFF);
        packet[2] = static_cast<melonDS::u8>(keys & 0xFF);
        packet[3] = static_cast<melonDS::u8>((keys >> 8) & 0xFF);
        packet[4] = action;
        packet[5] = effectiveInput.Touching ? 1 : 0;
        packet[6] = static_cast<melonDS::u8>(std::min<int>(effectiveInput.TouchX, 255));
        packet[7] = static_cast<melonDS::u8>(std::min<int>(effectiveInput.TouchY, 191));
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

    if (G.Input.NetplayTrace && (frame % 60) == 0)
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

void ClearMvlCameraInitHoldIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds);

void ApplyRollbackResimFramePatches(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!nds || instanceID < 0 || instanceID >= 16)
        return;

    ApplyMvlRuntimeConfigIfNeeded(nds);
    ForceNSMLPacketBridgeNetReadyIfNeeded(instanceID, frame, nds);
    ForceNSMLGameLocalPlayerIDIfNeeded(frame, nds);
    NormalizeMvlEntranceSpawnStateIfNeeded(instanceID, frame, nds);
    ClearMvlCameraInitHoldIfNeeded(instanceID, frame, nds);
    ForcePlayerDeathCountersIfNeeded(instanceID, frame, nds);
    ForcePlayerInventoryPowerupsIfNeeded(instanceID, frame, nds);
    ForcePlayerStarCountersIfNeeded(instanceID, frame, nds);
}

void ApplyRollbackResimPostFramePatches(melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!nds)
        return;

    ForceNSMLGameLocalPlayerIDIfNeeded(frame, nds);
    melonDS::NSML_RefreshMarioVsLuigiPacketSlots(nds);
    ForceNSMLGameLocalPlayerIDIfNeeded(frame, nds);
}

bool RollbackResimulateIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.Rollback.Enabled || !G.Rollback.Resimulate || !G.Input.NetplayOnly || !nds)
        return false;
    if (instanceID < 0 || instanceID >= 16)
        return false;

    melonDS::u32 mismatchFrame = kNoFrameLimit;
    melonDS::u32 restoreFrame = kNoFrameLimit;
    RollbackStoredState checkpoint;
    std::vector<RollbackStoredState> restoreChain;
    std::vector<RollbackStoredState> reverseStates;
    std::vector<melonDS::u8> latestMainRAM;
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        if (G.PendingRollbackFrame == kNoFrameLimit)
            return false;
        mismatchFrame = G.PendingRollbackFrame;
        if (mismatchFrame == frame)
        {
            if (G.Input.NetplayTrace)
            {
                std::printf("NSMB Rollback: current-frame mismatch consumed without resim frame=%u\n",
                    frame);
                std::fflush(stdout);
            }
            G.PendingRollbackFrame = kNoFrameLimit;
            G.PendingRollbackObservedFrame = kNoFrameLimit;
            return false;
        }
        if (mismatchFrame >= frame)
            return false;
        if (G.Rollback.MaxResimFrames > 0)
        {
            const melonDS::u32 maxResimFrames = static_cast<melonDS::u32>(G.Rollback.MaxResimFrames);
            if (frame - mismatchFrame > maxResimFrames)
            {
                const melonDS::u32 cappedFrame = frame - maxResimFrames;
                if (G.Input.NetplayTrace)
                {
                    std::printf("NSMB Rollback: capping resim window originalMismatch=%u cappedMismatch=%u current=%u maxFrames=%u\n",
                        mismatchFrame,
                        cappedFrame,
                        frame,
                        maxResimFrames);
                    std::fflush(stdout);
                }
                mismatchFrame = cappedFrame;
            }
        }
        if (G.Rollback.ResimulateDelayFrames > 0
            && G.PendingRollbackObservedFrame != kNoFrameLimit
            && frame < G.PendingRollbackObservedFrame + static_cast<melonDS::u32>(G.Rollback.ResimulateDelayFrames))
        {
            return false;
        }

        if (!G.RollbackStore.LatestAtOrBefore(mismatchFrame, restoreFrame, checkpoint))
        {
            std::printf(
                "NSMB Rollback: cannot resimulate mismatch=%u at current=%u, checkpoint missing window=%d interval=%d\n",
                mismatchFrame,
                frame,
                G.Rollback.Window,
                G.Rollback.CheckpointInterval);
            G.PendingRollbackFrame = kNoFrameLimit;
            G.PendingRollbackObservedFrame = kNoFrameLimit;
            return false;
        }
        const bool restoreReady = IsRollbackPreimageBackend()
            ? BuildRollbackPreimageRestoreLocked(restoreFrame, reverseStates, latestMainRAM)
            : BuildRollbackRestoreChainLocked(restoreFrame, restoreChain);
        if (!restoreReady)
        {
            std::printf(
                "NSMB Rollback: cannot resimulate mismatch=%u from delta checkpoint=%u, base=%u chain missing\n",
                mismatchFrame,
                restoreFrame,
                checkpoint.BaseFrame);
            G.PendingRollbackFrame = kNoFrameLimit;
            G.PendingRollbackObservedFrame = kNoFrameLimit;
            return false;
        }
        G.PendingRollbackFrame = kNoFrameLimit;
        G.PendingRollbackObservedFrame = kNoFrameLimit;

        G.RollbackStore.EraseAfter(restoreFrame);
    }

    const auto rollbackStart = std::chrono::steady_clock::now();
    const auto restoreStart = rollbackStart;
    const bool restored = IsRollbackPreimageBackend()
        ? RestoreRollbackPreimageState(nds, checkpoint, reverseStates, latestMainRAM)
        : RestoreRollbackStoredStates(nds, restoreChain);
    if (!restored)
    {
        std::printf("NSMB Rollback: resim restore failed inst=%d restoreFrame=%u current=%u\n",
            instanceID,
            restoreFrame,
            frame);
        return false;
    }
    const unsigned long long restoreUs = ElapsedUs(restoreStart);
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        RefreshRollbackFrameDeltaShadowLocked(restoreFrame, nds);
    }

    const int localPlayer = CurrentPacketBridgeLocalPlayer();
    melonDS::u32 resimulated = 0;
    unsigned long long resimRunFrameTotalUs = 0;
    unsigned long long resimRunFrameMaxUs = 0;
    unsigned long long resimCheckpointSaveTotalUs = 0;
    unsigned long long resimCheckpointSaveMaxUs = 0;
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

        ApplyRollbackResimFramePatches(instanceID, f, nds);
        WritePacketBridgeJitScratchInputs(
            instanceID,
            f,
            nds,
            localPlayer,
            localInput,
            remoteInput,
            true,
            predictedRemote);

        const InputState runtimeLocalInput = ConvertStockXToTouch(localInput);
        nds->SetKeyMask(runtimeLocalInput.KeyMask);
        if (runtimeLocalInput.Touching)
            nds->TouchScreen(runtimeLocalInput.TouchX, runtimeLocalInput.TouchY);
        else
            nds->ReleaseScreen();

        const bool skipRender = G.Rollback.SkipRenderDuringResim;
        if (skipRender)
            nds->GPU.SetRollbackSkipRender(true);
        const auto runFrameStart = std::chrono::steady_clock::now();
        nds->RunFrame();
        const unsigned long long runFrameUs = ElapsedUs(runFrameStart);
        resimRunFrameTotalUs += runFrameUs;
        if (runFrameUs > resimRunFrameMaxUs)
            resimRunFrameMaxUs = runFrameUs;
        if (skipRender)
            nds->GPU.SetRollbackSkipRender(false);
        ApplyRollbackResimPostFramePatches(f + 1, nds);
        resimulated++;

        const bool saveResimCheckpoint =
            !G.Rollback.SkipIntermediateResimCheckpoints || (f + 1) == frame;
        if (saveResimCheckpoint)
        {
            const auto checkpointSaveStart = std::chrono::steady_clock::now();
            {
                std::lock_guard<std::mutex> lock(G.Mutex);
                SaveRollbackCheckpointNowLocked(f + 1, nds);
            }
            const unsigned long long checkpointSaveUs = ElapsedUs(checkpointSaveStart);
            resimCheckpointSaveTotalUs += checkpointSaveUs;
            if (checkpointSaveUs > resimCheckpointSaveMaxUs)
                resimCheckpointSaveMaxUs = checkpointSaveUs;
        }

        if (nds->NumFrames != f + 1 && G.Input.NetplayTrace)
        {
            std::printf("NSMB Rollback: resim frame counter drift expected=%u actual=%u\n",
                f + 1,
                nds->NumFrames);
        }
    }

    const unsigned long long rollbackTotalUs = ElapsedUs(rollbackStart);
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        RefreshRollbackFrameDeltaShadowLocked(frame, nds);
        RecordRollbackCheckpointRestoreLocked(restoreUs);
        RecordRollbackResimTimingLocked(
            resimulated,
            resimRunFrameTotalUs,
            resimRunFrameMaxUs,
            resimCheckpointSaveTotalUs,
            resimCheckpointSaveMaxUs,
            rollbackTotalUs);
        G.RollbackResimulateCount++;
    }
    if (G.Input.NetplayTrace)
    {
        std::printf("NSMB Rollback: resimulated from checkpoint=%u mismatch=%u to current=%u frames=%u bytes=%zu restoreUs=%llu runUs=%llu runMaxUs=%llu checkpointSaveUs=%llu checkpointSaveMaxUs=%llu totalUs=%llu\n",
            restoreFrame,
            mismatchFrame,
            frame,
            resimulated,
            CheckpointBytes(checkpoint),
            restoreUs,
            resimRunFrameTotalUs,
            resimRunFrameMaxUs,
            resimCheckpointSaveTotalUs,
            resimCheckpointSaveMaxUs,
            rollbackTotalUs);
        std::fflush(stdout);
    }
    return true;
}

void ThrottleInputNetplayFrameLead(melonDS::NDS* nds, melonDS::u32 frame, melonDS::u32 sendFrame)
{
    if (!G.Input.NetplayOnly || G.Input.MaxFrameLead < 0 || !G.Enabled || !G.Ready)
        return;
    if (G.Connection.StartFrame != 0 && frame < G.Connection.StartFrame)
        return;
    if (IsPastTestInputRange(sendFrame))
        return;

    TraceHangPhase("begin", "frame-lead-throttle", -1, frame, frame, sendFrame);
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
            MaybeResendNetplayStartReadyLocked();
            remoteFrame = G.LastReceivedInputFrame;
        }

        if (remoteFrame == kNoFrameLimit)
        {
            TraceHangPhase("end", "frame-lead-throttle", -1, frame, frame, sendFrame);
            return;
        }

        const int lead = static_cast<int>(sendFrame) - static_cast<int>(remoteFrame);
        if (lead <= G.Input.MaxFrameLead)
        {
            if (blocked)
            {
                const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start).count();
                const auto waitedUs = static_cast<unsigned long long>(std::max<long long>(0, elapsedUs));
                RecordFrameLeadThrottleStats(waitedUs, loops);
                if (G.Input.HealthTrace && G.LastInputHealthThrottleResolvedFrame != frame)
                {
                    std::lock_guard<std::mutex> lock(G.Mutex);
                    G.LastInputHealthThrottleResolvedFrame = frame;
                    PrintInputHealthLineLocked(
                        "throttle-resolved",
                        frame,
                        frame,
                        sendFrame,
                        0,
                        waitedUs,
                        0,
                        CurrentInputLeadLocked(sendFrame),
                        true,
                        false);
                }
            }
            TraceHangPhase("end", "frame-lead-throttle", -1, frame, frame, sendFrame);
            return;
        }
        blocked = true;
        TraceHangPhase("blocked", "frame-lead-throttle", -1, frame, frame, sendFrame);
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            MaybeResendLatestInputForFrameLeadLocked();
        }

        if (G.Input.NetplayTrace && G.LastInputFrameThrottleTraceFrame != frame)
        {
            G.LastInputFrameThrottleTraceFrame = frame;
            std::printf("NSMB InputNetplay: frame throttle frame=%u sendFrame=%u remoteInputFrame=%u lead=%d maxLead=%d\n",
                frame,
                sendFrame,
                remoteFrame,
                lead,
                G.Input.MaxFrameLead);
            std::fflush(stdout);
        }

        if (G.Input.HealthTrace && G.LastInputHealthThrottleFrame != frame)
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            G.LastInputHealthThrottleFrame = frame;
            PrintInputHealthLineLocked(
                "throttle-blocked",
                frame,
                frame,
                sendFrame,
                0,
                0,
                0,
                CurrentInputLeadLocked(sendFrame),
                false,
                false);
        }

        if (G.TestEnabled && G.Bootstrap.WaitTimeoutMs > 0)
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= G.Bootstrap.WaitTimeoutMs)
            {
                std::printf("NSMB Test: input frame throttle timeout frame=%u sendFrame=%u remoteInputFrame=%u lead=%d waitedMs=%d\n",
                    frame,
                    sendFrame,
                    remoteFrame,
                    lead,
                    G.Bootstrap.WaitTimeoutMs);
                std::fflush(stdout);
                TraceHangPhase("timeout", "frame-lead-throttle", -1, frame, frame, sendFrame);
                if (G.Connection.RemoteInputTimeoutFatal)
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
    if (!G.RuntimePatch.PacketBridgeJitHelperPatchEnabled || !nds || !nds->MainRAM)
        return;
    if (!G.RuntimePatch.ScriptRemotePacketEnabled && !G.Enabled)
        return;
    if (G.RuntimePatch.ScriptRemotePacketEnabled &&
        (G.RuntimePatch.ScriptRemotePacketInputInstance < 0 || G.RuntimePatch.ScriptRemotePacketInputInstance >= 16))
        return;
    melonDS::u32 startFrame = G.RuntimePatch.ScriptRemotePacketStartFrame;
    if (G.Input.NetplayOnly)
    {
        startFrame = std::max(startFrame, G.RuntimePatch.PacketBridgeJitHelperPatchFrame);
        startFrame = std::max(startFrame, G.Connection.StartFrame);
    }
    if (G.RuntimePatch.ScriptRemotePacketEndFrame != 0 && frame > G.RuntimePatch.ScriptRemotePacketEndFrame)
        return;

    const bool traceScratch = G.ActiveFrameSpikeTrace;
    const auto scratchStart = std::chrono::steady_clock::now();
    unsigned long long peerStartWaitUs = 0;
    unsigned long long networkUs = 0;
    unsigned long long throttleUs = 0;
    unsigned long long lockstepRemoteWaitUs = 0;
    unsigned long long writeUs = 0;
    bool wroteScratch = false;

    const melonDS::u32 logicalFrame = InputNetplayLogicalFrame(frame);
    if (G.Input.NetplayOnly
        && G.Harness.WaitForPeerAtNetplayStart
        && G.LocalNetplayStartReadyFrame == kNoFrameLimit)
    {
        return;
    }

    const int localPlayer = CurrentPacketBridgeLocalPlayer();
    if (G.Input.NetplayOnly && G.Harness.WaitForPeerBeforeStart && G.Connection.StartFrame > 0
        && !RuleAIProvidesInputForPlayer(localPlayer ^ 1)
        && !ImitationAIProvidesInputForPlayer(localPlayer ^ 1))
    {
        const melonDS::u32 delay = static_cast<melonDS::u32>(std::max(0, G.Connection.Delay));
        const melonDS::u32 sendStartFrame = (G.Connection.StartFrame > delay)
            ? G.Connection.StartFrame - delay
            : 0;
        if (frame == sendStartFrame)
        {
            std::printf("NSMB InputNetplay: waiting for peer before send start frame=%u applyStart=%u\n",
                sendStartFrame,
                G.Connection.StartFrame);
            std::fflush(stdout);
            const auto waitStart = std::chrono::steady_clock::now();
            WaitForPeerIfNeeded(true);
            peerStartWaitUs = static_cast<unsigned long long>(ElapsedUs(waitStart));
        }
    }

    InputState effectiveLocalInput = localInput;
    InputState remoteInput = NeutralInput();
    bool hasRemoteInput = false;
    bool predictedRemoteInput = false;
    melonDS::u32 sendFrame = logicalFrame;
    if (G.Enabled && G.Ready)
    {
        sendFrame = G.Input.NetplayOnly
            ? logicalFrame + static_cast<melonDS::u32>(std::max(0, G.Connection.Delay))
            : frame;
        const auto networkStart = std::chrono::steady_clock::now();
        {
            std::unique_lock<std::mutex> lock(G.Mutex);
            PumpNetworkLocked(nds, frame);
            SendMatchSeedLocked();
            G.LocalInputs[sendFrame] = localInput;
            SendInputLocked(sendFrame, localInput);
            if (G.Input.NetplayOnly)
            {
                auto localIt = G.LocalInputs.find(logicalFrame);
                effectiveLocalInput = localIt != G.LocalInputs.end() ? localIt->second : NeutralInput();
            }
            auto it = G.RemoteInputs.find(logicalFrame);
            if (it != G.RemoteInputs.end())
            {
                remoteInput = it->second;
                hasRemoteInput = true;
            }
            else if (G.Rollback.Enabled && G.Input.NetplayOnly
                && (G.Connection.StartFrame == 0 || logicalFrame >= G.Connection.StartFrame)
                && TryWaitForRollbackRemoteInputLocked(lock, nds, frame, logicalFrame, remoteInput))
            {
                hasRemoteInput = true;
            }
            else if (G.Rollback.Enabled && G.Input.NetplayOnly
                && (G.Connection.StartFrame == 0 || logicalFrame >= G.Connection.StartFrame))
            {
                hasRemoteInput = GetRollbackRemoteInputLocked(logicalFrame, remoteInput, predictedRemoteInput);
            }
        }
        networkUs = static_cast<unsigned long long>(ElapsedUs(networkStart));

        const auto throttleStart = std::chrono::steady_clock::now();
        ThrottleInputNetplayFrameLead(nds, frame, sendFrame);
        throttleUs = static_cast<unsigned long long>(ElapsedUs(throttleStart));

        const bool aiProvidesRemoteInput =
            RuleAIProvidesInputForPlayer(localPlayer ^ 1) ||
            ImitationAIProvidesInputForPlayer(localPlayer ^ 1);
        if (!hasRemoteInput
            && !G.Rollback.Enabled
            && G.Connection.LocalWaitsForRemote
            && !aiProvidesRemoteInput
            && (!G.Input.NetplayOnly || G.Connection.StartFrame == 0 || logicalFrame >= G.Connection.StartFrame))
        {
            const auto waitStart = std::chrono::steady_clock::now();
            remoteInput = WaitForRemoteInput(logicalFrame);
            lockstepRemoteWaitUs = static_cast<unsigned long long>(ElapsedUs(waitStart));
            hasRemoteInput = true;
        }
    }

    const bool beforeStart = logicalFrame < startFrame;
    if (!beforeStart)
    {
        const auto writeStart = std::chrono::steady_clock::now();
        WritePacketBridgeJitScratchInputs(
            instanceID,
            logicalFrame,
            nds,
            localPlayer,
            effectiveLocalInput,
            remoteInput,
            hasRemoteInput,
            predictedRemoteInput);
        writeUs = static_cast<unsigned long long>(ElapsedUs(writeStart));
        wroteScratch = true;
    }

    if (traceScratch)
    {
        const unsigned long long totalUs = static_cast<unsigned long long>(ElapsedUs(scratchStart));
        const unsigned long long thresholdUs = static_cast<unsigned long long>(
            std::min(G.ActiveFrameSpikeThresholdUs, 10000));
        if (totalUs >= thresholdUs)
        {
            std::printf(
                "NSMB PacketBridgeScratchSpike: inst=%d frame=%u logicalFrame=%u totalMs=%.3f peerStartWaitMs=%.3f networkMs=%.3f throttleMs=%.3f lockstepRemoteWaitMs=%.3f writeMs=%.3f wrote=%d beforeStart=%d hasRemote=%d predictedRemote=%d\n",
                instanceID,
                frame,
                logicalFrame,
                static_cast<double>(totalUs) / 1000.0,
                static_cast<double>(peerStartWaitUs) / 1000.0,
                static_cast<double>(networkUs) / 1000.0,
                static_cast<double>(throttleUs) / 1000.0,
                static_cast<double>(lockstepRemoteWaitUs) / 1000.0,
                static_cast<double>(writeUs) / 1000.0,
                wroteScratch ? 1 : 0,
                beforeStart ? 1 : 0,
                hasRemoteInput ? 1 : 0,
                predictedRemoteInput ? 1 : 0);
            std::fflush(stdout);
        }
    }
    if (G.Input.HealthTrace
        && G.Input.HealthTraceInterval > 0
        && logicalFrame != G.LastInputHealthSummaryFrame
        && (logicalFrame % static_cast<melonDS::u32>(G.Input.HealthTraceInterval)) == 0)
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        G.LastInputHealthSummaryFrame = logicalFrame;
        PrintInputHealthLineLocked(
            "summary",
            frame,
            logicalFrame,
            sendFrame,
            lockstepRemoteWaitUs,
            throttleUs,
            networkUs,
            CurrentInputLeadLocked(sendFrame),
            hasRemoteInput,
            predictedRemoteInput);
    }
    if (beforeStart)
        return;
}

void ApplyPacketBridgeJitHelperPatchIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.RuntimePatch.PacketBridgeJitHelperPatchEnabled || !nds || !nds->MainRAM)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    melonDS::u32 patchFrame = G.RuntimePatch.PacketBridgeJitHelperPatchFrame;
    if (G.PacketBridgeJitHelperPatchResumeFrame[instanceID] != 0)
        patchFrame = std::max(patchFrame, G.PacketBridgeJitHelperPatchResumeFrame[instanceID]);
    if (G.PacketBridgeJitHelperPatchApplied[instanceID] || frame < patchFrame)
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
    G.PacketBridgeJitHelperPatchResumeFrame[instanceID] = 0;
    std::printf(
        "NSMB Test: packet bridge JIT keys/touch helper patch inst=%d frame=%u scratch=0x%08X\n",
        instanceID,
        frame,
        kPacketBridgeJitScratchBaseAddr);
}

void ClearMvlCameraInitHoldIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.ClearMvlCameraInitHoldEnabled || !nds || !nds->MainRAM)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (G.ClearMvlCameraInitHoldApplied[instanceID])
        return;
    if (frame < G.ClearMvlCameraInitHoldStartFrame)
        return;
    if (G.ClearMvlCameraInitHoldEndFrame != 0 && frame > G.ClearMvlCameraInitHoldEndFrame)
        return;
    if (G.ClearMvlCameraInitHoldHostOnly && G.NetRole != Role::Host)
        return;
    if (G.ClearMvlCameraInitHoldClientOnly && G.NetRole != Role::Client)
        return;
    if (nds->ARM9Read32(kGameStageGroupAddr) != 9 || nds->ARM9Read32(kGameVsModeAddr) != 1)
        return;

    constexpr melonDS::u32 kMvlCameraModeFlagsAddr = 0x020CA880;
    const melonDS::u8 oldValue = nds->ARM9Read8(kMvlCameraModeFlagsAddr);
    if ((oldValue & 0x08) == 0)
        return;

    const melonDS::u8 newValue = static_cast<melonDS::u8>(oldValue & ~0x08u);
    nds->ARM9Write8(kMvlCameraModeFlagsAddr, newValue);
    G.ClearMvlCameraInitHoldApplied[instanceID] = true;

    std::printf(
        "NSMB Test: clear MvL camera init hold inst=%d frame=%u addr=%08X old=0x%02X value=0x%02X\n",
        instanceID,
        frame,
        kMvlCameraModeFlagsAddr,
        oldValue,
        newValue);
    std::fflush(stdout);
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

bool WriteObjectTransformByBase(
    melonDS::NDS* nds,
    melonDS::u32 base,
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
    if (!nds || !nds->MainRAM || base < kMainRAMBase)
        return false;

    const melonDS::u32 off = base - kMainRAMBase;
    const melonDS::u32 ramLen = nds->MainRAMMask + 1;
    if (off + 0xDC > ramLen)
        return false;

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

void FillWireWorldActorState(const ObjectScanSample& actor, WireWorldActorState& state)
{
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

bool IsWorldActorSnapshotCandidate(const GameStateObjectScanEntry& entry)
{
    if (!entry.Actor.Found || entry.Actor.StateType == 0 || entry.Actor.StateType > 2)
        return false;
    if (entry.Actor.Flags >= 0x10000000)
        return false;

    switch (entry.ObjectID)
    {
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
        break;
    }

    return entry.ObjectID != 0 && entry.ObjectID < 0x0300;
}

struct WorldActorSnapshotCandidate
{
    melonDS::u32 ObjectID = 0;
    ObjectScanSample Actor;
};

std::vector<WorldActorSnapshotCandidate> CollectWorldActorSnapshotCandidates(melonDS::NDS* nds)
{
    std::vector<WorldActorSnapshotCandidate> actors;
    const GameStateObjectScanCache cache = BuildGameStateObjectScanCache(nds);
    actors.reserve(std::min(cache.Entries.size(), kMaxWorldActorSnapshots));
    for (const GameStateObjectScanEntry& entry : cache.Entries)
    {
        if (!IsWorldActorSnapshotCandidate(entry))
            continue;

        WorldActorSnapshotCandidate state {};
        state.ObjectID = entry.ObjectID;
        state.Actor = entry.Actor;
        actors.push_back(state);
    }

    std::sort(actors.begin(), actors.end(), [](const WorldActorSnapshotCandidate& lhs, const WorldActorSnapshotCandidate& rhs) {
        if (lhs.ObjectID != rhs.ObjectID)
            return lhs.ObjectID < rhs.ObjectID;
        if (lhs.Actor.Settings != rhs.Actor.Settings)
            return lhs.Actor.Settings < rhs.Actor.Settings;
        if (lhs.Actor.PosX != rhs.Actor.PosX)
            return lhs.Actor.PosX < rhs.Actor.PosX;
        return lhs.Actor.GUID < rhs.Actor.GUID;
    });
    if (actors.size() > kMaxWorldActorSnapshots)
        actors.resize(kMaxWorldActorSnapshots);
    return actors;
}

bool ApplyWireWorldActorState(
    melonDS::NDS* nds,
    const WireWorldActorState& state,
    melonDS::u32 predictFrames,
    melonDS::u32 localBase)
{
    if (!state.Found)
        return false;

    return WriteObjectTransformByBase(
        nds,
        localBase,
        state.PosX + state.VelX * predictFrames,
        state.PosY + state.VelY * predictFrames,
        state.PosZ + state.VelZ * predictFrames,
        state.PrevX + state.VelX * predictFrames,
        state.PrevY + state.VelY * predictFrames,
        state.PrevZ + state.VelZ * predictFrames,
        state.VelX,
        state.VelY,
        state.VelZ);
}

bool ApplyWireWorldMovingHazardState(
    melonDS::NDS* nds,
    const WireWorldActorState& state,
    melonDS::u32 predictFrames,
    melonDS::u32 localBase)
{
    if (!state.Found)
        return false;

    nds->ARM9Write16(localBase + 0x0E, static_cast<melonDS::u16>(state.StateType));
    nds->ARM9Write32(localBase + 0x10, state.Flags);

    const melonDS::u32 posX = state.PosX + state.VelX * predictFrames;
    const melonDS::u32 posY = state.PosY + state.VelY * predictFrames;
    const melonDS::u32 posZ = state.PosZ + state.VelZ * predictFrames;
    if (!WriteObjectTransformByBase(
        nds,
        localBase,
        posX,
        posY,
        posZ,
        posX,
        posY,
        posZ,
        state.VelX,
        state.VelY,
        0))
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

melonDS::u64 WorldMovingHazardMatchDistance(
    const WireWorldActorState& remoteActor,
    const ObjectScanSample& localActor)
{
    const auto componentDistance = [](melonDS::u32 lhs, melonDS::u32 rhs)
    {
        const std::int64_t delta =
            static_cast<std::int64_t>(static_cast<std::int32_t>(lhs)) -
            static_cast<std::int64_t>(static_cast<std::int32_t>(rhs));
        return static_cast<melonDS::u64>(delta < 0 ? -delta : delta);
    };

    return componentDistance(remoteActor.PosX, localActor.PosX) +
        componentDistance(remoteActor.PosY, localActor.PosY) +
        componentDistance(remoteActor.PosZ, localActor.PosZ);
}

void ApplyRemoteWorldActorSnapshotState(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.Enabled || !G.StateSync.WorldApplyActorSnapshot || G.NetRole != Role::Client || !nds || !nds->MainRAM)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (frame < G.Connection.StartFrame)
        return;

    WireWorldActorSnapshotState sample {};
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        PumpNetworkLocked();
        if (!G.RemoteWorldActorSnapshotSampleValid)
            return;
        sample = G.RemoteWorldActorSnapshotSample;
    }

    const std::vector<WorldActorSnapshotCandidate> localActors = CollectWorldActorSnapshotCandidates(nds);
    const std::size_t remoteCount = std::min(
        static_cast<std::size_t>(sample.Count),
        kMaxWorldActorSnapshots);
    if (remoteCount == 0 || localActors.empty())
        return;

    std::array<int, kMaxWorldActorSnapshots> localIndices {};
    std::array<bool, kMaxWorldActorSnapshots> localUsed {};
    localIndices.fill(-1);
    std::array<melonDS::u32, kMaxWorldActorSnapshots> nextRemoteGUIDs {};
    std::array<melonDS::u32, kMaxWorldActorSnapshots> nextLocalGUIDs {};

    for (std::size_t i = 0; i < remoteCount; i++)
    {
        const WireWorldObjectActorState& remoteActor = sample.Actors[i];
        for (std::size_t mapIndex = 0; mapIndex < kMaxWorldActorSnapshots; mapIndex++)
        {
            if (G.WorldActorSnapshotRemoteGUIDMaps[instanceID][mapIndex] != remoteActor.Actor.GUID)
                continue;

            const melonDS::u32 localGUID = G.WorldActorSnapshotLocalGUIDMaps[instanceID][mapIndex];
            for (std::size_t localIndex = 0; localIndex < localActors.size() && localIndex < kMaxWorldActorSnapshots; localIndex++)
            {
                if (!localUsed[localIndex] &&
                    localActors[localIndex].Actor.GUID == localGUID &&
                    localActors[localIndex].ObjectID == remoteActor.ObjectID &&
                    localActors[localIndex].Actor.Settings == remoteActor.Actor.Settings)
                {
                    localIndices[i] = static_cast<int>(localIndex);
                    localUsed[localIndex] = true;
                    break;
                }
            }
            break;
        }
    }

    for (std::size_t i = 0; i < remoteCount; i++)
    {
        if (localIndices[i] >= 0)
            continue;

        const WireWorldObjectActorState& remoteActor = sample.Actors[i];
        std::size_t closestIndex = localActors.size();
        melonDS::u64 closestDistance = std::numeric_limits<melonDS::u64>::max();
        for (std::size_t localIndex = 0; localIndex < localActors.size() && localIndex < kMaxWorldActorSnapshots; localIndex++)
        {
            if (localUsed[localIndex])
                continue;
            const WorldActorSnapshotCandidate& localActor = localActors[localIndex];
            if (localActor.ObjectID != remoteActor.ObjectID ||
                localActor.Actor.Settings != remoteActor.Actor.Settings ||
                localActor.Actor.StateType == 0 ||
                localActor.Actor.StateType > 2)
                continue;

            const melonDS::u64 distance = WorldMovingHazardMatchDistance(remoteActor.Actor, localActor.Actor);
            if (distance < closestDistance ||
                (distance == closestDistance &&
                    (closestIndex == localActors.size() ||
                        localActor.Actor.GUID < localActors[closestIndex].Actor.GUID)))
            {
                closestIndex = localIndex;
                closestDistance = distance;
            }
        }
        if (closestIndex == localActors.size())
            continue;

        localIndices[i] = static_cast<int>(closestIndex);
        localUsed[closestIndex] = true;
    }

    const melonDS::u32 predictFrames = std::min(
        frame > sample.Frame ? frame - sample.Frame : 0,
        static_cast<melonDS::u32>(std::max(0, G.StateSync.WorldMaxPredictFrames)));
    melonDS::u32 applied = 0;
    bool mapChanged = false;
    for (std::size_t i = 0; i < remoteCount; i++)
    {
        if (localIndices[i] < 0)
            continue;

        const WireWorldObjectActorState& remoteActor = sample.Actors[i];
        const WorldActorSnapshotCandidate& localActor = localActors[localIndices[i]];
        if (ApplyWireWorldActorState(nds, remoteActor.Actor, predictFrames, localActor.Actor.Base))
        {
            applied++;
            nextRemoteGUIDs[i] = remoteActor.Actor.GUID;
            nextLocalGUIDs[i] = localActor.Actor.GUID;
        }
    }

    for (std::size_t i = 0; i < kMaxWorldActorSnapshots; i++)
    {
        mapChanged = mapChanged ||
            G.WorldActorSnapshotRemoteGUIDMaps[instanceID][i] != nextRemoteGUIDs[i] ||
            G.WorldActorSnapshotLocalGUIDMaps[instanceID][i] != nextLocalGUIDs[i];
        G.WorldActorSnapshotRemoteGUIDMaps[instanceID][i] = nextRemoteGUIDs[i];
        G.WorldActorSnapshotLocalGUIDMaps[instanceID][i] = nextLocalGUIDs[i];
    }

    if ((G.Bootstrap.InputTraceEnabled || G.Input.NetplayTrace) &&
        (G.Bootstrap.InputTraceInterval <= 1 || (frame % static_cast<melonDS::u32>(G.Bootstrap.InputTraceInterval)) == 0))
    {
        std::printf("NSMB WorldActors: apply inst=%d frame=%u sampleFrame=%u count=%zu applied=%u predict=%u mapChanged=%d\n",
            instanceID,
            frame,
            sample.Frame,
            remoteCount,
            applied,
            predictFrames,
            mapChanged ? 1 : 0);
    }
}

bool SpawnRemoteWorldItem(
    int instanceID,
    melonDS::u32 frame,
    melonDS::NDS* nds,
    const WireWorldActorState& state)
{
    if (!nds || !nds->MainRAM || !state.Found || (state.StateType != 1 && state.StateType != 2))
        return false;

    WriteARM9U32(nds, kDirectBootTrampolineDataAddr + 0x00, state.PosX);
    WriteARM9U32(nds, kDirectBootTrampolineDataAddr + 0x04, state.PosY);
    WriteARM9U32(nds, kDirectBootTrampolineDataAddr + 0x08, state.PosZ);
    WriteARM9U32(nds, kDirectBootTrampolineDataAddr + 0x0C, 0);
    WriteARM9U32(nds, kDirectBootTrampolineDataAddr + 0x10, 0);

    const melonDS::u32 oldPC = nds->ARM9.R[15] - ((nds->ARM9.CPSR & 0x20) ? 2 : 4);
    const melonDS::u32 returnPC = oldPC | ((nds->ARM9.CPSR & 0x20) ? 1u : 0u);

    std::vector<melonDS::u32> code;
    code.reserve(48);
    EmitARM(code, 0xE92D5FFFu); // push {r0-r12, lr}
    EmitARM(code, 0xE10F5000u); // mrs r5, cpsr
    EmitARM(code, 0xE92D0020u); // push {r5}
    EmitARM(code, 0xE24DD00Cu); // sub sp, sp, #0x0c
    EmitStackArg(code, 0x00, 0);
    EmitStackArg(code, 0x04, 0);
    EmitStackArg(code, 0x08, 0);
    EmitLoadImm(code, 0, kVsWorldItemObjectID);
    EmitLoadImm(code, 1, state.Settings);
    EmitLoadImm(code, 2, kDirectBootTrampolineDataAddr);
    EmitLoadImm(code, 3, kDirectBootTrampolineDataAddr + 0x0C);
    EmitARM(code, 0xE59FC008u); // ldr ip, [pc, #8]
    EmitARM(code, 0xE28FE008u); // add lr, pc, #8
    EmitARM(code, 0xE12FFF1Cu); // bx ip
    EmitARM(code, 0xE1A00000u); // nop
    EmitARM(code, kA2DEActorSpawnActorAddr);
    EmitARM(code, 0xE28DD00Cu); // add sp, sp, #0x0c
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

    std::printf(
        "NSMB WorldItem: spawn inst=%d frame=%u remoteGuid=%u settings=%08X pos=%08X/%08X/%08X trampoline=%08X return=%08X\n",
        instanceID,
        frame,
        state.GUID,
        state.Settings,
        state.PosX,
        state.PosY,
        state.PosZ,
        kDirectBootTrampolineAddr,
        returnPC);
    std::fflush(stdout);
    nds->ARM9.JumpTo(kDirectBootTrampolineAddr);
    return true;
}

struct WorldItemApplyResult
{
    bool Applied = false;
    bool Spawned = false;
};

WorldItemApplyResult ApplyRemoteWorldItemLikeState(
    int instanceID,
    melonDS::u32 frame,
    melonDS::NDS* nds,
    const WireWorldActorState& sample,
    melonDS::u32 predictFrames,
    melonDS::u32& lastSpawnedRemoteGUID,
    melonDS::u32& lastConfirmedRemoteGUID,
    melonDS::u32& pendingRemoteGUID,
    melonDS::u32& pendingFirstMissingFrame,
    const char* label)
{
    WorldItemApplyResult result {};
    if (!sample.Found)
    {
        lastSpawnedRemoteGUID = 0;
        lastConfirmedRemoteGUID = 0;
        pendingRemoteGUID = 0;
        pendingFirstMissingFrame = 0;
        return result;
    }
    if (sample.StateType != 1 && sample.StateType != 2)
        return result;

    if (pendingRemoteGUID != sample.GUID)
    {
        pendingRemoteGUID = sample.GUID;
        pendingFirstMissingFrame = frame;
    }

    ObjectScanSample localItem = FindNewestActiveObjectByIDAndSettings(
        nds,
        kVsWorldItemObjectID,
        sample.Settings,
        true);
    if (localItem.StateType == 1 || localItem.StateType == 2)
    {
        result.Applied = ApplyWireWorldActorState(nds, sample, predictFrames, localItem.Base);
        lastSpawnedRemoteGUID = sample.GUID;
        pendingRemoteGUID = 0;
        pendingFirstMissingFrame = 0;
        if (lastConfirmedRemoteGUID != sample.GUID)
        {
            lastConfirmedRemoteGUID = sample.GUID;
            if (G.StateSync.WorldTraceMovingHazards || G.StateSync.WorldTraceObjectLifecycles ||
                G.Bootstrap.InputTraceEnabled || G.Input.NetplayTrace)
            {
                std::printf(
                    "NSMB %s: active inst=%d frame=%u remoteGuid=%u localGuid=%u settings=%08X pos=%08X/%08X/%08X\n",
                    label,
                    instanceID,
                    frame,
                    sample.GUID,
                    localItem.GUID,
                    localItem.Settings,
                    nds->ARM9Read32(localItem.Base + 0x60),
                    nds->ARM9Read32(localItem.Base + 0x64),
                    nds->ARM9Read32(localItem.Base + 0x68));
                std::fflush(stdout);
            }
        }
        return result;
    }

    if (lastSpawnedRemoteGUID != sample.GUID &&
        frame - pendingFirstMissingFrame >= kVsWorldItemNaturalSpawnGraceFrames)
    {
        result.Spawned = SpawnRemoteWorldItem(instanceID, frame, nds, sample);
        if (result.Spawned)
            lastSpawnedRemoteGUID = sample.GUID;
    }
    return result;
}

void ApplyRemoteWorldState(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.Enabled || !G.StateSync.WorldApplyEnabled) return;
    if (instanceID < 0 || instanceID >= 16 || !nds || !nds->MainRAM) return;
    if (frame < G.Connection.StartFrame) return;
    TraceWorldMovingHazardsIfNeeded(instanceID, frame, nds);
    TraceWorldObjectLifecyclesIfNeeded(instanceID, frame, nds);
    TraceWorldEffectsIfNeeded(instanceID, frame, nds);

    WireWorldState sample {};
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        PumpNetworkLocked();
        if (!G.RemoteWorldStateSampleValid)
            return;
        sample = G.RemoteWorldStateSample;
    }

    const melonDS::u32 predictFrames = std::min(
        frame > sample.Frame ? frame - sample.Frame : 0,
        static_cast<melonDS::u32>(std::max(0, G.StateSync.WorldMaxPredictFrames)));
    const bool applyHostAuthoritativeWorld = G.NetRole == Role::Client;
    bool starApplied = false;
    if (applyHostAuthoritativeWorld)
    {
        const ObjectScanSample star = GetWorldActorCached(
            instanceID,
            frame,
            nds,
            kVsBattleStarActorObjectID,
            kVsBattleStarActorSettings,
            G.WorldStarActorBaseCache,
            G.WorldStarActorGUIDCache);
        starApplied =
            G.StateSync.WorldApplyStarActor &&
            star.StateType == 1 &&
            sample.Star.StateType == 1 &&
            ApplyWireWorldActorState(nds, sample.Star, predictFrames, star.Base);
    }
    WorldItemApplyResult worldItemResult {};
    WorldItemApplyResult neutralWorldItemResult {};
    WorldItemApplyResult droppedStarItemResult {};
    if (G.StateSync.WorldSpawnItem)
    {
        neutralWorldItemResult = ApplyRemoteWorldItemLikeState(
            instanceID,
            frame,
            nds,
            sample.NeutralItem,
            predictFrames,
            G.LastSpawnedNeutralWorldItemRemoteGUID[instanceID],
            G.LastConfirmedNeutralWorldItemRemoteGUID[instanceID],
            G.PendingNeutralWorldItemRemoteGUID[instanceID],
            G.PendingNeutralWorldItemFirstMissingFrame[instanceID],
            "NeutralWorldItem");
        if (applyHostAuthoritativeWorld)
        {
            worldItemResult = ApplyRemoteWorldItemLikeState(
                instanceID,
                frame,
                nds,
                sample.Item,
                predictFrames,
                G.LastSpawnedWorldItemRemoteGUID[instanceID],
                G.LastConfirmedWorldItemRemoteGUID[instanceID],
                G.PendingWorldItemRemoteGUID[instanceID],
                G.PendingWorldItemFirstMissingFrame[instanceID],
                "WorldItem");
        }
        droppedStarItemResult = ApplyRemoteWorldItemLikeState(
            instanceID,
            frame,
            nds,
            sample.DroppedStarItem,
            predictFrames,
            G.LastSpawnedDroppedStarItemRemoteGUID[instanceID],
            G.LastConfirmedDroppedStarItemRemoteGUID[instanceID],
            G.PendingDroppedStarItemRemoteGUID[instanceID],
            G.PendingDroppedStarItemFirstMissingFrame[instanceID],
            "DroppedStarItem");
    }

    if ((G.Bootstrap.InputTraceEnabled || G.Input.NetplayTrace) &&
        (G.Bootstrap.InputTraceInterval <= 1 || (frame % static_cast<melonDS::u32>(G.Bootstrap.InputTraceInterval)) == 0))
    {
        std::printf("NSMB WorldState: apply inst=%d frame=%u sampleFrame=%u predict=%u star=%d neutralItem=%d neutralSpawn=%d item=%d itemSpawn=%d droppedItem=%d droppedSpawn=%d hazard=%d hazardPos=%08X/%08X\n",
            instanceID,
            frame,
            sample.Frame,
            predictFrames,
            starApplied ? 1 : 0,
            neutralWorldItemResult.Applied ? 1 : 0,
            neutralWorldItemResult.Spawned ? 1 : 0,
            worldItemResult.Applied ? 1 : 0,
            worldItemResult.Spawned ? 1 : 0,
            droppedStarItemResult.Applied ? 1 : 0,
            droppedStarItemResult.Spawned ? 1 : 0,
            0,
            sample.MovingHazard.PosX,
            sample.MovingHazard.PosY);
    }
}

void ApplyRemoteMovingHazardState(int instanceID, melonDS::u32 frame, melonDS::NDS* nds, bool preferFreshSample = false)
{
    if (!G.Enabled || !G.StateSync.WorldApplyMovingHazard || G.NetRole != Role::Client) return;
    if (instanceID < 0 || instanceID >= 16 || !nds || !nds->MainRAM) return;
    if (frame < G.Connection.StartFrame) return;

    WireMovingHazardState sample {};
    bool sampleValid = false;
    const auto waitDeadline = std::chrono::steady_clock::now() + std::chrono::microseconds(1500);
    {
        while (true)
        {
            {
                std::lock_guard<std::mutex> lock(G.Mutex);
                PumpNetworkLocked();
                if (G.RemoteMovingHazardStateSampleValid)
                {
                    sample = G.RemoteMovingHazardStateSample;
                    sampleValid = true;
                }
            }
            if (!preferFreshSample || sample.Frame >= frame || std::chrono::steady_clock::now() >= waitDeadline)
                break;
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        if (!sampleValid || (preferFreshSample && sample.Frame < frame))
            return;
    }

    const std::vector<ObjectScanSample> localActors = GetWorldMovingHazardsCached(instanceID, frame, nds);
    const std::size_t remoteCount = std::min(
        static_cast<std::size_t>(sample.Count),
        kMaxWorldMovingHazards);
    if (localActors.size() != remoteCount)
        return;
    const std::size_t pairCount = remoteCount;
    const melonDS::u32 predictFrames = std::min(
        frame > sample.Frame ? frame - sample.Frame : 0,
        static_cast<melonDS::u32>(std::max(0, G.StateSync.WorldMaxPredictFrames)));
    int localIndices[kMaxWorldMovingHazards] { -1, -1, -1, -1 };
    bool localUsed[kMaxWorldMovingHazards] {};
    melonDS::u32 nextRemoteGUIDs[kMaxWorldMovingHazards] {};
    melonDS::u32 nextLocalGUIDs[kMaxWorldMovingHazards] {};

    for (std::size_t i = 0; i < pairCount; i++)
    {
        const WireWorldActorState& remoteActor = sample.Actors[i];
        for (std::size_t mapIndex = 0; mapIndex < kMaxWorldMovingHazards; mapIndex++)
        {
            if (G.WorldMovingHazardRemoteGUIDMaps[instanceID][mapIndex] != remoteActor.GUID)
                continue;

            const melonDS::u32 localGUID = G.WorldMovingHazardLocalGUIDMaps[instanceID][mapIndex];
            for (std::size_t localIndex = 0; localIndex < localActors.size(); localIndex++)
            {
                if (!localUsed[localIndex] && localActors[localIndex].GUID == localGUID)
                {
                    localIndices[i] = static_cast<int>(localIndex);
                    localUsed[localIndex] = true;
                    break;
                }
            }
            break;
        }
    }

    for (std::size_t i = 0; i < pairCount; i++)
    {
        if (localIndices[i] >= 0)
            continue;

        const WireWorldActorState& remoteActor = sample.Actors[i];
        std::size_t closestIndex = localActors.size();
        melonDS::u64 closestDistance = std::numeric_limits<melonDS::u64>::max();
        for (std::size_t localIndex = 0; localIndex < localActors.size(); localIndex++)
        {
            if (localUsed[localIndex])
                continue;

            const melonDS::u64 distance =
                WorldMovingHazardMatchDistance(remoteActor, localActors[localIndex]);
            if (distance < closestDistance ||
                (distance == closestDistance &&
                    (closestIndex == localActors.size() ||
                        localActors[localIndex].GUID < localActors[closestIndex].GUID)))
            {
                closestIndex = localIndex;
                closestDistance = distance;
            }
        }
        if (closestIndex == localActors.size())
            return;
        localIndices[i] = static_cast<int>(closestIndex);
        localUsed[closestIndex] = true;
    }

    bool mapChanged = false;
    for (std::size_t i = 0; i < pairCount; i++)
    {
        const ObjectScanSample& localActor = localActors[localIndices[i]];
        const WireWorldActorState& remoteActor = sample.Actors[i];
        nextRemoteGUIDs[i] = remoteActor.GUID;
        nextLocalGUIDs[i] = localActor.GUID;
        mapChanged = mapChanged ||
            G.WorldMovingHazardRemoteGUIDMaps[instanceID][i] != nextRemoteGUIDs[i] ||
            G.WorldMovingHazardLocalGUIDMaps[instanceID][i] != nextLocalGUIDs[i];
        ApplyWireWorldMovingHazardState(nds, remoteActor, predictFrames, localActor.Base);
    }

    for (std::size_t i = 0; i < kMaxWorldMovingHazards; i++)
    {
        mapChanged = mapChanged ||
            G.WorldMovingHazardRemoteGUIDMaps[instanceID][i] != nextRemoteGUIDs[i] ||
            G.WorldMovingHazardLocalGUIDMaps[instanceID][i] != nextLocalGUIDs[i];
        G.WorldMovingHazardRemoteGUIDMaps[instanceID][i] = nextRemoteGUIDs[i];
        G.WorldMovingHazardLocalGUIDMaps[instanceID][i] = nextLocalGUIDs[i];
    }
    if (mapChanged && G.StateSync.WorldTraceMovingHazards)
    {
        std::printf("NSMB WorldHazards: map inst=%d frame=%u count=%zu",
            instanceID,
            frame,
            pairCount);
        for (std::size_t i = 0; i < pairCount; i++)
            std::printf(" slot%zu=%u/%u", i, nextRemoteGUIDs[i], nextLocalGUIDs[i]);
        std::printf("\n");
    }
}

void ApplyRemoteWorldEffectState(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.Enabled || !G.StateSync.WorldApplyEffects || G.NetRole != Role::Client || !nds || !nds->MainRAM)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (frame < G.Connection.StartFrame)
        return;

    WireWorldEffectState sample {};
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        PumpNetworkLocked();
        if (!G.RemoteWorldEffectStateSampleValid)
            return;
        sample = G.RemoteWorldEffectStateSample;
    }

    for (std::size_t i = 0; i < std::min<std::size_t>(sample.Count, kMaxWorldEffects); i++)
    {
        const WireWorldEffectSlot& remote = sample.Effects[i];
        if (!remote.Found || !IsValidMainRAMRange(nds, remote.Base, kWorldEffectWordEnd + sizeof(melonDS::u32)))
            continue;

        melonDS::u32 localVTable = 0;
        if (!ReadMainRAMAddressU32(nds, remote.Base, localVTable) ||
            (localVTable != kEffectVTablePtr && localVTable != kEffectVTableStart))
            continue;

        for (std::size_t wordIndex = 0; wordIndex < kWorldEffectWordCount; wordIndex++)
        {
            const melonDS::u32 relativeOffset =
                kWorldEffectWordStart + static_cast<melonDS::u32>(wordIndex * sizeof(melonDS::u32));
            WriteARM9U32(nds, remote.Base + relativeOffset, remote.Words[wordIndex]);
        }
    }
}

bool WritePlayerRuntimeStateByBase(melonDS::NDS* nds, melonDS::u32 base, const WirePlayerState& state)
{
    if (!nds || !nds->MainRAM || base < kMainRAMBase)
        return false;

    const melonDS::u32 off = base - kMainRAMBase;
    const melonDS::u32 ramLen = nds->MainRAMMask + 1;
    if (off + kPlayerBaseVisibleFlagOffset + 1 > ramLen)
        return false;

    nds->ARM9Write32(base + kPlayerBaseActionFlagOffset, state.ActionFlag);
    nds->ARM9Write32(base + kPlayerBaseSubActionFlagOffset, state.SubActionFlag);
    nds->ARM9Write32(base + kPlayerBasePhysicsFlagOffset, state.PhysicsFlag);
    nds->ARM9Write32(base + kPlayerBaseTransitionFlagOffset, state.TransitionFlag);
    nds->ARM9Write32(base + kPlayerBaseCollisionFlagOffset, state.CollisionFlag);
    nds->ARM9Write32(base + kPlayerBaseEnvironmentFlagOffset, state.EnvironmentFlag);
    nds->ARM9Write16(base + kPlayerBaseDamageCooldownOffset, static_cast<melonDS::u16>(state.DamageCooldown & 0xFFFFu));
    nds->ARM9Write8(base + kPlayerBaseUpdateLockedOffset, static_cast<melonDS::u8>(state.RuntimeFlags0 & 0xFFu));
    nds->ARM9Write8(base + kPlayerBaseCharacterIDOffset, static_cast<melonDS::u8>((state.RuntimeFlags0 >> 8) & 0xFFu));
    nds->ARM9Write8(base + kPlayerBaseTransitioningFlagOffset, static_cast<melonDS::u8>((state.RuntimeFlags0 >> 16) & 0xFFu));
    nds->ARM9Write8(base + kPlayerBaseCameraFocusModeOffset, static_cast<melonDS::u8>((state.RuntimeFlags0 >> 24) & 0xFFu));
    nds->ARM9Write8(base + kPlayerBaseDefeatedFlagOffset, static_cast<melonDS::u8>(state.RuntimeFlags1 & 0xFFu));
    nds->ARM9Write8(base + kPlayerBasePlayerIDOffset, static_cast<melonDS::u8>((state.RuntimeFlags1 >> 8) & 0xFFu));
    nds->ARM9Write8(base + kPlayerBaseVisibleFlagOffset, static_cast<melonDS::u8>((state.RuntimeFlags1 >> 16) & 0xFFu));
    return true;
}

bool WritePlayerMinimalTransitionStateByBase(melonDS::NDS* nds, melonDS::u32 base, const WirePlayerState& state)
{
    if (!nds || !nds->MainRAM || base < kMainRAMBase)
        return false;

    bool ok = true;
    ok = WriteMainRAMAddrU8IfChanged(
        nds,
        base + kPlayerBaseDefeatedFlagOffset,
        static_cast<melonDS::u8>(state.RuntimeFlags1 & 0xFFu)) && ok;
    ok = WriteMainRAMAddrU8IfChanged(
        nds,
        base + kPlayerBaseVisibleFlagOffset,
        static_cast<melonDS::u8>((state.RuntimeFlags1 >> 16) & 0xFFu)) && ok;
    return ok;
}

bool IsPlayerInActorTransition(melonDS::NDS* nds, melonDS::u32 base)
{
    if (!nds || !nds->MainRAM || base < kMainRAMBase)
        return false;

    const melonDS::u32 off = base - kMainRAMBase;
    const melonDS::u32 ramLen = nds->MainRAMMask + 1;
    if (off + kPlayerBaseTransitionStepOffset + 1 > ramLen)
        return false;

    const melonDS::u8 transitionStep = nds->ARM9Read8(base + kPlayerBaseTransitionStepOffset);
    return transitionStep != 1;
}

void ReadPlayerGlobalState(melonDS::NDS* nds, melonDS::u32 player, WirePlayerState& state)
{
    if (!nds || !nds->MainRAM || player > 1)
        return;

    state.PlayerCount = nds->ARM9Read32(kGamePlayerCountAddr);
    state.Powerup = nds->ARM9Read8(kGamePlayerPowerupAddr + player);
    state.InventoryPowerup = nds->ARM9Read8(kGamePlayerInventoryPowerupAddr + player);
    state.Dead = nds->ARM9Read8(kGamePlayerDeadAddr + player);
    state.Character = nds->ARM9Read8(kGamePlayerCharacterAddr + player);
    state.TransitionStatus = nds->ARM9Read32(kGamePlayerTransitionStatusAddr + sizeof(melonDS::u32) * player);
    state.Lives = nds->ARM9Read32(kGamePlayerLivesAddr + sizeof(melonDS::u32) * player);
    state.BattleStars = nds->ARM9Read32(kGamePlayerBattleStarsAddr + sizeof(melonDS::u32) * player);
    state.Coins = nds->ARM9Read32(kGamePlayerCoinsAddr + sizeof(melonDS::u32) * player);
    state.Score = nds->ARM9Read32(kGamePlayerScoreAddr + sizeof(melonDS::u32) * player);
    state.DisplayedStars = nds->ARM9Read32(kGamePlayerDisplayedStarsAddr + sizeof(melonDS::u32) * player);
    state.Deaths = nds->ARM9Read32(kGamePlayerDeathsAddr + sizeof(melonDS::u32) * player);
    state.CollectedStars = nds->ARM9Read32(kGamePlayerCollectedStarsAddr + sizeof(melonDS::u32) * player);
}

bool WritePlayerGlobalState(melonDS::NDS* nds, const WirePlayerState& state)
{
    if (!nds || !nds->MainRAM || state.Player > 1)
        return false;

    const melonDS::u32 player = state.Player;
    bool ok = true;

    melonDS::u32 currentDeaths = 0;
    melonDS::u8 currentDead = 0;
    const melonDS::u32 deathsAddr = kGamePlayerDeathsAddr + sizeof(melonDS::u32) * player;
    const melonDS::u32 deadAddr = kGamePlayerDeadAddr + player;
    melonDS::u32 deathsOff = 0;
    melonDS::u32 deadOff = 0;
    if (MainRAMOffsetFromAddr(nds, deathsAddr, sizeof(currentDeaths), deathsOff))
        ReadMainRAMU32(nds, deathsOff, currentDeaths);
    if (MainRAMOffsetFromAddr(nds, deadAddr, sizeof(currentDead), deadOff))
        ReadMainRAMU8(nds, deadOff, currentDead);

    const bool deathEvent =
        (state.Dead != 0) ||
        (currentDead != 0) ||
        (state.Deaths != currentDeaths);
    const bool starEvent =
        state.BattleStars != 0 ||
        state.DisplayedStars != 0 ||
        state.CollectedStars != 0;

    if (state.Lives != 0)
        ok = WriteMainRAMAddrU32IfChanged(nds, kGamePlayerLivesAddr + sizeof(melonDS::u32) * player, state.Lives) && ok;
    ok = WriteMainRAMAddrU32IfChanged(nds, kGamePlayerCoinsAddr + sizeof(melonDS::u32) * player, state.Coins) && ok;
    if (deathEvent)
    {
        ok = WriteMainRAMAddrU8IfChanged(nds, deadAddr, static_cast<melonDS::u8>(state.Dead & 0xFFu)) && ok;
        ok = WriteMainRAMAddrU32IfChanged(nds, deathsAddr, state.Deaths) && ok;
        ok = WriteMainRAMAddrU32IfChanged(nds, kGamePlayerTransitionStatusAddr + sizeof(melonDS::u32) * player, state.TransitionStatus) && ok;
    }
    if (starEvent)
    {
        ok = WriteMainRAMAddrU32IfChanged(nds, kGamePlayerBattleStarsAddr + sizeof(melonDS::u32) * player, state.BattleStars) && ok;
        ok = WriteMainRAMAddrU32IfChanged(nds, kGamePlayerDisplayedStarsAddr + sizeof(melonDS::u32) * player, state.DisplayedStars) && ok;
        ok = WriteMainRAMAddrU32IfChanged(nds, kGamePlayerCollectedStarsAddr + sizeof(melonDS::u32) * player, state.CollectedStars) && ok;
    }
    return ok;
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

bool FindLatestRemotePlayerStateLocked(melonDS::u32 player, melonDS::u32 frame, WirePlayerState& sample, melonDS::u32& sampleFrame)
{
    bool found = false;
    melonDS::u32 bestFrame = 0;
    const melonDS::u64 endKey = PlayerStateKey(player + 1, 0);
    for (auto it = G.RemotePlayerStateSamples.lower_bound(PlayerStateKey(player, 0));
         it != G.RemotePlayerStateSamples.end() && it->first < endKey;
         ++it)
    {
        const WirePlayerState& value = it->second;
        if (value.Frame > frame)
            continue;
        if (found && value.Frame <= bestFrame)
            continue;
        found = true;
        bestFrame = value.Frame;
        sample = value;
    }

    sampleFrame = bestFrame;
    return found;
}

void ApplyRemotePlayerState(int instanceID, melonDS::u32 frame, melonDS::NDS* nds, bool preferFreshSample = false)
{
    if (!G.Enabled || !G.StateSync.PlayerApplyEnabled) return;
    if (instanceID < 0 || instanceID >= 16 || !nds || !nds->MainRAM) return;
    if (frame < G.Connection.StartFrame) return;

    const int remotePlayer = CurrentPacketBridgeLocalPlayer() ^ 1;
    WirePlayerState sample {};
    melonDS::u32 sampleFrame = 0;
    bool sampleValid = false;
    const auto waitDeadline = std::chrono::steady_clock::now() + std::chrono::microseconds(500);
    {
        while (true)
        {
            {
                std::lock_guard<std::mutex> lock(G.Mutex);
                PumpNetworkLocked();
                sampleValid = FindLatestRemotePlayerStateLocked(static_cast<melonDS::u32>(remotePlayer), frame, sample, sampleFrame);
            }
            if (!preferFreshSample || (sampleValid && sampleFrame >= frame) || std::chrono::steady_clock::now() >= waitDeadline)
                break;
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        if (!sampleValid || (preferFreshSample && sampleFrame < frame))
            return;
    }
    const bool shouldApplyGlobals =
        G.StateSync.PlayerGlobalsEnabled &&
        sampleFrame > G.LastAppliedPlayerGlobalsFrame[instanceID][remotePlayer];
    const bool globalsApplied = shouldApplyGlobals && WritePlayerGlobalState(nds, sample);
    if (globalsApplied)
        G.LastAppliedPlayerGlobalsFrame[instanceID][remotePlayer] = sampleFrame;
    if (!sample.Found)
    {
        if ((G.Bootstrap.InputTraceEnabled || G.Input.NetplayTrace) &&
            (G.Bootstrap.InputTraceInterval <= 1 || (frame % static_cast<melonDS::u32>(G.Bootstrap.InputTraceInterval)) == 0))
        {
            std::printf("NSMB PlayerState: apply globals-only inst=%d frame=%u sampleFrame=%u remotePlayer=%d globals=%d lives=%u deaths=%u dead=%u stars=%u\n",
                instanceID,
                frame,
                sampleFrame,
                remotePlayer,
                globalsApplied ? 1 : 0,
                sample.Lives,
                sample.Deaths,
                sample.Dead,
                sample.BattleStars);
        }
        return;
    }

    const ObjectScanSample localActor = GetPlayerActorCached(instanceID, remotePlayer, nds);
    const melonDS::u32 localBase = localActor.Found ? localActor.Base : 0;
    const bool transitionMinimalApply =
        G.StateSync.PlayerGlobalsEnabled &&
        localActor.Found &&
        IsPlayerInActorTransition(nds, localBase);
    const melonDS::u32 predictFrames = std::min(
        frame - sampleFrame,
        static_cast<melonDS::u32>(std::max(0, G.StateSync.PlayerMaxPredictFrames)));
    const bool transformApplied = !transitionMinimalApply && WriteObjectTransformByBase(
            nds,
            localBase,
            sample.PosX + sample.VelX * predictFrames,
            sample.PosY + sample.VelY * predictFrames,
            sample.PosZ + sample.VelZ * predictFrames,
            sample.PrevX + sample.VelX * predictFrames,
            sample.PrevY + sample.VelY * predictFrames,
            sample.PrevZ + sample.VelZ * predictFrames,
            sample.VelX,
            sample.VelY,
            sample.VelZ);
    const bool runtimeApplied = transitionMinimalApply
        ? WritePlayerMinimalTransitionStateByBase(nds, localBase, sample)
        : (transformApplied && WritePlayerRuntimeStateByBase(nds, localBase, sample));

    if ((G.Bootstrap.InputTraceEnabled || G.Input.NetplayTrace) &&
        (G.Bootstrap.InputTraceInterval <= 1 || (frame % static_cast<melonDS::u32>(G.Bootstrap.InputTraceInterval)) == 0))
    {
        std::printf("NSMB PlayerState: apply inst=%d frame=%u sampleFrame=%u remotePlayer=%d predict=%u transitionMin=%d transform=%d runtime=%d globals=%d pos=%08X/%08X vel=%08X/%08X lives=%u deaths=%u dead=%u stars=%u\n",
            instanceID,
            frame,
            sampleFrame,
            remotePlayer,
            predictFrames,
            transitionMinimalApply ? 1 : 0,
            transformApplied ? 1 : 0,
            runtimeApplied ? 1 : 0,
            globalsApplied ? 1 : 0,
            sample.PosX,
            sample.PosY,
            sample.VelX,
            sample.VelY,
            sample.Lives,
            sample.Deaths,
            sample.Dead,
            sample.BattleStars);
    }
}

void ApplyRemoteGameState(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.Enabled || !G.StateSync.GameApplyEnabled) return;
    if (!G.StateSync.GameApplyRemotePlayerOnly && G.NetRole != Role::Client) return;
    if (instanceID < 0 || instanceID >= 16 || !nds || !nds->MainRAM) return;
    if (frame < G.Connection.StartFrame) return;

    GameStateSample sample;
    melonDS::u32 sampleFrame = 0;
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        PumpNetworkLocked();
        if (!FindLatestRemoteGameStateLocked(instanceID, frame, sample, sampleFrame))
            return;
    }

    if (G.StateSync.GameApplyRemotePlayerOnly)
    {
        const int remotePlayer = CurrentPacketBridgeLocalPlayer() ^ 1;
        const PlayerActorScanSample localPlayers = FindPlayerActors(nds);
        bool applied = false;
        if (remotePlayer == 0 && sample.PlayerActor0Found)
        {
            const melonDS::u32 localBase = localPlayers.Actor0.Found ? localPlayers.Actor0.Base : 0;
            applied = WriteObjectTransformByBase(
                nds,
                localBase,
                sample.PlayerActor0PosX,
                sample.PlayerActor0PosY,
                sample.PlayerActor0PosZ,
                sample.PlayerActor0PrevX,
                sample.PlayerActor0PrevY,
                sample.PlayerActor0PrevZ,
                sample.PlayerActor0VelX,
                sample.PlayerActor0VelY,
                sample.PlayerActor0VelZ);
        }
        else if (remotePlayer == 1 && sample.PlayerActor1Found)
        {
            const melonDS::u32 localBase = localPlayers.Actor1.Found ? localPlayers.Actor1.Base : 0;
            applied = WriteObjectTransformByBase(
                nds,
                localBase,
                sample.PlayerActor1PosX,
                sample.PlayerActor1PosY,
                sample.PlayerActor1PosZ,
                sample.PlayerActor1PrevX,
                sample.PlayerActor1PrevY,
                sample.PlayerActor1PrevZ,
                sample.PlayerActor1VelX,
                sample.PlayerActor1VelY,
                sample.PlayerActor1VelZ);
        }

        if ((G.Bootstrap.InputTraceEnabled || G.Input.NetplayTrace) &&
            (G.Bootstrap.InputTraceInterval <= 1 || (frame % static_cast<melonDS::u32>(G.Bootstrap.InputTraceInterval)) == 0))
        {
            std::printf("NSMB PoC: applied remote-player snapshot inst=%d frame=%u sampleFrame=%u remotePlayer=%d applied=%d\n",
                instanceID,
                frame,
                sampleFrame,
                remotePlayer,
                applied ? 1 : 0);
        }
        return;
    }

    if (G.StateSync.GameApplyCriticalGlobals)
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

    if (G.StateSync.GameApplyStageObjects && sample.StageCameraFound)
    {
        WriteObjectWordByIDAndSettings(nds, kStageCameraObjectID, 0, 0x190, sample.StageCameraWord190);
        WriteObjectWordByIDAndSettings(nds, kStageCameraObjectID, 0, 0x194, sample.StageCameraWord194);
        WriteObjectWordByIDAndSettings(nds, kStageCameraObjectID, 0, 0x19C, sample.StageCameraWord19C);
        WriteObjectWordByIDAndSettings(nds, kStageCameraObjectID, 0, 0x1A0, sample.StageCameraWord1A0);
    }
    if (G.StateSync.GameApplyStageObjects && sample.StageSceneFound)
    {
        WriteObjectWordByIDAndSettings(nds, kStageSceneObjectID, G.MvlCurrentStageSceneSettings, 0x154, sample.StageSceneWord154);
        WriteObjectWordByIDAndSettings(nds, kStageSceneObjectID, G.MvlCurrentStageSceneSettings, 0x160, sample.StageSceneWord160);
    }
    if (G.StateSync.GameApplyStageObjects && sample.MovingHazardFound)
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
    if (G.StateSync.GameApplyStarObjects && sample.VsStarFound)
    {
        if (!WriteObjectPositionByGUID(nds, sample.VsStarGUID, sample.VsStarPosX, sample.VsStarPosY, sample.VsStarPosZ))
            WriteVsBattleStarCandidatePosition(nds, sample.VsStarPosX, sample.VsStarPosY, sample.VsStarPosZ);
    }
    else if (G.StateSync.GameApplyStarObjects)
    {
        WriteVsBattleStarCandidatePosition(nds, 0, 0, 0);
    }
    if (G.StateSync.GameApplyStarObjects && sample.VsStarActorFound)
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
    else if (G.StateSync.GameApplyStarObjects)
    {
        WriteObjectPositionByIDAndSettings(nds, kVsBattleStarActorObjectID, kVsBattleStarActorSettings, 0, 0, 0);
    }
    if (G.StateSync.GameApplyPlayerActors && sample.PlayerActor0Found)
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
    if (G.StateSync.GameApplyPlayerActors && sample.PlayerActor1Found)
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

    if (G.Bootstrap.InputTraceEnabled &&
        (G.Bootstrap.InputTraceInterval <= 1 || (frame % static_cast<melonDS::u32>(G.Bootstrap.InputTraceInterval)) == 0))
    {
        std::printf("NSMB PoC: applied remote game state inst=%d frame=%u sampleFrame=%u\n",
            instanceID,
            frame,
            sampleFrame);
    }
}

PlayerCollisionMgrSample ReadPlayerCollisionMgrSample(melonDS::NDS* nds, const ObjectScanSample& actor)
{
    PlayerCollisionMgrSample sample;
    if (!nds || !actor.Found || !IsValidMainRAMRange(
            nds,
            actor.Base + kPlayerStageActorCollisionMgrOffset,
            0xB8))
        return sample;

    const melonDS::u32 base = actor.Base + kPlayerStageActorCollisionMgrOffset;
    sample.Found = 1;
    sample.Base = base;
    sample.DeltaX = nds->ARM9Read32(base + kCollisionMgrDeltaXOffset);
    sample.DeltaY = nds->ARM9Read32(base + kCollisionMgrDeltaYOffset);
    sample.CollisionResult = nds->ARM9Read32(base + kCollisionMgrCollisionResultOffset);
    sample.GroundCollision = nds->ARM9Read32(base + kCollisionMgrGroundCollisionOffset);
    sample.AttachedTileX = nds->ARM9Read16(base + kCollisionMgrAttachedTileXOffset);
    sample.AttachedTileY = nds->ARM9Read16(base + kCollisionMgrAttachedTileYOffset);
    sample.BottomModifierTileType = nds->ARM9Read16(base + kCollisionMgrBottomModifierTileTypeOffset);
    sample.BottomSlopeType = nds->ARM9Read8(base + kCollisionMgrBottomSlopeTypeOffset);
    sample.TopModifierTileType = nds->ARM9Read16(base + kCollisionMgrTopModifierTileTypeOffset);
    sample.TopSlopeType = nds->ARM9Read8(base + kCollisionMgrTopSlopeTypeOffset);
    sample.SideModifierTileTypeLeft = nds->ARM9Read16(base + kCollisionMgrSideModifierTileTypeOffset);
    sample.SideModifierTileTypeRight = nds->ARM9Read16(base + kCollisionMgrSideModifierTileTypeOffset + 2);
    sample.ByteA4 = nds->ARM9Read8(base + kCollisionMgrByteA4Offset);
    sample.ByteA5 = nds->ARM9Read8(base + kCollisionMgrByteA5Offset);
    sample.PreviousByteA4 = nds->ARM9Read8(base + kCollisionMgrPreviousByteA4Offset);
    sample.PreviousByteA5 = nds->ARM9Read8(base + kCollisionMgrPreviousByteA5Offset);
    sample.FlagsA8 = nds->ARM9Read8(base + kCollisionMgrFlagsA8Offset);
    sample.TileByteAB = nds->ARM9Read8(base + kCollisionMgrTileByteABOffset);
    sample.ModifierState = nds->ARM9Read8(base + kCollisionMgrModifierStateOffset);
    sample.UnknownB1 = nds->ARM9Read8(base + kCollisionMgrUnknownB1Offset);
    auto readSensor = [nds](melonDS::u32 sensorBase) {
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
    sample.BottomSensor = readSensor(nds->ARM9Read32(base + kCollisionMgrBottomSensorPtrOffset));
    sample.TopSensor = readSensor(nds->ARM9Read32(base + kCollisionMgrTopSensorPtrOffset));
    sample.SideSensor = readSensor(nds->ARM9Read32(base + kCollisionMgrSideSensorPtrOffset));
    sample.LineSensor = readSensor(nds->ARM9Read32(base + kCollisionMgrLineSensorPtrOffset));
    return sample;
}

PlayerHitboxSample ReadPlayerHitboxSample(melonDS::NDS* nds, const ObjectScanSample& actor)
{
    PlayerHitboxSample sample;
    if (!nds || !actor.Found ||
        !IsValidMainRAMRange(nds, actor.Base + kPlayerHitboxCenterOffsetX, 8) ||
        !IsValidMainRAMRange(nds, actor.Base + kStageActorHitboxHalfWidthOffset, 8))
        return sample;

    sample.Found = 1;
    sample.CenterOffsetX = nds->ARM9Read32(actor.Base + kPlayerHitboxCenterOffsetX);
    sample.CenterOffsetY = nds->ARM9Read32(actor.Base + kPlayerHitboxCenterOffsetY);
    sample.HalfWidth = nds->ARM9Read32(actor.Base + kStageActorHitboxHalfWidthOffset);
    sample.HalfHeight = nds->ARM9Read32(actor.Base + kStageActorHitboxHalfHeightOffset);
    return sample;
}

std::int32_t SignedARM9U32(melonDS::u32 value)
{
    return static_cast<std::int32_t>(value);
}

bool ReadStageLayoutTileBehavior(
    melonDS::NDS* nds,
    melonDS::u32 worldX,
    melonDS::u32 worldY,
    melonDS::u32 playerID,
    AITileProbeSample& out)
{
    out.WorldX = worldX;
    out.WorldY = worldY;
    if (!nds || !nds->MainRAM)
    {
        out.Status = 1;
        return false;
    }

    const melonDS::u32 stageLayout = nds->ARM9Read32(kStageLayoutPtrAddr);
    const melonDS::u32 wrapX = nds->ARM9Read32(kGameWrapXAddr);
    out.StageLayout = stageLayout;
    if (!IsValidMainRAMRange(nds, stageLayout, kStageLayoutCameraWrapAddOffset + sizeof(melonDS::u16)))
    {
        out.Status = 2;
        return false;
    }

    std::int32_t pixelXSigned = SignedARM9U32(worldX) >> 12;
    const std::int32_t pixelYSigned = (-SignedARM9U32(worldY)) >> 12;
    if (wrapX != 0)
        pixelXSigned &= static_cast<std::int32_t>(wrapX >> 12);
    if (pixelYSigned < 0)
    {
        out.Status = 3;
        return false;
    }

    melonDS::u32 pixelX = static_cast<melonDS::u32>(pixelXSigned) & 0xFFFFu;
    melonDS::u32 pixelY = static_cast<melonDS::u32>(pixelYSigned) & 0xFFFFu;

    const melonDS::u32 playerOffset =
        kStageLayoutPlayerDataOffset +
        (playerID & 1u) * kStageLayoutPlayerDataStride +
        kStageLayoutPlayerCameraWrapOffset;
    if (nds->ARM9Read16(stageLayout + playerOffset) == 0xFF00)
        pixelX = (pixelX + nds->ARM9Read16(stageLayout + kStageLayoutCameraWrapAddOffset)) & 0xFFFFu;

    const melonDS::u32 stageBlocks = nds->ARM9Read32(kStageBlocksPtrAddr);
    if (IsValidMainRAMRange(nds, stageBlocks, 4) && (nds->ARM9Read16(stageBlocks + 2) & 0x20) != 0)
        pixelX &= nds->ARM9Read16(stageLayout + kStageLayoutWrapMaskOffset);

    out.PixelX = pixelX;
    out.PixelY = pixelY;
    if (pixelX >= 0x2000 || pixelY >= 0x1000)
    {
        out.Status = 4;
        return false;
    }

    const melonDS::u32 chunkIndex = (pixelX >> 8) + ((pixelY >> 8) << 5);
    const melonDS::u32 chunkID = nds->ARM9Read8(stageLayout + kStageLayoutChunkMapOffset + chunkIndex);
    out.ChunkID = chunkID;
    const melonDS::u32 chunkPtr = nds->ARM9Read32(kStageLayoutChunkPtrTableAddr + chunkID * sizeof(melonDS::u32));
    out.ChunkPtr = chunkPtr;
    if (!IsValidMainRAMRange(nds, chunkPtr, 0x200))
    {
        out.Status = 5;
        return false;
    }

    const melonDS::u32 tileOffset = (((pixelX & 0xF0u) >> 4) << 1) + ((pixelY & 0xF0u) << 1);
    const melonDS::u32 tileID = nds->ARM9Read16(chunkPtr + tileOffset);
    out.TileID = tileID;
    melonDS::u32 behavior = 0;
    melonDS::u32 behaviorTable = 0;
    if (tileID < 0x100)
    {
        behaviorTable = kStageLayoutTileBehaviorBaseTableAddr;
        out.BehaviorTable = behaviorTable;
        behavior = nds->ARM9Read32(behaviorTable + tileID * sizeof(melonDS::u32));
    }
    else if (tileID < 0x600)
    {
        behaviorTable = nds->ARM9Read32(kStageLayoutDynamicTileBehaviorTablePtrAddr);
        out.BehaviorTable = behaviorTable;
        const melonDS::u32 behaviorOffset = (tileID - 0x100) * sizeof(melonDS::u32);
        if (!IsValidMainRAMRange(nds, behaviorTable + behaviorOffset, sizeof(melonDS::u32)))
        {
            out.Status = 6;
            return false;
        }
        behavior = nds->ARM9Read32(behaviorTable + behaviorOffset);
    }
    else
    {
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

AIPlayerTileProbeSample ReadAIPlayerTileProbeSample(melonDS::NDS* nds, const ObjectScanSample& actor)
{
    AIPlayerTileProbeSample probe;
    static constexpr struct ProbeDef
    {
        const char* Name;
        int X;
        int Y;
        bool Directional;
    } kProbeDefs[kAITileProbeCount] = {
        {"center", 0, 0, false},
        {"feet", 0, -24, false},
        {"below", 0, -48, false},
        {"aheadBody", 16, 0, true},
        {"aheadFeet", 16, -24, true},
        {"aheadBelow", 16, -48, true},
        {"ahead2Feet", 32, -24, true},
        {"ahead2Below", 32, -48, true},
        {"above", 0, 24, false},
        {"leftBody", -16, 0, false},
        {"leftFeet", -16, -24, false},
        {"leftBelow", -16, -48, false},
        {"left2Below", -32, -48, false},
        {"rightBody", 16, 0, false},
        {"rightFeet", 16, -24, false},
        {"rightBelow", 16, -48, false},
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
    probe.Found = IsValidMainRAMRange(nds, probe.StageLayout, kStageLayoutCameraWrapAddOffset + sizeof(melonDS::u16)) ? 1 : 0;
    if (!probe.Found)
        return probe;

    const melonDS::u32 playerID = nds->ARM9Read8(actor.Base + kPlayerBasePlayerIDOffset) & 1u;
    const std::int32_t actorPixelX = SignedARM9U32(actor.PosX) >> 12;
    const std::int32_t actorPixelY = (-SignedARM9U32(actor.PosY)) >> 12;
    const std::int32_t anchorTileX = actorPixelX >> 4;
    const std::int32_t anchorTileY = actorPixelY >> 4;
    for (int i = 0; i < kAITileProbeCount; i++)
    {
        const int offsetX = kProbeDefs[i].Directional ? kProbeDefs[i].X * direction : kProbeDefs[i].X;
        const int offsetY = kProbeDefs[i].Y;
        AITileProbeSample& sample = probe.Samples[i];
        sample.Name = kProbeDefs[i].Name;
        sample.OffsetX = static_cast<melonDS::u32>(offsetX);
        sample.OffsetY = static_cast<melonDS::u32>(offsetY);
        const melonDS::u32 worldX = actor.PosX + static_cast<melonDS::u32>(offsetX * 4096);
        const melonDS::u32 worldY = actor.PosY + static_cast<melonDS::u32>(offsetY * 4096);
        ReadStageLayoutTileBehavior(nds, worldX, worldY, playerID, sample);
    }
    for (int row = 0; row < kAITileGridHeight; row++)
    {
        for (int col = 0; col < kAITileGridWidth; col++)
        {
            const int index = row * kAITileGridWidth + col;
            const std::int32_t relTileX = kAITileGridMinRelX + col;
            const std::int32_t relTileY = kAITileGridMinRelY + row;
            const std::int32_t tileX = anchorTileX + relTileX;
            const std::int32_t tileY = anchorTileY + relTileY;
            AITileGridSample& cell = probe.Grid[index];
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

GameStateSample ReadGameStateSample(melonDS::NDS* nds)
{
    GameStateSample sample;
    if (!nds || !nds->MainRAM)
        return sample;
    const GameStateObjectScanCache objectScanCache = BuildGameStateObjectScanCache(nds);
    const ScopedGameStateObjectScanCache scopedObjectScanCache(objectScanCache);

    GameStateReader::ReadCoreState(nds, sample);

    GameStateReader::ReadBattleStarState(nds, sample);

    const PlayerActorScanSample players = FindPlayerActors(nds);
    GameStateReader::CopyPlayerActor(players.Actor0, sample, 0);
    sample.PlayerActor0CollisionMgr = ReadPlayerCollisionMgrSample(nds, players.Actor0);
    sample.PlayerActor0Hitbox = ReadPlayerHitboxSample(nds, players.Actor0);
    sample.PlayerActor0TileProbe = ReadAIPlayerTileProbeSample(nds, players.Actor0);
    GameStateReader::ReadPlayerTileDamage(nds, players.Actor0, sample, 0);
    GameStateReader::CopyPlayerActor(players.Actor1, sample, 1);
    sample.PlayerActor1CollisionMgr = ReadPlayerCollisionMgrSample(nds, players.Actor1);
    sample.PlayerActor1Hitbox = ReadPlayerHitboxSample(nds, players.Actor1);
    sample.PlayerActor1TileProbe = ReadAIPlayerTileProbeSample(nds, players.Actor1);
    GameStateReader::ReadPlayerTileDamage(nds, players.Actor1, sample, 1);
    GameStateReader::ReadPlayerTransitionState(nds, players.Actor0, sample, 0);
    GameStateReader::ReadPlayerTransitionState(nds, players.Actor1, sample, 1);

    GameStateReader::ReadPlayerBaseRuntimeState(nds, players.Actor0, sample, 0);
    GameStateReader::ReadPlayerBaseRuntimeState(nds, players.Actor1, sample, 1);

    GameStateReader::ReadPlayerAndCameraGlobals(nds, sample);

    GameStateReader::ReadStageObjectState(nds, G.MvlCurrentStageSceneSettings, sample);

    sample.Hash = ComputeBasicGameStateHash(sample);
    return sample;
}

void UpdateHangGameSnapshot(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.HangDiagnosticsEnabled || !nds || !nds->MainRAM || instanceID < 0 || instanceID >= 16)
        return;

    const GameStateSample sample = ReadGameStateSample(nds);
    G.HangInstance.store(instanceID, std::memory_order_release);
    G.HangFrame.store(frame, std::memory_order_release);
    G.HangArm9PC.store(sample.Arm9PC, std::memory_order_release);
    G.HangArm9LR.store(sample.Arm9LR, std::memory_order_release);
    G.HangArm9SP.store(sample.Arm9SP, std::memory_order_release);
    G.HangArm9CPSR.store(sample.Arm9CPSR, std::memory_order_release);
    G.HangStageID.store(sample.StageID, std::memory_order_release);
    G.HangStageGroup.store(sample.StageGroup, std::memory_order_release);
    G.HangVsMode.store(sample.VsMode, std::memory_order_release);
    G.HangNetState14.store(sample.NetState14, std::memory_order_release);
    G.HangNetState1C.store(sample.NetState1C, std::memory_order_release);
    G.HangNetState20.store(sample.NetState20, std::memory_order_release);
    G.HangNetState24.store(sample.NetState24, std::memory_order_release);
    G.HangNetState5C.store(sample.NetState5C, std::memory_order_release);
    G.HangNetPacketTick.store(sample.NetPacketTick, std::memory_order_release);
    G.HangAppFrameLength.store(sample.AppFrameLength, std::memory_order_release);
    G.HangAppUpdateTask.store(sample.AppUpdateTask, std::memory_order_release);
    G.HangAppSleeping.store(sample.AppSleeping, std::memory_order_release);
    G.HangStageSceneState.store(sample.StageSceneStateType, std::memory_order_release);
    G.HangPlayer0Transition.store(sample.PlayerTransitionStatus0, std::memory_order_release);
    G.HangPlayer1Transition.store(sample.PlayerTransitionStatus1, std::memory_order_release);
    G.HangGameSnapshotUnixMs.store(NowUnixMs(), std::memory_order_release);
}

void SaveScreenshot(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (G.ScreenshotDir.empty() || G.ScreenshotInterval <= 0) return;
    if ((frame % static_cast<melonDS::u32>(G.ScreenshotInterval)) != 0) return;

    void* topBuffer = nullptr;
    void* bottomBuffer = nullptr;
    if (!nds->GPU.GetFramebuffers(&topBuffer, &bottomBuffer))
    {
        if (EnvFlag("MELONDS_NSML_SCREENSHOT_REG_TRACE"))
        {
            std::printf("NSMB Test: screenshot skipped inst=%d frame=%u reason=no-framebuffer\n", instanceID, frame);
            std::fflush(stdout);
        }
        return;
    }
    if (!topBuffer || !bottomBuffer)
    {
        if (EnvFlag("MELONDS_NSML_SCREENSHOT_REG_TRACE"))
        {
            std::printf(
                "NSMB Test: screenshot skipped inst=%d frame=%u reason=null-buffer top=%p bottom=%p\n",
                instanceID,
                frame,
                topBuffer,
                bottomBuffer);
            std::fflush(stdout);
        }
        return;
    }

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
    if (!nds || !nds->MainRAM || G.Harness.MemPatchFile.empty() || !G.Harness.MemPatchFrameSet) return;
    if (frame != G.Harness.MemPatchFrame || G.MemPatchApplied[instanceID]) return;
    if (G.Harness.MemPatchInstance >= 0 && G.Harness.MemPatchInstance != instanceID) return;
    if (G.MemPatchRanges.empty()) return;

    std::string patchFile = G.Harness.MemPatchFile;
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

bool WriteNetAndGameRandomSeed(melonDS::NDS* nds, melonDS::u32 seed)
{
    constexpr melonDS::u32 kNetRandomValueOffset = kNetRandomValueAddr - kMainRAMBase;
    constexpr melonDS::u32 kNetRandomCallCountOffset = kNetRandomCallCountAddr - kMainRAMBase;
    constexpr melonDS::u32 kGameRandomValueOffset = kGameRandomValueAddr - kMainRAMBase;
    constexpr melonDS::u32 kGameRandomCallCountOffset = kGameRandomCallCountAddr - kMainRAMBase;

    if (!nds || !nds->MainRAM) return false;
    if (kNetRandomValueOffset + sizeof(seed) > nds->MainRAMMask + 1) return false;
    if (kGameRandomValueOffset + sizeof(seed) > nds->MainRAMMask + 1) return false;

    std::memcpy(&nds->MainRAM[kNetRandomValueOffset], &seed, sizeof(seed));
    nds->MainRAM[kNetRandomCallCountOffset] = 0;
    std::memcpy(&nds->MainRAM[kGameRandomValueOffset], &seed, sizeof(seed));
    nds->MainRAM[kGameRandomCallCountOffset] = 0;
    return true;
}

void ApplyNetRandomPatch(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!nds || !nds->MainRAM || !G.NetRandomPatchEnabled) return;
    if (instanceID < 0 || instanceID >= 16) return;
    if (G.NetRandomPatchApplied[instanceID]) return;

    bool shouldPatch = frame == G.NetRandomPatchFrame;
    melonDS::u8 randomCallCountBeforePatch = 0;
    melonDS::u8 gameRandomCallCountBeforePatch = 0;
    if (G.NetRandomPatchAuto)
    {
        const melonDS::u32 ggid = nds->ARM9Read32(kNetGGIDAddr);
        randomCallCountBeforePatch = nds->ARM9Read8(kNetRandomCallCountAddr);
        gameRandomCallCountBeforePatch = nds->ARM9Read8(kGameRandomCallCountAddr);
        shouldPatch = IsMarioVsLuigiGameplay(nds) || IsMarioVsLuigiGGID(ggid);
    }
    if (!shouldPatch) return;

    const melonDS::u32 patchValue = (!G.MatchSeedSequence.empty() || G.Mvl.AutoRestartAfterResult)
        ? MatchSeedForGame(instanceID)
        : G.NetRandomPatchValue;
    if (!WriteNetAndGameRandomSeed(nds, patchValue)) return;
    G.NetRandomPatchApplied[instanceID] = true;

    std::printf("NSMB Test: patched Net/Game random inst=%d frame=%u value=0x%08X auto=%d oldNetCount=0x%02X oldGameCount=0x%02X resetCount=1\n",
        instanceID,
        frame,
        patchValue,
        G.NetRandomPatchAuto ? 1 : 0,
        randomCallCountBeforePatch,
        gameRandomCallCountBeforePatch);
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

    GameStateTraceHashes traceHashes;
    const GameStateTraceHashes* extendedHashes = nullptr;
    if (G.GameStateTraceExtended)
    {
        traceHashes.PlayerGlobal = HashMainRAMRange(nds, kGamePlayerGlobalBlockAddr, 0xC0);
        traceHashes.WifiCandidate = HashMainRAMRange(nds, kGameCandidateWifiBlockAddr, 0x2200);
        traceHashes.RenderCandidate = HashMainRAMRange(nds, kGameCandidateRenderBlockAddr, 0x240);
        traceHashes.NetState = HashMainRAMRange(nds, kNetStateBaseAddr, 0x180);
        extendedHashes = &traceHashes;
    }
    WriteGameStateTraceRow(G.GameStateTrace, instanceID, frame, sample, extendedHashes);
    G.GameStateTrace.flush();
}

void WriteJsonHex(std::ostream& out, melonDS::u32 value, int width = 8)
{
    const std::ios::fmtflags flags = out.flags();
    const char fill = out.fill();
    out << "\"0x" << std::uppercase << std::hex << std::setw(width) << std::setfill('0') << value << "\"";
    out.flags(flags);
    out.fill(fill);
}

std::int32_t SignedU32(melonDS::u32 value)
{
    return static_cast<std::int32_t>(value);
}

#include "NsmbAiObservation.h"
#include "NsmbAiObservation.cpp"

void SyncGameState(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.Enabled || !G.StateSync.GameEnabled || !nds) return;
    if (instanceID < 0 || instanceID >= 16) return;
    if (frame < G.Connection.StartFrame) return;
    if ((frame % static_cast<melonDS::u32>(G.StateSync.GameInterval)) != 0) return;

    const GameStateSample sample = ReadGameStateSample(nds);
    GameStateSyncHashes hashes;
    hashes.Basic = sample.Hash;
    if (G.StateSync.GameExtended)
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

    if (!G.Transport.IsConnected()) return;
    const WireGameState packet = EncodeWireGameState(
        frame,
        static_cast<melonDS::u32>(instanceID),
        sample,
        hashes);

    G.Transport.Send(&packet, sizeof(packet), ENET_PACKET_FLAG_RELIABLE, false);
}

void SyncPlayerState(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.Enabled || !G.StateSync.PlayerEnabled || !nds) return;
    if (instanceID < 0 || instanceID >= 16) return;
    if (frame < G.Connection.StartFrame) return;
    if ((frame % static_cast<melonDS::u32>(G.StateSync.PlayerInterval)) != 0) return;

    const int localPlayer = CurrentPacketBridgeLocalPlayer();
    const ObjectScanSample actor = GetPlayerActorCached(instanceID, localPlayer, nds);
    const bool found = actor.Found != 0;

    WirePlayerState packet {};
    packet.Magic = kMagic;
    packet.Version = kVersion;
    packet.Kind = kWireKindPlayerState;
    packet.Frame = frame;
    packet.Instance = static_cast<melonDS::u32>(instanceID);
    packet.Player = static_cast<melonDS::u32>(localPlayer);
    packet.Found = found ? 1u : 0u;
    if (G.StateSync.PlayerGlobalsEnabled)
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
    if (found && IsARM9MainRAMAddress(actor.Base))
    {
        packet.ActionFlag = nds->ARM9Read32(actor.Base + kPlayerBaseActionFlagOffset);
        packet.SubActionFlag = nds->ARM9Read32(actor.Base + kPlayerBaseSubActionFlagOffset);
        packet.PhysicsFlag = nds->ARM9Read32(actor.Base + kPlayerBasePhysicsFlagOffset);
        packet.DamageCooldown = nds->ARM9Read16(actor.Base + kPlayerBaseDamageCooldownOffset);
        packet.TransitionFlag = nds->ARM9Read32(actor.Base + kPlayerBaseTransitionFlagOffset);
        packet.CollisionFlag = nds->ARM9Read32(actor.Base + kPlayerBaseCollisionFlagOffset);
        packet.EnvironmentFlag = nds->ARM9Read32(actor.Base + kPlayerBaseEnvironmentFlagOffset);
        packet.RuntimeFlags0 =
            (static_cast<melonDS::u32>(nds->ARM9Read8(actor.Base + kPlayerBaseUpdateLockedOffset)) & 0xFFu)
            | ((static_cast<melonDS::u32>(nds->ARM9Read8(actor.Base + kPlayerBaseCharacterIDOffset)) & 0xFFu) << 8)
            | ((static_cast<melonDS::u32>(nds->ARM9Read8(actor.Base + kPlayerBaseTransitioningFlagOffset)) & 0xFFu) << 16)
            | ((static_cast<melonDS::u32>(nds->ARM9Read8(actor.Base + kPlayerBaseCameraFocusModeOffset)) & 0xFFu) << 24);
        packet.RuntimeFlags1 =
            (static_cast<melonDS::u32>(nds->ARM9Read8(actor.Base + kPlayerBaseDefeatedFlagOffset)) & 0xFFu)
            | ((static_cast<melonDS::u32>(nds->ARM9Read8(actor.Base + kPlayerBasePlayerIDOffset)) & 0xFFu) << 8)
            | ((static_cast<melonDS::u32>(nds->ARM9Read8(actor.Base + kPlayerBaseVisibleFlagOffset)) & 0xFFu) << 16);
    }

    std::lock_guard<std::mutex> lock(G.Mutex);
    if (G.LastSentPlayerStateFrame[instanceID] == frame) return;
    G.LastSentPlayerStateFrame[instanceID] = frame;
    if (!G.Transport.IsConnected())
    {
        if (G.Bootstrap.InputTraceEnabled &&
            (G.Bootstrap.InputTraceInterval <= 1 || (frame % static_cast<melonDS::u32>(G.Bootstrap.InputTraceInterval)) == 0))
        {
            std::printf("NSMB PlayerState: send skipped inst=%d frame=%u reason=no-peer\n",
                instanceID,
                frame);
        }
        return;
    }

    if ((G.Bootstrap.InputTraceEnabled || G.Input.NetplayTrace) &&
        (G.Bootstrap.InputTraceInterval <= 1 || (frame % static_cast<melonDS::u32>(G.Bootstrap.InputTraceInterval)) == 0))
    {
        std::printf("NSMB PlayerState: send inst=%d frame=%u player=%u found=%u pos=%08X/%08X vel=%08X/%08X\n",
            instanceID,
            frame,
            packet.Player,
            packet.Found,
            packet.PosX,
            packet.PosY,
            packet.VelX,
            packet.VelY);
    }

    G.Transport.Send(&packet, sizeof(packet), 0, false);
}

void SyncWorldState(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.Enabled || !G.StateSync.WorldEnabled || !nds) return;
    if (instanceID < 0 || instanceID >= 16) return;
    if (frame < G.Connection.StartFrame) return;
    if ((frame % static_cast<melonDS::u32>(G.StateSync.WorldInterval)) != 0) return;
    TraceWorldMovingHazardsIfNeeded(instanceID, frame, nds);
    TraceWorldObjectLifecyclesIfNeeded(instanceID, frame, nds);
    TraceWorldEffectsIfNeeded(instanceID, frame, nds);

    const ObjectScanSample star = GetWorldActorCached(
        instanceID,
        frame,
        nds,
        kVsBattleStarActorObjectID,
        kVsBattleStarActorSettings,
        G.WorldStarActorBaseCache,
        G.WorldStarActorGUIDCache);
    ObjectScanSample item;
    ObjectScanSample neutralItem;
    ObjectScanSample droppedStarItem;
    if (G.StateSync.WorldSpawnItem)
    {
        neutralItem = FindNewestActiveObjectByIDAndSettings(
            nds,
            kVsWorldItemObjectID,
            kVsNeutralWorldItemSettings,
            true);
        item = FindNewestActiveObjectByIDAndSettings(
            nds,
            kVsWorldItemObjectID,
            kVsWorldItemSettings,
            true);
        droppedStarItem = FindNewestActiveObjectByIDAndSettings(
            nds,
            kVsWorldItemObjectID,
            kVsDroppedStarItemSettings,
            true);
    }

    WireWorldState packet {};
    packet.Magic = kMagic;
    packet.Version = kVersion;
    packet.Kind = kWireKindWorldState;
    packet.Frame = frame;
    packet.Instance = static_cast<melonDS::u32>(instanceID);
    FillWireWorldActorState(star, packet.Star);
    FillWireWorldActorState(neutralItem, packet.NeutralItem);
    FillWireWorldActorState(item, packet.Item);
    FillWireWorldActorState(droppedStarItem, packet.DroppedStarItem);

    std::lock_guard<std::mutex> lock(G.Mutex);
    if (G.LastSentWorldStateFrame[instanceID] == frame) return;
    G.LastSentWorldStateFrame[instanceID] = frame;
    if (!G.Transport.IsConnected()) return;

    if ((G.Bootstrap.InputTraceEnabled || G.Input.NetplayTrace) &&
        (G.Bootstrap.InputTraceInterval <= 1 || (frame % static_cast<melonDS::u32>(G.Bootstrap.InputTraceInterval)) == 0))
    {
        std::printf("NSMB WorldState: send inst=%d frame=%u star=%u neutralItem=%u item=%u droppedItem=%u hazard=%u hazardPos=%08X/%08X\n",
            instanceID,
            frame,
            packet.Star.Found,
            packet.NeutralItem.Found,
            packet.Item.Found,
            packet.DroppedStarItem.Found,
            packet.MovingHazard.Found,
            packet.MovingHazard.PosX,
            packet.MovingHazard.PosY);
    }

    G.Transport.Send(&packet, sizeof(packet), 0, false);
}

void SyncWorldEffectState(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.Enabled || !G.StateSync.WorldEnabled || !G.StateSync.WorldApplyEffects ||
        G.NetRole != Role::Host || !nds || !nds->MainRAM)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (frame < G.Connection.StartFrame)
        return;
    if ((frame % static_cast<melonDS::u32>(G.StateSync.WorldInterval)) != 0)
        return;

    WireWorldEffectState packet {};
    packet.Magic = kMagic;
    packet.Version = kVersion;
    packet.Kind = kWireKindWorldEffectState;
    packet.Frame = frame;
    packet.Instance = static_cast<melonDS::u32>(instanceID);

    for (melonDS::u32 slotIndex = 0; slotIndex < kWorldEffectSlotCount && packet.Count < kMaxWorldEffects; slotIndex++)
    {
        const melonDS::u32 base = kWorldEffectSlotBase + slotIndex * kWorldEffectSlotStride;
        WireWorldEffectSlot slot {};
        if (!ReadWorldEffectSlot(nds, base, slot))
            continue;
        packet.Effects[packet.Count++] = slot;
    }
    if (packet.Count == 0)
        return;

    std::lock_guard<std::mutex> lock(G.Mutex);
    if (!G.Transport.IsConnected())
        return;
    G.Transport.Send(&packet, sizeof(packet), 0, false);
}

void SyncMovingHazardState(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.Enabled || !G.StateSync.WorldApplyMovingHazard || G.NetRole != Role::Host || !nds) return;
    if (instanceID < 0 || instanceID >= 16) return;
    if (frame < G.Connection.StartFrame) return;
    if ((frame % static_cast<melonDS::u32>(G.StateSync.WorldInterval)) != 0) return;

    const std::vector<ObjectScanSample> actors = GetWorldMovingHazardsCached(instanceID, frame, nds);
    WireMovingHazardState packet {};
    packet.Magic = kMagic;
    packet.Version = kVersion;
    packet.Kind = kWireKindMovingHazardState;
    packet.Frame = frame;
    packet.Instance = static_cast<melonDS::u32>(instanceID);
    packet.Count = static_cast<melonDS::u32>(std::min(actors.size(), kMaxWorldMovingHazards));
    for (melonDS::u32 i = 0; i < packet.Count; i++)
        FillWireWorldActorState(actors[i], packet.Actors[i]);

    std::lock_guard<std::mutex> lock(G.Mutex);
    if (!G.Transport.IsConnected()) return;
    G.Transport.Send(&packet, sizeof(packet), 0, false);
}

void SyncWorldActorSnapshotState(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.Enabled || !G.StateSync.WorldEnabled || !G.StateSync.WorldApplyActorSnapshot ||
        G.NetRole != Role::Host || !nds || !nds->MainRAM)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (frame < G.Connection.StartFrame)
        return;
    if ((frame % static_cast<melonDS::u32>(G.StateSync.WorldInterval)) != 0)
        return;

    const std::vector<WorldActorSnapshotCandidate> actors = CollectWorldActorSnapshotCandidates(nds);
    if (actors.empty())
        return;

    WireWorldActorSnapshotState packet {};
    packet.Magic = kMagic;
    packet.Version = kVersion;
    packet.Kind = kWireKindWorldActorSnapshot;
    packet.Frame = frame;
    packet.Instance = static_cast<melonDS::u32>(instanceID);
    packet.Count = static_cast<melonDS::u32>(std::min(actors.size(), kMaxWorldActorSnapshots));
    for (melonDS::u32 i = 0; i < packet.Count; i++)
    {
        packet.Actors[i].ObjectID = actors[i].ObjectID;
        FillWireWorldActorState(actors[i].Actor, packet.Actors[i].Actor);
    }

    std::lock_guard<std::mutex> lock(G.Mutex);
    if (!G.Transport.IsConnected())
        return;
    G.Transport.Send(&packet, sizeof(packet), 0, false);
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
    if (G.Harness.StateSaveDir.empty() || G.Harness.StateSaveFrame == 0) return false;
    if (frame != G.Harness.StateSaveFrame || G.StateSaved[instanceID]) return false;

    std::error_code ec;
    std::filesystem::create_directories(G.Harness.StateSaveDir, ec);
    if (ec)
    {
        std::printf("NSMB Test: failed to create state save dir: %s (%s)\n",
            G.Harness.StateSaveDir.c_str(),
            ec.message().c_str());
        return false;
    }

    melonDS::Savestate state;
    if (state.Error || !nds->DoSavestate(&state) || state.Error)
    {
        std::printf("NSMB Test: failed to create savestate inst=%d frame=%u\n", instanceID, frame);
        return false;
    }

    const std::filesystem::path path = StatePath(G.Harness.StateSaveDir, instanceID);
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
    for (int i = 0; i < G.Bootstrap.TestInstanceCount; i++)
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
        if (G.Harness.StateSaveDir.empty() || G.Harness.StateSaveFrame == 0) return false;
        if (frame != G.Harness.StateSaveFrame || G.LocalMPSaved) return false;
        if (!AllStatesSavedLocked()) return false;
        G.LocalMPSaved = true;
        stateSaveDir = G.Harness.StateSaveDir;
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
    if (G.Bootstrap.TestInstanceCount <= 1) return true;

    const auto start = std::chrono::steady_clock::now();
    for (;;)
    {
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            bool allLoaded = true;
            for (int i = 0; i < G.Bootstrap.TestInstanceCount; i++)
            {
                if (!G.StateLoaded[i])
                {
                    allLoaded = false;
                    break;
                }
            }
            if (allLoaded) return true;
        }

        if (G.Bootstrap.WaitTimeoutMs > 0)
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= G.Bootstrap.WaitTimeoutMs)
            {
                std::printf("NSMB Test: state load barrier timeout inst=%d waitedMs=%d\n",
                    instanceID,
                    G.Bootstrap.WaitTimeoutMs);
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

        if (G.Bootstrap.WaitTimeoutMs > 0)
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= G.Bootstrap.WaitTimeoutMs)
            {
                std::printf("NSMB Test: LocalMP load barrier timeout inst=%d waitedMs=%d\n",
                    instanceID,
                    G.Bootstrap.WaitTimeoutMs);
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
        if (G.Harness.StateLoadDir.empty() || !G.Harness.StateLoadFrameSet) return false;
        if (!G.LocalMPLoadStarted)
        {
            G.LocalMPLoadStarted = true;
            shouldLoad = true;
            stateLoadDir = G.Harness.StateLoadDir;
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
        if (G.Harness.StateLoadDir.empty() || !G.Harness.StateLoadFrameSet) return false;
        if (frame != G.Harness.StateLoadFrame || G.StateLoaded[instanceID]) return false;
        stateLoadDir = G.Harness.StateLoadDir;
        stateLoadFrame = G.Harness.StateLoadFrame;
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

PerformanceCounters GetPerformanceCounters()
{
    InitFromEnvironment();
    std::lock_guard<std::mutex> lock(G.Mutex);
    PerformanceCounters counters;
    counters.RemoteInputWaitCount = G.RemoteInputWaitCount;
    counters.RemoteInputWaitUs = G.RemoteInputWaitUs;
    counters.RemoteInputWaitMaxUs = G.RemoteInputWaitMaxUs;
    counters.FrameLeadThrottleCount = G.FrameLeadThrottleCount;
    counters.FrameLeadThrottleUs = G.FrameLeadThrottleUs;
    counters.FrameLeadThrottleMaxUs = G.FrameLeadThrottleMaxUs;
    counters.LastSentInputFrame = G.LastSentInputFrame;
    counters.LastReceivedInputFrame = G.LastReceivedInputFrame;
    if (G.LastSentInputFrame != kNoFrameLimit && G.LastReceivedInputFrame != kNoFrameLimit)
    {
        counters.InputLead = static_cast<int>(G.LastSentInputFrame)
            - static_cast<int>(G.LastReceivedInputFrame);
    }
    return counters;
}

void InitFromEnvironment()
{
    if (G.EnvChecked.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lock(G.Mutex);
    if (G.EnvChecked.load(std::memory_order_relaxed)) return;
    struct MarkEnvironmentChecked
    {
        ~MarkEnvironmentChecked()
        {
            G.EnvChecked.store(true, std::memory_order_release);
        }
    } markEnvironmentChecked;

    G.Bootstrap = Config::LoadBootstrapConfig();
    const Config::DiagnosticsConfig diagnosticsConfig =
        Config::LoadDiagnosticsConfig(static_cast<int>(kDiagnosticRingCapacity));
    G.Harness = Config::LoadHarnessConfig();
    G.Enabled = G.Bootstrap.Enabled;
    G.TestEnabled = G.Bootstrap.TestEnabled;
    G.HangDiagnosticsEnabled = diagnosticsConfig.HangDiagnosticsEnabled;
    G.HangWatchdogIntervalMs = diagnosticsConfig.HangWatchdogIntervalMs;
    G.HangThresholdMs = diagnosticsConfig.HangThresholdMs;
    G.HangWatchdogPath = diagnosticsConfig.HangWatchdogPath;
    G.HangPhaseEventsPath = diagnosticsConfig.HangPhaseEventsPath;
    G.HangDumpPath = diagnosticsConfig.HangDumpPath;
    G.ActiveFpsStartFrame = diagnosticsConfig.ActiveFpsStartFrame;
    G.ActiveFrameSpikeThresholdUs = diagnosticsConfig.ActiveFrameSpikeThresholdUs;
    G.ActiveFrameSpikeTrace = diagnosticsConfig.ActiveFrameSpikeTrace;
    G.FrameHeartbeatInterval = diagnosticsConfig.FrameHeartbeatInterval;
    G.GameplayHeartbeatInterval = diagnosticsConfig.GameplayHeartbeatInterval;
    if (!diagnosticsConfig.FrameHeartbeatPath.empty())
    {
        G.FrameHeartbeatPath = diagnosticsConfig.FrameHeartbeatPath;
        G.FrameHeartbeat.open(G.FrameHeartbeatPath, std::ios::out | std::ios::trunc);
        StartFrameHeartbeatThreadIfNeeded();
    }
    if (!diagnosticsConfig.InputRecordPath.empty())
    {
        G.InputRecordPath = diagnosticsConfig.InputRecordPath;
        G.InputRecordStartFrame = diagnosticsConfig.InputRecordStartFrame;
        G.InputRecordEndFrame = diagnosticsConfig.InputRecordEndFrame;
        G.InputRecordInstance = diagnosticsConfig.InputRecordInstance;
        G.InputRecord.open(G.InputRecordPath, std::ios::out | std::ios::trunc);
        if (G.InputRecord)
        {
            G.InputRecordEnabled = true;
            G.InputRecord << "# NSMB input recording generated by melonDS NSML PoC\n";
            G.InputRecord << "# startFrame=" << G.InputRecordStartFrame
                << " endFrame=" << G.InputRecordEndFrame
                << " instance=" << G.InputRecordInstance << "\n";
            G.InputRecord.flush();
            std::printf("NSMB Test: recording input to %s start=%u end=%u instance=%d\n",
                G.InputRecordPath.c_str(),
                G.InputRecordStartFrame,
                G.InputRecordEndFrame,
                G.InputRecordInstance);
        }
        else
        {
            std::printf("NSMB Test: failed to open input record file: %s\n",
                G.InputRecordPath.c_str());
        }
    }
    G.ScreenHashEnabled = diagnosticsConfig.ScreenHashEnabled;
    G.PacketBridge = Config::LoadPacketBridgeConfig();
    G.Input = Config::LoadInputConfig(false);
    G.RuntimePatch = Config::LoadRuntimePatchConfig();
    G.Mvl = Config::LoadMvlConfig();
    G.MvlCurrentStage = G.Mvl.DirectBootStage;
    G.MvlCurrentStageSceneSettings = G.Mvl.StageSceneSettings;
    if (!G.Mvl.InvalidCourseMode.empty())
    {
        std::printf("NSMB MvL settings: unknown courseMode=%s; using fixed stage=%d\n",
            G.Mvl.InvalidCourseMode.c_str(),
            G.MvlCurrentStage);
    }

    const char* inputScript = std::getenv("MELONDS_NSML_INPUT_SCRIPT");
    if (inputScript && inputScript[0]) G.InputScriptPath = inputScript;

    G.HashLogPath = diagnosticsConfig.HashLogPath;
    G.ScreenshotDir = diagnosticsConfig.ScreenshotDir;
    G.ScreenshotInterval = diagnosticsConfig.ScreenshotInterval;
    G.RamDumpDir = diagnosticsConfig.RamDumpDir;
    G.RamDumpInterval = diagnosticsConfig.RamDumpInterval;
    if (!ParseFrameRanges(diagnosticsConfig.RamDumpFrames.c_str(), G.RamDumpRanges))
    {
        std::printf("NSMB Test: invalid RAM dump frame list\n");
        G.RamDumpRanges.clear();
    }
    G.GameStateTracePath = diagnosticsConfig.GameStateTracePath;
    G.DiagnosticsPath = diagnosticsConfig.DiagnosticsPath;
    G.DiagnosticEventsPath = diagnosticsConfig.DiagnosticEventsPath;
    G.DiagnosticEventsEnabled = diagnosticsConfig.DiagnosticEventsEnabled;
    G.DiagnosticRingFrames = diagnosticsConfig.DiagnosticRingFrames;
    if (G.DiagnosticEventsEnabled && !G.DiagnosticEventsPath.empty())
    {
        std::error_code ec;
        std::filesystem::remove(G.DiagnosticEventsPath, ec);
    }
    G.GameStateTraceInterval = diagnosticsConfig.GameStateTraceInterval;
    G.GameStateTraceStartFrame = diagnosticsConfig.GameStateTraceStartFrame;
    G.GameStateTraceEndFrame = diagnosticsConfig.GameStateTraceEndFrame;
    G.GameStateTraceExtended = diagnosticsConfig.GameStateTraceExtended;
    G.AIPlayLogPath = diagnosticsConfig.AIPlayLogPath;
    G.AIObservationV2Path = diagnosticsConfig.AIObservationV2Path;
    G.AIObservationV3Path = diagnosticsConfig.AIObservationV3Path;
    G.AIPlayLogInterval = diagnosticsConfig.AIPlayLogInterval;
    G.AIPlayLogFlushInterval = diagnosticsConfig.AIPlayLogFlushInterval;
    G.AIPlayLogStartFrame = diagnosticsConfig.AIPlayLogStartFrame;
    G.AIPlayLogEndFrame = diagnosticsConfig.AIPlayLogEndFrame;
    G.AIPlayLogMaxObjects = diagnosticsConfig.AIPlayLogMaxObjects;
    G.AIObservationV2StageFilter = diagnosticsConfig.AIObservationV2StageFilter;
    G.AIObservationV3StageFilter = diagnosticsConfig.AIObservationV3StageFilter;
    G.AIPlayLogGameplayOnly = diagnosticsConfig.AIPlayLogGameplayOnly;
    G.StateSync = Config::LoadStateSyncConfig();

    if (!ParseFrameRanges(G.Harness.MemPatchRanges.c_str(), G.MemPatchRanges))
    {
        std::printf("NSMB Test: invalid memory patch range list\n");
        G.MemPatchRanges.clear();
    }
    G.AI = Config::LoadAIConfig();
    G.ImitationAIEnabled = G.AI.Imitation.Enabled;
    if (G.ImitationAIEnabled)
    {
        if (G.AI.Imitation.ModelPath.empty())
        {
            std::printf("NSMB ImitationAI: enabled but MELONDS_NSML_IMITATION_AI_MODEL is empty\n");
            G.ImitationAIEnabled = false;
        }
        else
        {
            std::string torchCompactError;
            G.ImitationAITorchCompactModelLoaded =
                NsmbImitationAI::LoadTorchCompactPolicyModel(G.AI.Imitation.ModelPath, G.ImitationAITorchCompactModel, torchCompactError);
            if (G.ImitationAITorchCompactModelLoaded)
            {
                G.ImitationAIModelLoaded = true;
            }
            else
            {
            std::string compactError;
            G.ImitationAICompactModelLoaded =
                NsmbImitationAI::LoadCompactActionPolicyModel(G.AI.Imitation.ModelPath, G.ImitationAICompactModel, compactError);
            if (G.ImitationAICompactModelLoaded)
            {
                G.ImitationAIModelLoaded = true;
            }
            else
            {
                std::string modelError;
                G.ImitationAIModelLoaded =
                    NsmbImitationAI::LoadLinearPolicyModel(G.AI.Imitation.ModelPath, G.ImitationAIModel, modelError);
                if (!G.ImitationAIModelLoaded)
                {
                    std::printf(
                        "NSMB ImitationAI: failed to load model path=%s torchCompactError=%s compactError=%s linearError=%s\n",
                        G.AI.Imitation.ModelPath.c_str(),
                        torchCompactError.c_str(),
                        compactError.c_str(),
                        modelError.c_str());
                    G.ImitationAIEnabled = false;
                }
            }
            }
        }
    }
    G.Rollback = Config::LoadRollbackConfig();
    G.ClearMvlCameraInitHoldEnabled = EnvFlag("MELONDS_NSML_CLEAR_MVL_CAMERA_INIT_HOLD");
    G.ClearMvlCameraInitHoldHostOnly = EnvFlag("MELONDS_NSML_CLEAR_MVL_CAMERA_INIT_HOLD_HOST_ONLY");
    G.ClearMvlCameraInitHoldClientOnly = EnvFlag("MELONDS_NSML_CLEAR_MVL_CAMERA_INIT_HOLD_CLIENT_ONLY");
    G.ClearMvlCameraInitHoldStartFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_CLEAR_MVL_CAMERA_INIT_HOLD_START_FRAME", 840)));
    G.ClearMvlCameraInitHoldEndFrame = static_cast<melonDS::u32>(
        std::max(0, EnvInt("MELONDS_NSML_CLEAR_MVL_CAMERA_INIT_HOLD_END_FRAME", 0)));

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
    G.MatchSeedSequence = Config::EnvU32List("MELONDS_NSML_MATCH_SEED_SEQUENCE");
    if (!G.MatchSeedSequence.empty())
    {
        G.MatchSeed = G.MatchSeedSequence.front();
        G.MatchSeedConfigured = true;
        G.NetRandomPatchValue = G.MatchSeed;
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
            WriteGameStateTraceHeader(G.GameStateTrace, G.GameStateTraceExtended);
        }
    }
    if ((G.TestEnabled || G.Enabled) && !G.AIPlayLogPath.empty())
    {
        std::error_code dirError;
        const std::filesystem::path aiPlayLogPath(G.AIPlayLogPath);
        const std::filesystem::path aiPlayLogParent = aiPlayLogPath.parent_path();
        if (!aiPlayLogParent.empty())
            std::filesystem::create_directories(aiPlayLogParent, dirError);
        G.AIPlayLog.open(G.AIPlayLogPath, std::ios::out | std::ios::trunc);
        if (!G.AIPlayLog)
        {
            std::printf("NSMB AIPlayLog: failed to open path=%s\n", G.AIPlayLogPath.c_str());
        }
        else
        {
            G.AIPlayLogLinesSinceFlush = 0;
            std::printf(
                "NSMB AIPlayLog: enabled path=%s interval=%d flushInterval=%d start=%u end=%u maxObjects=%d gameplayOnly=%d\n",
                G.AIPlayLogPath.c_str(),
                G.AIPlayLogInterval,
                G.AIPlayLogFlushInterval,
                G.AIPlayLogStartFrame,
                G.AIPlayLogEndFrame,
                G.AIPlayLogMaxObjects,
                G.AIPlayLogGameplayOnly ? 1 : 0);
        }
    }
    if ((G.TestEnabled || G.Enabled) && !G.AIObservationV2Path.empty())
    {
        std::error_code dirError;
        const std::filesystem::path observationPath(G.AIObservationV2Path);
        const std::filesystem::path observationParent = observationPath.parent_path();
        if (!observationParent.empty())
            std::filesystem::create_directories(observationParent, dirError);
        G.AIObservationV2Log.open(G.AIObservationV2Path, std::ios::out | std::ios::trunc);
        if (!G.AIObservationV2Log)
        {
            std::printf("NSMB AIObservationV2: failed to open path=%s\n", G.AIObservationV2Path.c_str());
        }
        else
        {
            G.AIObservationV2LinesSinceFlush = 0;
            std::printf(
                "NSMB AIObservationV2: enabled path=%s interval=%d flushInterval=%d start=%u end=%u maxObjects=%d stageFilter=%d gameplayOnly=%d\n",
                G.AIObservationV2Path.c_str(),
                G.AIPlayLogInterval,
                G.AIPlayLogFlushInterval,
                G.AIPlayLogStartFrame,
                G.AIPlayLogEndFrame,
                G.AIPlayLogMaxObjects,
                G.AIObservationV2StageFilter,
                G.AIPlayLogGameplayOnly ? 1 : 0);
        }
    }
    if ((G.TestEnabled || G.Enabled) && !G.AIObservationV3Path.empty())
    {
        std::error_code dirError;
        const std::filesystem::path observationPath(G.AIObservationV3Path);
        const std::filesystem::path observationParent = observationPath.parent_path();
        if (!observationParent.empty())
            std::filesystem::create_directories(observationParent, dirError);
        G.AIObservationV3Log.open(G.AIObservationV3Path, std::ios::out | std::ios::trunc);
        if (!G.AIObservationV3Log)
        {
            std::printf("NSMB AIObservationV3: failed to open path=%s\n", G.AIObservationV3Path.c_str());
        }
        else
        {
            G.AIObservationV3LinesSinceFlush = 0;
            std::printf(
                "NSMB AIObservationV3: enabled path=%s interval=%d flushInterval=%d start=%u end=%u maxObjects=%d stageFilter=%d gameplayOnly=%d\n",
                G.AIObservationV3Path.c_str(),
                G.AIPlayLogInterval,
                G.AIPlayLogFlushInterval,
                G.AIPlayLogStartFrame,
                G.AIPlayLogEndFrame,
                G.AIPlayLogMaxObjects,
                G.AIObservationV3StageFilter,
                G.AIPlayLogGameplayOnly ? 1 : 0);
        }
    }

    if (G.TestEnabled)
    {
        if (!LoadInputScriptLocked())
            G.TestEnabled = false;
        if (!G.RuntimePatch.ScriptRemotePacketInputScriptPath.empty() &&
            !LoadInputScriptFileLocked(G.RuntimePatch.ScriptRemotePacketInputScriptPath, GScriptRemotePacketInputScript))
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

        std::printf("NSMB Test: enabled tUnixMs=%llu frames=%u instances=%d frameBarrier=%d serialRun=%d input=%s hashLog=%s interval=%d screenshotDir=%s screenshotInterval=%d ramDumpDir=%s ramDumpInterval=%d ramDumpRanges=%zu gameStateTrace=%s gameStateTraceInterval=%d stateSync=%d stateApply=%d stateSyncInterval=%d playerStateSync=%d playerStateApply=%d playerStateGlobals=%d playerStateInterval=%d playerStatePredict=%d memPatchFile=%s memPatchFrame=%u memPatchRanges=%zu netRandomEnabled=%d netRandomAuto=%d netRandomFrame=%u netRandomValue=0x%08X stateSaveDir=%s stateSaveFrame=%u stateLoadDir=%s stateLoadFrame=%u waitTimeoutMs=%d quitGraceMs=%d inputTrace=%d inputTraceInterval=%d seedWaitMs=%d waitForPeer=%d waitForPeerAtStart=%d deferNetworkUntilStart=%d netplayFrameBarrier=%d packetBridge=%d packetBridgeOnly=%d packetBridgePreGame=%d packetBridgeTrace=%d packetBridgeWait=%d packetBridgeWaitMs=%d packetBridgeWaitStart=%u packetBridgeWaitAhead=%d packetBridgeDirect=%d packetBridgeForceTick=%d packetBridgeForceTickStart=%u packetBridgeMaxTickLead=%d packetBridgeMaxFrameLead=%d packetBridgeThrottleMs=%d packetBridgeThrottleStart=%u directBoot=%d directBootFrame=%u directBootScene=%d directBootStage=%d directBootPlayerID=%d mvlSceneSettings=0x%08X mvlCourseMode=%s mvlBigStarTarget=%d\n",
            NowUnixMs(),
            G.Bootstrap.TestFrames,
            G.Bootstrap.TestInstanceCount,
            G.Harness.FrameBarrierEnabled ? 1 : 0,
            G.Harness.SerialRunEnabled ? 1 : 0,
            G.InputScriptPath.empty() ? "<none>" : G.InputScriptPath.c_str(),
            G.HashLogPath.empty() ? "<none>" : G.HashLogPath.c_str(),
            G.Bootstrap.HashInterval,
            G.ScreenshotDir.empty() ? "<none>" : G.ScreenshotDir.c_str(),
            G.ScreenshotInterval,
            G.RamDumpDir.empty() ? "<none>" : G.RamDumpDir.c_str(),
            G.RamDumpInterval,
            G.RamDumpRanges.size(),
            G.GameStateTracePath.empty() ? "<none>" : G.GameStateTracePath.c_str(),
            G.GameStateTraceInterval,
            G.StateSync.GameEnabled ? 1 : 0,
            G.StateSync.GameApplyEnabled ? 1 : 0,
            G.StateSync.GameInterval,
            G.StateSync.PlayerEnabled ? 1 : 0,
            G.StateSync.PlayerApplyEnabled ? 1 : 0,
            G.StateSync.PlayerGlobalsEnabled ? 1 : 0,
            G.StateSync.PlayerInterval,
            G.StateSync.PlayerMaxPredictFrames,
            G.Harness.MemPatchFile.empty() ? "<none>" : G.Harness.MemPatchFile.c_str(),
            G.Harness.MemPatchFrameSet ? G.Harness.MemPatchFrame : 0,
            G.MemPatchRanges.size(),
            G.NetRandomPatchEnabled ? 1 : 0,
            G.NetRandomPatchAuto ? 1 : 0,
            G.NetRandomPatchFrame,
            G.NetRandomPatchValue,
            G.Harness.StateSaveDir.empty() ? "<none>" : G.Harness.StateSaveDir.c_str(),
            G.Harness.StateSaveFrame,
            G.Harness.StateLoadDir.empty() ? "<none>" : G.Harness.StateLoadDir.c_str(),
            G.Harness.StateLoadFrameSet ? G.Harness.StateLoadFrame : 0,
            G.Bootstrap.WaitTimeoutMs,
            G.Bootstrap.QuitGraceMs,
            G.Bootstrap.InputTraceEnabled ? 1 : 0,
            G.Bootstrap.InputTraceInterval,
            G.Harness.SeedWaitTimeoutMs,
            G.Harness.WaitForPeerBeforeStart ? 1 : 0,
            G.Harness.WaitForPeerAtNetplayStart ? 1 : 0,
            G.Harness.DeferNetworkUntilStart ? 1 : 0,
            G.Harness.NetplayFrameBarrierEnabled ? 1 : 0,
            G.PacketBridge.Enabled ? 1 : 0,
            G.PacketBridge.Only ? 1 : 0,
            G.PacketBridge.AllowPreGame ? 1 : 0,
            G.PacketBridge.TraceEnabled ? 1 : 0,
            G.PacketBridge.WaitEnabled ? 1 : 0,
            G.PacketBridge.WaitTimeoutMs,
            G.PacketBridge.WaitStartFrame,
            G.PacketBridge.WaitTickAhead,
            G.PacketBridge.DirectCaptureEnabled ? 1 : 0,
            G.PacketBridge.ForceTickEnabled ? 1 : 0,
            G.PacketBridge.ForceTickStartFrame,
            G.PacketBridge.MaxTickLead,
            G.PacketBridge.MaxFrameLead,
            G.PacketBridge.ThrottleTimeoutMs,
            G.PacketBridge.ThrottleStartFrame,
            G.Mvl.DirectBootEnabled ? 1 : 0,
            G.Mvl.DirectBootFrame,
            G.Mvl.DirectBootScene,
            G.MvlCurrentStage,
            G.Mvl.DirectBootPlayerID,
            G.MvlCurrentStageSceneSettings,
            G.Mvl.CourseMode.c_str(),
            G.Mvl.BigStarTarget);
        std::printf("NSMB Diagnostics: events=%d path=%s ringFrames=%d diagnosticsFile=%s\n",
            G.DiagnosticEventsEnabled ? 1 : 0,
            G.DiagnosticEventsPath.empty() ? "<none>" : G.DiagnosticEventsPath.c_str(),
            G.DiagnosticRingFrames,
            G.DiagnosticsPath.empty() ? "<none>" : G.DiagnosticsPath.c_str());
        std::fflush(stdout);
    }

    G.Connection = Config::LoadConnectionConfig(G.TestEnabled);
    G.NetRole = G.Connection.Client ? Role::Client : Role::Host;

    if (!G.Enabled) return;


    if (G.NetRole == Role::Host && !G.MatchSeedConfigured)
    {
        G.MatchSeed = GenerateMatchSeed();
        G.MatchSeedConfigured = true;
    }

    if (G.MatchSeedConfigured
        && G.Harness.StateLoadDir.empty()
        && !G.PacketBridge.Only)
    {
        G.NetRandomPatchEnabled = true;
        G.NetRandomPatchAuto = true;
        G.NetRandomPatchValue = G.MatchSeed;
    }

    const NsmbNetplayTransport::InitializeResult transportResult = G.Transport.Initialize({
        G.NetRole == Role::Client,
        static_cast<std::uint16_t>(G.Connection.Port),
        G.Connection.PeerHost,
    });
    if (transportResult == NsmbNetplayTransport::InitializeResult::ENetInitializationFailed)
    {
        std::printf("NSMB PoC: ENet initialization failed\n");
        G.Enabled = false;
        return;
    }
    if (transportResult == NsmbNetplayTransport::InitializeResult::HostCreationFailed)
    {
        std::printf("NSMB PoC: failed to create ENet host\n");
        G.Enabled = false;
        return;
    }
    if (G.NetRole == Role::Client && !G.Transport.IsConnecting())
    {
        std::printf("NSMB PoC: failed to queue peer connect\n");
        std::fflush(stdout);
    }

    G.Ready = true;
    EmitDiagnosticStartupEvent();
    StartNetworkPumpThreadIfNeeded();
    StartHangWatchdogIfNeeded();
    TraceHangPhase("startup", "enabled", G.Connection.LocalInstance, 0, 0, 0);
    std::printf("NSMB PoC: enabled tUnixMs=%llu role=%s port=%d peer=%s delay=%d warmup=%d localInstance=%d netplayStartFrame=%u localWait=%d remoteTimeoutFatal=%d waitForPeer=%d waitForPeerAtStart=%d deferNetworkUntilStart=%d netplayFrameBarrier=%d packetBridge=%d packetBridgeOnly=%d packetBridgePreGame=%d packetBridgeTrace=%d packetBridgeWait=%d packetBridgeWaitMs=%d packetBridgeWaitStart=%u packetBridgeWaitAhead=%d packetBridgeDirect=%d packetBridgeForceTick=%d packetBridgeForceTickStart=%u packetBridgeMaxTickLead=%d packetBridgeMaxFrameLead=%d packetBridgeThrottleMs=%d packetBridgeThrottleStart=%u inputNetplayOnly=%d inputNetplayTrace=%d inputHealthTrace=%d inputHealthInterval=%d inputHealthWaitThresholdMs=%d inputMaxFrameLead=%d inputUnreliable=%d inputBundleHistory=%d inputSendDelay=%d inputSendJitter=%d inputSendDelayStart=%u inputSendDelayEnd=%u inputDropModulo=%d inputDropOffset=%d inputDropStart=%u inputDropEnd=%u netPumpThread=%d netPumpSleepUs=%d inputWaitPollUs=%d rollbackInputWaitUs=%d rollback=%d rollbackBackend=%s rollbackWindow=%d rollbackCheckpointInterval=%d rollbackResimDelay=%d rollbackResimulate=%d rollbackRestoreProbe=%d rollbackPredProbeModulo=%d rollbackPredProbeLimit=%d matchSeed=0x%08X seedConfigured=%d directBoot=%d directBootFrame=%u directBootScene=%d directBootStage=%d directBootPlayerID=%d mvlSceneSettings=0x%08X mvlCourseMode=%s mvlBigStarTarget=%d\n",
        NowUnixMs(),
        G.NetRole == Role::Host ? "host" : "client",
        G.Connection.Port,
        G.Connection.PeerHost.c_str(),
        G.Connection.Delay,
        G.Connection.WarmupFrames,
        G.Connection.LocalInstance,
        G.Connection.StartFrame,
        G.Connection.LocalWaitsForRemote ? 1 : 0,
        G.Connection.RemoteInputTimeoutFatal ? 1 : 0,
        G.Harness.WaitForPeerBeforeStart ? 1 : 0,
        G.Harness.WaitForPeerAtNetplayStart ? 1 : 0,
        G.Harness.DeferNetworkUntilStart ? 1 : 0,
        G.Harness.NetplayFrameBarrierEnabled ? 1 : 0,
        G.PacketBridge.Enabled ? 1 : 0,
        G.PacketBridge.Only ? 1 : 0,
        G.PacketBridge.AllowPreGame ? 1 : 0,
        G.PacketBridge.TraceEnabled ? 1 : 0,
        G.PacketBridge.WaitEnabled ? 1 : 0,
        G.PacketBridge.WaitTimeoutMs,
        G.PacketBridge.WaitStartFrame,
        G.PacketBridge.WaitTickAhead,
        G.PacketBridge.DirectCaptureEnabled ? 1 : 0,
        G.PacketBridge.ForceTickEnabled ? 1 : 0,
        G.PacketBridge.ForceTickStartFrame,
        G.PacketBridge.MaxTickLead,
        G.PacketBridge.MaxFrameLead,
        G.PacketBridge.ThrottleTimeoutMs,
        G.PacketBridge.ThrottleStartFrame,
        G.Input.NetplayOnly ? 1 : 0,
        G.Input.NetplayTrace ? 1 : 0,
        G.Input.HealthTrace ? 1 : 0,
        G.Input.HealthTraceInterval,
        G.Input.HealthTraceWaitThresholdMs,
        G.Input.MaxFrameLead,
        G.Input.UseHistoryBundle ? 1 : 0,
        G.Input.BundleHistory,
        G.Input.SendDelayFrames,
        G.Input.SendJitterFrames,
        G.Input.SendDelayStartFrame,
        G.Input.SendDelayEndFrame,
        G.Input.DropModulo,
        G.Input.DropOffset,
        G.Input.DropStartFrame,
        G.Input.DropEndFrame,
        G.Harness.NetworkPumpThreadEnabled ? 1 : 0,
        G.Harness.NetworkPumpSleepUs,
        G.Input.WaitPollUs,
        G.Rollback.InputWaitUs,
        G.Rollback.Enabled ? 1 : 0,
        RollbackBackendName(),
        G.Rollback.Window,
        G.Rollback.CheckpointInterval,
        G.Rollback.ResimulateDelayFrames,
        G.Rollback.Resimulate ? 1 : 0,
        G.Rollback.RestoreProbe ? 1 : 0,
        G.Rollback.PredictionProbeModulo,
        G.Rollback.PredictionProbeLimit,
        G.MatchSeed,
        G.MatchSeedConfigured ? 1 : 0,
        G.Mvl.DirectBootEnabled ? 1 : 0,
        G.Mvl.DirectBootFrame,
        G.Mvl.DirectBootScene,
        G.MvlCurrentStage,
        G.Mvl.DirectBootPlayerID,
        G.MvlCurrentStageSceneSettings,
        G.Mvl.CourseMode.c_str(),
        G.Mvl.BigStarTarget);
    if (G.AI.Rule.Enabled)
    {
        std::printf(
            "NSMB RuleAI: enabled player=%s startFrame=%u deadzone=0x%X wrapWidth=0x%X closeRange=0x%X hazardRange=0x%X/0x%X jump=%d/%d trace=%d traceInterval=%d\n",
            G.AI.Rule.PlayerSpec.c_str(),
            G.AI.Rule.StartFrame,
            G.AI.Rule.HorizontalDeadzone,
            G.AI.Rule.HorizontalWrapWidth,
            G.AI.Rule.CloseRange,
            G.AI.Rule.HazardHorizontalRange,
            G.AI.Rule.HazardVerticalRange,
            G.AI.Rule.JumpFrames,
            G.AI.Rule.JumpInterval,
            G.AI.Rule.TraceEnabled ? 1 : 0,
            G.AI.Rule.TraceInterval);
        if (G.AI.Rule.HostOnly || G.AI.Rule.ClientOnly)
        {
            std::printf("NSMB RuleAI: roleFilter hostOnly=%d clientOnly=%d\n",
                G.AI.Rule.HostOnly ? 1 : 0,
                G.AI.Rule.ClientOnly ? 1 : 0);
        }
    }
    if (G.ImitationAIEnabled)
    {
        if (G.ImitationAITorchCompactModelLoaded)
        {
            std::printf(
                "NSMB ImitationAI: enabled player=%s startFrame=%u modelType=torchCompact allowedHeldMask=0x%03X trace=%d traceInterval=%d inferInterval=%d neutralHoldFrames=%d model=%s features=%zu heads=%zu schema=%s labelSchema=%s\n",
                G.AI.Imitation.PlayerSpec.c_str(),
                G.AI.Imitation.StartFrame,
                G.AI.Imitation.AllowedHeldMask,
                G.AI.Imitation.TraceEnabled ? 1 : 0,
                G.AI.Imitation.TraceInterval,
                G.AI.Imitation.InferInterval,
                G.AI.Imitation.NeutralHoldFrames,
                G.AI.Imitation.ModelPath.c_str(),
                G.ImitationAITorchCompactModel.FeatureCount(),
                G.ImitationAITorchCompactModel.Heads.size(),
                G.ImitationAITorchCompactModel.Schema.c_str(),
                G.ImitationAITorchCompactModel.LabelSchema.c_str());
        }
        else if (G.ImitationAICompactModelLoaded)
        {
            std::printf(
                "NSMB ImitationAI: enabled player=%s startFrame=%u modelType=compact allowedHeldMask=0x%03X trace=%d traceInterval=%d model=%s features=%zu heads=%zu schema=%s labelSchema=%s\n",
                G.AI.Imitation.PlayerSpec.c_str(),
                G.AI.Imitation.StartFrame,
                G.AI.Imitation.AllowedHeldMask,
                G.AI.Imitation.TraceEnabled ? 1 : 0,
                G.AI.Imitation.TraceInterval,
                G.AI.Imitation.ModelPath.c_str(),
                G.ImitationAICompactModel.FeatureCount(),
                G.ImitationAICompactModel.Heads.size(),
                G.ImitationAICompactModel.Schema.c_str(),
                G.ImitationAICompactModel.LabelSchema.c_str());
        }
        else
        {
            std::printf(
                "NSMB ImitationAI: enabled player=%s startFrame=%u modelType=linear threshold=%.3f allowedHeldMask=0x%03X trace=%d traceInterval=%d model=%s features=%zu buttons=%zu schema=%s featureSchema=%s\n",
                G.AI.Imitation.PlayerSpec.c_str(),
                G.AI.Imitation.StartFrame,
                G.AI.Imitation.Threshold,
                G.AI.Imitation.AllowedHeldMask,
                G.AI.Imitation.TraceEnabled ? 1 : 0,
                G.AI.Imitation.TraceInterval,
                G.AI.Imitation.ModelPath.c_str(),
                G.ImitationAIModel.FeatureCount(),
                G.ImitationAIModel.ButtonCount(),
                G.ImitationAIModel.Schema.c_str(),
                G.ImitationAIModel.FeatureSchemaID.c_str());
        }
        if (G.AI.Imitation.HostOnly || G.AI.Imitation.ClientOnly)
        {
            std::printf("NSMB ImitationAI: roleFilter hostOnly=%d clientOnly=%d\n",
                G.AI.Imitation.HostOnly ? 1 : 0,
                G.AI.Imitation.ClientOnly ? 1 : 0);
        }
        std::printf(
            "NSMB ImitationAI: hazardGuard enabled=%d horizontalRange=0x%X verticalRange=0x%X closeRange=0x%X\n",
            G.AI.Imitation.HazardGuardEnabled ? 1 : 0,
            G.AI.Imitation.HazardGuardHorizontalRange,
            G.AI.Imitation.HazardGuardVerticalRange,
            G.AI.Imitation.HazardGuardCloseRange);
    }
    std::fflush(stdout);
}

class BeforeHookPhaseTrace
{
public:
    enum class Phase
    {
        Init,
        StartSync,
        LoadState,
        RuntimeConfig,
        ProbeRestore,
        JitPatch,
        Rollback,
        Boot,
        Patch,
        PacketBridgeSetup,
        TestSnap,
        Setup,
        ActorState,
        Barrier,
        Checkpoint,
        Scratch,
        Network,
        Gate,
        RemoteWait,
    };

    BeforeHookPhaseTrace(int instanceID, melonDS::u32 frame)
        : Enabled(G.ActiveFrameSpikeTrace)
        , InstanceID(instanceID)
        , Frame(frame)
        , Start(std::chrono::steady_clock::now())
        , Last(Start)
    {
    }

    ~BeforeHookPhaseTrace()
    {
        if (!Enabled)
            return;

        const auto now = std::chrono::steady_clock::now();
        const auto tailUs = ElapsedUs(Last, now);
        const auto totalUs = ElapsedUs(Start, now);
        if (totalUs < std::min(G.ActiveFrameSpikeThresholdUs, 10000))
            return;

        std::printf(
            "NSMB BeforeHookPhaseSpike: inst=%d frame=%u totalMs=%.3f initMs=%.3f startSyncMs=%.3f loadStateMs=%.3f runtimeConfigMs=%.3f probeRestoreMs=%.3f jitPatchMs=%.3f rollbackMs=%.3f bootMs=%.3f patchMs=%.3f packetBridgeSetupMs=%.3f testSnapMs=%.3f setupMs=%.3f actorStateMs=%.3f barrierMs=%.3f checkpointMs=%.3f scratchMs=%.3f networkMs=%.3f gateMs=%.3f remoteWaitMs=%.3f tailMs=%.3f\n",
            InstanceID,
            Frame,
            static_cast<double>(totalUs) / 1000.0,
            static_cast<double>(InitUs) / 1000.0,
            static_cast<double>(StartSyncUs) / 1000.0,
            static_cast<double>(LoadStateUs) / 1000.0,
            static_cast<double>(RuntimeConfigUs) / 1000.0,
            static_cast<double>(ProbeRestoreUs) / 1000.0,
            static_cast<double>(JitPatchUs) / 1000.0,
            static_cast<double>(RollbackUs) / 1000.0,
            static_cast<double>(BootUs) / 1000.0,
            static_cast<double>(PatchUs) / 1000.0,
            static_cast<double>(PacketBridgeSetupUs) / 1000.0,
            static_cast<double>(TestSnapUs) / 1000.0,
            static_cast<double>(SetupUs) / 1000.0,
            static_cast<double>(ActorStateUs) / 1000.0,
            static_cast<double>(BarrierUs) / 1000.0,
            static_cast<double>(CheckpointUs) / 1000.0,
            static_cast<double>(ScratchUs) / 1000.0,
            static_cast<double>(NetworkUs) / 1000.0,
            static_cast<double>(GateUs) / 1000.0,
            static_cast<double>(RemoteWaitUs) / 1000.0,
            static_cast<double>(tailUs) / 1000.0);
        std::fflush(stdout);
    }

    void SetFrame(melonDS::u32 frame)
    {
        Frame = frame;
    }

    void Mark(Phase phase)
    {
        if (!Enabled)
            return;

        const auto now = std::chrono::steady_clock::now();
        const auto elapsedUs = ElapsedUs(Last, now);
        Last = now;
        switch (phase)
        {
        case Phase::Init: InitUs += elapsedUs; break;
        case Phase::StartSync: StartSyncUs += elapsedUs; break;
        case Phase::LoadState: LoadStateUs += elapsedUs; break;
        case Phase::RuntimeConfig: RuntimeConfigUs += elapsedUs; break;
        case Phase::ProbeRestore: ProbeRestoreUs += elapsedUs; break;
        case Phase::JitPatch: JitPatchUs += elapsedUs; break;
        case Phase::Rollback: RollbackUs += elapsedUs; break;
        case Phase::Boot: BootUs += elapsedUs; break;
        case Phase::Patch: PatchUs += elapsedUs; break;
        case Phase::PacketBridgeSetup: PacketBridgeSetupUs += elapsedUs; break;
        case Phase::TestSnap: TestSnapUs += elapsedUs; break;
        case Phase::Setup: SetupUs += elapsedUs; break;
        case Phase::ActorState: ActorStateUs += elapsedUs; break;
        case Phase::Barrier: BarrierUs += elapsedUs; break;
        case Phase::Checkpoint: CheckpointUs += elapsedUs; break;
        case Phase::Scratch: ScratchUs += elapsedUs; break;
        case Phase::Network: NetworkUs += elapsedUs; break;
        case Phase::Gate: GateUs += elapsedUs; break;
        case Phase::RemoteWait: RemoteWaitUs += elapsedUs; break;
        }
    }

private:
    using Clock = std::chrono::steady_clock;

    static long long ElapsedUs(Clock::time_point start, Clock::time_point end)
    {
        return std::max<long long>(
            0,
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
    }

    bool Enabled = false;
    int InstanceID = -1;
    melonDS::u32 Frame = 0;
    Clock::time_point Start;
    Clock::time_point Last;
    long long InitUs = 0;
    long long StartSyncUs = 0;
    long long LoadStateUs = 0;
    long long RuntimeConfigUs = 0;
    long long ProbeRestoreUs = 0;
    long long JitPatchUs = 0;
    long long RollbackUs = 0;
    long long BootUs = 0;
    long long PatchUs = 0;
    long long PacketBridgeSetupUs = 0;
    long long TestSnapUs = 0;
    long long SetupUs = 0;
    long long ActorStateUs = 0;
    long long BarrierUs = 0;
    long long CheckpointUs = 0;
    long long ScratchUs = 0;
    long long NetworkUs = 0;
    long long GateUs = 0;
    long long RemoteWaitUs = 0;
};

bool CanRunFrameHooks(int instanceID, melonDS::NDS* nds)
{
    return (G.TestEnabled || G.Enabled)
        && instanceID >= 0
        && instanceID < 16
        && nds;
}

void RunBeforeFrameRuntimeConfigPhase(int instanceID, melonDS::NDS* nds)
{
    if (!CanRunFrameHooks(instanceID, nds))
        return;
    RefreshMvlGameSelectionForInstance(instanceID);
    ApplyMvlRuntimeConfigIfNeeded(nds);
}

void RunBeforeFrameBootPhase(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!CanRunFrameHooks(instanceID, nds))
        return;
    SaveMvlAutoRestartBootstrapCheckpointIfNeeded(instanceID, frame, nds);
    InjectDirectMvlBootCall(instanceID, frame, nds);
    RestartMvlAfterResultIfNeeded(instanceID, frame, nds);
}

void RunBeforeFramePatchPhase(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!CanRunFrameHooks(instanceID, nds))
        return;
    if (G.TestEnabled)
        ApplyMemPatch(instanceID, frame, nds);
    ApplyNetRandomPatch(instanceID, frame, nds);
}

void RunBeforeFrameSetupPhase(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!CanRunFrameHooks(instanceID, nds))
        return;
    PushScriptRemotePacketIfNeeded(instanceID, frame, nds);
    NormalizeMvlEntranceSpawnStateIfNeeded(instanceID, frame, nds);
    ClearMvlCameraInitHoldIfNeeded(instanceID, frame, nds);
    ForcePlayerDeathCountersIfNeeded(instanceID, frame, nds);
    ForcePlayerPowerupsIfNeeded(instanceID, frame, nds);
    ForcePlayerInventoryPowerupsIfNeeded(instanceID, frame, nds);
    ForcePlayerStarCountersIfNeeded(instanceID, frame, nds);
}

void RunBeforeFrameActorStatePhase(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.Enabled || instanceID < 0 || instanceID >= 16 || !nds)
        return;
    ApplyRemoteGameState(instanceID, frame, nds);
    ApplyRemoteMovingHazardState(instanceID, frame, nds);
    ApplyRemoteWorldActorSnapshotState(instanceID, frame, nds);
    ApplyRemoteWorldState(instanceID, frame, nds);
    ApplyRemoteWorldEffectState(instanceID, frame, nds);
    ApplyRemotePlayerState(instanceID, frame, nds);
    SyncWorldState(instanceID, frame, nds);
    SyncWorldEffectState(instanceID, frame, nds);
    SyncMovingHazardState(instanceID, frame, nds);
    SyncWorldActorSnapshotState(instanceID, frame, nds);
    SyncPlayerState(instanceID, frame, nds);
}

InputState BeforeRunFrame(int instanceID, melonDS::u32 frame, melonDS::NDS* nds, const InputState& polledInput)
{
    BeforeHookPhaseTrace phaseTrace(instanceID, frame);
    InitFromEnvironment();
    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::Init);
    melonDS::u32 inputFrame = frame;
    if (G.TestEnabled && instanceID >= 0 && instanceID < 16)
        inputFrame = G.TestFrameCount[instanceID];
    phaseTrace.SetFrame(inputFrame);

    if (G.Enabled && G.Input.NetplayOnly && G.Harness.WaitForPeerBeforeStart && inputFrame == 0
        && !RuleAIProvidesInputForPlayer(CurrentPacketBridgeLocalPlayer() ^ 1)
        && !ImitationAIProvidesInputForPlayer(CurrentPacketBridgeLocalPlayer() ^ 1))
    {
        if (!G.Harness.WaitForPeerAtNetplayStart)
            WaitForPeerIfNeeded(true);
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            PumpNetworkLocked(nds, inputFrame);
            SendMatchSeedLocked();
        }
        WaitForMatchSeedIfNeeded();
    }
    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::StartSync);

    if (G.TestEnabled && instanceID >= 0 && instanceID < 16 && nds)
        LoadState(instanceID, inputFrame, nds);
    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::LoadState);

    RunBeforeFrameRuntimeConfigPhase(instanceID, nds);
    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::RuntimeConfig);

    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        RestoreRollbackCheckpointForProbeIfNeeded(instanceID, inputFrame, nds);
    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::ProbeRestore);

    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ApplyPacketBridgeJitHelperPatchIfNeeded(instanceID, inputFrame, nds);
    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::JitPatch);

    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        RollbackResimulateIfNeeded(instanceID, inputFrame, nds);
    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::Rollback);

    RunBeforeFrameBootPhase(instanceID, inputFrame, nds);
    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::Boot);

    RunBeforeFramePatchPhase(instanceID, inputFrame, nds);
    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::Patch);

    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
    {
        ForceNSMLPacketBridgeNetReadyIfNeeded(instanceID, inputFrame, nds);
        ForceNSMLGameLocalPlayerIDIfNeeded(inputFrame, nds);
    }
    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::PacketBridgeSetup);

    if (G.TestEnabled && instanceID >= 0 && instanceID < 16 && nds)
        ApplyPlayerStickToStar(instanceID, inputFrame, nds);
    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::TestSnap);
    RunBeforeFrameSetupPhase(instanceID, inputFrame, nds);

    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::Setup);
    RunBeforeFrameActorStatePhase(instanceID, inputFrame, nds);

    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::ActorState);
    WaitForSerialRunTurn(instanceID, inputFrame);
    WaitAtFrameBarrier(GBeforeFrameBarrier, instanceID, inputFrame, "before");
    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::Barrier);

    const InputState inputFallback = G.Harness.NeutralizePolledInput
        ? (G.Harness.NeutralizePolledInputPreserveTouch ? NeutralInputPreservingTouch(polledInput) : NeutralInput())
        : polledInput;
    InputState testInput = ApplyInputScript(instanceID, inputFrame, inputFallback);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        testInput = ApplyRuleBasedAIInput(
            instanceID,
            inputFrame,
            nds,
            CurrentPacketBridgeLocalPlayer(),
            testInput);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        testInput = ApplyImitationAIInput(
            instanceID,
            inputFrame,
            nds,
            CurrentPacketBridgeLocalPlayer(),
            testInput);
    RecordAIPlayLogAppliedInput(
        instanceID,
        inputFrame,
        CurrentPacketBridgeLocalPlayer(),
        testInput);
    RecordInputIfNeeded(instanceID, inputFrame, testInput);
    const melonDS::u32 syncFrame = G.TestEnabled ? inputFrame : frame;

    if (G.Enabled && G.Input.NetplayOnly)
        WaitForRemoteNetplayStartReadyIfNeeded(nds, syncFrame);

    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        SaveRollbackCheckpointIfNeeded(instanceID, syncFrame, nds);
    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::Checkpoint);

    const bool pauseInputNetplayForRestart = G.Enabled
        && ShouldPauseInputNetplayForMvlAutoRestart(instanceID, nds);
    if (!pauseInputNetplayForRestart
        && (G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        WritePacketBridgeJitScratchIfNeeded(instanceID, syncFrame, nds, testInput);
    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::Scratch);

    if (G.Enabled && G.Input.NetplayOnly)
        return ConvertStockXToTouch(testInput);

    if (!G.Enabled || !G.Ready)
        return (G.TestEnabled || G.Enabled) ? ConvertStockXToTouch(testInput) : testInput;
    if (syncFrame == 0 && G.PacketBridge.Only)
    {
        WaitForPeerIfNeeded();
    }
    else if (syncFrame == 0)
    {
        WaitForPeerIfNeeded();
        WaitForMatchSeedIfNeeded();
    }

    if (G.PacketBridge.Enabled && G.PacketBridge.Only)
    {
        const bool bridgeNetworkActive =
            !G.Harness.DeferNetworkUntilStart || G.Connection.StartFrame == 0 || syncFrame >= G.Connection.StartFrame;
        InputState packetBridgeInput = testInput;
        if (bridgeNetworkActive && G.PacketBridge.LocalInputDelay > 0)
        {
            const melonDS::u32 inputDelay = static_cast<melonDS::u32>(G.PacketBridge.LocalInputDelay);
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
        if (bridgeNetworkActive && G.PacketBridge.NeutralizeLocalInput)
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            G.PacketBridgePacketInputs[syncFrame] = testInput;
            packetBridgeInput = G.PacketBridge.PreserveLocalTouch
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
            ForceNSMLGameLocalPlayerIDIfNeeded(syncFrame, nds);
            melonDS::NSML_RefreshMarioVsLuigiPacketSlots(nds);
            ForceNSMLGameLocalPlayerIDIfNeeded(syncFrame, nds);
            ThrottleNSMLPacketBridgeLead(nds, syncFrame);
            WaitForNSMLPacketBridgeRemote(nds, syncFrame);
        }
        return ConvertStockXToTouch(packetBridgeInput);
    }

    const bool isLocal = (instanceID == G.Connection.LocalInstance);
    const melonDS::u32 delay = static_cast<melonDS::u32>(G.Connection.Delay);
    const melonDS::u32 sendStartFrame = (G.Connection.StartFrame > delay)
        ? G.Connection.StartFrame - delay
        : 0;
    const bool netplaySendActive = (G.Connection.StartFrame == 0 || syncFrame >= sendStartFrame);
    const bool netplayApplyActive = (G.Connection.StartFrame == 0 || syncFrame >= G.Connection.StartFrame);
    const bool networkPumpActive = ShouldPumpNetworkAtFrame(syncFrame, sendStartFrame);

    if ((isLocal || G.TestEnabled) && networkPumpActive)
    {
        InputState localInput = testInput;
        if (!isLocal && G.TestEnabled)
            localInput = ApplyInputScript(G.Connection.LocalInstance, syncFrame, NeutralInput());

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
    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::Network);

    if (!netplayApplyActive)
        return ConvertStockXToTouch(testInput);

    if (G.Connection.StartFrame != 0
        && G.Connection.WarmupFrames > 0
        && syncFrame < G.Connection.StartFrame + static_cast<melonDS::u32>(G.Connection.WarmupFrames))
    {
        return ConvertStockXToTouch(testInput);
    }

    const melonDS::u32 targetFrame = syncFrame;

    if (G.Harness.NetplayFrameBarrierEnabled)
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
        if (!G.Connection.LocalWaitsForRemote)
            return ConvertStockXToTouch(delayedLocalInput);
        if (IsPastTestInputRange(targetFrame))
            return ConvertStockXToTouch(delayedLocalInput);
    }
    else if (IsPastTestInputRange(targetFrame))
    {
        return NeutralInput();
    }

    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::Gate);
    const InputState remoteInput = WaitForRemoteInput(targetFrame);
    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::RemoteWait);

    if (isLocal)
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        auto it = G.LocalInputs.find(targetFrame);
        return ConvertStockXToTouch(it != G.LocalInputs.end() ? it->second : NeutralInput());
    }

    return ConvertStockXToTouch(remoteInput);
}

void TracePlayerLifeChanges(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if ((!G.RuntimePatch.TracePlayerLifeChanges && !G.DiagnosticEventsEnabled) || !nds || !nds->MainRAM) return;
    if (instanceID < 0 || instanceID >= 16) return;

    GameStateSample& last = G.LastPlayerLifeSample[instanceID];
    const bool valid = G.LastPlayerLifeSampleValid[instanceID];
    const melonDS::u32 oldPlayer0Lives = last.Player0Lives;
    const melonDS::u32 oldPlayer1Lives = last.Player1Lives;
    const melonDS::u32 oldPlayer0Deaths = last.Player0Deaths;
    const melonDS::u32 oldPlayer1Deaths = last.Player1Deaths;
    const melonDS::u32 oldPlayer0Dead = last.Player0Dead;
    const melonDS::u32 oldPlayer1Dead = last.Player1Dead;
    const melonDS::u32 oldTransition0 = last.PlayerTransitionStatus0;
    const melonDS::u32 oldTransition1 = last.PlayerTransitionStatus1;
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
        if (G.RuntimePatch.TracePlayerLifeChanges)
        {
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

        if (valid)
        {
            const bool player0DeathEvent =
                (sample.PlayerActor0Found != 0 || player0Deaths != 0 || player0Dead != 0) &&
                (player0Deaths > oldPlayer0Deaths ||
                    player0Lives < oldPlayer0Lives ||
                    (player0Dead != oldPlayer0Dead && player0Dead != 0));
            const bool player1DeathEvent =
                (sample.PlayerActor1Found != 0 || player1Deaths != 0 || player1Dead != 0) &&
                (player1Deaths > oldPlayer1Deaths ||
                    player1Lives < oldPlayer1Lives ||
                    (player1Dead != oldPlayer1Dead && player1Dead != 0));
            const bool player0TransitionEvent =
                !player0DeathEvent && transition0 != oldTransition0 &&
                (sample.PlayerActor0DefeatedFlag != 0 || sample.PlayerActor0TransitioningFlag != 0);
            const bool player1TransitionEvent =
                !player1DeathEvent && transition1 != oldTransition1 &&
                (sample.PlayerActor1DefeatedFlag != 0 || sample.PlayerActor1TransitioningFlag != 0);
            if (player0DeathEvent)
                EmitPlayerLifeEvent(instanceID, frame, 0, "death", sample, nds);
            else if (player0TransitionEvent)
                EmitPlayerLifeEvent(instanceID, frame, 0, "death-transition", sample, nds);
            if (player1DeathEvent)
                EmitPlayerLifeEvent(instanceID, frame, 1, "death", sample, nds);
            else if (player1TransitionEvent)
                EmitPlayerLifeEvent(instanceID, frame, 1, "death-transition", sample, nds);
        }
    }
    G.LastPlayerLifeSampleValid[instanceID] = true;
}

void TraceGameplayHeartbeatIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (G.GameplayHeartbeatInterval <= 0 || !nds || !nds->MainRAM)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (frame < G.Connection.StartFrame)
        return;
    if (frame == G.LastGameplayHeartbeat[instanceID] ||
        (frame % static_cast<melonDS::u32>(G.GameplayHeartbeatInterval)) != 0)
        return;

    G.LastGameplayHeartbeat[instanceID] = frame;
    const GameStateObjectScanCache cache = BuildGameStateObjectScanCache(nds);
    const ScopedGameStateObjectScanCache scopedCache(cache);
    const PlayerActorScanSample players = FindPlayerActors(nds);
    const ObjectLifecycleSummary objects = SummarizeObjectLifecycle(nds);
    std::printf(
        "NSMB GameplayHeartbeat: role=%s inst=%d frame=%u "
        "p0=%u/%08X/%08X/%08X/%08X/%08X "
        "p1=%u/%08X/%08X/%08X/%08X/%08X "
        "objects=%u/%u/%u/%u/%u/%u",
        G.NetRole == Role::Host ? "host" : "client",
        instanceID,
        frame,
        players.Actor0.Found,
        players.Actor0.PosX,
        players.Actor0.PosY,
        players.Actor0.VelX,
        players.Actor0.VelY,
        players.Actor0.Flags,
        players.Actor1.Found,
        players.Actor1.PosX,
        players.Actor1.PosY,
        players.Actor1.VelX,
        players.Actor1.VelY,
        players.Actor1.Flags,
        objects.Total,
        objects.Active,
        objects.Dead,
        objects.NotCreated,
        objects.SkipUpdate,
        objects.SkipRender);
    std::printf(" activeIds=");
    for (std::size_t i = 0; i < kObjectTraceSlots; i++)
    {
        if (i != 0)
            std::printf(",");
        std::printf("%03X:%08X", objects.ActiveID[i], objects.ActiveSettings[i]);
    }
    const std::vector<ObjectScanSample> hazards =
        FindActiveObjectsByIDAndSettings(nds, kVsMovingHazardObjectID, kVsMovingHazardSettings);
    std::printf(" hazards=");
    const std::size_t hazardCount = std::min(hazards.size(), kMaxWorldMovingHazards);
    for (std::size_t i = 0; i < kMaxWorldMovingHazards; i++)
    {
        if (i != 0)
            std::printf(",");
        if (i >= hazardCount)
        {
            std::printf("-");
            continue;
        }
        const ObjectScanSample& hazard = hazards[i];
        std::printf(
            "%u:%08X:%08X:%08X:%u:%08X",
            hazard.GUID,
            hazard.PosX,
            hazard.PosY,
            hazard.VelX,
            hazard.StateType,
            hazard.Flags);
    }
    std::printf("\n");
    std::fflush(stdout);
}

melonDS::u32 PrepareAfterFrameLogFrame(int instanceID, melonDS::u32 frame)
{
    if (!G.TestEnabled)
        return frame;

    const melonDS::u32 logFrame = ++G.TestFrameCount[instanceID];
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
        : (G.Connection.StartFrame != 0
            ? G.Connection.StartFrame + 120
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
    RecordActiveFrameTiming(instanceID, logFrame);
    const bool heartbeatActive = G.ActiveTimerStarted[instanceID] || logFrame >= activeStartFrame;
    if (G.FrameHeartbeatInterval > 0
        && heartbeatActive
        && logFrame != G.LastFrameHeartbeat[instanceID]
        && (logFrame % static_cast<melonDS::u32>(G.FrameHeartbeatInterval)) == 0)
    {
        G.LastFrameHeartbeat[instanceID] = logFrame;
        std::printf("NSMB Heartbeat: inst=%d frame=%u\n", instanceID, logFrame);
        if (G.FrameHeartbeat)
            G.PendingFrameHeartbeat.store(logFrame, std::memory_order_release);
        else
            std::fflush(stdout);
    }
    return logFrame;
}

void RunAfterFramePacketBridgePhase(melonDS::u32 logFrame, melonDS::NDS* nds)
{
    const bool bridgeNetworkActive =
        !G.Harness.DeferNetworkUntilStart || G.Connection.StartFrame == 0 || logFrame >= G.Connection.StartFrame;
    if (G.Enabled && G.PacketBridge.Enabled && bridgeNetworkActive)
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        PumpNSMLPacketBridgeLocked(nds, logFrame);
        ForceNSMLGameLocalPlayerIDIfNeeded(logFrame, nds);
        CaptureAndSendNSMLPacketLocked(logFrame, nds);
    }
    if (G.Enabled && G.PacketBridge.Enabled && bridgeNetworkActive)
    {
        ForceNSMLGameLocalPlayerIDIfNeeded(logFrame, nds);
        melonDS::NSML_RefreshMarioVsLuigiPacketSlots(nds);
        ForceNSMLGameLocalPlayerIDIfNeeded(logFrame, nds);
    }
    if (G.Enabled && G.PacketBridge.Enabled && bridgeNetworkActive)
        ThrottleNSMLPacketBridgeFrameLead(nds, logFrame);
}

void TraceRollbackStatsIfNeeded(melonDS::u32 logFrame)
{
    if (!G.Rollback.Enabled
        || !G.Input.NetplayTrace
        || logFrame == G.LastRollbackTraceFrame
        || (logFrame % 120) != 0)
        return;

    std::lock_guard<std::mutex> lock(G.Mutex);
    G.LastRollbackTraceFrame = logFrame;
    const size_t avgBytes = G.RollbackCheckpointSaveCount == 0
        ? 0
        : static_cast<size_t>(G.RollbackCheckpointTotalBytes / G.RollbackCheckpointSaveCount);
    const unsigned long long saveAvgUs = G.RollbackCheckpointSaveCount == 0
        ? 0
        : G.RollbackCheckpointSaveTotalUs / G.RollbackCheckpointSaveCount;
    const unsigned long long restoreAvgUs = G.RollbackCheckpointRestoreOpCount == 0
        ? 0
        : G.RollbackCheckpointRestoreTotalUs / G.RollbackCheckpointRestoreOpCount;
    const unsigned long long resimRunAvgUs = G.RollbackMeasuredResimFrameCount == 0
        ? 0
        : G.RollbackResimRunFrameTotalUs / G.RollbackMeasuredResimFrameCount;
    const unsigned long long resimCheckpointSaveAvgUs = G.RollbackMeasuredResimFrameCount == 0
        ? 0
        : G.RollbackResimCheckpointSaveTotalUs / G.RollbackMeasuredResimFrameCount;
    const unsigned long long resimTotalAvgUs = G.RollbackMeasuredResimOpCount == 0
        ? 0
        : G.RollbackResimCorrectionTotalUs / G.RollbackMeasuredResimOpCount;
    size_t deltaCheckpoints = 0;
    size_t keyframeCheckpoints = 0;
    size_t preimageCheckpoints = 0;
    size_t preimageBytes = 0;
    size_t mainRAMCopyBytes = 0;
    for (const auto& [storedFrame, stored] : G.RollbackStore.States())
    {
        (void)storedFrame;
        if (stored.MainRAMDelta)
            deltaCheckpoints++;
        if (stored.MainRAMFramePreimage)
        {
            preimageCheckpoints++;
            preimageBytes += stored.MainRAMPreimagePages.size() * sizeof(melonDS::u32)
                + stored.MainRAMPreimage.size();
        }
        else if ((G.Rollback.Backend == RollbackBackend::CoreDelta
            || G.Rollback.Backend == RollbackBackend::CoreFrameDelta)
            && !stored.Buffer.empty())
            keyframeCheckpoints++;
        mainRAMCopyBytes += stored.MainRAMCopy.size();
    }
    std::printf(
        "NSMB Rollback: frame=%u backend=%s checkpoints=%zu checkpointSaves=%u bytesLast=%zu bytesMin=%zu bytesMax=%zu bytesAvg=%zu saveAvgUs=%llu saveMaxUs=%llu restoreOps=%u restoreAvgUs=%llu restoreMaxUs=%llu resimOps=%u resimFrames=%llu resimRunAvgUs=%llu resimRunMaxUs=%llu resimSaveAvgUs=%llu resimSaveMaxUs=%llu resimTotalAvgUs=%llu resimTotalMaxUs=%llu delta=%zu keyframes=%zu preimages=%zu preimageBytes=%zu mainRAMCopies=%zu keyInt=%d page=%d coreSkip=0x%X tinyFlags=0x%X predicted=%zu predictions=%u predProbe=%u mismatches=%u restores=%u resims=%u pending=%u observed=%u\n",
        logFrame,
        RollbackBackendName(),
        G.RollbackStore.Size(),
        G.RollbackCheckpointSaveCount,
        G.RollbackCheckpointLastBytes,
        G.RollbackCheckpointMinBytes,
        G.RollbackCheckpointMaxBytes,
        avgBytes,
        saveAvgUs,
        G.RollbackCheckpointSaveMaxUs,
        G.RollbackCheckpointRestoreOpCount,
        restoreAvgUs,
        G.RollbackCheckpointRestoreMaxUs,
        G.RollbackMeasuredResimOpCount,
        G.RollbackMeasuredResimFrameCount,
        resimRunAvgUs,
        G.RollbackResimRunFrameMaxUs,
        resimCheckpointSaveAvgUs,
        G.RollbackResimCheckpointSaveMaxUs,
        resimTotalAvgUs,
        G.RollbackResimCorrectionMaxUs,
        deltaCheckpoints,
        keyframeCheckpoints,
        preimageCheckpoints,
        preimageBytes,
        mainRAMCopyBytes,
        G.Rollback.DeltaKeyframeInterval,
        G.Rollback.MainRAMPageSize,
        G.Rollback.CoreSkipMask,
        G.Rollback.TinyCoreFlags,
        G.PredictedRemoteInputs.size(),
        G.RollbackPredictionCount,
        G.RollbackPredictionProbeCount,
        G.RollbackMismatchCount,
        G.RollbackRestoreCount,
        G.RollbackResimulateCount,
        G.PendingRollbackFrame,
        G.PendingRollbackObservedFrame);
    std::fflush(stdout);
}

void RunAfterFrameRuntimePatchPhase(int instanceID, melonDS::u32 logFrame, melonDS::NDS* nds)
{
    if (!CanRunFrameHooks(instanceID, nds))
        return;

    ForcePlayerDeathCountersIfNeeded(instanceID, logFrame, nds);
    NormalizeMvlEntranceSpawnStateIfNeeded(instanceID, logFrame, nds);
    ForcePlayerPowerupsIfNeeded(instanceID, logFrame, nds);
    ForcePlayerInventoryPowerupsIfNeeded(instanceID, logFrame, nds);
    ForcePlayerStarCountersIfNeeded(instanceID, logFrame, nds);
    PushScriptRemotePacketIfNeeded(instanceID, logFrame, nds);
    SaveMvlAutoRestartCheckpointIfNeeded(instanceID, logFrame, nds);
}

void SaveAfterFrameArtifacts(int instanceID, melonDS::u32 logFrame, melonDS::NDS* nds)
{
    SaveState(instanceID, logFrame, nds);
    SaveLocalMPState(logFrame);
    SaveScreenshot(instanceID, logFrame, nds);
    SaveRamDump(instanceID, logFrame, nds);
}

void AfterRunFrame(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    const auto afterHookCallStart = std::chrono::steady_clock::now();
    InitFromEnvironment();
    const auto afterInit = std::chrono::steady_clock::now();
    if ((!G.Enabled && !G.TestEnabled) || !nds) return;

    if (instanceID < 0 || instanceID >= 16) return;

    const melonDS::u32 logFrame = PrepareAfterFrameLogFrame(instanceID, frame);
    const auto afterHeartbeat = std::chrono::steady_clock::now();
    TraceHangPhase("begin", "after-frame", instanceID, logFrame, logFrame, logFrame);
    UpdateHangGameSnapshot(instanceID, logFrame, nds);

    TraceHangPhase("begin", "gameplay-heartbeat", instanceID, logFrame, logFrame, logFrame);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        TraceGameplayHeartbeatIfNeeded(instanceID, logFrame, nds);

    TraceHangPhase("begin", "after-frame-barriers", instanceID, logFrame, logFrame, logFrame);
    WaitAtFrameBarrier(GAfterFrameBarrier, instanceID, logFrame, "after");
    AdvanceSerialRunTurn(instanceID, logFrame - 1);
    WaitForPeerAtNetplayStartBarrier(instanceID, logFrame);
    const auto afterBarrier = std::chrono::steady_clock::now();

    TraceHangPhase("begin", "apply-remote-game-state", instanceID, logFrame, logFrame, logFrame);
    if (G.Enabled)
        ApplyRemoteGameState(instanceID, logFrame, nds);

    TraceHangPhase("begin", "packet-bridge", instanceID, logFrame, logFrame, logFrame);
    RunAfterFramePacketBridgePhase(logFrame, nds);
    const auto afterBridge = std::chrono::steady_clock::now();

    TraceHangPhase("begin", "life-trace", instanceID, logFrame, logFrame, logFrame);
    if (CanRunFrameHooks(instanceID, nds))
        TracePlayerLifeChanges(instanceID, logFrame, nds);
    const auto afterLifeTrace = std::chrono::steady_clock::now();

    TraceHangPhase("begin", "diagnostic-snapshot", instanceID, logFrame, logFrame, logFrame);
    if (CanRunFrameHooks(instanceID, nds))
        RecordDiagnosticSnapshotIfNeeded(instanceID, logFrame, nds);
    const auto afterDiagnosticSnapshot = std::chrono::steady_clock::now();

    TraceHangPhase("begin", "rollback-trace", instanceID, logFrame, logFrame, logFrame);
    TraceRollbackStatsIfNeeded(logFrame);
    const auto afterRollbackTrace = std::chrono::steady_clock::now();

    TraceHangPhase("begin", "runtime-force", instanceID, logFrame, logFrame, logFrame);
    RunAfterFrameRuntimePatchPhase(instanceID, logFrame, nds);
    const auto afterRuntimeForce = std::chrono::steady_clock::now();

    TraceHangPhase("begin", "artifacts", instanceID, logFrame, logFrame, logFrame);
    SaveAfterFrameArtifacts(instanceID, logFrame, nds);
    const auto afterArtifacts = std::chrono::steady_clock::now();
    const auto afterPreSnapshot = std::chrono::steady_clock::now();
    TraceHangPhase("begin", "sync-world", instanceID, logFrame, logFrame, logFrame);
    if (G.Enabled && instanceID >= 0 && instanceID < 16 && nds)
        SyncWorldState(instanceID, logFrame, nds);
    if (G.Enabled && instanceID >= 0 && instanceID < 16 && nds)
        SyncWorldEffectState(instanceID, logFrame, nds);
    if (G.Enabled && instanceID >= 0 && instanceID < 16 && nds)
        SyncMovingHazardState(instanceID, logFrame, nds);
    if (G.Enabled && instanceID >= 0 && instanceID < 16 && nds)
        SyncWorldActorSnapshotState(instanceID, logFrame, nds);
    if (G.Enabled && instanceID >= 0 && instanceID < 16 && nds)
        SyncPlayerState(instanceID, logFrame, nds);
    TraceHangPhase("begin", "apply-hazard", instanceID, logFrame, logFrame, logFrame);
    if (G.Enabled && instanceID >= 0 && instanceID < 16 && nds)
        ApplyRemoteMovingHazardState(instanceID, logFrame, nds, true);
    const auto afterApplyHazard = std::chrono::steady_clock::now();
    TraceHangPhase("begin", "apply-world", instanceID, logFrame, logFrame, logFrame);
    if (G.Enabled && instanceID >= 0 && instanceID < 16 && nds)
        ApplyRemoteWorldActorSnapshotState(instanceID, logFrame, nds);
    if (G.Enabled && instanceID >= 0 && instanceID < 16 && nds)
        ApplyRemoteWorldState(instanceID, logFrame, nds);
    if (G.Enabled && instanceID >= 0 && instanceID < 16 && nds)
        ApplyRemoteWorldEffectState(instanceID, logFrame, nds);
    const auto afterApplyWorld = std::chrono::steady_clock::now();
    TraceHangPhase("begin", "apply-player", instanceID, logFrame, logFrame, logFrame);
    if (G.Enabled && instanceID >= 0 && instanceID < 16 && nds)
        ApplyRemotePlayerState(instanceID, logFrame, nds, true);
    const auto afterApplyPlayer = std::chrono::steady_clock::now();
    TraceHangPhase("begin", "game-state-trace", instanceID, logFrame, logFrame, logFrame);
    TraceGameState(instanceID, logFrame, nds);
    TraceAIPlayLog(instanceID, logFrame, nds);
    const auto afterTrace = std::chrono::steady_clock::now();
    TraceHangPhase("begin", "sync-game", instanceID, logFrame, logFrame, logFrame);
    SyncGameState(instanceID, logFrame, nds);
    const auto afterSyncGame = std::chrono::steady_clock::now();
    TraceHangPhase("begin", "sync-world-tail", instanceID, logFrame, logFrame, logFrame);
    SyncWorldState(instanceID, logFrame, nds);
    SyncWorldEffectState(instanceID, logFrame, nds);
    const auto afterSyncWorld = std::chrono::steady_clock::now();
    TraceHangPhase("begin", "sync-hazard-tail", instanceID, logFrame, logFrame, logFrame);
    SyncMovingHazardState(instanceID, logFrame, nds);
    SyncWorldActorSnapshotState(instanceID, logFrame, nds);
    const auto afterSyncHazard = std::chrono::steady_clock::now();
    TraceHangPhase("begin", "sync-player-tail", instanceID, logFrame, logFrame, logFrame);
    SyncPlayerState(instanceID, logFrame, nds);
    const auto afterSyncPlayer = std::chrono::steady_clock::now();
    TraceHangPhase("end", "after-frame", instanceID, logFrame, logFrame, logFrame);

    if (G.ActiveFrameSpikeTrace)
    {
        const auto totalUs = std::chrono::duration_cast<std::chrono::microseconds>(
            afterSyncPlayer - afterHookCallStart).count();
        if (totalUs >= std::min(G.ActiveFrameSpikeThresholdUs, 10000))
        {
            const auto elapsedMs = [](auto start, auto end) {
                return std::chrono::duration<double, std::milli>(end - start).count();
            };
            std::printf(
                "NSMB AfterHookPhaseSpike: inst=%d frame=%u totalMs=%.3f initMs=%.3f heartbeatMs=%.3f barrierMs=%.3f bridgeMs=%.3f lifeTraceMs=%.3f diagnosticSnapshotMs=%.3f rollbackTraceMs=%.3f runtimeForceMs=%.3f artifactsMs=%.3f preSnapshotTailMs=%.3f applyHazardMs=%.3f applyWorldMs=%.3f applyPlayerMs=%.3f traceMs=%.3f syncGameMs=%.3f syncWorldMs=%.3f syncHazardMs=%.3f syncPlayerMs=%.3f\n",
                instanceID,
                logFrame,
                elapsedMs(afterHookCallStart, afterSyncPlayer),
                elapsedMs(afterHookCallStart, afterInit),
                elapsedMs(afterInit, afterHeartbeat),
                elapsedMs(afterHeartbeat, afterBarrier),
                elapsedMs(afterBarrier, afterBridge),
                elapsedMs(afterBridge, afterLifeTrace),
                elapsedMs(afterLifeTrace, afterDiagnosticSnapshot),
                elapsedMs(afterDiagnosticSnapshot, afterRollbackTrace),
                elapsedMs(afterRollbackTrace, afterRuntimeForce),
                elapsedMs(afterRuntimeForce, afterArtifacts),
                elapsedMs(afterArtifacts, afterPreSnapshot),
                elapsedMs(afterPreSnapshot, afterApplyHazard),
                elapsedMs(afterApplyHazard, afterApplyWorld),
                elapsedMs(afterApplyWorld, afterApplyPlayer),
                elapsedMs(afterApplyPlayer, afterTrace),
                elapsedMs(afterTrace, afterSyncGame),
                elapsedMs(afterSyncGame, afterSyncWorld),
                elapsedMs(afterSyncWorld, afterSyncHazard),
                elapsedMs(afterSyncHazard, afterSyncPlayer));
        }
    }

    if (!G.Bootstrap.HashEnabled) return;
    if ((logFrame % static_cast<melonDS::u32>(G.Bootstrap.HashInterval)) != 0) return;

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
    if (!G.TestEnabled || G.Bootstrap.TestFrames == kNoFrameLimit) return false;
    if (instanceID != G.Bootstrap.TestInstanceCount - 1) return false;
    if (G.TestFrameCount[instanceID] < G.Bootstrap.TestFrames) return false;

    std::lock_guard<std::mutex> lock(G.Mutex);
    for (int i = 0; i < G.Bootstrap.TestInstanceCount; i++)
    {
        if (G.TestFrameCount[i] < G.Bootstrap.TestFrames)
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
            ? (static_cast<double>(G.Bootstrap.TestFrames) * 1000.0) / static_cast<double>(elapsedMs)
            : 0.0;
        std::printf("NSMB Test: frame limit reached at frame=%u instances=%d elapsedMs=%lld fps=%.2f\n",
            G.Bootstrap.TestFrames,
            G.Bootstrap.TestInstanceCount,
            static_cast<long long>(elapsedMs),
            fps);
        if (G.ActiveTimerStarted[instanceID] && G.Bootstrap.TestFrames > G.ActiveTimerStartFrame[instanceID])
        {
            const auto activeElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - G.ActiveTimerStart[instanceID]).count();
            const melonDS::u32 activeFrames = G.Bootstrap.TestFrames - G.ActiveTimerStartFrame[instanceID];
            const double activeFps = activeElapsedMs > 0
                ? (static_cast<double>(activeFrames) * 1000.0) / static_cast<double>(activeElapsedMs)
                : 0.0;
            std::printf("NSMB Test: active fps startFrame=%u frames=%u elapsedMs=%lld fps=%.2f\n",
                G.ActiveTimerStartFrame[instanceID],
                activeFrames,
                static_cast<long long>(activeElapsedMs),
                activeFps);
            const melonDS::u32 timingSamples = G.ActiveFrameSamples[instanceID];
            if (timingSamples > 0)
            {
                const double avgFrameMs =
                    static_cast<double>(G.ActiveFrameTotalUs[instanceID]) /
                    static_cast<double>(timingSamples) / 1000.0;
                const double maxFrameMs =
                    static_cast<double>(G.ActiveFrameMaxUs[instanceID]) / 1000.0;
                std::printf(
                    "NSMB Test: active frame timing startFrame=%u samples=%u avgFrameMs=%.3f maxFrameMs=%.3f maxFrame=%u over16ms=%u over25ms=%u over33ms=%u spikeThresholdMs=%.3f\n",
                    G.ActiveTimerStartFrame[instanceID],
                    timingSamples,
                    avgFrameMs,
                    maxFrameMs,
                    G.ActiveFrameMaxFrame[instanceID],
                    G.ActiveFrameOver16ms[instanceID],
                    G.ActiveFrameOver25ms[instanceID],
                    G.ActiveFrameOver33ms[instanceID],
                    static_cast<double>(G.ActiveFrameSpikeThresholdUs) / 1000.0);
            }
        }
        if (G.Enabled && G.Input.NetplayOnly)
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
        if (G.Enabled && G.Bootstrap.QuitGraceMs > 0)
        {
            const auto end = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(G.Bootstrap.QuitGraceMs);
            while (std::chrono::steady_clock::now() < end)
            {
                PumpNetworkLocked();
                G.Transport.Flush();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        {
            std::lock_guard<std::mutex> recordLock(G.InputRecordMutex);
            FlushInputRecordSpanLocked();
            if (G.InputRecord)
                G.InputRecord.close();
        }
        std::_Exit(0);
    }
    return true;
}

void Shutdown()
{
    StopHangWatchdog();
    StopFrameHeartbeatThread();
    StopNetworkPumpThread();

    {
        std::lock_guard<std::mutex> recordLock(G.InputRecordMutex);
        FlushInputRecordSpanLocked();
        if (G.InputRecord)
            G.InputRecord.close();
    }

    std::lock_guard<std::mutex> lock(G.Mutex);

    G.Transport.Shutdown();

    if (G.HashLog)
        G.HashLog.close();

    if (G.GameStateTrace)
        G.GameStateTrace.close();
    if (G.AIPlayLog)
    {
        G.AIPlayLog.flush();
        G.AIPlayLog.close();
    }
    if (G.AIObservationV2Log)
    {
        G.AIObservationV2Log.flush();
        G.AIObservationV2Log.close();
    }
    if (G.AIObservationV3Log)
    {
        G.AIObservationV3Log.flush();
        G.AIObservationV3Log.close();
    }
}

}
