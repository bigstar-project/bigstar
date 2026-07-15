#include "NsmbMvlRuntime.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using NsmbNetplayPoC::Config::MvlConfig;
using NsmbNetplayPoC::MvlRuntime::IsFrameInRange;
using NsmbNetplayPoC::MvlRuntime::IsRoleAllowed;
using NsmbNetplayPoC::MvlRuntime::ResolveResultWinner;
using NsmbNetplayPoC::MvlRuntime::ResultSnapshot;
using NsmbNetplayPoC::MvlRuntime::Runtime;
using NsmbNetplayPoC::MvlRuntime::kNoFrame;

void Require(bool condition, const std::string &message) {
  if (condition)
    return;
  std::cerr << "FAIL: " << message << '\n';
  std::exit(1);
}

void TestGameSelection() {
  Runtime runtime;
  MvlConfig config;
  config.StageSequence = {4, 2};
  config.MatchSeedSequence = {100, 200};

  Require(runtime.GameIndex(-1) == 0 && runtime.GameIndex(16) == 0,
          "invalid instances use the first game");
  Require(runtime.StageForGame(0, config, 3) == 4 &&
              runtime.MatchSeedForGame(0, config) == 100,
          "first game uses the first configured selection");
  runtime.Instances[0].RestartCount = 1;
  Require(runtime.StageForGame(0, config, 3) == 2 &&
              runtime.MatchSeedForGame(0, config) == 200,
          "second game advances stage and seed selections");
  runtime.Instances[0].RestartCount = 9;
  Require(runtime.StageForGame(0, config, 3) == 2 &&
              runtime.MatchSeedForGame(0, config) == 200,
          "selection sequences hold their final value");

  config.StageSequence.clear();
  config.MatchSeedSequence.clear();
  config.CourseMode = "random";
  config.MatchSeedConfigured = true;
  config.MatchSeed = 8;
  runtime.Instances[0].RestartCount = 2;
  Require(runtime.StageForGame(0, config, 3) == 0 &&
              runtime.MatchSeedForGame(0, config) == 10,
          "seeded random course and incremental seed preserve old policy");
  config.CourseMode = "fixed";
  Require(runtime.StageForGame(0, config, -3) == 0 &&
              runtime.StageForGame(0, config, 9) == 4,
          "fixed current stage remains clamped");
}

void TestRestartFramePolicy() {
  Runtime runtime;
  melonDS::u32 zero = 0;
  melonDS::u32 noFrame = kNoFrame;
  runtime.RebaseStartupFrame(1000, zero);
  runtime.RebaseStartupFrame(1000, noFrame);
  Require(zero == 0 && noFrame == kNoFrame,
          "disabled and unlimited frame sentinels are stable");

  melonDS::u32 frame = 900;
  runtime.RebaseStartupFrame(2000, frame);
  Require(frame == 2900, "initial startup frame is relative to process start");
  runtime.SetStartupFrameBase(2000);
  frame = 2900;
  runtime.RebaseStartupFrame(5000, frame);
  Require(frame == 5900, "later restart keeps the original relative delay");

  frame = 5900;
  runtime.SetStartupFrameBase(5000);
  runtime.RebaseStartupFrameFromCheckpoint(8000, 700, frame);
  Require(frame == 8200, "checkpoint restore subtracts captured frame");
  frame = 5100;
  runtime.RebaseStartupFrameFromCheckpoint(8000, 700, frame);
  Require(frame == 8001, "past checkpoint event resumes on the next frame");

  runtime.Instances[1].LastRestartFrame = 40;
  runtime.Instances[7].LastRestartFrame = 120;
  Require(runtime.RestartPacketCutoffFrame() == 120,
          "packet cutoff uses the latest instance restart");
}

void TestInstanceMarkerOwnership() {
  Runtime runtime;
  auto &first = runtime.Instances[0];
  Require(!first.ClearCameraInitHoldApplied && !first.NetRandomPatchApplied,
          "MvL instance markers start clear");

  first.ClearCameraInitHoldApplied = true;
  first.NetRandomPatchApplied = true;
  runtime.ResetStartupHookState(-1);
  runtime.ResetStartupHookState(0);
  Require(!first.ClearCameraInitHoldApplied,
          "restart clears startup hook markers");
  Require(first.NetRandomPatchApplied,
          "restart preserves seed lifecycle markers");
  Require(!runtime.Instances[1].NetRandomPatchApplied,
          "MvL instance markers remain isolated");
}

void TestResultWinnerPolicy() {
  ResultSnapshot result;
  Require(ResolveResultWinner(result) == -1, "fully tied result unresolved");
  result.Dead[0] = 1;
  Require(ResolveResultWinner(result) == 1, "living player beats dead player");
  result.Dead[0] = 0;
  result.BattleStars[0] = 2;
  result.BattleStars[1] = 1;
  result.Lives[0] = 0;
  result.Lives[1] = 9;
  Require(ResolveResultWinner(result) == 0,
          "battle stars take priority over later tie breakers");
  result.BattleStars[0] = result.BattleStars[1] = 1;
  result.DisplayedStars[1] = 2;
  Require(ResolveResultWinner(result) == 1,
          "displayed stars break a battle-star tie");
  result.DisplayedStars[1] = 0;
  result.Lives[0] = result.Lives[1] = 3;
  result.Deaths[0] = 4;
  result.Deaths[1] = 2;
  Require(ResolveResultWinner(result) == 1, "fewer deaths wins last");
}

void TestGameHookPolicy() {
  Require(!IsFrameInRange(99, 100, 200) &&
              IsFrameInRange(100, 100, 200) &&
              IsFrameInRange(200, 100, 200) &&
              !IsFrameInRange(201, 100, 200),
          "runtime patch frame window is inclusive");
  Require(IsFrameInRange(5000, 100, 0),
          "zero end frame keeps a runtime patch active");
  Require(IsRoleAllowed(true, true, false) &&
              !IsRoleAllowed(false, true, false) &&
              IsRoleAllowed(false, false, true) &&
              !IsRoleAllowed(true, false, true),
          "runtime patch role restrictions select one peer");
  Require(!IsRoleAllowed(true, true, true) &&
              !IsRoleAllowed(false, true, true),
          "conflicting runtime patch role restrictions disable both peers");
}

} // namespace

int main() {
  TestGameSelection();
  TestRestartFramePolicy();
  TestInstanceMarkerOwnership();
  TestResultWinnerPolicy();
  TestGameHookPolicy();
  std::cout << "NsmbMvlRuntime tests passed\n";
  return 0;
}
