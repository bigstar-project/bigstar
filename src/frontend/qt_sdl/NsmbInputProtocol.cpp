#include "NsmbInputProtocol.h"

#include <cstdint>
#include <cstring>

namespace NsmbNetplayPoC::InputProtocol
{

namespace
{

constexpr melonDS::u32 kMagic = 0x4C4D534E; // "NSML", little endian
constexpr melonDS::u32 kVersion = 1;
constexpr melonDS::u32 kInputBundleKind = 0x42504E49; // "INPB", little endian

struct WireInput
{
    melonDS::u32 Magic;
    melonDS::u32 Version;
    melonDS::u32 Frame;
    melonDS::u32 KeyMask;
    melonDS::u16 TouchX;
    melonDS::u16 TouchY;
    melonDS::u8 Touching;
    melonDS::u8 Reserved[3];
};

struct WireInputBundleHeader
{
    melonDS::u32 Magic;
    melonDS::u32 Version;
    melonDS::u32 Kind;
    melonDS::u32 Count;
};

struct WireInputBundleEntry
{
    melonDS::u32 Frame;
    melonDS::u32 KeyMask;
    melonDS::u16 TouchX;
    melonDS::u16 TouchY;
    melonDS::u8 Touching;
    melonDS::u8 Reserved[3];
};

static_assert(sizeof(WireInput) == kInputPacketSize);
static_assert(sizeof(WireInputBundleHeader) == kInputBundleHeaderSize);
static_assert(sizeof(WireInputBundleEntry) == kInputBundleEntrySize);

WireInput ToWireInput(const FramedInput& input)
{
    WireInput wire {};
    wire.Magic = kMagic;
    wire.Version = kVersion;
    wire.Frame = input.Frame;
    wire.KeyMask = input.Input.KeyMask;
    wire.TouchX = input.Input.TouchX;
    wire.TouchY = input.Input.TouchY;
    wire.Touching = input.Input.Touching ? 1 : 0;
    return wire;
}

FramedInput FromWireInput(const WireInput& wire)
{
    return {
        wire.Frame,
        {
            wire.KeyMask,
            wire.Touching != 0,
            wire.TouchX,
            wire.TouchY,
        },
    };
}

}

std::vector<char> EncodeInput(const FramedInput& input)
{
    const WireInput wire = ToWireInput(input);
    std::vector<char> payload(sizeof(wire));
    std::memcpy(payload.data(), &wire, sizeof(wire));
    return payload;
}

std::vector<char> EncodeInputBundle(const std::vector<FramedInput>& inputs)
{
    if (inputs.empty() || inputs.size() > kMaxInputBundleEntries)
        return {};

    WireInputBundleHeader header {};
    header.Magic = kMagic;
    header.Version = kVersion;
    header.Kind = kInputBundleKind;
    header.Count = static_cast<melonDS::u32>(inputs.size());

    std::vector<WireInputBundleEntry> entries;
    entries.reserve(inputs.size());
    for (const FramedInput& input : inputs)
    {
        entries.push_back({
            input.Frame,
            input.Input.KeyMask,
            input.Input.TouchX,
            input.Input.TouchY,
            input.Input.Touching ? static_cast<melonDS::u8>(1) : static_cast<melonDS::u8>(0),
            {},
        });
    }

    std::vector<char> payload(sizeof(header) + entries.size() * sizeof(WireInputBundleEntry));
    std::memcpy(payload.data(), &header, sizeof(header));
    std::memcpy(payload.data() + sizeof(header), entries.data(), entries.size() * sizeof(WireInputBundleEntry));
    return payload;
}

bool DecodeInput(const void* data, std::size_t size, FramedInput& input)
{
    if (!data || size != sizeof(WireInput))
        return false;
    WireInput wire;
    std::memcpy(&wire, data, sizeof(wire));
    if (wire.Magic != kMagic || wire.Version != kVersion)
        return false;
    input = FromWireInput(wire);
    return true;
}

bool DecodeInputBundle(
    const void* data,
    std::size_t size,
    std::vector<FramedInput>& inputs)
{
    inputs.clear();
    if (!data || size < sizeof(WireInputBundleHeader))
        return false;

    WireInputBundleHeader header;
    std::memcpy(&header, data, sizeof(header));
    if (header.Magic != kMagic ||
        header.Version != kVersion ||
        header.Kind != kInputBundleKind ||
        header.Count == 0 ||
        header.Count > kMaxInputBundleEntries ||
        size != sizeof(header) + sizeof(WireInputBundleEntry) * header.Count)
    {
        return false;
    }

    const auto* bytes = static_cast<const std::uint8_t*>(data) + sizeof(header);
    inputs.reserve(header.Count);
    for (melonDS::u32 index = 0; index < header.Count; index++)
    {
        WireInputBundleEntry entry;
        std::memcpy(&entry, bytes + sizeof(entry) * index, sizeof(entry));
        inputs.push_back({
            entry.Frame,
            {
                entry.KeyMask,
                entry.Touching != 0,
                entry.TouchX,
                entry.TouchY,
            },
        });
    }
    return true;
}

}
