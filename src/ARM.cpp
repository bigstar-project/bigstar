/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <chrono>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "NDS.h"
#include "DSi.h"
#include "ARM.h"
#include "ARMInterpreter.h"
#include "AREngine.h"
#include "ARMJIT.h"
#include "Platform.h"
#include "GPU.h"
#include "ARMJIT_Memory.h"

namespace melonDS
{
using Platform::Log;
using Platform::LogLevel;

static std::mutex NSMLTraceConfigMutex;
static std::mutex NSMLTraceOutputMutex;
static std::mutex NSMLPacketBridgeMutex;

struct NSMLPacketReplayEntry
{
    bool Valid[2] {};
    std::array<u8, 52> Packet[2] {};
};

struct NSMLLocalPacketCapture
{
    bool Available = false;
    u32 Tick = 0;
    u32 Keys = 0;
    u32 Frame = 0;
    std::array<u8, 52> Packet {};
};

static std::map<NDS*, NSMLLocalPacketCapture> NSMLLocalPackets;
static std::map<NDS*, std::map<u32, NSMLPacketReplayEntry>> NSMLLiveReplayPackets;
static std::map<NDS*, std::map<u32, u32>> NSMLPreservedNetWords;

static bool NSMLEnvFlag(const char* name)
{
    const char* value = getenv(name);
    return value && value[0] && strcmp(value, "0") != 0;
}

static bool NSMLPacketBridgeEnabled()
{
    static int enabled = -1;
    if (enabled < 0)
        enabled = NSMLEnvFlag("MELONDS_NSML_PACKET_BRIDGE") ? 1 : 0;
    return enabled != 0;
}

static int NSMLPacketBridgeReplayTickOffset()
{
    static int offset = 0x7FFFFFFF;
    if (offset == 0x7FFFFFFF)
    {
        if (const char* value = getenv("MELONDS_NSML_PACKET_BRIDGE_REPLAY_TICK_OFFSET"))
            offset = atoi(value);
        else
            offset = 0;
    }
    return offset;
}

static u32 NSMLPacketBridgeLocalPlayer()
{
    if (const char* role = getenv("MELONDS_NSML_ROLE"))
    {
        if (!strcmp(role, "client"))
            return 1;
    }
    if (const char* role = getenv("MELONDS_NSML_LAN_ROLE"))
    {
        if (!strcmp(role, "client"))
            return 1;
    }
    return 0;
}

static bool NSMLPacketBridgeMaintainPacketFreeBytes()
{
    static int enabled = -1;
    if (enabled < 0)
        enabled = NSMLEnvFlag("MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_PACKET_FREE_BYTES") ? 1 : 0;
    return enabled != 0;
}

static void NSMLMaintainPacketFreeBytes(NDS& nds)
{
    if (!NSMLPacketBridgeMaintainPacketFreeBytes())
        return;

    // PacketBridge supplies remote packets below LocalMP. Keep the Net packet
    // free-byte receive bitmap consistent with the two-player packet stream.
    nds.ARM9Write32(0x020880A4, 0x00000003);
    nds.ARM9Write32(0x020880A8, 0x00000003);
}

static bool NSMLPacketBridgeMaintainSessionPeers()
{
    static int enabled = -1;
    if (enabled < 0)
        enabled = NSMLEnvFlag("MELONDS_NSML_PACKET_BRIDGE_MAINTAIN_SESSION_PEERS") ? 1 : 0;
    return enabled != 0;
}

static void NSMLWritePeerIdentity(NDS& nds, u32 addr, u32 player)
{
    static constexpr u8 kPeerIdentity[2][6] = {
        { 'M', 'A', 'R', 'I', 'O', 0 },
        { 'L', 'U', 'I', 'G', 'I', 0 },
    };
    for (u32 i = 0; i < 6; i++)
        nds.ARM9Write8(addr + i, kPeerIdentity[player & 1][i]);
}

static void NSMLMaintainSessionPeers(NDS& nds)
{
    if (!NSMLPacketBridgeMaintainSessionPeers())
        return;

    // LoadGameSM waits for the lower Net peer/session tables, not only for
    // packet payloads. Populate the two-player peer entries that LocalMP would
    // normally maintain, while leaving higher VSConnect state alone.
    nds.ARM9Write32(0x02087E24, 0x00000002);

    const u32 compactPeerBase = nds.ARM9Read32(0x02087E70);
    if (compactPeerBase >= 0x02000000 && compactPeerBase < 0x02400000)
    {
        for (u32 player = 0; player < 2; player++)
        {
            const u32 entry = compactPeerBase + player * 0x1E;
            nds.ARM9Write8(entry + 0x01, 6);
            NSMLWritePeerIdentity(nds, entry + 0x02, player);
            nds.ARM9Write8(entry + 0x1D, 1);
        }
    }

    constexpr u32 fullPeerBase = 0x0208B6C0;
    for (u32 player = 0; player < 2; player++)
    {
        const u32 entry = fullPeerBase + player * 0xC0;
        nds.ARM9Write16(entry + 0x00, 1);
        NSMLWritePeerIdentity(nds, entry + 0x04, player);
        nds.ARM9Write8(entry + 0x50, static_cast<u8>(player));
        nds.ARM9Write8(entry + 0x51, 6);
        NSMLWritePeerIdentity(nds, entry + 0x52, player);
    }
}

static bool NSMLPacketBridgeStageStartReadyProbe()
{
    static int enabled = -1;
    if (enabled < 0)
        enabled = NSMLEnvFlag("MELONDS_NSML_PACKET_BRIDGE_STAGE_START_READY_PROBE") ? 1 : 0;
    return enabled != 0;
}

static u32 NSMLPacketBridgeWaitTimeoutMs()
{
    static u32 timeout = 0xFFFFFFFF;
    if (timeout == 0xFFFFFFFF)
    {
        if (const char* value = getenv("MELONDS_NSML_PACKET_BRIDGE_WAIT_TIMEOUT_MS"))
            timeout = static_cast<u32>(strtoul(value, nullptr, 0));
        else
            timeout = 0;
    }
    return timeout;
}

static bool IsNSMLMarioVsLuigiGameplay(NDS& nds)
{
    return nds.ARM9Read32(0x02085058) == 9
        && nds.ARM9Read32(0x020850C4) == 1
        && nds.ARM9Read32(0x02087E78) == 0x42;
}

static bool NSMLPacketBridgeAllowPreGame()
{
    static int enabled = -1;
    if (enabled < 0)
        enabled = NSMLEnvFlag("MELONDS_NSML_PACKET_BRIDGE_ALLOW_PRE_GAME") ? 1 : 0;
    return enabled != 0;
}

static bool IsNSMLMarioVsLuigiPacketContext(NDS& nds)
{
    if (IsNSMLMarioVsLuigiGameplay(nds))
        return true;

    return NSMLPacketBridgeAllowPreGame()
        && nds.ARM9Read32(0x02087E78) == 0x42;
}

static u32 NSMLFindObjectBaseByID(NDS& nds, u16 objectID)
{
    if (!nds.MainRAM)
        return 0;

    for (u32 off = 0x080000; off + 0x80 <= nds.MainRAMMask + 1; off += 4)
    {
        const u32 base = 0x02000000 + off;
        const u32 vtable = nds.ARM9Read32(base);
        const u16 candidateID = nds.ARM9Read16(base + 0x0C);
        const u16 stateType = nds.ARM9Read16(base + 0x0E);
        const u32 flags = nds.ARM9Read32(base + 0x10);
        if (candidateID != objectID || stateType == 0 || stateType > 2)
            continue;
        if (vtable < 0x02000000 || vtable >= 0x02400000)
            continue;
        if ((flags & 0xFFFF0000u) == 0)
            continue;
        return base;
    }

    return 0;
}

static void NSMLProbeStageStartReadyBits(NDS& nds)
{
    if (!NSMLPacketBridgeStageStartReadyProbe() || !IsNSMLMarioVsLuigiPacketContext(nds))
        return;
    if (nds.ARM9Read16(0x0203B484) != 0x0006)
        return;

    const u32 vsConnectBase = NSMLFindObjectBaseByID(nds, 0x0006);
    if (vsConnectBase == 0)
        return;
    if (nds.ARM9Read32(vsConnectBase + 0x120) != 0x021512B8)
        return;
    if (nds.ARM9Read32(vsConnectBase + 0x144) < 5)
        return;

    nds.ARM9Write8(vsConnectBase + 0x156, 0x03);
}

static void NSMLEmitMovImm(std::vector<u32>& code, int reg, u32 value)
{
    if (value <= 0xFF)
    {
        code.push_back(0xE3A00000u | (static_cast<u32>(reg) << 12) | value);
        return;
    }

    code.push_back(0xE59F0000u | (static_cast<u32>(reg) << 12));
    code.push_back(0xEA000000u);
    code.push_back(value);
}

static void NSMLEmitBLViaIP(std::vector<u32>& code, u32 target)
{
    code.push_back(0xE59FC008u); // ldr ip, [pc, #8]
    code.push_back(0xE28FE008u); // add lr, pc, #8
    code.push_back(0xE12FFF1Cu); // bx ip
    code.push_back(0xE1A00000u); // nop
    code.push_back(target);
}

static void NSMLEmitStackArg(std::vector<u32>& code, u32 offset, u32 value)
{
    NSMLEmitMovImm(code, 4, value);
    code.push_back(0xE58D4000u | offset); // str r4, [sp, #offset]
}

static void NSMLEmitStoreImm32(std::vector<u32>& code, u32 addr, u32 value)
{
    NSMLEmitMovImm(code, 4, value);
    NSMLEmitMovImm(code, 6, addr);
    code.push_back(0xE5864000u); // str r4, [r6]
}

static void NSMLEmitStoreImm8(std::vector<u32>& code, u32 addr, u32 value)
{
    NSMLEmitMovImm(code, 4, value & 0xFFu);
    NSMLEmitMovImm(code, 6, addr);
    code.push_back(0xE5C64000u); // strb r4, [r6]
}

static bool HandleNSMLSafeLevelCall(ARM* cpu, u32 instrAddr)
{
    static int enabled = -1;
    if (enabled < 0)
        enabled = (NSMLEnvFlag("MELONDS_NSML_SAFE_START_LOAD_CALL")
            || NSMLEnvFlag("MELONDS_NSML_SAFE_LOAD_LEVEL_CALL")
            || NSMLEnvFlag("MELONDS_NSML_SAFE_COURSE_SELECT_CALL")
            || NSMLEnvFlag("MELONDS_NSML_SAFE_COURSE_SELECT_FACTORY_CALL")
            || NSMLEnvFlag("MELONDS_NSML_SAFE_CREATE_LOAD_GAME_CALL")
            || NSMLEnvFlag("MELONDS_NSML_SAFE_UPDATE_LOAD_GAME_CALL")
            || NSMLEnvFlag("MELONDS_NSML_SAFE_SCHEDULE_LOAD_GAME_CALL")
            || NSMLEnvFlag("MELONDS_NSML_SAFE_STAGE_SCENE_FACTORY_CALL")
            || NSMLEnvFlag("MELONDS_NSML_SAFE_TRY_CHANGE_SCENE_CALL")
            || NSMLEnvFlag("MELONDS_NSML_SCENE_AUTO_ACTIVE_CLEAR")) ? 1 : 0;
    if (!enabled || !cpu || cpu->Num != 0)
        return false;

    if (instrAddr == 0x0201314C && IsNSMLMarioVsLuigiPacketContext(cpu->NDS))
    {
        static bool configured = false;
        static u32 loadMinFrame = 2850;
        static u32 stageMinFrame = 3050;
        if (!configured)
        {
            if (const char* value = getenv("MELONDS_NSML_SCENE_AUTO_ACTIVE_CLEAR_LOAD_FRAME"))
                loadMinFrame = static_cast<u32>(strtoul(value, nullptr, 0));
            if (const char* value = getenv("MELONDS_NSML_SCENE_AUTO_ACTIVE_CLEAR_STAGE_FRAME"))
                stageMinFrame = static_cast<u32>(strtoul(value, nullptr, 0));
            configured = true;
        }
        static std::map<NDS*, u32> activeClearMask;
        const u16 currentScene = cpu->NDS.ARM9Read16(0x0203B484);
        const u16 nextScene = cpu->NDS.ARM9Read16(0x0203B480);
        if (currentScene == 0x0005 && nextScene == 0x000F && cpu->NDS.NumFrames >= loadMinFrame && (activeClearMask[&cpu->NDS] & 0x1) == 0)
        {
            cpu->NDS.ARM9Write8(0x0203B478, 0);
            activeClearMask[&cpu->NDS] |= 0x1;
            printf("NSMB SceneAutoActiveClear: load frame=%u current=%04X next=%04X\n", cpu->NDS.NumFrames, currentScene, nextScene);
            fflush(stdout);
        }
        else if (currentScene == 0x000F && nextScene == 0x0003 && cpu->NDS.NumFrames >= stageMinFrame && (activeClearMask[&cpu->NDS] & 0x2) == 0)
        {
            cpu->NDS.ARM9Write8(0x0203B478, 0);
            activeClearMask[&cpu->NDS] |= 0x2;
            printf("NSMB SceneAutoActiveClear: stage frame=%u current=%04X next=%04X\n", cpu->NDS.NumFrames, currentScene, nextScene);
            fflush(stdout);
        }
        return false;
    }

    static int loadLevel = -1;
    if (loadLevel < 0)
        loadLevel = NSMLEnvFlag("MELONDS_NSML_SAFE_LOAD_LEVEL_CALL") ? 1 : 0;
    static int courseSelect = -1;
    if (courseSelect < 0)
        courseSelect = NSMLEnvFlag("MELONDS_NSML_SAFE_COURSE_SELECT_CALL") ? 1 : 0;
    static int courseSelectFactory = -1;
    if (courseSelectFactory < 0)
        courseSelectFactory = NSMLEnvFlag("MELONDS_NSML_SAFE_COURSE_SELECT_FACTORY_CALL") ? 1 : 0;
    static int createLoadGame = -1;
    if (createLoadGame < 0)
        createLoadGame = NSMLEnvFlag("MELONDS_NSML_SAFE_CREATE_LOAD_GAME_CALL") ? 1 : 0;
    static int updateLoadGame = -1;
    if (updateLoadGame < 0)
        updateLoadGame = NSMLEnvFlag("MELONDS_NSML_SAFE_UPDATE_LOAD_GAME_CALL") ? 1 : 0;
    static int scheduleLoadGame = -1;
    if (scheduleLoadGame < 0)
        scheduleLoadGame = NSMLEnvFlag("MELONDS_NSML_SAFE_SCHEDULE_LOAD_GAME_CALL") ? 1 : 0;
    static int stageSceneFactory = -1;
    if (stageSceneFactory < 0)
        stageSceneFactory = NSMLEnvFlag("MELONDS_NSML_SAFE_STAGE_SCENE_FACTORY_CALL") ? 1 : 0;
    static int tryChangeScene = -1;
    if (tryChangeScene < 0)
        tryChangeScene = NSMLEnvFlag("MELONDS_NSML_SAFE_TRY_CHANGE_SCENE_CALL") ? 1 : 0;
    static int startLoadCall = -1;
    if (startLoadCall < 0)
        startLoadCall = NSMLEnvFlag("MELONDS_NSML_SAFE_START_LOAD_CALL") ? 1 : 0;

    static bool triggerConfigured = false;
    static u32 triggerPC = 0;
    static u32 startLoadTriggerPC = 0;
    static u32 stageSceneFactoryTriggerPC = 0;
    static u32 tryChangeTriggerPC = 0;
    static u32 startFrame = 0;
    static u32 stageSceneFactoryFrame = 0;
    static u32 tryChangeFrame = 0;
    static u32 minSP = 0;
    static int probe = -1;
    static int probeOnly = -1;
    static u32 probeMinSP = 0;
    static u32 probeMax = 0;
    static u32 requiredMode = 0xFFFFFFFFu;
    static u32 probeMode = 0xFFFFFFFFu;
    static u32 tryChangeTargetScene = 0xFFFFFFFFu;
    static u32 tryChangePreviousScene = 0xFFFFFFFFu;
    static int tryChangeSetOnly = -1;
    if (!triggerConfigured)
    {
        if (const char* value = getenv(updateLoadGame
                ? "MELONDS_NSML_SAFE_UPDATE_LOAD_GAME_CALL_PC"
                : courseSelectFactory
                ? "MELONDS_NSML_SAFE_COURSE_SELECT_FACTORY_CALL_PC"
                : createLoadGame
                ? "MELONDS_NSML_SAFE_CREATE_LOAD_GAME_CALL_PC"
                : courseSelect
                ? "MELONDS_NSML_SAFE_COURSE_SELECT_CALL_PC"
                : loadLevel
                ? "MELONDS_NSML_SAFE_LOAD_LEVEL_CALL_PC"
                : scheduleLoadGame
                ? "MELONDS_NSML_SAFE_SCHEDULE_LOAD_GAME_CALL_PC"
                : stageSceneFactory
                ? "MELONDS_NSML_SAFE_STAGE_SCENE_FACTORY_CALL_PC"
                : tryChangeScene
                ? "MELONDS_NSML_SAFE_TRY_CHANGE_SCENE_CALL_PC"
                : "MELONDS_NSML_SAFE_START_LOAD_CALL_PC"))
            triggerPC = static_cast<u32>(strtoul(value, nullptr, 0));
        else
            triggerPC = 0x0200F944;
        if (const char* value = getenv("MELONDS_NSML_SAFE_START_LOAD_CALL_PC"))
            startLoadTriggerPC = static_cast<u32>(strtoul(value, nullptr, 0));
        else
            startLoadTriggerPC = 0x0200F944;
        if (const char* value = getenv("MELONDS_NSML_SAFE_STAGE_SCENE_FACTORY_CALL_PC"))
            stageSceneFactoryTriggerPC = static_cast<u32>(strtoul(value, nullptr, 0));
        else
            stageSceneFactoryTriggerPC = 0x0200F944;
        if (const char* value = getenv("MELONDS_NSML_SAFE_TRY_CHANGE_SCENE_CALL_PC"))
            tryChangeTriggerPC = static_cast<u32>(strtoul(value, nullptr, 0));
        else
            tryChangeTriggerPC = 0x0200F944;
        if (const char* value = getenv(updateLoadGame
                ? "MELONDS_NSML_SAFE_UPDATE_LOAD_GAME_CALL_FRAME"
                : courseSelectFactory
                ? "MELONDS_NSML_SAFE_COURSE_SELECT_FACTORY_CALL_FRAME"
                : createLoadGame
                ? "MELONDS_NSML_SAFE_CREATE_LOAD_GAME_CALL_FRAME"
                : courseSelect
                ? "MELONDS_NSML_SAFE_COURSE_SELECT_CALL_FRAME"
                : loadLevel
                ? "MELONDS_NSML_SAFE_LOAD_LEVEL_CALL_FRAME"
                : scheduleLoadGame
                ? "MELONDS_NSML_SAFE_SCHEDULE_LOAD_GAME_CALL_FRAME"
                : stageSceneFactory
                ? "MELONDS_NSML_SAFE_STAGE_SCENE_FACTORY_CALL_FRAME"
                : tryChangeScene
                ? "MELONDS_NSML_SAFE_TRY_CHANGE_SCENE_CALL_FRAME"
                : "MELONDS_NSML_SAFE_START_LOAD_CALL_FRAME"))
            startFrame = static_cast<u32>(strtoul(value, nullptr, 0));
        else
            startFrame = 0;
        if (const char* value = getenv("MELONDS_NSML_SAFE_STAGE_SCENE_FACTORY_CALL_FRAME"))
            stageSceneFactoryFrame = static_cast<u32>(strtoul(value, nullptr, 0));
        else
            stageSceneFactoryFrame = 0;
        if (const char* value = getenv("MELONDS_NSML_SAFE_TRY_CHANGE_SCENE_CALL_FRAME"))
            tryChangeFrame = static_cast<u32>(strtoul(value, nullptr, 0));
        else
            tryChangeFrame = 0;
        if (const char* value = getenv("MELONDS_NSML_SAFE_CALL_MIN_SP"))
            minSP = static_cast<u32>(strtoul(value, nullptr, 0));
        probe = NSMLEnvFlag("MELONDS_NSML_SAFE_CALL_PROBE") ? 1 : 0;
        probeOnly = NSMLEnvFlag("MELONDS_NSML_SAFE_CALL_PROBE_ONLY") ? 1 : 0;
        if (const char* value = getenv("MELONDS_NSML_SAFE_CALL_PROBE_MIN_SP"))
            probeMinSP = static_cast<u32>(strtoul(value, nullptr, 0));
        else
            probeMinSP = minSP;
        if (const char* value = getenv("MELONDS_NSML_SAFE_CALL_PROBE_MAX"))
            probeMax = static_cast<u32>(strtoul(value, nullptr, 0));
        else
            probeMax = 80;
        if (const char* value = getenv("MELONDS_NSML_SAFE_CALL_REQUIRED_MODE"))
            requiredMode = static_cast<u32>(strtoul(value, nullptr, 0)) & 0x1Fu;
        if (const char* value = getenv("MELONDS_NSML_SAFE_CALL_PROBE_MODE"))
            probeMode = static_cast<u32>(strtoul(value, nullptr, 0)) & 0x1Fu;
        if (const char* value = getenv("MELONDS_NSML_SAFE_TRY_CHANGE_SCENE_TARGET"))
            tryChangeTargetScene = static_cast<u32>(strtoul(value, nullptr, 0)) & 0xFFFFu;
        if (const char* value = getenv("MELONDS_NSML_SAFE_TRY_CHANGE_SCENE_PREVIOUS"))
            tryChangePreviousScene = static_cast<u32>(strtoul(value, nullptr, 0)) & 0xFFFFu;
        tryChangeSetOnly = NSMLEnvFlag("MELONDS_NSML_SAFE_TRY_CHANGE_SCENE_SET_ONLY") ? 1 : 0;
        triggerConfigured = true;
    }
    static bool combinedStartLoadConfigured = false;
    static u32 combinedStartLoadFrame = 0;
    if (!combinedStartLoadConfigured)
    {
        if (const char* value = getenv("MELONDS_NSML_SAFE_START_LOAD_CALL_FRAME"))
            combinedStartLoadFrame = static_cast<u32>(strtoul(value, nullptr, 0));
        else
            combinedStartLoadFrame = 0;
        combinedStartLoadConfigured = true;
    }

    const bool combinedCourseSelectThenStartLoad =
        courseSelectFactory && NSMLEnvFlag("MELONDS_NSML_SAFE_START_LOAD_CALL");
    const bool combinedStartLoadThenTryChange =
        tryChangeScene && startLoadCall;
    const bool combinedStartLoadThenStageScene =
        stageSceneFactory && startLoadCall;
    static std::map<NDS*, u32> appliedMask;
    static std::map<NDS*, u32> appliedFrame;
    const bool startLoadAlreadyApplied = (appliedMask[&cpu->NDS] & 0x10) != 0;
    const bool courseSelectFactoryAlreadyApplied = (appliedMask[&cpu->NDS] & 0x02) != 0;
    const bool tryChangeAlreadyApplied = (appliedMask[&cpu->NDS] & 0x80) != 0;
    const bool effectiveStartLoad =
        (!loadLevel && !courseSelectFactory && !courseSelect && !createLoadGame && !updateLoadGame && !scheduleLoadGame && !stageSceneFactory && !tryChangeScene && !startLoadCall)
        || (startLoadCall && !combinedCourseSelectThenStartLoad && !combinedStartLoadThenTryChange && !combinedStartLoadThenStageScene && !startLoadAlreadyApplied && cpu->NDS.NumFrames >= combinedStartLoadFrame)
        || (combinedCourseSelectThenStartLoad && !startLoadAlreadyApplied && cpu->NDS.NumFrames >= combinedStartLoadFrame)
        || (combinedStartLoadThenTryChange && !startLoadAlreadyApplied && cpu->NDS.NumFrames >= combinedStartLoadFrame)
        || (combinedStartLoadThenStageScene && !startLoadAlreadyApplied && cpu->NDS.NumFrames >= combinedStartLoadFrame);
    const bool effectiveCourseSelectFactory =
        courseSelectFactory && !courseSelectFactoryAlreadyApplied && !effectiveStartLoad;
    const bool effectiveCourseSelect =
        courseSelect && !effectiveStartLoad && !effectiveCourseSelectFactory;
    const bool effectiveLoadLevel = loadLevel;
    const bool effectiveCreateLoadGame = createLoadGame;
    const bool effectiveUpdateLoadGame = updateLoadGame;
    const bool effectiveScheduleLoadGame = scheduleLoadGame;
    const bool effectiveStageSceneFactory =
        stageSceneFactory && !effectiveStartLoad && !effectiveCourseSelectFactory && (!tryChangeScene || tryChangeAlreadyApplied);
    const bool effectiveTryChangeScene = tryChangeScene && !effectiveStartLoad && !effectiveCourseSelectFactory;

    const u32 effectiveStartFrame = effectiveStartLoad ? combinedStartLoadFrame : effectiveStageSceneFactory ? stageSceneFactoryFrame : effectiveTryChangeScene ? tryChangeFrame : startFrame;
    const u32 effectiveTriggerPC = effectiveStartLoad ? startLoadTriggerPC : effectiveStageSceneFactory ? stageSceneFactoryTriggerPC : effectiveTryChangeScene ? tryChangeTriggerPC : triggerPC;

    if (cpu->NDS.NumFrames < effectiveStartFrame)
        return false;
    if (effectiveTriggerPC != 0 && instrAddr != effectiveTriggerPC)
        return false;
    if (effectiveTriggerPC == 0 && (instrAddr < 0x02000000 || instrAddr >= 0x02400000))
        return false;
    if (effectiveTryChangeScene && probe > 0 && effectiveTriggerPC == 0)
    {
        static std::map<NDS*, u32> probeCount;
        static std::map<NDS*, std::map<u32, u32>> probeSeenPCs;
        if (cpu->R[13] >= probeMinSP
            && (probeMode == 0xFFFFFFFFu || (cpu->CPSR & 0x1Fu) == probeMode)
            && probeCount[&cpu->NDS] < probeMax
            && probeSeenPCs[&cpu->NDS][instrAddr] == 0)
        {
            probeSeenPCs[&cpu->NDS][instrAddr] = 1;
            probeCount[&cpu->NDS]++;
            printf("NSMB SafeCallProbe: tryChangeScene frame=%u pc=%08X sp=%08X lr=%08X cpsr=%08X sceneActive=%u scenePrev=%04X sceneNext=%04X sceneCurrent=%04X sceneSettings=%08X\n",
                cpu->NDS.NumFrames,
                instrAddr,
                cpu->R[13],
                cpu->R[14],
                cpu->CPSR,
                cpu->NDS.ARM9Read16(0x0203B478),
                cpu->NDS.ARM9Read16(0x0203B47C),
                cpu->NDS.ARM9Read16(0x0203B480),
                cpu->NDS.ARM9Read16(0x0203B484),
                cpu->NDS.ARM9Read32(0x02088578));
            fflush(stdout);
        }
        if (probeOnly > 0)
            return false;
    }
    if (minSP != 0 && cpu->R[13] < minSP)
        return false;
    if (!effectiveStartLoad && requiredMode != 0xFFFFFFFFu && (cpu->CPSR & 0x1Fu) != requiredMode)
        return false;
    if (!loadLevel
        && !effectiveStartLoad
        && !effectiveCourseSelectFactory
        && !effectiveStageSceneFactory
        && !IsNSMLMarioVsLuigiPacketContext(cpu->NDS))
        return false;

    const u32 modeMask =
        effectiveLoadLevel ? 0x01 :
        effectiveCourseSelectFactory ? 0x02 :
        effectiveCourseSelect ? 0x04 :
        effectiveCreateLoadGame ? 0x40 :
        effectiveUpdateLoadGame ? 0x08 :
        effectiveScheduleLoadGame ? 0x20 :
        effectiveStageSceneFactory ? 0x100 :
        effectiveTryChangeScene ? 0x80 :
        0x10;
    if (effectiveUpdateLoadGame)
    {
        if (appliedFrame[&cpu->NDS] == cpu->NDS.NumFrames)
            return false;
    }
    else if ((appliedMask[&cpu->NDS] & modeMask) != 0)
        return false;

    const u32 vsConnectBase = NSMLFindObjectBaseByID(cpu->NDS, 0x0006);
    if (vsConnectBase == 0 && !effectiveLoadLevel && !effectiveTryChangeScene)
        return false;

    constexpr u32 trampolineAddr = 0x023C0000;
    constexpr u32 loadLevelAddr = 0x020068A8;
    constexpr u32 startLoadLevelAddr = 0x0214E0C0;
    constexpr u32 createCourseSelectAddr = 0x0214F858;
    constexpr u32 courseSelectFactoryAddr = 0x020130A8;
    constexpr u32 createLoadGameSMAddr = 0x021520A0;
    constexpr u32 updateLoadGameSMAddr = 0x02151E94;
    constexpr u32 scheduleSubMenuChangeAddr = 0x021528A0;
    constexpr u32 loadGameSMSubMenuAddr = 0x02156624;
    constexpr u32 tryChangeSceneAddr = 0x0201314C;
    constexpr u32 applySceneRequestAddr = 0x02007ACC;
    constexpr u32 startSceneTransitionAddr = 0x02011CE8;
    const u32 returnPC = instrAddr | ((cpu->CPSR & 0x20) ? 1u : 0u);
    u32 playerID = cpu->NDS.ARM9Read32(0x020850BC);
    if (const char* value = getenv("MELONDS_NSML_SAFE_LOAD_LEVEL_PLAYER_ID"))
        playerID = static_cast<u32>(strtoul(value, nullptr, 0));
    if (const char* role = getenv("MELONDS_NSML_ROLE"))
    {
        if (!strcmp(role, "client"))
            playerID = 1;
    }
    if (effectiveTryChangeScene && tryChangeTargetScene != 0xFFFFFFFFu)
    {
        if (tryChangePreviousScene != 0xFFFFFFFFu)
            cpu->NDS.ARM9Write16(0x0203B47C, static_cast<u16>(tryChangePreviousScene));
        cpu->NDS.ARM9Write16(0x0203B480, static_cast<u16>(tryChangeTargetScene));
        cpu->NDS.ARM9Write32(0x02088578, 0);
        cpu->NDS.ARM9Write8(0x02087F04, 3);
        cpu->NDS.ARM9Write32(0x02087F04, 0xFFFF0003);
        cpu->NDS.ARM9Write32(0x0208B044, 0xFFFF0003);
        cpu->NDS.ARM9Write32(0x0208B048, 0);
        cpu->NDS.ARM9Write32(0x0208B04C, 0);
        if (tryChangeSetOnly > 0)
        {
            appliedMask[&cpu->NDS] |= modeMask;
            printf("NSMB SafeCall: setSceneRequest frame=%u pc=%08X sp=%08X lr=%08X cpsr=%08X prev=%04X next=%04X\n",
                cpu->NDS.NumFrames,
                instrAddr,
                cpu->R[13],
                cpu->R[14],
                cpu->CPSR,
                cpu->NDS.ARM9Read16(0x0203B47C),
                cpu->NDS.ARM9Read16(0x0203B480));
            fflush(stdout);
            return false;
        }
    }

    std::vector<u32> code;
    code.reserve(96);
    code.push_back(0xE92D5FFFu); // push {r0-r12, lr}
    code.push_back(0xE10F5000u); // mrs r5, cpsr
    code.push_back(0xE92D0020u); // push {r5}
    if (effectiveLoadLevel)
    {
        code.push_back(0xE24DD034u); // sub sp, sp, #0x34
        NSMLEmitMovImm(code, 0, 0x0F); // scene
        NSMLEmitMovImm(code, 1, 0x01); // vs
        NSMLEmitMovImm(code, 2, 0x09); // MvsL stage group
        NSMLEmitMovImm(code, 3, 0x00); // stage
        NSMLEmitStackArg(code, 0x00, 0x00); // act
        NSMLEmitStackArg(code, 0x04, playerID);
        NSMLEmitStackArg(code, 0x08, 0x03); // player mask
        NSMLEmitStackArg(code, 0x0C, 0x00); // character1
        NSMLEmitStackArg(code, 0x10, 0x01); // character2
        NSMLEmitStackArg(code, 0x14, 0x00); // powerup
        NSMLEmitStackArg(code, 0x18, 0xFF); // entrance
        NSMLEmitStackArg(code, 0x1C, 0x01); // flag
        NSMLEmitStackArg(code, 0x20, 0x01);
        NSMLEmitStackArg(code, 0x24, 0xFF);
        NSMLEmitStackArg(code, 0x28, 0x00);
        NSMLEmitStackArg(code, 0x2C, 0x00);
        NSMLEmitStackArg(code, 0x30, 0xFFFFFFFFu);
        NSMLEmitBLViaIP(code, loadLevelAddr);
        code.push_back(0xE28DD034u); // add sp, sp, #0x34
    }
    else if (effectiveCourseSelect)
    {
        NSMLEmitMovImm(code, 0, vsConnectBase);
        NSMLEmitBLViaIP(code, createCourseSelectAddr);
    }
    else if (effectiveCourseSelectFactory)
    {
        NSMLEmitMovImm(code, 0, 0x05);
        NSMLEmitMovImm(code, 1, 0x01);
        NSMLEmitMovImm(code, 2, 0x02186A78);
        NSMLEmitMovImm(code, 3, 0x04);
        NSMLEmitBLViaIP(code, courseSelectFactoryAddr);
    }
    else if (effectiveCreateLoadGame)
    {
        NSMLEmitMovImm(code, 0, vsConnectBase);
        NSMLEmitMovImm(code, 1, createLoadGameSMAddr);
        NSMLEmitMovImm(code, 2, 0);
        NSMLEmitMovImm(code, 3, vsConnectBase + 0x118);
        NSMLEmitBLViaIP(code, createLoadGameSMAddr);
    }
    else if (effectiveUpdateLoadGame)
    {
        NSMLEmitMovImm(code, 0, vsConnectBase);
        NSMLEmitMovImm(code, 1, updateLoadGameSMAddr);
        NSMLEmitMovImm(code, 2, 0);
        NSMLEmitMovImm(code, 3, vsConnectBase + 0x120);
        NSMLEmitBLViaIP(code, updateLoadGameSMAddr);
    }
    else if (effectiveScheduleLoadGame)
    {
        NSMLEmitMovImm(code, 0, vsConnectBase);
        NSMLEmitMovImm(code, 1, loadGameSMSubMenuAddr);
        NSMLEmitMovImm(code, 2, 0x1E);
        NSMLEmitMovImm(code, 3, 1);
        NSMLEmitBLViaIP(code, scheduleSubMenuChangeAddr);
    }
    else if (effectiveStageSceneFactory)
    {
        NSMLEmitStoreImm32(code, 0x02085058, 0x00000009); // Game::stageGroup
        NSMLEmitStoreImm32(code, 0x020850BC, playerID); // Game::localPlayerID
        NSMLEmitStoreImm32(code, 0x020850C4, 0x00000001); // Game::vsMode
        NSMLEmitStoreImm32(code, 0x02087E14, 0x00000001);
        NSMLEmitStoreImm32(code, 0x02087E1C, 0x00000006);
        NSMLEmitStoreImm32(code, 0x02087E20, 0x00000002);
        NSMLEmitStoreImm32(code, 0x02087E24, 0x00000002);
        NSMLEmitStoreImm32(code, 0x02087E78, 0x00000042);
        NSMLEmitStoreImm32(code, 0x02087F04, 0xFFFF0003);
        NSMLEmitStoreImm32(code, 0x0208B044, 0xFFFF0003);
        NSMLEmitStoreImm32(code, 0x0208B048, 0x00000000);
        NSMLEmitStoreImm32(code, 0x0208B04C, 0x00000000);
        NSMLEmitMovImm(code, 0, 0x02088568);
        NSMLEmitMovImm(code, 1, 0x00B5FF00);
        NSMLEmitMovImm(code, 2, 0x02088558);
        NSMLEmitMovImm(code, 3, 0x02084FB4);
        NSMLEmitBLViaIP(code, applySceneRequestAddr);
        NSMLEmitMovImm(code, 0, 0x1E);
        NSMLEmitBLViaIP(code, startSceneTransitionAddr);
        NSMLEmitMovImm(code, 0, 0x03);
        NSMLEmitMovImm(code, 1, 0x00B5FF00);
        NSMLEmitMovImm(code, 2, 0x0208B040);
        NSMLEmitMovImm(code, 3, 0x01);
        NSMLEmitBLViaIP(code, courseSelectFactoryAddr);
        NSMLEmitStoreImm32(code, 0x0203B478, 0x00000001); // Scene::isSceneActive
        NSMLEmitStoreImm32(code, 0x0203B47C, 0x00000005); // Scene::previousSceneID
        NSMLEmitStoreImm32(code, 0x0203B480, 0x00000003); // Scene::nextSceneID
        NSMLEmitStoreImm32(code, 0x0203B484, 0x0000000F); // Scene::currentSceneID
    }
    else if (effectiveTryChangeScene)
    {
        NSMLEmitBLViaIP(code, tryChangeSceneAddr);
    }
    else
    {
        cpu->NDS.ARM9Write32(vsConnectBase + 0x218 + 0x008, 0x00000001);
        cpu->NDS.ARM9Write32(vsConnectBase + 0x218 + 0x064, 0x00000409);
        cpu->NDS.ARM9Write32(vsConnectBase + 0x218 + 0x078, 0x00000000);
        cpu->NDS.ARM9Write32(vsConnectBase + 0x218 + 0x07C, 0x020177AC);
        NSMLEmitStoreImm32(code, 0x02085058, 0x09); // Game::stageGroup
        NSMLEmitStoreImm32(code, 0x020850BC, playerID); // Game::localPlayerID
        NSMLEmitStoreImm32(code, 0x020850C4, 0x01); // Game::vsMode
        NSMLEmitMovImm(code, 0, vsConnectBase + 0x218);
        NSMLEmitMovImm(code, 1, startLoadLevelAddr);
        NSMLEmitMovImm(code, 2, 0);
        NSMLEmitMovImm(code, 3, 0x02156488);
        NSMLEmitBLViaIP(code, startLoadLevelAddr);
        NSMLEmitStoreImm32(code, 0x02087E14, 0x01);
        NSMLEmitStoreImm32(code, 0x02087E1C, 0x06);
        NSMLEmitStoreImm32(code, 0x02087E20, 0x02);
        NSMLEmitStoreImm32(code, 0x02087E24, 0x02);
        NSMLEmitStoreImm32(code, 0x02087E78, 0x42);
        NSMLEmitStoreImm32(code, 0x02085058, 0x09);
        NSMLEmitStoreImm32(code, 0x020850BC, playerID);
        NSMLEmitStoreImm32(code, 0x020850C4, 0x01);
        NSMLEmitStoreImm32(code, vsConnectBase + 0x144, 0x00000007);
        NSMLEmitStoreImm32(code, vsConnectBase + 0x148, 0x0000002C);
        NSMLEmitStoreImm32(code, vsConnectBase + 0x154, 0x00030000);
        NSMLEmitStoreImm32(code, vsConnectBase + 0x218 + 0x078, 0x00000000);
        NSMLEmitStoreImm32(code, vsConnectBase + 0x218 + 0x07C, 0x020177AC);
        NSMLEmitStoreImm32(code, 0x0203B478, 0x00000000); // Scene::isSceneActive
        NSMLEmitStoreImm32(code, 0x0203B47C, 0x00000005); // Scene::previousSceneID
        NSMLEmitStoreImm32(code, 0x0203B480, 0x0000000F); // Scene::nextSceneID
        NSMLEmitStoreImm32(code, 0x0203B484, 0x0000000F); // Scene::currentSceneID
    }
    code.push_back(0xE8BD0020u); // pop {r5}
    code.push_back(0xE128F005u); // msr apsr_nzcvq, r5
    code.push_back(0xE8BD5FFFu); // pop {r0-r12, lr}
    code.push_back(0xE59FC004u); // ldr ip, [pc, #4]
    code.push_back(0xE12FFF1Cu); // bx ip
    code.push_back(0xE1A00000u); // nop
    code.push_back(returnPC);

    for (size_t i = 0; i < code.size(); i++)
        cpu->NDS.ARM9Write32(trampolineAddr + static_cast<u32>(i * sizeof(u32)), code[i]);

    if (effectiveUpdateLoadGame)
        appliedFrame[&cpu->NDS] = cpu->NDS.NumFrames;
    else
        appliedMask[&cpu->NDS] |= modeMask;
    printf("NSMB SafeCall: %s frame=%u pc=%08X return=%08X sp=%08X lr=%08X cpsr=%08X vsConnect=%08X player=%u\n",
        effectiveLoadLevel ? "loadLevel" : effectiveCourseSelectFactory ? "courseSelectFactory" : effectiveCourseSelect ? "createCourseSelect" : effectiveCreateLoadGame ? "createLoadGameSM" : effectiveUpdateLoadGame ? "updateLoadGameSM" : effectiveScheduleLoadGame ? "scheduleLoadGameSM" : effectiveStageSceneFactory ? "stageSceneFactory" : effectiveTryChangeScene ? "tryChangeScene" : "startLoadLevel",
        cpu->NDS.NumFrames,
        instrAddr,
        returnPC,
        cpu->R[13],
        cpu->R[14],
        cpu->CPSR,
        vsConnectBase,
        playerID);
    fflush(stdout);
    cpu->JumpTo(trampolineAddr);
    return true;
}

static void HandleNSMLNetReadyHotPatch(ARM* cpu, u32 instrAddr)
{
    static int enabled = -1;
    static int forceStageSceneArg = -1;
    static int forceScheduleLoadGameSMArgs = -1;
    static int forceCourseSelectReady = -1;
    static u32 startFrame = 0;
    static u32 forceScheduleLoadGameSMArgsFrame = 0;
    static u32 forceCourseSelectReadyStartFrame = 0;
    if (enabled < 0)
    {
        const bool forceNetReady = NSMLEnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY");
        const bool forceScheduleLoadGameSM = NSMLEnvFlag("MELONDS_NSML_FORCE_SCHEDULE_LOAD_GAME_SM_ARGS");
        const bool forceCourseSelectReadyFlag = NSMLEnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_COURSE_SELECT_READY");
        enabled = (forceNetReady || forceScheduleLoadGameSM || forceCourseSelectReadyFlag) ? 1 : 0;
        forceStageSceneArg = NSMLEnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_STAGE_SCENE_ARG") ? 1 : 0;
        forceScheduleLoadGameSMArgs = forceScheduleLoadGameSM ? 1 : 0;
        forceCourseSelectReady = forceCourseSelectReadyFlag ? 1 : 0;
        if (const char* value = getenv("MELONDS_NSML_PACKET_BRIDGE_FORCE_NET_READY_START_FRAME"))
            startFrame = static_cast<u32>(strtoul(value, nullptr, 0));
        if (const char* value = getenv("MELONDS_NSML_FORCE_SCHEDULE_LOAD_GAME_SM_ARGS_FRAME"))
            forceScheduleLoadGameSMArgsFrame = static_cast<u32>(strtoul(value, nullptr, 0));
        if (const char* value = getenv("MELONDS_NSML_PACKET_BRIDGE_FORCE_COURSE_SELECT_READY_START_FRAME"))
            forceCourseSelectReadyStartFrame = static_cast<u32>(strtoul(value, nullptr, 0));
        printf("NSMB PacketBridge: net hotpatch config forceNetReady=%d forceScheduleLoadGameSMArgs=%d forceCourseSelectReady=%d scheduleFrame=%u\n",
            forceNetReady ? 1 : 0,
            forceScheduleLoadGameSMArgs,
            forceCourseSelectReady,
            forceScheduleLoadGameSMArgsFrame);
        fflush(stdout);
    }
    if (!enabled || !cpu || cpu->Num != 0)
        return;
    if (forceScheduleLoadGameSMArgs && cpu->NDS.NumFrames >= forceScheduleLoadGameSMArgsFrame
        && instrAddr == 0x021528A0)
    {
        cpu->R[1] = 0x02156624;
        cpu->R[2] = 0x1E;
        cpu->R[3] = 1;
        static int logCount = 0;
        if (logCount < 8)
        {
            printf("NSMB PacketBridge: redirect scheduleSubMenuChange to loadGameSM frame=%u vsConnect=%08X\n",
                cpu->NDS.NumFrames,
                cpu->R[0]);
            fflush(stdout);
            logCount++;
        }
    }
    if (forceCourseSelectReady && instrAddr == 0x0214C3C4)
    {
        const u32 courseSelectBase = NSMLFindObjectBaseByID(cpu->NDS, 0x0005);
        if (NSMLPacketBridgeEnabled()
            && cpu->NDS.NumFrames >= forceCourseSelectReadyStartFrame
            && courseSelectBase != 0
            && cpu->NDS.ARM9Read32(0x02085058) == 9
            && cpu->NDS.ARM9Read16(0x0203B484) == 0x000F)
        {
            cpu->R[0] = 1;
            static int logCount = 0;
            if (logCount < 16)
            {
                printf("NSMB PacketBridge: force CourseSelect ready result frame=%u courseSelect=%08X timer=%u settings=%08X\n",
                    cpu->NDS.NumFrames,
                    courseSelectBase,
                    cpu->NDS.ARM9Read32(courseSelectBase + 0x64),
                    cpu->NDS.ARM9Read32(courseSelectBase + 0x08));
                fflush(stdout);
                logCount++;
            }
        }
        return;
    }
    if (cpu->NDS.NumFrames < startFrame)
        return;
    if (instrAddr == 0x020068A8
        && (IsNSMLMarioVsLuigiPacketContext(cpu->NDS)
            || (NSMLPacketBridgeEnabled() && cpu->R[0] == 0x0F && cpu->R[2] == 9)))
    {
        if (cpu->R[0] == 0x0F && cpu->R[2] == 9)
        {
            static int roleClient = -1;
            if (roleClient < 0)
            {
                const char* role = getenv("MELONDS_NSML_ROLE");
                roleClient = (role && strcmp(role, "client") == 0) ? 1 : 0;
            }
            const u32 sp = cpu->R[13];
            cpu->R[1] = 1; // vs
            cpu->DataWrite32(sp + 0x00, 0); // act
            cpu->DataWrite32(sp + 0x04, roleClient ? 1 : 0); // playerID
            cpu->DataWrite32(sp + 0x08, 3); // playerMask
            cpu->DataWrite32(sp + 0x0C, 0); // character1: Mario
            cpu->DataWrite32(sp + 0x10, 1); // character2: Luigi
            cpu->DataWrite32(sp + 0x14, 0); // powerup
            cpu->DataWrite32(sp + 0x18, 0xFF); // entrance
            cpu->DataWrite32(sp + 0x1C, 1); // flag
            cpu->DataWrite32(sp + 0x20, 1); // unused1, matches normal MvL load path
            cpu->DataWrite32(sp + 0x24, 0xFF); // controlOptions
            cpu->DataWrite32(sp + 0x28, 0); // unused2
            cpu->DataWrite32(sp + 0x2C, 0); // challengeMode
            cpu->DataWrite32(sp + 0x30, 0xFFFFFFFFu); // rngSeed: use network/random state
            cpu->NDS.ARM9Write32(0x02087E78, 0x42);
            static int logCount = 0;
            if (logCount < 8)
            {
                printf("NSMB PacketBridge: force Game::loadLevel MvL args frame=%u playerID=%u\n",
                    cpu->NDS.NumFrames,
                    roleClient ? 1u : 0u);
                fflush(stdout);
                logCount++;
            }
        }
        return;
    }
    if (forceStageSceneArg && instrAddr == 0x020130A8 && IsNSMLMarioVsLuigiPacketContext(cpu->NDS))
    {
        if (cpu->R[0] == 0x00000003
            && cpu->R[1] == 0x00B5FF00
            && (cpu->R[2] < 0x02000000 || cpu->R[2] >= 0x02400000))
        {
            cpu->R[2] = 0x0208B040;
            static int logCount = 0;
            if (logCount < 8)
            {
                printf("NSMB PacketBridge: force stage scene create arg frame=%u\n",
                    cpu->NDS.NumFrames);
                fflush(stdout);
                logCount++;
            }
        }
        return;
    }
    if (instrAddr == 0x021514E4 && IsNSMLMarioVsLuigiPacketContext(cpu->NDS))
    {
        const u32 vsConnectBase = NSMLFindObjectBaseByID(cpu->NDS, 0x0006);
        if (vsConnectBase != 0
            && cpu->NDS.ARM9Read32(vsConnectBase + 0x120) == 0x02151E94
            && cpu->NDS.ARM9Read32(vsConnectBase + 0x144) == 6
            && (cpu->NDS.ARM9Read32(vsConnectBase + 0x154) & 0x00030000) == 0x00030000)
        {
            cpu->R[0] = 1;
            static int logCount = 0;
            if (logCount < 8)
            {
                printf("NSMB PacketBridge: force load-game net-ready result frame=%u vsConnect=%08X\n",
                    cpu->NDS.NumFrames,
                    vsConnectBase);
                fflush(stdout);
                logCount++;
            }
        }
        return;
    }
    if (instrAddr == 0x0214ED18 && IsNSMLMarioVsLuigiPacketContext(cpu->NDS))
    {
        const u32 courseSelectBase = NSMLFindObjectBaseByID(cpu->NDS, 0x0005);
        if (courseSelectBase != 0 && cpu->NDS.ARM9Read8(courseSelectBase + 0x64) == 1)
        {
            cpu->R[0] = 1;
            static int logCount = 0;
            if (logCount < 8)
            {
                printf("NSMB PacketBridge: force CourseSelect state1 ready result frame=%u courseSelect=%08X\n",
                    cpu->NDS.NumFrames,
                    courseSelectBase);
                fflush(stdout);
                logCount++;
            }
        }
        return;
    }
    if (instrAddr != 0x02151E94) // VSConnect::updateLoadGameSM()
        return;
    if (!IsNSMLMarioVsLuigiPacketContext(cpu->NDS))
        return;

    cpu->NDS.ARM9Write32(0x020880A4, 0x00000003); // Net::packetFreeBytesRecvBitmap
    cpu->NDS.ARM9Write32(0x020880A8, 0x00000003);
}

static bool HandleNSMLNetResetBypass(ARM* cpu, u32 instrAddr)
{
    static int enabled = -1;
    if (enabled < 0)
        enabled = NSMLEnvFlag("MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_RESET") ? 1 : 0;
    if (!enabled || !cpu || cpu->Num != 0)
        return false;
    if (instrAddr != 0x0200EDF8 && instrAddr != 0x0200EE00)
        return false;
    if (!NSMLPacketBridgeEnabled() || !IsNSMLMarioVsLuigiPacketContext(cpu->NDS))
        return false;

    static int logCount = 0;
    if (logCount < 16)
    {
        printf("NSMB PacketBridge: bypass Net reset function at %08X frame=%u lr=%08X\n",
            instrAddr,
            cpu->NDS.NumFrames,
            cpu->R[14]);
        logCount++;
    }

    if (instrAddr == 0x0200EE00)
    {
        // 0x0200EE00 is after the function prologue at 0x0200EDF8. If a build
        // reaches this address directly, undo the pushed r4/lr before returning.
        cpu->R[13] += 8;
    }
    cpu->JumpTo(cpu->R[14]);
    return true;
}

static bool HandleNSMLNetDisconnectBypass(ARM* cpu, u32 instrAddr)
{
    static int enabled = -1;
    if (enabled < 0)
        enabled = NSMLEnvFlag("MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_DISCONNECT") ? 1 : 0;
    if (!enabled || !cpu || cpu->Num != 0)
        return false;
    if (!NSMLPacketBridgeEnabled() || !IsNSMLMarioVsLuigiPacketContext(cpu->NDS))
        return false;

    static u32 startFrame = 0xFFFFFFFF;
    if (startFrame == 0xFFFFFFFF)
    {
        if (const char* value = getenv("MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_DISCONNECT_START_FRAME"))
            startFrame = static_cast<u32>(strtoul(value, nullptr, 0));
        else
            startFrame = 0;
    }
    if (cpu->NDS.NumFrames < startFrame)
        return false;

    static int mode = -1;
    if (mode < 0)
    {
        mode = 0;
        if (const char* value = getenv("MELONDS_NSML_PACKET_BRIDGE_BYPASS_NET_DISCONNECT_MODE"))
        {
            if (!strcmp(value, "force-active"))
                mode = 1;
        }
    }

    if (mode == 0)
    {
        if (instrAddr != 0x02010174)
            return false;

        static int skipLogCount = 0;
        if (skipLogCount < 16)
        {
            const u32 flags = cpu->NDS.ARM9Read16(0x02087E20);
            printf("NSMB PacketBridge: skip Net disconnect branch at %08X frame=%u lr=%08X flags=0x%04X\n",
                instrAddr,
                cpu->NDS.NumFrames,
                cpu->R[14],
                flags);
            skipLogCount++;
        }

        cpu->JumpTo(0x0201019C);
        return true;
    }

    if (instrAddr != 0x02010130)
        return false;

    const u32 flags = cpu->NDS.ARM9Read16(0x02087E20);
    if (flags != 0x0002)
        return false;

    static int logCount = 0;
    if (logCount < 16)
    {
        printf("NSMB PacketBridge: force Net active flags at %08X frame=%u lr=%08X\n",
            instrAddr,
            cpu->NDS.NumFrames,
            cpu->R[14]);
        printf("NSMB PacketBridge: Net flags old=0x%04X new=0x%04X\n",
            flags,
            0x0004);
        logCount++;
    }

    cpu->NDS.ARM9Write16(0x02087E20, 0x0004);
    return false;
}

static u32 NSMLPacketBridgeEnvFrame(const char* name, u32 fallback)
{
    if (const char* value = getenv(name))
        return static_cast<u32>(strtoul(value, nullptr, 0));
    return fallback;
}

static u32 NSMLPacketBridgeCanonicalTick(NDS& nds)
{
    static int enabled = -1;
    if (enabled < 0)
        enabled = NSMLEnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK") ? 1 : 0;
    if (!enabled || !NSMLPacketBridgeEnabled())
        return nds.ARM9Read16(0x02087F00);

    static u32 startFrame = 0xFFFFFFFF;
    if (startFrame == 0xFFFFFFFF)
        startFrame = NSMLPacketBridgeEnvFrame("MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK_START_FRAME", 0);
    if (nds.NumFrames < startFrame)
        return nds.ARM9Read16(0x02087F00);

    static int baseSet = -1;
    static u32 base = 0;
    if (baseSet < 0)
    {
        if (const char* value = getenv("MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK_BASE"))
        {
            base = static_cast<u32>(strtoul(value, nullptr, 0));
            baseSet = 1;
        }
        else
        {
            baseSet = 0;
        }
    }

    if (!baseSet)
        return nds.ARM9Read16(0x02087F00);

    return (base + (nds.NumFrames - startFrame)) & 0xFFFF;
}

static bool HandleNSMLTransferPacketBypass(ARM* cpu, u32 instrAddr)
{
    static int enabled = -1;
    if (enabled < 0)
        enabled = NSMLEnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_RESULT") ? 1 : 0;
    if (!enabled || !cpu || cpu->Num != 0 || instrAddr != 0x0200F98C)
        return false;
    if (NSMLEnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_CLIENT_ONLY"))
    {
        const char* role = getenv("MELONDS_NSML_ROLE");
        if (!role || strcmp(role, "client") != 0)
            return false;
    }
    if (!IsNSMLMarioVsLuigiPacketContext(cpu->NDS))
        return false;

    static u32 startFrame = 0xFFFFFFFF;
    if (startFrame == 0xFFFFFFFF)
        startFrame = NSMLPacketBridgeEnvFrame("MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_START_FRAME", 0);
    if (cpu->NDS.NumFrames < startFrame)
        return false;

    static u32 result = 0xFFFFFFFF;
    if (result == 0xFFFFFFFF)
        result = NSMLPacketBridgeEnvFrame("MELONDS_NSML_PACKET_BRIDGE_FORCE_TRANSFER_RESULT_VALUE", 8);

    static int logCount = 0;
    if (logCount < 16)
    {
        printf("NSMB PacketBridge: force transferPacket result at %08X frame=%u lr=%08X result=0x%08X\n",
            instrAddr,
            cpu->NDS.NumFrames,
            cpu->R[14],
            result);
        logCount++;
    }

    cpu->R[0] = result;
    cpu->JumpTo(cpu->R[14]);
    return true;
}

static void BuildNSMLMarioVsLuigiPacket(NDS& nds, std::array<u8, 52>& packet, u32& tick, u32& keys)
{
    packet.fill(0);
    tick = NSMLPacketBridgeCanonicalTick(nds);
    keys = nds.ARM9Read16(0x02087F02);
    packet[0] = static_cast<u8>(tick & 0xFF);
    packet[1] = static_cast<u8>((tick >> 8) & 0xFF);
    packet[2] = static_cast<u8>(keys & 0xFF);
    packet[3] = static_cast<u8>((keys >> 8) & 0xFF);
    packet[4] = nds.ARM9Read8(0x02087F04);
    packet[5] = nds.ARM9Read8(0x02087F05);
    packet[6] = nds.ARM9Read8(0x02087F06);
    packet[7] = nds.ARM9Read8(0x02087F07);
    for (u32 i = 0; i < 44; i++)
        packet[8 + i] = nds.ARM9Read8(0x02087F08 + i);
    if (NSMLEnvFlag("MELONDS_NSML_PACKET_BRIDGE_FORCE_PREGAME_ACTION1")
        && NSMLPacketBridgeAllowPreGame()
        && !IsNSMLMarioVsLuigiGameplay(nds))
    {
        packet[4] = 1;
    }
}

bool NSML_TakeMarioVsLuigiLocalPacket(NDS* nds, u8 outPacket[52], u32* outTick, u32* outKeys)
{
    if (!nds || !outPacket)
        return false;

    std::lock_guard<std::mutex> lock(NSMLPacketBridgeMutex);
    auto it = NSMLLocalPackets.find(nds);
    if (it == NSMLLocalPackets.end() || !it->second.Available)
        return false;

    memcpy(outPacket, it->second.Packet.data(), it->second.Packet.size());
    if (outTick) *outTick = it->second.Tick;
    if (outKeys) *outKeys = it->second.Keys;
    it->second.Available = false;
    return true;
}

bool NSML_BuildMarioVsLuigiLocalPacket(NDS* nds, u8 outPacket[52], u32* outTick, u32* outKeys)
{
    if (!nds || !outPacket || !IsNSMLMarioVsLuigiPacketContext(*nds))
        return false;

    std::array<u8, 52> packet {};
    u32 tick = 0;
    u32 keys = 0;
    BuildNSMLMarioVsLuigiPacket(*nds, packet, tick, keys);

    memcpy(outPacket, packet.data(), packet.size());
    if (outTick) *outTick = tick;
    if (outKeys) *outKeys = keys;
    return true;
}

void NSML_PushMarioVsLuigiRemotePacket(NDS* nds, u32 player, const u8 packet[52])
{
    if (!nds || !packet || player > 1)
        return;

    const u32 packetTick = packet[0] | (packet[1] << 8);
    const int offset = NSMLPacketBridgeReplayTickOffset();
    const u32 tick = static_cast<u32>((static_cast<int>(packetTick) + offset) & 0xFFFF);
    std::lock_guard<std::mutex> lock(NSMLPacketBridgeMutex);
    auto& packets = NSMLLiveReplayPackets[nds];
    auto& entry = packets[tick];
    memcpy(entry.Packet[player].data(), packet, entry.Packet[player].size());
    entry.Valid[player] = true;

    if (packets.size() > 512)
    {
        const u32 keepFrom = tick > 256 ? tick - 256 : 0;
        packets.erase(packets.begin(), packets.lower_bound(keepFrom));
    }
}

bool NSML_HasMarioVsLuigiRemotePacket(NDS* nds, u32 player, u32 tick)
{
    if (!nds || player > 1)
        return false;

    tick &= 0xFFFF;
    std::lock_guard<std::mutex> lock(NSMLPacketBridgeMutex);
    auto ndsIt = NSMLLiveReplayPackets.find(nds);
    if (ndsIt == NSMLLiveReplayPackets.end())
        return false;

    auto packetIt = ndsIt->second.find(tick);
    return packetIt != ndsIt->second.end() && packetIt->second.Valid[player];
}

static bool NSMLLiveReplayHasLead(NDS* nds, u32 player, u32 tick, u32 lead)
{
    if (!nds || player > 1 || lead == 0)
        return true;

    tick &= 0xFFFF;
    std::lock_guard<std::mutex> lock(NSMLPacketBridgeMutex);
    auto ndsIt = NSMLLiveReplayPackets.find(nds);
    if (ndsIt == NSMLLiveReplayPackets.end())
        return false;

    const u32 required = (tick + lead) & 0xFFFF;
    for (const auto& [packetTick, entry] : ndsIt->second)
    {
        if (!entry.Valid[player])
            continue;

        const u32 distance = (packetTick - tick) & 0xFFFF;
        if (distance <= 0x7FFF && packetTick == required)
            return true;
    }
    return false;
}

static bool NSMLFindLiveReplayPacketLocked(
    NDS* nds,
    u32 player,
    u32 tick,
    u32 fallbackWindow,
    std::array<u8, 52>& outPacket)
{
    if (!nds || player > 1)
        return false;

    auto ndsIt = NSMLLiveReplayPackets.find(nds);
    if (ndsIt == NSMLLiveReplayPackets.end())
        return false;

    auto liveIt = ndsIt->second.find(tick);
    if (liveIt != ndsIt->second.end() && liveIt->second.Valid[player])
    {
        outPacket = liveIt->second.Packet[player];
        return true;
    }

    if (fallbackWindow == 0)
        return false;

    static int nearestFallback = -1;
    if (nearestFallback < 0)
        nearestFallback = NSMLEnvFlag("MELONDS_NSML_PACKET_REPLAY_LIVE_FALLBACK_NEAREST") ? 1 : 0;

    const u32 window = std::min<u32>(fallbackWindow, 4096);
    for (u32 age = 1; age <= window; age++)
    {
        const u32 fallbackTick = (tick - age) & 0xFFFF;
        auto fallbackIt = ndsIt->second.find(fallbackTick);
        if (fallbackIt != ndsIt->second.end() && fallbackIt->second.Valid[player])
        {
            outPacket = fallbackIt->second.Packet[player];
            return true;
        }

        if (nearestFallback)
        {
            const u32 futureTick = (tick + age) & 0xFFFF;
            auto futureIt = ndsIt->second.find(futureTick);
            if (futureIt != ndsIt->second.end() && futureIt->second.Valid[player])
            {
                outPacket = futureIt->second.Packet[player];
                return true;
            }
        }
    }

    return false;
}

static bool NSMLSelectBridgePacketForPlayer(
    NDS& nds,
    u32 player,
    std::array<u8, 52>& packet)
{
    if (player > 1 || !NSMLPacketBridgeEnabled() || !IsNSMLMarioVsLuigiPacketContext(nds))
        return false;

    static int fallbackWindow = -1;
    static int normalizeTick = -1;
    if (fallbackWindow < 0)
    {
        if (const char* value = getenv("MELONDS_NSML_PACKET_REPLAY_LIVE_FALLBACK_WINDOW"))
            fallbackWindow = std::max(0, atoi(value));
        else
            fallbackWindow = 0;
    }
    if (normalizeTick < 0)
        normalizeTick = NSMLEnvFlag("MELONDS_NSML_PACKET_REPLAY_RETURN_LOOKUP_TICK") ? 1 : 0;

    const u32 localPlayer = NSMLPacketBridgeLocalPlayer();

    const u32 tick = NSMLPacketBridgeCanonicalTick(nds);
    if (player == localPlayer)
    {
        u32 ignoredTick = 0;
        u32 ignoredKeys = 0;
        BuildNSMLMarioVsLuigiPacket(nds, packet, ignoredTick, ignoredKeys);
        return true;
    }

    bool found = false;
    const u32 waitTimeoutMs = NSMLPacketBridgeWaitTimeoutMs();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(waitTimeoutMs);
    do
    {
        {
            std::lock_guard<std::mutex> lock(NSMLPacketBridgeMutex);
            found = NSMLFindLiveReplayPacketLocked(
                &nds,
                player,
                tick,
                static_cast<u32>(fallbackWindow),
                packet);
        }
        if (found || waitTimeoutMs == 0)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);

    if (found && normalizeTick)
    {
        packet[0] = static_cast<u8>(tick & 0xFF);
        packet[1] = static_cast<u8>((tick >> 8) & 0xFF);
    }
    return found;
}

static u32 NSMLWriteBridgePacketScratch(NDS& nds, u32 player, const std::array<u8, 52>& packet)
{
    constexpr u32 scratchBase = 0x023C1000;
    const u32 addr = scratchBase + (player & 1) * 0x40;
    for (u32 i = 0; i < packet.size(); i++)
        nds.ARM9Write8(addr + i, packet[i]);
    return addr;
}

static bool HandleNSMLLowerMPBridge(ARM* cpu, u32 instrAddr)
{
    if (!cpu || cpu->Num != 0 || !NSMLPacketBridgeEnabled())
        return false;
    if (!IsNSMLMarioVsLuigiPacketContext(cpu->NDS))
        return false;

    NSMLMaintainPacketFreeBytes(cpu->NDS);
    NSMLMaintainSessionPeers(cpu->NDS);

    static int traceLower = -1;
    if (traceLower < 0)
    {
        traceLower = (NSMLEnvFlag("MELONDS_NSML_PACKET_BRIDGE_TRACE")
            || NSMLEnvFlag("MELONDS_NSML_PACKET_BRIDGE_LOWER_TRACE")) ? 1 : 0;
    }

    if (instrAddr == 0x0204619C)
    {
        // transferPacket() enters the packet-copy path when this lower-MP
        // status probe returns false. PacketBridge supplies packets through
        // the per-player pointer hook below, so keep that path active.
        static int statusResult = -1;
        if (statusResult < 0)
        {
            if (const char* value = getenv("MELONDS_NSML_PACKET_BRIDGE_LOWER_STATUS_RESULT"))
                statusResult = atoi(value) != 0 ? 1 : 0;
            else
                statusResult = 0;
        }
        static u32 traceCount = 0;
        if (traceLower && (traceCount < 24 || (traceCount % 300) == 0))
            printf("NSMB PacketBridge lower: statusProbe 0204619C frame=%u tick=0x%04X lr=%08X -> %d\n",
                cpu->NDS.NumFrames,
                NSMLPacketBridgeCanonicalTick(cpu->NDS) & 0xFFFF,
                cpu->R[14],
                statusResult);
        traceCount++;
        cpu->R[0] = static_cast<u32>(statusResult);
        cpu->JumpTo(cpu->R[14]);
        return true;
    }

    if (instrAddr != 0x0204622C && instrAddr != 0x02046480)
        return false;

    const u32 player = cpu->R[0] & 0xFF;
    std::array<u8, 52> packet {};
    const bool hasPacket = NSMLSelectBridgePacketForPlayer(cpu->NDS, player, packet);
    const u32 tick = NSMLPacketBridgeCanonicalTick(cpu->NDS) & 0xFFFF;

    if (instrAddr == 0x0204622C)
    {
        static u32 traceCount[2] {};
        if (traceLower && player < 2 && (traceCount[player] < 32 || (traceCount[player] % 300) == 0))
            printf("NSMB PacketBridge lower: hasPacket 0204622C player=%u frame=%u tick=0x%04X action=0x%02X pktTick=0x%04X -> %u\n",
                player,
                cpu->NDS.NumFrames,
                tick,
                hasPacket ? packet[4] : 0xFF,
                hasPacket ? static_cast<u32>(packet[0] | (packet[1] << 8)) : 0xFFFF,
                hasPacket ? 1 : 0);
        if (player < 2)
            traceCount[player]++;
        cpu->R[0] = hasPacket ? 1 : 0;
        cpu->JumpTo(cpu->R[14]);
        return true;
    }

    const u32 packetPtr = hasPacket ? NSMLWriteBridgePacketScratch(cpu->NDS, player, packet) : 0;
    static u32 traceCount[2] {};
    if (traceLower && player < 2 && (traceCount[player] < 32 || (traceCount[player] % 300) == 0))
        printf("NSMB PacketBridge lower: getPacket 02046480 player=%u frame=%u tick=0x%04X action=0x%02X pktTick=0x%04X -> %08X\n",
            player,
            cpu->NDS.NumFrames,
            tick,
            hasPacket ? packet[4] : 0xFF,
            hasPacket ? static_cast<u32>(packet[0] | (packet[1] << 8)) : 0xFFFF,
            packetPtr);
    if (player < 2)
        traceCount[player]++;
    cpu->R[0] = packetPtr;
    cpu->JumpTo(cpu->R[14]);
    return true;
}

static void NSMLWriteLiveReplayPacketsToLocalMPSlots(
    NDS& nds,
    u32 tick,
    u32 fallbackWindow,
    bool normalizePacketTick)
{
    std::array<std::array<u8, 52>, 2> packets {};
    bool valid[2] {};
    {
        std::lock_guard<std::mutex> lock(NSMLPacketBridgeMutex);
        for (u32 player = 0; player < 2; player++)
            valid[player] = NSMLFindLiveReplayPacketLocked(&nds, player, tick, fallbackWindow, packets[player]);
    }

    for (u32 player = 0; player < 2; player++)
    {
        if (!valid[player])
            continue;

        if (normalizePacketTick)
        {
            packets[player][0] = static_cast<u8>(tick & 0xFF);
            packets[player][1] = static_cast<u8>((tick >> 8) & 0xFF);
        }

        nds.ARM9Write32(0x0208AE50 + player * 4, 1);
        const u32 packetAddr = 0x0208B040 + player * 0x3E;
        for (u32 i = 0; i < packets[player].size(); i++)
            nds.ARM9Write8(packetAddr + i, packets[player][i]);
    }
}

static void NSMLWriteReplayEntryToLocalMPSlots(
    NDS& nds,
    const NSMLPacketReplayEntry& entry,
    u32 tick,
    bool normalizePacketTick)
{
    for (u32 player = 0; player < 2; player++)
    {
        if (!entry.Valid[player])
            continue;

        std::array<u8, 52> packet = entry.Packet[player];
        if (normalizePacketTick)
        {
            packet[0] = static_cast<u8>(tick & 0xFF);
            packet[1] = static_cast<u8>((tick >> 8) & 0xFF);
        }

        nds.ARM9Write32(0x0208AE50 + player * 4, 1);
        const u32 packetAddr = 0x0208B040 + player * 0x3E;
        for (u32 i = 0; i < packet.size(); i++)
            nds.ARM9Write8(packetAddr + i, packet[i]);
    }
}

static bool NSMLFindReplayEntryForTick(
    const std::map<u32, NSMLPacketReplayEntry>& packets,
    u32 tick,
    u32 fallbackWindow,
    const NSMLPacketReplayEntry** outEntry,
    u32* outTick)
{
    auto it = packets.find(tick);
    if (it != packets.end())
    {
        if (outEntry) *outEntry = &it->second;
        if (outTick) *outTick = tick;
        return true;
    }

    const u32 window = std::min<u32>(fallbackWindow, 4096);
    for (u32 age = 1; age <= window; age++)
    {
        const u32 fallbackTick = (tick - age) & 0xFFFF;
        it = packets.find(fallbackTick);
        if (it != packets.end())
        {
            if (outEntry) *outEntry = &it->second;
            if (outTick) *outTick = fallbackTick;
            return true;
        }
    }

    return false;
}

void NSML_RefreshMarioVsLuigiPacketSlots(NDS* nds)
{
    if (!nds || !NSMLPacketBridgeEnabled() || !IsNSMLMarioVsLuigiPacketContext(*nds))
        return;

    static int fallbackWindow = -1;
    static int normalizeTick = -1;
    static int suppressDisconnect = -1;
    static int suppressBlackout = -1;
    static int preserveNetPointers = -1;
    if (fallbackWindow < 0)
    {
        if (const char* value = getenv("MELONDS_NSML_PACKET_REPLAY_LIVE_FALLBACK_WINDOW"))
            fallbackWindow = std::max(0, atoi(value));
        else
            fallbackWindow = 0;
    }
    if (normalizeTick < 0)
        normalizeTick = NSMLEnvFlag("MELONDS_NSML_PACKET_REPLAY_RETURN_LOOKUP_TICK") ? 1 : 0;
    if (suppressDisconnect < 0)
        suppressDisconnect = NSMLEnvFlag("MELONDS_NSML_PACKET_BRIDGE_SUPPRESS_DISCONNECT") ? 1 : 0;
    if (suppressBlackout < 0)
        suppressBlackout = NSMLEnvFlag("MELONDS_NSML_PACKET_BRIDGE_SUPPRESS_BLACKOUT") ? 1 : 0;
    if (preserveNetPointers < 0)
        preserveNetPointers = NSMLEnvFlag("MELONDS_NSML_PACKET_BRIDGE_PRESERVE_NET_POINTERS") ? 1 : 0;

    const u32 tick = nds->ARM9Read16(0x02087F00);
    NSMLMaintainPacketFreeBytes(*nds);
    NSMLMaintainSessionPeers(*nds);
    NSMLProbeStageStartReadyBits(*nds);
    NSMLWriteLiveReplayPacketsToLocalMPSlots(
        *nds,
        tick,
        static_cast<u32>(fallbackWindow),
        normalizeTick != 0);

    if (suppressDisconnect)
    {
        const u16 flags = nds->ARM9Read16(0x02087E5C);
        nds->ARM9Write16(0x02087E5C, flags & static_cast<u16>(~0xC390));
        if (nds->ARM9Read8(0x02087E1C) == 9)
            nds->ARM9Write8(0x02087E1C, 6);
    }

    if (preserveNetPointers)
    {
        static constexpr u32 addrs[] = {
            0x02087E0C,
            0x02087E90,
            0x02087ED8,
            0x02087EDC,
            0x02088020,
        };
        auto& saved = NSMLPreservedNetWords[nds];
        for (const u32 addr : addrs)
        {
            const u32 current = nds->ARM9Read32(addr);
            auto it = saved.find(addr);
            if (current != 0)
            {
                saved[addr] = current;
            }
            else if (it != saved.end() && it->second != 0)
            {
                nds->ARM9Write32(addr, it->second);
            }
        }
    }

    if (suppressBlackout)
    {
        // MvL disables display layers when the underlying LocalMP path disappears.
        // Keep the normal gameplay display setup while the packet bridge supplies game packets.
        nds->ARM9Write32(0x04000000, 0xC8211F3D);
        nds->ARM9Write32(0x04001000, 0x00011E10);
        nds->ARM9Write16(0x04000050, 0x3F40);
        nds->ARM9Write16(0x04000054, 0x0000);
        nds->ARM9Write16(0x04001050, 0x00FF);
        nds->ARM9Write16(0x04001054, 0x0000);
    }
}

static std::vector<std::string> SplitNSMLCsvLine(const std::string& line)
{
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, ','))
        out.push_back(item);
    return out;
}

static int FindNSMLCsvColumn(const std::vector<std::string>& header, const char* name)
{
    for (int i = 0; i < static_cast<int>(header.size()); i++)
    {
        if (header[i] == name)
            return i;
    }
    return -1;
}

static bool ParseNSMLHexPacket(const std::string& hex, std::array<u8, 52>& out)
{
    if (hex.size() < out.size() * 2)
        return false;

    for (size_t i = 0; i < out.size(); i++)
    {
        char tmp[3] { hex[i * 2], hex[i * 2 + 1], '\0' };
        out[i] = static_cast<u8>(strtoul(tmp, nullptr, 16));
    }
    return true;
}

static bool HandleNSMLPacketReplay(ARM* cpu, u32 instrAddr)
{
    struct PacketReplayConfig
    {
        bool Checked = false;
        bool Enabled = false;
        bool Strict = false;
        bool StrictPlayer[2] { true, true };
        u32 StrictStartFrame = 0;
        u32 StrictRequireLead = 0;
        u32 LookupTickDelay = 0;
        u32 LiveFallbackWindow = 0;
        bool ReturnLookupTick = false;
        bool ReplayOpEnabled[4] { true, true, true, true };
        FILE* LogFile = nullptr;
        std::map<u32, NSMLPacketReplayEntry> Packets;
    };

    static PacketReplayConfig cfg;
    if (!cfg.Checked)
    {
        std::lock_guard<std::mutex> configLock(NSMLTraceConfigMutex);
        if (!cfg.Checked)
        {
            cfg.Strict = getenv("MELONDS_NSML_PACKET_REPLAY_STRICT") != nullptr;
            if (const char* strictPlayers = getenv("MELONDS_NSML_PACKET_REPLAY_STRICT_PLAYERS"))
            {
                cfg.StrictPlayer[0] = false;
                cfg.StrictPlayer[1] = false;
                char buf[32];
                strncpy(buf, strictPlayers, sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
                for (char* tok = strtok(buf, ", \t\r\n"); tok; tok = strtok(nullptr, ", \t\r\n"))
                {
                    const u32 strictPlayer = static_cast<u32>(strtoul(tok, nullptr, 0));
                    if (strictPlayer <= 1)
                        cfg.StrictPlayer[strictPlayer] = true;
                }
            }
            if (const char* strictStartFrame = getenv("MELONDS_NSML_PACKET_REPLAY_STRICT_START_FRAME"))
                cfg.StrictStartFrame = static_cast<u32>(strtoul(strictStartFrame, nullptr, 0));
            if (const char* strictRequireLead = getenv("MELONDS_NSML_PACKET_REPLAY_STRICT_REQUIRE_LEAD"))
                cfg.StrictRequireLead = static_cast<u32>(strtoul(strictRequireLead, nullptr, 0));
            if (const char* lookupTickDelay = getenv("MELONDS_NSML_PACKET_REPLAY_LOOKUP_TICK_DELAY"))
                cfg.LookupTickDelay = static_cast<u32>(strtoul(lookupTickDelay, nullptr, 0));
            if (const char* liveFallbackWindow = getenv("MELONDS_NSML_PACKET_REPLAY_LIVE_FALLBACK_WINDOW"))
                cfg.LiveFallbackWindow = static_cast<u32>(strtoul(liveFallbackWindow, nullptr, 0));
            cfg.ReturnLookupTick = getenv("MELONDS_NSML_PACKET_REPLAY_RETURN_LOOKUP_TICK") != nullptr;
            if (const char* replayOps = getenv("MELONDS_NSML_PACKET_REPLAY_OPS"))
            {
                cfg.ReplayOpEnabled[0] = false;
                cfg.ReplayOpEnabled[1] = false;
                cfg.ReplayOpEnabled[2] = false;
                cfg.ReplayOpEnabled[3] = false;
                char buf[64];
                strncpy(buf, replayOps, sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
                for (char* tok = strtok(buf, ", \t\r\n"); tok; tok = strtok(nullptr, ", \t\r\n"))
                {
                    if (!_stricmp(tok, "keys")) cfg.ReplayOpEnabled[0] = true;
                    else if (!_stricmp(tok, "byte")) cfg.ReplayOpEnabled[1] = true;
                    else if (!_stricmp(tok, "tick")) cfg.ReplayOpEnabled[2] = true;
                    else if (!_stricmp(tok, "action")) cfg.ReplayOpEnabled[3] = true;
                }
            }
            const char* path = getenv("MELONDS_NSML_PACKET_REPLAY_FILE");
            const bool bridgeEnabled = NSMLPacketBridgeEnabled();
            if (const char* logPath = getenv("MELONDS_NSML_PACKET_REPLAY_LOG"))
            {
                if (logPath[0])
                    cfg.LogFile = fopen(logPath, "w");
                if (cfg.LogFile)
                    fprintf(cfg.LogFile, "frame,pc,tick,player,op,offset,value,hit\n");
            }
            if (path && path[0])
            {
                std::ifstream file(path);
                std::string line;
                if (std::getline(file, line))
                {
                    const auto header = SplitNSMLCsvLine(line);
                    const int tickCol = FindNSMLCsvColumn(header, "tick");
                    const int playerCol = FindNSMLCsvColumn(header, "player");
                    const int packetCol = FindNSMLCsvColumn(header, "packet_hex");
                    while (tickCol >= 0 && playerCol >= 0 && packetCol >= 0 && std::getline(file, line))
                    {
                        const auto cols = SplitNSMLCsvLine(line);
                        if (tickCol >= static_cast<int>(cols.size()) ||
                            playerCol >= static_cast<int>(cols.size()) ||
                            packetCol >= static_cast<int>(cols.size()))
                            continue;

                        const u32 tick = static_cast<u32>(strtoul(cols[tickCol].c_str(), nullptr, 0));
                        const u32 player = static_cast<u32>(strtoul(cols[playerCol].c_str(), nullptr, 0));
                        if (player > 1)
                            continue;

                        auto& entry = cfg.Packets[tick];
                        if (ParseNSMLHexPacket(cols[packetCol], entry.Packet[player]))
                            entry.Valid[player] = true;
                    }
                }
                cfg.Enabled = !cfg.Packets.empty();
                if (cfg.Enabled)
                    Log(LogLevel::Info, "NSMB packet replay: loaded %zu ticks from %s\n", cfg.Packets.size(), path);
                else
                    Log(LogLevel::Warn, "NSMB packet replay: no packets loaded from %s\n", path);
            }
            if (bridgeEnabled)
                cfg.Enabled = true;
            cfg.Checked = true;
        }
    }

    if (!cfg.Enabled || !cpu || cpu->Num != 0)
        return false;

    enum class Op
    {
        None,
        Keys,
        Byte,
        Tick,
        Action,
    };

    Op op = Op::None;
    if (instrAddr == 0x0200E700)
        op = Op::Keys;
    else if (instrAddr == 0x0200E978)
        op = Op::Byte;
    else if (instrAddr == 0x0200E9BC)
        op = Op::Tick;
    else if (instrAddr == 0x0200E9DC)
        op = Op::Action;
    else
        return false;

    const int opIndex =
        op == Op::Keys ? 0 :
        op == Op::Byte ? 1 :
        op == Op::Tick ? 2 :
        op == Op::Action ? 3 : -1;
    if (opIndex < 0 || !cfg.ReplayOpEnabled[opIndex])
        return false;

    const u32 player = cpu->R[0] & 0xFFFF;
    const u32 offset = cpu->R[1];
    if (!IsNSMLMarioVsLuigiPacketContext(cpu->NDS))
        return false;
    if (cfg.Strict && cpu->NDS.NumFrames < cfg.StrictStartFrame)
        return false;

    const u32 currentTick = NSMLPacketBridgeCanonicalTick(cpu->NDS);
    const u32 tick = (currentTick - cfg.LookupTickDelay) & 0xFFFF;
    if (NSMLPacketBridgeEnabled())
        NSMLWriteLiveReplayPacketsToLocalMPSlots(cpu->NDS, tick, cfg.LiveFallbackWindow, cfg.ReturnLookupTick);

    const NSMLPacketReplayEntry* replaySlotEntry = nullptr;
    u32 replaySlotTick = tick;
    if (NSMLFindReplayEntryForTick(cfg.Packets, tick, cfg.LiveFallbackWindow, &replaySlotEntry, &replaySlotTick))
    {
        NSMLWriteReplayEntryToLocalMPSlots(
            cpu->NDS,
            *replaySlotEntry,
            cfg.ReturnLookupTick ? tick : replaySlotTick,
            cfg.ReturnLookupTick);
    }

    u32 value = 0;
    bool hit = false;

    std::array<u8, 52> selectedPacket {};
    bool packetValid = false;
    if (player <= 1)
    {
        {
            std::lock_guard<std::mutex> lock(NSMLPacketBridgeMutex);
            packetValid = NSMLFindLiveReplayPacketLocked(
                &cpu->NDS,
                player,
                tick,
                cfg.LiveFallbackWindow,
                selectedPacket);
        }

        if (!packetValid)
        {
            const NSMLPacketReplayEntry* replayEntry = nullptr;
            u32 replayTick = tick;
            if (NSMLFindReplayEntryForTick(cfg.Packets, tick, cfg.LiveFallbackWindow, &replayEntry, &replayTick)
                && replayEntry->Valid[player])
            {
                selectedPacket = replayEntry->Packet[player];
                if (cfg.ReturnLookupTick)
                {
                    selectedPacket[0] = static_cast<u8>(tick & 0xFF);
                    selectedPacket[1] = static_cast<u8>((tick >> 8) & 0xFF);
                }
                packetValid = true;
            }
        }
    }

    if (packetValid)
    {
        const auto& packet = selectedPacket;
        switch (op)
        {
        case Op::Keys:
            value = packet[2] | (packet[3] << 8);
            hit = true;
            break;
        case Op::Byte:
            if (offset < 44)
            {
                value = packet[8 + offset];
                hit = true;
            }
            break;
        case Op::Tick:
            value = cfg.ReturnLookupTick ? tick : (packet[0] | (packet[1] << 8));
            hit = true;
            break;
        case Op::Action:
            value = packet[4];
            hit = true;
            break;
        case Op::None:
            break;
        }
    }

    if (cfg.LogFile)
    {
        std::lock_guard<std::mutex> outputLock(NSMLTraceOutputMutex);
        const char* opname =
            op == Op::Keys ? "keys" :
            op == Op::Byte ? "byte" :
            op == Op::Tick ? "tick" :
            op == Op::Action ? "action" : "none";
        fprintf(cfg.LogFile, "%u,%08X,%04X,%u,%s,%u,%08X,%d\n",
            cpu->NDS.NumFrames, instrAddr, tick, player, opname, offset, value, hit ? 1 : 0);
        fflush(cfg.LogFile);
    }

    if (!hit)
    {
        if (!cfg.Strict || player > 1 || !cfg.StrictPlayer[player] || cpu->NDS.NumFrames < cfg.StrictStartFrame)
            return false;
        if (!NSMLLiveReplayHasLead(&cpu->NDS, player, tick, cfg.StrictRequireLead))
            return false;

        switch (op)
        {
        case Op::Keys:
        case Op::Byte:
            value = 0;
            break;
        case Op::Tick:
            value = tick;
            break;
        case Op::Action:
            value = cpu->NDS.ARM9Read8(0x02087F04);
            break;
        case Op::None:
            value = 0;
            break;
        }
    }

    cpu->R[0] = value;
    cpu->JumpTo(cpu->R[14]);
    return true;
}

static void TraceNSMLPacketCapture(ARM* cpu, u32 instrAddr)
{
    struct PacketCaptureConfig
    {
        bool Checked = false;
        bool Enabled = false;
        bool BridgeEnabled = false;
        FILE* LogFile = nullptr;
        std::map<void*, u32> LastTickByNDS;
    };

    static PacketCaptureConfig cfg;
    if (!cfg.Checked)
    {
        std::lock_guard<std::mutex> configLock(NSMLTraceConfigMutex);
        if (!cfg.Checked)
        {
            if (const char* logPath = getenv("MELONDS_NSML_PACKET_CAPTURE_LOG"))
            {
                if (logPath[0])
                    cfg.LogFile = fopen(logPath, "w");
                if (cfg.LogFile)
                    fprintf(cfg.LogFile, "nds,frame,tick,keys,action,packet_hex\n");
            }
            cfg.BridgeEnabled = NSMLPacketBridgeEnabled();
            cfg.Enabled = cfg.LogFile != nullptr || cfg.BridgeEnabled;
            cfg.Checked = true;
        }
    }

    if (!cfg.Enabled || !cpu || cpu->Num != 0)
        return;
    if (instrAddr != 0x02011428) // Net::Core::processSendPacket()
        return;

    if (!IsNSMLMarioVsLuigiPacketContext(cpu->NDS))
        return;

    std::array<u8, 52> packet {};
    u32 builtTick = 0;
    u32 keys = 0;
    BuildNSMLMarioVsLuigiPacket(cpu->NDS, packet, builtTick, keys);

    const void* ndsKey = static_cast<const void*>(&cpu->NDS);
    auto last = cfg.LastTickByNDS.find(const_cast<void*>(ndsKey));
    if (last != cfg.LastTickByNDS.end() && last->second == builtTick)
        return;
    cfg.LastTickByNDS[const_cast<void*>(ndsKey)] = builtTick;

    if (cfg.BridgeEnabled)
    {
        std::lock_guard<std::mutex> lock(NSMLPacketBridgeMutex);
        auto& local = NSMLLocalPackets[&cpu->NDS];
        local.Available = true;
        local.Tick = builtTick;
        local.Keys = keys;
        local.Frame = cpu->NDS.NumFrames;
        local.Packet = packet;
    }

    if (cfg.LogFile)
    {
        std::lock_guard<std::mutex> outputLock(NSMLTraceOutputMutex);
        fprintf(cfg.LogFile, "%p,%u,0x%04X,0x%04X,0x%02X,",
            static_cast<void*>(&cpu->NDS),
            cpu->NDS.NumFrames,
            builtTick,
            keys,
            packet[4]);
        for (u8 byte : packet)
            fprintf(cfg.LogFile, "%02X", byte);
        fputc('\n', cfg.LogFile);
        fflush(cfg.LogFile);
    }
}

static bool TraceNSMLRandomCallImpl(ARM* cpu, u32 instrAddr, u32 lr, bool hasLR)
{
    struct RandomTraceConfig
    {
        bool Checked = false;
        bool Enabled = false;
        u32 Addr = 0x0200E5A0;
        u32 Addrs[256] {};
        int AddrCount = 0;
        u32 RandomValueAddr = 0x02088088;
        u32 RandomCallCountAddr = 0x02088068;
        u32 StartFrame = 0;
        u32 EndFrame = 0xFFFFFFFF;
        FILE* LogFile = nullptr;
    };

    static RandomTraceConfig cfg;
    if (!cfg.Checked)
    {
        std::lock_guard<std::mutex> configLock(NSMLTraceConfigMutex);
        if (!cfg.Checked)
        {
            cfg.Enabled = getenv("MELONDS_NSML_RANDOM_TRACE") != nullptr;
            if (const char* addr = getenv("MELONDS_NSML_RANDOM_TRACE_ADDR"))
                cfg.Addr = static_cast<u32>(strtoul(addr, nullptr, 0));
            cfg.Addrs[cfg.AddrCount++] = cfg.Addr;
            if (const char* addrs = getenv("MELONDS_NSML_RANDOM_TRACE_ADDRS"))
            {
                char buf[4096];
                strncpy(buf, addrs, sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
                for (char* tok = strtok(buf, ", \t\r\n"); tok && cfg.AddrCount < 256; tok = strtok(nullptr, ", \t\r\n"))
                    cfg.Addrs[cfg.AddrCount++] = static_cast<u32>(strtoul(tok, nullptr, 0));
            }
            if (const char* randomValueAddr = getenv("MELONDS_NSML_RANDOM_TRACE_VALUE_ADDR"))
                cfg.RandomValueAddr = static_cast<u32>(strtoul(randomValueAddr, nullptr, 0));
            if (const char* randomCallCountAddr = getenv("MELONDS_NSML_RANDOM_TRACE_CALLCOUNT_ADDR"))
                cfg.RandomCallCountAddr = static_cast<u32>(strtoul(randomCallCountAddr, nullptr, 0));
            if (const char* startFrame = getenv("MELONDS_NSML_RANDOM_TRACE_START_FRAME"))
                cfg.StartFrame = static_cast<u32>(strtoul(startFrame, nullptr, 0));
            if (const char* endFrame = getenv("MELONDS_NSML_RANDOM_TRACE_END_FRAME"))
                cfg.EndFrame = static_cast<u32>(strtoul(endFrame, nullptr, 0));
            if (const char* logPath = getenv("MELONDS_NSML_RANDOM_TRACE_LOG"))
            {
                if (logPath[0])
                {
                    cfg.LogFile = fopen(logPath, "w");
                    if (cfg.LogFile)
                        fprintf(cfg.LogFile, "nds,frame,pc,caller,lr,random_value,random_call_count\n");
                }
            }
            cfg.Checked = true;
        }
    }

    if (!cfg.Enabled || !cpu || cpu->Num != 0) return false;
    bool matched = false;
    for (int i = 0; i < cfg.AddrCount; i++)
    {
        if (instrAddr == cfg.Addrs[i])
        {
            matched = true;
            break;
        }
    }
    if (!matched) return false;
    if (cpu->NDS.NumFrames < cfg.StartFrame || cpu->NDS.NumFrames > cfg.EndFrame) return false;

    const u32 randomValue = cpu->NDS.ARM9Read32(cfg.RandomValueAddr);
    const u8 randomCallCount = cpu->NDS.ARM9Read8(cfg.RandomCallCountAddr);
    if (!hasLR)
        lr = cpu->R[14];
    const u32 caller = lr >= 4 ? lr - 4 : lr;
    if (cfg.LogFile)
    {
        std::lock_guard<std::mutex> outputLock(NSMLTraceOutputMutex);
        fprintf(cfg.LogFile,
            "%p,%u,%08X,%08X,%08X,%08X,%02X\n",
            static_cast<void*>(&cpu->NDS),
            cpu->NDS.NumFrames,
            instrAddr,
            caller,
            lr,
            randomValue,
            randomCallCount);
        fflush(cfg.LogFile);
    }
    else
    {
        std::lock_guard<std::mutex> outputLock(NSMLTraceOutputMutex);
        printf("NSMB Random: nds=%p frame=%u pc=%08X caller=%08X lr=%08X random=%08X count=%02X\n",
            static_cast<void*>(&cpu->NDS),
            cpu->NDS.NumFrames,
            instrAddr,
            caller,
            lr,
            randomValue,
            randomCallCount);
    }
    return true;
}

static bool IsNSMLMainRAMAddress(u32 addr)
{
    return (addr & 0xFF000000) == 0x02000000;
}

static bool IsNSMLDTCMAddress(ARM* cpu, u32 addr)
{
    return cpu && cpu->Num == 0 && cpu->NDS.ARM9.DTCM && ((addr & cpu->NDS.ARM9.DTCMMask) == cpu->NDS.ARM9.DTCMBase);
}

static u8 ReadNSMLTraceByte(ARM* cpu, u32 addr)
{
    if (IsNSMLDTCMAddress(cpu, addr))
        return cpu->NDS.ARM9.DTCM[addr & 0x3FFF];
    return cpu->NDS.ARM9Read8(addr);
}

static void WriteNSMLHexDump(FILE* file, ARM* cpu, u32 addr, u32 len)
{
    if (!file || !cpu || (!IsNSMLMainRAMAddress(addr) && !IsNSMLDTCMAddress(cpu, addr)) || len == 0)
    {
        fputc('-', file);
        return;
    }

    for (u32 i = 0; i < len; i++)
        fprintf(file, "%02X", ReadNSMLTraceByte(cpu, addr + i));
}

static bool ParseNSMLU32List(const char* value, std::vector<u32>& out)
{
    out.clear();
    if (!value || !value[0])
        return false;

    char buf[1024];
    strncpy(buf, value, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    for (char* tok = strtok(buf, ", \t\r\n"); tok; tok = strtok(nullptr, ", \t\r\n"))
        out.push_back(static_cast<u32>(strtoul(tok, nullptr, 0)));
    return !out.empty();
}

static void TraceNSMLWrite(ARM* cpu, u32 addr, u32 value, u32 size)
{
    struct WriteTraceConfig
    {
        bool Checked = false;
        bool Enabled = false;
        u32 StartFrame = 0;
        u32 EndFrame = 0;
        std::vector<u32> Addrs;
        FILE* LogFile = nullptr;
    };

    static WriteTraceConfig cfg;
    if (!cfg.Checked)
    {
        std::lock_guard<std::mutex> configLock(NSMLTraceConfigMutex);
        if (!cfg.Checked)
        {
            cfg.Enabled = getenv("MELONDS_NSML_WRITE_TRACE") != nullptr;
            ParseNSMLU32List(getenv("MELONDS_NSML_WRITE_TRACE_ADDRS"), cfg.Addrs);
            if (const char* startFrame = getenv("MELONDS_NSML_WRITE_TRACE_START_FRAME"))
                cfg.StartFrame = static_cast<u32>(strtoul(startFrame, nullptr, 0));
            if (const char* endFrame = getenv("MELONDS_NSML_WRITE_TRACE_END_FRAME"))
                cfg.EndFrame = static_cast<u32>(strtoul(endFrame, nullptr, 0));
            if (const char* logPath = getenv("MELONDS_NSML_WRITE_TRACE_LOG"))
            {
                if (logPath[0])
                    cfg.LogFile = fopen(logPath, "w");
                if (cfg.LogFile)
                    fprintf(cfg.LogFile, "nds,frame,pc,lr,addr,size,value,old\n");
            }
            cfg.Enabled = cfg.Enabled && cfg.LogFile && !cfg.Addrs.empty();
            cfg.Checked = true;
        }
    }

    if (!cfg.Enabled || !cpu || cpu->Num != 0)
        return;
    if (cpu->NDS.NumFrames < cfg.StartFrame)
        return;
    if (cfg.EndFrame != 0 && cpu->NDS.NumFrames > cfg.EndFrame)
        return;

    bool matched = false;
    for (u32 watchAddr : cfg.Addrs)
    {
        if (addr <= watchAddr && watchAddr < addr + (size / 8))
        {
            matched = true;
            break;
        }
    }
    if (!matched)
        return;

    u32 oldValue = 0;
    if (size == 8)
        oldValue = cpu->NDS.ARM9Read8(addr);
    else if (size == 16)
        oldValue = cpu->NDS.ARM9Read16(addr);
    else
        oldValue = cpu->NDS.ARM9Read32(addr);

    const u32 pc = cpu->R[15] - ((cpu->CPSR & 0x20) ? 2 : 4);
    std::lock_guard<std::mutex> outputLock(NSMLTraceOutputMutex);
    fprintf(cfg.LogFile, "%p,%u,%08X,%08X,%08X,%u,%08X,%08X\n",
        static_cast<void*>(&cpu->NDS),
        cpu->NDS.NumFrames,
        pc,
        cpu->R[14],
        addr,
        size,
        value,
        oldValue);
    fflush(cfg.LogFile);
}

static bool TraceNSMLCallImpl(ARM* cpu, u32 instrAddr)
{
    struct CallTraceConfig
    {
        bool Checked = false;
        bool Enabled = false;
        u32 Addrs[256] {};
        int AddrCount = 0;
        u32 StartFrame = 0;
        u32 EndFrame = 0xFFFFFFFF;
        u32 DumpLen = 32;
        bool UseR2AsDumpLen = false;
        FILE* LogFile = nullptr;
    };

    static CallTraceConfig cfg;
    if (!cfg.Checked)
    {
        std::lock_guard<std::mutex> configLock(NSMLTraceConfigMutex);
        if (!cfg.Checked)
        {
            cfg.Enabled = getenv("MELONDS_NSML_CALL_TRACE") != nullptr;

            // Useful A2DJ Net entry points by default. Override or extend with
            // MELONDS_NSML_CALL_TRACE_ADDRS while narrowing the packet boundary.
            cfg.Addrs[cfg.AddrCount++] = 0x0200E5E8; // Net::syncRandomFull()
            cfg.Addrs[cfg.AddrCount++] = 0x0200E5F4; // Net::syncRandomFast()
            cfg.Addrs[cfg.AddrCount++] = 0x02010810; // Net::onPacketPollingDefault()
            cfg.Addrs[cfg.AddrCount++] = 0x02010828; // Net::onRenderSignalStrengthDefault()
            cfg.Addrs[cfg.AddrCount++] = 0x02010930; // Net::setDefaultHandlers()
            cfg.Addrs[cfg.AddrCount++] = 0x02010F04; // Net::Core::shareRandomSeed()

            if (const char* addrs = getenv("MELONDS_NSML_CALL_TRACE_ADDRS"))
            {
                cfg.AddrCount = 0;
                char buf[4096];
                strncpy(buf, addrs, sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
                for (char* tok = strtok(buf, ", \t\r\n"); tok && cfg.AddrCount < 256; tok = strtok(nullptr, ", \t\r\n"))
                    cfg.Addrs[cfg.AddrCount++] = static_cast<u32>(strtoul(tok, nullptr, 0));
            }
            if (const char* startFrame = getenv("MELONDS_NSML_CALL_TRACE_START_FRAME"))
                cfg.StartFrame = static_cast<u32>(strtoul(startFrame, nullptr, 0));
            if (const char* endFrame = getenv("MELONDS_NSML_CALL_TRACE_END_FRAME"))
                cfg.EndFrame = static_cast<u32>(strtoul(endFrame, nullptr, 0));
            if (const char* dumpLen = getenv("MELONDS_NSML_CALL_TRACE_DUMP_LEN"))
                cfg.DumpLen = static_cast<u32>(strtoul(dumpLen, nullptr, 0));
            if (cfg.DumpLen > 512) cfg.DumpLen = 512;
            cfg.UseR2AsDumpLen = NSMLEnvFlag("MELONDS_NSML_CALL_TRACE_USE_R2_DUMP_LEN");
            if (const char* logPath = getenv("MELONDS_NSML_CALL_TRACE_LOG"))
            {
                if (logPath[0])
                {
                    cfg.LogFile = fopen(logPath, "w");
                    if (cfg.LogFile)
                        fprintf(cfg.LogFile, "nds,frame,pc,caller,lr,sp,cpsr,r0,r1,r2,r3,net_tick,net_action,net_seq_ids,net_seq_cursors,net_send_bitmap,net_seq_lengths,net_recv_bitmap,net_random,vs_step,vs_timer,vs_flags,r0_dump,r1_dump,r2_dump,r3_dump,sp_dump\n");
                }
            }
            cfg.Checked = true;
        }
    }

    if (!cfg.Enabled || !cpu || cpu->Num != 0) return false;
    if (cpu->NDS.NumFrames < cfg.StartFrame || cpu->NDS.NumFrames > cfg.EndFrame) return false;

    bool matched = false;
    for (int i = 0; i < cfg.AddrCount; i++)
    {
        if (instrAddr == cfg.Addrs[i])
        {
            matched = true;
            break;
        }
    }
    if (!matched) return false;

    const u32 lr = cpu->R[14];
    const u32 caller = lr >= 4 ? lr - 4 : lr;
    const u32 r0 = cpu->R[0];
    const u32 r1 = cpu->R[1];
    const u32 r2 = cpu->R[2];
    const u32 r3 = cpu->R[3];
    const u32 sp = cpu->R[13];
    const u32 cpsr = cpu->CPSR;
    const u32 netTick = cpu->NDS.ARM9Read16(0x02087F00);
    const u32 netAction = cpu->NDS.ARM9Read8(0x02087F04);
    const u32 netSeqIDs = cpu->NDS.ARM9Read32(0x02088078);
    const u32 netSeqCursors = cpu->NDS.ARM9Read32(0x0208807C);
    const u32 netSendBitmap = cpu->NDS.ARM9Read32(0x02088080);
    const u32 netSeqLengths = cpu->NDS.ARM9Read32(0x02088084);
    const u32 netRecvBitmap = cpu->NDS.ARM9Read32(0x020880A4);
    const u32 netRandom = cpu->NDS.ARM9Read32(0x02088088);
    const bool r0IsVsConnect = IsNSMLMainRAMAddress(r0) && cpu->NDS.ARM9Read16(r0 + 0x0C) == 0x0006;
    const u32 vsStep = r0IsVsConnect ? cpu->NDS.ARM9Read32(r0 + 0x144) : 0;
    const u32 vsTimer = r0IsVsConnect ? cpu->NDS.ARM9Read32(r0 + 0x148) : 0;
    const u32 vsFlags = r0IsVsConnect ? cpu->NDS.ARM9Read32(r0 + 0x154) : 0;
    u32 dumpLen = cfg.DumpLen;
    if (cfg.UseR2AsDumpLen && r2 > 0 && r2 < dumpLen) dumpLen = r2;

    FILE* out = cfg.LogFile ? cfg.LogFile : stdout;
    std::lock_guard<std::mutex> outputLock(NSMLTraceOutputMutex);
    fprintf(out,
        "%p,%u,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%04X,%02X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,",
        static_cast<void*>(&cpu->NDS),
        cpu->NDS.NumFrames,
        instrAddr,
        caller,
        lr,
        sp,
        cpsr,
        r0,
        r1,
        r2,
        r3,
        netTick,
        netAction,
        netSeqIDs,
        netSeqCursors,
        netSendBitmap,
        netSeqLengths,
        netRecvBitmap,
        netRandom,
        vsStep,
        vsTimer,
        vsFlags);
    WriteNSMLHexDump(out, cpu, r0, dumpLen);
    fputc(',', out);
    WriteNSMLHexDump(out, cpu, r1, dumpLen);
    fputc(',', out);
    WriteNSMLHexDump(out, cpu, r2, dumpLen);
    fputc(',', out);
    WriteNSMLHexDump(out, cpu, r3, dumpLen);
    fputc(',', out);
    WriteNSMLHexDump(out, cpu, sp, cfg.DumpLen);
    fputc('\n', out);
    fflush(out);
    return true;
}

bool TraceNSMLRandomCall(ARM* cpu, u32 instrAddr)
{
    TraceNSMLCallImpl(cpu, instrAddr);
    return TraceNSMLRandomCallImpl(cpu, instrAddr, 0, false);
}

bool TraceNSMLRandomCallFromJIT(ARM* cpu, u32 instrAddr, u32 lr)
{
    return TraceNSMLRandomCallImpl(cpu, instrAddr, lr, true);
}

#ifdef GDBSTUB_ENABLED
void ARM::GdbCheckA()
{
    if (!IsSingleStep && !BreakReq)
    { // check if eg. break signal is incoming etc.
        Gdb::StubState st = GdbStub.Enter(false, Gdb::TgtStatus::NoEvent, ~(u32)0u, BreakOnStartup);
        BreakOnStartup = false;
        IsSingleStep = st == Gdb::StubState::Step;
        BreakReq = st == Gdb::StubState::Attach || st == Gdb::StubState::Break;
    }
}
void ARM::GdbCheckB()
{
    if (IsSingleStep || BreakReq)
    { // use else here or we single-step the same insn twice in gdb
        u32 pc_real = R[15] - ((CPSR & 0x20) ? 2 : 4);
        Gdb::StubState st = GdbStub.Enter(true, Gdb::TgtStatus::SingleStep, pc_real);
        IsSingleStep = st == Gdb::StubState::Step;
        BreakReq = st == Gdb::StubState::Attach || st == Gdb::StubState::Break;
    }
}
void ARM::GdbCheckC()
{
    u32 pc_real = R[15] - ((CPSR & 0x20) ? 2 : 4);
    Gdb::StubState st = GdbStub.CheckBkpt(pc_real, true, true);
    if (st != Gdb::StubState::CheckNoHit)
    {
        IsSingleStep = st == Gdb::StubState::Step;
        BreakReq = st == Gdb::StubState::Attach || st == Gdb::StubState::Break;
    }
    else GdbCheckB();
}
#else
void ARM::GdbCheckA() {}
void ARM::GdbCheckB() {}
void ARM::GdbCheckC() {}
#endif


// instruction timing notes
//
// * simple instruction: 1S (code)
// * LDR: 1N+1N+1I (code/data/internal)
// * STR: 1N+1N (code/data)
// * LDM: 1N+1N+(n-1)S+1I
// * STM: 1N+1N+(n-1)S
// * MUL/etc: 1N+xI (code/internal)
// * branch: 1N+1S (code/code) (pipeline refill)
//
// MUL/MLA seems to take 1I on ARM9



const u32 ARM::ConditionTable[16] =
{
    0xF0F0, // EQ
    0x0F0F, // NE
    0xCCCC, // CS
    0x3333, // CC
    0xFF00, // MI
    0x00FF, // PL
    0xAAAA, // VS
    0x5555, // VC
    0x0C0C, // HI
    0xF3F3, // LS
    0xAA55, // GE
    0x55AA, // LT
    0x0A05, // GT
    0xF5FA, // LE
    0xFFFF, // AL
    0x0000  // NE
};

ARM::ARM(u32 num, bool jit, std::optional<GDBArgs> gdb, melonDS::NDS& nds) :
#ifdef GDBSTUB_ENABLED
    GdbStub(this),
    BreakOnStartup(false),
#endif
    Num(num), // well uh
    NDS(nds)
{
    SetGdbArgs(jit ? std::nullopt : gdb);
}

ARM::~ARM()
{
    // dorp
}

ARMv5::ARMv5(melonDS::NDS& nds, std::optional<GDBArgs> gdb, bool jit) : ARM(0, jit, gdb, nds)
{
    DTCM = NDS.JIT.Memory.GetARM9DTCM();

    PU_Map = PU_PrivMap;
}

ARMv4::ARMv4(melonDS::NDS& nds, std::optional<GDBArgs> gdb, bool jit) : ARM(1, jit, gdb, nds)
{
    //
}

ARMv5::~ARMv5()
{
    // DTCM is owned by Memory, not going to delete it
}

void ARM::SetGdbArgs(std::optional<GDBArgs> gdb)
{
#ifdef GDBSTUB_ENABLED
    GdbStub.Close();
    if (gdb)
    {
        int port = Num ? gdb->PortARM7 : gdb->PortARM9;
        GdbStub.Init(port);
        BreakOnStartup = Num ? gdb->ARM7BreakOnStartup : gdb->ARM9BreakOnStartup;
    }
    IsSingleStep = false;
#endif
}

void ARM::Reset()
{
    Cycles = 0;
    Halted = 0;

    IRQ = 0;

    for (int i = 0; i < 16; i++)
        R[i] = 0;

    CPSR = 0x000000D3;

    for (int i = 0; i < 7; i++)
        R_FIQ[i] = 0;
    for (int i = 0; i < 2; i++)
    {
        R_SVC[i] = 0;
        R_ABT[i] = 0;
        R_IRQ[i] = 0;
        R_UND[i] = 0;
    }

    R_FIQ[7] = 0x00000010;
    R_SVC[2] = 0x00000010;
    R_ABT[2] = 0x00000010;
    R_IRQ[2] = 0x00000010;
    R_UND[2] = 0x00000010;

    ExceptionBase = Num ? 0x00000000 : 0xFFFF0000;

    CodeMem.Mem = NULL;

#ifdef JIT_ENABLED
    FastBlockLookup = NULL;
    FastBlockLookupStart = 0;
    FastBlockLookupSize = 0;
#endif

#ifdef GDBSTUB_ENABLED
    IsSingleStep = false;
    BreakReq = false;
#endif

    // zorp
    JumpTo(ExceptionBase);
}

void ARMv5::Reset()
{
    PU_Map = PU_PrivMap;

    ARM::Reset();
}


void ARM::DoSavestate(Savestate* file)
{
    file->Section((char*)(Num ? "ARM7" : "ARM9"));

    file->Var32((u32*)&Cycles);
    //file->Var32((u32*)&CyclesToRun);

    // hack to make save states compatible
    u32 halted = Halted;
    file->Var32(&halted);
    Halted = halted;

    file->VarArray(R, 16*sizeof(u32));
    file->Var32(&CPSR);
    file->VarArray(R_FIQ, 8*sizeof(u32));
    file->VarArray(R_SVC, 3*sizeof(u32));
    file->VarArray(R_ABT, 3*sizeof(u32));
    file->VarArray(R_IRQ, 3*sizeof(u32));
    file->VarArray(R_UND, 3*sizeof(u32));
    file->Var32(&CurInstr);
#ifdef JIT_ENABLED
    if (file->Saving && NDS.IsJITEnabled())
    {
        // hack, the JIT doesn't really pipeline
        // but we still want JIT save states to be
        // loaded while running the interpreter
        FillPipeline();
    }
#endif
    file->VarArray(NextInstr, 2*sizeof(u32));

    file->Var32(&ExceptionBase);

    if (!file->Saving)
    {
        CPSR |= 0x00000010;
        R_FIQ[7] |= 0x00000010;
        R_SVC[2] |= 0x00000010;
        R_ABT[2] |= 0x00000010;
        R_IRQ[2] |= 0x00000010;
        R_UND[2] |= 0x00000010;

        if (!Num)
        {
            SetupCodeMem(R[15]); // should fix it
            ((ARMv5*)this)->RegionCodeCycles = ((ARMv5*)this)->MemTimings[R[15] >> 12][0];

            if ((CPSR & 0x1F) == 0x10)
                ((ARMv5*)this)->PU_Map = ((ARMv5*)this)->PU_UserMap;
            else
                ((ARMv5*)this)->PU_Map = ((ARMv5*)this)->PU_PrivMap;
        }
        else
        {
            CodeRegion = R[15] >> 24;
            CodeCycles = R[15] >> 15; // cheato
        }
    }
}

void ARMv5::DoSavestate(Savestate* file)
{
    ARM::DoSavestate(file);
    CP15DoSavestate(file);
}


void ARM::SetupCodeMem(u32 addr)
{
    if (!Num)
    {
        ((ARMv5*)this)->GetCodeMemRegion(addr, &CodeMem);
    }
    else
    {
        // not sure it's worth it for the ARM7
        // esp. as everything there generally runs on WRAM
        // and due to how it's mapped, we can't use this optimization
        //NDS::ARM7GetMemRegion(addr, false, &CodeMem);
    }
}

void ARMv5::JumpTo(u32 addr, bool restorecpsr)
{
    if (restorecpsr)
    {
        RestoreCPSR();

        if (CPSR & 0x20)    addr |= 0x1;
        else                addr &= ~0x1;
    }

    // aging cart debug crap
    //if (addr == 0x0201764C) printf("capture test %d: R1=%08X\n", R[6], R[1]);
    //if (addr == 0x020175D8) printf("capture test %d: res=%08X\n", R[6], R[0]);

    u32 oldregion = R[15] >> 24;
    u32 newregion = addr >> 24;

    RegionCodeCycles = MemTimings[addr >> 12][0];

    if (addr & 0x1)
    {
        addr &= ~0x1;
        R[15] = addr+2;

        if (newregion != oldregion) SetupCodeMem(addr);

        // two-opcodes-at-once fetch
        // doesn't matter if we put garbage in the MSbs there
        if (addr & 0x2)
        {
            NextInstr[0] = CodeRead32(addr-2, true) >> 16;
            Cycles += CodeCycles;
            NextInstr[1] = CodeRead32(addr+2, false);
            Cycles += CodeCycles;
        }
        else
        {
            NextInstr[0] = CodeRead32(addr, true);
            NextInstr[1] = NextInstr[0] >> 16;
            Cycles += CodeCycles;
        }

        CPSR |= 0x20;
    }
    else
    {
        addr &= ~0x3;
        R[15] = addr+4;

        if (newregion != oldregion) SetupCodeMem(addr);

        NextInstr[0] = CodeRead32(addr, true);
        Cycles += CodeCycles;
        NextInstr[1] = CodeRead32(addr+4, false);
        Cycles += CodeCycles;

        CPSR &= ~0x20;
    }

    if (!(PU_Map[addr>>12] & 0x04))
    {
        PrefetchAbort();
        return;
    }

    NDS.MonitorARM9Jump(addr);
}

void ARMv4::JumpTo(u32 addr, bool restorecpsr)
{
    if (restorecpsr)
    {
        RestoreCPSR();

        if (CPSR & 0x20)    addr |= 0x1;
        else                addr &= ~0x1;
    }

    u32 oldregion = R[15] >> 23;
    u32 newregion = addr >> 23;

    CodeRegion = addr >> 24;
    CodeCycles = addr >> 15; // cheato

    if (addr & 0x1)
    {
        addr &= ~0x1;
        R[15] = addr+2;

        //if (newregion != oldregion) SetupCodeMem(addr);

        NextInstr[0] = CodeRead16(addr);
        NextInstr[1] = CodeRead16(addr+2);
        Cycles += NDS.ARM7MemTimings[CodeCycles][0] + NDS.ARM7MemTimings[CodeCycles][1];

        CPSR |= 0x20;
    }
    else
    {
        addr &= ~0x3;
        R[15] = addr+4;

        //if (newregion != oldregion) SetupCodeMem(addr);

        NextInstr[0] = CodeRead32(addr);
        NextInstr[1] = CodeRead32(addr+4);
        Cycles += NDS.ARM7MemTimings[CodeCycles][2] + NDS.ARM7MemTimings[CodeCycles][3];

        CPSR &= ~0x20;
    }
}

void ARM::RestoreCPSR()
{
    u32 oldcpsr = CPSR;

    switch (CPSR & 0x1F)
    {
    case 0x11:
        CPSR = R_FIQ[7];
        break;

    case 0x12:
        CPSR = R_IRQ[2];
        break;

    case 0x13:
        CPSR = R_SVC[2];
        break;

    case 0x14:
    case 0x15:
    case 0x16:
    case 0x17:
        CPSR = R_ABT[2];
        break;

    case 0x18:
    case 0x19:
    case 0x1A:
    case 0x1B:
        CPSR = R_UND[2];
        break;

    default:
        Log(LogLevel::Warn, "!! attempt to restore CPSR under bad mode %02X, %08X\n", CPSR&0x1F, R[15]);
        break;
    }

    CPSR |= 0x00000010;

    UpdateMode(oldcpsr, CPSR);
}

void ARM::UpdateMode(u32 oldmode, u32 newmode, bool phony)
{
    if ((oldmode & 0x1F) == (newmode & 0x1F)) return;

    switch (oldmode & 0x1F)
    {
    case 0x11:
        std::swap(R[8], R_FIQ[0]);
        std::swap(R[9], R_FIQ[1]);
        std::swap(R[10], R_FIQ[2]);
        std::swap(R[11], R_FIQ[3]);
        std::swap(R[12], R_FIQ[4]);
        std::swap(R[13], R_FIQ[5]);
        std::swap(R[14], R_FIQ[6]);
        break;

    case 0x12:
        std::swap(R[13], R_IRQ[0]);
        std::swap(R[14], R_IRQ[1]);
        break;

    case 0x13:
        std::swap(R[13], R_SVC[0]);
        std::swap(R[14], R_SVC[1]);
        break;

    case 0x17:
        std::swap(R[13], R_ABT[0]);
        std::swap(R[14], R_ABT[1]);
        break;

    case 0x1B:
        std::swap(R[13], R_UND[0]);
        std::swap(R[14], R_UND[1]);
        break;
    }

    switch (newmode & 0x1F)
    {
    case 0x11:
        std::swap(R[8], R_FIQ[0]);
        std::swap(R[9], R_FIQ[1]);
        std::swap(R[10], R_FIQ[2]);
        std::swap(R[11], R_FIQ[3]);
        std::swap(R[12], R_FIQ[4]);
        std::swap(R[13], R_FIQ[5]);
        std::swap(R[14], R_FIQ[6]);
        break;

    case 0x12:
        std::swap(R[13], R_IRQ[0]);
        std::swap(R[14], R_IRQ[1]);
        break;

    case 0x13:
        std::swap(R[13], R_SVC[0]);
        std::swap(R[14], R_SVC[1]);
        break;

    case 0x17:
        std::swap(R[13], R_ABT[0]);
        std::swap(R[14], R_ABT[1]);
        break;

    case 0x1B:
        std::swap(R[13], R_UND[0]);
        std::swap(R[14], R_UND[1]);
        break;
    }

    if ((!phony) && (Num == 0))
    {
        if ((newmode & 0x1F) == 0x10)
            ((ARMv5*)this)->PU_Map = ((ARMv5*)this)->PU_UserMap;
        else
            ((ARMv5*)this)->PU_Map = ((ARMv5*)this)->PU_PrivMap;
    }
}

void ARM::TriggerIRQ()
{
    if (CPSR & 0x80)
        return;

    u32 oldcpsr = CPSR;
    CPSR &= ~0xFF;
    CPSR |= 0xD2;
    UpdateMode(oldcpsr, CPSR);

    R_IRQ[2] = oldcpsr;
    R[14] = R[15] + (oldcpsr & 0x20 ? 2 : 0);
    JumpTo(ExceptionBase + 0x18);

    // ARDS cheat support
    // normally, those work by hijacking the ARM7 VBlank handler
    if (Num == 1)
    {
        if ((NDS.IF[1] & NDS.IE[1]) & (1<<IRQ_VBlank))
            NDS.AREngine.RunCheats();
    }
}

void ARMv5::PrefetchAbort()
{
    Log(LogLevel::Warn, "ARM9: prefetch abort (%08X)\n", R[15]);

    u32 oldcpsr = CPSR;
    CPSR &= ~0xBF;
    CPSR |= 0x97;
    UpdateMode(oldcpsr, CPSR);

    // this shouldn't happen, but if it does, we're stuck in some nasty endless loop
    // so better take care of it
    if (!(PU_Map[ExceptionBase>>12] & 0x04))
    {
        Log(LogLevel::Error, "!!!!! EXCEPTION REGION NOT EXECUTABLE. THIS IS VERY BAD!!\n");
        NDS.Stop(Platform::StopReason::BadExceptionRegion);
        return;
    }

    R_ABT[2] = oldcpsr;
    R[14] = R[15] + (oldcpsr & 0x20 ? 2 : 0);
    JumpTo(ExceptionBase + 0x0C);
}

void ARMv5::DataAbort()
{
    Log(LogLevel::Warn,
        "ARM9: data abort (pc=%08X lr=%08X sp=%08X cpsr=%08X instr=%08X fault=%08X r0=%08X r1=%08X r2=%08X r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X r8=%08X r9=%08X r10=%08X r11=%08X r12=%08X)\n",
        R[15],
        R[14],
        R[13],
        CPSR,
        CurInstr,
        DataRegion,
        R[0],
        R[1],
        R[2],
        R[3],
        R[4],
        R[5],
        R[6],
        R[7],
        R[8],
        R[9],
        R[10],
        R[11],
        R[12]);

    u32 oldcpsr = CPSR;
    CPSR &= ~0xBF;
    CPSR |= 0x97;
    UpdateMode(oldcpsr, CPSR);

    R_ABT[2] = oldcpsr;
    R[14] = R[15] + (oldcpsr & 0x20 ? 4 : 0);
    JumpTo(ExceptionBase + 0x10);
}

void ARM::CheckGdbIncoming()
{
    GdbCheckA();
}

template <CPUExecuteMode mode>
void ARMv5::Execute()
{
    if constexpr (mode == CPUExecuteMode::InterpreterGDB)
        GdbCheckB();

    if (Halted)
    {
        if (Halted == 2)
        {
            Halted = 0;
        }
        else if (NDS.HaltInterrupted(0))
        {
            Halted = 0;
            if (NDS.IME[0] & 0x1)
                TriggerIRQ();
        }
        else
        {
            NDS.ARM9Timestamp = NDS.ARM9Target;
            return;
        }
    }

    while (NDS.ARM9Timestamp < NDS.ARM9Target)
    {
#ifdef JIT_ENABLED
        if constexpr (mode == CPUExecuteMode::JIT)
        {
            u32 instrAddr = R[15] - ((CPSR&0x20)?2:4);
            HandleNSMLNetReadyHotPatch(this, instrAddr);
            TraceNSMLPacketCapture(this, instrAddr);
            if (HandleNSMLSafeLevelCall(this, instrAddr))
            {
                NDS.ARM9Timestamp++;
                continue;
            }
            if (HandleNSMLLowerMPBridge(this, instrAddr))
            {
                NDS.ARM9Timestamp++;
                continue;
            }
            TraceNSMLRandomCall(this, instrAddr);

            if ((instrAddr < FastBlockLookupStart || instrAddr >= (FastBlockLookupStart + FastBlockLookupSize))
                && !NDS.JIT.SetupExecutableRegion(0, instrAddr, FastBlockLookup, FastBlockLookupStart, FastBlockLookupSize))
            {
                NDS.ARM9Timestamp = NDS.ARM9Target;
                Log(LogLevel::Error, "ARMv5 PC in non executable region %08X\n", R[15]);
                return;
            }

            JitBlockEntry block = NDS.JIT.LookUpBlock(0, FastBlockLookup,
                instrAddr - FastBlockLookupStart, instrAddr);
            if (block)
                ARM_Dispatch(this, block);
            else
                NDS.JIT.CompileBlock(this);

            if (StopExecution)
            {
                // this order is crucial otherwise idle loops waiting for an IRQ won't function
                if (IRQ)
                    TriggerIRQ();

                if (Halted || IdleLoop)
                {
                    if ((Halted == 1 || IdleLoop) && NDS.ARM9Timestamp < NDS.ARM9Target)
                    {
                        Cycles = 0;
                        NDS.ARM9Timestamp = NDS.ARM9Target;
                    }
                    IdleLoop = 0;
                    break;
                }
            }
        }
        else
#endif
        {
            if (CPSR & 0x20) // THUMB
            {
                if constexpr (mode == CPUExecuteMode::InterpreterGDB)
                    GdbCheckC();
                const u32 instrAddr = R[15] - 2;
                HandleNSMLNetReadyHotPatch(this, instrAddr);
                TraceNSMLPacketCapture(this, instrAddr);
                if (HandleNSMLSafeLevelCall(this, instrAddr))
                {
                    NDS.ARM9Timestamp++;
                    continue;
                }
                if (HandleNSMLLowerMPBridge(this, instrAddr))
                {
                    NDS.ARM9Timestamp++;
                    continue;
                }
                if (HandleNSMLTransferPacketBypass(this, instrAddr))
                {
                    NDS.ARM9Timestamp++;
                    continue;
                }
                if (HandleNSMLNetDisconnectBypass(this, instrAddr))
                {
                    NDS.ARM9Timestamp++;
                    continue;
                }
                if (HandleNSMLNetResetBypass(this, instrAddr))
                {
                    NDS.ARM9Timestamp++;
                    continue;
                }
                if (HandleNSMLPacketReplay(this, instrAddr))
                {
                    NDS.ARM9Timestamp++;
                    continue;
                }
                TraceNSMLRandomCall(this, instrAddr);

                // prefetch
                R[15] += 2;
                CurInstr = NextInstr[0];
                NextInstr[0] = NextInstr[1];
                if (R[15] & 0x2) { NextInstr[1] >>= 16; CodeCycles = 0; }
                else             NextInstr[1] = CodeRead32(R[15], false);

                // actually execute
                u32 icode = (CurInstr >> 6) & 0x3FF;
                ARMInterpreter::THUMBInstrTable[icode](this);
            }
            else
            {
                if constexpr (mode == CPUExecuteMode::InterpreterGDB)
                    GdbCheckC();
                const u32 instrAddr = R[15] - 4;
                HandleNSMLNetReadyHotPatch(this, instrAddr);
                TraceNSMLPacketCapture(this, instrAddr);
                if (HandleNSMLSafeLevelCall(this, instrAddr))
                {
                    NDS.ARM9Timestamp++;
                    continue;
                }
                if (HandleNSMLLowerMPBridge(this, instrAddr))
                {
                    NDS.ARM9Timestamp++;
                    continue;
                }
                if (HandleNSMLTransferPacketBypass(this, instrAddr))
                {
                    NDS.ARM9Timestamp++;
                    continue;
                }
                if (HandleNSMLNetDisconnectBypass(this, instrAddr))
                {
                    NDS.ARM9Timestamp++;
                    continue;
                }
                if (HandleNSMLNetResetBypass(this, instrAddr))
                {
                    NDS.ARM9Timestamp++;
                    continue;
                }
                if (HandleNSMLPacketReplay(this, instrAddr))
                {
                    NDS.ARM9Timestamp++;
                    continue;
                }
                TraceNSMLRandomCall(this, instrAddr);

                // prefetch
                R[15] += 4;
                CurInstr = NextInstr[0];
                NextInstr[0] = NextInstr[1];
                NextInstr[1] = CodeRead32(R[15], false);

                // actually execute
                if (CheckCondition(CurInstr >> 28))
                {
                    u32 icode = ((CurInstr >> 4) & 0xF) | ((CurInstr >> 16) & 0xFF0);
                    ARMInterpreter::ARMInstrTable[icode](this);
                }
                else if ((CurInstr & 0xFE000000) == 0xFA000000)
                {
                    ARMInterpreter::A_BLX_IMM(this);
                }
                else
                    AddCycles_C();
            }

            // TODO optimize this shit!!!
            if (Halted)
            {
                if (Halted == 1 && NDS.ARM9Timestamp < NDS.ARM9Target)
                {
                    NDS.ARM9Timestamp = NDS.ARM9Target;
                }
                break;
            }
            /*if (NDS::IF[0] & NDS::IE[0])
            {
                if (NDS::IME[0] & 0x1)
                    TriggerIRQ();
            }*/
            if (IRQ) TriggerIRQ();

        }

        NDS.ARM9Timestamp += Cycles;
        Cycles = 0;
    }

    if (Halted == 2)
        Halted = 0;
}
template void ARMv5::Execute<CPUExecuteMode::Interpreter>();
template void ARMv5::Execute<CPUExecuteMode::InterpreterGDB>();
#ifdef JIT_ENABLED
template void ARMv5::Execute<CPUExecuteMode::JIT>();
#endif

template <CPUExecuteMode mode>
void ARMv4::Execute()
{
    if constexpr (mode == CPUExecuteMode::InterpreterGDB)
        GdbCheckB();

    if (Halted)
    {
        if (Halted == 2)
        {
            Halted = 0;
        }
        else if (NDS.HaltInterrupted(1))
        {
            Halted = 0;
            if (NDS.IME[1] & 0x1)
                TriggerIRQ();
        }
        else
        {
            NDS.ARM7Timestamp = NDS.ARM7Target;
            return;
        }
    }

    while (NDS.ARM7Timestamp < NDS.ARM7Target)
    {
#ifdef JIT_ENABLED
        if constexpr (mode == CPUExecuteMode::JIT)
        {
            u32 instrAddr = R[15] - ((CPSR&0x20)?2:4);
            TraceNSMLRandomCall(this, instrAddr);

            if ((instrAddr < FastBlockLookupStart || instrAddr >= (FastBlockLookupStart + FastBlockLookupSize))
                && !NDS.JIT.SetupExecutableRegion(1, instrAddr, FastBlockLookup, FastBlockLookupStart, FastBlockLookupSize))
            {
                NDS.ARM7Timestamp = NDS.ARM7Target;
                Log(LogLevel::Error, "ARMv4 PC in non executable region %08X\n", R[15]);
                return;
            }

            JitBlockEntry block = NDS.JIT.LookUpBlock(1, FastBlockLookup,
                instrAddr - FastBlockLookupStart, instrAddr);
            if (block)
                ARM_Dispatch(this, block);
            else
                NDS.JIT.CompileBlock(this);

            if (StopExecution)
            {
                if (IRQ)
                    TriggerIRQ();

                if (Halted || IdleLoop)
                {
                    if ((Halted == 1 || IdleLoop) && NDS.ARM7Timestamp < NDS.ARM7Target)
                    {
                        Cycles = 0;
                        NDS.ARM7Timestamp = NDS.ARM7Target;
                    }
                    IdleLoop = 0;
                    break;
                }
            }
        }
        else
#endif
        {
            if (CPSR & 0x20) // THUMB
            {
                if constexpr (mode == CPUExecuteMode::InterpreterGDB)
                    GdbCheckC();

                // prefetch
                R[15] += 2;
                CurInstr = NextInstr[0];
                NextInstr[0] = NextInstr[1];
                NextInstr[1] = CodeRead16(R[15]);

                // actually execute
                u32 icode = (CurInstr >> 6);
                ARMInterpreter::THUMBInstrTable[icode](this);
            }
            else
            {
                if constexpr (mode == CPUExecuteMode::InterpreterGDB)
                    GdbCheckC();

                // prefetch
                R[15] += 4;
                CurInstr = NextInstr[0];
                NextInstr[0] = NextInstr[1];
                NextInstr[1] = CodeRead32(R[15]);

                // actually execute
                if (CheckCondition(CurInstr >> 28))
                {
                    u32 icode = ((CurInstr >> 4) & 0xF) | ((CurInstr >> 16) & 0xFF0);
                    ARMInterpreter::ARMInstrTable[icode](this);
                }
                else
                    AddCycles_C();
            }

            // TODO optimize this shit!!!
            if (Halted)
            {
                if (Halted == 1 && NDS.ARM7Timestamp < NDS.ARM7Target)
                {
                    NDS.ARM7Timestamp = NDS.ARM7Target;
                }
                break;
            }
            /*if (NDS::IF[1] & NDS::IE[1])
            {
                if (NDS::IME[1] & 0x1)
                    TriggerIRQ();
            }*/
            if (IRQ) TriggerIRQ();
        }

        NDS.ARM7Timestamp += Cycles;
        Cycles = 0;
    }

    if (Halted == 2)
        Halted = 0;

    if (Halted == 4)
    {
        assert(NDS.ConsoleType == 1);
        auto& dsi = dynamic_cast<melonDS::DSi&>(NDS);
        dsi.SoftReset();
        Halted = 2;
    }
}

template void ARMv4::Execute<CPUExecuteMode::Interpreter>();
template void ARMv4::Execute<CPUExecuteMode::InterpreterGDB>();
#ifdef JIT_ENABLED
template void ARMv4::Execute<CPUExecuteMode::JIT>();
#endif

void ARMv5::FillPipeline()
{
    SetupCodeMem(R[15]);

    if (CPSR & 0x20)
    {
        if ((R[15] - 2) & 0x2)
        {
            NextInstr[0] = CodeRead32(R[15] - 4, false) >> 16;
            NextInstr[1] = CodeRead32(R[15], false);
        }
        else
        {
            NextInstr[0] = CodeRead32(R[15] - 2, false);
            NextInstr[1] = NextInstr[0] >> 16;
        }
    }
    else
    {
        NextInstr[0] = CodeRead32(R[15] - 4, false);
        NextInstr[1] = CodeRead32(R[15], false);
    }
}

void ARMv4::FillPipeline()
{
    SetupCodeMem(R[15]);

    if (CPSR & 0x20)
    {
        NextInstr[0] = CodeRead16(R[15] - 2);
        NextInstr[1] = CodeRead16(R[15]);
    }
    else
    {
        NextInstr[0] = CodeRead32(R[15] - 4);
        NextInstr[1] = CodeRead32(R[15]);
    }
}

#ifdef GDBSTUB_ENABLED
u32 ARM::ReadReg(Gdb::Register reg)
{
    using Gdb::Register;
    int r = static_cast<int>(reg);

    if (reg < Register::pc) return R[r];
    else if (reg == Register::pc)
    {
        return R[r] - ((CPSR & 0x20) ? 2 : 4);
    }
    else if (reg == Register::cpsr) return CPSR;
    else if (reg == Register::sp_usr || reg == Register::lr_usr)
    {
        r -= static_cast<int>(Register::sp_usr);
        if (ModeIs(0x10) || ModeIs(0x1f))
        {
            return R[13 + r];
        }
        else switch (CPSR & 0x1f)
        {
        case 0x11: return R_FIQ[5 + r];
        case 0x12: return R_IRQ[0 + r];
        case 0x13: return R_SVC[0 + r];
        case 0x17: return R_ABT[0 + r];
        case 0x1b: return R_UND[0 + r];
        }
    }
    else if (reg >= Register::r8_fiq && reg <= Register::lr_fiq)
    {
        r -= static_cast<int>(Register::r8_fiq);
        return ModeIs(0x11) ? R[ 8 + r] : R_FIQ[r];
    }
    else if (reg == Register::sp_irq || reg == Register::lr_irq)
    {
        r -= static_cast<int>(Register::sp_irq);
        return ModeIs(0x12) ? R[13 + r] : R_IRQ[r];
    }
    else if (reg == Register::sp_svc || reg == Register::lr_svc)
    {
        r -= static_cast<int>(Register::sp_svc);
        return ModeIs(0x13) ? R[13 + r] : R_SVC[r];
    }
    else if (reg == Register::sp_abt || reg == Register::lr_abt)
    {
        r -= static_cast<int>(Register::sp_abt);
        return ModeIs(0x17) ? R[13 + r] : R_ABT[r];
    }
    else if (reg == Register::sp_und || reg == Register::lr_und)
    {
        r -= static_cast<int>(Register::sp_und);
        return ModeIs(0x1b) ? R[13 + r] : R_UND[r];
    }
    else if (reg == Register::spsr_fiq) return ModeIs(0x11) ? CPSR : R_FIQ[7];
    else if (reg == Register::spsr_irq) return ModeIs(0x12) ? CPSR : R_IRQ[2];
    else if (reg == Register::spsr_svc) return ModeIs(0x13) ? CPSR : R_SVC[2];
    else if (reg == Register::spsr_abt) return ModeIs(0x17) ? CPSR : R_ABT[2];
    else if (reg == Register::spsr_und) return ModeIs(0x1b) ? CPSR : R_UND[2];

    Log(LogLevel::Warn, "GDB reg read: unknown reg no %d\n", r);
    return 0xdeadbeef;
}
void ARM::WriteReg(Gdb::Register reg, u32 v)
{
    using Gdb::Register;
    int r = static_cast<int>(reg);

    if (reg < Register::pc) R[r] = v;
    else if (reg == Register::pc) JumpTo(v);
    else if (reg == Register::cpsr) CPSR = v;
    else if (reg == Register::sp_usr || reg == Register::lr_usr)
    {
        r -= static_cast<int>(Register::sp_usr);
        if (ModeIs(0x10) || ModeIs(0x1f))
        {
            R[13 + r] = v;
        }
        else switch (CPSR & 0x1f)
        {
        case 0x11: R_FIQ[5 + r] = v; break;
        case 0x12: R_IRQ[0 + r] = v; break;
        case 0x13: R_SVC[0 + r] = v; break;
        case 0x17: R_ABT[0 + r] = v; break;
        case 0x1b: R_UND[0 + r] = v; break;
        }
    }
    else if (reg >= Register::r8_fiq && reg <= Register::lr_fiq)
    {
        r -= static_cast<int>(Register::r8_fiq);
        *(ModeIs(0x11) ? &R[ 8 + r] : &R_FIQ[r]) = v;
    }
    else if (reg == Register::sp_irq || reg == Register::lr_irq)
    {
        r -= static_cast<int>(Register::sp_irq);
        *(ModeIs(0x12) ? &R[13 + r] : &R_IRQ[r]) = v;
    }
    else if (reg == Register::sp_svc || reg == Register::lr_svc)
    {
        r -= static_cast<int>(Register::sp_svc);
        *(ModeIs(0x13) ? &R[13 + r] : &R_SVC[r]) = v;
    }
    else if (reg == Register::sp_abt || reg == Register::lr_abt)
    {
        r -= static_cast<int>(Register::sp_abt);
        *(ModeIs(0x17) ? &R[13 + r] : &R_ABT[r]) = v;
    }
    else if (reg == Register::sp_und || reg == Register::lr_und)
    {
        r -= static_cast<int>(Register::sp_und);
        *(ModeIs(0x1b) ? &R[13 + r] : &R_UND[r]) = v;
    }
    else if (reg == Register::spsr_fiq)
    {
        *(ModeIs(0x11) ? &CPSR : &R_FIQ[7]) = v;
    }
    else if (reg == Register::spsr_irq)
    {
        *(ModeIs(0x12) ? &CPSR : &R_IRQ[2]) = v;
    }
    else if (reg == Register::spsr_svc)
    {
        *(ModeIs(0x13) ? &CPSR : &R_SVC[2]) = v;
    }
    else if (reg == Register::spsr_abt)
    {
        *(ModeIs(0x17) ? &CPSR : &R_ABT[2]) = v;
    }
    else if (reg == Register::spsr_und)
    {
        *(ModeIs(0x1b) ? &CPSR : &R_UND[2]) = v;
    }
    else Log(LogLevel::Warn, "GDB reg write: unknown reg no %d (write 0x%08x)\n", r, v);
}
u32 ARM::ReadMem(u32 addr, int size)
{
    if (size == 8) return BusRead8(addr);
    else if (size == 16) return BusRead16(addr);
    else if (size == 32) return BusRead32(addr);
    else return 0xfeedface;
}
void ARM::WriteMem(u32 addr, int size, u32 v)
{
    if (size == 8) BusWrite8(addr, (u8)v);
    else if (size == 16) BusWrite16(addr, (u16)v);
    else if (size == 32) BusWrite32(addr, v);
}

void ARM::ResetGdb()
{
    NDS.Reset();
    NDS.GPU.StartFrame(); // need this to properly kick off the scheduler & frame output
}
int ARM::RemoteCmd(const u8* cmd, size_t len)
{
    (void)len;

    Log(LogLevel::Info, "[ARMGDB] Rcmd: \"%s\"\n", cmd);
    if (!strcmp((const char*)cmd, "reset") || !strcmp((const char*)cmd, "r"))
    {
        Reset();
        return 0;
    }

    return 1; // not implemented (yet)
}

void ARMv5::WriteMem(u32 addr, int size, u32 v)
{
    if (addr < ITCMSize)
    {
        if (size == 8) *(u8*)&ITCM[addr & (ITCMPhysicalSize - 1)] = (u8)v;
        else if (size == 16) *(u16*)&ITCM[addr & (ITCMPhysicalSize - 1)] = (u16)v;
        else if (size == 32) *(u32*)&ITCM[addr & (ITCMPhysicalSize - 1)] = (u32)v;
        else {}
        return;
    }
    else if ((addr & DTCMMask) == DTCMBase)
    {
        if (size == 8) *(u8*)&DTCM[addr & (DTCMPhysicalSize - 1)] = (u8)v;
        else if (size == 16) *(u16*)&DTCM[addr & (DTCMPhysicalSize - 1)] = (u16)v;
        else if (size == 32) *(u32*)&DTCM[addr & (DTCMPhysicalSize - 1)] = (u32)v;
        else {}
        return;
    }

    ARM::WriteMem(addr, size, v);
}
u32 ARMv5::ReadMem(u32 addr, int size)
{
    if (addr < ITCMSize)
    {
        if (size == 8) return *(u8*)&ITCM[addr & (ITCMPhysicalSize - 1)];
        else if (size == 16) return *(u16*)&ITCM[addr & (ITCMPhysicalSize - 1)];
        else if (size == 32) return *(u32*)&ITCM[addr & (ITCMPhysicalSize - 1)];
        else return 0xfeedface;
    }
    else if ((addr & DTCMMask) == DTCMBase)
    {
        if (size == 8) return *(u8*)&DTCM[addr & (DTCMPhysicalSize - 1)];
        else if (size == 16) return *(u16*)&DTCM[addr & (DTCMPhysicalSize - 1)];
        else if (size == 32) return *(u32*)&DTCM[addr & (DTCMPhysicalSize - 1)];
        else return 0xfeedface;
    }

    return ARM::ReadMem(addr, size);
}
#endif

void ARMv4::DataRead8(u32 addr, u32* val)
{
    *val = BusRead8(addr);
    DataRegion = addr;
    DataCycles = NDS.ARM7MemTimings[addr >> 15][0];
}

void ARMv4::DataRead16(u32 addr, u32* val)
{
    addr &= ~1;

    *val = BusRead16(addr);
    DataRegion = addr;
    DataCycles = NDS.ARM7MemTimings[addr >> 15][0];
}

void ARMv4::DataRead32(u32 addr, u32* val)
{
    addr &= ~3;

    *val = BusRead32(addr);
    DataRegion = addr;
    DataCycles = NDS.ARM7MemTimings[addr >> 15][2];
}

void ARMv4::DataRead32S(u32 addr, u32* val)
{
    addr &= ~3;

    *val = BusRead32(addr);
    DataCycles += NDS.ARM7MemTimings[addr >> 15][3];
}

void ARMv4::DataWrite8(u32 addr, u8 val)
{
    BusWrite8(addr, val);
    DataRegion = addr;
    DataCycles = NDS.ARM7MemTimings[addr >> 15][0];
}

void ARMv4::DataWrite16(u32 addr, u16 val)
{
    addr &= ~1;

    BusWrite16(addr, val);
    DataRegion = addr;
    DataCycles = NDS.ARM7MemTimings[addr >> 15][0];
}

void ARMv4::DataWrite32(u32 addr, u32 val)
{
    addr &= ~3;

    BusWrite32(addr, val);
    DataRegion = addr;
    DataCycles = NDS.ARM7MemTimings[addr >> 15][2];
}

void ARMv4::DataWrite32S(u32 addr, u32 val)
{
    addr &= ~3;

    BusWrite32(addr, val);
    DataCycles += NDS.ARM7MemTimings[addr >> 15][3];
}


void ARMv4::AddCycles_C()
{
    // code only. this code fetch is sequential.
    Cycles += NDS.ARM7MemTimings[CodeCycles][(CPSR&0x20)?1:3];
}

void ARMv4::AddCycles_CI(s32 num)
{
    // code+internal. results in a nonseq code fetch.
    Cycles += NDS.ARM7MemTimings[CodeCycles][(CPSR&0x20)?0:2] + num;
}

void ARMv4::AddCycles_CDI()
{
    // LDR/LDM cycles.
    s32 numC = NDS.ARM7MemTimings[CodeCycles][(CPSR&0x20)?0:2];
    s32 numD = DataCycles;

    if ((DataRegion >> 24) == 0x02) // mainRAM
    {
        if (CodeRegion == 0x02)
            Cycles += numC + numD;
        else
        {
            numC++;
            Cycles += std::max(numC + numD - 3, std::max(numC, numD));
        }
    }
    else if (CodeRegion == 0x02)
    {
        numD++;
        Cycles += std::max(numC + numD - 3, std::max(numC, numD));
    }
    else
    {
        Cycles += numC + numD + 1;
    }
}

void ARMv4::AddCycles_CD()
{
    // TODO: max gain should be 5c when writing to mainRAM
    s32 numC = NDS.ARM7MemTimings[CodeCycles][(CPSR&0x20)?0:2];
    s32 numD = DataCycles;

    if ((DataRegion >> 24) == 0x02)
    {
        if (CodeRegion == 0x02)
            Cycles += numC + numD;
        else
            Cycles += std::max(numC + numD - 3, std::max(numC, numD));
    }
    else if (CodeRegion == 0x02)
    {
        Cycles += std::max(numC + numD - 3, std::max(numC, numD));
    }
    else
    {
        Cycles += numC + numD;
    }
}

u8 ARMv5::BusRead8(u32 addr)
{
    return NDS.ARM9Read8(addr);
}

u16 ARMv5::BusRead16(u32 addr)
{
    return NDS.ARM9Read16(addr);
}

u32 ARMv5::BusRead32(u32 addr)
{
    return NDS.ARM9Read32(addr);
}

void ARMv5::BusWrite8(u32 addr, u8 val)
{
    TraceNSMLWrite(this, addr, val, 8);
    NDS.ARM9Write8(addr, val);
}

void ARMv5::BusWrite16(u32 addr, u16 val)
{
    TraceNSMLWrite(this, addr, val, 16);
    NDS.ARM9Write16(addr, val);
}

void ARMv5::BusWrite32(u32 addr, u32 val)
{
    TraceNSMLWrite(this, addr, val, 32);
    NDS.ARM9Write32(addr, val);
}

u8 ARMv4::BusRead8(u32 addr)
{
    return NDS.ARM7Read8(addr);
}

u16 ARMv4::BusRead16(u32 addr)
{
    return NDS.ARM7Read16(addr);
}

u32 ARMv4::BusRead32(u32 addr)
{
    return NDS.ARM7Read32(addr);
}

void ARMv4::BusWrite8(u32 addr, u8 val)
{
    NDS.ARM7Write8(addr, val);
}

void ARMv4::BusWrite16(u32 addr, u16 val)
{
    NDS.ARM7Write16(addr, val);
}

void ARMv4::BusWrite32(u32 addr, u32 val)
{
    NDS.ARM7Write32(addr, val);
}
}

