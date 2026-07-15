#include "NsmbNetplayDiagnostics.h"
#include "NsmbImitationAI.h"

#include <QImage>
#include <QString>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off: dbghelp requires Windows types to be declared first.
#include <windows.h>
#include <dbghelp.h>
// clang-format on
#endif

namespace NsmbNetplayPoC::Diagnostics {
namespace {

std::uint64_t NowUnixMs() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

bool EnsureLogOpen(std::ofstream &file, const std::string &path) {
  if (path.empty())
    return false;
  if (file.is_open())
    return true;

  const std::filesystem::path logPath(path);
  std::error_code error;
  if (logPath.has_parent_path())
    std::filesystem::create_directories(logPath.parent_path(), error);
  file.open(logPath, std::ios::out | std::ios::app | std::ios::binary);
  if (!file) {
    std::printf("NSMB HangDiagnostics: failed to open log: %s\n",
                logPath.string().c_str());
    std::fflush(stdout);
    return false;
  }
  return true;
}

template <typename... Args>
std::string FormatPrintf(const char *format, Args... args) {
  const int length = std::snprintf(nullptr, 0, format, args...);
  if (length <= 0)
    return {};
  std::vector<char> buffer(static_cast<std::size_t>(length) + 1);
  std::snprintf(buffer.data(), buffer.size(), format, args...);
  return std::string(buffer.data(), static_cast<std::size_t>(length));
}

#ifdef _WIN32
bool WriteMiniDump(const std::string &path) {
  if (path.empty())
    return false;

  const std::filesystem::path dumpPath(path);
  std::error_code error;
  if (dumpPath.has_parent_path())
    std::filesystem::create_directories(dumpPath.parent_path(), error);

  HMODULE dbghelp = LoadLibraryA("Dbghelp.dll");
  if (!dbghelp)
    return false;

  using MiniDumpWriteDumpFn = BOOL(WINAPI *)(
      HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
      PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
  auto miniDumpWriteDump = reinterpret_cast<MiniDumpWriteDumpFn>(
      GetProcAddress(dbghelp, "MiniDumpWriteDump"));
  if (!miniDumpWriteDump) {
    FreeLibrary(dbghelp);
    return false;
  }

  HANDLE file =
      CreateFileA(dumpPath.string().c_str(), GENERIC_WRITE, 0, nullptr,
                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    FreeLibrary(dbghelp);
    return false;
  }

  const BOOL ok =
      miniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                        MiniDumpNormal, nullptr, nullptr, nullptr);
  CloseHandle(file);
  FreeLibrary(dbghelp);
  return ok != FALSE;
}
#else
bool WriteMiniDump(const std::string &) { return false; }
#endif

} // namespace

void AppendJsonHex32(std::ostream &out, const char *key, melonDS::u32 value) {
  const std::ios::fmtflags flags = out.flags();
  const char fill = out.fill();
  out << '\"' << key << "\":\"0x" << std::hex << std::uppercase
      << std::setw(8) << std::setfill('0') << value << std::dec << '\"';
  out.flags(flags);
  out.fill(fill);
}

void AppendJsonHex64(std::ostream &out, const char *key, melonDS::u64 value) {
  const std::ios::fmtflags flags = out.flags();
  const char fill = out.fill();
  out << '\"' << key << "\":\"0x" << std::hex << std::uppercase
      << std::setw(16) << std::setfill('0') << value << std::dec << '\"';
  out.flags(flags);
  out.fill(fill);
}

void AppendDiagnosticPlayerJson(std::ostream &out,
                                const DiagnosticPlayerSnapshot &player) {
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

void AppendDiagnosticFrameJson(std::ostream &out,
                               const DiagnosticFrameSnapshot &snapshot) {
  out << "{";
  out << "\"frame\":" << snapshot.Frame << ",";
  out << "\"instance\":" << snapshot.Instance << ",";
  out << "\"stageID\":" << snapshot.StageID << ",";
  out << "\"stageGroup\":" << snapshot.StageGroup << ",";
  out << "\"vsMode\":" << snapshot.VsMode << ",";
  out << "\"localPlayerID\":" << snapshot.LocalPlayerID << ",";
  out << "\"scene\":" << snapshot.SceneCurrentSceneID << ",";
  out << "\"nextScene\":" << snapshot.SceneNextSceneID << ",";
  out << "\"freeze\":" << snapshot.StageActorFreezeFlag << ",";
  out << "\"playerCount\":" << snapshot.PlayerCount << ",";
  AppendJsonHex32(out, "inputConsole0", snapshot.InputConsole0Held); out << ",";
  AppendJsonHex32(out, "inputConsole1", snapshot.InputConsole1Held); out << ",";
  AppendJsonHex32(out, "inputPlayer0", snapshot.InputPlayer0Held); out << ",";
  AppendJsonHex32(out, "inputPlayer1", snapshot.InputPlayer1Held); out << ",";
  out << "\"lastSentInputFrame\":" << snapshot.LastSentInputFrame << ",";
  out << "\"lastReceivedInputFrame\":" << snapshot.LastReceivedInputFrame << ",";
  AppendJsonHex64(out, "playerGlobalHash", snapshot.PlayerGlobalHash); out << ",";
  AppendJsonHex64(out, "playerGlobalHash0", snapshot.PlayerGlobalHash0); out << ",";
  AppendJsonHex64(out, "playerGlobalHash1", snapshot.PlayerGlobalHash1); out << ",";
  AppendJsonHex64(out, "playerActorHash0", snapshot.PlayerActorHash0); out << ",";
  AppendJsonHex64(out, "playerActorHash1", snapshot.PlayerActorHash1); out << ",";
  AppendJsonHex32(out, "cameraX0", snapshot.StageCameraGlobalX0); out << ",";
  AppendJsonHex32(out, "cameraX1", snapshot.StageCameraGlobalX1); out << ",";
  AppendJsonHex32(out, "cameraY0", snapshot.StageCameraGlobalY0); out << ",";
  AppendJsonHex32(out, "cameraY1", snapshot.StageCameraGlobalY1); out << ",";
  AppendJsonHex32(out, "cameraWidth0", snapshot.StageCameraGlobalWidth0); out << ",";
  AppendJsonHex32(out, "cameraWidth1", snapshot.StageCameraGlobalWidth1); out << ",";
  AppendJsonHex32(out, "cameraHeight0", snapshot.StageCameraGlobalHeight0); out << ",";
  AppendJsonHex32(out, "cameraHeight1", snapshot.StageCameraGlobalHeight1); out << ",";
  out << "\"players\":[";
  AppendDiagnosticPlayerJson(out, snapshot.Player[0]);
  out << ",";
  AppendDiagnosticPlayerJson(out, snapshot.Player[1]);
  out << "]}";
}

void AppendGameStatePlayerJson(std::ostream &out,
                               const GameStateModel::GameStateSample &sample,
                               int player) {
  const bool first = player == 0;
  DiagnosticPlayerSnapshot snapshot;
  snapshot.Found = first ? sample.PlayerActor0Found : sample.PlayerActor1Found;
  snapshot.Base = first ? sample.PlayerActor0Base : sample.PlayerActor1Base;
  snapshot.GUID = first ? sample.PlayerActor0GUID : sample.PlayerActor1GUID;
  snapshot.Settings = first ? sample.PlayerActor0Settings : sample.PlayerActor1Settings;
  snapshot.StateType = first ? sample.PlayerActor0StateType : sample.PlayerActor1StateType;
  snapshot.Flags = first ? sample.PlayerActor0Flags : sample.PlayerActor1Flags;
  snapshot.PosX = first ? sample.PlayerActor0PosX : sample.PlayerActor1PosX;
  snapshot.PosY = first ? sample.PlayerActor0PosY : sample.PlayerActor1PosY;
  snapshot.PosZ = first ? sample.PlayerActor0PosZ : sample.PlayerActor1PosZ;
  snapshot.PrevX = first ? sample.PlayerActor0PrevX : sample.PlayerActor1PrevX;
  snapshot.PrevY = first ? sample.PlayerActor0PrevY : sample.PlayerActor1PrevY;
  snapshot.PrevZ = first ? sample.PlayerActor0PrevZ : sample.PlayerActor1PrevZ;
  snapshot.VelX = first ? sample.PlayerActor0VelX : sample.PlayerActor1VelX;
  snapshot.VelY = first ? sample.PlayerActor0VelY : sample.PlayerActor1VelY;
  snapshot.VelZ = first ? sample.PlayerActor0VelZ : sample.PlayerActor1VelZ;
  snapshot.ActionFlag = first ? sample.PlayerActor0ActionFlag : sample.PlayerActor1ActionFlag;
  snapshot.SubActionFlag = first ? sample.PlayerActor0SubActionFlag : sample.PlayerActor1SubActionFlag;
  snapshot.PhysicsFlag = first ? sample.PlayerActor0PhysicsFlag : sample.PlayerActor1PhysicsFlag;
  snapshot.DamageCooldown = first ? sample.PlayerActor0DamageCooldown : sample.PlayerActor1DamageCooldown;
  snapshot.TransitionFlag = first ? sample.PlayerActor0TransitionFlag : sample.PlayerActor1TransitionFlag;
  snapshot.CollisionFlag = first ? sample.PlayerActor0CollisionFlag : sample.PlayerActor1CollisionFlag;
  snapshot.EnvironmentFlag = first ? sample.PlayerActor0EnvironmentFlag : sample.PlayerActor1EnvironmentFlag;
  snapshot.LinkedActor = first ? sample.PlayerActor0LinkedActor : sample.PlayerActor1LinkedActor;
  snapshot.TransitionStep = first ? sample.PlayerActor0TransitionStep : sample.PlayerActor1TransitionStep;
  snapshot.UpdateLocked = first ? sample.PlayerActor0UpdateLocked : sample.PlayerActor1UpdateLocked;
  snapshot.CharacterIDBase = first ? sample.PlayerActor0CharacterIDBase : sample.PlayerActor1CharacterIDBase;
  snapshot.TransitioningFlag = first ? sample.PlayerActor0TransitioningFlag : sample.PlayerActor1TransitioningFlag;
  snapshot.CameraFocusMode = first ? sample.PlayerActor0CameraFocusMode : sample.PlayerActor1CameraFocusMode;
  snapshot.DefeatedFlag = first ? sample.PlayerActor0DefeatedFlag : sample.PlayerActor1DefeatedFlag;
  snapshot.PlayerBaseID = first ? sample.PlayerActor0PlayerBaseID : sample.PlayerActor1PlayerBaseID;
  snapshot.VisibleFlag = first ? sample.PlayerActor0VisibleFlag : sample.PlayerActor1VisibleFlag;
  snapshot.TransitFunc = first ? sample.PlayerActor0TransitFunc : sample.PlayerActor1TransitFunc;
  snapshot.TransitArg = first ? sample.PlayerActor0TransitArg : sample.PlayerActor1TransitArg;
  snapshot.Powerup = first ? sample.Player0Powerup : sample.Player1Powerup;
  snapshot.InventoryPowerup = first ? sample.Player0InventoryPowerup : sample.Player1InventoryPowerup;
  snapshot.Dead = first ? sample.Player0Dead : sample.Player1Dead;
  snapshot.Character = first ? sample.Player0Character : sample.Player1Character;
  snapshot.TransitionStatus = first ? sample.PlayerTransitionStatus0 : sample.PlayerTransitionStatus1;
  snapshot.Lives = first ? sample.Player0Lives : sample.Player1Lives;
  snapshot.BattleStars = first ? sample.Player0BattleStars : sample.Player1BattleStars;
  snapshot.Coins = first ? sample.Player0Coins : sample.Player1Coins;
  snapshot.Score = first ? sample.Player0Score : sample.Player1Score;
  snapshot.DisplayedStars = first ? sample.Player0DisplayedStars : sample.Player1DisplayedStars;
  snapshot.Deaths = first ? sample.Player0Deaths : sample.Player1Deaths;
  snapshot.CollectedStars = first ? sample.Player0CollectedStars : sample.Player1CollectedStars;
  AppendDiagnosticPlayerJson(out, snapshot);
}

namespace {

constexpr melonDS::s32 kDiagnosticFixedOne = 0x1000;
constexpr melonDS::s32 kDiagnosticOffscreenMargin =
    512 * kDiagnosticFixedOne;
constexpr melonDS::s32 kDiagnosticLargePositionDelta =
    256 * kDiagnosticFixedOne;

melonDS::s64 FixedDelta(melonDS::u32 lhs, melonDS::u32 rhs) {
  return static_cast<melonDS::s64>(static_cast<melonDS::s32>(lhs)) -
         static_cast<melonDS::s64>(static_cast<melonDS::s32>(rhs));
}

melonDS::u32 CameraX(const DiagnosticFrameSnapshot &snapshot, int player) {
  return player == 0 ? snapshot.StageCameraGlobalX0
                     : snapshot.StageCameraGlobalX1;
}

melonDS::u32 CameraY(const DiagnosticFrameSnapshot &snapshot, int player) {
  return player == 0 ? snapshot.StageCameraGlobalY0
                     : snapshot.StageCameraGlobalY1;
}

melonDS::u32 CameraWidth(const DiagnosticFrameSnapshot &snapshot, int player) {
  const melonDS::u32 value = player == 0 ? snapshot.StageCameraGlobalWidth0
                                         : snapshot.StageCameraGlobalWidth1;
  return value != 0 ? value : 256 * kDiagnosticFixedOne;
}

melonDS::u32 CameraHeight(const DiagnosticFrameSnapshot &snapshot, int player) {
  const melonDS::u32 value = player == 0 ? snapshot.StageCameraGlobalHeight0
                                         : snapshot.StageCameraGlobalHeight1;
  return value != 0 ? value : 192 * kDiagnosticFixedOne;
}

bool IsLiveForPositionCheck(const DiagnosticPlayerSnapshot &player) {
  return player.Found != 0 && player.Dead == 0 && player.VisibleFlag != 0 &&
         player.TransitioningFlag == 0 && player.DefeatedFlag == 0;
}

} // namespace

void AppendDiagnosticPlayerContextJson(
    std::ostream &out, const DiagnosticFrameSnapshot &snapshot,
    const DiagnosticFrameSnapshot *previous, int player) {
  const DiagnosticPlayerSnapshot &current = snapshot.Player[player];
  const DiagnosticPlayerSnapshot *prior =
      previous ? &previous->Player[player] : nullptr;
  const melonDS::u32 cameraX = CameraX(snapshot, player);
  const melonDS::u32 cameraY = CameraY(snapshot, player);
  const melonDS::u32 cameraWidth = CameraWidth(snapshot, player);
  const melonDS::u32 cameraHeight = CameraHeight(snapshot, player);
  const melonDS::s64 screenX = FixedDelta(current.PosX, cameraX);
  const melonDS::s64 screenY = FixedDelta(current.PosY, cameraY);
  const melonDS::s64 deltaX = prior ? FixedDelta(current.PosX, prior->PosX) : 0;
  const melonDS::s64 deltaY = prior ? FixedDelta(current.PosY, prior->PosY) : 0;

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
  if (prior) {
    out << ",\"previous\":";
    AppendDiagnosticPlayerJson(out, *prior);
  }
}

bool IsPlayerScreenPositionAnomalous(
    const DiagnosticFrameSnapshot &snapshot,
    const DiagnosticFrameSnapshot *previous, int player) {
  if (player < 0 || player > 1)
    return false;
  const DiagnosticPlayerSnapshot &current = snapshot.Player[player];
  if (!IsLiveForPositionCheck(current))
    return false;

  const melonDS::s64 screenX = FixedDelta(current.PosX, CameraX(snapshot, player));
  const melonDS::s64 cameraWidth = CameraWidth(snapshot, player);
  if (screenX < -static_cast<melonDS::s64>(kDiagnosticOffscreenMargin) ||
      screenX > cameraWidth + kDiagnosticOffscreenMargin)
    return true;

  if (previous && previous->Valid &&
      IsLiveForPositionCheck(previous->Player[player])) {
    const melonDS::s64 deltaX =
        FixedDelta(current.PosX, previous->Player[player].PosX);
    const melonDS::s64 deltaY =
        FixedDelta(current.PosY, previous->Player[player].PosY);
    if (std::llabs(deltaX) > kDiagnosticLargePositionDelta ||
        std::llabs(deltaY) > kDiagnosticLargePositionDelta)
      return true;
  }
  return false;
}

std::string JsonEscape(const std::string &value) {
  std::ostringstream out;
  for (const char ch : value) {
    switch (ch) {
    case '\\': out << "\\\\"; break;
    case '"': out << "\\\""; break;
    case '\b': out << "\\b"; break;
    case '\f': out << "\\f"; break;
    case '\n': out << "\\n"; break;
    case '\r': out << "\\r"; break;
    case '\t': out << "\\t"; break;
    default:
      if (static_cast<unsigned char>(ch) < 0x20) {
        out << "\\u" << std::hex << std::uppercase << std::setw(4)
            << std::setfill('0')
            << static_cast<int>(static_cast<unsigned char>(ch)) << std::dec
            << std::nouppercase << std::setfill(' ');
      } else {
        out << ch;
      }
      break;
    }
  }
  return out.str();
}

void AppendDiagnosticRingJson(
    std::ostream &out,
    const std::vector<DiagnosticFrameSnapshot> &snapshots) {
  out << "\"ring\":[";
  for (std::size_t i = 0; i < snapshots.size(); i++) {
    if (i != 0)
      out << ",";
    AppendDiagnosticFrameJson(out, snapshots[i]);
  }
  out << "]";
}

std::string FormatStartReadyEvent(
    const char *role, const char *direction, melonDS::u32 localFrame,
    melonDS::u32 remoteFrame, melonDS::u32 noFrameLimit,
    melonDS::u32 logicalStart, melonDS::u32 lastSentInputFrame,
    melonDS::u32 lastReceivedInputFrame, std::size_t localQueue,
    std::size_t remoteQueue, std::size_t delayedInputs) {
  const long long delta =
      (localFrame == noFrameLimit || remoteFrame == noFrameLimit)
          ? 0
          : static_cast<long long>(remoteFrame) -
                static_cast<long long>(localFrame);
  std::ostringstream json;
  json << "{\"event\":\"start_ready\"," << "\"role\":\"" << role
       << "\"," << "\"direction\":\""
       << (direction ? direction : "unknown") << "\"," << "\"localFrame\":"
       << localFrame << "," << "\"remoteFrame\":" << remoteFrame << ","
       << "\"delta\":" << delta << "," << "\"logicalStart\":"
       << logicalStart << "," << "\"lastSentInputFrame\":"
       << lastSentInputFrame << "," << "\"lastReceivedInputFrame\":"
       << lastReceivedInputFrame << "," << "\"localQueue\":" << localQueue
       << "," << "\"remoteQueue\":" << remoteQueue << ","
       << "\"delayedInputs\":" << delayedInputs << "}";
  return json.str();
}

std::string FormatDiagnosticStartupEvent(
    const char *role, int ringFrames, bool stateSync, bool stateSyncExtended,
    int stateSyncInterval, const std::string &diagnosticsPath,
    const std::string &eventsPath) {
  std::ostringstream json;
  json << "{\"event\":\"diagnostic_started\"," << "\"role\":\"" << role
       << "\"," << "\"ringFrames\":" << ringFrames << ","
       << "\"stateSync\":" << (stateSync ? "true" : "false") << ","
       << "\"stateSyncExtended\":"
       << (stateSyncExtended ? "true" : "false") << ","
       << "\"stateSyncInterval\":" << stateSyncInterval << ","
       << "\"diagnosticsFile\":\"" << JsonEscape(diagnosticsPath) << "\","
       << "\"eventsFile\":\"" << JsonEscape(eventsPath) << "\"}";
  return json.str();
}

std::string FormatDiagnosticPlayerSnapshotEvent(
    const char *event, const char *role, int instanceID,
    const DiagnosticFrameSnapshot &snapshot,
    const DiagnosticFrameSnapshot *previous, int player,
    const std::vector<DiagnosticFrameSnapshot> &ring) {
  std::ostringstream json;
  json << "{\"event\":\"" << event << "\"," << "\"role\":\"" << role
       << "\"," << "\"instance\":" << instanceID << "," << "\"frame\":"
       << snapshot.Frame << "," << "\"stageID\":" << snapshot.StageID << ","
       << "\"stageGroup\":" << snapshot.StageGroup << "," << "\"scene\":"
       << snapshot.SceneCurrentSceneID << "," << "\"nextScene\":"
       << snapshot.SceneNextSceneID << ",";
  AppendDiagnosticPlayerContextJson(json, snapshot, previous, player);
  json << ",";
  AppendDiagnosticRingJson(json, ring);
  json << "}";
  return json.str();
}

std::string FormatDiagnosticPostWindowEvent(
    const char *role, int instanceID, melonDS::u32 frame,
    melonDS::u32 triggerUntilFrame,
    const std::vector<DiagnosticFrameSnapshot> &ring) {
  std::ostringstream json;
  json << "{\"event\":\"diagnostic_post_window\"," << "\"role\":\"" << role
       << "\"," << "\"instance\":" << instanceID << "," << "\"frame\":"
       << frame << "," << "\"triggerUntilFrame\":" << triggerUntilFrame
       << ",";
  AppendDiagnosticRingJson(json, ring);
  json << "}";
  return json.str();
}

std::string FormatPlayerGlobalMismatchEvent(
    const char *role, int instanceID, melonDS::u32 frame,
    const GameStateModel::GameStateSyncHashes &local,
    const GameStateModel::GameStateSyncHashes &remote,
    const DiagnosticFrameSnapshot *latest,
    const GameStateModel::GameStateSample *remoteSample,
    const std::vector<DiagnosticFrameSnapshot> &ring) {
  std::ostringstream json;
  json << "{\"event\":\"player_global_mismatch\"," << "\"role\":\"" << role
       << "\"," << "\"instance\":" << instanceID << "," << "\"frame\":"
       << frame << ",";
  AppendJsonHex64(json, "localBasic", local.Basic); json << ",";
  AppendJsonHex64(json, "remoteBasic", remote.Basic); json << ",";
  AppendJsonHex64(json, "localPlayerGlobal", local.PlayerGlobal); json << ",";
  AppendJsonHex64(json, "remotePlayerGlobal", remote.PlayerGlobal); json << ",";
  AppendJsonHex64(json, "localWifiCandidate", local.WifiCandidate); json << ",";
  AppendJsonHex64(json, "remoteWifiCandidate", remote.WifiCandidate); json << ",";
  AppendJsonHex64(json, "localRenderCandidate", local.RenderCandidate); json << ",";
  AppendJsonHex64(json, "remoteRenderCandidate", remote.RenderCandidate); json << ",";

  if (latest) {
    AppendJsonHex64(json, "localPlayerGlobalHash0", latest->PlayerGlobalHash0); json << ",";
    AppendJsonHex64(json, "localPlayerGlobalHash1", latest->PlayerGlobalHash1); json << ",";
    AppendJsonHex64(json, "localPlayerActorHash0", latest->PlayerActorHash0); json << ",";
    AppendJsonHex64(json, "localPlayerActorHash1", latest->PlayerActorHash1); json << ",";
    json << "\"latestLocal\":";
    AppendDiagnosticFrameJson(json, *latest);
    json << ",";
  }

  if (remoteSample) {
    json << "\"remotePlayers\":[";
    AppendGameStatePlayerJson(json, *remoteSample, 0);
    json << ",";
    AppendGameStatePlayerJson(json, *remoteSample, 1);
    json << "],";
    if (latest) {
      GameStateModel::GameStateSample localSample;
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

      bool first = true;
      json << "\"remoteSampleDiffs\":[";
      const auto appendDiff = [&](const char *field, melonDS::u32 lhs,
                                  melonDS::u32 rhs) {
        if (lhs == rhs)
          return;
        if (!first)
          json << ",";
        first = false;
        json << "{\"field\":\"" << field << "\",";
        AppendJsonHex32(json, "local", lhs); json << ",";
        AppendJsonHex32(json, "remote", rhs); json << "}";
      };
      appendDiff("stageID", localSample.StageID, remoteSample->StageID);
      appendDiff("stageGroup", localSample.StageGroup, remoteSample->StageGroup);
      appendDiff("player0PosX", localSample.PlayerActor0PosX, remoteSample->PlayerActor0PosX);
      appendDiff("player0PosY", localSample.PlayerActor0PosY, remoteSample->PlayerActor0PosY);
      appendDiff("player0VelX", localSample.PlayerActor0VelX, remoteSample->PlayerActor0VelX);
      appendDiff("player0VelY", localSample.PlayerActor0VelY, remoteSample->PlayerActor0VelY);
      appendDiff("player1PosX", localSample.PlayerActor1PosX, remoteSample->PlayerActor1PosX);
      appendDiff("player1PosY", localSample.PlayerActor1PosY, remoteSample->PlayerActor1PosY);
      appendDiff("player1VelX", localSample.PlayerActor1VelX, remoteSample->PlayerActor1VelX);
      appendDiff("player1VelY", localSample.PlayerActor1VelY, remoteSample->PlayerActor1VelY);
      appendDiff("player0Deaths", localSample.Player0Deaths, remoteSample->Player0Deaths);
      appendDiff("player1Deaths", localSample.Player1Deaths, remoteSample->Player1Deaths);
      appendDiff("player0BattleStars", localSample.Player0BattleStars, remoteSample->Player0BattleStars);
      appendDiff("player1BattleStars", localSample.Player1BattleStars, remoteSample->Player1BattleStars);
      appendDiff("vsCoinCount", localSample.VsCoinCount, remoteSample->VsCoinCount);
      json << "],";
    }
  }
  AppendDiagnosticRingJson(json, ring);
  json << "}";
  return json.str();
}

std::string FormatPlayerLifeEvent(
    const char *role, const char *reason, int instanceID, melonDS::u32 frame,
    int player, const GameStateModel::GameStateSample &sample,
    const std::vector<DiagnosticMovingHazardSnapshot> &nearbyHazards,
    bool includeRing, const std::vector<DiagnosticFrameSnapshot> &ring) {
  std::ostringstream json;
  json << "{\"event\":\"player_life_change\"," << "\"role\":\"" << role
       << "\"," << "\"reason\":\"" << (reason ? reason : "change") << "\","
       << "\"instance\":" << instanceID << "," << "\"frame\":" << frame
       << "," << "\"player\":" << player << "," << "\"stageID\":"
       << sample.StageID << "," << "\"stageGroup\":" << sample.StageGroup
       << "," << "\"scene\":" << sample.SceneCurrentSceneID << ","
       << "\"nextScene\":" << sample.SceneNextSceneID << ","
       << "\"players\":[";
  AppendGameStatePlayerJson(json, sample, 0);
  json << ",";
  AppendGameStatePlayerJson(json, sample, 1);
  json << "],\"nearbyMovingHazards\":[";
  for (std::size_t i = 0; i < nearbyHazards.size(); i++) {
    if (i != 0)
      json << ",";
    const DiagnosticMovingHazardSnapshot &hazard = nearbyHazards[i];
    json << "{\"guid\":" << hazard.GUID << ",";
    AppendJsonHex32(json, "base", hazard.Base); json << ",";
    AppendJsonHex32(json, "x", hazard.PosX); json << ",";
    AppendJsonHex32(json, "y", hazard.PosY); json << ",";
    AppendJsonHex32(json, "velX", hazard.VelX); json << ",";
    AppendJsonHex32(json, "velY", hazard.VelY); json << ",";
    json << "\"stateType\":" << hazard.StateType << ",";
    AppendJsonHex32(json, "flags", hazard.Flags);
    json << "}";
  }
  json << "]";
  if (includeRing) {
    json << ",";
    AppendDiagnosticRingJson(json, ring);
  }
  json << "}";
  return json.str();
}

std::string FormatTestStartupReport(
    std::uint64_t unixMs, const Config::BootstrapConfig &bootstrap,
    const Config::DiagnosticsConfig &diagnostics,
    const Config::HarnessConfig &harness,
    const Config::StateSyncConfig &stateSync,
    const Config::PacketBridgeConfig &packetBridge,
    const Config::MvlConfig &mvl, std::size_t ramDumpRangeCount,
    int currentStage, melonDS::u32 currentSceneSettings) {
  return FormatPrintf(
      "NSMB Test: enabled tUnixMs=%llu frames=%u instances=%d frameBarrier=%d serialRun=%d input=%s hashLog=%s interval=%d screenshotDir=%s screenshotInterval=%d ramDumpDir=%s ramDumpInterval=%d ramDumpRanges=%zu gameStateTrace=%s gameStateTraceInterval=%d stateSync=%d stateApply=%d stateSyncInterval=%d netRandomEnabled=%d netRandomAuto=%d netRandomFrame=%u netRandomValue=0x%08X stateSaveDir=%s stateSaveFrame=%u stateLoadDir=%s stateLoadFrame=%u waitTimeoutMs=%d quitGraceMs=%d inputTrace=%d inputTraceInterval=%d seedWaitMs=%d waitForPeer=%d waitForPeerAtStart=%d deferNetworkUntilStart=%d netplayFrameBarrier=%d packetBridge=%d packetBridgeOnly=%d packetBridgePreGame=%d packetBridgeTrace=%d packetBridgeWait=%d packetBridgeWaitMs=%d packetBridgeWaitStart=%u packetBridgeWaitAhead=%d packetBridgeDirect=%d packetBridgeForceTick=%d packetBridgeForceTickStart=%u packetBridgeMaxTickLead=%d packetBridgeMaxFrameLead=%d packetBridgeThrottleMs=%d packetBridgeThrottleStart=%u mvlStage=%d mvlSceneSettings=0x%08X mvlCourseMode=%s mvlBigStarTarget=%d\n",
      static_cast<unsigned long long>(unixMs), bootstrap.TestFrames,
      bootstrap.TestInstanceCount, harness.FrameBarrierEnabled ? 1 : 0,
      harness.SerialRunEnabled ? 1 : 0,
      harness.InputScriptPath.empty() ? "<none>" : harness.InputScriptPath.c_str(),
      diagnostics.HashLogPath.empty() ? "<none>" : diagnostics.HashLogPath.c_str(),
      bootstrap.HashInterval,
      diagnostics.ScreenshotDir.empty() ? "<none>" : diagnostics.ScreenshotDir.c_str(),
      diagnostics.ScreenshotInterval,
      diagnostics.RamDumpDir.empty() ? "<none>" : diagnostics.RamDumpDir.c_str(),
      diagnostics.RamDumpInterval, ramDumpRangeCount,
      diagnostics.GameStateTracePath.empty()
          ? "<none>"
          : diagnostics.GameStateTracePath.c_str(),
      diagnostics.GameStateTraceInterval, stateSync.GameEnabled ? 1 : 0,
      stateSync.GameApplyEnabled ? 1 : 0, stateSync.GameInterval,
      mvl.NetRandom.Enabled ? 1 : 0,
      mvl.NetRandom.Auto ? 1 : 0, mvl.NetRandom.Frame, mvl.NetRandom.Value,
      harness.StateSaveDir.empty() ? "<none>" : harness.StateSaveDir.c_str(),
      harness.StateSaveFrame,
      harness.StateLoadDir.empty() ? "<none>" : harness.StateLoadDir.c_str(),
      harness.StateLoadFrameSet ? harness.StateLoadFrame : 0,
      bootstrap.WaitTimeoutMs, bootstrap.QuitGraceMs,
      bootstrap.InputTraceEnabled ? 1 : 0, bootstrap.InputTraceInterval,
      harness.SeedWaitTimeoutMs, harness.WaitForPeerBeforeStart ? 1 : 0,
      harness.WaitForPeerAtNetplayStart ? 1 : 0,
      harness.DeferNetworkUntilStart ? 1 : 0,
      harness.NetplayFrameBarrierEnabled ? 1 : 0,
      packetBridge.Enabled ? 1 : 0, packetBridge.Only ? 1 : 0,
      packetBridge.AllowPreGame ? 1 : 0, packetBridge.TraceEnabled ? 1 : 0,
      packetBridge.WaitEnabled ? 1 : 0, packetBridge.WaitTimeoutMs,
      packetBridge.WaitStartFrame, packetBridge.WaitTickAhead,
      packetBridge.DirectCaptureEnabled ? 1 : 0,
      packetBridge.ForceTickEnabled ? 1 : 0,
      packetBridge.ForceTickStartFrame, packetBridge.MaxTickLead,
       packetBridge.MaxFrameLead, packetBridge.ThrottleTimeoutMs,
       packetBridge.ThrottleStartFrame, currentStage, currentSceneSettings,
       mvl.CourseMode.c_str(),
      mvl.BigStarTarget);
}

std::string FormatNetplayStartupReport(
    std::uint64_t unixMs, const char *role,
    const Config::ConnectionConfig &connection,
    const Config::HarnessConfig &harness,
    const Config::PacketBridgeConfig &packetBridge,
    const Config::InputConfig &input, const Config::RollbackConfig &rollback,
    const char *rollbackBackend, const Config::MvlConfig &mvl,
    int currentStage, melonDS::u32 currentSceneSettings) {
  return FormatPrintf(
      "NSMB PoC: enabled tUnixMs=%llu role=%s port=%d peer=%s delay=%d warmup=%d localInstance=%d netplayStartFrame=%u localWait=%d remoteTimeoutFatal=%d waitForPeer=%d waitForPeerAtStart=%d deferNetworkUntilStart=%d netplayFrameBarrier=%d packetBridge=%d packetBridgeOnly=%d packetBridgePreGame=%d packetBridgeTrace=%d packetBridgeWait=%d packetBridgeWaitMs=%d packetBridgeWaitStart=%u packetBridgeWaitAhead=%d packetBridgeDirect=%d packetBridgeForceTick=%d packetBridgeForceTickStart=%u packetBridgeMaxTickLead=%d packetBridgeMaxFrameLead=%d packetBridgeThrottleMs=%d packetBridgeThrottleStart=%u inputNetplayOnly=%d inputNetplayTrace=%d inputHealthTrace=%d inputHealthInterval=%d inputHealthWaitThresholdMs=%d inputMaxFrameLead=%d inputUnreliable=%d inputBundleHistory=%d inputSendDelay=%d inputSendJitter=%d inputSendDelayStart=%u inputSendDelayEnd=%u inputDropModulo=%d inputDropOffset=%d inputDropStart=%u inputDropEnd=%u netPumpThread=%d netPumpSleepUs=%d inputWaitPollUs=%d rollbackInputWaitUs=%d rollback=%d rollbackBackend=%s rollbackWindow=%d rollbackCheckpointInterval=%d rollbackResimDelay=%d rollbackResimulate=%d rollbackRestoreProbe=%d rollbackPredProbeModulo=%d rollbackPredProbeLimit=%d matchSeed=0x%08X seedConfigured=%d mvlStage=%d mvlSceneSettings=0x%08X mvlCourseMode=%s mvlBigStarTarget=%d\n",
      static_cast<unsigned long long>(unixMs), role, connection.Port,
      connection.PeerHost.c_str(), connection.Delay, connection.WarmupFrames,
      connection.LocalInstance, connection.StartFrame,
      connection.LocalWaitsForRemote ? 1 : 0,
      connection.RemoteInputTimeoutFatal ? 1 : 0,
      harness.WaitForPeerBeforeStart ? 1 : 0,
      harness.WaitForPeerAtNetplayStart ? 1 : 0,
      harness.DeferNetworkUntilStart ? 1 : 0,
      harness.NetplayFrameBarrierEnabled ? 1 : 0,
      packetBridge.Enabled ? 1 : 0, packetBridge.Only ? 1 : 0,
      packetBridge.AllowPreGame ? 1 : 0, packetBridge.TraceEnabled ? 1 : 0,
      packetBridge.WaitEnabled ? 1 : 0, packetBridge.WaitTimeoutMs,
      packetBridge.WaitStartFrame, packetBridge.WaitTickAhead,
      packetBridge.DirectCaptureEnabled ? 1 : 0,
      packetBridge.ForceTickEnabled ? 1 : 0,
      packetBridge.ForceTickStartFrame, packetBridge.MaxTickLead,
      packetBridge.MaxFrameLead, packetBridge.ThrottleTimeoutMs,
      packetBridge.ThrottleStartFrame, input.NetplayOnly ? 1 : 0,
      input.NetplayTrace ? 1 : 0, input.HealthTrace ? 1 : 0,
      input.HealthTraceInterval, input.HealthTraceWaitThresholdMs,
      input.MaxFrameLead, input.UseHistoryBundle ? 1 : 0, input.BundleHistory,
      input.SendDelayFrames, input.SendJitterFrames, input.SendDelayStartFrame,
      input.SendDelayEndFrame, input.DropModulo, input.DropOffset,
      input.DropStartFrame, input.DropEndFrame,
      harness.NetworkPumpThreadEnabled ? 1 : 0, harness.NetworkPumpSleepUs,
      input.WaitPollUs, rollback.InputWaitUs, rollback.Enabled ? 1 : 0,
      rollbackBackend, rollback.Window, rollback.CheckpointInterval,
      rollback.ResimulateDelayFrames, rollback.Resimulate ? 1 : 0,
      rollback.RestoreProbe ? 1 : 0, rollback.PredictionProbeModulo,
       rollback.PredictionProbeLimit, mvl.MatchSeed,
       mvl.MatchSeedConfigured ? 1 : 0, currentStage, currentSceneSettings,
       mvl.CourseMode.c_str(),
      mvl.BigStarTarget);
}

std::string FormatImitationModelInitializationReport(
    const std::string &modelPath,
    const NsmbImitationAI::ModelInitializationResult &result) {
  if (!result.RequestedEnabled || result.Loaded)
    return {};
  if (result.ModelPathEmpty) {
    return "NSMB ImitationAI: enabled but "
           "MELONDS_NSML_IMITATION_AI_MODEL is empty\n";
  }
  return FormatPrintf(
      "NSMB ImitationAI: failed to load model path=%s "
      "torchCompactError=%s compactError=%s linearError=%s\n",
      modelPath.c_str(), result.Errors.TorchCompact.c_str(),
      result.Errors.Compact.c_str(), result.Errors.Linear.c_str());
}

std::string FormatAIStartupReport(
    const Config::AIConfig &ai, bool imitationEnabled,
    const NsmbImitationAI::ModelDescription &model) {
  std::string report;
  if (ai.Rule.Enabled) {
    report += FormatPrintf(
        "NSMB RuleAI: enabled player=%s startFrame=%u deadzone=0x%X "
        "wrapWidth=0x%X closeRange=0x%X hazardRange=0x%X/0x%X "
        "jump=%d/%d trace=%d traceInterval=%d\n",
        ai.Rule.PlayerSpec.c_str(), ai.Rule.StartFrame,
        ai.Rule.HorizontalDeadzone, ai.Rule.HorizontalWrapWidth,
        ai.Rule.CloseRange, ai.Rule.HazardHorizontalRange,
        ai.Rule.HazardVerticalRange, ai.Rule.JumpFrames,
        ai.Rule.JumpInterval, ai.Rule.TraceEnabled ? 1 : 0,
        ai.Rule.TraceInterval);
    if (ai.Rule.HostOnly || ai.Rule.ClientOnly) {
      report += FormatPrintf(
          "NSMB RuleAI: roleFilter hostOnly=%d clientOnly=%d\n",
          ai.Rule.HostOnly ? 1 : 0, ai.Rule.ClientOnly ? 1 : 0);
    }
  }

  if (!imitationEnabled || model.Type == NsmbImitationAI::ModelType::None)
    return report;

  switch (model.Type) {
  case NsmbImitationAI::ModelType::TorchCompact:
    report += FormatPrintf(
        "NSMB ImitationAI: enabled player=%s startFrame=%u "
        "modelType=torchCompact allowedHeldMask=0x%03X trace=%d "
        "traceInterval=%d inferInterval=%d neutralHoldFrames=%d model=%s "
        "features=%zu heads=%zu schema=%s labelSchema=%s\n",
        ai.Imitation.PlayerSpec.c_str(), ai.Imitation.StartFrame,
        ai.Imitation.AllowedHeldMask, ai.Imitation.TraceEnabled ? 1 : 0,
        ai.Imitation.TraceInterval, ai.Imitation.InferInterval,
        ai.Imitation.NeutralHoldFrames, ai.Imitation.ModelPath.c_str(),
        model.FeatureCount, model.OutputCount, model.Schema.c_str(),
        model.DetailSchema.c_str());
    break;
  case NsmbImitationAI::ModelType::Compact:
    report += FormatPrintf(
        "NSMB ImitationAI: enabled player=%s startFrame=%u "
        "modelType=compact allowedHeldMask=0x%03X trace=%d "
        "traceInterval=%d model=%s features=%zu heads=%zu schema=%s "
        "labelSchema=%s\n",
        ai.Imitation.PlayerSpec.c_str(), ai.Imitation.StartFrame,
        ai.Imitation.AllowedHeldMask, ai.Imitation.TraceEnabled ? 1 : 0,
        ai.Imitation.TraceInterval, ai.Imitation.ModelPath.c_str(),
        model.FeatureCount, model.OutputCount, model.Schema.c_str(),
        model.DetailSchema.c_str());
    break;
  case NsmbImitationAI::ModelType::Linear:
    report += FormatPrintf(
        "NSMB ImitationAI: enabled player=%s startFrame=%u "
        "modelType=linear threshold=%.3f allowedHeldMask=0x%03X trace=%d "
        "traceInterval=%d model=%s features=%zu buttons=%zu schema=%s "
        "featureSchema=%s\n",
        ai.Imitation.PlayerSpec.c_str(), ai.Imitation.StartFrame,
        ai.Imitation.Threshold, ai.Imitation.AllowedHeldMask,
        ai.Imitation.TraceEnabled ? 1 : 0, ai.Imitation.TraceInterval,
        ai.Imitation.ModelPath.c_str(), model.FeatureCount,
        model.OutputCount, model.Schema.c_str(), model.DetailSchema.c_str());
    break;
  case NsmbImitationAI::ModelType::None:
    break;
  }
  if (ai.Imitation.HostOnly || ai.Imitation.ClientOnly) {
    report += FormatPrintf(
        "NSMB ImitationAI: roleFilter hostOnly=%d clientOnly=%d\n",
        ai.Imitation.HostOnly ? 1 : 0, ai.Imitation.ClientOnly ? 1 : 0);
  }
  report += FormatPrintf(
      "NSMB ImitationAI: hazardGuard enabled=%d horizontalRange=0x%X "
      "verticalRange=0x%X closeRange=0x%X\n",
      ai.Imitation.HazardGuardEnabled ? 1 : 0,
      ai.Imitation.HazardGuardHorizontalRange,
      ai.Imitation.HazardGuardVerticalRange,
      ai.Imitation.HazardGuardCloseRange);
  return report;
}

bool ShouldCaptureRamDumpFrame(
    melonDS::u32 frame, int interval,
    const std::vector<std::pair<melonDS::u32, melonDS::u32>> &ranges) {
  if (interval > 0 &&
      (frame % static_cast<melonDS::u32>(interval)) == 0) {
    return true;
  }
  return std::any_of(ranges.begin(), ranges.end(), [frame](const auto &range) {
    return frame >= range.first && frame <= range.second;
  });
}

bool ShouldCaptureScreenshotFrame(const Config::DiagnosticsConfig &config,
                                  melonDS::u32 frame) {
  return !config.ScreenshotDir.empty() && config.ScreenshotInterval > 0 &&
         (frame % static_cast<melonDS::u32>(config.ScreenshotInterval)) == 0;
}

void CaptureScreenshot(const Config::DiagnosticsConfig &config,
                       int instanceID, melonDS::u32 frame,
                       const ScreenshotFrame &screenshot) {
  if (!screenshot.FramebufferAvailable) {
    if (config.ScreenshotRegisterTrace) {
      std::printf("NSMB Test: screenshot skipped inst=%d frame=%u "
                  "reason=no-framebuffer\n",
                  instanceID, frame);
      std::fflush(stdout);
    }
    return;
  }
  if (!screenshot.TopBuffer || !screenshot.BottomBuffer) {
    if (config.ScreenshotRegisterTrace) {
      std::printf("NSMB Test: screenshot skipped inst=%d frame=%u "
                  "reason=null-buffer top=%p bottom=%p\n",
                  instanceID, frame, screenshot.TopBuffer,
                  screenshot.BottomBuffer);
      std::fflush(stdout);
    }
    return;
  }

  std::error_code error;
  std::filesystem::create_directories(config.ScreenshotDir, error);
  if (error) {
    std::printf("NSMB Test: failed to create screenshot dir: %s (%s)\n",
                config.ScreenshotDir.c_str(), error.message().c_str());
    return;
  }

  QImage image(256, 384, QImage::Format_RGB32);
  std::memcpy(image.scanLine(0), screenshot.TopBuffer, 256 * 192 * 4);
  std::memcpy(image.scanLine(192), screenshot.BottomBuffer, 256 * 192 * 4);

  int blackPixels = 0;
  int brightPixels = 0;
  for (int y = 0; y < image.height(); y += 4) {
    const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
    for (int x = 0; x < image.width(); x += 4) {
      const QRgb pixel = row[x];
      const int red = qRed(pixel);
      const int green = qGreen(pixel);
      const int blue = qBlue(pixel);
      if (red <= 2 && green <= 2 && blue <= 2)
        blackPixels++;
      if (red >= 24 || green >= 24 || blue >= 24)
        brightPixels++;
    }
  }
  if (blackPixels > 6100 && brightPixels == 0) {
    std::printf(
        "NSMB Test: black framebuffer inst=%d frame=%u dispcntA=0x%08X "
        "dispcntB=0x%08X dispstat=0x%04X powcnt1=0x%04X "
        "bldcntA=0x%04X bldyA=0x%04X bldcntB=0x%04X bldyB=0x%04X "
        "netState=0x%02X netFlags=0x%04X\n",
        instanceID, frame, screenshot.DisplayControlA,
        screenshot.DisplayControlB, screenshot.DisplayStatus,
        screenshot.PowerControl, screenshot.BlendControlA,
        screenshot.BlendY_A, screenshot.BlendControlB, screenshot.BlendY_B,
        screenshot.NetState, screenshot.NetFlags);
    std::fflush(stdout);
  } else if (config.ScreenshotRegisterTrace) {
    std::printf(
        "NSMB Test: screenshot regs inst=%d frame=%u dispcntA=0x%08X "
        "dispcntB=0x%08X bldcntA=0x%04X bldyA=0x%04X "
        "bldcntB=0x%04X bldyB=0x%04X netState=0x%02X netFlags=0x%04X "
        "blackSample=%d brightSample=%d\n",
        instanceID, frame, screenshot.DisplayControlA,
        screenshot.DisplayControlB, screenshot.BlendControlA,
        screenshot.BlendY_A, screenshot.BlendControlB, screenshot.BlendY_B,
        screenshot.NetState, screenshot.NetFlags, blackPixels, brightPixels);
    std::fflush(stdout);
  }

  char filename[256];
  std::snprintf(filename, sizeof(filename), "inst%d_frame%06u.png", instanceID,
                frame);
  const std::filesystem::path path =
      std::filesystem::path(config.ScreenshotDir) / filename;
  if (!image.save(QString::fromStdWString(path.wstring()))) {
    std::printf("NSMB Test: failed to save screenshot: %ls\n", path.c_str());
  }
}

void CaptureRamDumpIfNeeded(
    const Config::DiagnosticsConfig &config,
    const std::vector<std::pair<melonDS::u32, melonDS::u32>> &ranges,
    int instanceID, melonDS::u32 frame, const melonDS::u8 *mainRAM,
    melonDS::u32 mainRAMLength) {
  if (!mainRAM || mainRAMLength == 0 || config.RamDumpDir.empty() ||
      !ShouldCaptureRamDumpFrame(frame, config.RamDumpInterval, ranges)) {
    return;
  }

  std::error_code error;
  std::filesystem::create_directories(config.RamDumpDir, error);
  if (error) {
    std::printf("NSMB Test: failed to create RAM dump dir: %s (%s)\n",
                config.RamDumpDir.c_str(), error.message().c_str());
    return;
  }

  char filename[256];
  std::snprintf(filename, sizeof(filename), "inst%d_frame%06u_mainram.bin",
                instanceID, frame);
  const std::filesystem::path path =
      std::filesystem::path(config.RamDumpDir) / filename;
  const melonDS::u32 length = std::min<melonDS::u32>(mainRAMLength, 0x400000);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    std::printf("NSMB Test: failed to open RAM dump for write: %ls\n",
                path.c_str());
    return;
  }
  file.write(reinterpret_cast<const char *>(mainRAM), length);
  if (!file) {
    std::printf("NSMB Test: failed to write RAM dump: %ls\n", path.c_str());
  }
}

struct Runtime::Impl {
  struct ActivePerformanceState {
    std::atomic<bool> TimerStarted{false};
    melonDS::u32 TimerStartFrame = 0;
    TimePoint TimerStart;
    bool FrameTimingStarted = false;
    TimePoint LastFrameTime;
    melonDS::u32 Samples = 0;
    std::uint64_t TotalUs = 0;
    std::uint64_t MaxUs = 0;
    melonDS::u32 MaxFrame = 0;
    melonDS::u32 Over16ms = 0;
    melonDS::u32 Over25ms = 0;
    melonDS::u32 Over33ms = 0;
    melonDS::u32 LastSpikeRollbackRestoreCount = 0;
    melonDS::u32 LastSpikeRollbackResimulateCount = 0;
  };

  Config::DiagnosticsConfig Config;
  bool Host = true;
  std::ofstream WatchdogLog;
  std::ofstream PhaseEventsLog;
  std::mutex LogMutex;
  std::atomic<bool> WatchdogStop{false};
  bool WatchdogThreadStarted = false;
  std::thread WatchdogThread;
  int FrameHeartbeatInterval = 0;
  std::array<melonDS::u32, 16> LastFrameHeartbeat{};
  std::ofstream FrameHeartbeat;
  std::atomic<melonDS::u32> PendingFrameHeartbeat{0};
  std::atomic<bool> FrameHeartbeatStop{false};
  bool FrameHeartbeatThreadStarted = false;
  std::thread FrameHeartbeatThread;
  mutable std::mutex PerformanceMutex;
  bool TestTimerStarted = false;
  TimePoint TestTimerStart;
  std::array<ActivePerformanceState, 16> ActivePerformance{};
  std::array<melonDS::u32, 16> LastGameplayHeartbeat{};
  std::mutex HashLogMutex;
  std::ofstream HashLog;
  bool ScreenHashEnabled = false;
  std::array<melonDS::u64, 16> LastHashFrame{};
  std::mutex DiagnosticEventMutex;
  std::ofstream DiagnosticEventLog;
  std::string DiagnosticEventPath;
  struct DiagnosticInstanceState {
    melonDS::u32 PostTriggerUntilFrame = 0;
    melonDS::u32 LastMismatchFrame = 0;
    melonDS::u32 LastLifeEventFrame[2]{};
    melonDS::u32 LastPitTransitionFrame[2]{};
    melonDS::u32 LastPositionAnomalyFrame[2]{};
    std::array<DiagnosticFrameSnapshot, kDiagnosticRingCapacity> Ring{};
    std::size_t RingNext = 0;
    bool HasPlayerLifeState = false;
    PlayerLifeState LastPlayerLifeState;
    std::array<bool, static_cast<std::size_t>(RuntimePatchLogKind::Count)>
        RuntimePatchLogged{};
  };
  mutable std::mutex DiagnosticStateMutex;
  std::array<DiagnosticInstanceState, 16> DiagnosticState{};
  std::atomic<const char *> Phase{"startup"};
  std::atomic<const char *> Event{"startup"};
  std::atomic<std::uint64_t> PhaseUnixMs{0};
  std::atomic<std::uint64_t> LastDumpUnixMs{0};
  std::atomic<int> Instance{-1};
  std::atomic<melonDS::u32> Frame{0};
  std::atomic<melonDS::u32> LogicalFrame{0};
  std::atomic<melonDS::u32> SendFrame{0};
  std::atomic<melonDS::u32> RemoteWaitTarget{0};
  std::atomic<int> RemoteWaitActive{0};
  std::atomic<std::uint64_t> RemoteWaitStartUnixMs{0};
  std::atomic<std::uint64_t> RemoteWaitProgressUnixMs{0};
  std::atomic<melonDS::u32> LastSentFrame{0};
  std::atomic<melonDS::u32> LastRecvFrame{0};
  std::atomic<int> Lead{0};
  std::atomic<std::size_t> LocalQueue{0};
  std::atomic<std::size_t> RemoteQueue{0};
  std::atomic<std::size_t> DelayedQueue{0};
  std::atomic<int> PeerState{-1};
  std::atomic<int> ConnectingPeerState{-1};
  std::atomic<std::uint64_t> LastENetSendUnixMs{0};
  std::atomic<std::uint64_t> LastENetRecvUnixMs{0};
  std::atomic<int> LastENetSendResult{0};
  std::atomic<int> LastENetServiceResult{0};
  std::atomic<int> LastENetEventType{0};
  std::atomic<melonDS::u32> LastENetEventData{0};
  std::atomic<std::size_t> LastENetSendBytes{0};
  std::atomic<melonDS::u32> Arm9PC{0};
  std::atomic<melonDS::u32> Arm9LR{0};
  std::atomic<melonDS::u32> Arm9SP{0};
  std::atomic<melonDS::u32> Arm9CPSR{0};
  std::atomic<melonDS::u32> StageID{0};
  std::atomic<melonDS::u32> StageGroup{0};
  std::atomic<melonDS::u32> VsMode{0};
  std::atomic<melonDS::u32> NetState14{0};
  std::atomic<melonDS::u32> NetState1C{0};
  std::atomic<melonDS::u32> NetState20{0};
  std::atomic<melonDS::u32> NetState24{0};
  std::atomic<melonDS::u32> NetState5C{0};
  std::atomic<melonDS::u32> NetPacketTick{0};
  std::atomic<melonDS::u32> AppFrameLength{0};
  std::atomic<melonDS::u32> AppUpdateTask{0};
  std::atomic<melonDS::u32> AppSleeping{0};
  std::atomic<melonDS::u32> StageSceneState{0};
  std::atomic<melonDS::u32> Player0Transition{0};
  std::atomic<melonDS::u32> Player1Transition{0};
  std::atomic<std::uint64_t> GameSnapshotUnixMs{0};

  void WritePhaseEvent(std::uint64_t now, const char *event, const char *phase,
                       int instanceID, melonDS::u32 frame,
                       melonDS::u32 logicalFrame, melonDS::u32 sendFrame) {
    if (!EnsureLogOpen(PhaseEventsLog, Config.HangPhaseEventsPath))
      return;

    PhaseEventsLog << "{\"tUnixMs\":" << now << ",\"event\":\""
                   << (event ? event : "phase") << "\",\"phase\":\""
                   << (phase ? phase : "unknown")
                   << "\",\"instance\":" << instanceID << ",\"frame\":" << frame
                   << ",\"logicalFrame\":" << logicalFrame
                   << ",\"sendFrame\":" << sendFrame << ",\"lastSent\":"
                   << LastSentFrame.load(std::memory_order_acquire)
                   << ",\"lastRecv\":"
                   << LastRecvFrame.load(std::memory_order_acquire)
                   << ",\"lead\":" << Lead.load(std::memory_order_acquire)
                   << ",\"remoteWaitActive\":"
                   << RemoteWaitActive.load(std::memory_order_acquire)
                   << ",\"remoteWaitTarget\":"
                   << RemoteWaitTarget.load(std::memory_order_acquire) << "}\n";
    PhaseEventsLog.flush();
  }

  void RunWatchdog() {
    while (!WatchdogStop.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(
          std::max(100, Config.HangWatchdogIntervalMs)));

      const std::uint64_t now = NowUnixMs();
      const std::uint64_t phaseUnixMs =
          PhaseUnixMs.load(std::memory_order_acquire);
      const std::uint64_t phaseAgeMs =
          phaseUnixMs == 0 || now < phaseUnixMs ? 0 : now - phaseUnixMs;
      const bool stalled =
          Config.HangThresholdMs > 0 &&
          phaseAgeMs >= static_cast<std::uint64_t>(Config.HangThresholdMs);
      bool dumpWritten = false;
      if (stalled && !Config.HangDumpPath.empty() &&
          LastDumpUnixMs.load(std::memory_order_acquire) == 0) {
        LastDumpUnixMs.store(now, std::memory_order_release);
        dumpWritten = WriteMiniDump(Config.HangDumpPath);
      }

      std::lock_guard<std::mutex> lock(LogMutex);
      if (!EnsureLogOpen(WatchdogLog, Config.HangWatchdogPath))
        continue;

      WatchdogLog
          << "{\"tUnixMs\":" << now << ",\"event\":\"watchdog\""
          << ",\"role\":\"" << (Host ? "host" : "client") << "\""
          << ",\"phase\":\"" << Phase.load(std::memory_order_acquire)
          << "\",\"phaseEvent\":\"" << Event.load(std::memory_order_acquire)
          << "\",\"phaseAgeMs\":" << phaseAgeMs
          << ",\"stalled\":" << (stalled ? 1 : 0)
          << ",\"dumpWritten\":" << (dumpWritten ? 1 : 0)
          << ",\"instance\":" << Instance.load(std::memory_order_acquire)
          << ",\"frame\":" << Frame.load(std::memory_order_acquire)
          << ",\"logicalFrame\":"
          << LogicalFrame.load(std::memory_order_acquire)
          << ",\"sendFrame\":" << SendFrame.load(std::memory_order_acquire)
          << ",\"lastSent\":" << LastSentFrame.load(std::memory_order_acquire)
          << ",\"lastRecv\":" << LastRecvFrame.load(std::memory_order_acquire)
          << ",\"lead\":" << Lead.load(std::memory_order_acquire)
          << ",\"localQueue\":" << LocalQueue.load(std::memory_order_acquire)
          << ",\"remoteQueue\":" << RemoteQueue.load(std::memory_order_acquire)
          << ",\"delayedQueue\":"
          << DelayedQueue.load(std::memory_order_acquire)
          << ",\"remoteWaitActive\":"
          << RemoteWaitActive.load(std::memory_order_acquire)
          << ",\"remoteWaitTarget\":"
          << RemoteWaitTarget.load(std::memory_order_acquire)
          << ",\"remoteWaitStartUnixMs\":"
          << RemoteWaitStartUnixMs.load(std::memory_order_acquire)
          << ",\"remoteWaitProgressUnixMs\":"
          << RemoteWaitProgressUnixMs.load(std::memory_order_acquire)
          << ",\"peerState\":" << PeerState.load(std::memory_order_acquire)
          << ",\"connectingPeerState\":"
          << ConnectingPeerState.load(std::memory_order_acquire)
          << ",\"lastENetSendUnixMs\":"
          << LastENetSendUnixMs.load(std::memory_order_acquire)
          << ",\"lastENetRecvUnixMs\":"
          << LastENetRecvUnixMs.load(std::memory_order_acquire)
          << ",\"lastENetSendResult\":"
          << LastENetSendResult.load(std::memory_order_acquire)
          << ",\"lastENetServiceResult\":"
          << LastENetServiceResult.load(std::memory_order_acquire)
          << ",\"lastENetEventType\":"
          << LastENetEventType.load(std::memory_order_acquire)
          << ",\"lastENetEventData\":"
          << LastENetEventData.load(std::memory_order_acquire)
          << ",\"lastENetSendBytes\":"
          << LastENetSendBytes.load(std::memory_order_acquire)
          << ",\"arm9PC\":\"0x" << std::hex
          << Arm9PC.load(std::memory_order_acquire) << "\",\"arm9LR\":\"0x"
          << Arm9LR.load(std::memory_order_acquire) << "\",\"arm9SP\":\"0x"
          << Arm9SP.load(std::memory_order_acquire) << "\",\"arm9CPSR\":\"0x"
          << Arm9CPSR.load(std::memory_order_acquire) << "\",\"stageID\":\"0x"
          << StageID.load(std::memory_order_acquire) << "\",\"stageGroup\":\"0x"
          << StageGroup.load(std::memory_order_acquire) << "\",\"vsMode\":\"0x"
          << VsMode.load(std::memory_order_acquire) << "\",\"netState14\":\"0x"
          << NetState14.load(std::memory_order_acquire)
          << "\",\"netState1C\":\"0x"
          << NetState1C.load(std::memory_order_acquire)
          << "\",\"netState20\":\"0x"
          << NetState20.load(std::memory_order_acquire)
          << "\",\"netState24\":\"0x"
          << NetState24.load(std::memory_order_acquire)
          << "\",\"netState5C\":\"0x"
          << NetState5C.load(std::memory_order_acquire)
          << "\",\"netPacketTick\":\"0x"
          << NetPacketTick.load(std::memory_order_acquire)
          << "\",\"appFrameLength\":\"0x"
          << AppFrameLength.load(std::memory_order_acquire)
          << "\",\"appUpdateTask\":\"0x"
          << AppUpdateTask.load(std::memory_order_acquire)
          << "\",\"appSleeping\":\"0x"
          << AppSleeping.load(std::memory_order_acquire)
          << "\",\"stageSceneState\":\"0x"
          << StageSceneState.load(std::memory_order_acquire)
          << "\",\"player0Transition\":\"0x"
          << Player0Transition.load(std::memory_order_acquire)
          << "\",\"player1Transition\":\"0x"
          << Player1Transition.load(std::memory_order_acquire)
          << "\",\"gameSnapshotUnixMs\":" << std::dec
          << GameSnapshotUnixMs.load(std::memory_order_acquire) << "}\n";
      WatchdogLog.flush();
    }
  }

  void RunFrameHeartbeat() {
    melonDS::u32 writtenFrame = 0;
    while (!FrameHeartbeatStop.load(std::memory_order_acquire)) {
      const melonDS::u32 frame =
          PendingFrameHeartbeat.load(std::memory_order_acquire);
      if (frame != 0 && frame != writtenFrame && FrameHeartbeat) {
        FrameHeartbeat << frame << '\n';
        FrameHeartbeat.flush();
        writtenFrame = frame;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  void StopFrameHeartbeat() {
    if (FrameHeartbeatThreadStarted) {
      FrameHeartbeatStop.store(true, std::memory_order_release);
      if (FrameHeartbeatThread.joinable())
        FrameHeartbeatThread.join();
      FrameHeartbeatThreadStarted = false;
    }
    if (FrameHeartbeat.is_open())
      FrameHeartbeat.close();
  }
};

Runtime::Runtime() : State(std::make_unique<Impl>()) {}

Runtime::~Runtime() { Stop(); }

bool Runtime::ConfigureFrameHeartbeat(int interval, const std::string &path) {
  State->FrameHeartbeatInterval = std::max(0, interval);
  if (path.empty())
    return false;

  State->FrameHeartbeat.open(path, std::ios::out | std::ios::trunc);
  if (!State->FrameHeartbeat)
    return false;

  State->FrameHeartbeatStop.store(false, std::memory_order_release);
  State->FrameHeartbeatThreadStarted = true;
  State->FrameHeartbeatThread =
      std::thread([this] { State->RunFrameHeartbeat(); });
  return true;
}

bool Runtime::PublishFrameHeartbeat(int instanceID, melonDS::u32 frame,
                                    bool active) {
  if (State->FrameHeartbeatInterval <= 0 || !active || instanceID < 0 ||
      instanceID >= static_cast<int>(State->LastFrameHeartbeat.size()) ||
      frame == State->LastFrameHeartbeat[instanceID] ||
      (frame % static_cast<melonDS::u32>(State->FrameHeartbeatInterval)) != 0) {
    return false;
  }

  State->LastFrameHeartbeat[instanceID] = frame;
  std::printf("NSMB Heartbeat: inst=%d frame=%u\n", instanceID, frame);
  if (State->FrameHeartbeat)
    State->PendingFrameHeartbeat.store(frame, std::memory_order_release);
  else
    std::fflush(stdout);
  return true;
}

bool Runtime::ConfigureHashLog(const std::string &path,
                               bool screenHashEnabled) {
  std::lock_guard<std::mutex> lock(State->HashLogMutex);
  if (State->HashLog.is_open())
    State->HashLog.close();
  State->HashLog.clear();
  State->ScreenHashEnabled = screenHashEnabled;
  State->LastHashFrame.fill(0);
  if (path.empty())
    return true;

  State->HashLog.open(path, std::ios::out | std::ios::trunc);
  if (!State->HashLog)
    return false;
  State->HashLog << (screenHashEnabled
                         ? "instance,frame,hash,screenHash\n"
                         : "instance,frame,hash\n");
  return true;
}

bool Runtime::RecordFrameHash(int instanceID, melonDS::u32 frame,
                              melonDS::u64 stateHash,
                              melonDS::u64 screenHash) {
  std::lock_guard<std::mutex> lock(State->HashLogMutex);
  if (instanceID < 0 ||
      instanceID >= static_cast<int>(State->LastHashFrame.size()) ||
      State->LastHashFrame[instanceID] == frame) {
    return false;
  }
  State->LastHashFrame[instanceID] = frame;

  if (State->ScreenHashEnabled) {
    std::printf("NSMB PoC: inst=%d frame=%u hash=%016llX screen=%016llX\n",
                instanceID, frame,
                static_cast<unsigned long long>(stateHash),
                static_cast<unsigned long long>(screenHash));
  } else {
    std::printf("NSMB PoC: inst=%d frame=%u hash=%016llX\n", instanceID,
                frame, static_cast<unsigned long long>(stateHash));
  }

  if (State->HashLog) {
    State->HashLog << instanceID << ',' << frame << ',' << std::hex
                   << stateHash;
    if (State->ScreenHashEnabled)
      State->HashLog << ',' << screenHash;
    State->HashLog << std::dec << '\n';
    State->HashLog.flush();
  }
  return true;
}

bool Runtime::WriteDiagnosticEvent(const std::string &path,
                                   const std::string &json) {
  std::lock_guard<std::mutex> lock(State->DiagnosticEventMutex);
  if (path.empty())
    return false;

  if (State->DiagnosticEventLog.is_open() &&
      State->DiagnosticEventPath != path) {
    State->DiagnosticEventLog.close();
  }
  if (!State->DiagnosticEventLog.is_open()) {
    State->DiagnosticEventLog.clear();
    const std::filesystem::path eventPath(path);
    std::error_code error;
    if (eventPath.has_parent_path())
      std::filesystem::create_directories(eventPath.parent_path(), error);
    State->DiagnosticEventLog.open(
        eventPath, std::ios::out | std::ios::app | std::ios::binary);
    if (!State->DiagnosticEventLog) {
      std::printf("NSMB Diagnostics: failed to open event log: %s\n",
                  eventPath.string().c_str());
      std::fflush(stdout);
      return false;
    }
    State->DiagnosticEventPath = path;
  }

  State->DiagnosticEventLog << json << '\n';
  State->DiagnosticEventLog.flush();
  return static_cast<bool>(State->DiagnosticEventLog);
}

void Runtime::StartTestTimer(TimePoint now) {
  std::lock_guard<std::mutex> lock(State->PerformanceMutex);
  if (State->TestTimerStarted)
    return;
  State->TestTimerStarted = true;
  State->TestTimerStart = now;
}

std::int64_t Runtime::TestElapsedMs(TimePoint now) const {
  std::lock_guard<std::mutex> lock(State->PerformanceMutex);
  if (!State->TestTimerStarted)
    return 0;
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             now - State->TestTimerStart)
      .count();
}

bool Runtime::StartActiveTimer(int instanceID, melonDS::u32 frame,
                               TimePoint now) {
  if (instanceID < 0 ||
      instanceID >= static_cast<int>(State->ActivePerformance.size())) {
    return false;
  }
  Impl::ActivePerformanceState &active =
      State->ActivePerformance[instanceID];
  if (active.TimerStarted.load(std::memory_order_acquire))
    return false;
  std::lock_guard<std::mutex> lock(State->PerformanceMutex);
  if (active.TimerStarted.load(std::memory_order_relaxed))
    return false;
  active.TimerStartFrame = frame;
  active.TimerStart = now;
  active.TimerStarted.store(true, std::memory_order_release);
  return true;
}

bool Runtime::IsActiveTimerStarted(int instanceID) const {
  return instanceID >= 0 &&
         instanceID < static_cast<int>(State->ActivePerformance.size()) &&
         State->ActivePerformance[instanceID].TimerStarted.load(
             std::memory_order_acquire);
}

Runtime::ActiveFrameSample Runtime::RecordActiveFrameTiming(
    int instanceID, melonDS::u32 frame, TimePoint now, bool traceSpikes,
    std::uint64_t spikeThresholdUs, melonDS::u32 rollbackRestoreCount,
    melonDS::u32 rollbackResimulateCount) {
  std::lock_guard<std::mutex> lock(State->PerformanceMutex);
  ActiveFrameSample sample;
  if (instanceID < 0 ||
      instanceID >= static_cast<int>(State->ActivePerformance.size())) {
    return sample;
  }

  Impl::ActivePerformanceState &active =
      State->ActivePerformance[instanceID];
  if (!active.TimerStarted.load(std::memory_order_acquire))
    return sample;
  if (!active.FrameTimingStarted) {
    active.FrameTimingStarted = true;
    active.LastFrameTime = now;
    return sample;
  }

  const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                           now - active.LastFrameTime)
                           .count();
  active.LastFrameTime = now;
  if (elapsed <= 0)
    return sample;

  sample.Recorded = true;
  sample.ElapsedUs = static_cast<std::uint64_t>(elapsed);
  active.Samples++;
  active.TotalUs += sample.ElapsedUs;
  if (sample.ElapsedUs > active.MaxUs) {
    active.MaxUs = sample.ElapsedUs;
    active.MaxFrame = frame;
  }
  if (sample.ElapsedUs > 16667)
    active.Over16ms++;
  if (sample.ElapsedUs > 25000)
    active.Over25ms++;
  if (sample.ElapsedUs > 33334)
    active.Over33ms++;

  sample.Spike = traceSpikes && sample.ElapsedUs >= spikeThresholdUs;
  if (sample.Spike) {
    sample.RollbackRestoreDelta =
        rollbackRestoreCount - active.LastSpikeRollbackRestoreCount;
    sample.RollbackResimulateDelta =
        rollbackResimulateCount - active.LastSpikeRollbackResimulateCount;
    active.LastSpikeRollbackRestoreCount = rollbackRestoreCount;
    active.LastSpikeRollbackResimulateCount = rollbackResimulateCount;
  }
  return sample;
}

Runtime::ActiveFrameSummary Runtime::ActiveFrameTimingSummary(
    int instanceID, melonDS::u32 endFrame, TimePoint now) const {
  std::lock_guard<std::mutex> lock(State->PerformanceMutex);
  ActiveFrameSummary summary;
  if (instanceID < 0 ||
      instanceID >= static_cast<int>(State->ActivePerformance.size())) {
    return summary;
  }

  const Impl::ActivePerformanceState &active =
      State->ActivePerformance[instanceID];
  if (!active.TimerStarted.load(std::memory_order_acquire))
    return summary;
  summary.Started = true;
  summary.StartFrame = active.TimerStartFrame;
  summary.Frames = endFrame > active.TimerStartFrame
                       ? endFrame - active.TimerStartFrame
                       : 0;
  summary.ElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now - active.TimerStart)
                          .count();
  summary.Samples = active.Samples;
  summary.TotalUs = active.TotalUs;
  summary.MaxUs = active.MaxUs;
  summary.MaxFrame = active.MaxFrame;
  summary.Over16ms = active.Over16ms;
  summary.Over25ms = active.Over25ms;
  summary.Over33ms = active.Over33ms;
  return summary;
}

bool Runtime::ShouldTraceGameplayHeartbeat(int instanceID, melonDS::u32 frame,
                                           melonDS::u32 startFrame,
                                           int interval) {
  if (interval <= 0 || instanceID < 0 ||
      instanceID >= static_cast<int>(State->LastGameplayHeartbeat.size()) ||
      frame < startFrame ||
      (frame % static_cast<melonDS::u32>(interval)) != 0) {
    return false;
  }
  std::lock_guard<std::mutex> lock(State->PerformanceMutex);
  if (frame == State->LastGameplayHeartbeat[instanceID])
    return false;
  State->LastGameplayHeartbeat[instanceID] = frame;
  return true;
}

std::optional<DiagnosticFrameSnapshot>
Runtime::LatestDiagnosticSnapshot(int instanceID) const {
  std::lock_guard<std::mutex> lock(State->DiagnosticStateMutex);
  if (instanceID < 0 ||
      instanceID >= static_cast<int>(State->DiagnosticState.size())) {
    return std::nullopt;
  }
  const Impl::DiagnosticInstanceState &diagnostic =
      State->DiagnosticState[instanceID];
  if (diagnostic.RingNext == 0)
    return std::nullopt;
  const std::size_t index =
      (diagnostic.RingNext + kDiagnosticRingCapacity - 1) %
      kDiagnosticRingCapacity;
  if (!diagnostic.Ring[index].Valid)
    return std::nullopt;
  return diagnostic.Ring[index];
}

void Runtime::RecordDiagnosticSnapshot(
    int instanceID, const DiagnosticFrameSnapshot &snapshot) {
  std::lock_guard<std::mutex> lock(State->DiagnosticStateMutex);
  if (instanceID < 0 ||
      instanceID >= static_cast<int>(State->DiagnosticState.size())) {
    return;
  }
  Impl::DiagnosticInstanceState &diagnostic =
      State->DiagnosticState[instanceID];
  diagnostic.Ring[diagnostic.RingNext % kDiagnosticRingCapacity] = snapshot;
  diagnostic.RingNext =
      (diagnostic.RingNext + 1) % kDiagnosticRingCapacity;
}

std::vector<DiagnosticFrameSnapshot>
Runtime::DiagnosticSnapshotWindow(int instanceID,
                                  std::size_t frameCount) const {
  std::lock_guard<std::mutex> lock(State->DiagnosticStateMutex);
  std::vector<DiagnosticFrameSnapshot> snapshots;
  if (instanceID < 0 ||
      instanceID >= static_cast<int>(State->DiagnosticState.size())) {
    return snapshots;
  }
  frameCount = std::clamp<std::size_t>(frameCount, 1,
                                      kDiagnosticRingCapacity);
  const Impl::DiagnosticInstanceState &diagnostic =
      State->DiagnosticState[instanceID];
  snapshots.reserve(frameCount);
  for (std::size_t offset = 0; offset < frameCount; offset++) {
    const std::size_t index =
        (diagnostic.RingNext + kDiagnosticRingCapacity - frameCount + offset) %
        kDiagnosticRingCapacity;
    if (diagnostic.Ring[index].Valid)
      snapshots.push_back(diagnostic.Ring[index]);
  }
  return snapshots;
}

void Runtime::ScheduleDiagnosticPostTrigger(int instanceID,
                                            melonDS::u32 untilFrame) {
  std::lock_guard<std::mutex> lock(State->DiagnosticStateMutex);
  if (instanceID < 0 ||
      instanceID >= static_cast<int>(State->DiagnosticState.size())) {
    return;
  }
  State->DiagnosticState[instanceID].PostTriggerUntilFrame = untilFrame;
}

std::optional<melonDS::u32>
Runtime::TakeDueDiagnosticPostTrigger(int instanceID, melonDS::u32 frame) {
  std::lock_guard<std::mutex> lock(State->DiagnosticStateMutex);
  if (instanceID < 0 ||
      instanceID >= static_cast<int>(State->DiagnosticState.size())) {
    return std::nullopt;
  }
  melonDS::u32 &untilFrame =
      State->DiagnosticState[instanceID].PostTriggerUntilFrame;
  if (untilFrame == 0 || frame < untilFrame)
    return std::nullopt;
  const melonDS::u32 result = untilFrame;
  untilFrame = 0;
  return result;
}

bool Runtime::ShouldEmitDiagnosticMismatch(int instanceID,
                                           melonDS::u32 frame,
                                           melonDS::u32 cooldownFrames) {
  std::lock_guard<std::mutex> lock(State->DiagnosticStateMutex);
  if (instanceID < 0 ||
      instanceID >= static_cast<int>(State->DiagnosticState.size())) {
    return false;
  }
  melonDS::u32 &last = State->DiagnosticState[instanceID].LastMismatchFrame;
  if (last != 0 && frame < last + cooldownFrames)
    return false;
  last = frame;
  return true;
}

bool Runtime::ShouldEmitDiagnosticLifeEvent(int instanceID, int player,
                                            melonDS::u32 frame,
                                            bool transitionOnly,
                                            melonDS::u32 cooldownFrames) {
  std::lock_guard<std::mutex> lock(State->DiagnosticStateMutex);
  if (instanceID < 0 ||
      instanceID >= static_cast<int>(State->DiagnosticState.size()) ||
      player < 0 || player >= 2) {
    return false;
  }
  melonDS::u32 &last =
      State->DiagnosticState[instanceID].LastLifeEventFrame[player];
  if (last == frame)
    return false;
  if (transitionOnly && last != 0 && frame < last + cooldownFrames)
    return false;
  last = frame;
  return true;
}

bool Runtime::ShouldEmitDiagnosticPitTransition(
    int instanceID, int player, melonDS::u32 frame,
    melonDS::u32 cooldownFrames) {
  std::lock_guard<std::mutex> lock(State->DiagnosticStateMutex);
  if (instanceID < 0 ||
      instanceID >= static_cast<int>(State->DiagnosticState.size()) ||
      player < 0 || player >= 2) {
    return false;
  }
  melonDS::u32 &last =
      State->DiagnosticState[instanceID].LastPitTransitionFrame[player];
  if (last != 0 && frame < last + cooldownFrames)
    return false;
  last = frame;
  return true;
}

bool Runtime::ShouldEmitDiagnosticPositionAnomaly(
    int instanceID, int player, melonDS::u32 frame,
    melonDS::u32 cooldownFrames) {
  std::lock_guard<std::mutex> lock(State->DiagnosticStateMutex);
  if (instanceID < 0 ||
      instanceID >= static_cast<int>(State->DiagnosticState.size()) ||
      player < 0 || player >= 2) {
    return false;
  }
  melonDS::u32 &last =
      State->DiagnosticState[instanceID].LastPositionAnomalyFrame[player];
  if (last != 0 && frame < last + cooldownFrames)
    return false;
  last = frame;
  return true;
}

PlayerLifeObservation
Runtime::ObservePlayerLifeState(int instanceID,
                                const PlayerLifeState &current) {
  std::lock_guard<std::mutex> lock(State->DiagnosticStateMutex);
  PlayerLifeObservation observation;
  if (instanceID < 0 ||
      instanceID >= static_cast<int>(State->DiagnosticState.size())) {
    return observation;
  }

  auto &instance = State->DiagnosticState[instanceID];
  observation.Accepted = true;
  observation.HadPrevious = instance.HasPlayerLifeState;
  observation.Previous = instance.LastPlayerLifeState;
  observation.Changed =
      !instance.HasPlayerLifeState ||
      !std::equal(std::begin(current.Lives), std::end(current.Lives),
                  std::begin(instance.LastPlayerLifeState.Lives)) ||
      !std::equal(std::begin(current.Deaths), std::end(current.Deaths),
                  std::begin(instance.LastPlayerLifeState.Deaths)) ||
      !std::equal(std::begin(current.Dead), std::end(current.Dead),
                  std::begin(instance.LastPlayerLifeState.Dead)) ||
      !std::equal(std::begin(current.Transition), std::end(current.Transition),
                  std::begin(instance.LastPlayerLifeState.Transition));
  instance.LastPlayerLifeState = current;
  instance.HasPlayerLifeState = true;
  return observation;
}

bool Runtime::TakeRuntimePatchLog(int instanceID, RuntimePatchLogKind kind) {
  std::lock_guard<std::mutex> lock(State->DiagnosticStateMutex);
  const std::size_t index = static_cast<std::size_t>(kind);
  if (instanceID < 0 ||
      instanceID >= static_cast<int>(State->DiagnosticState.size()) ||
      index >= static_cast<std::size_t>(RuntimePatchLogKind::Count)) {
    return false;
  }
  bool &logged = State->DiagnosticState[instanceID].RuntimePatchLogged[index];
  if (logged)
    return false;
  logged = true;
  return true;
}

void Runtime::ResetRuntimePatchLog(int instanceID, RuntimePatchLogKind kind) {
  std::lock_guard<std::mutex> lock(State->DiagnosticStateMutex);
  const std::size_t index = static_cast<std::size_t>(kind);
  if (instanceID < 0 ||
      instanceID >= static_cast<int>(State->DiagnosticState.size()) ||
      index >= static_cast<std::size_t>(RuntimePatchLogKind::Count)) {
    return;
  }
  State->DiagnosticState[instanceID].RuntimePatchLogged[index] = false;
}

void Runtime::StartHangDiagnostics(const Config::DiagnosticsConfig &config,
                                   bool host) {
  State->Config = config;
  State->Host = host;
  if (!config.HangDiagnosticsEnabled || State->WatchdogThreadStarted)
    return;
  State->WatchdogStop.store(false, std::memory_order_release);
  State->WatchdogThreadStarted = true;
  State->WatchdogThread = std::thread([this] { State->RunWatchdog(); });
}

void Runtime::Stop() {
  if (State->WatchdogThreadStarted) {
    State->WatchdogStop.store(true, std::memory_order_release);
    if (State->WatchdogThread.joinable())
      State->WatchdogThread.join();
    State->WatchdogThreadStarted = false;
  }

  State->StopFrameHeartbeat();

  {
    std::lock_guard<std::mutex> lock(State->HashLogMutex);
    if (State->HashLog.is_open())
      State->HashLog.close();
  }

  {
    std::lock_guard<std::mutex> lock(State->DiagnosticEventMutex);
    if (State->DiagnosticEventLog.is_open())
      State->DiagnosticEventLog.close();
    State->DiagnosticEventPath.clear();
  }

  std::lock_guard<std::mutex> lock(State->LogMutex);
  if (State->WatchdogLog)
    State->WatchdogLog.close();
  if (State->PhaseEventsLog)
    State->PhaseEventsLog.close();
}

void Runtime::TracePhase(const char *event, const char *phase, int instanceID,
                         melonDS::u32 frame, melonDS::u32 logicalFrame,
                         melonDS::u32 sendFrame) {
  if (!State->Config.HangDiagnosticsEnabled)
    return;

  const std::uint64_t now = NowUnixMs();
  State->Event.store(event ? event : "phase", std::memory_order_release);
  State->Phase.store(phase ? phase : "unknown", std::memory_order_release);
  State->PhaseUnixMs.store(now, std::memory_order_release);
  State->Instance.store(instanceID, std::memory_order_release);
  State->Frame.store(frame, std::memory_order_release);
  State->LogicalFrame.store(logicalFrame, std::memory_order_release);
  State->SendFrame.store(sendFrame, std::memory_order_release);

  std::lock_guard<std::mutex> lock(State->LogMutex);
  State->WritePhaseEvent(now, event, phase, instanceID, frame, logicalFrame,
                         sendFrame);
}

void Runtime::UpdateNetplaySnapshot(
    melonDS::u32 lastSentFrame, melonDS::u32 lastReceivedFrame,
    melonDS::u32 frameForLead, melonDS::u32 noFrameLimit,
    std::size_t localQueue, std::size_t remoteQueue, std::size_t delayedQueue,
    int peerState, int connectingPeerState) {
  if (!State->Config.HangDiagnosticsEnabled)
    return;
  State->LastSentFrame.store(lastSentFrame, std::memory_order_release);
  State->LastRecvFrame.store(lastReceivedFrame, std::memory_order_release);
  const int lead =
      frameForLead == noFrameLimit || lastReceivedFrame == noFrameLimit
          ? 0
          : static_cast<int>(frameForLead) -
                static_cast<int>(lastReceivedFrame);
  State->Lead.store(lead, std::memory_order_release);
  State->LocalQueue.store(localQueue, std::memory_order_release);
  State->RemoteQueue.store(remoteQueue, std::memory_order_release);
  State->DelayedQueue.store(delayedQueue, std::memory_order_release);
  State->PeerState.store(peerState, std::memory_order_release);
  State->ConnectingPeerState.store(connectingPeerState,
                                   std::memory_order_release);
}

void Runtime::ResetNetplaySnapshot(melonDS::u32 noFrameLimit) {
  State->RemoteWaitActive.store(0, std::memory_order_release);
  State->RemoteWaitTarget.store(0, std::memory_order_release);
  State->LastSentFrame.store(noFrameLimit, std::memory_order_release);
  State->LastRecvFrame.store(noFrameLimit, std::memory_order_release);
  State->LocalQueue.store(0, std::memory_order_release);
  State->RemoteQueue.store(0, std::memory_order_release);
  State->DelayedQueue.store(0, std::memory_order_release);
}

void Runtime::RecordENetService(int result) {
  State->LastENetServiceResult.store(result, std::memory_order_release);
}

void Runtime::RecordENetEvent(int type, melonDS::u32 data) {
  State->LastENetEventType.store(type, std::memory_order_release);
  State->LastENetEventData.store(data, std::memory_order_release);
}

void Runtime::RecordENetReceive(std::uint64_t unixMs) {
  State->LastENetRecvUnixMs.store(unixMs, std::memory_order_release);
}

void Runtime::RecordENetSend(int result, std::size_t bytes,
                             std::uint64_t unixMs) {
  State->LastENetSendResult.store(result, std::memory_order_release);
  State->LastENetSendBytes.store(bytes, std::memory_order_release);
  State->LastENetSendUnixMs.store(unixMs, std::memory_order_release);
}

void Runtime::BeginRemoteWait(melonDS::u32 targetFrame, std::uint64_t unixMs) {
  State->RemoteWaitActive.store(1, std::memory_order_release);
  State->RemoteWaitTarget.store(targetFrame, std::memory_order_release);
  State->RemoteWaitStartUnixMs.store(unixMs, std::memory_order_release);
  State->RemoteWaitProgressUnixMs.store(unixMs, std::memory_order_release);
}

void Runtime::ProgressRemoteWait(std::uint64_t unixMs) {
  State->RemoteWaitProgressUnixMs.store(unixMs, std::memory_order_release);
}

void Runtime::EndRemoteWait() {
  State->RemoteWaitActive.store(0, std::memory_order_release);
}

void Runtime::UpdateGameSnapshot(int instanceID, melonDS::u32 frame,
                                 const GameStateModel::GameStateSample &sample,
                                 std::uint64_t unixMs) {
  State->Instance.store(instanceID, std::memory_order_release);
  State->Frame.store(frame, std::memory_order_release);
  State->Arm9PC.store(sample.Arm9PC, std::memory_order_release);
  State->Arm9LR.store(sample.Arm9LR, std::memory_order_release);
  State->Arm9SP.store(sample.Arm9SP, std::memory_order_release);
  State->Arm9CPSR.store(sample.Arm9CPSR, std::memory_order_release);
  State->StageID.store(sample.StageID, std::memory_order_release);
  State->StageGroup.store(sample.StageGroup, std::memory_order_release);
  State->VsMode.store(sample.VsMode, std::memory_order_release);
  State->NetState14.store(sample.NetState14, std::memory_order_release);
  State->NetState1C.store(sample.NetState1C, std::memory_order_release);
  State->NetState20.store(sample.NetState20, std::memory_order_release);
  State->NetState24.store(sample.NetState24, std::memory_order_release);
  State->NetState5C.store(sample.NetState5C, std::memory_order_release);
  State->NetPacketTick.store(sample.NetPacketTick, std::memory_order_release);
  State->AppFrameLength.store(sample.AppFrameLength, std::memory_order_release);
  State->AppUpdateTask.store(sample.AppUpdateTask, std::memory_order_release);
  State->AppSleeping.store(sample.AppSleeping, std::memory_order_release);
  State->StageSceneState.store(sample.StageSceneStateType,
                               std::memory_order_release);
  State->Player0Transition.store(sample.PlayerTransitionStatus0,
                                 std::memory_order_release);
  State->Player1Transition.store(sample.PlayerTransitionStatus1,
                                 std::memory_order_release);
  State->GameSnapshotUnixMs.store(unixMs, std::memory_order_release);
}

} // namespace NsmbNetplayPoC::Diagnostics
