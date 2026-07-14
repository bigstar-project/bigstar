#include "NsmbNetplayDiagnostics.h"

#include <algorithm>
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
#include <windows.h>
#include <dbghelp.h>
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

  HANDLE file = CreateFileA(dumpPath.string().c_str(), GENERIC_WRITE, 0,
                            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    FreeLibrary(dbghelp);
    return false;
  }

  const BOOL ok = miniDumpWriteDump(
      GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, nullptr,
      nullptr, nullptr);
  CloseHandle(file);
  FreeLibrary(dbghelp);
  return ok != FALSE;
}
#else
bool WriteMiniDump(const std::string &) { return false; }
#endif

} // namespace

struct HangRuntime::Impl {
  Config::DiagnosticsConfig Config;
  bool Host = true;
  std::ofstream WatchdogLog;
  std::ofstream PhaseEventsLog;
  std::mutex LogMutex;
  std::atomic<bool> WatchdogStop{false};
  bool WatchdogThreadStarted = false;
  std::thread WatchdogThread;
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

  void WritePhaseEvent(std::uint64_t now, const char *event,
                       const char *phase, int instanceID,
                       melonDS::u32 frame, melonDS::u32 logicalFrame,
                       melonDS::u32 sendFrame) {
    if (!EnsureLogOpen(PhaseEventsLog, Config.HangPhaseEventsPath))
      return;

    PhaseEventsLog << "{\"tUnixMs\":" << now << ",\"event\":\""
                   << (event ? event : "phase") << "\",\"phase\":\""
                   << (phase ? phase : "unknown") << "\",\"instance\":"
                   << instanceID << ",\"frame\":" << frame
                   << ",\"logicalFrame\":" << logicalFrame
                   << ",\"sendFrame\":" << sendFrame
                   << ",\"lastSent\":"
                   << LastSentFrame.load(std::memory_order_acquire)
                   << ",\"lastRecv\":"
                   << LastRecvFrame.load(std::memory_order_acquire)
                   << ",\"lead\":" << Lead.load(std::memory_order_acquire)
                   << ",\"remoteWaitActive\":"
                   << RemoteWaitActive.load(std::memory_order_acquire)
                   << ",\"remoteWaitTarget\":"
                   << RemoteWaitTarget.load(std::memory_order_acquire)
                   << "}\n";
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
          << Arm9SP.load(std::memory_order_acquire)
          << "\",\"arm9CPSR\":\"0x"
          << Arm9CPSR.load(std::memory_order_acquire)
          << "\",\"stageID\":\"0x" << StageID.load(std::memory_order_acquire)
          << "\",\"stageGroup\":\"0x"
          << StageGroup.load(std::memory_order_acquire)
          << "\",\"vsMode\":\"0x" << VsMode.load(std::memory_order_acquire)
          << "\",\"netState14\":\"0x"
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
};

HangRuntime::HangRuntime() : Runtime(std::make_unique<Impl>()) {}

HangRuntime::~HangRuntime() { Stop(); }

void HangRuntime::Start(const Config::DiagnosticsConfig &config, bool host) {
  Runtime->Config = config;
  Runtime->Host = host;
  if (!config.HangDiagnosticsEnabled || Runtime->WatchdogThreadStarted)
    return;
  Runtime->WatchdogStop.store(false, std::memory_order_release);
  Runtime->WatchdogThreadStarted = true;
  Runtime->WatchdogThread =
      std::thread([this] { Runtime->RunWatchdog(); });
}

void HangRuntime::Stop() {
  if (Runtime->WatchdogThreadStarted) {
    Runtime->WatchdogStop.store(true, std::memory_order_release);
    if (Runtime->WatchdogThread.joinable())
      Runtime->WatchdogThread.join();
    Runtime->WatchdogThreadStarted = false;
  }

  std::lock_guard<std::mutex> lock(Runtime->LogMutex);
  if (Runtime->WatchdogLog)
    Runtime->WatchdogLog.close();
  if (Runtime->PhaseEventsLog)
    Runtime->PhaseEventsLog.close();
}

void HangRuntime::TracePhase(const char *event, const char *phase,
                             int instanceID, melonDS::u32 frame,
                             melonDS::u32 logicalFrame,
                             melonDS::u32 sendFrame) {
  if (!Runtime->Config.HangDiagnosticsEnabled)
    return;

  const std::uint64_t now = NowUnixMs();
  Runtime->Event.store(event ? event : "phase", std::memory_order_release);
  Runtime->Phase.store(phase ? phase : "unknown", std::memory_order_release);
  Runtime->PhaseUnixMs.store(now, std::memory_order_release);
  Runtime->Instance.store(instanceID, std::memory_order_release);
  Runtime->Frame.store(frame, std::memory_order_release);
  Runtime->LogicalFrame.store(logicalFrame, std::memory_order_release);
  Runtime->SendFrame.store(sendFrame, std::memory_order_release);

  std::lock_guard<std::mutex> lock(Runtime->LogMutex);
  Runtime->WritePhaseEvent(now, event, phase, instanceID, frame, logicalFrame,
                           sendFrame);
}

void HangRuntime::UpdateNetplaySnapshot(
    melonDS::u32 lastSentFrame, melonDS::u32 lastReceivedFrame,
    melonDS::u32 frameForLead, melonDS::u32 noFrameLimit,
    std::size_t localQueue, std::size_t remoteQueue, std::size_t delayedQueue,
    int peerState, int connectingPeerState) {
  if (!Runtime->Config.HangDiagnosticsEnabled)
    return;
  Runtime->LastSentFrame.store(lastSentFrame, std::memory_order_release);
  Runtime->LastRecvFrame.store(lastReceivedFrame, std::memory_order_release);
  const int lead = frameForLead == noFrameLimit ||
                           lastReceivedFrame == noFrameLimit
                       ? 0
                       : static_cast<int>(frameForLead) -
                             static_cast<int>(lastReceivedFrame);
  Runtime->Lead.store(lead, std::memory_order_release);
  Runtime->LocalQueue.store(localQueue, std::memory_order_release);
  Runtime->RemoteQueue.store(remoteQueue, std::memory_order_release);
  Runtime->DelayedQueue.store(delayedQueue, std::memory_order_release);
  Runtime->PeerState.store(peerState, std::memory_order_release);
  Runtime->ConnectingPeerState.store(connectingPeerState,
                                      std::memory_order_release);
}

void HangRuntime::ResetNetplaySnapshot(melonDS::u32 noFrameLimit) {
  Runtime->RemoteWaitActive.store(0, std::memory_order_release);
  Runtime->RemoteWaitTarget.store(0, std::memory_order_release);
  Runtime->LastSentFrame.store(noFrameLimit, std::memory_order_release);
  Runtime->LastRecvFrame.store(noFrameLimit, std::memory_order_release);
  Runtime->LocalQueue.store(0, std::memory_order_release);
  Runtime->RemoteQueue.store(0, std::memory_order_release);
  Runtime->DelayedQueue.store(0, std::memory_order_release);
}

void HangRuntime::RecordENetService(int result) {
  Runtime->LastENetServiceResult.store(result, std::memory_order_release);
}

void HangRuntime::RecordENetEvent(int type, melonDS::u32 data) {
  Runtime->LastENetEventType.store(type, std::memory_order_release);
  Runtime->LastENetEventData.store(data, std::memory_order_release);
}

void HangRuntime::RecordENetReceive(std::uint64_t unixMs) {
  Runtime->LastENetRecvUnixMs.store(unixMs, std::memory_order_release);
}

void HangRuntime::RecordENetSend(int result, std::size_t bytes,
                                 std::uint64_t unixMs) {
  Runtime->LastENetSendResult.store(result, std::memory_order_release);
  Runtime->LastENetSendBytes.store(bytes, std::memory_order_release);
  Runtime->LastENetSendUnixMs.store(unixMs, std::memory_order_release);
}

void HangRuntime::BeginRemoteWait(melonDS::u32 targetFrame,
                                  std::uint64_t unixMs) {
  Runtime->RemoteWaitActive.store(1, std::memory_order_release);
  Runtime->RemoteWaitTarget.store(targetFrame, std::memory_order_release);
  Runtime->RemoteWaitStartUnixMs.store(unixMs, std::memory_order_release);
  Runtime->RemoteWaitProgressUnixMs.store(unixMs, std::memory_order_release);
}

void HangRuntime::ProgressRemoteWait(std::uint64_t unixMs) {
  Runtime->RemoteWaitProgressUnixMs.store(unixMs, std::memory_order_release);
}

void HangRuntime::EndRemoteWait() {
  Runtime->RemoteWaitActive.store(0, std::memory_order_release);
}

void HangRuntime::UpdateGameSnapshot(
    int instanceID, melonDS::u32 frame,
    const GameStateModel::GameStateSample &sample, std::uint64_t unixMs) {
  Runtime->Instance.store(instanceID, std::memory_order_release);
  Runtime->Frame.store(frame, std::memory_order_release);
  Runtime->Arm9PC.store(sample.Arm9PC, std::memory_order_release);
  Runtime->Arm9LR.store(sample.Arm9LR, std::memory_order_release);
  Runtime->Arm9SP.store(sample.Arm9SP, std::memory_order_release);
  Runtime->Arm9CPSR.store(sample.Arm9CPSR, std::memory_order_release);
  Runtime->StageID.store(sample.StageID, std::memory_order_release);
  Runtime->StageGroup.store(sample.StageGroup, std::memory_order_release);
  Runtime->VsMode.store(sample.VsMode, std::memory_order_release);
  Runtime->NetState14.store(sample.NetState14, std::memory_order_release);
  Runtime->NetState1C.store(sample.NetState1C, std::memory_order_release);
  Runtime->NetState20.store(sample.NetState20, std::memory_order_release);
  Runtime->NetState24.store(sample.NetState24, std::memory_order_release);
  Runtime->NetState5C.store(sample.NetState5C, std::memory_order_release);
  Runtime->NetPacketTick.store(sample.NetPacketTick, std::memory_order_release);
  Runtime->AppFrameLength.store(sample.AppFrameLength,
                                std::memory_order_release);
  Runtime->AppUpdateTask.store(sample.AppUpdateTask, std::memory_order_release);
  Runtime->AppSleeping.store(sample.AppSleeping, std::memory_order_release);
  Runtime->StageSceneState.store(sample.StageSceneStateType,
                                 std::memory_order_release);
  Runtime->Player0Transition.store(sample.PlayerTransitionStatus0,
                                   std::memory_order_release);
  Runtime->Player1Transition.store(sample.PlayerTransitionStatus1,
                                   std::memory_order_release);
  Runtime->GameSnapshotUnixMs.store(unixMs, std::memory_order_release);
}

} // namespace NsmbNetplayPoC::Diagnostics
