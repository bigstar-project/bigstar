#include "NsmbSessionProtocol.h"

#include <cstring>

namespace NsmbNetplayPoC::SessionProtocol {

namespace {

constexpr melonDS::u32 kMagic = 0x4C4D534E; // "NSML", little endian
constexpr melonDS::u32 kVersion = 1;
constexpr melonDS::u32 kMatchSeedKind = 0x44454553;  // "SEED", little endian
constexpr melonDS::u32 kStartReadyKind = 0x54525453; // "STRT", little endian

struct WireMessage {
  melonDS::u32 Magic;
  melonDS::u32 Version;
  melonDS::u32 Kind;
  melonDS::u32 Value;
};

static_assert(sizeof(WireMessage) == kSessionPacketSize);

melonDS::u32 ToWireKind(MessageKind kind) {
  switch (kind) {
  case MessageKind::MatchSeed:
    return kMatchSeedKind;
  case MessageKind::StartReady:
    return kStartReadyKind;
  }
  return 0;
}

bool FromWireKind(melonDS::u32 kind, MessageKind &result) {
  switch (kind) {
  case kMatchSeedKind:
    result = MessageKind::MatchSeed;
    return true;
  case kStartReadyKind:
    result = MessageKind::StartReady;
    return true;
  default:
    return false;
  }
}

} // namespace

std::vector<char> Encode(const Message &message) {
  const WireMessage wire{
      kMagic,
      kVersion,
      ToWireKind(message.Kind),
      message.Value,
  };
  std::vector<char> payload(sizeof(wire));
  std::memcpy(payload.data(), &wire, sizeof(wire));
  return payload;
}

bool Decode(const void *data, std::size_t size, Message &message) {
  if (!data || size != sizeof(WireMessage))
    return false;

  WireMessage wire;
  std::memcpy(&wire, data, sizeof(wire));
  MessageKind kind;
  if (wire.Magic != kMagic || wire.Version != kVersion ||
      !FromWireKind(wire.Kind, kind))
    return false;

  message = {kind, wire.Value};
  return true;
}

} // namespace NsmbNetplayPoC::SessionProtocol
