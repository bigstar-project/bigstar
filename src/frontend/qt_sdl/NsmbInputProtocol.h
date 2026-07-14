#ifndef NSMBINPUTPROTOCOL_H
#define NSMBINPUTPROTOCOL_H

#include "NsmbNetplayPoC.h"

#include <cstddef>
#include <vector>

namespace NsmbNetplayPoC::InputProtocol
{

constexpr std::size_t kInputPacketSize = 24;
constexpr std::size_t kInputBundleHeaderSize = 16;
constexpr std::size_t kInputBundleEntrySize = 16;
constexpr std::size_t kMaxInputBundleEntries = 32;

struct FramedInput
{
    melonDS::u32 Frame = 0;
    InputState Input;
};

std::vector<char> EncodeInput(const FramedInput& input);
std::vector<char> EncodeInputBundle(const std::vector<FramedInput>& inputs);
bool DecodeInput(const void* data, std::size_t size, FramedInput& input);
bool DecodeInputBundle(
    const void* data,
    std::size_t size,
    std::vector<FramedInput>& inputs);

}

#endif
