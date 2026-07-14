#pragma once

#include "NsmbGameState.h"
#include "NsmbNetplayPoC.h"

#include <algorithm>
#include <array>

namespace NsmbNetplayPoC::AIObservation {

class TrackingRuntime {
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

  std::array<std::array<AppliedInputRecord, 2>, 16> AppliedInputs{};
  std::array<std::array<bool, 2>, 16> AppliedInputValid{};
  std::array<melonDS::u32, 16> FireballHandler{};
  std::array<std::array<FireballOwnerRecord,
                        GameStateModel::kAIFireballSlotCount>,
             16>
      FireballOwners{};
};

} // namespace NsmbNetplayPoC::AIObservation
