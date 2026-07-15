#ifndef NSMB_MVL_RUNTIME_H
#define NSMB_MVL_RUNTIME_H

#include "NsmbNetplayConfig.h"
#include "types.h"

#include <array>
#include <cstddef>
#include <vector>

namespace NsmbNetplayPoC::MvlRuntime {

constexpr std::size_t kInstanceCount = 16;
constexpr melonDS::u32 kNoFrame = static_cast<melonDS::u32>(-1);

struct Checkpoint {
  std::vector<char> Buffer;
  melonDS::u32 Frame = 0;
  int Stage = 0;
  bool Logged = false;
};

struct InstanceState {
  bool InResult = false;
  bool ResultScored = false;
  bool ResultUnresolvedLogged = false;
  bool DirectBootApplied = false;
  bool EntranceSpawnNormalizedLogged = false;
  bool ClearCameraInitHoldApplied = false;
  bool NetRandomPatchApplied = false;
  melonDS::u32 ResultFrame = 0;
  melonDS::u32 LastRestartFrame = 0;
  int RestartCount = 0;
  int Wins[2]{};
  Checkpoint BootstrapCheckpoint;
  Checkpoint GameplayCheckpoint;
};

struct ResultSnapshot {
  melonDS::u32 BattleStars[2]{};
  melonDS::u32 DisplayedStars[2]{};
  melonDS::u32 CollectedStars[2]{};
  melonDS::u32 Lives[2]{};
  melonDS::u32 Deaths[2]{};
  melonDS::u32 Dead[2]{};
};

int ResolveResultWinner(const ResultSnapshot &result);

class Runtime {
public:
  bool IsValidInstance(int instanceID) const;
  int GameIndex(int instanceID) const;
  int StageForGame(int instanceID, const Config::MvlConfig &config,
                   int currentStage) const;
  melonDS::u32 MatchSeedForGame(int instanceID,
                                const Config::MvlConfig &config) const;
  melonDS::u32 RestartPacketCutoffFrame() const;
  void ResetStartupHookState(int instanceID);

  melonDS::u32 RelativeStartupFrame(melonDS::u32 value) const;
  void RebaseStartupFrame(melonDS::u32 restartFrame,
                          melonDS::u32 &value) const;
  void RebaseStartupFrameFromCheckpoint(melonDS::u32 restoreFrame,
                                        melonDS::u32 checkpointFrame,
                                        melonDS::u32 &value) const;
  void SetStartupFrameBase(melonDS::u32 frame);

  std::array<InstanceState, kInstanceCount> Instances;

private:
  melonDS::u32 StartupFrameBase_ = 0;
};

} // namespace NsmbNetplayPoC::MvlRuntime

#endif
