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

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#include "LocalMP.h"

using namespace melonDS;
using namespace melonDS::Platform;

using Platform::Log;
using Platform::LogLevel;

namespace melonDS
{

namespace
{

constexpr u32 kLocalMPTestStateMagic = 0x4C4D5053; // "SPML", little endian
constexpr u32 kLocalMPTestStateVersion = 1;

struct LocalMPTestState
{
    u32 Magic;
    u32 Version;
    MPStatusData Status;
    u32 PacketReadOffset[16];
    u32 ReplyReadOffset[16];
    s32 LastHostID;
    u8 MPPacketQueue[kPacketQueueSize];
    u8 MPReplyQueue[kReplyQueueSize];
};

struct LocalMPTestConfig
{
    bool Checked = false;
    bool StrictWait = false;
    bool LogTimeouts = false;
    bool FixedTimestamp = false;
    bool NormalizeAckTiming = false;
    u64 TimestampValue = 0;
    int StrictWaitTimeoutMs = 5000;
    bool TraceEnabled = false;
    std::string TracePath;
    std::ofstream Trace;
    std::mutex TraceMutex;
    u64 TraceSeq = 0;
};

LocalMPTestConfig& TestConfig()
{
    static LocalMPTestConfig cfg;
    if (!cfg.Checked)
    {
        cfg.Checked = true;
        cfg.StrictWait = getenv("MELONDS_NSML_LOCALMP_STRICT_WAIT") != nullptr;
        cfg.LogTimeouts = getenv("MELONDS_NSML_LOCALMP_LOG_TIMEOUTS") != nullptr;
        cfg.NormalizeAckTiming = getenv("MELONDS_NSML_LOCALMP_NORMALIZE_ACK_TIMING") != nullptr;
        if (const char* fixedTimestamp = getenv("MELONDS_NSML_LOCALMP_FIXED_TIMESTAMP"))
        {
            cfg.TimestampValue = strtoull(fixedTimestamp, nullptr, 0);
            cfg.FixedTimestamp = cfg.TimestampValue != 0;
        }
        if (const char* timeout = getenv("MELONDS_NSML_LOCALMP_STRICT_WAIT_MS"))
            cfg.StrictWaitTimeoutMs = atoi(timeout);
        if (cfg.StrictWaitTimeoutMs < 0)
            cfg.StrictWaitTimeoutMs = 0;
        if (const char* tracePath = getenv("MELONDS_NSML_LOCALMP_TRACE"))
        {
            cfg.TracePath = tracePath;
            cfg.Trace.open(cfg.TracePath, std::ios::out | std::ios::trunc);
            cfg.TraceEnabled = cfg.Trace.is_open();
            if (cfg.TraceEnabled)
                cfg.Trace << "seq,event,inst,type,len,timestamp,ret,aidmask,dataHash,packetRead,replyRead,packetWrite,replyWrite,connected,lastHost\n";
        }
    }
    return cfg;
}

void TraceLocalMP(LocalMPTestConfig& cfg, const char* event, int inst, u32 type, int len,
                  u64 timestamp, u16 ret, u16 aidmask, u64 dataHash, const MPStatusData& status,
                  const u32* packetReadOffset, const u32* replyReadOffset, int lastHost)
{
    if (!cfg.TraceEnabled)
        return;

    std::lock_guard<std::mutex> lock(cfg.TraceMutex);
    cfg.Trace << cfg.TraceSeq++ << ','
              << event << ','
              << inst << ','
              << type << ','
              << len << ','
              << timestamp << ','
              << ret << ','
              << aidmask << ','
              << std::hex << dataHash << std::dec << ','
              << packetReadOffset[inst] << ','
              << replyReadOffset[inst] << ','
              << status.PacketWriteOffset << ','
              << status.ReplyWriteOffset << ','
              << status.ConnectedBitmask << ','
              << lastHost << '\n';
    cfg.Trace.flush();
}

u64 HashBytes(const u8* data, int len)
{
    u64 hash = 1469598103934665603ull;
    for (int i = 0; i < len; i++)
    {
        hash ^= data[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

bool WaitSemaphoreForTest(Platform::Semaphore* sem, int timeoutMs, bool strictWait)
{
    if (!strictWait)
        return Semaphore_TryWait(sem, timeoutMs);

    const auto start = std::chrono::steady_clock::now();
    for (;;)
    {
        if (Semaphore_TryWait(sem, 0))
            return true;

        if (timeoutMs > 0)
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= timeoutMs)
                return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

}

LocalMP::LocalMP() noexcept :
    MPQueueLock(Mutex_Create())
{
    memset(MPPacketQueue, 0, kPacketQueueSize);
    memset(MPReplyQueue, 0, kReplyQueueSize);
    memset(&MPStatus, 0, sizeof(MPStatus));
    memset(PacketReadOffset, 0, sizeof(PacketReadOffset));
    memset(ReplyReadOffset, 0, sizeof(ReplyReadOffset));

    // prepare semaphores
    // semaphores 0-15: regular frames; semaphore I is posted when instance I needs to process a new frame
    // semaphores 16-31: MP replies; semaphore I is posted when instance I needs to process a new MP reply

    for (int i = 0; i < 32; i++)
    {
        SemPool[i] = Semaphore_Create();
    }

    Log(LogLevel::Info, "MP comm init OK\n");
}

LocalMP::~LocalMP() noexcept
{
    for (int i = 0; i < 32; i++)
    {
        Semaphore_Free(SemPool[i]);
        SemPool[i] = nullptr;
    }

    Mutex_Free(MPQueueLock);
}

void LocalMP::Begin(int inst)
{
    Mutex_Lock(MPQueueLock);
    PacketReadOffset[inst] = MPStatus.PacketWriteOffset;
    ReplyReadOffset[inst] = MPStatus.ReplyWriteOffset;
    Semaphore_Reset(SemPool[inst]);
    Semaphore_Reset(SemPool[16 + inst]);
    MPStatus.ConnectedBitmask |= (1 << inst);
    Mutex_Unlock(MPQueueLock);
}

void LocalMP::End(int inst)
{
    Mutex_Lock(MPQueueLock);
    MPStatus.ConnectedBitmask &= ~(1 << inst);
    Mutex_Unlock(MPQueueLock);
}

void LocalMP::FIFORead(int inst, int fifo, void* buf, int len) noexcept
{
    u8* data;

    u32 offset, datalen;
    if (fifo == 0)
    {
        offset = PacketReadOffset[inst];
        data = MPPacketQueue;
        datalen = kPacketQueueSize;
    }
    else
    {
        offset = ReplyReadOffset[inst];
        data = MPReplyQueue;
        datalen = kReplyQueueSize;
    }

    if ((offset + len) >= datalen)
    {
        u32 part1 = datalen - offset;
        memcpy(buf, &data[offset], part1);
        memcpy(&((u8*)buf)[part1], data, len - part1);
        offset = len - part1;
    }
    else
    {
        memcpy(buf, &data[offset], len);
        offset += len;
    }

    if (fifo == 0) PacketReadOffset[inst] = offset;
    else           ReplyReadOffset[inst] = offset;
}

void LocalMP::FIFOWrite(int inst, int fifo, void* buf, int len) noexcept
{
    u8* data;

    u32 offset, datalen;
    if (fifo == 0)
    {
        offset = MPStatus.PacketWriteOffset;
        data = MPPacketQueue;
        datalen = kPacketQueueSize;
    }
    else
    {
        offset = MPStatus.ReplyWriteOffset;
        data = MPReplyQueue;
        datalen = kReplyQueueSize;
    }

    if ((offset + len) >= datalen)
    {
        u32 part1 = datalen - offset;
        memcpy(&data[offset], buf, part1);
        memcpy(data, &((u8*)buf)[part1], len - part1);
        offset = len - part1;
    }
    else
    {
        memcpy(&data[offset], buf, len);
        offset += len;
    }

    if (fifo == 0) MPStatus.PacketWriteOffset = offset;
    else           MPStatus.ReplyWriteOffset = offset;
}

int LocalMP::SendPacketGeneric(int inst, u32 type, u8* packet, int len, u64 timestamp) noexcept
{
    LocalMPTestConfig& cfg = TestConfig();
    if (len > kMaxFrameSize)
    {
        Log(LogLevel::Warn, "wifi: attempting to send frame too big (len=%d max=%d)\n", len, kMaxFrameSize);
        return 0;
    }

    Mutex_Lock(MPQueueLock);

    u16 mask = MPStatus.ConnectedBitmask;

    // TODO: check if the FIFO is full!

    type &= 0xFFFF;
    if (cfg.NormalizeAckTiming && type == 3 && len >= 4)
    {
        *(u32*)&packet[0] = 0;
        if (len >= 0xC + 0x1C)
        {
            *(u16*)&packet[0xC + 0x16] = 0;
            *(u16*)&packet[0xC + 0x18] = 0;
            *(u16*)&packet[0xC + 0x1A] = 0;
        }
    }

    MPPacketHeader pktheader;
    pktheader.Magic = 0x4946494E;
    pktheader.SenderID = inst;
    pktheader.Type = type;
    pktheader.Length = len;
    pktheader.Timestamp = cfg.FixedTimestamp ? cfg.TimestampValue : timestamp;

    int nfifo = (type == 2) ? 1 : 0;
    FIFOWrite(inst, nfifo, &pktheader, sizeof(pktheader));
    if (len)
        FIFOWrite(inst, nfifo, packet, len);

    if (type == 1)
    {
        // NOTE: this is not guarded against, say, multiple multiplay games happening on the same machine
        // we would need to pass the packet's SenderID through the wifi module for that
        MPStatus.MPHostinst = inst;
        MPStatus.MPReplyBitmask = 0;
        ReplyReadOffset[inst] = MPStatus.ReplyWriteOffset;
        Semaphore_Reset(SemPool[16 + inst]);
    }
    else if (type == 2)
    {
        MPStatus.MPReplyBitmask |= (1 << inst);
    }

    TraceLocalMP(cfg, "send", inst, type, len, pktheader.Timestamp, MPStatus.MPReplyBitmask, 0,
        HashBytes(packet, len),
        MPStatus, PacketReadOffset, ReplyReadOffset, LastHostID);

    Mutex_Unlock(MPQueueLock);

    if (type == 2)
    {
        Semaphore_Post(SemPool[16 +  MPStatus.MPHostinst]);
    }
    else
    {
        for (int i = 0; i < 16; i++)
        {
            if (mask & (1<<i))
                Semaphore_Post(SemPool[i]);
        }
    }

    return len;
}

int LocalMP::RecvPacketGeneric(int inst, u8* packet, bool block, u64* timestamp) noexcept
{
    LocalMPTestConfig& cfg = TestConfig();
    for (;;)
    {
        const int timeout = block ? (cfg.StrictWait ? cfg.StrictWaitTimeoutMs : RecvTimeout) : 0;
        if (!WaitSemaphoreForTest(SemPool[inst], timeout, block && cfg.StrictWait))
        {
            if (cfg.LogTimeouts && block)
                Log(LogLevel::Debug, "LocalMP timeout: recv host inst=%d timeout=%d strict=%d\n",
                    inst, timeout, cfg.StrictWait ? 1 : 0);
            return 0;
        }

        Mutex_Lock(MPQueueLock);

        MPPacketHeader pktheader = {};
        FIFORead(inst, 0, &pktheader, sizeof(pktheader));

        if (pktheader.Magic != 0x4946494E)
        {
            Log(LogLevel::Warn, "PACKET FIFO OVERFLOW\n");
            PacketReadOffset[inst] = MPStatus.PacketWriteOffset;
            Semaphore_Reset(SemPool[inst]);
            Mutex_Unlock(MPQueueLock);
            return 0;
        }

        if (pktheader.SenderID == inst)
        {
            // skip this packet
            PacketReadOffset[inst] += pktheader.Length;
            if (PacketReadOffset[inst] >= kPacketQueueSize)
                PacketReadOffset[inst] -= kPacketQueueSize;

            TraceLocalMP(cfg, "recv-skip-self", inst, pktheader.Type, pktheader.Length,
                pktheader.Timestamp, 0, 0, 0, MPStatus, PacketReadOffset, ReplyReadOffset, LastHostID);

            Mutex_Unlock(MPQueueLock);
            continue;
        }

        if (pktheader.Length)
        {
            FIFORead(inst, 0, packet, pktheader.Length);

            if (pktheader.Type == 1)
                LastHostID = pktheader.SenderID;
        }

        if (timestamp) *timestamp = pktheader.Timestamp;
        TraceLocalMP(cfg, "recv", inst, pktheader.Type, pktheader.Length, pktheader.Timestamp,
            0, 0, pktheader.Length ? HashBytes(packet, pktheader.Length) : 0,
            MPStatus, PacketReadOffset, ReplyReadOffset, LastHostID);
        Mutex_Unlock(MPQueueLock);
        return pktheader.Length;
    }
}

int LocalMP::SendPacket(int inst, u8* packet, int len, u64 timestamp)
{
    return SendPacketGeneric(inst, 0, packet, len, timestamp);
}

int LocalMP::RecvPacket(int inst, u8* packet, u64* timestamp)
{
    return RecvPacketGeneric(inst, packet, false, timestamp);
}

int LocalMP::SendCmd(int inst, u8* packet, int len, u64 timestamp)
{
    return SendPacketGeneric(inst, 1, packet, len, timestamp);
}

int LocalMP::SendReply(int inst, u8* packet, int len, u64 timestamp, u16 aid)
{
    return SendPacketGeneric(inst, 2 | (aid<<16), packet, len, timestamp);
}

int LocalMP::SendAck(int inst, u8* packet, int len, u64 timestamp)
{
    return SendPacketGeneric(inst, 3, packet, len, timestamp);
}

int LocalMP::RecvHostPacket(int inst, u8* packet, u64* timestamp)
{
    if (LastHostID != -1)
    {
        // check if the host is still connected

        u16 curinstmask = MPStatus.ConnectedBitmask;

        if (!(curinstmask & (1 << LastHostID)))
            return -1;
    }

    return RecvPacketGeneric(inst, packet, true, timestamp);
}

u16 LocalMP::RecvReplies(int inst, u8* packets, u64 timestamp, u16 aidmask)
{
    LocalMPTestConfig& cfg = TestConfig();
    u16 ret = 0;
    u16 myinstmask = (1 << inst);
    u16 curinstmask;

    curinstmask = MPStatus.ConnectedBitmask;

    // if all clients have left: return early
    if ((myinstmask & curinstmask) == curinstmask)
        return 0;

    for (;;)
    {
        const int timeout = cfg.StrictWait ? cfg.StrictWaitTimeoutMs : RecvTimeout;
        if (!WaitSemaphoreForTest(SemPool[16+inst], timeout, cfg.StrictWait))
        {
            // no more replies available
            if (cfg.LogTimeouts)
                Log(LogLevel::Debug, "LocalMP timeout: recv replies inst=%d timestamp=%016llX aidmask=%04X ret=%04X timeout=%d strict=%d\n",
                    inst,
                    timestamp,
                    aidmask,
                    ret,
                    timeout,
                    cfg.StrictWait ? 1 : 0);
            return ret;
        }

        Mutex_Lock(MPQueueLock);

        MPPacketHeader pktheader = {};
        FIFORead(inst, 1, &pktheader, sizeof(pktheader));

        if (pktheader.Magic != 0x4946494E)
        {
            Log(LogLevel::Warn, "REPLY FIFO OVERFLOW\n");
            ReplyReadOffset[inst] = MPStatus.ReplyWriteOffset;
            Semaphore_Reset(SemPool[16 + inst]);
            Mutex_Unlock(MPQueueLock);
            return 0;
        }

        if ((pktheader.SenderID == inst) || // packet we sent out (shouldn't happen, but hey)
            (pktheader.Timestamp < (timestamp - 32))) // stale packet
        {
            // skip this packet
            ReplyReadOffset[inst] += pktheader.Length;
            if (ReplyReadOffset[inst] >= kReplyQueueSize)
                ReplyReadOffset[inst] -= kReplyQueueSize;

            TraceLocalMP(cfg, "reply-skip", inst, pktheader.Type, pktheader.Length,
                pktheader.Timestamp, ret, aidmask, 0, MPStatus, PacketReadOffset, ReplyReadOffset, LastHostID);

            Mutex_Unlock(MPQueueLock);
            continue;
        }

        if (pktheader.Length)
        {
            u32 aid = (pktheader.Type >> 16);
            FIFORead(inst, 1, &packets[(aid-1)*1024], pktheader.Length);
            ret |= (1 << aid);
        }

        myinstmask |= (1 << pktheader.SenderID);
        if (((myinstmask & curinstmask) == curinstmask) ||
            ((ret & aidmask) == aidmask))
        {
            // all the clients have sent their reply

            TraceLocalMP(cfg, "replies", inst, pktheader.Type, pktheader.Length,
                pktheader.Timestamp, ret, aidmask,
                pktheader.Length ? HashBytes(&packets[((pktheader.Type >> 16)-1)*1024], pktheader.Length) : 0,
                MPStatus, PacketReadOffset, ReplyReadOffset, LastHostID);

            Mutex_Unlock(MPQueueLock);
            return ret;
        }

        TraceLocalMP(cfg, "reply-partial", inst, pktheader.Type, pktheader.Length,
            pktheader.Timestamp, ret, aidmask,
            pktheader.Length ? HashBytes(&packets[((pktheader.Type >> 16)-1)*1024], pktheader.Length) : 0,
            MPStatus, PacketReadOffset, ReplyReadOffset, LastHostID);

        Mutex_Unlock(MPQueueLock);
    }
}

bool LocalMP::SnapshotForTest(std::vector<u8>& out) noexcept
{
    out.resize(sizeof(LocalMPTestState));
    auto* state = reinterpret_cast<LocalMPTestState*>(out.data());

    Mutex_Lock(MPQueueLock);
    state->Magic = kLocalMPTestStateMagic;
    state->Version = kLocalMPTestStateVersion;
    state->Status = MPStatus;
    memcpy(state->PacketReadOffset, PacketReadOffset, sizeof(PacketReadOffset));
    memcpy(state->ReplyReadOffset, ReplyReadOffset, sizeof(ReplyReadOffset));
    state->LastHostID = LastHostID;
    memcpy(state->MPPacketQueue, MPPacketQueue, sizeof(MPPacketQueue));
    memcpy(state->MPReplyQueue, MPReplyQueue, sizeof(MPReplyQueue));
    Mutex_Unlock(MPQueueLock);

    return true;
}

bool LocalMP::RestoreForTest(const u8* data, std::size_t len) noexcept
{
    if (!data || len != sizeof(LocalMPTestState))
        return false;

    const auto* state = reinterpret_cast<const LocalMPTestState*>(data);
    if (state->Magic != kLocalMPTestStateMagic ||
        state->Version != kLocalMPTestStateVersion)
        return false;

    Mutex_Lock(MPQueueLock);
    MPStatus = state->Status;
    memcpy(PacketReadOffset, state->PacketReadOffset, sizeof(PacketReadOffset));
    memcpy(ReplyReadOffset, state->ReplyReadOffset, sizeof(ReplyReadOffset));
    LastHostID = state->LastHostID;
    memcpy(MPPacketQueue, state->MPPacketQueue, sizeof(MPPacketQueue));
    memcpy(MPReplyQueue, state->MPReplyQueue, sizeof(MPReplyQueue));
    Mutex_Unlock(MPQueueLock);

    for (int i = 0; i < 32; i++)
        Semaphore_Reset(SemPool[i]);

    for (int i = 0; i < 16; i++)
    {
        if ((MPStatus.ConnectedBitmask & (1 << i)) &&
            PacketReadOffset[i] != MPStatus.PacketWriteOffset)
            Semaphore_Post(SemPool[i]);
    }

    if (MPStatus.MPHostinst < 16 &&
        ReplyReadOffset[MPStatus.MPHostinst] != MPStatus.ReplyWriteOffset)
        Semaphore_Post(SemPool[16 + MPStatus.MPHostinst]);

    return true;
}

}

