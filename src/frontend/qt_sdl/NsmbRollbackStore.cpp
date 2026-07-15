#include "NsmbRollbackStore.h"

#include <algorithm>
#include <set>
#include <utility>

namespace NsmbMvlNetplay::RollbackStorage {

std::size_t CheckpointBytes(const StoredState &checkpoint) {
  return checkpoint.Buffer.size() +
         checkpoint.MainRAMPreimagePages.size() * sizeof(melonDS::u32) +
         checkpoint.MainRAMPreimage.size();
}

bool ShouldSaveCheckpoint(melonDS::u32 frame, int interval,
                          melonDS::u32 netplayStartFrame) {
  if (interval <= 1)
    return true;
  if (netplayStartFrame != kNoFrame && frame == netplayStartFrame)
    return true;
  return frame % static_cast<melonDS::u32>(interval) == 0;
}

melonDS::u32 ClampResimulationMismatch(melonDS::u32 mismatchFrame,
                                       melonDS::u32 currentFrame,
                                       int maxResimFrames) {
  if (maxResimFrames <= 0 || mismatchFrame >= currentFrame)
    return mismatchFrame;
  const melonDS::u32 maximum = static_cast<melonDS::u32>(maxResimFrames);
  return currentFrame - mismatchFrame > maximum ? currentFrame - maximum
                                                : mismatchFrame;
}

bool IsResimulationDelayElapsed(melonDS::u32 currentFrame,
                                std::optional<melonDS::u32> observedFrame,
                                int delayFrames) {
  return delayFrames <= 0 || !observedFrame ||
         currentFrame >=
             *observedFrame + static_cast<melonDS::u32>(delayFrames);
}

std::size_t StatisticsSnapshot::AverageCheckpointBytes() const {
  return CheckpointSaveCount == 0
             ? 0
             : static_cast<std::size_t>(CheckpointTotalBytes /
                                        CheckpointSaveCount);
}

unsigned long long StatisticsSnapshot::AverageCheckpointSaveUs() const {
  return CheckpointSaveCount == 0 ? 0
                                  : CheckpointSaveTotalUs / CheckpointSaveCount;
}

unsigned long long StatisticsSnapshot::AverageCheckpointRestoreUs() const {
  return CheckpointRestoreOpCount == 0
             ? 0
             : CheckpointRestoreTotalUs / CheckpointRestoreOpCount;
}

unsigned long long StatisticsSnapshot::AverageResimRunFrameUs() const {
  return MeasuredResimFrameCount == 0
             ? 0
             : ResimRunFrameTotalUs / MeasuredResimFrameCount;
}

unsigned long long StatisticsSnapshot::AverageResimCheckpointSaveUs() const {
  return MeasuredResimFrameCount == 0
             ? 0
             : ResimCheckpointSaveTotalUs / MeasuredResimFrameCount;
}

unsigned long long StatisticsSnapshot::AverageResimCorrectionUs() const {
  return MeasuredResimOpCount == 0
             ? 0
             : ResimCorrectionTotalUs / MeasuredResimOpCount;
}

void Statistics::RecordCheckpointSave(std::size_t bytes,
                                      unsigned long long elapsedUs) {
  std::lock_guard<std::mutex> lock(Mutex_);
  Snapshot_.CheckpointSaveCount++;
  Snapshot_.CheckpointLastBytes = bytes;
  if (Snapshot_.CheckpointMinBytes == 0 || bytes < Snapshot_.CheckpointMinBytes)
    Snapshot_.CheckpointMinBytes = bytes;
  Snapshot_.CheckpointMaxBytes =
      std::max(Snapshot_.CheckpointMaxBytes, bytes);
  Snapshot_.CheckpointTotalBytes += static_cast<unsigned long long>(bytes);
  Snapshot_.CheckpointSaveTotalUs += elapsedUs;
  Snapshot_.CheckpointSaveMaxUs =
      std::max(Snapshot_.CheckpointSaveMaxUs, elapsedUs);
}

void Statistics::RecordCheckpointRestore(unsigned long long elapsedUs) {
  std::lock_guard<std::mutex> lock(Mutex_);
  Snapshot_.CheckpointRestoreOpCount++;
  Snapshot_.CheckpointRestoreTotalUs += elapsedUs;
  Snapshot_.CheckpointRestoreMaxUs =
      std::max(Snapshot_.CheckpointRestoreMaxUs, elapsedUs);
}

void Statistics::RecordProbeRestore() {
  std::lock_guard<std::mutex> lock(Mutex_);
  Snapshot_.RestoreCount++;
}

void Statistics::RecordResimulation(
    melonDS::u32 frames, unsigned long long runFrameTotalUs,
    unsigned long long runFrameMaxUs,
    unsigned long long checkpointSaveTotalUs,
    unsigned long long checkpointSaveMaxUs,
    unsigned long long correctionTotalUs) {
  std::lock_guard<std::mutex> lock(Mutex_);
  Snapshot_.ResimulateCount++;
  Snapshot_.MeasuredResimOpCount++;
  Snapshot_.MeasuredResimFrameCount += frames;
  Snapshot_.ResimRunFrameTotalUs += runFrameTotalUs;
  Snapshot_.ResimRunFrameMaxUs =
      std::max(Snapshot_.ResimRunFrameMaxUs, runFrameMaxUs);
  Snapshot_.ResimCheckpointSaveTotalUs += checkpointSaveTotalUs;
  Snapshot_.ResimCheckpointSaveMaxUs =
      std::max(Snapshot_.ResimCheckpointSaveMaxUs, checkpointSaveMaxUs);
  Snapshot_.ResimCorrectionTotalUs += correctionTotalUs;
  Snapshot_.ResimCorrectionMaxUs =
      std::max(Snapshot_.ResimCorrectionMaxUs, correctionTotalUs);
}

StatisticsSnapshot Statistics::Snapshot() const {
  std::lock_guard<std::mutex> lock(Mutex_);
  return Snapshot_;
}

bool Statistics::ShouldTrace(melonDS::u32 frame, melonDS::u32 interval) {
  if (interval == 0 || (frame % interval) != 0)
    return false;
  std::lock_guard<std::mutex> lock(Mutex_);
  if (frame == LastTraceFrame_)
    return false;
  LastTraceFrame_ = frame;
  return true;
}

bool Store::Empty() const { return States_.empty(); }

std::size_t Store::Size() const { return States_.size(); }

const Store::StateMap &Store::States() const { return States_; }

std::size_t Store::Put(melonDS::u32 frame, StoredState checkpoint) {
  States_[frame] = std::move(checkpoint);
  return CheckpointBytes(States_.at(frame));
}

bool Store::Copy(melonDS::u32 frame, StoredState &checkpoint) const {
  const auto state = States_.find(frame);
  if (state == States_.end())
    return false;
  checkpoint = state->second;
  return true;
}

bool Store::LatestAtOrBefore(melonDS::u32 frame, melonDS::u32 &storedFrame,
                             StoredState &checkpoint) const {
  auto state = States_.upper_bound(frame);
  if (state == States_.begin())
    return false;
  --state;
  storedFrame = state->first;
  checkpoint = state->second;
  return true;
}

void Store::EraseAfter(melonDS::u32 frame) {
  States_.erase(States_.upper_bound(frame), States_.end());
}

void Store::Prune(melonDS::u32 currentFrame, melonDS::u32 window) {
  const melonDS::u32 keepFrom =
      currentFrame > window ? currentFrame - window : 0;

  std::set<melonDS::u32> requiredFrames;
  const auto markRequiredChain = [&](melonDS::u32 startFrame) {
    auto start = States_.find(startFrame);
    if (start == States_.end())
      return;
    requiredFrames.insert(startFrame);
    const StoredState *cursor = &start->second;
    for (std::size_t depth = 0; depth < States_.size(); depth++) {
      if (!cursor->MainRAMDelta || cursor->BaseFrame == kNoFrame)
        break;
      if (!requiredFrames.insert(cursor->BaseFrame).second)
        break;
      const auto base = States_.find(cursor->BaseFrame);
      if (base == States_.end())
        break;
      cursor = &base->second;
    }
  };

  for (const auto &[storedFrame, stored] : States_) {
    (void)stored;
    if (storedFrame >= keepFrom)
      markRequiredChain(storedFrame);
  }
  if (FrameShadowFrame_ != kNoFrame)
    markRequiredChain(FrameShadowFrame_);

  for (auto state = States_.begin(); state != States_.end();) {
    if (state->first < keepFrom &&
        requiredFrames.find(state->first) == requiredFrames.end())
      state = States_.erase(state);
    else
      ++state;
  }
}

void Store::PrepareSave(melonDS::u32 frame, DeltaMode mode,
                        melonDS::u32 keyframeInterval,
                        melonDS::u32 netplayStartFrame, StoredState &checkpoint,
                        std::vector<melonDS::u8> &baseMainRAM) const {
  checkpoint = {};
  baseMainRAM.clear();
  if (mode == DeltaMode::None)
    return;

  if (mode == DeltaMode::Preimage) {
    if (HasUsableFrameShadow()) {
      checkpoint.MainRAMFramePreimage = true;
      checkpoint.BaseFrame = FrameShadowFrame_;
      baseMainRAM = FrameShadowMainRAM_;
    }
    return;
  }

  bool forceKeyframe = keyframeInterval <= 1 || States_.empty() ||
                       (frame % keyframeInterval) == 0;
  if (netplayStartFrame != 0 && frame == netplayStartFrame)
    forceKeyframe = true;

  if (mode == DeltaMode::FrameDelta) {
    if (!forceKeyframe && HasUsableFrameShadow()) {
      checkpoint.MainRAMDelta = true;
      checkpoint.BaseFrame = FrameShadowFrame_;
      baseMainRAM = FrameShadowMainRAM_;
    }
    return;
  }

  if (forceKeyframe)
    return;

  auto state = States_.upper_bound(frame);
  while (state != States_.begin()) {
    --state;
    if (!state->second.MainRAMDelta && !state->second.MainRAMCopy.empty()) {
      checkpoint.MainRAMDelta = true;
      checkpoint.BaseFrame = state->first;
      baseMainRAM = state->second.MainRAMCopy;
      return;
    }
  }
}

bool Store::BuildRestoreChain(melonDS::u32 frame,
                              std::vector<StoredState> &chain) const {
  chain.clear();
  auto state = States_.find(frame);
  if (state == States_.end())
    return false;

  for (std::size_t depth = 0; depth <= States_.size(); depth++) {
    chain.push_back(state->second);
    if (!state->second.MainRAMDelta) {
      std::reverse(chain.begin(), chain.end());
      return true;
    }
    state = States_.find(state->second.BaseFrame);
    if (state == States_.end())
      return false;
  }
  return false;
}

bool Store::BuildPreimageRestore(
    melonDS::u32 frame, std::vector<StoredState> &reverseStates,
    std::vector<melonDS::u8> &latestMainRAM) const {
  reverseStates.clear();
  latestMainRAM = FrameShadowMainRAM_;
  melonDS::u32 cursorFrame = FrameShadowFrame_;
  if (cursorFrame == kNoFrame || cursorFrame < frame || latestMainRAM.empty())
    return false;

  for (std::size_t depth = 0; cursorFrame > frame && depth <= States_.size();
       depth++) {
    const auto state = States_.find(cursorFrame);
    if (state == States_.end() || !state->second.MainRAMFramePreimage ||
        state->second.BaseFrame == kNoFrame ||
        state->second.BaseFrame >= cursorFrame)
      return false;
    reverseStates.push_back(state->second);
    cursorFrame = state->second.BaseFrame;
  }
  return cursorFrame == frame;
}

void Store::UpdateFrameShadow(melonDS::u32 frame, const melonDS::u8 *mainRAM,
                              std::size_t length) {
  if (!mainRAM || length == 0) {
    FrameShadowMainRAM_.clear();
    FrameShadowFrame_ = kNoFrame;
    return;
  }
  FrameShadowMainRAM_.assign(mainRAM, mainRAM + length);
  FrameShadowFrame_ = frame;
}

melonDS::u32 Store::FrameShadowFrame() const { return FrameShadowFrame_; }

bool Store::HasUsableFrameShadow() const {
  return FrameShadowFrame_ != kNoFrame && !FrameShadowMainRAM_.empty() &&
         States_.find(FrameShadowFrame_) != States_.end();
}

} // namespace NsmbMvlNetplay::RollbackStorage
