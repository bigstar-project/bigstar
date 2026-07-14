#include "NsmbRollbackStore.h"

#include <algorithm>
#include <set>
#include <utility>

namespace NsmbNetplayPoC::RollbackStorage {

std::size_t CheckpointBytes(const StoredState &checkpoint) {
  return checkpoint.Buffer.size() +
         checkpoint.MainRAMPreimagePages.size() * sizeof(melonDS::u32) +
         checkpoint.MainRAMPreimage.size();
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

} // namespace NsmbNetplayPoC::RollbackStorage
