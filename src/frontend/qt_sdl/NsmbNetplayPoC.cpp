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
#include "NsmbPacketBridgeIntegration.h"
#include "NsmbPacketBridgeRuntime.h"
#include "NsmbMvlRuntime.h"
#include "NsmbMvlGameHooks.h"
#include "NsmbNetplayCoordinator.h"
#include "NsmbInputDelivery.h"
#include "NsmbInputProtocol.h"
#include "NsmbNetplayProtocol.h"
#include "NsmbNetplayTransport.h"
#include "NsmbNetplayDiagnostics.h"
#include "NsmbNetplaySession.h"
#include "NsmbTestStateHarness.h"
#include "NsmbGameState.h"
#include "NsmbGameStateReader.h"
#include "NsmbGameStateWriter.h"
#include "NsmbRollbackStore.h"
#include "NsmbRollbackRuntime.h"
#include "NsmbAiObservation.h"
#include "NsmbInputTimeline.h"
#include "NsmbImitationAI.h"
#include "NsmbRuleAI.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <filesystem>
#include <limits>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <enet/enet.h>

#include "NDS.h"
#include "ARM.h"
#include "Savestate.h"
#include "Platform.h"

namespace NsmbNetplayPoC
{

namespace
{

constexpr std::size_t kTrackedWorldMovingHazardCount = 4;
using WireProtocol::WireGameState;
using WireProtocol::kWireKindState;
using GameStateReader::WorldEffectSlotSample;
using GameStateModel::AITerrainDerivedSummary;
using GameStateModel::AITileGridSample;
using GameStateModel::AITileProbeSample;
using GameStateModel::AIPlayerTileProbeSample;
using GameStateModel::GameStateHashMismatch;
using GameStateModel::GameStateSample;
using GameStateModel::GameStateSyncHashes;
using GameStateModel::GameStateTraceHashes;
using GameStateModel::GameStateTraceWriter;
using GameStateModel::CombinedGameStateHash;
using GameStateModel::ComputeBasicGameStateHash;
using GameStateModel::DecodedGameState;
using GameStateModel::DecodeWireGameState;
using GameStateModel::EncodeWireGameState;
using GameStateModel::StateSyncRuntime;
using GameStateModel::PlayerCollisionMgrSample;
using GameStateModel::PlayerHitboxSample;
using GameStateModel::kAITileGridCount;
using GameStateModel::kAITileGridHeight;
using GameStateModel::kAITileGridMinRelX;
using GameStateModel::kAITileGridMinRelY;
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
using GameStateReader::HashFramebuffers;
using GameStateReader::HashMainRAMRange;
using GameStateReader::HashNDS;
using GameStateReader::ReadObjectByBase;
using GameStateReader::ReadAIPlayerTileProbeSample;
using GameStateReader::ReadPlayerCollisionMgrSample;
using GameStateReader::ReadPlayerHitboxSample;
using GameStateReader::SummarizeObjectLifecycle;
constexpr melonDS::u32 kNoFrameLimit = 0;
constexpr melonDS::u32 kMainRAMBase = 0x02000000;
constexpr melonDS::u32 kGameStageIDAddr = 0x02085A14;
constexpr melonDS::u32 kGameStageGroupAddr = 0x02085A18;
constexpr melonDS::u32 kGameVsModeAddr = 0x02085A84;
constexpr melonDS::u32 kNetStateBaseAddr = 0x020887E8;
constexpr melonDS::u32 kNetLocalAidAddr = 0x020887F0;

unsigned long long NowUnixMs()
{
    return static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}
constexpr melonDS::u32 kNetRandomValueAddr = 0x02088A68;
constexpr melonDS::u32 kInputKeyXMask = 1u << 10;
constexpr melonDS::u16 kMvlStockItemTouchX = 217;
constexpr melonDS::u16 kMvlStockItemTouchY = 153;
constexpr melonDS::u32 kGamePlayerGlobalBlockAddr = 0x0208B324;
constexpr melonDS::u32 kGamePlayerDeadAddr = 0x0208B328;
constexpr melonDS::u32 kGamePlayerTransitionStatusAddr = 0x0208B354; // Game::playerVSPipeState
constexpr melonDS::u32 kGamePlayerLivesAddr = 0x0208B364;
constexpr melonDS::u32 kGamePlayerBattleStarsAddr = 0x0208B36C;
constexpr melonDS::u32 kGamePlayerDisplayedStarsAddr = 0x0208B38C;
constexpr melonDS::u32 kGamePlayerDeathsAddr = 0x0208B394;
constexpr melonDS::u32 kGamePlayerCollectedStarsAddr = 0x0208B39C;
constexpr melonDS::u32 kGameCandidateWifiBlockAddr = 0x0208B7A0;
constexpr melonDS::u32 kGameCandidateRenderBlockAddr = 0x023F8300;
constexpr melonDS::u16 kPlayerObjectID = 0x0015;
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
constexpr melonDS::u32 kEffectVTableStart = 0x02126A24;
constexpr melonDS::u32 kEffectVTablePtr = 0x02126A2C;
constexpr melonDS::u32 kWorldEffectSlotBase = 0x021C3268;
constexpr melonDS::u32 kWorldEffectSlotStride = 0x1D4;
constexpr melonDS::u32 kWorldEffectSlotCount = 32;
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
constexpr melonDS::u32 kSceneIsSceneActiveAddr = 0x0203BD28;
constexpr melonDS::u32 kScenePreviousSceneIDAddr = 0x0203BD2C;
constexpr melonDS::u32 kSceneNextSceneIDAddr = 0x0203BD30;
constexpr melonDS::u32 kSceneCurrentSceneIDAddr = 0x0203BD34;
constexpr melonDS::u32 kSceneNextSceneSettingsAddr = 0x02088F38;
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

constexpr melonDS::u32 kDiagnosticPostTriggerFrames = 120;
constexpr melonDS::u32 kDiagnosticRepeatedAnomalyFrames = 120;
constexpr melonDS::u32 kPlayerPitDeathTransitStateAddr = 0x021196B0;


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
using Diagnostics::DiagnosticFrameSnapshot;
using Diagnostics::DiagnosticPlayerSnapshot;
using Diagnostics::BeforeHookPhaseTrace;
using Diagnostics::IsPlayerScreenPositionAnomalous;
using Diagnostics::JsonEscape;

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

int CurrentPacketBridgeLocalPlayer();
const PacketBridge::IntegrationHooks& PacketBridgeHooks();
const NetplaySession::Hooks& NetplaySessionHooks();

struct State
{
    std::mutex Mutex;
    std::atomic<bool> EnvChecked { false };
    Config::BootstrapConfig Bootstrap;
    Config::DiagnosticsConfig Diagnostics;
    Diagnostics::Runtime DiagnosticsRuntime;
    bool Enabled = false;
    bool Ready = false;
    bool TestEnabled = false;
    bool TestAnnouncedQuit = false;
    Config::ConnectionConfig Connection;
    Role NetRole = Role::Host;
    Config::StateSyncConfig StateSync;
    NetplaySession::Runtime NetplaySession;
    Coordination::Runtime Coordinator;
    Config::PacketBridgeConfig PacketBridge;
    PacketBridge::Runtime PacketBridgeRuntime;
    Config::InputConfig Input;
    InputTimeline::Runtime InputRuntime;
    InputTimeline::Recorder InputRecorder;
    Config::MvlConfig Mvl;
    MvlRuntime::Runtime MvlSeries;
    int MvlCurrentStage = 0;
    melonDS::u32 MvlCurrentStageSceneSettings = kMvlStageSceneDefaultSettings;
    Config::AIConfig AI;
    NsmbImitationAI::Runtime ImitationAI;
    Config::RuntimePatchConfig RuntimePatch;
    Config::HarnessConfig Harness;
    GameStateTraceWriter GameStateTrace;
    AIObservation::Runtime AIObservationRuntime;
    Config::RollbackConfig Rollback;
    RollbackStorage::Store RollbackStore;
    RollbackStorage::Statistics RollbackStats;
    NsmbNetplayTransport::Transport Transport;
    StateSyncRuntime GameSync;
    std::vector<std::pair<melonDS::u32, melonDS::u32>> RamDumpRanges;
};

State G;
std::vector<InputTimeline::InputSpan> GInputScript;

MvlGameHooks::Context MvlHooksContext()
{
    return {
        G.Mvl,
        G.RuntimePatch,
        G.DiagnosticsRuntime,
        G.MvlSeries,
        G.MvlCurrentStage,
        G.MvlCurrentStageSceneSettings,
        G.NetRole == Role::Host,
    };
}

RollbackRuntime::Context RollbackContext()
{
    return {
        G.Rollback,
        G.Input,
        G.InputRuntime,
        G.RollbackStore,
        G.RollbackStats,
        G.Mutex,
        G.Connection.StartFrame,
    };
}

PacketBridge::IntegrationContext PacketBridgeContext()
{
    return {
        G.PacketBridge,
        G.RuntimePatch,
        G.Input,
        G.Connection,
        G.PacketBridgeRuntime,
        G.Transport,
        G.Mutex,
    };
}

NetplaySession::Context NetplaySessionContext()
{
    return {
        G.Bootstrap,
        G.Diagnostics,
        G.Connection,
        G.PacketBridge,
        G.Input,
        G.Rollback,
        G.Harness,
        G.Mvl,
        G.InputRuntime,
        G.Coordinator,
        G.DiagnosticsRuntime,
        G.Transport,
        G.NetplaySession,
        G.Mutex,
        G.Enabled,
        G.Ready,
        G.TestEnabled,
        G.NetRole == Role::Host,
    };
}

void TraceHangPhase(const char* event, const char* phase, int instanceID = -1,
    melonDS::u32 frame = 0, melonDS::u32 logicalFrame = 0, melonDS::u32 sendFrame = 0);
void UpdateHangGameSnapshot(int instanceID, melonDS::u32 frame, melonDS::NDS* nds);
void StartHangWatchdogIfNeeded();
void StopDiagnostics();


void TraceHangPhase(
    const char* event,
    const char* phase,
    int instanceID,
    melonDS::u32 frame,
    melonDS::u32 logicalFrame,
    melonDS::u32 sendFrame)
{
    G.DiagnosticsRuntime.TracePhase(event, phase, instanceID, frame, logicalFrame, sendFrame);
}

void StartHangWatchdogIfNeeded()
{
    G.DiagnosticsRuntime.StartHangDiagnostics(G.Diagnostics, G.NetRole == Role::Host);
}

void StopDiagnostics()
{
    G.DiagnosticsRuntime.Stop();
}

melonDS::u32 ComposeMvlSceneSettingsForStage(int stage)
{
    const melonDS::u32 clampedStage = static_cast<melonDS::u32>(std::clamp(stage, 0, 4));
    return ((0xB4u + clampedStage) << 16) | 0xFF00u;
}

int MvlStageForGame(int instanceID)
{
    return G.MvlSeries.StageForGame(instanceID, G.Mvl, G.MvlCurrentStage);
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
    return G.MvlSeries.MatchSeedForGame(instanceID, G.Mvl);
}

melonDS::u32 MvlRestartPacketCutoffFrame()
{
    return G.MvlSeries.RestartPacketCutoffFrame();
}

void ResetMvlRuntimeSyncStateForRestart(int instanceID, melonDS::u32 frame)
{
    if (instanceID < 0 || instanceID >= 16)
        return;

    std::lock_guard<std::mutex> lock(G.Mutex);

    G.GameSync.ResetForRestart(instanceID);
    G.PacketBridgeRuntime.ResetQueuesForRestart();
    G.NetplaySession.Delivery.Clear();
    G.InputRuntime.ResetForRestart(kNoFrameLimit);
    G.DiagnosticsRuntime.ResetNetplaySnapshot(kNoFrameLimit);
    G.NetplaySession.Handshake.ResetStartHandshake();

    G.GameStateTrace.ResetForRestart(instanceID);

    std::printf("NSMB MvL auto restart: reset sync caches inst=%d frame=%u cutoff=%u\n",
        instanceID,
        frame,
        MvlRestartPacketCutoffFrame());
    std::fflush(stdout);
}

void RebaseMvlAutoRestartStartupFrames(int instanceID, melonDS::u32 restartFrame)
{
    if (instanceID < 0 || instanceID >= 16)
        return;

    G.MvlSeries.RebaseStartupFrame(restartFrame, G.Connection.StartFrame);
    G.MvlSeries.RebaseStartupFrame(restartFrame, G.PacketBridge.ForceTickStartFrame);
    G.MvlSeries.RebaseStartupFrame(restartFrame, G.PacketBridge.ForceGameLocalPlayerIDStartFrame);
    G.MvlSeries.RebaseStartupFrame(restartFrame, G.PacketBridge.ThrottleStartFrame);
    G.MvlSeries.RebaseStartupFrame(restartFrame, G.Input.SendDelayStartFrame);
    G.MvlSeries.RebaseStartupFrame(restartFrame, G.Input.SendDelayEndFrame);
    G.MvlSeries.RebaseStartupFrame(restartFrame, G.RuntimePatch.PacketBridgeJitHelperPatchFrame);
    G.MvlSeries.RebaseStartupFrame(restartFrame, G.Mvl.CameraInitHold.StartFrame);
    G.MvlSeries.RebaseStartupFrame(restartFrame, G.Mvl.CameraInitHold.EndFrame);

    G.MvlSeries.SetStartupFrameBase(restartFrame);
    std::printf(
        "NSMB MvL auto restart: rebased startup frames inst=%d restartFrame=%u netplayStart=%u packetJit=%u\n",
        instanceID,
        restartFrame,
        G.Connection.StartFrame,
        G.RuntimePatch.PacketBridgeJitHelperPatchFrame);
    std::fflush(stdout);
}

void RebaseMvlAutoRestartStartupFramesFromCheckpoint(
    int instanceID,
    melonDS::u32 restoreFrame,
    melonDS::u32 checkpointFrame)
{
    if (instanceID < 0 || instanceID >= 16)
        return;

    G.MvlSeries.RebaseStartupFrameFromCheckpoint(restoreFrame, checkpointFrame, G.Connection.StartFrame);
    G.MvlSeries.RebaseStartupFrameFromCheckpoint(restoreFrame, checkpointFrame, G.PacketBridge.ForceTickStartFrame);
    G.MvlSeries.RebaseStartupFrameFromCheckpoint(restoreFrame, checkpointFrame, G.PacketBridge.ForceGameLocalPlayerIDStartFrame);
    G.MvlSeries.RebaseStartupFrameFromCheckpoint(restoreFrame, checkpointFrame, G.PacketBridge.ThrottleStartFrame);
    G.MvlSeries.RebaseStartupFrameFromCheckpoint(restoreFrame, checkpointFrame, G.Input.SendDelayStartFrame);
    G.MvlSeries.RebaseStartupFrameFromCheckpoint(restoreFrame, checkpointFrame, G.Input.SendDelayEndFrame);
    G.MvlSeries.RebaseStartupFrameFromCheckpoint(restoreFrame, checkpointFrame, G.RuntimePatch.PacketBridgeJitHelperPatchFrame);
    G.MvlSeries.RebaseStartupFrameFromCheckpoint(restoreFrame, checkpointFrame, G.Mvl.CameraInitHold.StartFrame);
    G.MvlSeries.RebaseStartupFrameFromCheckpoint(restoreFrame, checkpointFrame, G.Mvl.CameraInitHold.EndFrame);

    G.MvlSeries.SetStartupFrameBase(
        restoreFrame > checkpointFrame ? restoreFrame - checkpointFrame : restoreFrame);
    std::printf(
        "NSMB MvL auto restart: rebased startup frames from checkpoint inst=%d restoreFrame=%u checkpointFrame=%u netplayStart=%u packetJit=%u\n",
        instanceID,
        restoreFrame,
        checkpointFrame,
        G.Connection.StartFrame,
        G.RuntimePatch.PacketBridgeJitHelperPatchFrame);
    std::fflush(stdout);
}

void ResetMvlAutoRestartStartupHookState(int instanceID)
{
    if (instanceID < 0 || instanceID >= 16)
        return;

    G.MvlSeries.ResetStartupHookState(instanceID);
    G.PacketBridgeRuntime.ResetStartupHookState(instanceID);
    G.Coordinator.ResetNetplayStartWait();
    G.NetplaySession.Handshake.ResetStartHandshake();
    G.InputRuntime.LastInputFrameLeadResendAt = {};
    G.InputRuntime.InputFrameLeadResendCount = 0;
    G.Coordinator.ResetNetplayLockstep(instanceID);
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

InputState NeutralInputPreservingTouch(const InputState& source)
{
    InputState input = NeutralInput();
    input.Touching = source.Touching;
    input.TouchX = source.TouchX;
    input.TouchY = source.TouchY;
    return input;
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
    return LoadInputScriptFileLocked(G.Harness.InputScriptPath, GInputScript);
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
    if (G.Diagnostics.DiagnosticsPath.empty())
        return;

    const std::filesystem::path path(G.Diagnostics.DiagnosticsPath);
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

void ReportGameStateMismatchLocked(const GameStateHashMismatch& mismatch)
{
    const int instanceID = mismatch.InstanceID;
    const melonDS::u32 frame = mismatch.Frame;
    const GameStateSyncHashes& lhs = mismatch.Local;
    const GameStateSyncHashes& rhs = mismatch.Remote;
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
    if (!G.ImitationAI.IsEnabled() || !G.ImitationAI.HasModel())
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
    G.AIObservationRuntime.RecordAppliedInput(instanceID, frame, player, input);
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

void HandleReceivedGameStateLocked(const void* data, std::size_t size)
{
    WireGameState packet;
    std::memcpy(&packet, data, size);
    DecodedGameState decoded;
    if (!DecodeWireGameState(packet, decoded))
        return;

    const auto mismatch = G.GameSync.RecordRemoteGameState(decoded);
    if (mismatch)
        ReportGameStateMismatchLocked(*mismatch);
}
unsigned long long ElapsedUs(std::chrono::steady_clock::time_point start)
{
    return static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count());
}

void RecordActiveFrameTiming(int instanceID, melonDS::u32 frame)
{
    RollbackStorage::StatisticsSnapshot rollbackStats;
    if (G.Diagnostics.ActiveFrameSpikeTrace)
        rollbackStats = G.RollbackStats.Snapshot();
    const Diagnostics::Runtime::ActiveFrameSample sample =
        G.DiagnosticsRuntime.RecordActiveFrameTiming(
            instanceID,
            frame,
            std::chrono::steady_clock::now(),
            G.Diagnostics.ActiveFrameSpikeTrace,
            static_cast<std::uint64_t>(G.Diagnostics.ActiveFrameSpikeThresholdUs),
            rollbackStats.RestoreCount,
            rollbackStats.ResimulateCount);
    if (!sample.Spike)
        return;

    std::printf(
        "NSMB PerfSpike: inst=%d frame=%u frameTimeUs=%llu thresholdUs=%d rollbackRestores=%u rollbackResims=%u rollbackRestoreDelta=%u rollbackResimDelta=%u saveMaxUs=%llu restoreMaxUs=%llu resimRunMaxUs=%llu resimSaveMaxUs=%llu resimTotalMaxUs=%llu\n",
        instanceID,
        frame,
        static_cast<unsigned long long>(sample.ElapsedUs),
        G.Diagnostics.ActiveFrameSpikeThresholdUs,
        rollbackStats.RestoreCount,
        rollbackStats.ResimulateCount,
        sample.RollbackRestoreDelta,
        sample.RollbackResimulateDelta,
        rollbackStats.CheckpointSaveMaxUs,
        rollbackStats.CheckpointRestoreMaxUs,
        rollbackStats.ResimRunFrameMaxUs,
        rollbackStats.ResimCheckpointSaveMaxUs,
        rollbackStats.ResimCorrectionMaxUs);
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

const NetplaySession::Hooks& NetplaySessionHooks()
{
    static const NetplaySession::Hooks hooks {
        [](const char* direction, melonDS::u32 localFrame, melonDS::u32 remoteFrame) {
            EmitStartReadyEventLocked(direction, localFrame, remoteFrame);
        },
        [](const void* data, std::size_t size, melonDS::NDS* nds, melonDS::u32 localFrame) {
            PacketBridge::ReceivePacketLocked(
                PacketBridgeContext(),
                data,
                size,
                nds,
                localFrame,
                MvlRestartPacketCutoffFrame());
        },
        [](const void* data, std::size_t size) {
            HandleReceivedGameStateLocked(data, size);
        },
        [](melonDS::NDS* nds) {
            return IsInputNetplayGameplayStartReady(nds);
        },
    };
    return hooks;
}
bool WaitAtFrameBarrier(
    Coordination::FrameBarrierKind kind,
    int instanceID,
    melonDS::u32 frame,
    const char* name)
{
    if (!G.TestEnabled || !G.Harness.FrameBarrierEnabled || G.Bootstrap.TestInstanceCount <= 1)
        return true;
    if (instanceID < 0 || instanceID >= G.Bootstrap.TestInstanceCount)
        return true;

    return G.Coordinator.WaitAtFrameBarrier(
        kind,
        instanceID,
        frame,
        G.Bootstrap.TestInstanceCount,
        G.Bootstrap.WaitTimeoutMs,
        name);
}

bool WaitForSerialRunTurn(int instanceID, melonDS::u32 frame)
{
    if (!G.TestEnabled || !G.Harness.SerialRunEnabled || G.Bootstrap.TestInstanceCount <= 1)
        return true;
    if (instanceID < 0 || instanceID >= G.Bootstrap.TestInstanceCount)
        return true;

    return G.Coordinator.WaitForSerialTurn(
        instanceID,
        frame,
        G.Bootstrap.TestInstanceCount,
        G.Bootstrap.WaitTimeoutMs);
}

void AdvanceSerialRunTurn(int instanceID, melonDS::u32 frame)
{
    if (!G.TestEnabled || !G.Harness.SerialRunEnabled || G.Bootstrap.TestInstanceCount <= 1)
        return;
    if (instanceID < 0 || instanceID >= G.Bootstrap.TestInstanceCount)
        return;

    G.Coordinator.AdvanceSerialTurn(instanceID, frame, G.Bootstrap.TestInstanceCount);
}

bool WriteARM9U32(melonDS::NDS* nds, melonDS::u32 addr, melonDS::u32 value)
{
    if (!nds || (addr & 3) != 0)
        return false;

    nds->ARM9Write32(addr, value);
    return true;
}

void SaveMvlAutoRestartBootstrapCheckpointIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.Mvl.AutoRestartAfterResult || G.Mvl.TargetWins <= 1 || !nds)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    MvlRuntime::InstanceState& restart = G.MvlSeries.Instances[instanceID];
    if (!restart.BootstrapCheckpoint.Buffer.empty())
        return;
    if (restart.RestartCount != 0 || restart.InResult)
        return;
    const melonDS::u32 generatedBootstrapFrame = G.Mvl.AutoRestartBootstrapFrame;
    const melonDS::u32 stageGroup = nds->ARM9Read32(kGameStageGroupAddr);
    const melonDS::u16 currentScene = nds->ARM9Read16(kSceneCurrentSceneIDAddr);
    const melonDS::u16 nextScene = nds->ARM9Read16(kSceneNextSceneIDAddr);
    const bool generatedRomReady = frame >= generatedBootstrapFrame
        && stageGroup != 9
        && currentScene == 0x0004
        && nextScene == 0x0006
        && nds->ARM9Read16(kSceneIsSceneActiveAddr) != 0;
    if (!generatedRomReady)
        return;

    melonDS::Savestate state;
    if (state.Error || !nds->DoSavestate(&state) || state.Error)
    {
        if (!restart.BootstrapCheckpoint.Logged)
        {
            std::printf("NSMB MvL auto restart: failed to save bootstrap checkpoint inst=%d frame=%u\n",
                instanceID,
                frame);
            std::fflush(stdout);
            restart.BootstrapCheckpoint.Logged = true;
        }
        return;
    }

    restart.BootstrapCheckpoint.Buffer.assign(
        reinterpret_cast<const char*>(state.Buffer()),
        reinterpret_cast<const char*>(state.Buffer()) + state.Length());
    restart.BootstrapCheckpoint.Frame = frame;
    restart.BootstrapCheckpoint.Logged = true;
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

    const PacketBridge::JitHookRestoreResult result =
        G.PacketBridgeRuntime.ScheduleJitHookAfterRestore(
            instanceID, restoreFrame, checkpointFrame, G.RuntimePatch);
    if (result.Action == PacketBridge::JitHookRestoreAction::Disabled)
        return;

    if (result.Action == PacketBridge::JitHookRestoreAction::KeepApplied)
    {
        std::printf(
            "NSMB MvL auto restart: keeping packet bridge JIT helper patch inst=%d restoreFrame=%u checkpointFrame=%u patchFrame=%u\n",
            instanceID,
            restoreFrame,
            checkpointFrame,
            G.RuntimePatch.PacketBridgeJitHelperPatchFrame);
        std::fflush(stdout);
        return;
    }

    std::printf(
        "NSMB MvL auto restart: scheduled packet bridge JIT helper patch inst=%d restoreFrame=%u checkpointFrame=%u patchFrame=%u resumeFrame=%u\n",
        instanceID,
        restoreFrame,
        checkpointFrame,
        G.RuntimePatch.PacketBridgeJitHelperPatchFrame,
        result.ResumeFrame);
    std::fflush(stdout);
}

bool RestoreMvlAutoRestartBootstrapCheckpoint(int instanceID, melonDS::u32 frame, melonDS::NDS* nds, melonDS::u32 requestedSeed)
{
    if (!nds || instanceID < 0 || instanceID >= 16)
        return false;
    MvlRuntime::InstanceState& restart = G.MvlSeries.Instances[instanceID];
    if (restart.BootstrapCheckpoint.Buffer.empty())
        return false;

    ResetMvlAutoRestartStartupHookState(instanceID);
    RebaseMvlAutoRestartStartupFramesFromCheckpoint(
        instanceID,
        frame,
        restart.BootstrapCheckpoint.Frame);
    melonDS::Savestate state(
        restart.BootstrapCheckpoint.Buffer.data(),
        static_cast<melonDS::u32>(restart.BootstrapCheckpoint.Buffer.size()),
        false);
    if (state.Error || !nds->DoSavestate(&state) || state.Error)
    {
        std::printf("NSMB MvL auto restart: failed to restore bootstrap checkpoint inst=%d frame=%u bytes=%zu\n",
            instanceID,
            frame,
            restart.BootstrapCheckpoint.Buffer.size());
        std::fflush(stdout);
        return false;
    }

    RollbackRuntime::InvalidateMainRAMJIT(G.Rollback, nds, nds->MainRAMMask + 1);
    melonDS::Platform::MP_Begin(nds->UserData);
    G.MvlSeries.Instances[instanceID].NetRandomPatchApplied = false;
    G.Mvl.NetRandom.Value = requestedSeed;
    G.Mvl.NetRandom.Enabled = true;
    G.Mvl.NetRandom.Auto = true;
    if (G.NetRole == Role::Host)
        WriteARM9U32(nds, kNetLocalAidAddr, 0);
    else if (G.NetRole == Role::Client)
        WriteARM9U32(nds, kNetLocalAidAddr, 1);
    MvlGameHooks::WriteRandomSeed(nds, requestedSeed);
    MvlGameHooks::ApplyRuntimeConfig(MvlHooksContext(), nds);
    std::printf("NSMB MvL auto restart: restored bootstrap checkpoint inst=%d frame=%u seed=0x%08X bytes=%zu\n",
        instanceID,
        frame,
        requestedSeed,
        restart.BootstrapCheckpoint.Buffer.size());
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
    MvlGameHooks::ApplyRuntimeConfig(MvlHooksContext(), nds);
    MvlGameHooks::WriteRandomSeed(nds, requestedSeed);
    nds->SetupDirectBoot(std::string {});
    nds->Start();
    melonDS::Platform::MP_Begin(nds->UserData);
    MvlGameHooks::ApplyRuntimeConfig(MvlHooksContext(), nds);
    MvlGameHooks::WriteRandomSeed(nds, requestedSeed);
    G.Mvl.NetRandom.Enabled = true;
    G.Mvl.NetRandom.Auto = true;
    G.MvlSeries.Instances[instanceID].NetRandomPatchApplied = false;
    G.Mvl.NetRandom.Value = requestedSeed;
    std::printf(
        "NSMB MvL auto restart: hard reset console for next match inst=%d frame=%u seed=0x%08X\n",
        instanceID,
        frame,
        requestedSeed);
    std::fflush(stdout);
    return true;
}

MvlRuntime::ResultSnapshot ReadMvlResultSnapshot(melonDS::NDS* nds)
{
    MvlRuntime::ResultSnapshot result {};
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

bool RestartMvlAfterResultIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.Mvl.AutoRestartAfterResult || G.Mvl.TargetWins < 1 || !nds || instanceID < 0 || instanceID >= 16)
        return false;
    MvlRuntime::InstanceState& restart = G.MvlSeries.Instances[instanceID];

    constexpr melonDS::u16 kResultsScene = 0x000A;
    const melonDS::u16 currentScene = nds->ARM9Read16(kSceneCurrentSceneIDAddr);
    if (currentScene != kResultsScene)
    {
        if (restart.RestartCount > 0
            && currentScene == 0x0003
            && nds->ARM9Read16(kScenePreviousSceneIDAddr) == kResultsScene
            && nds->ARM9Read16(kSceneNextSceneIDAddr) == 0x0003
            && frame - restart.LastRestartFrame >= 30)
        {
            nds->ARM9Write16(kSceneNextSceneIDAddr, 0x0181);
        }
        restart.InResult = false;
        restart.ResultScored = false;
        restart.ResultUnresolvedLogged = false;
        return false;
    }

    if (!restart.InResult)
    {
        restart.InResult = true;
        restart.ResultUnresolvedLogged = false;
        restart.ResultFrame = frame;
        return false;
    }

    if (!restart.ResultScored)
    {
        const MvlRuntime::ResultSnapshot result = ReadMvlResultSnapshot(nds);
        const int winner = MvlRuntime::ResolveResultWinner(result);
        if (winner < 0)
        {
            if (!restart.ResultUnresolvedLogged
                && frame - restart.ResultFrame >= G.Mvl.AutoRestartDelayFrames)
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
                    restart.Wins[0],
                    restart.Wins[1],
                    G.Mvl.TargetWins);
                std::fflush(stdout);
                restart.ResultUnresolvedLogged = true;
            }
            return false;
        }
        restart.Wins[winner]++;
        restart.ResultScored = true;
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
            restart.Wins[0],
            restart.Wins[1],
            G.Mvl.TargetWins);
        std::fflush(stdout);
    }

    const int leadingWins = std::max(restart.Wins[0], restart.Wins[1]);
    if (leadingWins >= G.Mvl.TargetWins)
        return false;
    if (frame - restart.ResultFrame < G.Mvl.AutoRestartDelayFrames)
        return false;

    const int nextRestartCount = restart.RestartCount + 1;
    restart.RestartCount = nextRestartCount;
    const int requestedStage = std::clamp(MvlStageForGame(instanceID), 0, 4);
    const melonDS::u32 requestedSeed = MatchSeedForGame(instanceID);
    G.MvlCurrentStage = requestedStage;
    G.MvlCurrentStageSceneSettings = ComposeMvlSceneSettingsForStage(requestedStage);
    MvlGameHooks::WriteRandomSeed(nds, requestedSeed);
    int restartPath = 0;
    if (RestoreMvlAutoRestartBootstrapCheckpoint(instanceID, frame, nds, requestedSeed))
    {
        restartPath = 3;
    }
    else if (!restart.GameplayCheckpoint.Buffer.empty()
        && restart.GameplayCheckpoint.Stage == requestedStage)
    {
        melonDS::Savestate state(
            restart.GameplayCheckpoint.Buffer.data(),
            static_cast<melonDS::u32>(restart.GameplayCheckpoint.Buffer.size()),
            false);
        if (!state.Error && nds->DoSavestate(&state) && !state.Error)
        {
            restartPath = 1;
            RollbackRuntime::InvalidateMainRAMJIT(G.Rollback, nds, nds->MainRAMMask + 1);
            melonDS::Platform::MP_Begin(nds->UserData);
            SchedulePacketBridgeJitHelperPatchAfterRestore(
                instanceID,
                frame,
                restart.GameplayCheckpoint.Frame);
            WriteARM9U32(nds, kGameStageGroupAddr, 0x00000009);
            WriteARM9U32(nds, kGameVsModeAddr, 0x00000001);
            WriteARM9U32(nds, kSceneNextSceneSettingsAddr, G.MvlCurrentStageSceneSettings);
            MvlGameHooks::ApplyRuntimeConfig(MvlHooksContext(), nds);
            MvlGameHooks::WriteRandomSeed(nds, requestedSeed);
        }
    }
    else if (ResetMvlAutoRestartConsoleForNextMatch(instanceID, frame, nds, requestedSeed))
    {
        restartPath = 4;
    }

    if (restartPath == 0)
    {
        restart.RestartCount = nextRestartCount - 1;
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
    restart.LastRestartFrame = frame;
    ResetMvlRuntimeSyncStateForRestart(instanceID, frame);
    restart.InResult = false;
    restart.ResultScored = false;
    restart.ResultUnresolvedLogged = false;
    const int actualStage = static_cast<int>(nds->ARM9Read32(kGameStageIDAddr));
    std::printf(
        "NSMB MvL auto restart: inst=%d frame=%u nextGame=%d stage=%d requestedStage=%d seed=0x%08X matchWins=%d/%d target=%d checkpoint=%d\n",
        instanceID,
        frame,
        restart.RestartCount + 1,
        actualStage,
        requestedStage,
        requestedSeed,
        restart.Wins[0],
        restart.Wins[1],
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
    const MvlRuntime::InstanceState& restart = G.MvlSeries.Instances[instanceID];
    if (!restart.InResult)
        return false;

    const int leadingWins = std::max(restart.Wins[0], restart.Wins[1]);
    return leadingWins < G.Mvl.TargetWins;
}

void SaveMvlAutoRestartCheckpointIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.Mvl.AutoRestartAfterResult || G.Mvl.TargetWins <= 1 || !nds || !nds->MainRAM)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    MvlRuntime::InstanceState& restart = G.MvlSeries.Instances[instanceID];
    if (!restart.GameplayCheckpoint.Buffer.empty())
        return;
    if (restart.InResult || restart.RestartCount != 0)
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
        if (!restart.GameplayCheckpoint.Logged)
        {
            std::printf("NSMB MvL auto restart: failed to save checkpoint inst=%d frame=%u\n", instanceID, frame);
            std::fflush(stdout);
            restart.GameplayCheckpoint.Logged = true;
        }
        return;
    }

    restart.GameplayCheckpoint.Buffer.assign(
        reinterpret_cast<const char*>(state.Buffer()),
        reinterpret_cast<const char*>(state.Buffer()) + state.Length());
    restart.GameplayCheckpoint.Frame = frame;
    restart.GameplayCheckpoint.Stage = static_cast<int>(sample.StageID);
    restart.GameplayCheckpoint.Logged = true;
    std::printf(
        "NSMB MvL auto restart: saved checkpoint inst=%d frame=%u bytes=%u stage=%u settings=0x%08X\n",
        instanceID,
        frame,
        state.Length(),
        sample.StageID,
        sample.StageSceneSettings);
    std::fflush(stdout);
}

void WriteDiagnosticEventLocked(const std::string& json)
{
    if (!G.Diagnostics.DiagnosticEventsEnabled || G.Diagnostics.DiagnosticEventsPath.empty())
        return;
    if (!G.DiagnosticsRuntime.WriteDiagnosticEvent(G.Diagnostics.DiagnosticEventsPath, json))
        G.Diagnostics.DiagnosticEventsEnabled = false;
}

void EmitStartReadyEventLocked(const char* direction, melonDS::u32 localFrame, melonDS::u32 remoteFrame)
{
    if (!G.Diagnostics.DiagnosticEventsEnabled)
        return;

    WriteDiagnosticEventLocked(Diagnostics::FormatStartReadyEvent(
        G.NetRole == Role::Host ? "host" : "client", direction, localFrame,
        remoteFrame, kNoFrameLimit, G.Connection.StartFrame,
        G.InputRuntime.LastSentInputFrame, G.InputRuntime.LastReceivedInputFrame,
        G.InputRuntime.LocalInputs.size(), G.InputRuntime.RemoteInputs.size(),
        G.NetplaySession.Delivery.PendingCount()));
}

void EmitDiagnosticStartupEvent()
{
    if (!G.Diagnostics.DiagnosticEventsEnabled)
        return;

    WriteDiagnosticEventLocked(Diagnostics::FormatDiagnosticStartupEvent(
        G.NetRole == Role::Host ? "host" : "client",
        G.Diagnostics.DiagnosticRingFrames, G.StateSync.GameEnabled,
        G.StateSync.GameExtended, G.StateSync.GameInterval,
        G.Diagnostics.DiagnosticsPath, G.Diagnostics.DiagnosticEventsPath));
}

std::vector<DiagnosticFrameSnapshot> DiagnosticRingWindow(int instanceID)
{
    if (instanceID < 0 || instanceID >= 16)
        return {};
    const std::size_t ringFrames = static_cast<std::size_t>(
        std::clamp(G.Diagnostics.DiagnosticRingFrames, 1,
            static_cast<int>(Diagnostics::kDiagnosticRingCapacity)));
    return G.DiagnosticsRuntime.DiagnosticSnapshotWindow(instanceID, ringFrames);
}

void EmitDiagnosticPitTransitionEvent(
    int instanceID,
    const DiagnosticFrameSnapshot& snap,
    const DiagnosticFrameSnapshot* previous,
    int player)
{
    if (!G.Diagnostics.DiagnosticEventsEnabled || instanceID < 0 || instanceID >= 16 || player < 0 || player > 1)
        return;
    if (snap.Player[player].TransitFunc != kPlayerPitDeathTransitStateAddr)
        return;
    if (previous && previous->Valid && previous->Player[player].TransitFunc == kPlayerPitDeathTransitStateAddr)
        return;
    if (!G.DiagnosticsRuntime.ShouldEmitDiagnosticPitTransition(
            instanceID, player, snap.Frame, kDiagnosticRepeatedAnomalyFrames))
        return;

    G.DiagnosticsRuntime.ScheduleDiagnosticPostTrigger(
        instanceID, snap.Frame + kDiagnosticPostTriggerFrames);

    const std::string json = Diagnostics::FormatDiagnosticPlayerSnapshotEvent(
        "player_pit_transition", G.NetRole == Role::Host ? "host" : "client",
        instanceID, snap, previous, player, DiagnosticRingWindow(instanceID));

    std::lock_guard<std::mutex> lock(G.Mutex);
    WriteDiagnosticEventLocked(json);
}

void EmitDiagnosticPositionAnomalyEvent(
    int instanceID,
    const DiagnosticFrameSnapshot& snap,
    const DiagnosticFrameSnapshot* previous,
    int player)
{
    if (!G.Diagnostics.DiagnosticEventsEnabled || instanceID < 0 || instanceID >= 16 || player < 0 || player > 1)
        return;
    if (!IsPlayerScreenPositionAnomalous(snap, previous, player))
        return;
    if (!G.DiagnosticsRuntime.ShouldEmitDiagnosticPositionAnomaly(
            instanceID, player, snap.Frame, kDiagnosticRepeatedAnomalyFrames))
        return;

    const std::string json = Diagnostics::FormatDiagnosticPlayerSnapshotEvent(
        "player_position_anomaly", G.NetRole == Role::Host ? "host" : "client",
        instanceID, snap, previous, player, DiagnosticRingWindow(instanceID));

    std::lock_guard<std::mutex> lock(G.Mutex);
    WriteDiagnosticEventLocked(json);
}

void RecordDiagnosticSnapshotIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.Diagnostics.DiagnosticEventsEnabled || !nds || !nds->MainRAM)
        return;
    if (instanceID < 0 || instanceID >= 16)
        return;
    if (frame < G.Connection.StartFrame)
        return;

    DiagnosticFrameSnapshot snap;
    snap.Valid = true;
    snap.Frame = frame;
    snap.Instance = static_cast<melonDS::u32>(instanceID);
    GameStateReader::ReadDiagnosticFrameSnapshot(nds, snap);
    snap.LastSentInputFrame = G.InputRuntime.LastSentInputFrame;
    snap.LastReceivedInputFrame = G.InputRuntime.LastReceivedInputFrame;
    GameStateReader::ReadDiagnosticPlayerSnapshot(
        instanceID, frame, nds, 0, G.GameSync, snap.Player[0]);
    GameStateReader::ReadDiagnosticPlayerSnapshot(
        instanceID, frame, nds, 1, G.GameSync, snap.Player[1]);
    if (snap.Player[0].Found && RollbackRuntime::IsValidMainRAMRange(nds, snap.Player[0].Base, 0xC00))
        snap.PlayerActorHash0 = HashMainRAMRange(nds, snap.Player[0].Base, 0xC00);
    if (snap.Player[1].Found && RollbackRuntime::IsValidMainRAMRange(nds, snap.Player[1].Base, 0xC00))
        snap.PlayerActorHash1 = HashMainRAMRange(nds, snap.Player[1].Base, 0xC00);

    const std::optional<DiagnosticFrameSnapshot> previousSnapshot =
        G.DiagnosticsRuntime.LatestDiagnosticSnapshot(instanceID);
    const DiagnosticFrameSnapshot* previous = previousSnapshot
        ? &previousSnapshot.value()
        : nullptr;
    for (int player = 0; player < 2; player++)
    {
        EmitDiagnosticPositionAnomalyEvent(instanceID, snap, previous, player);
        EmitDiagnosticPitTransitionEvent(instanceID, snap, previous, player);
    }

    G.DiagnosticsRuntime.RecordDiagnosticSnapshot(instanceID, snap);

    const std::optional<melonDS::u32> triggerFrame =
        G.DiagnosticsRuntime.TakeDueDiagnosticPostTrigger(instanceID, frame);
    if (triggerFrame)
    {
        const std::string json = Diagnostics::FormatDiagnosticPostWindowEvent(
            G.NetRole == Role::Host ? "host" : "client", instanceID, frame,
            triggerFrame.value(), DiagnosticRingWindow(instanceID));
        std::lock_guard<std::mutex> lock(G.Mutex);
        WriteDiagnosticEventLocked(json);
    }
}

void EmitGameStateMismatchEventLocked(
    int instanceID,
    melonDS::u32 frame,
    const GameStateSyncHashes& local,
    const GameStateSyncHashes& remote)
{
    if (!G.Diagnostics.DiagnosticEventsEnabled || instanceID < 0 || instanceID >= 16)
        return;
    if (local.PlayerGlobal == remote.PlayerGlobal)
        return;
    if (!G.DiagnosticsRuntime.ShouldEmitDiagnosticMismatch(instanceID, frame, 300))
        return;

    G.DiagnosticsRuntime.ScheduleDiagnosticPostTrigger(
        instanceID, frame + kDiagnosticPostTriggerFrames);

    GameStateSample remoteSample;
    const GameStateSample* remoteSamplePtr = nullptr;
    if (const GameStateSample* stored = G.GameSync.RemoteState.FindGameState(instanceID, frame))
    {
        remoteSample = *stored;
        remoteSamplePtr = &remoteSample;
    }

    const std::optional<DiagnosticFrameSnapshot> latestSnapshot =
        G.DiagnosticsRuntime.LatestDiagnosticSnapshot(instanceID);
    const DiagnosticFrameSnapshot* latest = latestSnapshot
        ? &latestSnapshot.value()
        : nullptr;
    WriteDiagnosticEventLocked(Diagnostics::FormatPlayerGlobalMismatchEvent(
        G.NetRole == Role::Host ? "host" : "client", instanceID, frame, local,
        remote, latest, remoteSamplePtr, DiagnosticRingWindow(instanceID)));
}

std::vector<Diagnostics::DiagnosticMovingHazardSnapshot>
ReadNearbyDiagnosticHazards(melonDS::NDS* nds)
{
    const std::vector<ObjectScanSample> actors =
        FindActiveObjectsByIDAndSettings(nds, kVsMovingHazardObjectID, kVsMovingHazardSettings);
    const std::size_t count = std::min<std::size_t>(
        actors.size(), kTrackedWorldMovingHazardCount);
    std::vector<Diagnostics::DiagnosticMovingHazardSnapshot> hazards;
    hazards.reserve(count);
    for (std::size_t i = 0; i < count; i++)
    {
        Diagnostics::DiagnosticMovingHazardSnapshot hazard;
        hazard.GUID = actors[i].GUID;
        hazard.Base = actors[i].Base;
        hazard.PosX = actors[i].PosX;
        hazard.PosY = actors[i].PosY;
        hazard.VelX = actors[i].VelX;
        hazard.VelY = actors[i].VelY;
        hazard.StateType = actors[i].StateType;
        hazard.Flags = actors[i].Flags;
        hazards.push_back(hazard);
    }
    return hazards;
}

void EmitPlayerLifeEvent(
    int instanceID,
    melonDS::u32 frame,
    int player,
    const char* reason,
    const GameStateSample& sample,
    melonDS::NDS* nds)
{
    if (!G.Diagnostics.DiagnosticEventsEnabled || instanceID < 0 || instanceID >= 16 || player < 0 || player > 1)
        return;
    const bool transitionOnly = reason && std::strcmp(reason, "death-transition") == 0;
    if (!G.DiagnosticsRuntime.ShouldEmitDiagnosticLifeEvent(
            instanceID, player, frame, transitionOnly, 300))
        return;

    if (!transitionOnly)
        G.DiagnosticsRuntime.ScheduleDiagnosticPostTrigger(
            instanceID, frame + kDiagnosticPostTriggerFrames);

    const std::vector<Diagnostics::DiagnosticMovingHazardSnapshot> hazards =
        ReadNearbyDiagnosticHazards(nds);
    const std::vector<DiagnosticFrameSnapshot> ring =
        transitionOnly ? std::vector<DiagnosticFrameSnapshot>{}
                       : DiagnosticRingWindow(instanceID);
    const std::string json = Diagnostics::FormatPlayerLifeEvent(
        G.NetRole == Role::Host ? "host" : "client", reason, instanceID, frame,
        player, sample, hazards, !transitionOnly, ring);

    std::lock_guard<std::mutex> lock(G.Mutex);
    WriteDiagnosticEventLocked(json);
}

void TraceWorldMovingHazardsIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.StateSync.WorldTraceMovingHazards || instanceID < 0 || instanceID >= 16)
        return;
    if ((frame % 60) != 0 || G.GameSync.LastTracedWorldMovingHazardsFrame[instanceID] == frame)
        return;
    G.GameSync.LastTracedWorldMovingHazardsFrame[instanceID] = frame;

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
        RollbackRuntime::ReadMainRAMAddressU32(nds, base + relativeOffset, value);
        std::printf("%s%02X:%08X", relativeOffset == 0 ? "" : "/", relativeOffset, value);
    }
    std::printf("\n");
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
        G.GameSync.LastTracedWorldEffectsFrame[instanceID] == frame)
        return;
    G.GameSync.LastTracedWorldEffectsFrame[instanceID] = frame;

    melonDS::u32 count = 0;
    for (melonDS::u32 slotIndex = 0; slotIndex < kWorldEffectSlotCount; slotIndex++)
    {
        const melonDS::u32 base = kWorldEffectSlotBase + slotIndex * kWorldEffectSlotStride;
        WorldEffectSlotSample slot {};
        if (!GameStateReader::ReadWorldEffectSlot(nds, base, slot))
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
        G.GameSync.LastTracedWorldObjectLifecyclesFrame[instanceID] == frame)
        return;
    G.GameSync.LastTracedWorldObjectLifecyclesFrame[instanceID] = frame;

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

const PacketBridge::IntegrationHooks& PacketBridgeHooks()
{
    static const PacketBridge::IntegrationHooks hooks {
        [] { NetplaySession::SendMatchSeedLocked(NetplaySessionContext()); },
        [](melonDS::NDS* nds, melonDS::u32 frame) {
            NetplaySession::PumpLocked(
                NetplaySessionContext(), NetplaySessionHooks(), nds, frame);
        },
        [](const InputState& input) { return ConvertStockXToTouch(input); },
        [](int instanceID,
            melonDS::u32 frame,
            melonDS::NDS* nds,
            int player,
            const InputState& input) {
            const InputState ruleInput =
                ApplyRuleBasedAIInput(instanceID, frame, nds, player, input);
            return ApplyImitationAIInput(
                instanceID, frame, nds, player, ruleInput);
        },
        [](int instanceID,
            melonDS::u32 frame,
            int player,
            const InputState& input) {
            RecordAIPlayLogAppliedInput(instanceID, frame, player, input);
        },
    };
    return hooks;
}

void ApplyRollbackResimFramePatches(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!nds || instanceID < 0 || instanceID >= 16)
        return;

    MvlGameHooks::ApplyRuntimeConfig(MvlHooksContext(), nds);
    PacketBridge::ForceGameLocalPlayerIDIfNeeded(
        PacketBridgeContext(), frame, nds);
    MvlGameHooks::ClearCameraInitHold(MvlHooksContext(), instanceID, frame, nds);
    MvlGameHooks::ForcePlayerDeathCounters(MvlHooksContext(), instanceID, frame, nds);
    MvlGameHooks::ForcePlayerInventoryPowerups(MvlHooksContext(), instanceID, frame, nds);
    MvlGameHooks::ForcePlayerStarCounters(MvlHooksContext(), instanceID, frame, nds);
}

void ApplyRollbackResimPostFramePatches(melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!nds)
        return;

    PacketBridge::ForceGameLocalPlayerIDIfNeeded(
        PacketBridgeContext(), frame, nds);
    melonDS::NSML_RefreshMarioVsLuigiPacketSlots(nds);
    PacketBridge::ForceGameLocalPlayerIDIfNeeded(
        PacketBridgeContext(), frame, nds);
}

bool RollbackResimulateIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    static const RollbackRuntime::ResimulationHooks hooks {
        [] { return CurrentPacketBridgeLocalPlayer(); },
        [](int hookInstanceID, melonDS::u32 hookFrame, melonDS::NDS* hookNds) {
            ApplyRollbackResimFramePatches(hookInstanceID, hookFrame, hookNds);
        },
        [](int hookInstanceID,
            melonDS::u32 hookFrame,
            melonDS::NDS* hookNds,
            int localPlayer,
            const InputState& localInput,
            const InputState& remoteInput,
            bool hasRemoteInput,
            bool predictedRemoteInput) {
            PacketBridge::WriteJitScratchInputs(
                PacketBridgeContext(),
                PacketBridgeHooks(),
                hookInstanceID,
                hookFrame,
                hookNds,
                localPlayer,
                localInput,
                remoteInput,
                hasRemoteInput,
                predictedRemoteInput);
        },
        [](const InputState& input) { return ConvertStockXToTouch(input); },
        [](melonDS::u32 hookFrame, melonDS::NDS* hookNds) {
            ApplyRollbackResimPostFramePatches(hookFrame, hookNds);
        },
    };
    return RollbackRuntime::ResimulateIfNeeded(
        RollbackContext(), hooks, instanceID, frame, nds);
}

void WritePacketBridgeJitScratchIfNeeded(
    int instanceID,
    melonDS::u32 frame,
    melonDS::NDS* nds,
    const InputState& localInput)
{
    if (!G.RuntimePatch.PacketBridgeJitHelperPatchEnabled || !nds || !nds->MainRAM)
        return;
    if (!G.Enabled)
        return;
    melonDS::u32 startFrame = 0;
    if (G.Input.NetplayOnly)
    {
        startFrame = std::max(startFrame, G.RuntimePatch.PacketBridgeJitHelperPatchFrame);
        startFrame = std::max(startFrame, G.Connection.StartFrame);
    }
    const bool traceScratch = G.Diagnostics.ActiveFrameSpikeTrace;
    const auto scratchStart = std::chrono::steady_clock::now();
    unsigned long long peerStartWaitUs = 0;
    unsigned long long networkUs = 0;
    unsigned long long throttleUs = 0;
    unsigned long long lockstepRemoteWaitUs = 0;
    unsigned long long writeUs = 0;
    bool wroteScratch = false;

    const melonDS::u32 logicalFrame =
        NetplaySession::LogicalFrame(NetplaySessionContext(), frame);
    if (G.Input.NetplayOnly
        && G.Harness.WaitForPeerAtNetplayStart
        && !G.NetplaySession.Handshake.LocalReadyFrame())
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
            NetplaySession::WaitForPeer(
                NetplaySessionContext(), NetplaySessionHooks(), true);
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
            NetplaySession::PumpLocked(
                NetplaySessionContext(), NetplaySessionHooks(), nds, frame);
            NetplaySession::SendMatchSeedLocked(NetplaySessionContext());
            G.InputRuntime.LocalInputs[sendFrame] = localInput;
            NetplaySession::SendInputLocked(
                NetplaySessionContext(), NetplaySessionHooks(), sendFrame, localInput);
            if (G.Input.NetplayOnly)
            {
                auto localIt = G.InputRuntime.LocalInputs.find(logicalFrame);
                effectiveLocalInput = localIt != G.InputRuntime.LocalInputs.end() ? localIt->second : NeutralInput();
            }
            auto it = G.InputRuntime.RemoteInputs.find(logicalFrame);
            if (it != G.InputRuntime.RemoteInputs.end())
            {
                remoteInput = it->second;
                hasRemoteInput = true;
            }
            else if (G.Rollback.Enabled && G.Input.NetplayOnly
                && (G.Connection.StartFrame == 0 || logicalFrame >= G.Connection.StartFrame)
                && NetplaySession::TryWaitForRollbackRemoteInputLocked(
                    NetplaySessionContext(),
                    NetplaySessionHooks(),
                    lock,
                    nds,
                    frame,
                    logicalFrame,
                    remoteInput))
            {
                hasRemoteInput = true;
            }
            else if (G.Rollback.Enabled && G.Input.NetplayOnly
                && (G.Connection.StartFrame == 0 || logicalFrame >= G.Connection.StartFrame))
            {
                hasRemoteInput = RollbackRuntime::ResolveRemoteInputLocked(
                    RollbackContext(), logicalFrame, remoteInput, predictedRemoteInput);
            }
        }
        networkUs = static_cast<unsigned long long>(ElapsedUs(networkStart));

        const auto throttleStart = std::chrono::steady_clock::now();
        NetplaySession::ThrottleFrameLead(
            NetplaySessionContext(), NetplaySessionHooks(), nds, frame, sendFrame);
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
            remoteInput = NetplaySession::WaitForRemoteInput(
                NetplaySessionContext(), NetplaySessionHooks(), logicalFrame);
            lockstepRemoteWaitUs = static_cast<unsigned long long>(ElapsedUs(waitStart));
            hasRemoteInput = true;
        }
    }

    const bool beforeStart = logicalFrame < startFrame;
    if (!beforeStart)
    {
        const auto writeStart = std::chrono::steady_clock::now();
        PacketBridge::WriteJitScratchInputs(
            PacketBridgeContext(),
            PacketBridgeHooks(),
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
            std::min(G.Diagnostics.ActiveFrameSpikeThresholdUs, 10000));
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
        && logicalFrame != G.InputRuntime.LastInputHealthSummaryFrame
        && (logicalFrame % static_cast<melonDS::u32>(G.Input.HealthTraceInterval)) == 0)
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        G.InputRuntime.LastInputHealthSummaryFrame = logicalFrame;
        NetplaySession::PrintInputHealthLocked(
            NetplaySessionContext(),
            "summary",
            frame,
            logicalFrame,
            sendFrame,
            lockstepRemoteWaitUs,
            throttleUs,
            networkUs,
            NetplaySession::CurrentInputLead(NetplaySessionContext(), sendFrame),
            hasRemoteInput,
            predictedRemoteInput);
    }
    if (beforeStart)
        return;
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
        NetplaySession::PumpLocked(NetplaySessionContext(), NetplaySessionHooks());
        if (!G.GameSync.RemoteState.FindLatestGameState(instanceID, frame, sample, sampleFrame))
            return;
    }

    GameStateWriter::GameStateApplyOptions options;
    options.RemotePlayerOnly = G.StateSync.GameApplyRemotePlayerOnly;
    if (options.RemotePlayerOnly)
        options.RemotePlayer = CurrentPacketBridgeLocalPlayer() ^ 1;
    options.CriticalGlobals = G.StateSync.GameApplyCriticalGlobals;
    options.StageObjects = G.StateSync.GameApplyStageObjects;
    options.StarObjects = G.StateSync.GameApplyStarObjects;
    options.PlayerActors = G.StateSync.GameApplyPlayerActors;
    options.StageSceneSettings = G.MvlCurrentStageSceneSettings;
    const GameStateWriter::GameStateApplyResult result =
        GameStateWriter::ApplyGameState(nds, sample, options);

    if (options.RemotePlayerOnly)
    {
        if ((G.Bootstrap.InputTraceEnabled || G.Input.NetplayTrace) &&
            (G.Bootstrap.InputTraceInterval <= 1 || (frame % static_cast<melonDS::u32>(G.Bootstrap.InputTraceInterval)) == 0))
        {
            std::printf("NSMB PoC: applied remote-player snapshot inst=%d frame=%u sampleFrame=%u remotePlayer=%d applied=%d\n",
                instanceID,
                frame,
                sampleFrame,
                options.RemotePlayer,
                result.RemotePlayerApplied ? 1 : 0);
        }
        return;
    }

    if (G.Bootstrap.InputTraceEnabled &&
        (G.Bootstrap.InputTraceInterval <= 1 || (frame % static_cast<melonDS::u32>(G.Bootstrap.InputTraceInterval)) == 0))
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
    if (!G.Diagnostics.HangDiagnosticsEnabled || !nds || !nds->MainRAM || instanceID < 0 || instanceID >= 16)
        return;

    const GameStateSample sample = ReadGameStateSample(nds);
    G.DiagnosticsRuntime.UpdateGameSnapshot(instanceID, frame, sample, NowUnixMs());
}

void TraceGameState(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (G.Diagnostics.GameStateTracePath.empty()) return;
    if (!nds || !nds->MainRAM) return;
    if (instanceID < 0 || instanceID >= 16) return;
    if (frame < G.Diagnostics.GameStateTraceStartFrame) return;
    if (G.Diagnostics.GameStateTraceEndFrame != 0 && frame > G.Diagnostics.GameStateTraceEndFrame) return;
    if ((frame % static_cast<melonDS::u32>(G.Diagnostics.GameStateTraceInterval)) != 0) return;
    if ((kNetRandomValueAddr - kMainRAMBase) + sizeof(melonDS::u32) > nds->MainRAMMask + 1) return;

    const GameStateSample sample = ReadGameStateSample(nds);

    std::lock_guard<std::mutex> lock(G.Mutex);
    if (!G.GameStateTrace.IsOpen()) return;

    GameStateTraceHashes traceHashes;
    const GameStateTraceHashes* extendedHashes = nullptr;
    if (G.Diagnostics.GameStateTraceExtended)
    {
        traceHashes.PlayerGlobal = HashMainRAMRange(nds, kGamePlayerGlobalBlockAddr, 0xC0);
        traceHashes.WifiCandidate = HashMainRAMRange(nds, kGameCandidateWifiBlockAddr, 0x2200);
        traceHashes.RenderCandidate = HashMainRAMRange(nds, kGameCandidateRenderBlockAddr, 0x240);
        traceHashes.NetState = HashMainRAMRange(nds, kNetStateBaseAddr, 0x180);
        extendedHashes = &traceHashes;
    }
    G.GameStateTrace.Write(instanceID, frame, sample, extendedHashes);
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
    if (!G.GameSync.BeginGameStateSync(instanceID, frame)) return;
    const auto mismatch =
        G.GameSync.RecordLocalGameStateHashes(instanceID, frame, hashes);
    if (mismatch)
        ReportGameStateMismatchLocked(*mismatch);

    if (!G.Transport.IsConnected()) return;
    const WireGameState packet = EncodeWireGameState(
        frame,
        static_cast<melonDS::u32>(instanceID),
        sample,
        hashes);

    G.Transport.Send(&packet, sizeof(packet), ENET_PACKET_FLAG_RELIABLE, false);
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
    counters.RemoteInputWaitCount = G.InputRuntime.RemoteInputWaitCount;
    counters.RemoteInputWaitUs = G.InputRuntime.RemoteInputWaitUs;
    counters.RemoteInputWaitMaxUs = G.InputRuntime.RemoteInputWaitMaxUs;
    counters.FrameLeadThrottleCount = G.InputRuntime.FrameLeadThrottleCount;
    counters.FrameLeadThrottleUs = G.InputRuntime.FrameLeadThrottleUs;
    counters.FrameLeadThrottleMaxUs = G.InputRuntime.FrameLeadThrottleMaxUs;
    counters.LastSentInputFrame = G.InputRuntime.LastSentInputFrame;
    counters.LastReceivedInputFrame = G.InputRuntime.LastReceivedInputFrame;
    if (G.InputRuntime.LastSentInputFrame != kNoFrameLimit && G.InputRuntime.LastReceivedInputFrame != kNoFrameLimit)
    {
        counters.InputLead = static_cast<int>(G.InputRuntime.LastSentInputFrame)
            - static_cast<int>(G.InputRuntime.LastReceivedInputFrame);
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
    G.Diagnostics = Config::LoadDiagnosticsConfig(
        static_cast<int>(Diagnostics::kDiagnosticRingCapacity));
    G.Harness = Config::LoadHarnessConfig();
    G.Enabled = G.Bootstrap.Enabled;
    G.TestEnabled = G.Bootstrap.TestEnabled;
    G.DiagnosticsRuntime.ConfigureFrameHeartbeat(
        G.Diagnostics.FrameHeartbeatInterval,
        G.Diagnostics.FrameHeartbeatPath);
    if (!G.Diagnostics.InputRecordPath.empty())
    {
        if (G.InputRecorder.Open(
                G.Diagnostics.InputRecordPath,
                G.Diagnostics.InputRecordStartFrame,
                G.Diagnostics.InputRecordEndFrame,
                G.Diagnostics.InputRecordInstance))
        {
            std::printf("NSMB Test: recording input to %s start=%u end=%u instance=%d\n",
                G.Diagnostics.InputRecordPath.c_str(),
                G.Diagnostics.InputRecordStartFrame,
                G.Diagnostics.InputRecordEndFrame,
                G.Diagnostics.InputRecordInstance);
        }
        else
        {
            std::printf("NSMB Test: failed to open input record file: %s\n",
                G.Diagnostics.InputRecordPath.c_str());
        }
    }
    G.PacketBridge = Config::LoadPacketBridgeConfig();
    G.Input = Config::LoadInputConfig(false);
    G.RuntimePatch = Config::LoadRuntimePatchConfig();
    G.Mvl = Config::LoadMvlConfig();
    G.MvlCurrentStage = G.Mvl.Stage;
    G.MvlCurrentStageSceneSettings = G.Mvl.StageSceneSettings;
    if (!G.Mvl.InvalidCourseMode.empty())
    {
        std::printf("NSMB MvL settings: unknown courseMode=%s; using fixed stage=%d\n",
            G.Mvl.InvalidCourseMode.c_str(),
            G.MvlCurrentStage);
    }

    if (!Config::ParseFrameRanges(G.Diagnostics.RamDumpFrames.c_str(), G.RamDumpRanges))
    {
        std::printf("NSMB Test: invalid RAM dump frame list\n");
        G.RamDumpRanges.clear();
    }
    if (G.Diagnostics.DiagnosticEventsEnabled && !G.Diagnostics.DiagnosticEventsPath.empty())
    {
        std::error_code ec;
        std::filesystem::remove(G.Diagnostics.DiagnosticEventsPath, ec);
    }
    G.StateSync = Config::LoadStateSyncConfig();

    G.AI = Config::LoadAIConfig();
    const NsmbImitationAI::ModelInitializationResult imitationModelResult =
        G.ImitationAI.InitializeModel(
            G.AI.Imitation.Enabled, G.AI.Imitation.ModelPath);
    const std::string imitationModelReport =
        Diagnostics::FormatImitationModelInitializationReport(
            G.AI.Imitation.ModelPath, imitationModelResult);
    std::fputs(imitationModelReport.c_str(), stdout);
    G.Rollback = Config::LoadRollbackConfig();

    if ((G.TestEnabled || G.Enabled) && !G.Diagnostics.GameStateTracePath.empty())
    {
        if (!G.GameStateTrace.Open(G.Diagnostics.GameStateTracePath,
                                  G.Diagnostics.GameStateTraceExtended))
        {
            std::printf("NSMB Test: failed to open game state trace: %s\n", G.Diagnostics.GameStateTracePath.c_str());
        }
    }
    G.AIObservationRuntime.OpenConfiguredLogs(
        G.TestEnabled || G.Enabled, G.Diagnostics);

    if (G.TestEnabled)
    {
        if (!LoadInputScriptLocked())
            G.TestEnabled = false;
        if (!G.DiagnosticsRuntime.ConfigureHashLog(
                G.Diagnostics.HashLogPath,
                G.Diagnostics.ScreenHashEnabled))
        {
            std::printf("NSMB Test: failed to open hash log: %s\n", G.Diagnostics.HashLogPath.c_str());
        }

        const std::string startupReport = Diagnostics::FormatTestStartupReport(
            NowUnixMs(), G.Bootstrap, G.Diagnostics, G.Harness, G.StateSync,
            G.PacketBridge, G.Mvl, G.RamDumpRanges.size(), G.MvlCurrentStage,
            G.MvlCurrentStageSceneSettings);
        std::fputs(startupReport.c_str(), stdout);
        std::printf("NSMB Diagnostics: events=%d path=%s ringFrames=%d diagnosticsFile=%s\n",
            G.Diagnostics.DiagnosticEventsEnabled ? 1 : 0,
            G.Diagnostics.DiagnosticEventsPath.empty() ? "<none>" : G.Diagnostics.DiagnosticEventsPath.c_str(),
            G.Diagnostics.DiagnosticRingFrames,
            G.Diagnostics.DiagnosticsPath.empty() ? "<none>" : G.Diagnostics.DiagnosticsPath.c_str());
        std::fflush(stdout);
    }

    G.Connection = Config::LoadConnectionConfig(G.TestEnabled);
    G.NetRole = G.Connection.Client ? Role::Client : Role::Host;

    if (!G.Enabled) return;


    if (G.NetRole == Role::Host && !G.Mvl.MatchSeedConfigured)
    {
        G.Mvl.MatchSeed = GenerateMatchSeed();
        G.Mvl.MatchSeedConfigured = true;
    }

    if (G.Mvl.MatchSeedConfigured
        && G.Harness.StateLoadDir.empty()
        && !G.PacketBridge.Only)
    {
        G.Mvl.NetRandom.Enabled = true;
        G.Mvl.NetRandom.Auto = true;
        G.Mvl.NetRandom.Value = G.Mvl.MatchSeed;
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
    NetplaySession::StartNetworkPumpThreadIfNeeded(
        NetplaySessionContext(), NetplaySessionHooks());
    StartHangWatchdogIfNeeded();
    TraceHangPhase("startup", "enabled", G.Connection.LocalInstance, 0, 0, 0);
    const std::string startupReport = Diagnostics::FormatNetplayStartupReport(
        NowUnixMs(), G.NetRole == Role::Host ? "host" : "client",
        G.Connection, G.Harness, G.PacketBridge, G.Input, G.Rollback,
        RollbackRuntime::BackendName(G.Rollback), G.Mvl, G.MvlCurrentStage,
        G.MvlCurrentStageSceneSettings);
    std::fputs(startupReport.c_str(), stdout);
    const std::string aiStartupReport = Diagnostics::FormatAIStartupReport(
        G.AI, G.ImitationAI.IsEnabled(), G.ImitationAI.DescribeModel());
    std::fputs(aiStartupReport.c_str(), stdout);
    std::fflush(stdout);
}

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
    MvlGameHooks::ApplyRuntimeConfig(MvlHooksContext(), nds);
}

void RunBeforeFrameBootPhase(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!CanRunFrameHooks(instanceID, nds))
        return;
    SaveMvlAutoRestartBootstrapCheckpointIfNeeded(instanceID, frame, nds);
    RestartMvlAfterResultIfNeeded(instanceID, frame, nds);
}

void RunBeforeFramePatchPhase(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!CanRunFrameHooks(instanceID, nds))
        return;
    MvlGameHooks::ApplyNetRandomPatch(MvlHooksContext(), instanceID, frame, nds);
}

void RunBeforeFrameSetupPhase(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!CanRunFrameHooks(instanceID, nds))
        return;
    MvlGameHooks::ClearCameraInitHold(MvlHooksContext(), instanceID, frame, nds);
    MvlGameHooks::ForcePlayerDeathCounters(MvlHooksContext(), instanceID, frame, nds);
    MvlGameHooks::ForcePlayerPowerups(MvlHooksContext(), instanceID, frame, nds);
    MvlGameHooks::ForcePlayerInventoryPowerups(MvlHooksContext(), instanceID, frame, nds);
    MvlGameHooks::ForcePlayerStarCounters(MvlHooksContext(), instanceID, frame, nds);
}

void RunBeforeFrameActorStatePhase(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!G.Enabled || instanceID < 0 || instanceID >= 16 || !nds)
        return;
    ApplyRemoteGameState(instanceID, frame, nds);
}

InputState BeforeRunFrame(int instanceID, melonDS::u32 frame, melonDS::NDS* nds, const InputState& polledInput)
{
    BeforeHookPhaseTrace phaseTrace(
        G.Diagnostics.ActiveFrameSpikeTrace,
        G.Diagnostics.ActiveFrameSpikeThresholdUs,
        instanceID,
        frame);
    InitFromEnvironment();
    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::Init);
    melonDS::u32 inputFrame = frame;
    if (G.TestEnabled && instanceID >= 0 && instanceID < 16)
        inputFrame = G.Coordinator.TestFrame(instanceID);
    phaseTrace.SetFrame(inputFrame);

    if (G.Enabled && G.Input.NetplayOnly && G.Harness.WaitForPeerBeforeStart && inputFrame == 0
        && !RuleAIProvidesInputForPlayer(CurrentPacketBridgeLocalPlayer() ^ 1)
        && !ImitationAIProvidesInputForPlayer(CurrentPacketBridgeLocalPlayer() ^ 1))
    {
        if (!G.Harness.WaitForPeerAtNetplayStart)
            NetplaySession::WaitForPeer(
                NetplaySessionContext(), NetplaySessionHooks(), true);
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            NetplaySession::PumpLocked(
                NetplaySessionContext(), NetplaySessionHooks(), nds, inputFrame);
            NetplaySession::SendMatchSeedLocked(NetplaySessionContext());
        }
        NetplaySession::WaitForMatchSeed(
            NetplaySessionContext(), NetplaySessionHooks());
    }
    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::StartSync);

    if (G.TestEnabled && instanceID >= 0 && instanceID < 16 && nds)
    {
        TestStateHarness::LoadState(
            {G.Harness, G.Bootstrap, G.Coordinator, G.Mutex},
            instanceID,
            inputFrame,
            nds);
    }
    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::LoadState);

    RunBeforeFrameRuntimeConfigPhase(instanceID, nds);
    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::RuntimeConfig);

    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        RollbackRuntime::RestoreCheckpointForProbeIfNeeded(
            RollbackContext(), instanceID, inputFrame, nds);
    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::ProbeRestore);

    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        PacketBridge::ApplyJitHelperPatchIfNeeded(
            PacketBridgeContext(), instanceID, inputFrame, nds);
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
        PacketBridge::ForceGameLocalPlayerIDIfNeeded(
            PacketBridgeContext(), inputFrame, nds);
    }
    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::PacketBridgeSetup);

    if (G.TestEnabled && instanceID >= 0 && instanceID < 16 && nds)
        MvlGameHooks::ApplyPlayerStickToStar(MvlHooksContext(), instanceID, inputFrame, nds);
    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::TestSnap);
    RunBeforeFrameSetupPhase(instanceID, inputFrame, nds);

    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::Setup);
    RunBeforeFrameActorStatePhase(instanceID, inputFrame, nds);

    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::ActorState);
    WaitForSerialRunTurn(instanceID, inputFrame);
    WaitAtFrameBarrier(Coordination::FrameBarrierKind::Before, instanceID, inputFrame, "before");
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
    G.InputRecorder.Record(instanceID, inputFrame, testInput);
    const melonDS::u32 syncFrame = G.TestEnabled ? inputFrame : frame;

    if (G.Enabled && G.Input.NetplayOnly)
        NetplaySession::WaitForRemoteStartReady(
            NetplaySessionContext(), NetplaySessionHooks(), nds, syncFrame);

    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        RollbackRuntime::SaveCheckpointIfNeeded(
            RollbackContext(), instanceID, syncFrame, nds);
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
        NetplaySession::WaitForPeer(
            NetplaySessionContext(), NetplaySessionHooks());
    }
    else if (syncFrame == 0)
    {
        NetplaySession::WaitForPeer(
            NetplaySessionContext(), NetplaySessionHooks());
        NetplaySession::WaitForMatchSeed(
            NetplaySessionContext(), NetplaySessionHooks());
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
            G.InputRuntime.LocalInputs.emplace(syncFrame + inputDelay, testInput);
            auto delayed = G.InputRuntime.LocalInputs.find(syncFrame);
            packetBridgeInput = delayed != G.InputRuntime.LocalInputs.end() ? delayed->second : NeutralInput();

            const melonDS::u32 keepFrom = syncFrame > 180 ? syncFrame - 180 : 0;
            for (auto it = G.InputRuntime.LocalInputs.begin(); it != G.InputRuntime.LocalInputs.end(); )
            {
                if (it->first < keepFrom)
                    it = G.InputRuntime.LocalInputs.erase(it);
                else
                    ++it;
            }
        }
        if (bridgeNetworkActive && G.PacketBridge.NeutralizeLocalInput)
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            G.PacketBridgeRuntime.StorePacketInput(syncFrame, testInput);
            packetBridgeInput = G.PacketBridge.PreserveLocalTouch
                ? NeutralInputPreservingTouch(testInput)
                : NeutralInput();
        }
        if (bridgeNetworkActive)
        {
            {
                std::lock_guard<std::mutex> lock(G.Mutex);
                PacketBridge::PumpLocked(
                    PacketBridgeContext(), PacketBridgeHooks(), nds, syncFrame);
                PacketBridge::ForceTickIfNeeded(
                    PacketBridgeContext(), instanceID, syncFrame, nds);
            }
            PacketBridge::ForceGameLocalPlayerIDIfNeeded(
                PacketBridgeContext(), syncFrame, nds);
            melonDS::NSML_RefreshMarioVsLuigiPacketSlots(nds);
            PacketBridge::ForceGameLocalPlayerIDIfNeeded(
                PacketBridgeContext(), syncFrame, nds);
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
    const bool networkPumpActive = NetplaySession::ShouldPumpNetworkAtFrame(
        NetplaySessionContext(), syncFrame, sendStartFrame);

    if ((isLocal || G.TestEnabled) && networkPumpActive)
    {
        InputState localInput = testInput;
        if (!isLocal && G.TestEnabled)
            localInput = ApplyInputScript(G.Connection.LocalInstance, syncFrame, NeutralInput());

        std::lock_guard<std::mutex> lock(G.Mutex);
        NetplaySession::PumpLocked(
            NetplaySessionContext(), NetplaySessionHooks(), nds, syncFrame);
        PacketBridge::ApplyPendingPacketsLocked(PacketBridgeContext(), nds);
        NetplaySession::SendMatchSeedLocked(NetplaySessionContext());
        if (netplaySendActive)
        {
            const melonDS::u32 effectiveFrame = syncFrame + delay;
            G.InputRuntime.LocalInputs.emplace(effectiveFrame, localInput);
            for (const auto& [storedFrame, input] : G.InputRuntime.LocalInputs)
                NetplaySession::SendInputLocked(
                    NetplaySessionContext(), NetplaySessionHooks(), storedFrame, input);
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
        WaitAtFrameBarrier(Coordination::FrameBarrierKind::Netplay, instanceID, targetFrame, "netplay");

    if (G.TestEnabled && instanceID >= 0 && instanceID < 16 &&
        !G.Coordinator.IsNetplayLockstepStarted(instanceID))
    {
        bool needsInitialRemoteInput = false;
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            NetplaySession::PumpLocked(
                NetplaySessionContext(), NetplaySessionHooks(), nds, targetFrame);
            PacketBridge::ApplyPendingPacketsLocked(PacketBridgeContext(), nds);
            needsInitialRemoteInput = G.Coordinator.NeedsInitialRemoteInput(
                G.InputRuntime.RemoteInputs.find(targetFrame) != G.InputRuntime.RemoteInputs.end());
        }

        if (needsInitialRemoteInput)
            (void)NetplaySession::WaitForRemoteInput(
                NetplaySessionContext(), NetplaySessionHooks(), targetFrame);

        std::lock_guard<std::mutex> lock(G.Mutex);

        G.Coordinator.MarkNetplayLockstepStarted(instanceID);
        std::printf("NSMB PoC: lockstep started inst=%d frame=%u\n", instanceID, targetFrame);
    }

    if (isLocal)
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        if (targetFrame > 120)
            G.InputRuntime.PruneHistory(targetFrame - 120);

        auto it = G.InputRuntime.LocalInputs.find(targetFrame);
        const InputState delayedLocalInput = it != G.InputRuntime.LocalInputs.end() ? it->second : NeutralInput();
        if (!G.Connection.LocalWaitsForRemote)
            return ConvertStockXToTouch(delayedLocalInput);
        if (NetplaySession::IsPastTestInputRange(NetplaySessionContext(), targetFrame))
            return ConvertStockXToTouch(delayedLocalInput);
    }
    else if (NetplaySession::IsPastTestInputRange(NetplaySessionContext(), targetFrame))
    {
        return NeutralInput();
    }

    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::Gate);
    const InputState remoteInput = NetplaySession::WaitForRemoteInput(
        NetplaySessionContext(), NetplaySessionHooks(), targetFrame);
    phaseTrace.Mark(BeforeHookPhaseTrace::Phase::RemoteWait);

    if (isLocal)
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        auto it = G.InputRuntime.LocalInputs.find(targetFrame);
        return ConvertStockXToTouch(it != G.InputRuntime.LocalInputs.end() ? it->second : NeutralInput());
    }

    return ConvertStockXToTouch(remoteInput);
}

void TracePlayerLifeChanges(int instanceID, melonDS::u32 frame,
                            melonDS::NDS *nds) {
  if ((!G.RuntimePatch.TracePlayerLifeChanges &&
       !G.Diagnostics.DiagnosticEventsEnabled) ||
      !nds || !nds->MainRAM)
    return;
  if (instanceID < 0 || instanceID >= 16)
    return;

  Diagnostics::PlayerLifeState current;
  current.Lives[0] = nds->ARM9Read32(kGamePlayerLivesAddr);
  current.Lives[1] =
      nds->ARM9Read32(kGamePlayerLivesAddr + sizeof(melonDS::u32));
  current.Deaths[0] = nds->ARM9Read32(kGamePlayerDeathsAddr);
  current.Deaths[1] =
      nds->ARM9Read32(kGamePlayerDeathsAddr + sizeof(melonDS::u32));
  current.Dead[0] = nds->ARM9Read8(kGamePlayerDeadAddr);
  current.Dead[1] = nds->ARM9Read8(kGamePlayerDeadAddr + 1);
  current.Transition[0] = nds->ARM9Read32(kGamePlayerTransitionStatusAddr);
  current.Transition[1] =
      nds->ARM9Read32(kGamePlayerTransitionStatusAddr + sizeof(melonDS::u32));
  const Diagnostics::PlayerLifeObservation observation =
      G.DiagnosticsRuntime.ObservePlayerLifeState(instanceID, current);
  if (!observation.Accepted || !observation.Changed)
    return;

  const bool valid = observation.HadPrevious;
  const Diagnostics::PlayerLifeState &last = observation.Previous;
  const melonDS::u32 player0Lives = current.Lives[0];
  const melonDS::u32 player1Lives = current.Lives[1];
  const melonDS::u32 player0Deaths = current.Deaths[0];
  const melonDS::u32 player1Deaths = current.Deaths[1];
  const melonDS::u32 player0Dead = current.Dead[0];
  const melonDS::u32 player1Dead = current.Dead[1];
  const melonDS::u32 transition0 = current.Transition[0];
  const melonDS::u32 transition1 = current.Transition[1];

  if (!IsMarioVsLuigiGameplay(nds) && frame < 800)
    return;

  const GameStateSample sample = ReadGameStateSample(nds);
  if (G.RuntimePatch.TracePlayerLifeChanges) {
    std::printf(
        "NSMB LifeDelta: inst=%d frame=%u lives=%u/%u deaths=%u/%u dead=%u/%u "
        "trans=%u/%u "
        "cam={x=%08X/%08X y=%08X/%08X w=%08X/%08X h=%08X/%08X} "
        "p0={found=%u base=%08X pid11E=%u pid7B4=%u def=%u tring=%u updLock=%u "
        "vis=%u x=%08X y=%08X vel=%08X/%08X flags=%08X act=%08X sub=%08X "
        "phy=%08X transFlag=%08X coll=%08X env=%08X linked=%08X "
        "transitFunc=%08X transitArg=%08X} "
        "p1={found=%u base=%08X pid11E=%u pid7B4=%u def=%u tring=%u updLock=%u "
        "vis=%u x=%08X y=%08X vel=%08X/%08X flags=%08X act=%08X sub=%08X "
        "phy=%08X transFlag=%08X coll=%08X env=%08X linked=%08X "
        "transitFunc=%08X transitArg=%08X}\n",
        instanceID, frame, sample.Player0Lives, sample.Player1Lives,
        sample.Player0Deaths, sample.Player1Deaths, sample.Player0Dead,
        sample.Player1Dead, sample.PlayerTransitionStatus0,
        sample.PlayerTransitionStatus1, sample.StageCameraGlobalX0,
        sample.StageCameraGlobalX1, sample.StageCameraGlobalY0,
        sample.StageCameraGlobalY1, sample.StageCameraGlobalWidth0,
        sample.StageCameraGlobalWidth1, sample.StageCameraGlobalHeight0,
        sample.StageCameraGlobalHeight1, sample.PlayerActor0Found,
        sample.PlayerActor0Base, sample.PlayerActor0PlayerID,
        sample.PlayerActor0PlayerBaseID, sample.PlayerActor0DefeatedFlag,
        sample.PlayerActor0TransitioningFlag, sample.PlayerActor0UpdateLocked,
        sample.PlayerActor0VisibleFlag, sample.PlayerActor0PosX,
        sample.PlayerActor0PosY, sample.PlayerActor0VelX,
        sample.PlayerActor0VelY, sample.PlayerActor0Flags,
        sample.PlayerActor0ActionFlag, sample.PlayerActor0SubActionFlag,
        sample.PlayerActor0PhysicsFlag, sample.PlayerActor0TransitionFlag,
        sample.PlayerActor0CollisionFlag, sample.PlayerActor0EnvironmentFlag,
        sample.PlayerActor0LinkedActor, sample.PlayerActor0TransitFunc,
        sample.PlayerActor0TransitArg, sample.PlayerActor1Found,
        sample.PlayerActor1Base, sample.PlayerActor1PlayerID,
        sample.PlayerActor1PlayerBaseID, sample.PlayerActor1DefeatedFlag,
        sample.PlayerActor1TransitioningFlag, sample.PlayerActor1UpdateLocked,
        sample.PlayerActor1VisibleFlag, sample.PlayerActor1PosX,
        sample.PlayerActor1PosY, sample.PlayerActor1VelX,
        sample.PlayerActor1VelY, sample.PlayerActor1Flags,
        sample.PlayerActor1ActionFlag, sample.PlayerActor1SubActionFlag,
        sample.PlayerActor1PhysicsFlag, sample.PlayerActor1TransitionFlag,
        sample.PlayerActor1CollisionFlag, sample.PlayerActor1EnvironmentFlag,
        sample.PlayerActor1LinkedActor, sample.PlayerActor1TransitFunc,
        sample.PlayerActor1TransitArg);
  }

  if (valid) {
    const bool player0DeathEvent =
        (sample.PlayerActor0Found != 0 || player0Deaths != 0 ||
         player0Dead != 0) &&
        (player0Deaths > last.Deaths[0] || player0Lives < last.Lives[0] ||
         (player0Dead != last.Dead[0] && player0Dead != 0));
    const bool player1DeathEvent =
        (sample.PlayerActor1Found != 0 || player1Deaths != 0 ||
         player1Dead != 0) &&
        (player1Deaths > last.Deaths[1] || player1Lives < last.Lives[1] ||
         (player1Dead != last.Dead[1] && player1Dead != 0));
    const bool player0TransitionEvent =
        !player0DeathEvent && transition0 != last.Transition[0] &&
        (sample.PlayerActor0DefeatedFlag != 0 ||
         sample.PlayerActor0TransitioningFlag != 0);
    const bool player1TransitionEvent =
        !player1DeathEvent && transition1 != last.Transition[1] &&
        (sample.PlayerActor1DefeatedFlag != 0 ||
         sample.PlayerActor1TransitioningFlag != 0);
    if (player0DeathEvent)
      EmitPlayerLifeEvent(instanceID, frame, 0, "death", sample, nds);
    else if (player0TransitionEvent)
      EmitPlayerLifeEvent(instanceID, frame, 0, "death-transition", sample,
                          nds);
    if (player1DeathEvent)
      EmitPlayerLifeEvent(instanceID, frame, 1, "death", sample, nds);
    else if (player1TransitionEvent)
      EmitPlayerLifeEvent(instanceID, frame, 1, "death-transition", sample,
                          nds);
  }
}

void TraceGameplayHeartbeatIfNeeded(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!nds || !nds->MainRAM ||
        !G.DiagnosticsRuntime.ShouldTraceGameplayHeartbeat(
            instanceID,
            frame,
            G.Connection.StartFrame,
            G.Diagnostics.GameplayHeartbeatInterval))
        return;
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
    const std::size_t hazardCount =
        std::min(hazards.size(), kTrackedWorldMovingHazardCount);
    for (std::size_t i = 0; i < kTrackedWorldMovingHazardCount; i++)
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

    const melonDS::u32 logFrame = G.Coordinator.AdvanceTestFrame(instanceID);
    if (logFrame == 1)
        G.DiagnosticsRuntime.StartTestTimer(std::chrono::steady_clock::now());
    const melonDS::u32 activeStartFrame = G.Diagnostics.ActiveFpsStartFrame != 0
        ? G.Diagnostics.ActiveFpsStartFrame
        : (G.Connection.StartFrame != 0
            ? G.Connection.StartFrame + 120
            : 120);
    if (logFrame >= activeStartFrame)
        G.DiagnosticsRuntime.StartActiveTimer(
            instanceID, logFrame, std::chrono::steady_clock::now());
    RecordActiveFrameTiming(instanceID, logFrame);
    const bool heartbeatActive =
        G.DiagnosticsRuntime.IsActiveTimerStarted(instanceID) ||
        logFrame >= activeStartFrame;
    G.DiagnosticsRuntime.PublishFrameHeartbeat(instanceID, logFrame, heartbeatActive);
    return logFrame;
}

void RunAfterFramePacketBridgePhase(melonDS::u32 logFrame, melonDS::NDS* nds)
{
    const bool bridgeNetworkActive =
        !G.Harness.DeferNetworkUntilStart || G.Connection.StartFrame == 0 || logFrame >= G.Connection.StartFrame;
    if (G.Enabled && G.PacketBridge.Enabled && bridgeNetworkActive)
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        PacketBridge::PumpLocked(
            PacketBridgeContext(), PacketBridgeHooks(), nds, logFrame);
        PacketBridge::ForceGameLocalPlayerIDIfNeeded(
            PacketBridgeContext(), logFrame, nds);
        PacketBridge::CaptureAndSendPacketLocked(
            PacketBridgeContext(), PacketBridgeHooks(), logFrame, nds);
    }
    if (G.Enabled && G.PacketBridge.Enabled && bridgeNetworkActive)
    {
        PacketBridge::ForceGameLocalPlayerIDIfNeeded(
            PacketBridgeContext(), logFrame, nds);
        melonDS::NSML_RefreshMarioVsLuigiPacketSlots(nds);
        PacketBridge::ForceGameLocalPlayerIDIfNeeded(
            PacketBridgeContext(), logFrame, nds);
    }
    if (G.Enabled && G.PacketBridge.Enabled && bridgeNetworkActive)
        PacketBridge::ThrottleFrameLead(
            PacketBridgeContext(), PacketBridgeHooks(), nds, logFrame);
}

void RunAfterFrameRuntimePatchPhase(int instanceID, melonDS::u32 logFrame, melonDS::NDS* nds)
{
    if (!CanRunFrameHooks(instanceID, nds))
        return;

    MvlGameHooks::ForcePlayerDeathCounters(MvlHooksContext(), instanceID, logFrame, nds);
    MvlGameHooks::ForcePlayerPowerups(MvlHooksContext(), instanceID, logFrame, nds);
    MvlGameHooks::ForcePlayerInventoryPowerups(MvlHooksContext(), instanceID, logFrame, nds);
    MvlGameHooks::ForcePlayerStarCounters(MvlHooksContext(), instanceID, logFrame, nds);
    SaveMvlAutoRestartCheckpointIfNeeded(instanceID, logFrame, nds);
}

void CaptureScreenshotIfNeeded(int instanceID, melonDS::u32 frame,
    melonDS::NDS* nds)
{
    if (!nds ||
        !Diagnostics::ShouldCaptureScreenshotFrame(G.Diagnostics, frame))
        return;

    void* topBuffer = nullptr;
    void* bottomBuffer = nullptr;
    Diagnostics::ScreenshotFrame screenshot;
    screenshot.FramebufferAvailable =
        nds->GPU.GetFramebuffers(&topBuffer, &bottomBuffer);
    screenshot.TopBuffer = topBuffer;
    screenshot.BottomBuffer = bottomBuffer;
    if (screenshot.FramebufferAvailable && topBuffer && bottomBuffer)
    {
        screenshot.DisplayControlA = nds->ARM9Read32(0x04000000);
        screenshot.DisplayControlB = nds->ARM9Read32(0x04001000);
        screenshot.DisplayStatus = nds->ARM9Read16(0x04000004);
        screenshot.PowerControl = nds->ARM9Read16(0x04000304);
        screenshot.BlendControlA = nds->ARM9Read16(0x04000050);
        screenshot.BlendY_A = nds->ARM9Read16(0x04000054);
        screenshot.BlendControlB = nds->ARM9Read16(0x04001050);
        screenshot.BlendY_B = nds->ARM9Read16(0x04001054);
        screenshot.NetState = nds->ARM9Read8(0x02088804);
        screenshot.NetFlags = nds->ARM9Read16(0x0208883C);
    }
    Diagnostics::CaptureScreenshot(
        G.Diagnostics, instanceID, frame, screenshot);
}

void SaveAfterFrameArtifacts(int instanceID, melonDS::u32 logFrame, melonDS::NDS* nds)
{
    const TestStateHarness::Context stateHarness{
        G.Harness, G.Bootstrap, G.Coordinator, G.Mutex};
    TestStateHarness::SaveState(stateHarness, instanceID, logFrame, nds);
    TestStateHarness::SaveLocalMPState(stateHarness, logFrame);
    CaptureScreenshotIfNeeded(instanceID, logFrame, nds);
    Diagnostics::CaptureRamDumpIfNeeded(
        G.Diagnostics, G.RamDumpRanges, instanceID, logFrame,
        nds ? nds->MainRAM : nullptr,
        nds ? std::min<melonDS::u32>(nds->MainRAMMask + 1, 0x400000) : 0);
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
    WaitAtFrameBarrier(Coordination::FrameBarrierKind::After, instanceID, logFrame, "after");
    AdvanceSerialRunTurn(instanceID, logFrame - 1);
    NetplaySession::WaitForPeerAtStartBarrier(
        NetplaySessionContext(), NetplaySessionHooks(), instanceID, logFrame);
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
    RollbackRuntime::TraceStatsIfNeeded(RollbackContext(), logFrame);
    const auto afterRollbackTrace = std::chrono::steady_clock::now();

    TraceHangPhase("begin", "runtime-force", instanceID, logFrame, logFrame, logFrame);
    RunAfterFrameRuntimePatchPhase(instanceID, logFrame, nds);
    const auto afterRuntimeForce = std::chrono::steady_clock::now();

    TraceHangPhase("begin", "artifacts", instanceID, logFrame, logFrame, logFrame);
    SaveAfterFrameArtifacts(instanceID, logFrame, nds);
    const auto afterArtifacts = std::chrono::steady_clock::now();
    const auto afterPreSnapshot = std::chrono::steady_clock::now();
    TraceHangPhase("begin", "world-trace", instanceID, logFrame, logFrame, logFrame);
    if (CanRunFrameHooks(instanceID, nds))
    {
        TraceWorldMovingHazardsIfNeeded(instanceID, logFrame, nds);
        TraceWorldObjectLifecyclesIfNeeded(instanceID, logFrame, nds);
        TraceWorldEffectsIfNeeded(instanceID, logFrame, nds);
    }
    const auto afterWorldTrace = std::chrono::steady_clock::now();
    TraceHangPhase("begin", "game-state-trace", instanceID, logFrame, logFrame, logFrame);
    TraceGameState(instanceID, logFrame, nds);
    TraceAIPlayLog(instanceID, logFrame, nds);
    const auto afterTrace = std::chrono::steady_clock::now();
    TraceHangPhase("begin", "sync-game", instanceID, logFrame, logFrame, logFrame);
    SyncGameState(instanceID, logFrame, nds);
    const auto afterSyncGame = std::chrono::steady_clock::now();
    TraceHangPhase("end", "after-frame", instanceID, logFrame, logFrame, logFrame);

    if (G.Diagnostics.ActiveFrameSpikeTrace)
    {
        const auto totalUs = std::chrono::duration_cast<std::chrono::microseconds>(
            afterSyncGame - afterHookCallStart).count();
        if (totalUs >= std::min(G.Diagnostics.ActiveFrameSpikeThresholdUs, 10000))
        {
            const auto elapsedMs = [](auto start, auto end) {
                return std::chrono::duration<double, std::milli>(end - start).count();
            };
            std::printf(
                "NSMB AfterHookPhaseSpike: inst=%d frame=%u totalMs=%.3f initMs=%.3f heartbeatMs=%.3f barrierMs=%.3f bridgeMs=%.3f lifeTraceMs=%.3f diagnosticSnapshotMs=%.3f rollbackTraceMs=%.3f runtimeForceMs=%.3f artifactsMs=%.3f preSnapshotTailMs=%.3f worldTraceMs=%.3f traceMs=%.3f syncGameMs=%.3f\n",
                instanceID,
                logFrame,
                elapsedMs(afterHookCallStart, afterSyncGame),
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
                elapsedMs(afterPreSnapshot, afterWorldTrace),
                elapsedMs(afterWorldTrace, afterTrace),
                elapsedMs(afterTrace, afterSyncGame));
        }
    }

    if (!G.Bootstrap.HashEnabled) return;
    if ((logFrame % static_cast<melonDS::u32>(G.Bootstrap.HashInterval)) != 0) return;

    const melonDS::u64 hash = HashNDS(nds);
    const melonDS::u64 screenHash = G.Diagnostics.ScreenHashEnabled ? HashFramebuffers(nds) : 0;
    G.DiagnosticsRuntime.RecordFrameHash(instanceID, logFrame, hash, screenHash);
}

bool ShouldQuitAfterFrame(int instanceID, melonDS::u32 frame)
{
    InitFromEnvironment();
    if (!G.TestEnabled || G.Bootstrap.TestFrames == kNoFrameLimit) return false;
    if (instanceID != G.Bootstrap.TestInstanceCount - 1) return false;
    if (G.Coordinator.TestFrame(instanceID) < G.Bootstrap.TestFrames) return false;

    std::lock_guard<std::mutex> lock(G.Mutex);
    if (!G.Coordinator.AllTestFramesReached(
            G.Bootstrap.TestInstanceCount, G.Bootstrap.TestFrames))
        return false;

    if (!G.TestAnnouncedQuit)
    {
        G.TestAnnouncedQuit = true;
        const std::int64_t elapsedMs = G.DiagnosticsRuntime.TestElapsedMs(
            std::chrono::steady_clock::now());
        const double fps = elapsedMs > 0
            ? (static_cast<double>(G.Bootstrap.TestFrames) * 1000.0) / static_cast<double>(elapsedMs)
            : 0.0;
        std::printf("NSMB Test: frame limit reached at frame=%u instances=%d elapsedMs=%lld fps=%.2f\n",
            G.Bootstrap.TestFrames,
            G.Bootstrap.TestInstanceCount,
            static_cast<long long>(elapsedMs),
            fps);
        const Diagnostics::Runtime::ActiveFrameSummary activeTiming =
            G.DiagnosticsRuntime.ActiveFrameTimingSummary(
                instanceID,
                G.Bootstrap.TestFrames,
                std::chrono::steady_clock::now());
        if (activeTiming.Started && activeTiming.Frames > 0)
        {
            const double activeFps = activeTiming.ElapsedMs > 0
                ? (static_cast<double>(activeTiming.Frames) * 1000.0) / static_cast<double>(activeTiming.ElapsedMs)
                : 0.0;
            std::printf("NSMB Test: active fps startFrame=%u frames=%u elapsedMs=%lld fps=%.2f\n",
                activeTiming.StartFrame,
                activeTiming.Frames,
                static_cast<long long>(activeTiming.ElapsedMs),
                activeFps);
            if (activeTiming.Samples > 0)
            {
                const double avgFrameMs =
                    static_cast<double>(activeTiming.TotalUs) /
                    static_cast<double>(activeTiming.Samples) / 1000.0;
                const double maxFrameMs =
                    static_cast<double>(activeTiming.MaxUs) / 1000.0;
                std::printf(
                    "NSMB Test: active frame timing startFrame=%u samples=%u avgFrameMs=%.3f maxFrameMs=%.3f maxFrame=%u over16ms=%u over25ms=%u over33ms=%u spikeThresholdMs=%.3f\n",
                    activeTiming.StartFrame,
                    activeTiming.Samples,
                    avgFrameMs,
                    maxFrameMs,
                    activeTiming.MaxFrame,
                    activeTiming.Over16ms,
                    activeTiming.Over25ms,
                    activeTiming.Over33ms,
                    static_cast<double>(G.Diagnostics.ActiveFrameSpikeThresholdUs) / 1000.0);
            }
        }
        if (G.Enabled && G.Input.NetplayOnly)
        {
            const double remoteAvgUs = G.InputRuntime.RemoteInputWaitCount > 0
                ? static_cast<double>(G.InputRuntime.RemoteInputWaitUs) / static_cast<double>(G.InputRuntime.RemoteInputWaitCount)
                : 0.0;
            const double throttleAvgUs = G.InputRuntime.FrameLeadThrottleCount > 0
                ? static_cast<double>(G.InputRuntime.FrameLeadThrottleUs) / static_cast<double>(G.InputRuntime.FrameLeadThrottleCount)
                : 0.0;
            std::printf(
                "NSMB Test: input wait stats remoteWaitCount=%llu remoteWaitAvgUs=%.1f remoteWaitMaxUs=%llu remoteWaitLoops=%llu throttleCount=%llu throttleAvgUs=%.1f throttleMaxUs=%llu throttleLoops=%llu\n",
                G.InputRuntime.RemoteInputWaitCount,
                remoteAvgUs,
                G.InputRuntime.RemoteInputWaitMaxUs,
                G.InputRuntime.RemoteInputWaitLoops,
                G.InputRuntime.FrameLeadThrottleCount,
                throttleAvgUs,
                G.InputRuntime.FrameLeadThrottleMaxUs,
                G.InputRuntime.FrameLeadThrottleLoops);
        }
        std::fflush(nullptr);
        if (G.Enabled && G.Bootstrap.QuitGraceMs > 0)
        {
            const auto end = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(G.Bootstrap.QuitGraceMs);
            while (std::chrono::steady_clock::now() < end)
            {
                NetplaySession::PumpLocked(
                    NetplaySessionContext(), NetplaySessionHooks());
                G.Transport.Flush();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        G.InputRecorder.Close();
        std::_Exit(0);
    }
    return true;
}

void Shutdown()
{
    StopDiagnostics();
    NetplaySession::StopNetworkPumpThread(NetplaySessionContext());

    G.InputRecorder.Close();

    std::lock_guard<std::mutex> lock(G.Mutex);

    G.Transport.Shutdown();

    G.GameStateTrace.Close();
    G.AIObservationRuntime.CloseLogs();
}

}
