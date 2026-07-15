#include "NsmbTestStateHarness.h"

#include "LocalMP.h"
#include "MPInterface.h"
#include "NDS.h"
#include "Platform.h"
#include "Savestate.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace NsmbMvlNetplay::TestStateHarness {
namespace {

std::filesystem::path StatePath(const std::string &dir, int instanceID) {
  char filename[64];
  std::snprintf(filename, sizeof(filename), "inst%d.mln", instanceID);
  return std::filesystem::path(dir) / filename;
}

std::filesystem::path LocalMPStatePath(const std::string &dir) {
  return std::filesystem::path(dir) / "localmp.bin";
}

bool WaitForStateLoadBarrier(Context context, int instanceID) {
  if (context.Bootstrap.TestInstanceCount <= 1)
    return true;

  const auto start = std::chrono::steady_clock::now();
  for (;;) {
    if (context.Coordinator.AllStatesLoaded(
            context.Bootstrap.TestInstanceCount))
      return true;

    if (context.Bootstrap.WaitTimeoutMs > 0) {
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - start)
              .count();
      if (elapsed >= context.Bootstrap.WaitTimeoutMs) {
        std::printf(
            "NSMB Test: state load barrier timeout inst=%d waitedMs=%d\n",
            instanceID, context.Bootstrap.WaitTimeoutMs);
        return false;
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

bool WaitForLocalMPLoadFinished(Context context, int instanceID) {
  const auto start = std::chrono::steady_clock::now();
  for (;;) {
    const auto [finished, loaded] = context.Coordinator.LocalMPLoadStatus();
    if (finished)
      return loaded;

    if (context.Bootstrap.WaitTimeoutMs > 0) {
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - start)
              .count();
      if (elapsed >= context.Bootstrap.WaitTimeoutMs) {
        std::printf(
            "NSMB Test: LocalMP load barrier timeout inst=%d waitedMs=%d\n",
            instanceID, context.Bootstrap.WaitTimeoutMs);
        return false;
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

bool LoadLocalMPStateOnce(Context context, int instanceID) {
  if (context.Harness.StateLoadDir.empty() ||
      !context.Harness.StateLoadFrameSet)
    return false;

  const std::string stateLoadDir = context.Harness.StateLoadDir;
  if (!context.Coordinator.TryBeginLocalMPLoad())
    return WaitForLocalMPLoadFinished(context, instanceID);

  bool loaded = false;
  const std::filesystem::path path = LocalMPStatePath(stateLoadDir);
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::printf("NSMB Test: failed to open LocalMP state for read: %ls\n",
                path.c_str());
  } else {
    std::vector<melonDS::u8> buffer((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());

    if (melonDS::MPInterface::GetType() != melonDS::MPInterface_Local) {
      std::printf("NSMB Test: LocalMP restore skipped because MPInterface is "
                  "not Local\n");
    } else if (auto *localMP = dynamic_cast<melonDS::LocalMP *>(
                   &melonDS::MPInterface::Get())) {
      loaded = localMP->RestoreForTest(buffer.data(), buffer.size());
      std::printf("NSMB Test: loaded LocalMP state path=%ls bytes=%zu ok=%d\n",
                  path.c_str(), buffer.size(), loaded ? 1 : 0);
    } else {
      std::printf(
          "NSMB Test: LocalMP restore failed because LocalMP cast failed\n");
    }
  }

  context.Coordinator.FinishLocalMPLoad(loaded);
  return loaded;
}

} // namespace

bool SaveState(Context context, int instanceID, melonDS::u32 frame,
               melonDS::NDS *nds) {
  if (context.Harness.StateSaveDir.empty() ||
      context.Harness.StateSaveFrame == 0)
    return false;
  if (frame != context.Harness.StateSaveFrame ||
      context.Coordinator.IsStateSaved(instanceID))
    return false;

  std::error_code ec;
  std::filesystem::create_directories(context.Harness.StateSaveDir, ec);
  if (ec) {
    std::printf("NSMB Test: failed to create state save dir: %s (%s)\n",
                context.Harness.StateSaveDir.c_str(), ec.message().c_str());
    return false;
  }

  melonDS::Savestate state;
  if (state.Error || !nds->DoSavestate(&state) || state.Error) {
    std::printf("NSMB Test: failed to create savestate inst=%d frame=%u\n",
                instanceID, frame);
    return false;
  }

  const std::filesystem::path path =
      StatePath(context.Harness.StateSaveDir, instanceID);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    std::printf("NSMB Test: failed to open savestate for write: %ls\n",
                path.c_str());
    return false;
  }

  file.write(reinterpret_cast<const char *>(state.Buffer()), state.Length());
  if (!file) {
    std::printf("NSMB Test: failed to write savestate: %ls\n", path.c_str());
    return false;
  }

  context.Coordinator.MarkStateSaved(instanceID);
  std::printf("NSMB Test: saved state inst=%d frame=%u path=%ls bytes=%u\n",
              instanceID, frame, path.c_str(), state.Length());
  return true;
}

bool SaveLocalMPState(Context context, melonDS::u32 frame) {
  if (context.Harness.StateSaveDir.empty() ||
      context.Harness.StateSaveFrame == 0)
    return false;
  if (frame != context.Harness.StateSaveFrame)
    return false;
  if (!context.Coordinator.TryBeginLocalMPSave(
          context.Bootstrap.TestInstanceCount))
    return false;

  if (melonDS::MPInterface::GetType() != melonDS::MPInterface_Local) {
    std::printf("NSMB Test: LocalMP snapshot skipped because MPInterface is "
                "not Local\n");
    return false;
  }

  auto *localMP =
      dynamic_cast<melonDS::LocalMP *>(&melonDS::MPInterface::Get());
  if (!localMP) {
    std::printf(
        "NSMB Test: LocalMP snapshot failed because LocalMP cast failed\n");
    return false;
  }

  std::vector<melonDS::u8> buffer;
  if (!localMP->SnapshotForTest(buffer) || buffer.empty()) {
    std::printf("NSMB Test: LocalMP snapshot failed\n");
    return false;
  }

  const std::filesystem::path path =
      LocalMPStatePath(context.Harness.StateSaveDir);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    std::printf("NSMB Test: failed to open LocalMP state for write: %ls\n",
                path.c_str());
    return false;
  }

  file.write(reinterpret_cast<const char *>(buffer.data()),
             static_cast<std::streamsize>(buffer.size()));
  if (!file) {
    std::printf("NSMB Test: failed to write LocalMP state: %ls\n",
                path.c_str());
    return false;
  }

  std::printf("NSMB Test: saved LocalMP state frame=%u path=%ls bytes=%zu\n",
              frame, path.c_str(), buffer.size());
  return true;
}

bool LoadState(Context context, int instanceID, melonDS::u32 frame,
               melonDS::NDS *nds) {
  std::string stateLoadDir;
  melonDS::u32 stateLoadFrame = 0;
  {
    std::lock_guard<std::mutex> lock(context.Mutex);
    if (context.Harness.StateLoadDir.empty() ||
        !context.Harness.StateLoadFrameSet)
      return false;
    if (frame != context.Harness.StateLoadFrame ||
        context.Coordinator.IsStateLoaded(instanceID))
      return false;
    stateLoadDir = context.Harness.StateLoadDir;
    stateLoadFrame = context.Harness.StateLoadFrame;
  }

  const std::filesystem::path path = StatePath(stateLoadDir, instanceID);
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::printf("NSMB Test: failed to open savestate for read: %ls\n",
                path.c_str());
    return false;
  }

  std::vector<char> buffer((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  if (buffer.empty()) {
    std::printf("NSMB Test: savestate is empty: %ls\n", path.c_str());
    return false;
  }

  melonDS::Savestate state(buffer.data(),
                           static_cast<melonDS::u32>(buffer.size()), false);
  if (state.Error || !nds->DoSavestate(&state) || state.Error) {
    std::printf(
        "NSMB Test: failed to load savestate inst=%d frame=%u path=%ls\n",
        instanceID, stateLoadFrame, path.c_str());
    return false;
  }

  // NDS savestate loading restores Wifi::PowerOn before Wifi::SetPowerCnt()
  // runs, so the normal power-on side effect can be skipped. Re-register the
  // instance with LocalMP before restoring the shared LocalMP queue snapshot.
  melonDS::Platform::MP_Begin(nds->UserData);

  context.Coordinator.MarkStateLoaded(instanceID);
  std::printf("NSMB Test: loaded state inst=%d frame=%u path=%ls bytes=%zu\n",
              instanceID, stateLoadFrame, path.c_str(), buffer.size());
  WaitForStateLoadBarrier(context, instanceID);
  LoadLocalMPStateOnce(context, instanceID);
  return true;
}

} // namespace NsmbMvlNetplay::TestStateHarness
