#include "NsmbRollbackStore.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using NsmbNetplayPoC::RollbackStorage::CheckpointBytes;
using NsmbNetplayPoC::RollbackStorage::DeltaMode;
using NsmbNetplayPoC::RollbackStorage::Store;
using NsmbNetplayPoC::RollbackStorage::StoredState;

void Require(bool condition, const std::string &message) {
  if (condition)
    return;
  std::cerr << "FAIL: " << message << '\n';
  std::exit(1);
}

StoredState Keyframe(std::initializer_list<melonDS::u8> mainRAM = {}) {
  StoredState state;
  state.Buffer = {'k'};
  state.MainRAMCopy.assign(mainRAM);
  return state;
}

StoredState Delta(melonDS::u32 baseFrame) {
  StoredState state;
  state.Buffer = {'d'};
  state.MainRAMDelta = true;
  state.BaseFrame = baseFrame;
  return state;
}

StoredState Preimage(melonDS::u32 baseFrame) {
  StoredState state;
  state.Buffer = {'p'};
  state.MainRAMFramePreimage = true;
  state.BaseFrame = baseFrame;
  return state;
}

void TestCheckpointBytes() {
  StoredState state;
  state.Buffer.resize(5);
  state.MainRAMPreimagePages.resize(3);
  state.MainRAMPreimage.resize(7);
  state.MainRAMCopy.resize(100);
  state.MainRAMShadowCopy.resize(200);
  Require(CheckpointBytes(state) == 5 + 3 * sizeof(melonDS::u32) + 7,
          "checkpoint byte accounting must preserve the production metric");
}

void TestRestoreChain() {
  Store store;
  store.Put(10, Keyframe());
  store.Put(11, Delta(10));
  store.Put(12, Delta(11));

  std::vector<StoredState> chain;
  Require(store.BuildRestoreChain(12, chain), "complete delta chain");
  Require(chain.size() == 3, "delta chain length");
  Require(!chain[0].MainRAMDelta && chain[1].BaseFrame == 10 &&
              chain[2].BaseFrame == 11,
          "delta chain must be ordered keyframe to target");

  store.Put(13, Delta(99));
  Require(!store.BuildRestoreChain(13, chain), "missing delta base rejected");
  store.Put(14, Delta(14));
  Require(!store.BuildRestoreChain(14, chain), "cyclic delta base rejected");
}

void TestPrepareSaveModes() {
  Store store;
  StoredState checkpoint;
  std::vector<melonDS::u8> base;

  store.PrepareSave(10, DeltaMode::KeyframeDelta, 10, 0, checkpoint, base);
  Require(!checkpoint.MainRAMDelta && base.empty(),
          "empty store forces keyframe");

  store.Put(10, Keyframe({1, 2, 3, 4}));
  store.PrepareSave(11, DeltaMode::KeyframeDelta, 10, 0, checkpoint, base);
  Require(checkpoint.MainRAMDelta && checkpoint.BaseFrame == 10 &&
              base == std::vector<melonDS::u8>({1, 2, 3, 4}),
          "keyframe delta uses latest complete RAM copy");

  store.UpdateFrameShadow(10, base.data(), base.size());
  store.PrepareSave(11, DeltaMode::FrameDelta, 10, 0, checkpoint, base);
  Require(checkpoint.MainRAMDelta && checkpoint.BaseFrame == 10,
          "frame delta uses valid shadow");

  store.PrepareSave(10, DeltaMode::FrameDelta, 10, 10, checkpoint, base);
  Require(!checkpoint.MainRAMDelta && base.empty(),
          "netplay start frame forces keyframe");

  const melonDS::u8 shadow[] = {9, 8, 7};
  store.UpdateFrameShadow(10, shadow, sizeof(shadow));
  store.PrepareSave(11, DeltaMode::Preimage, 10, 0, checkpoint, base);
  Require(checkpoint.MainRAMFramePreimage && checkpoint.BaseFrame == 10 &&
              base == std::vector<melonDS::u8>({9, 8, 7}),
          "preimage mode uses valid frame shadow");
}

void TestPreimageRestoreAndPrune() {
  Store store;
  store.Put(1, Keyframe());
  store.Put(2, Preimage(1));
  store.Put(3, Preimage(2));
  store.Put(20, Keyframe());
  const melonDS::u8 shadow[] = {4, 3, 2, 1};
  store.UpdateFrameShadow(3, shadow, sizeof(shadow));

  std::vector<StoredState> reverse;
  std::vector<melonDS::u8> latest;
  Require(store.BuildPreimageRestore(1, reverse, latest),
          "complete reverse preimage chain");
  Require(reverse.size() == 2 && reverse[0].BaseFrame == 2 &&
              reverse[1].BaseFrame == 1,
          "preimage chain must run newest to target");
  Require(latest == std::vector<melonDS::u8>({4, 3, 2, 1}),
          "latest RAM shadow copied for restoration");

  store.Prune(20, 5);
  Require(store.Size() == 2 && store.States().count(3) == 1 &&
              store.States().count(20) == 1,
          "preimage prune preserves the current shadow frame contract");

  store.UpdateFrameShadow(20, shadow, sizeof(shadow));
  store.Prune(20, 5);
  Require(store.Size() == 1 && store.States().count(20) == 1,
          "unreferenced history outside rollback window is pruned");
}

void TestPrunePreservesDeltaDependencies() {
  Store store;
  store.Put(1, Keyframe());
  store.Put(2, Delta(1));
  store.Put(3, Delta(2));
  store.Put(20, Keyframe());
  const melonDS::u8 shadow[] = {1};
  store.UpdateFrameShadow(3, shadow, sizeof(shadow));

  store.Prune(20, 5);
  Require(store.Size() == 4,
          "prune preserves the shadow's complete delta dependency chain");
}

void TestLatestAndEraseAfter() {
  Store store;
  store.Put(5, Keyframe());
  store.Put(10, Keyframe());
  store.Put(15, Keyframe());

  melonDS::u32 frame = 0;
  StoredState checkpoint;
  Require(store.LatestAtOrBefore(12, frame, checkpoint) && frame == 10,
          "latest checkpoint at or before mismatch");
  Require(!store.LatestAtOrBefore(4, frame, checkpoint),
          "no checkpoint before history start");

  store.EraseAfter(10);
  Require(store.Size() == 2 && store.States().count(15) == 0,
          "future checkpoints erased before resimulation");
}

} // namespace

int main() {
  TestCheckpointBytes();
  TestRestoreChain();
  TestPrepareSaveModes();
  TestPreimageRestoreAndPrune();
  TestPrunePreservesDeltaDependencies();
  TestLatestAndEraseAfter();
  std::cout << "NsmbRollbackStore tests passed\n";
  return 0;
}
