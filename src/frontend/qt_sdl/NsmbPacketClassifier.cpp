#include "NsmbPacketClassifier.h"

namespace NsmbNetplayPoC::PacketClassifier {

namespace {

bool IsGamePacketSize(std::size_t size, const KnownPacketSizes &known) {
  return size == known.NSMLPacket || size == known.PlayerState ||
         size == known.WorldState || size == known.MovingHazardState ||
         size == known.WorldActorSnapshotState ||
         size == known.WorldEffectState || size == known.GameState;
}

} // namespace

PacketClass Classify(std::size_t packetSize, const KnownPacketSizes &sizes) {
  // Preserve the historical dispatch precedence. Input and session packets
  // win size collisions; larger unknown payloads are attempted as bundles.
  if (packetSize == sizes.Input)
    return PacketClass::Input;
  if (packetSize > sizes.Session && !IsGamePacketSize(packetSize, sizes))
    return PacketClass::InputBundleCandidate;
  if (packetSize == sizes.Session)
    return PacketClass::Session;
  if (packetSize == sizes.NSMLPacket)
    return PacketClass::NSMLPacket;
  if (packetSize == sizes.PlayerState)
    return PacketClass::PlayerState;
  if (packetSize == sizes.WorldState)
    return PacketClass::WorldState;
  if (packetSize == sizes.MovingHazardState)
    return PacketClass::MovingHazardState;
  if (packetSize == sizes.WorldActorSnapshotState)
    return PacketClass::WorldActorSnapshotState;
  if (packetSize == sizes.WorldEffectState)
    return PacketClass::WorldEffectState;
  if (packetSize == sizes.GameState)
    return PacketClass::GameState;
  return PacketClass::Unknown;
}

} // namespace NsmbNetplayPoC::PacketClassifier
