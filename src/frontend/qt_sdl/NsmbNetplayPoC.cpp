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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>

#include <enet/enet.h>

#include "NDS.h"

namespace NsmbNetplayPoC
{

namespace
{

constexpr melonDS::u32 kMagic = 0x4C4D534E; // "NSML", little endian
constexpr melonDS::u32 kVersion = 1;
constexpr int kDefaultDelay = 6;
constexpr int kMaxPumpEvents = 64;

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

struct State
{
    std::mutex Mutex;
    bool EnvChecked = false;
    bool Enabled = false;
    bool Ready = false;
    Role NetRole = Role::Host;
    int Delay = kDefaultDelay;
    int LocalInstance = 0;
    int Port = 8065;
    const char* PeerHost = "127.0.0.1";
    ENetHost* Host = nullptr;
    ENetPeer* Peer = nullptr;
    bool ENetInitialized = false;
    std::map<melonDS::u32, InputState> LocalInputs;
    std::map<melonDS::u32, InputState> RemoteInputs;
    melonDS::u64 LastLoggedHashFrame[16] {};
};

State G;

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

void SendInputLocked(melonDS::u32 frame, const InputState& input)
{
    if (!G.Peer) return;

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
}

InputState NeutralInput()
{
    return {};
}

void PruneInputHistoryLocked(melonDS::u32 keepFromFrame)
{
    G.LocalInputs.erase(G.LocalInputs.begin(), G.LocalInputs.lower_bound(keepFromFrame));
    G.RemoteInputs.erase(G.RemoteInputs.begin(), G.RemoteInputs.lower_bound(keepFromFrame));
}

InputState WaitForRemoteInput(melonDS::u32 targetFrame)
{
    for (;;)
    {
        {
            std::lock_guard<std::mutex> lock(G.Mutex);
            PumpNetworkLocked();

            auto it = G.RemoteInputs.find(targetFrame);
            if (it != G.RemoteInputs.end())
                return it->second;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
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
    if (!G.Enabled) return;

    const char* role = std::getenv("MELONDS_NSML_ROLE");
    G.NetRole = (role && std::strcmp(role, "client") == 0) ? Role::Client : Role::Host;
    G.Delay = std::max(0, EnvInt("MELONDS_NSML_DELAY", kDefaultDelay));
    G.Port = EnvInt("MELONDS_NSML_PORT", 8065);
    G.LocalInstance = EnvInt("MELONDS_NSML_LOCAL_INSTANCE", G.NetRole == Role::Host ? 0 : 1);

    const char* peer = std::getenv("MELONDS_NSML_PEER");
    if (peer && peer[0]) G.PeerHost = peer;

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
    std::printf("NSMB PoC: enabled role=%s port=%d peer=%s delay=%d localInstance=%d\n",
        G.NetRole == Role::Host ? "host" : "client",
        G.Port,
        G.PeerHost,
        G.Delay,
        G.LocalInstance);
}

InputState BeforeRunFrame(int instanceID, melonDS::u32 frame, const InputState& polledInput)
{
    InitFromEnvironment();
    if (!G.Enabled || !G.Ready) return polledInput;

    const bool isLocal = (instanceID == G.LocalInstance);

    if (isLocal)
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        PumpNetworkLocked();
        G.LocalInputs.emplace(frame, polledInput);
        SendInputLocked(frame, polledInput);
    }

    const melonDS::u32 targetFrame = frame >= static_cast<melonDS::u32>(G.Delay)
        ? frame - static_cast<melonDS::u32>(G.Delay)
        : 0;

    if (frame < static_cast<melonDS::u32>(G.Delay))
        return NeutralInput();

    const InputState remoteInput = WaitForRemoteInput(targetFrame);

    if (isLocal)
    {
        std::lock_guard<std::mutex> lock(G.Mutex);
        if (targetFrame > 120)
            PruneInputHistoryLocked(targetFrame - 120);

        auto it = G.LocalInputs.find(targetFrame);
        return it != G.LocalInputs.end() ? it->second : NeutralInput();
    }

    return remoteInput;
}

void AfterRunFrame(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    InitFromEnvironment();
    if (!G.Enabled || !nds) return;

    if ((frame % 60) != 0) return;
    if (instanceID < 0 || instanceID >= 16) return;

    const melonDS::u64 hash = HashNDS(nds);
    if (G.LastLoggedHashFrame[instanceID] == frame) return;
    G.LastLoggedHashFrame[instanceID] = frame;

    std::printf("NSMB PoC: inst=%d frame=%u hash=%016llX\n",
        instanceID,
        frame,
        static_cast<unsigned long long>(hash));
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
}

}
