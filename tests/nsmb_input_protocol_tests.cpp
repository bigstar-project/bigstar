#include "NsmbInputProtocol.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{

int Failures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::fprintf(stderr, "line %d: CHECK failed: %s\n", line, expression);
    Failures++;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

std::vector<std::uint8_t> Bytes(const std::vector<char>& payload)
{
    return { payload.begin(), payload.end() };
}

void TestInputGoldenBytesAndRoundTrip()
{
    NsmbMvlNetplay::InputProtocol::FramedInput input;
    input.Generation = 0x55667788;
    input.Frame = 0x11223344;
    input.Input.KeyMask = 0xA5B;
    input.Input.Touching = true;
    input.Input.TouchX = 0x123;
    input.Input.TouchY = 0x45;

    const auto payload = NsmbMvlNetplay::InputProtocol::EncodeInput(input);
    const std::vector<std::uint8_t> expected {
        0x4E, 0x53, 0x4D, 0x4C, 0x02, 0x00, 0x00, 0x00,
        0x88, 0x77, 0x66, 0x55,
        0x44, 0x33, 0x22, 0x11, 0x5B, 0x0A, 0x00, 0x00,
        0x23, 0x01, 0x45, 0x00, 0x01, 0x00, 0x00, 0x00,
    };
    CHECK(Bytes(payload) == expected);

    NsmbMvlNetplay::InputProtocol::FramedInput decoded;
    CHECK(NsmbMvlNetplay::InputProtocol::DecodeInput(payload.data(), payload.size(), decoded));
    CHECK(decoded.Frame == input.Frame);
    CHECK(decoded.Generation == input.Generation);
    CHECK(decoded.Input.KeyMask == input.Input.KeyMask);
    CHECK(decoded.Input.Touching);
    CHECK(decoded.Input.TouchX == input.Input.TouchX);
    CHECK(decoded.Input.TouchY == input.Input.TouchY);
}

void TestBundleGoldenBytesAndRoundTrip()
{
    std::vector<NsmbMvlNetplay::InputProtocol::FramedInput> inputs(2);
    inputs[0].Generation = 3;
    inputs[0].Frame = 7;
    inputs[0].Input.KeyMask = 0xFFE;
    inputs[1].Generation = 3;
    inputs[1].Frame = 8;
    inputs[1].Input.KeyMask = 0xFDF;
    inputs[1].Input.Touching = true;
    inputs[1].Input.TouchX = 10;
    inputs[1].Input.TouchY = 20;

    const auto payload = NsmbMvlNetplay::InputProtocol::EncodeInputBundle(inputs);
    const std::vector<std::uint8_t> expected {
        0x4E, 0x53, 0x4D, 0x4C, 0x02, 0x00, 0x00, 0x00,
        0x49, 0x4E, 0x50, 0x42, 0x03, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00,
        0x07, 0x00, 0x00, 0x00, 0xFE, 0x0F, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00, 0xDF, 0x0F, 0x00, 0x00,
        0x0A, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00,
    };
    CHECK(Bytes(payload) == expected);

    std::vector<NsmbMvlNetplay::InputProtocol::FramedInput> decoded;
    CHECK(NsmbMvlNetplay::InputProtocol::DecodeInputBundle(payload.data(), payload.size(), decoded));
    CHECK(decoded.size() == 2);
    CHECK(decoded[0].Generation == 3);
    CHECK(decoded[0].Frame == 7);
    CHECK(decoded[0].Input.KeyMask == 0xFFE);
    CHECK(decoded[1].Frame == 8);
    CHECK(decoded[1].Input.Touching);
    CHECK(decoded[1].Input.TouchX == 10);
    CHECK(decoded[1].Input.TouchY == 20);
}

void TestMalformedPacketsAreRejected()
{
    const auto validInput = NsmbMvlNetplay::InputProtocol::EncodeInput({ 0, 1, {} });
    NsmbMvlNetplay::InputProtocol::FramedInput decodedInput;
    CHECK(!NsmbMvlNetplay::InputProtocol::DecodeInput(nullptr, validInput.size(), decodedInput));
    CHECK(!NsmbMvlNetplay::InputProtocol::DecodeInput(validInput.data(), validInput.size() - 1, decodedInput));

    auto badInput = validInput;
    badInput[0] = 0;
    CHECK(!NsmbMvlNetplay::InputProtocol::DecodeInput(badInput.data(), badInput.size(), decodedInput));

    badInput = validInput;
    badInput[4] = 1;
    CHECK(!NsmbMvlNetplay::InputProtocol::DecodeInput(badInput.data(), badInput.size(), decodedInput));

    const auto validBundle = NsmbMvlNetplay::InputProtocol::EncodeInputBundle({ { 0, 1, {} } });
    std::vector<NsmbMvlNetplay::InputProtocol::FramedInput> decodedBundle;
    CHECK(!NsmbMvlNetplay::InputProtocol::DecodeInputBundle(nullptr, validBundle.size(), decodedBundle));
    CHECK(!NsmbMvlNetplay::InputProtocol::DecodeInputBundle(validBundle.data(), validBundle.size() - 1, decodedBundle));

    auto badMagic = validBundle;
    badMagic[0] = 0;
    CHECK(!NsmbMvlNetplay::InputProtocol::DecodeInputBundle(badMagic.data(), badMagic.size(), decodedBundle));

    auto badVersion = validBundle;
    badVersion[4] = 1;
    CHECK(!NsmbMvlNetplay::InputProtocol::DecodeInputBundle(badVersion.data(), badVersion.size(), decodedBundle));

    auto badKind = validBundle;
    badKind[8] = 0;
    CHECK(!NsmbMvlNetplay::InputProtocol::DecodeInputBundle(badKind.data(), badKind.size(), decodedBundle));

    auto badCount = validBundle;
    std::fill(badCount.begin() + 16, badCount.begin() + 20, 0);
    CHECK(!NsmbMvlNetplay::InputProtocol::DecodeInputBundle(badCount.data(), badCount.size(), decodedBundle));

    auto tooMany = validBundle;
    tooMany[16] = 33;
    CHECK(!NsmbMvlNetplay::InputProtocol::DecodeInputBundle(tooMany.data(), tooMany.size(), decodedBundle));
    CHECK(NsmbMvlNetplay::InputProtocol::EncodeInputBundle({}).empty());
    CHECK(NsmbMvlNetplay::InputProtocol::EncodeInputBundle(
        std::vector<NsmbMvlNetplay::InputProtocol::FramedInput>(33)).empty());
}

}

int main()
{
    TestInputGoldenBytesAndRoundTrip();
    TestBundleGoldenBytesAndRoundTrip();
    TestMalformedPacketsAreRejected();

    if (Failures != 0)
    {
        std::fprintf(stderr, "nsmb input protocol tests failed: %d\n", Failures);
        return 1;
    }
    std::printf("nsmb input protocol tests passed\n");
    return 0;
}
