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
#include <map>
#include <mutex>
#include <sstream>
#include <string>
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

static bool IsNSMLMarioVsLuigiGameplay(NDS& nds)
{
    return nds.ARM9Read32(0x02085058) == 9
        && nds.ARM9Read32(0x020850C4) == 1
        && nds.ARM9Read32(0x02087E78) == 0x42;
}

static void BuildNSMLMarioVsLuigiPacket(NDS& nds, std::array<u8, 52>& packet, u32& tick, u32& keys)
{
    packet.fill(0);
    tick = nds.ARM9Read16(0x02087F00);
    keys = nds.ARM9Read16(0x02087F02);
    packet[0] = static_cast<u8>(tick & 0xFF);
    packet[1] = static_cast<u8>((tick >> 8) & 0xFF);
    packet[2] = static_cast<u8>(keys & 0xFF);
    packet[3] = static_cast<u8>((keys >> 8) & 0xFF);
    packet[4] = 0x03; // MvL gameplay packet action.
    packet[5] = 0x00;
    packet[6] = 0xFF;
    packet[7] = 0xFF;
    for (u32 i = 0; i < 44; i++)
        packet[8 + i] = nds.ARM9Read8(0x02087F08 + i);
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

    const u32 player = cpu->R[0] & 0xFFFF;
    const u32 offset = cpu->R[1];
    if (!IsNSMLMarioVsLuigiGameplay(cpu->NDS))
        return false;

    const u32 tick = cpu->NDS.ARM9Read16(0x02087F00);
    u32 value = 0;
    bool hit = false;

    std::array<u8, 52> selectedPacket {};
    bool packetValid = false;
    if (player <= 1)
    {
        {
            std::lock_guard<std::mutex> lock(NSMLPacketBridgeMutex);
            auto ndsIt = NSMLLiveReplayPackets.find(&cpu->NDS);
            if (ndsIt != NSMLLiveReplayPackets.end())
            {
                auto liveIt = ndsIt->second.find(tick);
                if (liveIt != ndsIt->second.end() && liveIt->second.Valid[player])
                {
                    selectedPacket = liveIt->second.Packet[player];
                    packetValid = true;
                }
            }
        }

        if (!packetValid)
        {
            auto it = cfg.Packets.find(tick);
            if (it != cfg.Packets.end() && it->second.Valid[player])
            {
                selectedPacket = it->second.Packet[player];
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
            value = packet[0] | (packet[1] << 8);
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
            value = 0x03;
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

    if (!IsNSMLMarioVsLuigiGameplay(cpu->NDS))
        return;

    const u32 tick = cpu->NDS.ARM9Read16(0x02087F00);
    const void* ndsKey = static_cast<const void*>(&cpu->NDS);
    auto last = cfg.LastTickByNDS.find(const_cast<void*>(ndsKey));
    if (last != cfg.LastTickByNDS.end() && last->second == tick)
        return;
    cfg.LastTickByNDS[const_cast<void*>(ndsKey)] = tick;

    std::array<u8, 52> packet {};
    u32 builtTick = 0;
    u32 keys = 0;
    BuildNSMLMarioVsLuigiPacket(cpu->NDS, packet, builtTick, keys);

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
        fprintf(cfg.LogFile, "%p,%u,0x%04X,0x%04X,0x03,",
            static_cast<void*>(&cpu->NDS),
            cpu->NDS.NumFrames,
            builtTick,
            keys);
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

static void WriteNSMLHexDump(FILE* file, ARM* cpu, u32 addr, u32 len)
{
    if (!file || !cpu || !IsNSMLMainRAMAddress(addr) || len == 0)
    {
        fputc('-', file);
        return;
    }

    for (u32 i = 0; i < len; i++)
        fprintf(file, "%02X", cpu->NDS.ARM9Read8(addr + i));
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
            if (const char* logPath = getenv("MELONDS_NSML_CALL_TRACE_LOG"))
            {
                if (logPath[0])
                {
                    cfg.LogFile = fopen(logPath, "w");
                    if (cfg.LogFile)
                        fprintf(cfg.LogFile, "nds,frame,pc,caller,lr,r0,r1,r2,r3,r0_dump,r1_dump,r2_dump,r3_dump\n");
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
    u32 dumpLen = cfg.DumpLen;
    if (r2 > 0 && r2 < dumpLen) dumpLen = r2;

    FILE* out = cfg.LogFile ? cfg.LogFile : stdout;
    std::lock_guard<std::mutex> outputLock(NSMLTraceOutputMutex);
    fprintf(out,
        "%p,%u,%08X,%08X,%08X,%08X,%08X,%08X,%08X,",
        static_cast<void*>(&cpu->NDS),
        cpu->NDS.NumFrames,
        instrAddr,
        caller,
        lr,
        r0,
        r1,
        r2,
        r3);
    WriteNSMLHexDump(out, cpu, r0, dumpLen);
    fputc(',', out);
    WriteNSMLHexDump(out, cpu, r1, dumpLen);
    fputc(',', out);
    WriteNSMLHexDump(out, cpu, r2, dumpLen);
    fputc(',', out);
    WriteNSMLHexDump(out, cpu, r3, dumpLen);
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
    Log(LogLevel::Warn, "ARM9: data abort (%08X)\n", R[15]);

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
            TraceNSMLPacketCapture(this, instrAddr);
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
                TraceNSMLPacketCapture(this, instrAddr);
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
                TraceNSMLPacketCapture(this, instrAddr);
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
    NDS.ARM9Write8(addr, val);
}

void ARMv5::BusWrite16(u32 addr, u16 val)
{
    NDS.ARM9Write16(addr, val);
}

void ARMv5::BusWrite32(u32 addr, u32 val)
{
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

