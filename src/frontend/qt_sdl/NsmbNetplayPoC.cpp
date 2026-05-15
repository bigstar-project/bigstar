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
#include "Savestate.h"
#include "LocalMP.h"
#include "MPInterface.h"

namespace NsmbNetplayPoC
{

namespace
{

constexpr melonDS::u32 kMagic = 0x4C4D534E; // "NSML", little endian
constexpr melonDS::u32 kVersion = 1;
constexpr int kDefaultDelay = 6;
constexpr int kMaxPumpEvents = 64;
constexpr melonDS::u32 kNoFrameLimit = 0;

enum class Role
{
    Host,
    Client,
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

struct WireSeed
{
    melonDS::u32 Magic;
    melonDS::u32 Version;
    melonDS::u32 Kind;
    melonDS::u32 Seed;
};

constexpr melonDS::u32 kWireKindSeed = 0x44454553; // "SEED", little endian

static_assert(sizeof(WireSeed) == 16);

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
    melonDS::u32 TestFrames = kNoFrameLimit;
    int TestInstanceCount = 1;
    int HashInterval = 60;
    int TestWaitTimeoutMs = 5000;
    int TestQuitGraceMs = 0;
    bool InputTraceEnabled = false;
    int InputTraceInterval = 60;
    std::string InputScriptPath;
    std::string HashLogPath;
    std::string ScreenshotDir;
    std::string StateSaveDir;
    std::string StateLoadDir;
    std::string RamDumpDir;
    std::string MemPatchFile;
    std::ofstream HashLog;
    int ScreenshotInterval = 0;
    int RamDumpInterval = 0;
    int MemPatchInstance = -1;
    melonDS::u32 MemPatchFrame = 0;
    bool MemPatchFrameSet = false;
    bool MemPatchApplied[16] {};
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
    melonDS::u32 LastTracedSentInputFrame = kNoFrameLimit;
    melonDS::u32 LastTracedReceivedInputFrame = kNoFrameLimit;
    std::vector<std::pair<melonDS::u32, melonDS::u32>> RamDumpRanges;
    std::vector<std::pair<melonDS::u32, melonDS::u32>> MemPatchRanges;
    melonDS::u64 LastLoggedHashFrame[16] {};
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

struct FrameBarrier
{
    bool Waiting[16] {};
    melonDS::u32 Frame[16] {};
    int Generation = 0;
};

FrameBarrier GBeforeFrameBarrier;
FrameBarrier GAfterFrameBarrier;

bool EnvFlag(const char* name)
{
    const char* value = std::getenv(name);
    return value && value[0] && std::strcmp(value, "0") != 0;
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
    return {};
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

bool LoadInputScriptLocked()
{
    if (G.InputScriptPath.empty()) return true;

    std::ifstream file(G.InputScriptPath);
    if (!file)
    {
        std::printf("NSMB Test: failed to open input script: %s\n", G.InputScriptPath.c_str());
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
                    std::printf("NSMB Test: invalid input target at %s:%d\n", G.InputScriptPath.c_str(), lineNo);
                    return false;
                }
                span.Instance = static_cast<int>(targetInstance);
            }
        }

        const auto dash = range.find('-');
        if (dash == std::string::npos)
        {
            std::printf("NSMB Test: invalid range at %s:%d\n", G.InputScriptPath.c_str(), lineNo);
            return false;
        }

        if (!ParseU32(range.substr(0, dash), span.Start) ||
            !ParseU32(range.substr(dash + 1), span.End) ||
            span.End < span.Start ||
            !ParseInputSpec(buttons, span.Input))
        {
            std::printf("NSMB Test: invalid input line at %s:%d\n", G.InputScriptPath.c_str(), lineNo);
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
                std::printf("NSMB Test: invalid touch at %s:%d\n", G.InputScriptPath.c_str(), lineNo);
                return false;
            }
            span.Input.Touching = true;
            span.Input.TouchX = static_cast<melonDS::u16>(std::min<melonDS::u32>(x, 255));
            span.Input.TouchY = static_cast<melonDS::u16>(std::min<melonDS::u32>(y, 191));
        }

        GInputScript.push_back(span);
    }

    std::printf("NSMB Test: loaded %zu input spans from %s\n",
        GInputScript.size(),
        G.InputScriptPath.c_str());
    return true;
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

InputState ApplyInputScript(int instanceID, melonDS::u32 frame, const InputState& fallback)
{
    if (!G.TestEnabled || GInputScript.empty()) return fallback;

    for (const InputSpan& span : GInputScript)
    {
        if ((span.Instance < 0 || span.Instance == instanceID) &&
            frame >= span.Start && frame <= span.End)
            return span.Input;
    }

    return fallback;
}

void PumpNetworkLocked()
{
    if (!G.Host) return;

    ENetEvent event;
    for (int i = 0; i < kMaxPumpEvents; i++)
    {
        int result = enet_host_service(G.Host, &event, 0);
        if (result <= 0) break;

        switch (event.type)
        {
        case ENET_EVENT_TYPE_CONNECT:
            G.Peer = event.peer;
            G.MatchSeedSent = false;
            std::printf("NSMB PoC: peer connected\n");
            break;

        case ENET_EVENT_TYPE_RECEIVE:
            if (event.packet->dataLength == sizeof(WireInput))
            {
                WireInput packet;
                std::memcpy(&packet, event.packet->data, sizeof(packet));
                if (packet.Magic == kMagic && packet.Version == kVersion)
                {
                    G.RemoteInputs[packet.Frame] = {
                        packet.KeyMask,
                        packet.Touching != 0,
                        packet.TouchX,
                        packet.TouchY,
                    };
                    if (G.InputTraceEnabled
                        && packet.Frame != G.LastTracedReceivedInputFrame
                        && (G.InputTraceInterval <= 1 || (packet.Frame % static_cast<melonDS::u32>(G.InputTraceInterval)) == 0))
                    {
                        G.LastTracedReceivedInputFrame = packet.Frame;
                        std::printf("NSMB PoC: recv input frame=%u keys=0x%03X remoteQueue=%zu\n",
                            packet.Frame,
                            packet.KeyMask,
                            G.RemoteInputs.size());
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
                    G.NetRandomPatchValue = packet.Seed;
                    G.NetRandomPatchEnabled = true;
                    G.NetRandomPatchAuto = true;
                    std::printf("NSMB PoC: received match seed 0x%08X\n", packet.Seed);
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

void SendInputLocked(melonDS::u32 frame, const InputState& input)
{
    if (!G.Peer) return;

    SendMatchSeedLocked();

    WireInput packet {};
    packet.Magic = kMagic;
    packet.Version = kVersion;
    packet.Frame = frame;
    packet.KeyMask = input.KeyMask;
    packet.TouchX = input.TouchX;
    packet.TouchY = input.TouchY;
    packet.Touching = input.Touching ? 1 : 0;

    ENetPacket* enetPacket = enet_packet_create(&packet, sizeof(packet), ENET_PACKET_FLAG_RELIABLE);
    if (!enetPacket) return;

    enet_peer_send(G.Peer, 0, enetPacket);
    enet_host_flush(G.Host);

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

InputState WaitForRemoteInput(melonDS::u32 targetFrame)
{
    const auto start = std::chrono::steady_clock::now();
    for (;;)
    {
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            PumpNetworkLocked();

            auto it = G.RemoteInputs.find(targetFrame);
            if (it != G.RemoteInputs.end())
                return it->second;
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
                return NeutralInput();
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
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
    constexpr melonDS::u32 kNetRandomValueOffset = 0x02088088 - 0x02000000;
    constexpr melonDS::u32 kNetRandomCallCountOffset = 0x02088068 - 0x02000000;
    constexpr melonDS::u32 kNetGGIDOffset = 0x02087E78 - 0x02000000;
    constexpr melonDS::u32 kGameStageGroupOffset = 0x02085058 - 0x02000000;
    constexpr melonDS::u32 kGameVsModeOffset = 0x020850C4 - 0x02000000;

    if (!nds || !nds->MainRAM || !G.NetRandomPatchEnabled) return;
    if (instanceID < 0 || instanceID >= 16) return;
    if (G.NetRandomPatchApplied[instanceID]) return;
    if (kNetRandomValueOffset + sizeof(melonDS::u32) > nds->MainRAMMask + 1) return;

    bool shouldPatch = frame == G.NetRandomPatchFrame;
    if (G.NetRandomPatchAuto)
    {
        const melonDS::u32 stageGroup = nds->ARM9Read32(0x02000000 + kGameStageGroupOffset);
        const melonDS::u32 vsMode = nds->ARM9Read32(0x02000000 + kGameVsModeOffset);
        const melonDS::u32 ggid = nds->ARM9Read32(0x02000000 + kNetGGIDOffset);
        const melonDS::u8 randomCallCount = nds->ARM9Read8(0x02000000 + kNetRandomCallCountOffset);
        shouldPatch = stageGroup == 9 && vsMode == 1 && ggid == 0x42 && randomCallCount == 0;
    }
    if (!shouldPatch) return;

    std::memcpy(&nds->MainRAM[kNetRandomValueOffset], &G.NetRandomPatchValue, sizeof(G.NetRandomPatchValue));
    G.NetRandomPatchApplied[instanceID] = true;

    std::printf("NSMB Test: patched Net::random.value inst=%d frame=%u value=0x%08X auto=%d\n",
        instanceID,
        frame,
        G.NetRandomPatchValue,
        G.NetRandomPatchAuto ? 1 : 0);
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

    G.StateSaved[instanceID] = true;
    std::printf("NSMB Test: saved state inst=%d frame=%u path=%ls bytes=%u\n",
        instanceID,
        frame,
        path.c_str(),
        state.Length());
    return true;
}

bool WaitForStateSaveBarrier(int instanceID)
{
    if (G.TestInstanceCount <= 1) return true;

    const auto start = std::chrono::steady_clock::now();
    for (;;)
    {
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            bool allSaved = true;
            for (int i = 0; i < G.TestInstanceCount; i++)
            {
                if (!G.StateSaved[i])
                {
                    allSaved = false;
                    break;
                }
            }
            if (allSaved) return true;
        }

        if (G.TestWaitTimeoutMs > 0)
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= G.TestWaitTimeoutMs)
            {
                std::printf("NSMB Test: state save barrier timeout inst=%d waitedMs=%d\n",
                    instanceID,
                    G.TestWaitTimeoutMs);
                return false;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool SaveLocalMPState(melonDS::u32 frame)
{
    std::string stateSaveDir;
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        if (G.StateSaveDir.empty() || G.StateSaveFrame == 0) return false;
        if (frame != G.StateSaveFrame || G.LocalMPSaved) return false;
        for (int i = 0; i < G.TestInstanceCount; i++)
        {
            if (!G.StateSaved[i])
                return false;
        }
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

bool WaitForLocalMPSaveBarrier(int instanceID)
{
    const auto start = std::chrono::steady_clock::now();
    for (;;)
    {
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            if (G.LocalMPSaved) return true;
        }

        if (G.TestWaitTimeoutMs > 0)
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= G.TestWaitTimeoutMs)
            {
                std::printf("NSMB Test: LocalMP save barrier timeout inst=%d waitedMs=%d\n",
                    instanceID,
                    G.TestWaitTimeoutMs);
                return false;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
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
    G.FrameBarrierEnabled = EnvFlag("MELONDS_NSML_FRAME_BARRIER");
    G.SerialRunEnabled = EnvFlag("MELONDS_NSML_SERIAL_RUN");
    G.HashInterval = std::max(1, EnvInt("MELONDS_NSML_HASH_INTERVAL", 60));
    G.TestWaitTimeoutMs = std::max(0, EnvInt("MELONDS_NSML_WAIT_TIMEOUT_MS", 5000));
    G.TestQuitGraceMs = std::max(0, EnvInt("MELONDS_NSML_QUIT_GRACE_MS", 0));
    G.InputTraceEnabled = EnvFlag("MELONDS_NSML_INPUT_TRACE");
    G.InputTraceInterval = std::max(1, EnvInt("MELONDS_NSML_INPUT_TRACE_INTERVAL", 60));

    const char* inputScript = std::getenv("MELONDS_NSML_INPUT_SCRIPT");
    if (inputScript && inputScript[0]) G.InputScriptPath = inputScript;

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

    const char* netRandomValue = std::getenv("MELONDS_NSML_NET_RANDOM_VALUE");
    if (netRandomValue && netRandomValue[0])
    {
        G.NetRandomPatchEnabled = true;
        G.NetRandomPatchAuto = EnvFlag("MELONDS_NSML_NET_RANDOM_AUTO");
        G.NetRandomPatchValue = static_cast<melonDS::u32>(std::strtoul(netRandomValue, nullptr, 0));
        G.NetRandomPatchFrame = static_cast<melonDS::u32>(
            std::max(0, EnvInt("MELONDS_NSML_NET_RANDOM_FRAME", 0)));
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

    if (G.TestEnabled)
    {
        if (!LoadInputScriptLocked())
            G.TestEnabled = false;

        if (!G.HashLogPath.empty())
        {
            G.HashLog.open(G.HashLogPath, std::ios::out | std::ios::trunc);
            if (!G.HashLog)
            {
                std::printf("NSMB Test: failed to open hash log: %s\n", G.HashLogPath.c_str());
            }
            else
            {
                G.HashLog << "instance,frame,hash\n";
            }
        }

        std::printf("NSMB Test: enabled frames=%u instances=%d frameBarrier=%d serialRun=%d input=%s hashLog=%s interval=%d screenshotDir=%s screenshotInterval=%d ramDumpDir=%s ramDumpInterval=%d ramDumpRanges=%zu memPatchFile=%s memPatchFrame=%u memPatchRanges=%zu netRandomEnabled=%d netRandomAuto=%d netRandomFrame=%u netRandomValue=0x%08X stateSaveDir=%s stateSaveFrame=%u stateLoadDir=%s stateLoadFrame=%u waitTimeoutMs=%d quitGraceMs=%d inputTrace=%d inputTraceInterval=%d\n",
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
            G.InputTraceInterval);
    }

    if (!G.Enabled) return;

    const char* role = std::getenv("MELONDS_NSML_ROLE");
    G.NetRole = (role && std::strcmp(role, "client") == 0) ? Role::Client : Role::Host;
    G.Delay = std::max(0, EnvInt("MELONDS_NSML_DELAY", kDefaultDelay));
    G.NetplayWarmupFrames = std::max(0, EnvInt("MELONDS_NSML_NETPLAY_WARMUP_FRAMES", G.TestEnabled ? G.Delay * 2 : 0));
    G.Port = EnvInt("MELONDS_NSML_PORT", 8065);
    G.LocalInstance = EnvInt("MELONDS_NSML_LOCAL_INSTANCE", G.NetRole == Role::Host ? 0 : 1);
    G.NetplayStartFrame = static_cast<melonDS::u32>(std::max(0, EnvInt("MELONDS_NSML_NETPLAY_START_FRAME", 0)));
    G.LocalWaitsForRemote = !EnvFlag("MELONDS_NSML_NO_LOCAL_WAIT");

    const char* peer = std::getenv("MELONDS_NSML_PEER");
    if (peer && peer[0]) G.PeerHost = peer;

    if (G.NetRole == Role::Host && !G.MatchSeedConfigured)
    {
        G.MatchSeed = GenerateMatchSeed();
        G.MatchSeedConfigured = true;
    }

    if (G.NetRole == Role::Host && G.MatchSeedConfigured)
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
    std::printf("NSMB PoC: enabled role=%s port=%d peer=%s delay=%d warmup=%d localInstance=%d netplayStartFrame=%u localWait=%d matchSeed=0x%08X seedConfigured=%d\n",
        G.NetRole == Role::Host ? "host" : "client",
        G.Port,
        G.PeerHost,
        G.Delay,
        G.NetplayWarmupFrames,
        G.LocalInstance,
        G.NetplayStartFrame,
        G.LocalWaitsForRemote ? 1 : 0,
        G.MatchSeed,
        G.MatchSeedConfigured ? 1 : 0);
}

InputState BeforeRunFrame(int instanceID, melonDS::u32 frame, melonDS::NDS* nds, const InputState& polledInput)
{
    InitFromEnvironment();
    melonDS::u32 inputFrame = frame;
    if (G.TestEnabled && instanceID >= 0 && instanceID < 16)
        inputFrame = G.TestFrameCount[instanceID];

    if (G.TestEnabled && instanceID >= 0 && instanceID < 16 && nds)
        LoadState(instanceID, inputFrame, nds);

    if (G.TestEnabled && instanceID >= 0 && instanceID < 16 && nds)
        ApplyMemPatch(instanceID, inputFrame, nds);

    if ((G.TestEnabled || G.Enabled) && instanceID >= 0 && instanceID < 16 && nds)
        ApplyNetRandomPatch(instanceID, inputFrame, nds);

    WaitForSerialRunTurn(instanceID, inputFrame);
    WaitAtFrameBarrier(GBeforeFrameBarrier, instanceID, inputFrame, "before");

    const InputState testInput = ApplyInputScript(instanceID, inputFrame, polledInput);
    const melonDS::u32 syncFrame = G.TestEnabled ? inputFrame : frame;

    if (!G.Enabled || !G.Ready) return testInput;

    const bool isLocal = (instanceID == G.LocalInstance);
    const melonDS::u32 delay = static_cast<melonDS::u32>(G.Delay);
    const melonDS::u32 sendStartFrame = (G.NetplayStartFrame > delay)
        ? G.NetplayStartFrame - delay
        : 0;
    const bool netplaySendActive = (G.NetplayStartFrame == 0 || syncFrame >= sendStartFrame);
    const bool netplayApplyActive = (G.NetplayStartFrame == 0 || syncFrame >= G.NetplayStartFrame);

    if (isLocal || G.TestEnabled)
    {
        InputState localInput = testInput;
        if (!isLocal && G.TestEnabled)
            localInput = ApplyInputScript(G.LocalInstance, syncFrame, NeutralInput());

        std::lock_guard<std::mutex> lock(G.Mutex);
        PumpNetworkLocked();
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

    if (G.TestEnabled && instanceID >= 0 && instanceID < 16 && !G.NetplayLockstepStarted[instanceID])
    {
        bool needsInitialRemoteInput = false;
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            PumpNetworkLocked();
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

void AfterRunFrame(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    InitFromEnvironment();
    if ((!G.Enabled && !G.TestEnabled) || !nds) return;

    if (instanceID < 0 || instanceID >= 16) return;

    melonDS::u32 logFrame = frame;
    if (G.TestEnabled)
        logFrame = ++G.TestFrameCount[instanceID];

    WaitAtFrameBarrier(GAfterFrameBarrier, instanceID, logFrame, "after");
    AdvanceSerialRunTurn(instanceID, logFrame - 1);

    if (SaveState(instanceID, logFrame, nds))
    {
        WaitForStateSaveBarrier(instanceID);
        SaveLocalMPState(logFrame);
        WaitForLocalMPSaveBarrier(instanceID);
    }
    SaveScreenshot(instanceID, logFrame, nds);
    SaveRamDump(instanceID, logFrame, nds);

    if ((logFrame % static_cast<melonDS::u32>(G.HashInterval)) != 0) return;

    const melonDS::u64 hash = HashNDS(nds);
    if (G.LastLoggedHashFrame[instanceID] == logFrame) return;
    G.LastLoggedHashFrame[instanceID] = logFrame;

    std::printf("NSMB PoC: inst=%d frame=%u hash=%016llX\n",
        instanceID,
        logFrame,
        static_cast<unsigned long long>(hash));

    std::lock_guard<std::mutex> lock(G.Mutex);
    if (G.HashLog)
    {
        G.HashLog << instanceID << ',' << logFrame << ','
                  << std::hex << hash << std::dec << '\n';
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
        std::printf("NSMB Test: frame limit reached at frame=%u instances=%d\n",
            G.TestFrames,
            G.TestInstanceCount);
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
}

}
