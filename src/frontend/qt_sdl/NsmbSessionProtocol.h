#ifndef NSMBSESSIONPROTOCOL_H
#define NSMBSESSIONPROTOCOL_H

#include "types.h"

#include <cstddef>
#include <vector>

namespace NsmbNetplayPoC::SessionProtocol {

constexpr std::size_t kSessionPacketSize = 16;

enum class MessageKind {
  MatchSeed,
  StartReady,
};

struct Message {
  MessageKind Kind = MessageKind::MatchSeed;
  melonDS::u32 Value = 0;
};

std::vector<char> Encode(const Message &message);
bool Decode(const void *data, std::size_t size, Message &message);

} // namespace NsmbNetplayPoC::SessionProtocol

#endif
