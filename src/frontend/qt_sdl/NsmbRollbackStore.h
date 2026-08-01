#ifndef NSMB_ROLLBACK_STORE_H
#define NSMB_ROLLBACK_STORE_H

#include "types.h"

#include <cstddef>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

namespace NsmbMvlNetplay::RollbackStorage {

constexpr melonDS::u32 kNoFrame = 0;

struct StoredState {
  std::vector<char> Buffer;
  std::vector<melonDS::u8> MainRAMCopy;
  std::vector<melonDS::u8> MainRAMShadowCopy;
  std::vector<melonDS::u32> MainRAMPreimagePages;
  std::vector<melonDS::u8> MainRAMPreimage;
  melonDS::u32 BaseFrame = kNoFrame;
  bool MainRAMDelta = false;
  bool MainRAMFramePreimage = false;
};

enum class DeltaMode {
  None,
  KeyframeDelta,
  FrameDelta,
  Preimage,
};

std::size_t CheckpointBytes(const StoredState &checkpoint);

bool ShouldSaveCheckpoint(melonDS::u32 frame, int interval,
                          melonDS::u32 netplayStartFrame);
melonDS::u32 ClampResimulationMismatch(melonDS::u32 mismatchFrame,
                                       melonDS::u32 currentFrame,
                                       int maxResimFrames);
bool IsResimulationDelayElapsed(melonDS::u32 currentFrame,
                                std::optional<melonDS::u32> observedFrame,
                                int delayFrames);
bool ShouldSaveResimulationCheckpoint(melonDS::u32 completedFrame,
                                      melonDS::u32 currentFrame,
                                      bool skipIntermediate);

struct StatisticsSnapshot {
  melonDS::u32 RestoreCount = 0;
  melonDS::u32 ResimulateCount = 0;
  melonDS::u32 CheckpointSaveCount = 0;
  std::size_t CheckpointLastBytes = 0;
  std::size_t CheckpointMinBytes = 0;
  std::size_t CheckpointMaxBytes = 0;
  unsigned long long CheckpointTotalBytes = 0;
  unsigned long long CheckpointSaveTotalUs = 0;
  unsigned long long CheckpointSaveMaxUs = 0;
  unsigned long long CheckpointRestoreTotalUs = 0;
  unsigned long long CheckpointRestoreMaxUs = 0;
  melonDS::u32 CheckpointRestoreOpCount = 0;
  melonDS::u32 MeasuredResimOpCount = 0;
  unsigned long long MeasuredResimFrameCount = 0;
  unsigned long long ResimRunFrameTotalUs = 0;
  unsigned long long ResimRunFrameMaxUs = 0;
  unsigned long long ResimCheckpointSaveTotalUs = 0;
  unsigned long long ResimCheckpointSaveMaxUs = 0;
  unsigned long long ResimCorrectionTotalUs = 0;
  unsigned long long ResimCorrectionMaxUs = 0;

  std::size_t AverageCheckpointBytes() const;
  unsigned long long AverageCheckpointSaveUs() const;
  unsigned long long AverageCheckpointRestoreUs() const;
  unsigned long long AverageResimRunFrameUs() const;
  unsigned long long AverageResimCheckpointSaveUs() const;
  unsigned long long AverageResimCorrectionUs() const;
};

class Statistics {
public:
  void RecordCheckpointSave(std::size_t bytes,
                            unsigned long long elapsedUs);
  void RecordCheckpointRestore(unsigned long long elapsedUs);
  void RecordProbeRestore();
  void RecordResimulation(melonDS::u32 frames,
                          unsigned long long runFrameTotalUs,
                          unsigned long long runFrameMaxUs,
                          unsigned long long checkpointSaveTotalUs,
                          unsigned long long checkpointSaveMaxUs,
                          unsigned long long correctionTotalUs);
  StatisticsSnapshot Snapshot() const;
  bool ShouldTrace(melonDS::u32 frame, melonDS::u32 interval);

private:
  mutable std::mutex Mutex_;
  StatisticsSnapshot Snapshot_;
  melonDS::u32 LastTraceFrame_ = static_cast<melonDS::u32>(-1);
};

class Store {
public:
  using StateMap = std::map<melonDS::u32, StoredState>;

  bool Empty() const;
  std::size_t Size() const;
  const StateMap &States() const;

  std::size_t Put(melonDS::u32 frame, StoredState checkpoint);
  bool Copy(melonDS::u32 frame, StoredState &checkpoint) const;
  bool LatestAtOrBefore(melonDS::u32 frame, melonDS::u32 &storedFrame,
                        StoredState &checkpoint) const;
  void EraseAfter(melonDS::u32 frame);
  void Prune(melonDS::u32 currentFrame, melonDS::u32 window);

  void PrepareSave(melonDS::u32 frame, DeltaMode mode,
                   melonDS::u32 keyframeInterval,
                   melonDS::u32 netplayStartFrame, StoredState &checkpoint,
                   std::vector<melonDS::u8> &baseMainRAM) const;
  bool BuildRestoreChain(melonDS::u32 frame,
                         std::vector<StoredState> &chain) const;
  bool BuildPreimageRestore(melonDS::u32 frame,
                            std::vector<StoredState> &reverseStates,
                            std::vector<melonDS::u8> &latestMainRAM) const;

  void UpdateFrameShadow(melonDS::u32 frame, const melonDS::u8 *mainRAM,
                         std::size_t length);
  melonDS::u32 FrameShadowFrame() const;

private:
  bool HasUsableFrameShadow() const;

  StateMap States_;
  std::vector<melonDS::u8> FrameShadowMainRAM_;
  melonDS::u32 FrameShadowFrame_ = kNoFrame;
};

} // namespace NsmbMvlNetplay::RollbackStorage

#endif
