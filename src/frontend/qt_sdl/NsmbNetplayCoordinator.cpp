#include "NsmbNetplayCoordinator.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>

namespace NsmbNetplayPoC::Coordination {
namespace {

constexpr int kMaxInstances = 16;

int ClampInstanceCount(int instanceCount) {
  return std::clamp(instanceCount, 1, kMaxInstances);
}

bool IsValidInstance(int instanceID) {
  return instanceID >= 0 && instanceID < kMaxInstances;
}

} // namespace

struct Runtime::Impl {
  struct FrameBarrier {
    std::array<bool, kMaxInstances> Waiting{};
    std::array<melonDS::u32, kMaxInstances> Frame{};
    int Generation = 0;
  };

  mutable std::mutex Mutex;
  std::condition_variable Condition;
  std::array<bool, kMaxInstances> NetplayStartArrived{};
  bool NetplayStartComplete = false;
  std::array<bool, kMaxInstances> NetplayLockstepStarted{};
  bool NetplayAnyLockstepStarted = false;
  std::array<melonDS::u32, kMaxInstances> TestFrame{};
  std::array<FrameBarrier, 3> FrameBarriers{};
  melonDS::u32 SerialFrame = 0;
  int SerialInstance = 0;
  std::array<bool, kMaxInstances> StateSaved{};
  std::array<bool, kMaxInstances> StateLoaded{};
  bool LocalMPSaved = false;
  bool LocalMPLoadStarted = false;
  bool LocalMPLoadFinished = false;
  bool LocalMPLoaded = false;
  std::array<bool, kMaxInstances> MemoryPatchApplied{};

  FrameBarrier &GetFrameBarrier(FrameBarrierKind kind) {
    return FrameBarriers[static_cast<std::size_t>(kind)];
  }

  static bool AllArrived(const FrameBarrier &barrier, melonDS::u32 frame,
                         int instanceCount) {
    for (int instance = 0; instance < instanceCount; instance++) {
      if (!barrier.Waiting[instance] || barrier.Frame[instance] != frame)
        return false;
    }
    return true;
  }

  void Release(FrameBarrier &barrier, int instanceCount) {
    for (int instance = 0; instance < instanceCount; instance++)
      barrier.Waiting[instance] = false;
    barrier.Generation++;
    Condition.notify_all();
  }
};

Runtime::Runtime() : State(std::make_unique<Impl>()) {}

Runtime::~Runtime() = default;

void Runtime::ResetNetplayStartWait() {
  std::lock_guard<std::mutex> lock(State->Mutex);
  State->NetplayStartArrived.fill(false);
  State->NetplayStartComplete = false;
  State->Condition.notify_all();
}

NetplayStartWaitResult Runtime::WaitForNetplayStart(int instanceID,
                                                    int localInstance,
                                                    int instanceCount,
                                                    melonDS::u32 frame,
                                                    int timeoutMs) {
  if (!IsValidInstance(instanceID))
    return NetplayStartWaitResult::TimedOut;
  instanceCount = ClampInstanceCount(instanceCount);
  const bool isLocal = instanceID == localInstance;

  std::unique_lock<std::mutex> lock(State->Mutex);
  if (State->NetplayStartComplete)
    return NetplayStartWaitResult::AlreadyComplete;

  State->NetplayStartArrived[instanceID] = true;
  State->Condition.notify_all();
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

  if (isLocal) {
    const auto allArrived = [&] {
      for (int instance = 0; instance < instanceCount; instance++) {
        if (!State->NetplayStartArrived[instance])
          return false;
      }
      return true;
    };
    while (!allArrived()) {
      if (timeoutMs > 0) {
        if (State->Condition.wait_until(lock, deadline) ==
            std::cv_status::timeout) {
          std::printf("NSMB PoC: netplay start local barrier timeout inst=%d "
                      "frame=%u waitedMs=%d\n",
                      instanceID, frame, timeoutMs);
          break;
        }
      } else {
        State->Condition.wait(lock);
      }
    }
    return NetplayStartWaitResult::LocalLeader;
  }

  while (!State->NetplayStartComplete) {
    if (timeoutMs > 0) {
      if (State->Condition.wait_until(lock, deadline) ==
          std::cv_status::timeout) {
        std::printf("NSMB PoC: netplay start peer wait barrier timeout inst=%d "
                    "frame=%u waitedMs=%d\n",
                    instanceID, frame, timeoutMs);
        return NetplayStartWaitResult::TimedOut;
      }
    } else {
      State->Condition.wait(lock);
    }
  }
  return NetplayStartWaitResult::PeerComplete;
}

void Runtime::CompleteNetplayStartWait() {
  std::lock_guard<std::mutex> lock(State->Mutex);
  State->NetplayStartComplete = true;
  State->Condition.notify_all();
}

bool Runtime::IsNetplayLockstepStarted(int instanceID) const {
  if (!IsValidInstance(instanceID))
    return false;
  std::lock_guard<std::mutex> lock(State->Mutex);
  return State->NetplayLockstepStarted[instanceID];
}

bool Runtime::NeedsInitialRemoteInput(bool remoteInputAvailable) const {
  std::lock_guard<std::mutex> lock(State->Mutex);
  return !State->NetplayAnyLockstepStarted && !remoteInputAvailable;
}

void Runtime::MarkNetplayLockstepStarted(int instanceID) {
  if (!IsValidInstance(instanceID))
    return;
  std::lock_guard<std::mutex> lock(State->Mutex);
  State->NetplayLockstepStarted[instanceID] = true;
  State->NetplayAnyLockstepStarted = true;
}

void Runtime::ResetNetplayLockstep(int instanceID) {
  if (!IsValidInstance(instanceID))
    return;
  std::lock_guard<std::mutex> lock(State->Mutex);
  State->NetplayLockstepStarted[instanceID] = false;
  State->NetplayAnyLockstepStarted = false;
}

melonDS::u32 Runtime::TestFrame(int instanceID) const {
  if (!IsValidInstance(instanceID))
    return 0;
  std::lock_guard<std::mutex> lock(State->Mutex);
  return State->TestFrame[instanceID];
}

melonDS::u32 Runtime::AdvanceTestFrame(int instanceID) {
  if (!IsValidInstance(instanceID))
    return 0;
  std::lock_guard<std::mutex> lock(State->Mutex);
  return ++State->TestFrame[instanceID];
}

bool Runtime::AllTestFramesReached(int instanceCount,
                                   melonDS::u32 target) const {
  instanceCount = ClampInstanceCount(instanceCount);
  std::lock_guard<std::mutex> lock(State->Mutex);
  for (int instance = 0; instance < instanceCount; instance++) {
    if (State->TestFrame[instance] < target)
      return false;
  }
  return true;
}

bool Runtime::WaitAtFrameBarrier(FrameBarrierKind kind, int instanceID,
                                 melonDS::u32 frame, int instanceCount,
                                 int timeoutMs, const char *name) {
  instanceCount = ClampInstanceCount(instanceCount);
  if (!IsValidInstance(instanceID) || instanceID >= instanceCount)
    return true;

  std::unique_lock<std::mutex> lock(State->Mutex);
  Impl::FrameBarrier &barrier = State->GetFrameBarrier(kind);
  const int generation = barrier.Generation;
  barrier.Waiting[instanceID] = true;
  barrier.Frame[instanceID] = frame;

  if (Impl::AllArrived(barrier, frame, instanceCount)) {
    State->Release(barrier, instanceCount);
    return true;
  }

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (barrier.Generation == generation) {
    if (timeoutMs > 0) {
      if (State->Condition.wait_until(lock, deadline) ==
          std::cv_status::timeout) {
        std::printf("NSMB Test: %s frame barrier timeout inst=%d frame=%u "
                    "waitedMs=%d\n",
                    name, instanceID, frame, timeoutMs);
        barrier.Waiting[instanceID] = false;
        State->Condition.notify_all();
        return false;
      }
    } else {
      State->Condition.wait(lock);
    }

    if (Impl::AllArrived(barrier, frame, instanceCount)) {
      State->Release(barrier, instanceCount);
      return true;
    }
  }
  return true;
}

bool Runtime::WaitForSerialTurn(int instanceID, melonDS::u32 frame,
                                int instanceCount, int timeoutMs) {
  instanceCount = ClampInstanceCount(instanceCount);
  if (!IsValidInstance(instanceID) || instanceID >= instanceCount)
    return true;

  std::unique_lock<std::mutex> lock(State->Mutex);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  for (;;) {
    if (State->SerialFrame == frame && State->SerialInstance == instanceID)
      return true;

    if (timeoutMs > 0) {
      if (State->Condition.wait_until(lock, deadline) ==
          std::cv_status::timeout) {
        std::printf("NSMB Test: serial run timeout inst=%d frame=%u "
                    "expectedInst=%d expectedFrame=%u waitedMs=%d\n",
                    instanceID, frame, State->SerialInstance,
                    State->SerialFrame, timeoutMs);
        return false;
      }
    } else {
      State->Condition.wait(lock);
    }
  }
}

void Runtime::AdvanceSerialTurn(int instanceID, melonDS::u32 frame,
                                int instanceCount) {
  instanceCount = ClampInstanceCount(instanceCount);
  if (!IsValidInstance(instanceID) || instanceID >= instanceCount)
    return;

  std::lock_guard<std::mutex> lock(State->Mutex);
  if (State->SerialFrame != frame || State->SerialInstance != instanceID)
    return;
  State->SerialInstance++;
  if (State->SerialInstance >= instanceCount) {
    State->SerialInstance = 0;
    State->SerialFrame++;
  }
  State->Condition.notify_all();
}

bool Runtime::IsStateSaved(int instanceID) const {
  if (!IsValidInstance(instanceID))
    return false;
  std::lock_guard<std::mutex> lock(State->Mutex);
  return State->StateSaved[instanceID];
}

void Runtime::MarkStateSaved(int instanceID) {
  if (!IsValidInstance(instanceID))
    return;
  std::lock_guard<std::mutex> lock(State->Mutex);
  State->StateSaved[instanceID] = true;
}

bool Runtime::AllStatesSaved(int instanceCount) const {
  instanceCount = ClampInstanceCount(instanceCount);
  std::lock_guard<std::mutex> lock(State->Mutex);
  for (int instance = 0; instance < instanceCount; instance++) {
    if (!State->StateSaved[instance])
      return false;
  }
  return true;
}

bool Runtime::TryBeginLocalMPSave(int instanceCount) {
  instanceCount = ClampInstanceCount(instanceCount);
  std::lock_guard<std::mutex> lock(State->Mutex);
  if (State->LocalMPSaved)
    return false;
  for (int instance = 0; instance < instanceCount; instance++) {
    if (!State->StateSaved[instance])
      return false;
  }
  State->LocalMPSaved = true;
  return true;
}

bool Runtime::IsStateLoaded(int instanceID) const {
  if (!IsValidInstance(instanceID))
    return false;
  std::lock_guard<std::mutex> lock(State->Mutex);
  return State->StateLoaded[instanceID];
}

void Runtime::MarkStateLoaded(int instanceID) {
  if (!IsValidInstance(instanceID))
    return;
  std::lock_guard<std::mutex> lock(State->Mutex);
  State->StateLoaded[instanceID] = true;
}

bool Runtime::AllStatesLoaded(int instanceCount) const {
  instanceCount = ClampInstanceCount(instanceCount);
  std::lock_guard<std::mutex> lock(State->Mutex);
  for (int instance = 0; instance < instanceCount; instance++) {
    if (!State->StateLoaded[instance])
      return false;
  }
  return true;
}

bool Runtime::TryBeginLocalMPLoad() {
  std::lock_guard<std::mutex> lock(State->Mutex);
  if (State->LocalMPLoadStarted)
    return false;
  State->LocalMPLoadStarted = true;
  return true;
}

void Runtime::FinishLocalMPLoad(bool loaded) {
  std::lock_guard<std::mutex> lock(State->Mutex);
  State->LocalMPLoaded = loaded;
  State->LocalMPLoadFinished = true;
}

std::pair<bool, bool> Runtime::LocalMPLoadStatus() const {
  std::lock_guard<std::mutex> lock(State->Mutex);
  return {State->LocalMPLoadFinished, State->LocalMPLoaded};
}

bool Runtime::IsMemoryPatchApplied(int instanceID) const {
  if (!IsValidInstance(instanceID))
    return false;
  std::lock_guard<std::mutex> lock(State->Mutex);
  return State->MemoryPatchApplied[instanceID];
}

void Runtime::MarkMemoryPatchApplied(int instanceID) {
  if (!IsValidInstance(instanceID))
    return;
  std::lock_guard<std::mutex> lock(State->Mutex);
  State->MemoryPatchApplied[instanceID] = true;
}

} // namespace NsmbNetplayPoC::Coordination
