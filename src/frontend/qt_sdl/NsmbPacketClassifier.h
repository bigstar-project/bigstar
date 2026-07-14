#ifndef NSMBPACKETCLASSIFIER_H
#define NSMBPACKETCLASSIFIER_H

#include <cstddef>

namespace NsmbNetplayPoC::PacketClassifier {

enum class PacketClass {
  Unknown,
  Input,
  InputBundleCandidate,
  Session,
  NSMLPacket,
  PlayerState,
  WorldState,
  MovingHazardState,
  WorldActorSnapshotState,
  WorldEffectState,
  GameState,
};

struct KnownPacketSizes {
  std::size_t Input = 0;
  std::size_t Session = 0;
  std::size_t NSMLPacket = 0;
  std::size_t PlayerState = 0;
  std::size_t WorldState = 0;
  std::size_t MovingHazardState = 0;
  std::size_t WorldActorSnapshotState = 0;
  std::size_t WorldEffectState = 0;
  std::size_t GameState = 0;
};

PacketClass Classify(std::size_t packetSize, const KnownPacketSizes &sizes);

} // namespace NsmbNetplayPoC::PacketClassifier

#endif
