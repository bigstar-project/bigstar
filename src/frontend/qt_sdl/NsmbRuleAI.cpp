#include "NsmbRuleAI.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstdio>

namespace NsmbRuleAI
{

constexpr int kButtonB = 1;
constexpr int kButtonRight = 4;
constexpr int kButtonLeft = 5;
constexpr int kButtonY = 11;
constexpr int kHazardEscapeHoldFrames = 18;
constexpr int kHazardJumpCycleFrames = 20;
constexpr int kHazardJumpPressFrames = 4;
constexpr int kHazardJumpPulseFrames = 10;
constexpr int kHazardJumpPulseCooldownFrames = 24;
constexpr int kUnsafeStarAvoidFrames = 72;
constexpr int kHazardCategoryEnemyGoomba = 3;
constexpr int kHazardCategoryEnemyKoopa = 4;
constexpr int kEnemyStompHorizontalRange = 0x30000;
constexpr int kEnemyStompVerticalRange = 0x12000;
constexpr int kUnsafeStarFloorGapRange = 0x200000;

struct PlayerMemory
{
    bool Initialized = false;
    melonDS::u32 LastFrame = 0;
    std::int64_t LastX = 0;
    int StillFrames = 0;
    int EscapeFrames = 0;
    int EscapeDirection = 0;
    int HazardEscapeFrames = 0;
    int HazardEscapeDirection = 0;
    int HazardFrames = 0;
    int HazardLastSide = 0;
    int HazardLastCategoryID = 0;
    int JumpReleaseFrames = 0;
    int JumpPressFrames = 0;
    int JumpCooldownFrames = 0;
    int LastHorizontalIntent = 0;
    int UnsafeStarAvoidFrames = 0;
    int UnsafeStarRejectFrames = 0;
    int UnsafeStarEscapeDirection = 0;
    bool LastJumpPressed = false;
};

PlayerMemory GPlayerMemory[16][2] {};

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

int SignWithDeadzone(std::int32_t value, int deadzone)
{
    if (value > deadzone)
        return 1;
    if (value < -deadzone)
        return -1;
    return 0;
}

struct StarSafety
{
    bool Unsafe = false;
    bool ActiveAvoid = false;
};

StarSafety EvaluateStarSafety(
    const Config& config,
    const PlayerFrameState& self,
    melonDS::u32 starX,
    melonDS::u32 starY)
{
    const std::int32_t starDx = HorizontalDelta(config, starX, self.X);
    const std::int32_t starDy = CoordinateDelta(starY, self.Y);
    const int starIntent = SignWithDeadzone(starDx, config.HorizontalDeadzone);

    // NSMB stage coordinates decrease as objects move downward on screen.
    const bool starBelow = starDy < -0x18000;
    const bool starNotHighAbove = starDy < 0x20000;
    const bool targetSideIsHole =
        (starIntent < 0 && self.HoleLeft) ||
        (starIntent > 0 && self.HoleRight) ||
        (starIntent == 0 && self.HoleAhead);
    const bool targetSideHasFloorGap =
        targetSideIsHole ||
        (starIntent < 0 && self.FarHoleLeft) ||
        (starIntent > 0 && self.FarHoleRight);
    const bool pitMouth =
        self.GroundBelowSolid &&
        starBelow &&
        (self.HoleAhead || targetSideIsHole);
    const bool starBehindFloorGap =
        self.GroundBelowSolid &&
        starNotHighAbove &&
        targetSideHasFloorGap &&
        std::abs(starDx) <= kUnsafeStarFloorGapRange;
    const bool nearbyDeepDropStar =
        starBelow &&
        starDy < -0x30000 &&
        std::abs(starDx) <= 0x80000;
    const bool alreadyInVerticalPit =
        (self.BlockedLeft && self.BlockedRight) ||
        (!self.GroundBelowSolid && (self.HoleAhead || self.HoleLeft || self.HoleRight));

    StarSafety safety {};
    safety.Unsafe = starBehindFloorGap || nearbyDeepDropStar || pitMouth || alreadyInVerticalPit;
    safety.ActiveAvoid =
        pitMouth ||
        alreadyInVerticalPit ||
        (nearbyDeepDropStar && (self.HoleAhead || targetSideIsHole || !self.GroundBelowSolid));
    return safety;
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
    PlayerMemory* memory = nullptr;
    if (instanceID >= 0 && instanceID < 16)
        memory = &GPlayerMemory[instanceID][player];
    if (memory && memory->Initialized && frame < memory->LastFrame)
        *memory = {};

    if (self.Dead)
    {
        if (memory)
            *memory = {};
        return NeutralInputPreservingTouch(fallback);
    }

    const StarSafety starActorSafety = state.StarActorFound ?
        EvaluateStarSafety(config, self, state.StarActorX, state.StarActorY) :
        StarSafety {};
    const StarSafety starCandidateSafety = (!state.StarActorFound && state.StarFound) ?
        EvaluateStarSafety(config, self, state.StarX, state.StarY) :
        StarSafety {};
    const bool starActorUnsafe = starActorSafety.Unsafe;
    const bool starCandidateUnsafe = starCandidateSafety.Unsafe;
    const bool starActorActiveAvoid = starActorSafety.ActiveAvoid;
    const bool starCandidateActiveAvoid = starCandidateSafety.ActiveAvoid;
    if (memory && (starActorUnsafe || starCandidateUnsafe))
        memory->UnsafeStarRejectFrames = std::max(memory->UnsafeStarRejectFrames, kUnsafeStarAvoidFrames);
    if (memory && (starActorActiveAvoid || starCandidateActiveAvoid))
    {
        memory->UnsafeStarAvoidFrames =
            std::max(memory->UnsafeStarAvoidFrames, kUnsafeStarAvoidFrames);
        const melonDS::u32 unsafeStarX = starActorActiveAvoid ? state.StarActorX : state.StarX;
        int escapeDirection = -SignWithDeadzone(
            HorizontalDelta(config, unsafeStarX, self.X),
            config.HorizontalDeadzone);
        if (escapeDirection == 0)
            escapeDirection = SignWithDeadzone(-self.VelX, 0x800);
        if (escapeDirection == 0 && memory->LastHorizontalIntent != 0)
            escapeDirection = -memory->LastHorizontalIntent;
        memory->UnsafeStarEscapeDirection = std::clamp(escapeDirection, -1, 1);
    }
    else if (memory &&
             memory->UnsafeStarAvoidFrames > 0 &&
             self.GroundBelowSolid &&
             !self.HoleAhead &&
             !self.HoleLeft &&
             !self.HoleRight)
    {
        memory->UnsafeStarAvoidFrames = 0;
        memory->UnsafeStarEscapeDirection = 0;
    }
    const bool avoidingUnsafeStar = memory && memory->UnsafeStarAvoidFrames > 0;
    if (memory && memory->UnsafeStarAvoidFrames > 0)
        memory->UnsafeStarAvoidFrames--;
    const bool rejectingUnsafeStar = memory && memory->UnsafeStarRejectFrames > 0;
    if (memory && memory->UnsafeStarRejectFrames > 0)
        memory->UnsafeStarRejectFrames--;
    const bool starActorUsable = state.StarActorFound && !starActorUnsafe && !avoidingUnsafeStar && !rejectingUnsafeStar;
    const bool starCandidateUsable =
        !state.StarActorFound &&
        state.StarFound &&
        !starCandidateUnsafe &&
        !avoidingUnsafeStar &&
        !rejectingUnsafeStar &&
        self.BattleStars <= other.BattleStars;

    melonDS::u32 targetX = other.X;
    melonDS::u32 targetY = other.Y;
    const char* mode = "chase";

    if (starActorUsable)
    {
        targetX = state.StarActorX;
        targetY = state.StarActorY;
        mode = "starActor";
    }
    else if (starCandidateUsable)
    {
        targetX = state.StarX;
        targetY = state.StarY;
        mode = "star";
    }

    std::int32_t dx = HorizontalDelta(config, targetX, self.X);
    const std::int32_t rawDx = CoordinateDelta(targetX, self.X);
    const std::int32_t dy = CoordinateDelta(targetY, self.Y);
    const std::int32_t opponentDx = HorizontalDelta(config, other.X, self.X);
    const int absOpponentDx = std::abs(opponentDx);
    const std::int32_t hazardDx = self.HazardFound ? self.HazardDx : 0;
    const std::int32_t hazardDy = self.HazardFound ? self.HazardDy : 0;
    const bool stompableEnemyHazard =
        self.HazardCategoryID == kHazardCategoryEnemyGoomba ||
        self.HazardCategoryID == kHazardCategoryEnemyKoopa;
    const bool hazardDanger =
        self.HazardFound &&
        std::abs(hazardDx) <= config.HazardHorizontalRange &&
        std::abs(hazardDy) <= config.HazardVerticalRange &&
        (self.HazardClosing || self.HazardVeryClose);
    const bool evadingOpponent = self.BattleStars > other.BattleStars && absOpponentDx < config.CloseRange;
    const bool starTargetVisible = starActorUsable || starCandidateUsable;
    bool forceJump = false;
    if (avoidingUnsafeStar && !hazardDanger)
    {
        int safeIntent = memory ? memory->UnsafeStarEscapeDirection : 0;
        if (safeIntent < 0 && (self.HoleLeft || self.BlockedLeft) && !(self.HoleRight || self.BlockedRight))
            safeIntent = 1;
        else if (safeIntent > 0 && (self.HoleRight || self.BlockedRight) && !(self.HoleLeft || self.BlockedLeft))
            safeIntent = -1;
        else if (self.HoleLeft && !self.HoleRight && !self.BlockedRight)
            safeIntent = 1;
        else if (self.HoleRight && !self.HoleLeft && !self.BlockedLeft)
            safeIntent = -1;
        else if (safeIntent == 0 && memory && memory->LastHorizontalIntent != 0)
            safeIntent = -memory->LastHorizontalIntent;
        else if (safeIntent == 0)
            safeIntent = SignWithDeadzone(-self.VelX, 0x800);
        if (safeIntent == 0)
            safeIntent = SignWithDeadzone(opponentDx, config.HorizontalDeadzone);
        safeIntent = std::clamp(safeIntent, -1, 1);
        dx = safeIntent * std::max(config.HorizontalDeadzone + 1, config.CloseRange / 2);
        mode = safeIntent == 0 ? "holeStarHold" : "holeStarAvoid";
    }
    else if (self.BattleStars > other.BattleStars)
    {
        if (evadingOpponent)
        {
            dx = opponentDx <= 0 ? config.CloseRange : -config.CloseRange;
            mode = "evade";
        }
        else if (!starTargetVisible)
        {
            const int brakeIntent = SignWithDeadzone(-self.VelX, 0x800);
            dx = brakeIntent * std::max(config.HorizontalDeadzone + 1, config.CloseRange / 2);
            mode = brakeIntent == 0 ? "guardLead" : "guardBrake";
        }
    }
    if (hazardDanger)
    {
        const bool hazardOnLeft = hazardDx < 0;
        const int hazardSide = hazardOnLeft ? -1 : 1;
        int escapeDirection = hazardOnLeft ? 1 : -1;
        const int hazardDirection = hazardOnLeft ? -1 : 1;
        const bool stompWindow =
            stompableEnemyHazard &&
            std::abs(hazardDx) <= kEnemyStompHorizontalRange &&
            std::abs(hazardDy) <= kEnemyStompVerticalRange;
        if (memory)
        {
            if (memory->HazardLastSide != 0 &&
                (memory->HazardLastSide != hazardSide ||
                 memory->HazardLastCategoryID != self.HazardCategoryID))
            {
                memory->HazardEscapeFrames = 0;
                memory->HazardEscapeDirection = 0;
                memory->HazardFrames = 0;
            }
            if (memory->HazardEscapeFrames > 0 && memory->HazardEscapeDirection != 0)
                escapeDirection = memory->HazardEscapeDirection;
            memory->HazardLastSide = hazardSide;
            memory->HazardLastCategoryID = self.HazardCategoryID;
            memory->HazardEscapeDirection = escapeDirection;
            memory->HazardEscapeFrames = kHazardEscapeHoldFrames;
            memory->HazardFrames++;
        }
        const bool escapeBlocked = escapeDirection > 0 ?
            (self.BlockedRight || self.HoleRight) :
            (self.BlockedLeft || self.HoleLeft);
        const bool hazardSideBlocked = hazardDirection > 0 ?
            (self.BlockedRight || self.HoleRight) :
            (self.BlockedLeft || self.HoleLeft);
        if (escapeBlocked)
        {
            if (stompWindow && !hazardSideBlocked)
            {
                dx = hazardDirection * config.CloseRange;
                forceJump = true;
                mode = "hazardStomp";
            }
            else
            {
                dx = 0;
                forceJump = stompWindow;
                mode = "hazardHold";
            }
        }
        else
        {
            dx = escapeDirection * config.CloseRange;
            forceJump = stompWindow;
            mode = "hazard";
        }
    }
    else if (memory)
    {
        if (memory->HazardEscapeFrames > 0)
            memory->HazardEscapeFrames--;
        else
            memory->HazardEscapeDirection = 0;
        memory->HazardFrames = 0;
        memory->HazardLastSide = 0;
        memory->HazardLastCategoryID = 0;
    }

    int horizontalIntent = SignWithDeadzone(dx, config.HorizontalDeadzone);
    const int rawIntent = SignWithDeadzone(rawDx, config.HorizontalDeadzone);
    const bool blockedLeft = self.BlockedLeft || self.HoleLeft;
    const bool blockedRight = self.BlockedRight || self.HoleRight;
    const bool canRawRouteLeft = rawIntent < 0 && !blockedLeft;
    const bool canRawRouteRight = rawIntent > 0 && !blockedRight;
    const bool holeRecovery = false;
    if (evadingOpponent &&
        ((horizontalIntent < 0 && blockedLeft) ||
         (horizontalIntent > 0 && blockedRight)))
    {
        horizontalIntent = 0;
        dx = 0;
        if (memory)
        {
            memory->EscapeDirection = 0;
            memory->EscapeFrames = 0;
            memory->StillFrames = 0;
        }
        mode = "evadeHold";
    }
    if (!hazardDanger && !evadingOpponent &&
        ((horizontalIntent < 0 && blockedLeft && canRawRouteRight) ||
         (horizontalIntent > 0 && blockedRight && canRawRouteLeft) ||
         (horizontalIntent == 0 && ((self.BlockedLeft && canRawRouteRight) || (self.BlockedRight && canRawRouteLeft)))))
    {
        horizontalIntent = rawIntent;
        dx = horizontalIntent * std::max(config.HorizontalDeadzone + 1, config.CloseRange / 2);
        if (memory)
        {
            memory->EscapeDirection = horizontalIntent;
            memory->EscapeFrames = 0;
            memory->StillFrames = 0;
        }
        mode = "rawWallRoute";
    }
    const bool intentBlocked =
        (horizontalIntent < 0 && blockedLeft) ||
        (horizontalIntent > 0 && blockedRight);

    if (memory)
    {
        const std::int64_t currentX = SignedCoordinate(self.X);
        if (!memory->Initialized || frame < memory->LastFrame)
        {
            memory->Initialized = true;
            memory->StillFrames = 0;
            memory->EscapeFrames = 0;
        }
        else
        {
            const std::int64_t movement = currentX - memory->LastX;
            if (horizontalIntent != 0 && std::llabs(movement) < 0x300)
                memory->StillFrames++;
            else
                memory->StillFrames = 0;
        }
        memory->LastFrame = frame;
        memory->LastX = currentX;

        if (intentBlocked)
        {
            const int escapeDirection =
                (rawIntent != 0 && rawIntent != horizontalIntent) ? rawIntent : -horizontalIntent;
            memory->EscapeDirection = escapeDirection;
            memory->EscapeFrames = std::max(memory->EscapeFrames, config.WallEscapeFrames);
            mode = rawIntent != 0 && rawIntent != horizontalIntent ? "wallRoute" : "wallEscape";
        }
        else if (horizontalIntent != 0 &&
                  memory->StillFrames >= config.StuckFrames &&
                  (self.BlockedAhead || self.BlockedLeft || self.BlockedRight || self.HoleAhead))
        {
            memory->EscapeDirection = -horizontalIntent;
            memory->EscapeFrames = std::max(memory->EscapeFrames, config.WallEscapeFrames);
            memory->StillFrames = 0;
            mode = "stuckEscape";
        }

        if (memory->EscapeFrames > 0 && memory->EscapeDirection != 0)
        {
            horizontalIntent = memory->EscapeDirection;
            dx = horizontalIntent * std::max(config.HorizontalDeadzone + 1, config.CloseRange / 2);
            memory->EscapeFrames--;
        }
    }

    if (evadingOpponent &&
        ((horizontalIntent < 0 && blockedLeft) ||
         (horizontalIntent > 0 && blockedRight)))
    {
        horizontalIntent = 0;
        dx = 0;
        if (memory)
        {
            memory->EscapeDirection = 0;
            memory->EscapeFrames = 0;
            memory->StillFrames = 0;
        }
        mode = "evadeHold";
    }

    if (!hazardDanger && !evadingOpponent &&
        ((horizontalIntent < 0 && blockedLeft && canRawRouteRight) ||
         (horizontalIntent > 0 && blockedRight && canRawRouteLeft) ||
         (horizontalIntent == 0 && ((self.BlockedLeft && canRawRouteRight) || (self.BlockedRight && canRawRouteLeft)))))
    {
        horizontalIntent = rawIntent;
        dx = horizontalIntent * std::max(config.HorizontalDeadzone + 1, config.CloseRange / 2);
        if (memory)
        {
            memory->EscapeDirection = horizontalIntent;
            memory->EscapeFrames = 0;
            memory->StillFrames = 0;
        }
        mode = "rawWallRoute";
    }

    const bool leftOnlyHole = self.HoleLeft && !self.HoleRight && !self.BlockedRight;
    const bool rightOnlyHole = self.HoleRight && !self.HoleLeft && !self.BlockedLeft;
    if (holeRecovery && !hazardDanger && !evadingOpponent && self.GroundBelowSolid && self.HoleAhead)
    {
        if (leftOnlyHole)
            horizontalIntent = 1;
        else if (rightOnlyHole)
            horizontalIntent = -1;
        else if (self.HoleLeft && self.HoleRight)
        {
            const int fallbackIntent =
                memory && memory->LastHorizontalIntent != 0 ? memory->LastHorizontalIntent : rawIntent;
            horizontalIntent = std::clamp(fallbackIntent, -1, 1);
        }
        dx = horizontalIntent * std::max(config.HorizontalDeadzone + 1, config.CloseRange / 2);
        if (memory)
        {
            memory->EscapeDirection = horizontalIntent;
            memory->EscapeFrames = std::max(memory->EscapeFrames, config.WallEscapeFrames / 2);
            memory->StillFrames = 0;
        }
        mode = horizontalIntent == 0 ? "holeBrake" : "holePreempt";
    }
    if (!hazardDanger && !evadingOpponent &&
        ((horizontalIntent < 0 && leftOnlyHole) ||
         (horizontalIntent > 0 && rightOnlyHole)))
    {
        horizontalIntent = leftOnlyHole ? 1 : -1;
        dx = horizontalIntent * std::max(config.HorizontalDeadzone + 1, config.CloseRange / 2);
        if (memory)
        {
            memory->EscapeDirection = horizontalIntent;
            memory->EscapeFrames = std::max(memory->EscapeFrames, config.WallEscapeFrames / 2);
            memory->StillFrames = 0;
        }
        mode = "holeAvoid";
    }

    if ((holeRecovery || self.BattleStars > other.BattleStars) &&
        !self.GroundBelowSolid &&
        (self.HoleAhead || self.HoleLeft || self.HoleRight))
    {
        if (self.HoleLeft && !self.HoleRight && !self.BlockedRight)
            horizontalIntent = 1;
        else if (self.HoleRight && !self.HoleLeft && !self.BlockedLeft)
            horizontalIntent = -1;
        else if (self.HoleLeft && self.HoleRight)
        {
            int fallbackIntent = memory ? memory->LastHorizontalIntent : 0;
            if (fallbackIntent == 0)
                fallbackIntent = SignWithDeadzone(self.VelX, 0x800);
            if (fallbackIntent == 0)
                fallbackIntent = rawIntent;
            horizontalIntent = std::clamp(fallbackIntent, -1, 1);
        }
        else if (self.BlockedLeft && !self.BlockedRight)
            horizontalIntent = 1;
        else if (self.BlockedRight && !self.BlockedLeft)
            horizontalIntent = -1;
        else if (horizontalIntent < 0 && (self.HoleLeft || self.BlockedLeft))
            horizontalIntent = 0;
        else if (horizontalIntent > 0 && (self.HoleRight || self.BlockedRight))
            horizontalIntent = 0;
        else
            horizontalIntent = std::clamp(horizontalIntent, -1, 1);
        dx = horizontalIntent * std::max(config.HorizontalDeadzone + 1, config.CloseRange / 2);
        if (memory)
        {
            memory->EscapeDirection = horizontalIntent;
            memory->EscapeFrames = 0;
            memory->StillFrames = 0;
        }
        mode = horizontalIntent == 0 ? "airHold" : "airRecover";
    }

    NsmbNetplayPoC::InputState input = NeutralInputPreservingTouch(fallback);
    if (horizontalIntent > 0)
        PressButton(input, kButtonRight);
    else if (horizontalIntent < 0)
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
        (movingLeft && (self.HoleLeft || self.BlockedLeft)) ||
        (movingRight && (self.HoleRight || self.BlockedRight)) ||
        (movingHorizontally && (self.HoleAhead || self.BlockedAhead));
    const int hazardFrames = memory ? memory->HazardFrames : static_cast<int>(frame % kHazardJumpCycleFrames);
    const bool hazardJump =
        hazardDanger &&
        (!self.GroundBelowSolid ||
         ((hazardFrames % kHazardJumpCycleFrames) < kHazardJumpPressFrames));
    bool jumpPressed = periodicJump || targetAbove || closeOpponent || terrainJump || hazardJump || forceJump;
    const bool urgentHazardJump = hazardDanger && self.GroundBelowSolid && (hazardJump || forceJump);
    if (memory)
    {
        if (memory->JumpCooldownFrames > 0)
            memory->JumpCooldownFrames--;
        if (urgentHazardJump &&
            memory->JumpCooldownFrames == 0 &&
            memory->JumpReleaseFrames == 0 &&
            memory->JumpPressFrames == 0)
        {
            if (memory->LastJumpPressed)
                memory->JumpReleaseFrames = 1;
            memory->JumpPressFrames = kHazardJumpPulseFrames;
            memory->JumpCooldownFrames = kHazardJumpPulseCooldownFrames;
        }
        if (memory->JumpReleaseFrames > 0)
        {
            jumpPressed = false;
            memory->JumpReleaseFrames--;
        }
        else if (memory->JumpPressFrames > 0)
        {
            jumpPressed = true;
            memory->JumpPressFrames--;
        }
    }
    if (jumpPressed)
        PressButton(input, kButtonB);
    if (memory)
    {
        memory->LastJumpPressed = jumpPressed;
        if (horizontalIntent != 0)
            memory->LastHorizontalIntent = horizontalIntent;
    }

    if (config.TraceEnabled &&
        (config.TraceInterval <= 1 || (frame % static_cast<melonDS::u32>(config.TraceInterval)) == 0))
    {
        std::printf(
            "NSMB RuleAI: inst=%d frame=%u player=%d mode=%s self=%08X/%08X target=%08X/%08X dx=%d rawDx=%d intent=%d escape=%d/%d still=%d opponent=%08X/%08X stars=%u/%u hazard=%d/%d/%d cat=%d closing=%d close=%d terrain=ground:%d ahead:%d/%d left:%d/%d/%d right:%d/%d/%d keys=0x%03X\n",
            instanceID,
            frame,
            player,
            mode,
            self.X,
            self.Y,
            targetX,
            targetY,
            dx,
            rawDx,
            horizontalIntent,
            memory ? memory->EscapeDirection : 0,
            memory ? memory->EscapeFrames : 0,
            memory ? memory->StillFrames : 0,
            other.X,
            other.Y,
            self.BattleStars,
            other.BattleStars,
            hazardDanger ? 1 : 0,
            hazardDx,
            hazardDy,
            self.HazardCategoryID,
            self.HazardClosing ? 1 : 0,
            self.HazardVeryClose ? 1 : 0,
            self.GroundBelowSolid ? 1 : 0,
            self.BlockedAhead ? 1 : 0,
            self.HoleAhead ? 1 : 0,
            self.BlockedLeft ? 1 : 0,
            self.HoleLeft ? 1 : 0,
            self.FarHoleLeft ? 1 : 0,
            self.BlockedRight ? 1 : 0,
            self.HoleRight ? 1 : 0,
            self.FarHoleRight ? 1 : 0,
            input.KeyMask);
    }

    return input;
}

}
