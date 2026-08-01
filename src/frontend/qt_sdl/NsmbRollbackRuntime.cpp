#include "NsmbRollbackRuntime.h"

#include "NDS.h"
#include "Savestate.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace NsmbMvlNetplay::RollbackRuntime {
namespace {

constexpr melonDS::u32 kNoFrame = 0;
constexpr melonDS::u32 kMainRAMBase = 0x02000000;
constexpr melonDS::u32 kMainRAMModeFull = 0;
constexpr melonDS::u32 kMainRAMModeSparse = 1;
constexpr melonDS::u32 kMainRAMModeDelta = 2;
constexpr melonDS::u32 kMainRAMModeSkip = 3;
using StoredState = RollbackStorage::StoredState;

InputState NeutralInput() { return {}; }

InputTimeline::PredictionProbe PredictionProbe(
    const Config::RollbackConfig &config) {
  InputTimeline::PredictionProbe probe;
  probe.Modulo = config.PredictionProbeModulo;
  probe.Offset = config.PredictionProbeOffset;
  probe.Limit = config.PredictionProbeLimit;
  probe.StartFrame = config.PredictionProbeStartFrame;
  if (config.PredictionProbeEndFrame != kNoFrame)
    probe.EndFrame = config.PredictionProbeEndFrame;
  probe.KeyMask = config.PredictionProbeKeyMask;
  probe.ConfirmAfterOneFrame = config.PredictionProbeConfirmAfterOneFrame;
  return probe;
}

unsigned long long ElapsedUs(std::chrono::steady_clock::time_point start) {
  return static_cast<unsigned long long>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start)
          .count());
}

void PruneHistoryLocked(Context &context, melonDS::u32 frame) {
  const melonDS::u32 window =
      context.Config.Window > 0
          ? static_cast<melonDS::u32>(context.Config.Window)
          : 0;
  context.Store.Prune(frame, window);
  context.Inputs.RollbackInputs.Prune(frame, window,
                                      context.Inputs.RemoteInputs);
}

void PrepareDeltaSaveLocked(Context &context, melonDS::u32 frame,
                            StoredState &checkpoint,
                            std::vector<melonDS::u8> &baseMainRAM) {
  RollbackStorage::DeltaMode mode = RollbackStorage::DeltaMode::None;
  if (IsPreimageBackend(context.Config))
    mode = RollbackStorage::DeltaMode::Preimage;
  else if (context.Config.Backend == Config::RollbackBackend::CoreFrameDelta)
    mode = RollbackStorage::DeltaMode::FrameDelta;
  else if (context.Config.Backend == Config::RollbackBackend::CoreDelta)
    mode = RollbackStorage::DeltaMode::KeyframeDelta;
  const melonDS::u32 keyframeInterval =
      context.Config.DeltaKeyframeInterval > 0
          ? static_cast<melonDS::u32>(context.Config.DeltaKeyframeInterval)
          : 0;
  context.Store.PrepareSave(frame, mode, keyframeInterval,
                            context.NetplayStartFrame, checkpoint, baseMainRAM);
}

bool CaptureFramePreimage(const Context &context, StoredState &checkpoint,
                          melonDS::NDS *nds,
                          const std::vector<melonDS::u8> &baseMainRAM) {
  if (!checkpoint.MainRAMFramePreimage)
    return true;
  if (!nds || !nds->MainRAM || baseMainRAM.size() != nds->MainRAMMask + 1)
    return false;

  const melonDS::u32 length = nds->MainRAMMask + 1;
  const melonDS::u32 pageSize =
      static_cast<melonDS::u32>(context.Config.MainRAMPageSize);
  for (melonDS::u32 offset = 0; offset < length; offset += pageSize) {
    const melonDS::u32 pageBytes = std::min(pageSize, length - offset);
    if (std::memcmp(nds->MainRAM + offset, baseMainRAM.data() + offset,
                    pageBytes) == 0)
      continue;
    checkpoint.MainRAMPreimagePages.push_back(offset / pageSize);
    checkpoint.MainRAMPreimage.insert(checkpoint.MainRAMPreimage.end(),
                                      baseMainRAM.begin() + offset,
                                      baseMainRAM.begin() + offset + pageBytes);
  }
  return true;
}

bool SaveCheckpointBuffer(const Context &context, melonDS::NDS *nds,
                          std::vector<char> &buffer,
                          melonDS::u32 mainRAMMode = kMainRAMModeFull,
                          const melonDS::u8 *deltaBaseMainRAM = nullptr) {
  if (!nds)
    return false;

  if (context.Config.Backend == Config::RollbackBackend::TinyCorePreimage) {
    melonDS::Savestate state;
    const bool saved = nds->DoRollbackTinyCoreSavestate(
        &state, static_cast<melonDS::u32>(context.Config.TinyCoreFlags));
    if (state.Error || !saved || state.Error)
      return false;
    buffer.assign(static_cast<const char *>(state.Buffer()),
                  static_cast<const char *>(state.Buffer()) + state.Length());
    return true;
  }

  melonDS::Savestate state;
  const bool coreBackend =
      context.Config.Backend == Config::RollbackBackend::CoreLite ||
      context.Config.Backend == Config::RollbackBackend::CoreSparse ||
      context.Config.Backend == Config::RollbackBackend::CoreDelta ||
      context.Config.Backend == Config::RollbackBackend::CoreFrameDelta ||
      context.Config.Backend == Config::RollbackBackend::CorePreimage;
  const bool saved =
      coreBackend
          ? nds->DoRollbackSavestate(
                &state, mainRAMMode, deltaBaseMainRAM,
                static_cast<melonDS::u32>(context.Config.MainRAMPageSize),
                static_cast<melonDS::u32>(context.Config.CoreSkipMask))
          : nds->DoSavestate(&state);
  if (state.Error || !saved || state.Error)
    return false;
  buffer.assign(static_cast<const char *>(state.Buffer()),
                static_cast<const char *>(state.Buffer()) + state.Length());
  return true;
}

bool RestoreCheckpointBuffer(const Context &context, melonDS::NDS *nds,
                             const std::vector<char> &buffer,
                             const melonDS::u8 *deltaBaseMainRAM = nullptr) {
  if (!nds)
    return false;

  melonDS::Savestate state(const_cast<char *>(buffer.data()),
                           static_cast<melonDS::u32>(buffer.size()), false);
  if (context.Config.Backend == Config::RollbackBackend::TinyCorePreimage) {
    const bool restored = nds->DoRollbackTinyCoreSavestate(
        &state, static_cast<melonDS::u32>(context.Config.TinyCoreFlags));
    return !state.Error && restored && !state.Error;
  }

  const bool coreBackend =
      context.Config.Backend == Config::RollbackBackend::CoreLite ||
      context.Config.Backend == Config::RollbackBackend::CoreSparse ||
      context.Config.Backend == Config::RollbackBackend::CoreDelta ||
      context.Config.Backend == Config::RollbackBackend::CoreFrameDelta ||
      context.Config.Backend == Config::RollbackBackend::CorePreimage;
  const bool restored =
      coreBackend
          ? nds->DoRollbackSavestate(
                &state, kMainRAMModeFull, deltaBaseMainRAM,
                static_cast<melonDS::u32>(context.Config.MainRAMPageSize),
                static_cast<melonDS::u32>(context.Config.CoreSkipMask))
          : nds->DoSavestate(&state);
  return !state.Error && restored && !state.Error;
}

bool RestoreStoredStates(const Context &context, melonDS::NDS *nds,
                         const std::vector<StoredState> &chain) {
  if (chain.empty() || chain.front().MainRAMDelta)
    return false;
  if (!RestoreCheckpointBuffer(context, nds, chain.front().Buffer))
    return false;
  for (std::size_t index = 1; index < chain.size(); index++) {
    if (!chain[index].MainRAMDelta ||
        !RestoreCheckpointBuffer(context, nds, chain[index].Buffer,
                                 nds->MainRAM))
      return false;
  }
  return true;
}

bool RestorePreimageState(const Context &context, melonDS::NDS *nds,
                          const StoredState &checkpoint,
                          const std::vector<StoredState> &reverseStates,
                          const std::vector<melonDS::u8> &latestMainRAM) {
  if (!nds || !nds->MainRAM || latestMainRAM.size() != nds->MainRAMMask + 1)
    return false;
  const melonDS::u32 length = nds->MainRAMMask + 1;
  const melonDS::u32 pageSize =
      static_cast<melonDS::u32>(context.Config.MainRAMPageSize);
  std::memcpy(nds->MainRAM, latestMainRAM.data(), length);
  for (const StoredState &state : reverseStates) {
    std::size_t inputOffset = 0;
    for (const melonDS::u32 page : state.MainRAMPreimagePages) {
      const melonDS::u32 offset = page * pageSize;
      if (offset >= length)
        return false;
      const melonDS::u32 pageBytes = std::min(pageSize, length - offset);
      if (inputOffset + pageBytes > state.MainRAMPreimage.size())
        return false;
      std::memcpy(nds->MainRAM + offset,
                  state.MainRAMPreimage.data() + inputOffset, pageBytes);
      inputOffset += pageBytes;
    }
    if (inputOffset != state.MainRAMPreimage.size())
      return false;
  }
  if (!RestoreCheckpointBuffer(context, nds, checkpoint.Buffer))
    return false;
  InvalidateMainRAMJIT(context.Config, nds, length);
  return true;
}

void RefreshFrameShadowLocked(Context &context, melonDS::u32 frame,
                              melonDS::NDS *nds) {
  if ((context.Config.Backend != Config::RollbackBackend::CoreFrameDelta &&
       !IsPreimageBackend(context.Config)) ||
      !nds || !nds->MainRAM)
    return;
  context.Store.UpdateFrameShadow(frame, nds->MainRAM, nds->MainRAMMask + 1);
}

void SaveCheckpointNowLocked(Context &context, melonDS::u32 frame,
                             melonDS::NDS *nds, bool force = false) {
  if (!nds || context.Config.Window <= 0)
    return;
  if (!force &&
      !RollbackStorage::ShouldSaveCheckpoint(
          frame, context.Config.CheckpointInterval, context.NetplayStartFrame))
    return;

  StoredState checkpoint;
  std::vector<melonDS::u8> deltaBaseMainRAM;
  const auto saveStart = std::chrono::steady_clock::now();
  PrepareDeltaSaveLocked(context, frame, checkpoint, deltaBaseMainRAM);
  const melonDS::u32 mainRAMMode =
      IsPreimageBackend(context.Config)
          ? kMainRAMModeSkip
          : (checkpoint.MainRAMDelta
                 ? kMainRAMModeDelta
                 : (context.Config.Backend ==
                            Config::RollbackBackend::CoreSparse
                        ? kMainRAMModeSparse
                        : kMainRAMModeFull));
  if (checkpoint.MainRAMDelta &&
      (!nds->MainRAM || deltaBaseMainRAM.size() != nds->MainRAMMask + 1))
    return;
  if (!CaptureFramePreimage(context, checkpoint, nds, deltaBaseMainRAM))
    return;
  if (!SaveCheckpointBuffer(context, nds, checkpoint.Buffer, mainRAMMode,
                            checkpoint.MainRAMDelta ? deltaBaseMainRAM.data()
                                                    : nullptr))
    return;
  if (context.Config.Backend == Config::RollbackBackend::CoreDelta &&
      !checkpoint.MainRAMDelta && nds->MainRAM) {
    const melonDS::u32 length = nds->MainRAMMask + 1;
    checkpoint.MainRAMCopy.resize(length);
    std::memcpy(checkpoint.MainRAMCopy.data(), nds->MainRAM, length);
  }
  const std::size_t checkpointBytes =
      context.Store.Put(frame, std::move(checkpoint));
  RefreshFrameShadowLocked(context, frame, nds);
  context.Statistics.RecordCheckpointSave(checkpointBytes,
                                          ElapsedUs(saveStart));
  PruneHistoryLocked(context, frame);
}

} // namespace

const char *BackendName(const Config::RollbackConfig &config) {
  switch (config.Backend) {
  case Config::RollbackBackend::CoreLite:
    return "corelite";
  case Config::RollbackBackend::CoreSparse:
    return "coresparse";
  case Config::RollbackBackend::CoreDelta:
    return "coredelta";
  case Config::RollbackBackend::CoreFrameDelta:
    return "coreframedelta";
  case Config::RollbackBackend::CorePreimage:
    return "corepreimage";
  case Config::RollbackBackend::TinyCorePreimage:
    return "tinycorepreimage";
  case Config::RollbackBackend::Savestate:
  default:
    return "savestate";
  }
}

bool IsPreimageBackend(const Config::RollbackConfig &config) {
  return config.Backend == Config::RollbackBackend::CorePreimage ||
         config.Backend == Config::RollbackBackend::TinyCorePreimage;
}

bool IsValidMainRAMRange(melonDS::NDS *nds, melonDS::u32 address,
                         melonDS::u32 length) {
  if (!nds || !nds->MainRAM || length == 0 || address < kMainRAMBase)
    return false;
  const melonDS::u32 offset = address - kMainRAMBase;
  const melonDS::u32 ramLength = nds->MainRAMMask + 1;
  return offset < ramLength && length <= ramLength - offset;
}

bool ReadMainRAMAddressU32(melonDS::NDS *nds, melonDS::u32 address,
                           melonDS::u32 &value) {
  if (!IsValidMainRAMRange(nds, address, sizeof(value)))
    return false;
  std::memcpy(&value, nds->MainRAM + (address - kMainRAMBase), sizeof(value));
  return true;
}

void InvalidateMainRAMJIT(const Config::RollbackConfig &config,
                          melonDS::NDS *nds, melonDS::u32 length) {
  if (config.SkipJitReset || !nds || length == 0)
    return;
  for (melonDS::u32 offset = 0; offset < length; offset += 0x1000) {
    const melonDS::u32 address = kMainRAMBase + offset;
    nds->JIT.CheckAndInvalidate<0, melonDS::ARMJIT_Memory::memregion_MainRAM>(
        address);
    nds->JIT.CheckAndInvalidate<1, melonDS::ARMJIT_Memory::memregion_MainRAM>(
        address);
  }
}

bool ResolveRemoteInputLocked(Context context, melonDS::u32 frame,
                              InputState &input, bool &predicted) {
  const InputTimeline::PredictionProbe probe = PredictionProbe(context.Config);
  const auto resolved = context.Inputs.RollbackInputs.Resolve(
      frame, context.Inputs.RemoteInputs, NeutralInput(), probe);
  input = resolved.Input;
  predicted = resolved.Predicted;
  return true;
}

void SaveCheckpointIfNeeded(Context context, int instanceID,
                            melonDS::u32 frame, melonDS::NDS *nds) {
  if (!context.Config.Enabled || !context.Input.NetplayOnly || !nds ||
      instanceID < 0 || instanceID >= 16 ||
      (context.NetplayStartFrame != 0 && frame < context.NetplayStartFrame) ||
      context.Config.Window <= 0 ||
      !RollbackStorage::ShouldSaveCheckpoint(
          frame, context.Config.CheckpointInterval, context.NetplayStartFrame))
    return;

  StoredState checkpoint;
  std::vector<melonDS::u8> deltaBaseMainRAM;
  const auto saveStart = std::chrono::steady_clock::now();
  {
    std::lock_guard<std::mutex> lock(context.Mutex);
    PrepareDeltaSaveLocked(context, frame, checkpoint, deltaBaseMainRAM);
  }
  const melonDS::u32 mainRAMMode =
      IsPreimageBackend(context.Config)
          ? kMainRAMModeSkip
          : (checkpoint.MainRAMDelta
                 ? kMainRAMModeDelta
                 : (context.Config.Backend ==
                            Config::RollbackBackend::CoreSparse
                        ? kMainRAMModeSparse
                        : kMainRAMModeFull));
  if (checkpoint.MainRAMDelta &&
      (!nds->MainRAM || deltaBaseMainRAM.size() != nds->MainRAMMask + 1))
    return;
  if (!CaptureFramePreimage(context, checkpoint, nds, deltaBaseMainRAM))
    return;
  if (!SaveCheckpointBuffer(context, nds, checkpoint.Buffer, mainRAMMode,
                            checkpoint.MainRAMDelta ? deltaBaseMainRAM.data()
                                                    : nullptr)) {
    if (context.Input.NetplayTrace)
      std::printf("NSMB Rollback: failed to save checkpoint inst=%d frame=%u\n",
                  instanceID, frame);
    return;
  }
  if (context.Config.Backend == Config::RollbackBackend::CoreDelta &&
      !checkpoint.MainRAMDelta && nds->MainRAM) {
    const melonDS::u32 length = nds->MainRAMMask + 1;
    checkpoint.MainRAMCopy.resize(length);
    std::memcpy(checkpoint.MainRAMCopy.data(), nds->MainRAM, length);
  }
  {
    std::lock_guard<std::mutex> lock(context.Mutex);
    const std::size_t checkpointBytes =
        context.Store.Put(frame, std::move(checkpoint));
    RefreshFrameShadowLocked(context, frame, nds);
    context.Statistics.RecordCheckpointSave(checkpointBytes,
                                            ElapsedUs(saveStart));
    PruneHistoryLocked(context, frame);
  }
}

bool RestoreCheckpointForProbeIfNeeded(Context context, int instanceID,
                                       melonDS::u32 frame, melonDS::NDS *nds) {
  if (!context.Config.Enabled || !context.Config.RestoreProbe ||
      !context.Input.NetplayOnly || !nds || instanceID < 0 || instanceID >= 16)
    return false;

  melonDS::u32 restoreFrame = kNoFrame;
  StoredState checkpoint;
  std::vector<StoredState> restoreChain;
  std::vector<StoredState> reverseStates;
  std::vector<melonDS::u8> latestMainRAM;
  {
    std::lock_guard<std::mutex> lock(context.Mutex);
    const auto pendingFrame =
        context.Inputs.RollbackInputs.PendingRollbackFrame();
    if (!pendingFrame)
      return false;

    restoreFrame = *pendingFrame;
    if (!context.Store.Copy(restoreFrame, checkpoint)) {
      std::printf("NSMB Rollback: cannot restore frame=%u at current=%u, "
                  "checkpoint missing window=%d\n",
                  restoreFrame, frame, context.Config.Window);
      context.Inputs.RollbackInputs.ClearPendingRollbackFrame();
      return false;
    }
    const bool restoreReady =
        IsPreimageBackend(context.Config)
            ? context.Store.BuildPreimageRestore(restoreFrame, reverseStates,
                                                 latestMainRAM)
            : context.Store.BuildRestoreChain(restoreFrame, restoreChain);
    if (!restoreReady) {
      std::printf("NSMB Rollback: cannot restore delta chain frame=%u base=%u "
                  "missing\n",
                  restoreFrame, checkpoint.BaseFrame);
      context.Inputs.RollbackInputs.ClearPendingRollbackFrame();
      return false;
    }
    context.Inputs.RollbackInputs.ClearPendingRollbackFrame();
  }

  const auto restoreStart = std::chrono::steady_clock::now();
  const bool restored = IsPreimageBackend(context.Config)
                            ? RestorePreimageState(context, nds, checkpoint,
                                                   reverseStates, latestMainRAM)
                            : RestoreStoredStates(context, nds, restoreChain);
  if (!restored) {
    std::printf("NSMB Rollback: restore probe failed inst=%d restoreFrame=%u "
                "current=%u\n",
                instanceID, restoreFrame, frame);
    return false;
  }
  const unsigned long long restoreUs = ElapsedUs(restoreStart);
  {
    std::lock_guard<std::mutex> lock(context.Mutex);
    RefreshFrameShadowLocked(context, restoreFrame, nds);
    context.Statistics.RecordCheckpointRestore(restoreUs);
    context.Statistics.RecordProbeRestore();
  }
  std::printf("NSMB Rollback: restore probe loaded frame=%u at current=%u "
              "bytes=%zu\n",
              restoreFrame, frame,
              RollbackStorage::CheckpointBytes(checkpoint));
  std::fflush(stdout);
  return true;
}

bool ResimulateIfNeeded(Context context, const ResimulationHooks &hooks,
                        int instanceID, melonDS::u32 frame, melonDS::NDS *nds) {
  if (!context.Config.Enabled || !context.Config.Resimulate ||
      !context.Input.NetplayOnly || !nds || instanceID < 0 || instanceID >= 16)
    return false;

  melonDS::u32 mismatchFrame = kNoFrame;
  melonDS::u32 restoreFrame = kNoFrame;
  StoredState checkpoint;
  std::vector<StoredState> restoreChain;
  std::vector<StoredState> reverseStates;
  std::vector<melonDS::u8> latestMainRAM;
  {
    std::lock_guard<std::mutex> lock(context.Mutex);
    if (context.Config.PredictionProbeConfirmAfterOneFrame && frame > 0) {
      const melonDS::u32 confirmationFrame = frame - 1;
      const auto confirmation = context.Inputs.RollbackInputs
                                    .TakePredictionProbeConfirmation(
                                        confirmationFrame);
      if (confirmation) {
        const auto stored = context.Inputs.StoreRemote(
            confirmationFrame, *confirmation, frame, true, kNoFrame);
        if (context.Input.NetplayTrace && stored.Confirmation.Mismatch) {
          std::printf(
              "NSMB Rollback: one-frame prediction probe confirmed "
              "frame=%u current=%u\n",
              confirmationFrame, frame);
          std::fflush(stdout);
        }
      }
    }
    const auto pendingFrame =
        context.Inputs.RollbackInputs.PendingRollbackFrame();
    if (!pendingFrame)
      return false;
    mismatchFrame = *pendingFrame;
    if (mismatchFrame == frame) {
      if (context.Input.NetplayTrace) {
        std::printf("NSMB Rollback: current-frame mismatch consumed without "
                    "resim frame=%u\n",
                    frame);
        std::fflush(stdout);
      }
      context.Inputs.RollbackInputs.ClearPendingRollback();
      return false;
    }
    if (mismatchFrame >= frame)
      return false;
    const melonDS::u32 originalMismatchFrame = mismatchFrame;
    mismatchFrame = RollbackStorage::ClampResimulationMismatch(
        mismatchFrame, frame, context.Config.MaxResimFrames);
    if (mismatchFrame != originalMismatchFrame && context.Input.NetplayTrace) {
      std::printf("NSMB Rollback: capping resim window "
                  "originalMismatch=%u cappedMismatch=%u current=%u "
                  "maxFrames=%u\n",
                  originalMismatchFrame, mismatchFrame, frame,
                  static_cast<melonDS::u32>(context.Config.MaxResimFrames));
      std::fflush(stdout);
    }
    const auto observedFrame =
        context.Inputs.RollbackInputs.PendingRollbackObservedFrame();
    if (!RollbackStorage::IsResimulationDelayElapsed(
            frame, observedFrame, context.Config.ResimulateDelayFrames))
      return false;

    if (!context.Store.LatestAtOrBefore(mismatchFrame, restoreFrame,
                                        checkpoint)) {
      std::printf("NSMB Rollback: cannot resimulate mismatch=%u at current=%u, "
                  "checkpoint missing window=%d interval=%d\n",
                  mismatchFrame, frame, context.Config.Window,
                  context.Config.CheckpointInterval);
      context.Inputs.RollbackInputs.ClearPendingRollback();
      return false;
    }
    const bool restoreReady =
        IsPreimageBackend(context.Config)
            ? context.Store.BuildPreimageRestore(restoreFrame, reverseStates,
                                                 latestMainRAM)
            : context.Store.BuildRestoreChain(restoreFrame, restoreChain);
    if (!restoreReady) {
      std::printf("NSMB Rollback: cannot resimulate mismatch=%u from delta "
                  "checkpoint=%u, base=%u chain missing\n",
                  mismatchFrame, restoreFrame, checkpoint.BaseFrame);
      context.Inputs.RollbackInputs.ClearPendingRollback();
      return false;
    }
    context.Inputs.RollbackInputs.ClearPendingRollback();
    context.Store.EraseAfter(restoreFrame);
  }

  const auto rollbackStart = std::chrono::steady_clock::now();
  const auto restoreStart = rollbackStart;
  const bool restored = IsPreimageBackend(context.Config)
                            ? RestorePreimageState(context, nds, checkpoint,
                                                   reverseStates, latestMainRAM)
                            : RestoreStoredStates(context, nds, restoreChain);
  if (!restored) {
    std::printf("NSMB Rollback: resim restore failed inst=%d restoreFrame=%u "
                "current=%u\n",
                instanceID, restoreFrame, frame);
    return false;
  }
  const unsigned long long restoreUs = ElapsedUs(restoreStart);
  {
    std::lock_guard<std::mutex> lock(context.Mutex);
    RefreshFrameShadowLocked(context, restoreFrame, nds);
  }

  const int localPlayer = hooks.CurrentLocalPlayer();
  melonDS::u32 resimulated = 0;
  unsigned long long resimRunFrameTotalUs = 0;
  unsigned long long resimRunFrameMaxUs = 0;
  unsigned long long resimCheckpointSaveTotalUs = 0;
  unsigned long long resimCheckpointSaveMaxUs = 0;
  for (melonDS::u32 resimFrame = restoreFrame; resimFrame < frame;
       resimFrame++) {
    InputTimeline::ReplayFrameInputs replayInputs;
    {
      std::lock_guard<std::mutex> lock(context.Mutex);
      InputTimeline::PredictionProbe probe = PredictionProbe(context.Config);
      // A forced one-frame probe exists only to create the original bad
      // prediction. Replay must consume the confirmed input that triggered
      // rollback, not inject the diagnostic error a second time.
      probe.ConfirmAfterOneFrame = false;
      const auto resolved = InputTimeline::ResolveReplayFrameInputs(
          context.Inputs, resimFrame, localPlayer, NeutralInput(), probe);
      if (!resolved)
        return false;
      replayInputs = *resolved;
    }

    hooks.ApplyFramePatches(instanceID, resimFrame, nds);
    hooks.WritePacketBridgeInputs(instanceID, resimFrame, nds, localPlayer,
                                  replayInputs.Local, replayInputs.Remote, true,
                                  replayInputs.RemotePredicted);

    const InputState runtimeLocalInput =
        hooks.PrepareRuntimeInput(replayInputs.Local);
    nds->SetKeyMask(runtimeLocalInput.KeyMask);
    if (runtimeLocalInput.Touching)
      nds->TouchScreen(runtimeLocalInput.TouchX, runtimeLocalInput.TouchY);
    else
      nds->ReleaseScreen();

    const bool skipRender = context.Config.SkipRenderDuringResim;
    if (skipRender)
      nds->GPU.SetRollbackSkipRender(true);
    const auto runFrameStart = std::chrono::steady_clock::now();
    nds->RunFrame();
    const unsigned long long runFrameUs = ElapsedUs(runFrameStart);
    resimRunFrameTotalUs += runFrameUs;
    resimRunFrameMaxUs = std::max(resimRunFrameMaxUs, runFrameUs);
    if (skipRender)
      nds->GPU.SetRollbackSkipRender(false);
    hooks.ApplyPostFramePatches(resimFrame + 1, nds);
    resimulated++;

    // The ordinary before-frame path saves `frame` after resimulation returns.
    // Saving the final replayed frame here only serializes the same checkpoint
    // twice; retain intermediate checkpoints solely when explicitly requested.
    const bool saveResimCheckpoint =
        RollbackStorage::ShouldSaveResimulationCheckpoint(
            resimFrame + 1, frame,
            context.Config.SkipIntermediateResimCheckpoints);
    if (saveResimCheckpoint) {
      const auto checkpointSaveStart = std::chrono::steady_clock::now();
      {
        std::lock_guard<std::mutex> lock(context.Mutex);
        SaveCheckpointNowLocked(context, resimFrame + 1, nds);
      }
      const unsigned long long checkpointSaveUs =
          ElapsedUs(checkpointSaveStart);
      resimCheckpointSaveTotalUs += checkpointSaveUs;
      resimCheckpointSaveMaxUs =
          std::max(resimCheckpointSaveMaxUs, checkpointSaveUs);
    }

    if (nds->NumFrames != resimFrame + 1 && context.Input.NetplayTrace)
      std::printf("NSMB Rollback: resim frame counter drift expected=%u "
                  "actual=%u\n",
                  resimFrame + 1, nds->NumFrames);
  }

  const unsigned long long rollbackTotalUs = ElapsedUs(rollbackStart);
  {
    std::lock_guard<std::mutex> lock(context.Mutex);
    RefreshFrameShadowLocked(context, frame, nds);
    context.Statistics.RecordCheckpointRestore(restoreUs);
    context.Statistics.RecordResimulation(
        resimulated, resimRunFrameTotalUs, resimRunFrameMaxUs,
        resimCheckpointSaveTotalUs, resimCheckpointSaveMaxUs, rollbackTotalUs);
  }
  if (context.Input.NetplayTrace) {
    std::printf("NSMB Rollback: resimulated from checkpoint=%u mismatch=%u to "
                "current=%u frames=%u bytes=%zu restoreUs=%llu runUs=%llu "
                "runMaxUs=%llu checkpointSaveUs=%llu "
                "checkpointSaveMaxUs=%llu totalUs=%llu\n",
                restoreFrame, mismatchFrame, frame, resimulated,
                RollbackStorage::CheckpointBytes(checkpoint), restoreUs,
                resimRunFrameTotalUs, resimRunFrameMaxUs,
                resimCheckpointSaveTotalUs, resimCheckpointSaveMaxUs,
                rollbackTotalUs);
    std::fflush(stdout);
  }
  return true;
}

void TraceStatsIfNeeded(Context context, melonDS::u32 frame) {
  if (!context.Config.Enabled || !context.Input.NetplayTrace ||
      !context.Statistics.ShouldTrace(frame, 120))
    return;

  std::lock_guard<std::mutex> lock(context.Mutex);
  const RollbackStorage::StatisticsSnapshot stats =
      context.Statistics.Snapshot();
  std::size_t deltaCheckpoints = 0;
  std::size_t keyframeCheckpoints = 0;
  std::size_t preimageCheckpoints = 0;
  std::size_t preimageBytes = 0;
  std::size_t mainRAMCopyBytes = 0;
  for (const auto &[storedFrame, stored] : context.Store.States()) {
    (void)storedFrame;
    if (stored.MainRAMDelta)
      deltaCheckpoints++;
    if (stored.MainRAMFramePreimage) {
      preimageCheckpoints++;
      preimageBytes +=
          stored.MainRAMPreimagePages.size() * sizeof(melonDS::u32) +
          stored.MainRAMPreimage.size();
    } else if ((context.Config.Backend == Config::RollbackBackend::CoreDelta ||
                context.Config.Backend ==
                    Config::RollbackBackend::CoreFrameDelta) &&
               !stored.Buffer.empty()) {
      keyframeCheckpoints++;
    }
    mainRAMCopyBytes += stored.MainRAMCopy.size();
  }
  std::printf(
      "NSMB Rollback: frame=%u backend=%s checkpoints=%zu checkpointSaves=%u "
      "bytesLast=%zu bytesMin=%zu bytesMax=%zu bytesAvg=%zu saveAvgUs=%llu "
      "saveMaxUs=%llu restoreOps=%u restoreAvgUs=%llu restoreMaxUs=%llu "
      "resimOps=%u resimFrames=%llu resimRunAvgUs=%llu resimRunMaxUs=%llu "
      "resimSaveAvgUs=%llu resimSaveMaxUs=%llu resimTotalAvgUs=%llu "
      "resimTotalMaxUs=%llu delta=%zu keyframes=%zu preimages=%zu "
      "preimageBytes=%zu mainRAMCopies=%zu keyInt=%d page=%d coreSkip=0x%X "
      "tinyFlags=0x%X predicted=%zu predictions=%u predProbe=%u mismatches=%u "
      "restores=%u resims=%u pending=%u observed=%u\n",
      frame, BackendName(context.Config), context.Store.Size(),
      stats.CheckpointSaveCount, stats.CheckpointLastBytes,
      stats.CheckpointMinBytes, stats.CheckpointMaxBytes,
      stats.AverageCheckpointBytes(), stats.AverageCheckpointSaveUs(),
      stats.CheckpointSaveMaxUs, stats.CheckpointRestoreOpCount,
      stats.AverageCheckpointRestoreUs(), stats.CheckpointRestoreMaxUs,
      stats.MeasuredResimOpCount, stats.MeasuredResimFrameCount,
      stats.AverageResimRunFrameUs(), stats.ResimRunFrameMaxUs,
      stats.AverageResimCheckpointSaveUs(), stats.ResimCheckpointSaveMaxUs,
      stats.AverageResimCorrectionUs(), stats.ResimCorrectionMaxUs,
      deltaCheckpoints, keyframeCheckpoints, preimageCheckpoints, preimageBytes,
      mainRAMCopyBytes, context.Config.DeltaKeyframeInterval,
      context.Config.MainRAMPageSize, context.Config.CoreSkipMask,
      context.Config.TinyCoreFlags,
      context.Inputs.RollbackInputs.Predictions().size(),
      context.Inputs.RollbackInputs.PredictionCount(),
      context.Inputs.RollbackInputs.PredictionProbeCount(),
      context.Inputs.RollbackInputs.MismatchCount(), stats.RestoreCount,
      stats.ResimulateCount,
      context.Inputs.RollbackInputs.PendingRollbackFrame().value_or(kNoFrame),
      context.Inputs.RollbackInputs.PendingRollbackObservedFrame().value_or(
          kNoFrame));
  std::fflush(stdout);
}

} // namespace NsmbMvlNetplay::RollbackRuntime
