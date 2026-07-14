#include "NsmbNetplayDiagnostics.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
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

struct Runtime::Impl {
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
  std::mutex HashLogMutex;
  std::ofstream HashLog;
  bool ScreenHashEnabled = false;
  std::array<melonDS::u64, 16> LastHashFrame{};
  std::mutex DiagnosticEventMutex;
  std::ofstream DiagnosticEventLog;
  std::string DiagnosticEventPath;
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
