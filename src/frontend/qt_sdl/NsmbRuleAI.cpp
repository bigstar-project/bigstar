#include "NsmbRuleAI.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstdio>

namespace NsmbRuleAI
{

constexpr int kButtonA = 0;
constexpr int kButtonRight = 4;
constexpr int kButtonLeft = 5;
constexpr int kButtonY = 11;

std::string Upper(std::string value)
{
    for (char& ch : value)
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return value;
}

void PressButton(NsmbNetplayPoC::InputState& input, int bit)
{
    if (bit >= 0 && bit < 12)
        input.KeyMask &= ~(1u << bit);
}

bool ControlsPlayer(const Config& config, int player, int localPlayer)
{
    if (!config.Enabled || player < 0 || player > 1)
        return false;

    const std::string spec = Upper(config.PlayerSpec);
    if (spec == "0" || spec == "PLAYER0" || spec == "MARIO")
        return player == 0;
    if (spec == "1" || spec == "PLAYER1" || spec == "LUIGI")
        return player == 1;
    if (spec == "LOCAL")
        return player == localPlayer;
    return player == (localPlayer ^ 1);
}

std::int64_t SignedCoordinate(melonDS::u32 value)
{
    return static_cast<std::int64_t>(static_cast<std::int32_t>(value));
}

std::int32_t CoordinateDelta(melonDS::u32 target, melonDS::u32 self)
{
    return static_cast<std::int32_t>(
        SignedCoordinate(target) - SignedCoordinate(self));
}

std::int32_t HorizontalDelta(const Config& config, melonDS::u32 target, melonDS::u32 self)
{
    std::int64_t dx = SignedCoordinate(target) - SignedCoordinate(self);
    const std::int64_t wrapWidth = config.HorizontalWrapWidth;
    if (wrapWidth > 0)
    {
        const std::int64_t halfWidth = wrapWidth / 2;
        dx %= wrapWidth;
        if (dx > halfWidth)
            dx -= wrapWidth;
        else if (dx < -halfWidth)
            dx += wrapWidth;
    }
    return static_cast<std::int32_t>(dx);
}

NsmbNetplayPoC::InputState NeutralInputPreservingTouch(const NsmbNetplayPoC::InputState& source)
{
    NsmbNetplayPoC::InputState input {};
    input.KeyMask = 0xFFF;
    input.Touching = source.Touching;
    input.TouchX = source.TouchX;
    input.TouchY = source.TouchY;
    return input;
}

NsmbNetplayPoC::InputState DecideInput(
    const Config& config,
    const FrameState& state,
    int instanceID,
    melonDS::u32 frame,
    int player,
    int localPlayer,
    const NsmbNetplayPoC::InputState& fallback)
{
    if (!ControlsPlayer(config, player, localPlayer) || frame < config.StartFrame)
        return fallback;
    if (!state.InGameplay || !state.Players[player].Found)
        return fallback;

    const int opponent = player ^ 1;
    const PlayerFrameState& self = state.Players[player];
    const PlayerFrameState& other = state.Players[opponent];

    melonDS::u32 targetX = other.X;
    melonDS::u32 targetY = other.Y;
    const char* mode = "chase";

    if (state.StarActorFound)
    {
        targetX = state.StarActorX;
        targetY = state.StarActorY;
        mode = "starActor";
    }
    else if (state.StarFound)
    {
        targetX = state.StarX;
        targetY = state.StarY;
        mode = "star";
    }

    std::int32_t dx = HorizontalDelta(config, targetX, self.X);
    const std::int32_t dy = CoordinateDelta(targetY, self.Y);
    const std::int32_t opponentDx = HorizontalDelta(config, other.X, self.X);
    const int absOpponentDx = std::abs(opponentDx);
    const std::int32_t hazardDx = self.HazardFound ? self.HazardDx : 0;
    const std::int32_t hazardDy = self.HazardFound ? self.HazardDy : 0;
    const bool hazardDanger =
        self.HazardFound &&
        std::abs(hazardDx) <= config.HazardHorizontalRange &&
        std::abs(hazardDy) <= config.HazardVerticalRange &&
        (self.HazardClosing || self.HazardVeryClose);

    if (self.BattleStars > other.BattleStars && absOpponentDx < config.CloseRange)
    {
        dx = opponentDx <= 0 ? config.CloseRange : -config.CloseRange;
        mode = "evade";
    }
    if (hazardDanger)
    {
        const bool hazardOnLeft = hazardDx < 0;
        const bool escapeBlocked = hazardOnLeft ?
            (self.WallRight || self.HoleRight) :
            (self.WallLeft || self.HoleLeft);
        if (escapeBlocked)
            dx = 0;
        else
            dx = hazardOnLeft ? config.CloseRange : -config.CloseRange;
        mode = "hazard";
    }

    NsmbNetplayPoC::InputState input = NeutralInputPreservingTouch(fallback);
    if (dx > config.HorizontalDeadzone)
        PressButton(input, kButtonRight);
    else if (dx < -config.HorizontalDeadzone)
        PressButton(input, kButtonLeft);

    const bool movingHorizontally =
        (input.KeyMask & (1u << kButtonLeft)) == 0 ||
        (input.KeyMask & (1u << kButtonRight)) == 0;
    if (movingHorizontally)
        PressButton(input, kButtonY);

    const int jumpInterval = std::max(1, config.JumpInterval);
    const int jumpFrames = std::clamp(config.JumpFrames, 0, jumpInterval);
    const bool periodicJump =
        movingHorizontally &&
        (frame % static_cast<melonDS::u32>(jumpInterval)) < static_cast<melonDS::u32>(jumpFrames);
    const bool targetAbove = dy < -0x18000;
    const bool closeOpponent = absOpponentDx < config.CloseRange / 2;
    const bool movingLeft = (input.KeyMask & (1u << kButtonLeft)) == 0;
    const bool movingRight = (input.KeyMask & (1u << kButtonRight)) == 0;
    const bool terrainJump =
        (movingLeft && (self.HoleLeft || self.WallLeft)) ||
        (movingRight && (self.HoleRight || self.WallRight)) ||
        (movingHorizontally && (self.HoleAhead || self.WallAhead));
    if (periodicJump || targetAbove || closeOpponent || terrainJump || hazardDanger)
        PressButton(input, kButtonA);

    if (config.TraceEnabled &&
        (config.TraceInterval <= 1 || (frame % static_cast<melonDS::u32>(config.TraceInterval)) == 0))
    {
        std::printf(
            "NSMB RuleAI: inst=%d frame=%u player=%d mode=%s self=%08X/%08X target=%08X/%08X opponent=%08X/%08X stars=%u/%u hazard=%d/%d/%d closing=%d close=%d terrain=ground:%d ahead:%d/%d left:%d/%d right:%d/%d keys=0x%03X\n",
            instanceID,
            frame,
            player,
            mode,
            self.X,
            self.Y,
            targetX,
            targetY,
            other.X,
            other.Y,
            self.BattleStars,
            other.BattleStars,
            hazardDanger ? 1 : 0,
            hazardDx,
            hazardDy,
            self.HazardClosing ? 1 : 0,
            self.HazardVeryClose ? 1 : 0,
            self.GroundBelowSolid ? 1 : 0,
            self.WallAhead ? 1 : 0,
            self.HoleAhead ? 1 : 0,
            self.WallLeft ? 1 : 0,
            self.HoleLeft ? 1 : 0,
            self.WallRight ? 1 : 0,
            self.HoleRight ? 1 : 0,
            input.KeyMask);
    }

    return input;
}

}
