#ifndef NSMB_ROLLBACK_STORE_H
#define NSMB_ROLLBACK_STORE_H

#include "types.h"

#include <cstddef>
#include <map>
#include <vector>

namespace NsmbNetplayPoC::RollbackStorage {

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

} // namespace NsmbNetplayPoC::RollbackStorage

#endif
