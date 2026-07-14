#pragma once

#include "NsmbGameState.h"
#include "NsmbNetplayPoC.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

namespace NsmbNetplayPoC::AIObservation {

enum class LogKind : std::size_t {
  V1,
  V2,
  V3,
  Count,
};

class Runtime {
public:
  struct AppliedInputRecord {
    InputState Input;
    melonDS::u32 Frame = 0;
  };

  void RecordAppliedInput(int instanceID, melonDS::u32 frame, int player,
                          const InputState &input) {
    if (!ValidPlayer(instanceID, player))
      return;
    AppliedInputs[instanceID][player] = {input, frame};
    AppliedInputValid[instanceID][player] = true;
  }

  const AppliedInputRecord *AppliedInput(int instanceID, int player) const {
    if (!ValidPlayer(instanceID, player) ||
        !AppliedInputValid[instanceID][player]) {
      return nullptr;
    }
    return &AppliedInputs[instanceID][player];
  }

  bool OpenLog(LogKind kind, const std::string &path) {
    std::ofstream &stream = Log(kind);
    if (stream.is_open()) {
      stream.flush();
      stream.close();
    }
    stream.clear();
    LinesSinceFlush[LogIndex(kind)] = 0;

    std::error_code dirError;
    const std::filesystem::path logPath(path);
    const std::filesystem::path parent = logPath.parent_path();
    if (!parent.empty())
      std::filesystem::create_directories(parent, dirError);
    stream.open(path, std::ios::out | std::ios::trunc);
    return static_cast<bool>(stream);
  }

  bool CanWriteLog(LogKind kind) const {
    const std::ofstream &stream = Logs[LogIndex(kind)];
    return stream.is_open() && static_cast<bool>(stream);
  }

  std::ofstream &Log(LogKind kind) { return Logs[LogIndex(kind)]; }

  void RecordLogLine(LogKind kind, int flushInterval) {
    if (flushInterval <= 0)
      return;
    int &linesSinceFlush = LinesSinceFlush[LogIndex(kind)];
    linesSinceFlush++;
    if (linesSinceFlush < flushInterval)
      return;
    Log(kind).flush();
    linesSinceFlush = 0;
  }

  void CloseLogs() {
    for (std::ofstream &stream : Logs) {
      if (!stream.is_open())
        continue;
      stream.flush();
      stream.close();
    }
  }

  void UpdateFireballHandler(int instanceID, melonDS::u32 handler) {
    if (!ValidInstance(instanceID) || FireballHandler[instanceID] == handler)
      return;
    FireballHandler[instanceID] = handler;
    ResetFireballOwners(instanceID);
  }

  void ResetFireballOwners(int instanceID) {
    if (!ValidInstance(instanceID))
      return;
    for (FireballOwnerRecord &owner : FireballOwners[instanceID])
      owner = {};
  }

  void InvalidateFireballOwner(int instanceID, int slotIndex) {
    if (!ValidInstance(instanceID) || slotIndex < 0 ||
        slotIndex >= GameStateModel::kAIFireballSlotCount) {
      return;
    }
    FireballOwners[instanceID][slotIndex].Valid = false;
  }

  int ResolveFireballOwner(int instanceID, int slotIndex, int statelessOwner,
                           int statelessConfidence, int statelessHeuristic,
                           int &confidence, int &heuristic, bool &tracked) {
    tracked = false;
    confidence = statelessConfidence;
    heuristic = statelessHeuristic;
    if (!ValidInstance(instanceID) || slotIndex < 0 ||
        slotIndex >= GameStateModel::kAIFireballSlotCount) {
      return statelessOwner;
    }

    FireballOwnerRecord &owner = FireballOwners[instanceID][slotIndex];
    if (statelessOwner >= 0 && statelessConfidence >= 55 &&
        (!owner.Valid || statelessOwner == owner.Player ||
         statelessConfidence >= 80)) {
      owner.Valid = true;
      owner.Player = statelessOwner;
      owner.Confidence =
          std::max(owner.Confidence, std::min(95, statelessConfidence + 20));
      owner.Heuristic = 10 + statelessHeuristic;
    }
    if (!owner.Valid)
      return statelessOwner;

    tracked = true;
    confidence = owner.Confidence;
    heuristic = owner.Heuristic;
    return owner.Player;
  }

private:
  struct FireballOwnerRecord {
    bool Valid = false;
    int Player = -1;
    int Confidence = 0;
    int Heuristic = 0;
  };

  static bool ValidInstance(int instanceID) {
    return instanceID >= 0 && instanceID < 16;
  }
  static bool ValidPlayer(int instanceID, int player) {
    return ValidInstance(instanceID) && player >= 0 && player < 2;
  }
  static constexpr std::size_t LogIndex(LogKind kind) {
    return static_cast<std::size_t>(kind);
  }

  static constexpr std::size_t kLogCount =
      static_cast<std::size_t>(LogKind::Count);

  std::array<std::ofstream, kLogCount> Logs;
  std::array<int, kLogCount> LinesSinceFlush{};
  std::array<std::array<AppliedInputRecord, 2>, 16> AppliedInputs{};
  std::array<std::array<bool, 2>, 16> AppliedInputValid{};
  std::array<melonDS::u32, 16> FireballHandler{};
  std::array<std::array<FireballOwnerRecord,
                        GameStateModel::kAIFireballSlotCount>,
             16>
      FireballOwners{};
};

} // namespace NsmbNetplayPoC::AIObservation
