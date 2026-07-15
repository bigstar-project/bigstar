/*
    Experimental NSMB Mario vs Luigi netplay runtime.

    Usage example:
      Host:
        MELONDS_NSML_POC=1 MELONDS_NSML_ROLE=host MELONDS_NSML_PORT=8065 MELONDS_NSML_LOCAL_INSTANCE=0 melonDS.exe
      Client:
        MELONDS_NSML_POC=1 MELONDS_NSML_ROLE=client MELONDS_NSML_PEER=HOST_IP MELONDS_NSML_PORT=8065 MELONDS_NSML_LOCAL_INSTANCE=1 melonDS.exe

    Both sides should run two melonDS instances with Local MP enabled and the
    same ROM/BIOS/firmware/savestate setup. This module exchanges only input.
*/

#include "NsmbMvlNetplayRuntime.h"
#include "NsmbNetplayConfig.h"
#include "NsmbPacketBridgeIntegration.h"
#include "NsmbPacketBridgeRuntime.h"
#include "NsmbMvlRuntime.h"
#include "NsmbMvlGameHooks.h"
#include "NsmbMvlLifecycle.h"
#include "NsmbNetplayCoordinator.h"
#include "NsmbInputDelivery.h"
#include "NsmbInputProtocol.h"
#include "NsmbNetplayProtocol.h"
#include "NsmbNetplayTransport.h"
#include "NsmbNetplayDiagnostics.h"
#include "NsmbGameplayDiagnostics.h"
#include "NsmbNetplaySession.h"
#include "NsmbTestStateHarness.h"
#include "NsmbGameState.h"
#include "NsmbGameStateSync.h"
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

namespace NsmbMvlNetplay
{

namespace
{

using GameStateModel::GameStateHashMismatch;
using GameStateModel::GameStateSample;
using GameStateModel::GameStateTraceWriter;
using GameStateModel::StateSyncRuntime;
using GameStateReader::ObjectScanSample;
using GameStateReader::PlayerActorScanSample;
using GameStateReader::GameStateObjectScanCache;
using GameStateReader::BuildGameStateObjectScanCache;
using GameStateReader::FindObjectByID;
using GameStateReader::FindObjectByIDAndSettingsLoose;
using GameStateReader::FindPlayerActors;
using GameStateReader::HashFramebuffers;
using GameStateReader::HashNDS;
constexpr melonDS::u32 kNoFrameLimit = 0;
constexpr melonDS::u32 kGameStageGroupAddr = 0x02085A18;
constexpr melonDS::u32 kGameVsModeAddr = 0x02085A84;

unsigned long long NowUnixMs()
{
    return static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}
constexpr melonDS::u32 kInputKeyXMask = 1u << 10;
constexpr melonDS::u16 kMvlStockItemTouchX = 217;
constexpr melonDS::u16 kMvlStockItemTouchY = 153;
constexpr melonDS::u16 kStageSceneObjectID = 0x0003;
constexpr melonDS::u32 kMvlStageSceneDefaultSettings = 0x00B4FF00;
constexpr melonDS::u16 kStageControllerObjectID = 0x0130;
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

GameStateSample ReadGameStateSample(melonDS::NDS* nds);
using Diagnostics::BeforeHookPhaseTrace;

int CurrentPacketBridgeLocalPlayer();
const PacketBridge::IntegrationHooks& PacketBridgeHooks();
const NetplaySession::Hooks& NetplaySessionHooks();
const GameStateSync::Hooks& GameStateSyncHooks();
const MvlLifecycle::Hooks& MvlLifecycleHooks();

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

GameStateSync::Context GameStateSyncContext()
{
    return {
        G.Bootstrap,
        G.Diagnostics,
        G.StateSync,
        G.Input,
        G.Connection,
        G.DiagnosticsRuntime,
        G.GameSync,
        G.GameStateTrace,
        G.Transport,
        G.Mutex,
        G.MvlCurrentStageSceneSettings,
        G.Enabled,
        G.NetRole == Role::Client,
    };
}

MvlLifecycle::Context MvlLifecycleContext()
{
    return {
        G.Mvl,
        G.Connection,
        G.PacketBridge,
        G.Input,
        G.RuntimePatch,
        G.Rollback,
        G.MvlSeries,
        G.PacketBridgeRuntime,
        G.NetplaySession,
        G.InputRuntime,
        G.Coordinator,
        G.DiagnosticsRuntime,
        G.GameSync,
        G.GameStateTrace,
        G.Mutex,
        G.MvlCurrentStage,
        G.MvlCurrentStageSceneSettings,
        G.NetRole == Role::Host,
        G.NetRole == Role::Client,
    };
}

GameplayDiagnostics::Context GameplayDiagnosticsContext()
{
    return {
        G.Diagnostics,
        G.Connection,
        G.StateSync,
        G.RuntimePatch,
        G.DiagnosticsRuntime,
        G.InputRuntime,
        G.NetplaySession,
        G.GameSync,
        G.RollbackStats,
        G.Mutex,
        G.NetRole == Role::Host,
    };
}

const GameplayDiagnostics::Hooks& GameplayDiagnosticsHooks()
{
    static const GameplayDiagnostics::Hooks hooks {
        [](melonDS::NDS* nds) { return ReadGameStateSample(nds); },
        [](melonDS::NDS* nds) { return IsMarioVsLuigiGameplay(nds); },
    };
    return hooks;
}

AIObservation::Context AIObservationContext()
{
    return {
        G.AI,
        G.Diagnostics,
        G.ImitationAI,
        G.AIObservationRuntime,
        G.NetRole == Role::Host
            ? AIObservation::Role::Host
            : AIObservation::Role::Client,
        CurrentPacketBridgeLocalPlayer(),
    };
}

const AIObservation::Hooks& AIObservationHooks()
{
    static const AIObservation::Hooks hooks {
        [](melonDS::NDS* nds) { return ReadGameStateSample(nds); },
        [](melonDS::NDS* nds) { return IsMarioVsLuigiGameplay(nds); },
    };
    return hooks;
}

void TraceHangPhase(const char* event, const char* phase, int instanceID = -1,
    melonDS::u32 frame = 0, melonDS::u32 logicalFrame = 0, melonDS::u32 sendFrame = 0);


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
    frameStateServices.ObjectCategory = AIObservation::ObjectCategory;
    frameStateServices.DeriveTerrainSummary = AIObservation::DeriveTerrainSummary;
    frameStateServices.TargetHasFloorBelow = AIObservation::TargetHasFloorBelow;
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

unsigned long long ElapsedUs(std::chrono::steady_clock::time_point start)
{
    return static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count());
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
            GameplayDiagnostics::EmitStartReadyEventLocked(
                GameplayDiagnosticsContext(), direction, localFrame, remoteFrame);
        },
        [](const void* data, std::size_t size, melonDS::NDS* nds, melonDS::u32 localFrame) {
            PacketBridge::ReceivePacketLocked(
                PacketBridgeContext(),
                data,
                size,
                nds,
                localFrame,
                MvlLifecycle::RestartPacketCutoffFrame(MvlLifecycleContext()));
        },
        [](const void* data, std::size_t size) {
            GameStateSync::HandleReceivedPacketLocked(
                GameStateSyncContext(), GameStateSyncHooks(), data, size);
        },
        [](melonDS::NDS* nds) {
            return IsInputNetplayGameplayStartReady(nds);
        },
    };
    return hooks;
}

const GameStateSync::Hooks& GameStateSyncHooks()
{
    static const GameStateSync::Hooks hooks {
        [] {
            NetplaySession::PumpLocked(
                NetplaySessionContext(), NetplaySessionHooks());
        },
        [] { return CurrentPacketBridgeLocalPlayer(); },
        [](const GameStateHashMismatch& mismatch) {
            GameplayDiagnostics::ReportGameStateMismatchLocked(
                GameplayDiagnosticsContext(), mismatch);
        },
    };
    return hooks;
}

const MvlLifecycle::Hooks& MvlLifecycleHooks()
{
    static const MvlLifecycle::Hooks hooks {
        [](melonDS::NDS* nds) { return ReadGameStateSample(nds); },
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

int CurrentPacketBridgeLocalPlayer()
{
    if (G.NetRole == Role::Client)
        return 1;
    return 0;
}

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
            return AIObservation::ApplyImitationInput(
                AIObservationContext(), AIObservationHooks(),
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
        && !AIObservation::ProvidesImitationInput(
            AIObservationContext(), localPlayer ^ 1))
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
            AIObservation::ProvidesImitationInput(
                AIObservationContext(), localPlayer ^ 1);
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

GameStateSample ReadGameStateSample(melonDS::NDS* nds)
{
    return GameStateSync::ReadSample(nds, G.MvlCurrentStageSceneSettings);
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
        std::printf("NSMB MvL Netplay: ENet initialization failed\n");
        G.Enabled = false;
        return;
    }
    if (transportResult == NsmbNetplayTransport::InitializeResult::HostCreationFailed)
    {
        std::printf("NSMB MvL Netplay: failed to create ENet host\n");
        G.Enabled = false;
        return;
    }
    if (G.NetRole == Role::Client && !G.Transport.IsConnecting())
    {
        std::printf("NSMB MvL Netplay: failed to queue peer connect\n");
        std::fflush(stdout);
    }

    G.Ready = true;
    GameplayDiagnostics::EmitStartupEvent(GameplayDiagnosticsContext());
    NetplaySession::StartNetworkPumpThreadIfNeeded(
        NetplaySessionContext(), NetplaySessionHooks());
    GameplayDiagnostics::StartHangWatchdog(GameplayDiagnosticsContext());
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
    MvlLifecycle::RefreshGameSelection(MvlLifecycleContext(), instanceID);
    MvlGameHooks::ApplyRuntimeConfig(MvlHooksContext(), nds);
}

void RunBeforeFrameBootPhase(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    if (!CanRunFrameHooks(instanceID, nds))
        return;
    MvlLifecycle::SaveBootstrapCheckpointIfNeeded(
        MvlLifecycleContext(), instanceID, frame, nds);
    MvlLifecycle::RestartAfterResultIfNeeded(
        MvlLifecycleContext(), instanceID, frame, nds);
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
    GameStateSync::ApplyRemote(
        GameStateSyncContext(), GameStateSyncHooks(), instanceID, frame, nds);
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
        && !AIObservation::ProvidesImitationInput(
            AIObservationContext(), CurrentPacketBridgeLocalPlayer() ^ 1))
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
        testInput = AIObservation::ApplyImitationInput(
            AIObservationContext(), AIObservationHooks(), instanceID,
            inputFrame, nds, CurrentPacketBridgeLocalPlayer(), testInput);
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
        && MvlLifecycle::ShouldPauseInputForRestart(
            MvlLifecycleContext(), instanceID, nds);
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
        std::printf("NSMB MvL Netplay: lockstep started inst=%d frame=%u\n", instanceID, targetFrame);
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
    GameplayDiagnostics::RecordActiveFrameTiming(
        GameplayDiagnosticsContext(), instanceID, logFrame);
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
    MvlLifecycle::SaveGameplayCheckpointIfNeeded(
        MvlLifecycleContext(), MvlLifecycleHooks(), instanceID, logFrame, nds);
}


void SaveAfterFrameArtifacts(int instanceID, melonDS::u32 logFrame, melonDS::NDS* nds)
{
    const TestStateHarness::Context stateHarness{
        G.Harness, G.Bootstrap, G.Coordinator, G.Mutex};
    TestStateHarness::SaveState(stateHarness, instanceID, logFrame, nds);
    TestStateHarness::SaveLocalMPState(stateHarness, logFrame);
    GameplayDiagnostics::CaptureScreenshotIfNeeded(
        GameplayDiagnosticsContext(), instanceID, logFrame, nds);
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
    GameStateSync::UpdateHangSnapshot(
        GameStateSyncContext(), instanceID, logFrame, nds);

    TraceHangPhase("begin", "gameplay-heartbeat", instanceID, logFrame, logFrame, logFrame);
    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        GameplayDiagnostics::TraceGameplayHeartbeatIfNeeded(
            GameplayDiagnosticsContext(), instanceID, logFrame, nds);

    TraceHangPhase("begin", "after-frame-barriers", instanceID, logFrame, logFrame, logFrame);
    WaitAtFrameBarrier(Coordination::FrameBarrierKind::After, instanceID, logFrame, "after");
    AdvanceSerialRunTurn(instanceID, logFrame - 1);
    NetplaySession::WaitForPeerAtStartBarrier(
        NetplaySessionContext(), NetplaySessionHooks(), instanceID, logFrame);
    const auto afterBarrier = std::chrono::steady_clock::now();

    TraceHangPhase("begin", "apply-remote-game-state", instanceID, logFrame, logFrame, logFrame);
    if (G.Enabled)
        GameStateSync::ApplyRemote(
            GameStateSyncContext(), GameStateSyncHooks(), instanceID, logFrame, nds);

    TraceHangPhase("begin", "packet-bridge", instanceID, logFrame, logFrame, logFrame);
    RunAfterFramePacketBridgePhase(logFrame, nds);
    const auto afterBridge = std::chrono::steady_clock::now();

    TraceHangPhase("begin", "life-trace", instanceID, logFrame, logFrame, logFrame);
    if (CanRunFrameHooks(instanceID, nds))
        GameplayDiagnostics::TracePlayerLifeChanges(
            GameplayDiagnosticsContext(), GameplayDiagnosticsHooks(),
            instanceID, logFrame, nds);
    const auto afterLifeTrace = std::chrono::steady_clock::now();

    TraceHangPhase("begin", "diagnostic-snapshot", instanceID, logFrame, logFrame, logFrame);
    if (CanRunFrameHooks(instanceID, nds))
        GameplayDiagnostics::RecordSnapshotIfNeeded(
            GameplayDiagnosticsContext(), instanceID, logFrame, nds);
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
        GameStateSync::TraceWorld(GameStateSyncContext(), instanceID, logFrame, nds);
    const auto afterWorldTrace = std::chrono::steady_clock::now();
    TraceHangPhase("begin", "game-state-trace", instanceID, logFrame, logFrame, logFrame);
    GameStateSync::Trace(GameStateSyncContext(), instanceID, logFrame, nds);
    AIObservation::TracePlayLog(
        AIObservationContext(), AIObservationHooks(), instanceID, logFrame, nds);
    const auto afterTrace = std::chrono::steady_clock::now();
    TraceHangPhase("begin", "sync-game", instanceID, logFrame, logFrame, logFrame);
    GameStateSync::Sync(
        GameStateSyncContext(), GameStateSyncHooks(), instanceID, logFrame, nds);
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
    GameplayDiagnostics::Stop(GameplayDiagnosticsContext());
    NetplaySession::StopNetworkPumpThread(NetplaySessionContext());

    G.InputRecorder.Close();

    std::lock_guard<std::mutex> lock(G.Mutex);

    G.Transport.Shutdown();

    G.GameStateTrace.Close();
    G.AIObservationRuntime.CloseLogs();
}

}
