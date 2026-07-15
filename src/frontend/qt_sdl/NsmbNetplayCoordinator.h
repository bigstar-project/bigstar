#pragma once

#include "types.h"

#include <memory>
#include <utility>

namespace NsmbNetplayPoC::Coordination {

enum class FrameBarrierKind {
  Before,
  After,
  Netplay,
};

enum class NetplayStartWaitResult {
  AlreadyComplete,
  LocalLeader,
  PeerComplete,
  TimedOut,
};

class Runtime {
public:
  Runtime();
  ~Runtime();

  Runtime(const Runtime &) = delete;
  Runtime &operator=(const Runtime &) = delete;

  void ResetNetplayStartWait();
  NetplayStartWaitResult WaitForNetplayStart(int instanceID, int localInstance,
                                             int instanceCount,
                                             melonDS::u32 frame, int timeoutMs);
  void CompleteNetplayStartWait();

  bool IsNetplayLockstepStarted(int instanceID) const;
  bool NeedsInitialRemoteInput(bool remoteInputAvailable) const;
  void MarkNetplayLockstepStarted(int instanceID);
  void ResetNetplayLockstep(int instanceID);

  melonDS::u32 TestFrame(int instanceID) const;
  melonDS::u32 AdvanceTestFrame(int instanceID);
  bool AllTestFramesReached(int instanceCount, melonDS::u32 target) const;

  bool WaitAtFrameBarrier(FrameBarrierKind kind, int instanceID,
                          melonDS::u32 frame, int instanceCount, int timeoutMs,
                          const char *name);
  bool WaitForSerialTurn(int instanceID, melonDS::u32 frame, int instanceCount,
                         int timeoutMs);
  void AdvanceSerialTurn(int instanceID, melonDS::u32 frame, int instanceCount);

  bool IsStateSaved(int instanceID) const;
  void MarkStateSaved(int instanceID);
  bool AllStatesSaved(int instanceCount) const;
  bool TryBeginLocalMPSave(int instanceCount);

  bool IsStateLoaded(int instanceID) const;
  void MarkStateLoaded(int instanceID);
  bool AllStatesLoaded(int instanceCount) const;
  bool TryBeginLocalMPLoad();
  void FinishLocalMPLoad(bool loaded);
  std::pair<bool, bool> LocalMPLoadStatus() const;


private:
  struct Impl;
  std::unique_ptr<Impl> State;
};

} // namespace NsmbNetplayPoC::Coordination
