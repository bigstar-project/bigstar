#include "NsmbMvlRuntime.h"

#include <algorithm>

namespace NsmbMvlNetplay::MvlRuntime {

int ResolveResultWinner(const ResultSnapshot &result) {
  const auto higherWins = [](melonDS::u32 a, melonDS::u32 b) {
    if (a == b)
      return -1;
    return a > b ? 0 : 1;
  };
  const auto lowerWins = [](melonDS::u32 a, melonDS::u32 b) {
    if (a == b)
      return -1;
    return a < b ? 0 : 1;
  };

  const bool player0Dead = result.Dead[0] != 0;
  const bool player1Dead = result.Dead[1] != 0;
  if (player0Dead != player1Dead)
    return player0Dead ? 1 : 0;

  if (const int winner = higherWins(result.BattleStars[0],
                                    result.BattleStars[1]);
      winner >= 0) {
    return winner;
  }
  if (const int winner = higherWins(result.DisplayedStars[0],
                                    result.DisplayedStars[1]);
      winner >= 0) {
    return winner;
  }
  if (const int winner = higherWins(result.CollectedStars[0],
                                    result.CollectedStars[1]);
      winner >= 0) {
    return winner;
  }
  if (const int winner = higherWins(result.Lives[0], result.Lives[1]);
      winner >= 0) {
    return winner;
  }
  if (const int winner = lowerWins(result.Deaths[0], result.Deaths[1]);
      winner >= 0) {
    return winner;
  }
  return -1;
}

bool IsFrameInRange(melonDS::u32 frame, melonDS::u32 startFrame,
                    melonDS::u32 endFrame) {
  return frame >= startFrame && (endFrame == 0 || frame <= endFrame);
}

bool IsRoleAllowed(bool isHost, bool hostOnly, bool clientOnly) {
  return (!hostOnly || isHost) && (!clientOnly || !isHost);
}

bool Runtime::IsValidInstance(int instanceID) const {
  return instanceID >= 0 &&
         instanceID < static_cast<int>(Instances.size());
}

int Runtime::GameIndex(int instanceID) const {
  if (!IsValidInstance(instanceID))
    return 0;
  return std::max(0, Instances[instanceID].RestartCount);
}

int Runtime::StageForGame(int instanceID, const Config::MvlConfig &config,
                          int currentStage) const {
  const int index = GameIndex(instanceID);
  if (!config.StageSequence.empty()) {
    return config.StageSequence[std::min(
        index, static_cast<int>(config.StageSequence.size()) - 1)];
  }
  if (config.CourseMode == "random" && config.MatchSeedConfigured) {
    return static_cast<int>((config.MatchSeed +
                             static_cast<melonDS::u32>(index)) %
                            5u);
  }
  return std::clamp(currentStage, 0, 4);
}

melonDS::u32 Runtime::MatchSeedForGame(
    int instanceID, const Config::MvlConfig &config) const {
  const int index = GameIndex(instanceID);
  if (!config.MatchSeedSequence.empty()) {
    return config.MatchSeedSequence[std::min(
        index, static_cast<int>(config.MatchSeedSequence.size()) - 1)];
  }
  return config.MatchSeed + static_cast<melonDS::u32>(index);
}

melonDS::u32 Runtime::RestartPacketCutoffFrame() const {
  melonDS::u32 cutoff = 0;
  for (const InstanceState &instance : Instances)
    cutoff = std::max(cutoff, instance.LastRestartFrame);
  return cutoff;
}

void Runtime::ResetStartupHookState(int instanceID) {
  if (!IsValidInstance(instanceID))
    return;
  Instances[instanceID].ClearCameraInitHoldApplied = false;
}

melonDS::u32 Runtime::RelativeStartupFrame(melonDS::u32 value) const {
  if (value == 0 || value == kNoFrame)
    return value;
  if (StartupFrameBase_ != 0 && value >= StartupFrameBase_)
    return value - StartupFrameBase_;
  return value;
}

void Runtime::RebaseStartupFrame(melonDS::u32 restartFrame,
                                 melonDS::u32 &value) const {
  if (value == 0 || value == kNoFrame)
    return;
  value = restartFrame + RelativeStartupFrame(value);
}

void Runtime::RebaseStartupFrameFromCheckpoint(
    melonDS::u32 restoreFrame, melonDS::u32 checkpointFrame,
    melonDS::u32 &value) const {
  if (value == 0 || value == kNoFrame)
    return;

  const melonDS::u32 relative = RelativeStartupFrame(value);
  if (relative == 0 || relative == kNoFrame) {
    value = relative;
    return;
  }
  value = restoreFrame +
          (relative > checkpointFrame ? relative - checkpointFrame : 1u);
}

void Runtime::SetStartupFrameBase(melonDS::u32 frame) {
  StartupFrameBase_ = frame;
}

} // namespace NsmbMvlNetplay::MvlRuntime
