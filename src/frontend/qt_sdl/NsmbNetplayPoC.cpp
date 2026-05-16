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
constexpr melonDS::u32 kGameStageIDAddr = 0x02085054;
constexpr melonDS::u32 kGameStageGroupAddr = 0x02085058;
constexpr melonDS::u32 kGameLocalPlayerIDAddr = 0x020850BC;
constexpr melonDS::u32 kGameVsModeAddr = 0x020850C4;
constexpr melonDS::u32 kNetGGIDAddr = 0x02087E78;
constexpr melonDS::u32 kNetRandomBranchAddressAddr = 0x02087E7C;
constexpr melonDS::u32 kNetRandomCallCountAddr = 0x02088068;
constexpr melonDS::u32 kNetRandomValueAddr = 0x02088088;
constexpr melonDS::u32 kGamePlayerGlobalBlockAddr = 0x0208A964;
constexpr melonDS::u32 kGamePlayerCountAddr = 0x0208A988;
constexpr melonDS::u32 kGamePlayerBattleStarsAddr = 0x0208A9AC;
constexpr melonDS::u32 kGamePlayerCoinsAddr = 0x0208A9BC;
constexpr melonDS::u32 kGamePlayerScoreAddr = 0x0208A9C4;
constexpr melonDS::u32 kGamePlayerDisplayedStarsAddr = 0x0208A9CC;
constexpr melonDS::u32 kGamePlayerDeathsAddr = 0x0208A9D4;
constexpr melonDS::u32 kGamePlayerCollectedStarsAddr = 0x0208A9DC;
constexpr melonDS::u32 kGameVsCoinCountAddr = 0x0208A994;
constexpr melonDS::u32 kGameCandidateWifiBlockAddr = 0x0208BE00;
constexpr melonDS::u32 kGameCandidateRenderBlockAddr = 0x023F8300;
constexpr melonDS::u16 kPlayerObjectID = 0x0015;
constexpr melonDS::u16 kVsBattleStarActorObjectID = 0x0022;
constexpr melonDS::u32 kVsBattleStarActorSettings = 0x00000001;
constexpr melonDS::u16 kVsBattleStarCandidateObjectID = 0x010C;
constexpr melonDS::u16 kStageSceneObjectID = 0x0003;
constexpr melonDS::u16 kStageCameraObjectID = 0x013C;

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
constexpr melonDS::u32 kWireKindState = 0x54415453; // "STAT", little endian

static_assert(sizeof(WireSeed) == 16);

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
    melonDS::u32 BasicHashLo;
    melonDS::u32 BasicHashHi;
    melonDS::u32 PlayerGlobalHashLo;
    melonDS::u32 PlayerGlobalHashHi;
    melonDS::u32 WifiCandidateHashLo;
    melonDS::u32 WifiCandidateHashHi;
    melonDS::u32 RenderCandidateHashLo;
    melonDS::u32 RenderCandidateHashHi;
};

static_assert(sizeof(WireGameState) == 340);

struct GameStateSample
{
    melonDS::u32 StageID = 0;
    melonDS::u32 StageGroup = 0;
    melonDS::u32 VsMode = 0;
    melonDS::u32 LocalPlayerID = 0;
    melonDS::u32 GGID = 0;
    melonDS::u32 NetRandomValue = 0;
    melonDS::u32 NetRandomCallCount = 0;
    melonDS::u32 NetRandomBranchAddress = 0;
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
    melonDS::u32 PlayerActor0Settings = 0;
    melonDS::u32 PlayerActor0PosX = 0;
    melonDS::u32 PlayerActor0PosY = 0;
    melonDS::u32 PlayerActor0PosZ = 0;
    melonDS::u32 PlayerActor0PrevX = 0;
    melonDS::u32 PlayerActor0PrevY = 0;
    melonDS::u32 PlayerActor0PrevZ = 0;
    melonDS::u32 PlayerActor0VelX = 0;
    melonDS::u32 PlayerActor0VelY = 0;
    melonDS::u32 PlayerActor0VelZ = 0;
    melonDS::u32 PlayerActor1Found = 0;
    melonDS::u32 PlayerActor1GUID = 0;
    melonDS::u32 PlayerActor1Settings = 0;
    melonDS::u32 PlayerActor1PosX = 0;
    melonDS::u32 PlayerActor1PosY = 0;
    melonDS::u32 PlayerActor1PosZ = 0;
    melonDS::u32 PlayerActor1PrevX = 0;
    melonDS::u32 PlayerActor1PrevY = 0;
    melonDS::u32 PlayerActor1PrevZ = 0;
    melonDS::u32 PlayerActor1VelX = 0;
    melonDS::u32 PlayerActor1VelY = 0;
    melonDS::u32 PlayerActor1VelZ = 0;
    melonDS::u32 PlayerCount = 0;
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
    melonDS::u32 StageCameraWord190 = 0;
    melonDS::u32 StageCameraWord194 = 0;
    melonDS::u32 StageCameraWord19C = 0;
    melonDS::u32 StageCameraWord1A0 = 0;
    melonDS::u32 StageSceneFound = 0;
    melonDS::u32 StageSceneWord154 = 0;
    melonDS::u32 StageSceneWord160 = 0;
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
    melonDS::u32 VelX = 0;
    melonDS::u32 VelY = 0;
    melonDS::u32 VelZ = 0;
};

struct PlayerActorScanSample
{
    ObjectScanSample Actor0;
    ObjectScanSample Actor1;
};

struct GameStateSyncHashes
{
    melonDS::u64 Basic = 0;
    melonDS::u64 PlayerGlobal = 0;
    melonDS::u64 WifiCandidate = 0;
    melonDS::u64 RenderCandidate = 0;
};

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
    bool ScreenHashEnabled = false;
    bool GameStateTraceExtended = false;
    bool GameStateSyncEnabled = false;
    bool GameStateSyncExtended = false;
    bool GameStateApplyEnabled = false;
    int GameStateSyncInterval = 60;
    int SeedWaitTimeoutMs = 10000;
    bool WaitForPeerBeforeStart = false;
    bool WaitForPeerAtNetplayStart = false;
    bool WaitedForPeerAtNetplayStart = false;
    bool NetplayStartWaitArrived[16] {};
    bool NetplayStartWaitComplete = false;
    bool DeferNetworkUntilStart = false;
    bool NetplayFrameBarrierEnabled = false;
    std::string InputScriptPath;
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
    bool GameStateMismatchSeen = false;
    melonDS::u32 LastTracedSentInputFrame = kNoFrameLimit;
    melonDS::u32 LastTracedReceivedInputFrame = kNoFrameLimit;
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
FrameBarrier GNetplayFrameBarrier;

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
                    if (G.StateLoadDir.empty())
                    {
                        G.NetRandomPatchValue = packet.Seed;
                        G.NetRandomPatchEnabled = true;
                        G.NetRandomPatchAuto = true;
                    }
                    std::printf("NSMB PoC: received match seed 0x%08X\n", packet.Seed);
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

void ReadObjectTransform(melonDS::NDS* nds, melonDS::u32 off, ObjectScanSample& sample)
{
    ReadMainRAMU32(nds, off + 0x5C, sample.PosX);
    ReadMainRAMU32(nds, off + 0x60, sample.PosY);
    ReadMainRAMU32(nds, off + 0x64, sample.PosZ);
    ReadMainRAMU32(nds, off + 0x68, sample.PrevX);
    ReadMainRAMU32(nds, off + 0x6C, sample.PrevY);
    ReadMainRAMU32(nds, off + 0x70, sample.PrevZ);
    ReadMainRAMU32(nds, off + 0x74, sample.VelX);
    ReadMainRAMU32(nds, off + 0x78, sample.VelY);
    ReadMainRAMU32(nds, off + 0x7C, sample.VelZ);
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
        if (flags >= 0x01000000)
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
        if (flags >= 0x01000000)
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
        if (flags >= 0x01000000)
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
    WriteMainRAMU32(nds, starOffset + 0x5C, player.PosX);
    WriteMainRAMU32(nds, starOffset + 0x60, player.PosY);
    WriteMainRAMU32(nds, starOffset + 0x64, player.PosZ);
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
    WriteMainRAMU32(nds, off + 0x5C, posX);
    WriteMainRAMU32(nds, off + 0x60, posY);
    WriteMainRAMU32(nds, off + 0x64, posZ);
    WriteMainRAMU32(nds, off + 0x68, posX);
    WriteMainRAMU32(nds, off + 0x6C, posY);
    WriteMainRAMU32(nds, off + 0x70, posZ);
    if (clearVelocity)
    {
        WriteMainRAMU32(nds, off + 0x74, 0);
        WriteMainRAMU32(nds, off + 0x78, 0);
        WriteMainRAMU32(nds, off + 0x7C, 0);
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
        if (flags >= 0x01000000)
            continue;

        WriteMainRAMU32(nds, off + 0x5C, posX);
        WriteMainRAMU32(nds, off + 0x60, posY);
        WriteMainRAMU32(nds, off + 0x64, posZ);
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
    WriteMainRAMU32(nds, off + 0x5C, posX);
    WriteMainRAMU32(nds, off + 0x60, posY);
    WriteMainRAMU32(nds, off + 0x64, posZ);
    WriteMainRAMU32(nds, off + 0x68, posX);
    WriteMainRAMU32(nds, off + 0x6C, posY);
    WriteMainRAMU32(nds, off + 0x70, posZ);
    return true;
}

bool WriteVsBattleStarCandidatePosition(melonDS::NDS* nds, melonDS::u32 posX, melonDS::u32 posY, melonDS::u32 posZ)
{
    ObjectScanSample actor = FindVsBattleStarCandidate(nds);
    if (!actor.Found || actor.Base < kMainRAMBase)
        return false;

    const melonDS::u32 off = actor.Base - kMainRAMBase;
    WriteMainRAMU32(nds, off + 0x5C, posX);
    WriteMainRAMU32(nds, off + 0x60, posY);
    WriteMainRAMU32(nds, off + 0x64, posZ);
    WriteMainRAMU32(nds, off + 0x68, posX);
    WriteMainRAMU32(nds, off + 0x6C, posY);
    WriteMainRAMU32(nds, off + 0x70, posZ);
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
        if (flags >= 0x01000000)
            continue;

        WriteMainRAMU32(nds, off + 0x5C, posX);
        WriteMainRAMU32(nds, off + 0x60, posY);
        WriteMainRAMU32(nds, off + 0x64, posZ);
        WriteMainRAMU32(nds, off + 0x68, prevX);
        WriteMainRAMU32(nds, off + 0x6C, prevY);
        WriteMainRAMU32(nds, off + 0x70, prevZ);
        WriteMainRAMU32(nds, off + 0x74, velX);
        WriteMainRAMU32(nds, off + 0x78, velY);
        WriteMainRAMU32(nds, off + 0x7C, velZ);
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

    if (sample.StageCameraFound)
    {
        WriteObjectWordByIDAndSettings(nds, kStageCameraObjectID, 0, 0x190, sample.StageCameraWord190);
        WriteObjectWordByIDAndSettings(nds, kStageCameraObjectID, 0, 0x194, sample.StageCameraWord194);
        WriteObjectWordByIDAndSettings(nds, kStageCameraObjectID, 0, 0x19C, sample.StageCameraWord19C);
        WriteObjectWordByIDAndSettings(nds, kStageCameraObjectID, 0, 0x1A0, sample.StageCameraWord1A0);
    }
    if (sample.StageSceneFound)
    {
        WriteObjectWordByIDAndSettings(nds, kStageSceneObjectID, 0x00B5FF00, 0x154, sample.StageSceneWord154);
        WriteObjectWordByIDAndSettings(nds, kStageSceneObjectID, 0x00B5FF00, 0x160, sample.StageSceneWord160);
    }

    if (sample.VsStarFound)
    {
        if (!WriteObjectPositionByGUID(nds, sample.VsStarGUID, sample.VsStarPosX, sample.VsStarPosY, sample.VsStarPosZ))
            WriteVsBattleStarCandidatePosition(nds, sample.VsStarPosX, sample.VsStarPosY, sample.VsStarPosZ);
    }
    else
    {
        WriteVsBattleStarCandidatePosition(nds, 0, 0, 0);
    }
    if (sample.VsStarActorFound)
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
    else
    {
        WriteObjectPositionByIDAndSettings(nds, kVsBattleStarActorObjectID, kVsBattleStarActorSettings, 0, 0, 0);
    }
    if (sample.PlayerActor0Found)
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
    if (sample.PlayerActor1Found)
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
    sample.GGID = nds->ARM9Read32(kNetGGIDAddr);
    sample.NetRandomValue = nds->ARM9Read32(kNetRandomValueAddr);
    sample.NetRandomCallCount = nds->ARM9Read8(kNetRandomCallCountAddr);
    sample.NetRandomBranchAddress = nds->ARM9Read32(kNetRandomBranchAddressAddr);

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
    sample.PlayerActor0Settings = players.Actor0.Settings;
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
    sample.PlayerActor1Settings = players.Actor1.Settings;
    sample.PlayerActor1PosX = players.Actor1.PosX;
    sample.PlayerActor1PosY = players.Actor1.PosY;
    sample.PlayerActor1PosZ = players.Actor1.PosZ;
    sample.PlayerActor1PrevX = players.Actor1.PrevX;
    sample.PlayerActor1PrevY = players.Actor1.PrevY;
    sample.PlayerActor1PrevZ = players.Actor1.PrevZ;
    sample.PlayerActor1VelX = players.Actor1.VelX;
    sample.PlayerActor1VelY = players.Actor1.VelY;
    sample.PlayerActor1VelZ = players.Actor1.VelZ;

    sample.PlayerCount = nds->ARM9Read32(kGamePlayerCountAddr);
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

    melonDS::u32 stageWord = 0;
    if (ReadObjectWordByIDAndSettings(nds, kStageCameraObjectID, 0, 0x190, stageWord))
    {
        sample.StageCameraFound = 1;
        sample.StageCameraWord190 = stageWord;
        ReadObjectWordByIDAndSettings(nds, kStageCameraObjectID, 0, 0x194, sample.StageCameraWord194);
        ReadObjectWordByIDAndSettings(nds, kStageCameraObjectID, 0, 0x19C, sample.StageCameraWord19C);
        ReadObjectWordByIDAndSettings(nds, kStageCameraObjectID, 0, 0x1A0, sample.StageCameraWord1A0);
    }
    if (ReadObjectWordByIDAndSettings(nds, kStageSceneObjectID, 0x00B5FF00, 0x154, stageWord))
    {
        sample.StageSceneFound = 1;
        sample.StageSceneWord154 = stageWord;
        ReadObjectWordByIDAndSettings(nds, kStageSceneObjectID, 0x00B5FF00, 0x160, sample.StageSceneWord160);
    }

    sample.Hash = 1469598103934665603ull;
    MixGameStateValue(sample.Hash, sample.StageID);
    MixGameStateValue(sample.Hash, sample.StageGroup);
    MixGameStateValue(sample.Hash, sample.VsMode);
    MixGameStateValue(sample.Hash, sample.LocalPlayerID);
    MixGameStateValue(sample.Hash, sample.GGID);
    MixGameStateValue(sample.Hash, sample.NetRandomValue);
    MixGameStateValue(sample.Hash, sample.NetRandomCallCount);
    MixGameStateValue(sample.Hash, sample.NetRandomBranchAddress);
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
        const melonDS::u32 stageGroup = nds->ARM9Read32(kGameStageGroupAddr);
        const melonDS::u32 vsMode = nds->ARM9Read32(kGameVsModeAddr);
        const melonDS::u32 ggid = nds->ARM9Read32(kNetGGIDAddr);
        randomCallCountBeforePatch = nds->ARM9Read8(kNetRandomCallCountAddr);
        shouldPatch = stageGroup == 9 && vsMode == 1 && ggid == 0x42;
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
                     << ",0x" << sample.GGID
                     << ",0x" << sample.NetRandomValue
                     << ",0x" << sample.NetRandomCallCount
                     << ",0x" << sample.NetRandomBranchAddress
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
                     << ",0x" << sample.PlayerActor0Settings
                     << ",0x" << sample.PlayerActor0PosX
                     << ",0x" << sample.PlayerActor0PosY
                     << ",0x" << sample.PlayerActor0PosZ
                     << ",0x" << sample.PlayerActor1Found
                     << ",0x" << sample.PlayerActor1GUID
                     << ",0x" << sample.PlayerActor1Settings
                     << ",0x" << sample.PlayerActor1PosX
                     << ",0x" << sample.PlayerActor1PosY
                     << ",0x" << sample.PlayerActor1PosZ;

    if (G.GameStateTraceExtended)
    {
        const melonDS::u64 playerGlobalHash = HashMainRAMRange(nds, kGamePlayerGlobalBlockAddr, 0xC0);
        const melonDS::u64 wifiCandidateHash = HashMainRAMRange(nds, kGameCandidateWifiBlockAddr, 0x2200);
        const melonDS::u64 renderCandidateHash = HashMainRAMRange(nds, kGameCandidateRenderBlockAddr, 0x240);

        G.GameStateTrace << ",0x" << sample.PlayerCount
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
                         << ",0x" << playerGlobalHash
                         << ",0x" << wifiCandidateHash
                         << ",0x" << renderCandidateHash;
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
    G.FrameBarrierEnabled = EnvFlag("MELONDS_NSML_FRAME_BARRIER");
    G.SerialRunEnabled = EnvFlag("MELONDS_NSML_SERIAL_RUN");
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

    const char* gameStateTrace = std::getenv("MELONDS_NSML_GAME_STATE_TRACE");
    if (gameStateTrace && gameStateTrace[0]) G.GameStateTracePath = gameStateTrace;
    G.GameStateTraceInterval = std::max(1, EnvInt("MELONDS_NSML_GAME_STATE_TRACE_INTERVAL", 60));
    G.GameStateTraceExtended = EnvFlag("MELONDS_NSML_GAME_STATE_TRACE_EXTENDED");
    G.GameStateSyncEnabled = EnvFlag("MELONDS_NSML_STATE_SYNC");
    G.GameStateSyncExtended = EnvFlag("MELONDS_NSML_STATE_SYNC_EXTENDED");
    G.GameStateApplyEnabled = EnvFlag("MELONDS_NSML_STATE_APPLY");
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
            G.GameStateTrace << "instance,frame,stageID,stageGroup,vsMode,localPlayerID,ggid,netRandomValue,netRandomCallCount,netRandomBranchAddress,vsStarFound,vsStarGuid,vsStarBase,vsStarSettings,vsStarStateType,vsStarFlags,vsStarX,vsStarY,vsStarZ,vsStarActorFound,vsStarActorGuid,vsStarActorBase,vsStarActorSettings,vsStarActorStateType,vsStarActorFlags,vsStarActorX,vsStarActorY,vsStarActorZ,playerActor0Found,playerActor0Guid,playerActor0Settings,playerActor0X,playerActor0Y,playerActor0Z,playerActor1Found,playerActor1Guid,playerActor1Settings,playerActor1X,playerActor1Y,playerActor1Z";
            if (G.GameStateTraceExtended)
                G.GameStateTrace << ",playerCount,player0BattleStars,player1BattleStars,player0Coins,player1Coins,player0Score,player1Score,player0DisplayedStars,player1DisplayedStars,player0Deaths,player1Deaths,player0CollectedStars,player1CollectedStars,vsCoinCount,playerGlobalHash,wifiCandidateHash,renderCandidateHash";
            G.GameStateTrace << '\n';
        }
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
                if (G.ScreenHashEnabled)
                    G.HashLog << "instance,frame,hash,screenHash\n";
                else
                    G.HashLog << "instance,frame,hash\n";
            }
        }

        std::printf("NSMB Test: enabled frames=%u instances=%d frameBarrier=%d serialRun=%d input=%s hashLog=%s interval=%d screenshotDir=%s screenshotInterval=%d ramDumpDir=%s ramDumpInterval=%d ramDumpRanges=%zu gameStateTrace=%s gameStateTraceInterval=%d stateSync=%d stateApply=%d stateSyncInterval=%d memPatchFile=%s memPatchFrame=%u memPatchRanges=%zu netRandomEnabled=%d netRandomAuto=%d netRandomFrame=%u netRandomValue=0x%08X stateSaveDir=%s stateSaveFrame=%u stateLoadDir=%s stateLoadFrame=%u waitTimeoutMs=%d quitGraceMs=%d inputTrace=%d inputTraceInterval=%d seedWaitMs=%d waitForPeer=%d waitForPeerAtStart=%d deferNetworkUntilStart=%d netplayFrameBarrier=%d\n",
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
            G.NetplayFrameBarrierEnabled ? 1 : 0);
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

    if (G.NetRole == Role::Host && G.MatchSeedConfigured && G.StateLoadDir.empty())
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
    std::printf("NSMB PoC: enabled role=%s port=%d peer=%s delay=%d warmup=%d localInstance=%d netplayStartFrame=%u localWait=%d waitForPeer=%d waitForPeerAtStart=%d deferNetworkUntilStart=%d netplayFrameBarrier=%d matchSeed=0x%08X seedConfigured=%d\n",
        G.NetRole == Role::Host ? "host" : "client",
        G.Port,
        G.PeerHost,
        G.Delay,
        G.NetplayWarmupFrames,
        G.LocalInstance,
        G.NetplayStartFrame,
        G.LocalWaitsForRemote ? 1 : 0,
        G.WaitForPeerBeforeStart ? 1 : 0,
        G.WaitForPeerAtNetplayStart ? 1 : 0,
        G.DeferNetworkUntilStart ? 1 : 0,
        G.NetplayFrameBarrierEnabled ? 1 : 0,
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

    if (G.TestEnabled && instanceID >= 0 && instanceID < 16 && nds)
        ApplyVsStarSnap(instanceID, inputFrame, nds);

    if (G.TestEnabled && instanceID >= 0 && instanceID < 16 && nds)
        ApplyPlayerSnapToStar(instanceID, inputFrame, nds);

    if (G.TestEnabled && instanceID >= 0 && instanceID < 16 && nds)
        ApplyPlayerStickToStar(instanceID, inputFrame, nds);

    if (G.Enabled && instanceID >= 0 && instanceID < 16 && nds)
        ApplyRemoteGameState(instanceID, inputFrame, nds);

    WaitForSerialRunTurn(instanceID, inputFrame);
    WaitAtFrameBarrier(GBeforeFrameBarrier, instanceID, inputFrame, "before");

    const InputState testInput = ApplyInputScript(instanceID, inputFrame, polledInput);
    const melonDS::u32 syncFrame = G.TestEnabled ? inputFrame : frame;

    if (!G.Enabled || !G.Ready) return testInput;
    if (syncFrame == 0)
    {
        WaitForPeerIfNeeded();
        WaitForMatchSeedIfNeeded();
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

    if (G.NetplayFrameBarrierEnabled)
        WaitAtFrameBarrier(GNetplayFrameBarrier, instanceID, targetFrame, "netplay");

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
    WaitForPeerAtNetplayStartBarrier(instanceID, logFrame);

    if (G.Enabled)
        ApplyRemoteGameState(instanceID, logFrame, nds);

    SaveState(instanceID, logFrame, nds);
    SaveLocalMPState(logFrame);
    SaveScreenshot(instanceID, logFrame, nds);
    SaveRamDump(instanceID, logFrame, nds);
    TraceGameState(instanceID, logFrame, nds);
    SyncGameState(instanceID, logFrame, nds);

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

    if (G.GameStateTrace)
        G.GameStateTrace.close();
}

}
