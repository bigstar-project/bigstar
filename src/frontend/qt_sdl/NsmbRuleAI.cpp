#include "NsmbRuleAI.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <limits>

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

void PressButton(NsmbMvlNetplay::InputState& input, int bit)
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

std::int64_t WrappedHorizontalDelta(int horizontalWrapWidth, melonDS::u32 target, melonDS::u32 self)
{
    std::int64_t dx = SignedCoordinate(target) - SignedCoordinate(self);
    const std::int64_t wrapWidth = horizontalWrapWidth;
    if (wrapWidth <= 0)
        return dx;

    const std::int64_t halfWidth = wrapWidth / 2;
    dx %= wrapWidth;
    if (dx > halfWidth)
        dx -= wrapWidth;
    else if (dx < -halfWidth)
        dx += wrapWidth;
    return dx;
}

std::int32_t CoordinateDelta(melonDS::u32 target, melonDS::u32 self)
{
    return static_cast<std::int32_t>(
        SignedCoordinate(target) - SignedCoordinate(self));
}

std::int32_t HorizontalDelta(const Config& config, melonDS::u32 target, melonDS::u32 self)
{
    return static_cast<std::int32_t>(
        WrappedHorizontalDelta(config.HorizontalWrapWidth, target, self));
}

bool IsRuntimeHazardCategory(const char* category)
{
    return std::strcmp(category, "moving_hazard") == 0 || std::strcmp(category, "hazard") == 0 ||
           std::strcmp(category, "enemy_goomba") == 0 || std::strcmp(category, "enemy_koopa") == 0;
}

int RuntimeHazardCategoryID(const char* category)
{
    if (std::strcmp(category, "moving_hazard") == 0)
        return 1;
    if (std::strcmp(category, "hazard") == 0)
        return 2;
    if (std::strcmp(category, "enemy_goomba") == 0)
        return 3;
    if (std::strcmp(category, "enemy_koopa") == 0)
        return 4;
    return 0;
}

const char* ObjectCategory(ObjectCategoryFunction objectCategory, melonDS::u16 objectID,
                           melonDS::u32 settings)
{
    return objectCategory ? objectCategory(objectID, settings) : "";
}

RuntimeHazardThreat FindRuntimeHazard(
    const RuntimeHazardConfig& config,
    const NsmbMvlNetplay::GameStateReader::GameStateObjectScanCache& objectScanCache,
    melonDS::u32 selfX, melonDS::u32 selfY, melonDS::u32 selfVelX,
    ObjectCategoryFunction objectCategory)
{
    RuntimeHazardThreat best {};
    std::int64_t bestScore = 0;
    auto abs64 = [](std::int64_t value) { return value < 0 ? -value : value; };

    const std::int64_t selfVx = SignedCoordinate(selfVelX);
    for (const auto& entry : objectScanCache.Entries)
    {
        if (entry.LifecycleState != 1)
            continue;
        const char* category = ObjectCategory(objectCategory, entry.ObjectID, entry.Actor.Settings);
        if (!IsRuntimeHazardCategory(category))
            continue;

        const std::int64_t dx =
            WrappedHorizontalDelta(config.HorizontalWrapWidth, entry.Actor.PosX, selfX);
        const std::int64_t dy = SignedCoordinate(entry.Actor.PosY) - SignedCoordinate(selfY);
        if (abs64(dx) > config.HorizontalRange || abs64(dy) > config.VerticalRange)
        {
            continue;
        }

        const std::int64_t hazardVx = SignedCoordinate(entry.Actor.VelX);
        const std::int64_t relVx = hazardVx - selfVx;
        const bool closing = (dx < 0 && relVx > 0) || (dx > 0 && relVx < 0);
        const bool veryClose = abs64(dx) <= config.CloseRange || abs64(dy) <= 0x10000;
        std::int64_t score = abs64(dx) + abs64(dy) * 2;
        if (closing)
            score -= config.HorizontalRange;
        if (veryClose)
            score -= config.CloseRange;

        if (!best.Found || score < bestScore)
        {
            best.Found = true;
            best.Closing = closing;
            best.VeryClose = veryClose;
            best.Dx = dx;
            best.Dy = dy;
            best.VelX = hazardVx;
            best.VelY = SignedCoordinate(entry.Actor.VelY);
            best.CategoryID = RuntimeHazardCategoryID(category);
            best.ObjectID = entry.ObjectID;
            best.Settings = entry.Actor.Settings;
            bestScore = score;
        }
    }
    return best;
}

NsmbMvlNetplay::GameStateReader::ObjectScanSample NearestDroppedBattleStar(
    const Config& config,
    const NsmbMvlNetplay::GameStateReader::GameStateObjectScanCache& objectScanCache,
    melonDS::u32 selfX, melonDS::u32 selfY, const FrameStateServices& services)
{
    NsmbMvlNetplay::GameStateReader::ObjectScanSample best {};
    std::int64_t bestScore = 0;
    for (const auto& entry : objectScanCache.Entries)
    {
        if (entry.LifecycleState != 1)
            continue;
        const char* category =
            ObjectCategory(services.ObjectCategory, entry.ObjectID, entry.Actor.Settings);
        if (std::strcmp(category, "dropped_star_item") != 0)
            continue;
        const std::int64_t dx =
            WrappedHorizontalDelta(config.HorizontalWrapWidth, entry.Actor.PosX, selfX);
        const std::int64_t dy = SignedCoordinate(entry.Actor.PosY) - SignedCoordinate(selfY);
        const std::int64_t score = dx * dx + dy * dy;
        if (!best.Found || score < bestScore)
        {
            best = entry.Actor;
            bestScore = score;
        }
    }
    return best;
}

bool PlayerContactGround(melonDS::u32 collisionFlag)
{
    return (collisionFlag & (0x00000001u | 0x00002000u | 0x00008000u | 0x08000000u)) != 0;
}

void FillProbeSummary(PlayerFrameState& out,
                      const NsmbMvlNetplay::GameStateModel::AIPlayerTileProbeSample& probe,
                      melonDS::u32 collisionFlag, const FrameStateServices& services)
{
    const bool contactGround = PlayerContactGround(collisionFlag);
    const bool contactWallLeft = (collisionFlag & (0x00000008u | 0x00000400u | 0x20000000u)) != 0;
    const bool contactWallRight = (collisionFlag & (0x00000010u | 0x00000800u | 0x40000000u)) != 0;
    const auto summary =
        services.DeriveTerrainSummary
            ? services.DeriveTerrainSummary(probe, contactGround, contactWallLeft, contactWallRight)
            : NsmbMvlNetplay::GameStateModel::AITerrainDerivedSummary {};
    out.GroundBelowSolid = summary.EffectiveGroundBelowSolid != 0;
    out.BlockedAhead = summary.BlockedAhead != 0;
    out.HoleAhead = summary.EffectiveHoleAhead != 0;
    out.BlockedLeft = summary.BlockedLeft != 0;
    out.HoleLeft = summary.EffectiveHoleLeft != 0;
    out.FarHoleLeft = summary.FarHoleLeft != 0;
    out.BlockedRight = summary.BlockedRight != 0;
    out.HoleRight = summary.EffectiveHoleRight != 0;
    out.FarHoleRight = summary.FarHoleRight != 0;
}

bool TargetHasFloorBelow(const FrameStateServices& services,
                         const NsmbMvlNetplay::GameStateModel::AIPlayerTileProbeSample& probe,
                         melonDS::u32 selfX, melonDS::u32 selfY, melonDS::u32 targetX,
                         melonDS::u32 targetY)
{
    return !services.TargetHasFloorBelow ||
           services.TargetHasFloorBelow(probe, selfX, selfY, targetX, targetY);
}

void FillHazard(PlayerFrameState& out, const Config& config,
                const NsmbMvlNetplay::GameStateModel::GameStateSample& sample,
                const NsmbMvlNetplay::GameStateReader::GameStateObjectScanCache& objectScanCache,
                int player, melonDS::u32 x, melonDS::u32 y, melonDS::u32 vx,
                const FrameStateServices& services)
{
    const std::int64_t closeRange =
        std::max<std::int64_t>(0x10000, (config.HazardHorizontalRange * 3) / 4);
    RuntimeHazardThreat best = FindRuntimeHazard(
        RuntimeHazardConfig {config.HorizontalWrapWidth, config.HazardHorizontalRange,
                             config.HazardVerticalRange, closeRange},
        objectScanCache, x, y, vx, services.ObjectCategory);
    auto abs64 = [](std::int64_t value) { return value < 0 ? -value : value; };
    auto scoreThreat = [&abs64, &config, closeRange](const RuntimeHazardThreat& candidate) {
        std::int64_t score = abs64(candidate.Dx) + abs64(candidate.Dy) * 2;
        if (candidate.Closing)
            score -= config.HazardHorizontalRange;
        if (candidate.VeryClose)
            score -= closeRange;
        return score;
    };
    std::int64_t bestScore = best.Found ? scoreThreat(best) : 0;
    const std::int64_t selfVx = SignedCoordinate(vx);
    for (int slot = 0; slot < NsmbMvlNetplay::GameStateModel::kAIFireballSlotCount; slot++)
    {
        if (sample.FireballSlotActive[slot] == 0)
            continue;
        if (sample.FireballSlotKind[slot] == static_cast<melonDS::u32>(player))
            continue;
        const std::int64_t dx =
            WrappedHorizontalDelta(config.HorizontalWrapWidth, sample.FireballSlotPosX[slot], x);
        const std::int64_t dy =
            SignedCoordinate(sample.FireballSlotPosY[slot]) - SignedCoordinate(y);
        if (abs64(dx) > config.HazardHorizontalRange || abs64(dy) > config.HazardVerticalRange)
        {
            continue;
        }
        const std::int64_t fireVx = SignedCoordinate(sample.FireballSlotVelX[slot]);
        const std::int64_t relVx = fireVx - selfVx;
        RuntimeHazardThreat candidate {};
        candidate.Found = true;
        candidate.Closing = (dx < 0 && relVx > 0) || (dx > 0 && relVx < 0);
        candidate.VeryClose = abs64(dx) <= closeRange || abs64(dy) <= 0x10000;
        candidate.Dx = dx;
        candidate.Dy = dy;
        candidate.VelX = fireVx;
        candidate.VelY = SignedCoordinate(sample.FireballSlotVelY[slot]);
        const std::int64_t score = scoreThreat(candidate);
        if (!best.Found || score < bestScore)
        {
            best = candidate;
            bestScore = score;
        }
    }
    out.HazardFound = best.Found;
    out.HazardClosing = best.Closing;
    out.HazardVeryClose = best.VeryClose;
    out.HazardDx = static_cast<std::int32_t>(
        std::clamp<std::int64_t>(best.Dx, std::numeric_limits<std::int32_t>::min(),
                                 std::numeric_limits<std::int32_t>::max()));
    out.HazardDy = static_cast<std::int32_t>(
        std::clamp<std::int64_t>(best.Dy, std::numeric_limits<std::int32_t>::min(),
                                 std::numeric_limits<std::int32_t>::max()));
    out.HazardVelX = static_cast<std::int32_t>(
        std::clamp<std::int64_t>(best.VelX, std::numeric_limits<std::int32_t>::min(),
                                 std::numeric_limits<std::int32_t>::max()));
    out.HazardVelY = static_cast<std::int32_t>(
        std::clamp<std::int64_t>(best.VelY, std::numeric_limits<std::int32_t>::min(),
                                 std::numeric_limits<std::int32_t>::max()));
    out.HazardCategoryID = best.CategoryID;
}

FrameState BuildFrameState(
    const Config& config, const NsmbMvlNetplay::GameStateModel::GameStateSample& sample,
    const NsmbMvlNetplay::GameStateReader::GameStateObjectScanCache& objectScanCache,
    bool inGameplay, const FrameStateServices& services)
{
    FrameState state {};
    state.InGameplay = inGameplay;
    state.Players[0].Found = sample.PlayerActor0Found != 0;
    state.Players[0].X = sample.PlayerActor0PosX;
    state.Players[0].Y = sample.PlayerActor0PosY;
    state.Players[0].VelX = static_cast<std::int32_t>(sample.PlayerActor0VelX);
    state.Players[0].Dead = sample.Player0Dead != 0;
    state.Players[0].BattleStars = sample.Player0BattleStars;
    FillProbeSummary(state.Players[0], sample.PlayerActor0TileProbe,
                     sample.PlayerActor0CollisionFlag, services);
    FillHazard(state.Players[0], config, sample, objectScanCache, 0, sample.PlayerActor0PosX,
               sample.PlayerActor0PosY, sample.PlayerActor0VelX, services);
    state.Players[1].Found = sample.PlayerActor1Found != 0;
    state.Players[1].X = sample.PlayerActor1PosX;
    state.Players[1].Y = sample.PlayerActor1PosY;
    state.Players[1].VelX = static_cast<std::int32_t>(sample.PlayerActor1VelX);
    state.Players[1].Dead = sample.Player1Dead != 0;
    state.Players[1].BattleStars = sample.Player1BattleStars;
    FillProbeSummary(state.Players[1], sample.PlayerActor1TileProbe,
                     sample.PlayerActor1CollisionFlag, services);
    FillHazard(state.Players[1], config, sample, objectScanCache, 1, sample.PlayerActor1PosX,
               sample.PlayerActor1PosY, sample.PlayerActor1VelX, services);
    state.StarFound = sample.VsStarFound != 0;
    state.StarX = sample.VsStarPosX;
    state.StarY = sample.VsStarPosY;
    state.StarActorFound = sample.VsStarActorFound != 0;
    state.StarActorX = sample.VsStarActorPosX;
    state.StarActorY = sample.VsStarActorPosY;
    state.Players[0].StarActorFloorSupported =
        !state.StarActorFound ||
        TargetHasFloorBelow(services, sample.PlayerActor0TileProbe, sample.PlayerActor0PosX,
                            sample.PlayerActor0PosY, state.StarActorX, state.StarActorY);
    state.Players[1].StarActorFloorSupported =
        !state.StarActorFound ||
        TargetHasFloorBelow(services, sample.PlayerActor1TileProbe, sample.PlayerActor1PosX,
                            sample.PlayerActor1PosY, state.StarActorX, state.StarActorY);
    state.Players[0].StarCandidateFloorSupported =
        !state.StarFound ||
        TargetHasFloorBelow(services, sample.PlayerActor0TileProbe, sample.PlayerActor0PosX,
                            sample.PlayerActor0PosY, state.StarX, state.StarY);
    state.Players[1].StarCandidateFloorSupported =
        !state.StarFound ||
        TargetHasFloorBelow(services, sample.PlayerActor1TileProbe, sample.PlayerActor1PosX,
                            sample.PlayerActor1PosY, state.StarX, state.StarY);
    const auto droppedStar0 = NearestDroppedBattleStar(
        config, objectScanCache, sample.PlayerActor0PosX, sample.PlayerActor0PosY, services);
    const auto droppedStar1 = NearestDroppedBattleStar(
        config, objectScanCache, sample.PlayerActor1PosX, sample.PlayerActor1PosY, services);
    state.Players[0].DroppedStarFound = droppedStar0.Found != 0;
    state.Players[0].DroppedStarX = droppedStar0.PosX;
    state.Players[0].DroppedStarY = droppedStar0.PosY;
    state.Players[0].DroppedStarFloorSupported =
        !state.Players[0].DroppedStarFound ||
        TargetHasFloorBelow(services, sample.PlayerActor0TileProbe, sample.PlayerActor0PosX,
                            sample.PlayerActor0PosY, state.Players[0].DroppedStarX,
                            state.Players[0].DroppedStarY);
    state.Players[1].DroppedStarFound = droppedStar1.Found != 0;
    state.Players[1].DroppedStarX = droppedStar1.PosX;
    state.Players[1].DroppedStarY = droppedStar1.PosY;
    state.Players[1].DroppedStarFloorSupported =
        !state.Players[1].DroppedStarFound ||
        TargetHasFloorBelow(services, sample.PlayerActor1TileProbe, sample.PlayerActor1PosX,
                            sample.PlayerActor1PosY, state.Players[1].DroppedStarX,
                            state.Players[1].DroppedStarY);
    state.MovingHazardFound = sample.MovingHazardFound != 0;
    state.MovingHazardX = sample.MovingHazardPosX;
    state.MovingHazardY = sample.MovingHazardPosY;
    state.MovingHazardVelX = sample.MovingHazardVelX;
    state.MovingHazardVelY = sample.MovingHazardVelY;
    return state;
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
    melonDS::u32 starY,
    bool targetFloorSupported)
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
    const bool targetUnsupported =
        !targetFloorSupported &&
        starNotHighAbove &&
        std::abs(starDx) <= kUnsafeStarFloorGapRange;
    const bool nearbyDeepDropStar =
        starBelow &&
        starDy < -0x30000 &&
        std::abs(starDx) <= 0x80000;
    const bool alreadyInVerticalPit =
        (self.BlockedLeft && self.BlockedRight) ||
        (!self.GroundBelowSolid && (self.HoleAhead || self.HoleLeft || self.HoleRight));

    StarSafety safety {};
    safety.Unsafe =
        targetUnsupported ||
        starBehindFloorGap ||
        nearbyDeepDropStar ||
        pitMouth ||
        alreadyInVerticalPit;
    safety.ActiveAvoid =
        pitMouth ||
        alreadyInVerticalPit ||
        (targetUnsupported && starBelow && (self.HoleAhead || targetSideIsHole || !self.GroundBelowSolid)) ||
        (nearbyDeepDropStar && (self.HoleAhead || targetSideIsHole || !self.GroundBelowSolid));
    return safety;
}

NsmbMvlNetplay::InputState NeutralInputPreservingTouch(const NsmbMvlNetplay::InputState& source)
{
    NsmbMvlNetplay::InputState input {};
    input.KeyMask = 0xFFF;
    input.Touching = source.Touching;
    input.TouchX = source.TouchX;
    input.TouchY = source.TouchY;
    return input;
}

NsmbMvlNetplay::InputState DecideInput(
    const Config& config,
    const FrameState& state,
    int instanceID,
    melonDS::u32 frame,
    int player,
    int localPlayer,
    const NsmbMvlNetplay::InputState& fallback)
{
    if (!ControlsPlayer(config, player, localPlayer) || frame < config.StartFrame)
        return fallback;
    if (!state.InGameplay || !state.Players[player].Found)
        return fallback;

    const int opponent = player ^ 1;
    const PlayerFrameState& self = state.Players[player];
    const PlayerFrameState& other = state.Players[opponent];
    const bool opponentTrackable = other.Found && !other.Dead;
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
        EvaluateStarSafety(config, self, state.StarActorX, state.StarActorY, self.StarActorFloorSupported) :
        StarSafety {};
    const StarSafety starCandidateSafety = (!state.StarActorFound && state.StarFound) ?
        EvaluateStarSafety(config, self, state.StarX, state.StarY, self.StarCandidateFloorSupported) :
        StarSafety {};
    const StarSafety droppedStarSafety = self.DroppedStarFound ?
        EvaluateStarSafety(config, self, self.DroppedStarX, self.DroppedStarY, self.DroppedStarFloorSupported) :
        StarSafety {};
    const bool starActorUnsafe = starActorSafety.Unsafe;
    const bool starCandidateUnsafe = starCandidateSafety.Unsafe;
    const bool droppedStarUnsafe = droppedStarSafety.Unsafe;
    const bool starActorActiveAvoid = starActorSafety.ActiveAvoid;
    const bool starCandidateActiveAvoid = starCandidateSafety.ActiveAvoid;
    const bool droppedStarActiveAvoid = droppedStarSafety.ActiveAvoid;
    if (memory && (starActorUnsafe || starCandidateUnsafe || droppedStarUnsafe))
        memory->UnsafeStarRejectFrames = std::max(memory->UnsafeStarRejectFrames, kUnsafeStarAvoidFrames);
    if (memory && (starActorActiveAvoid || starCandidateActiveAvoid || droppedStarActiveAvoid))
    {
        memory->UnsafeStarAvoidFrames =
            std::max(memory->UnsafeStarAvoidFrames, kUnsafeStarAvoidFrames);
        const melonDS::u32 unsafeStarX = starActorActiveAvoid ? state.StarActorX :
            (starCandidateActiveAvoid ? state.StarX : self.DroppedStarX);
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
    const bool droppedStarUsable =
        self.DroppedStarFound &&
        !droppedStarUnsafe &&
        !avoidingUnsafeStar &&
        !rejectingUnsafeStar;
    const bool starCandidateUsable =
        !state.StarActorFound &&
        state.StarFound &&
        !starCandidateUnsafe &&
        !avoidingUnsafeStar &&
        !rejectingUnsafeStar &&
        self.BattleStars <= other.BattleStars;

    melonDS::u32 targetX = opponentTrackable ? other.X : self.X;
    melonDS::u32 targetY = opponentTrackable ? other.Y : self.Y;
    const char* mode = opponentTrackable ? "chase" : "hold";

    if (starActorUsable)
    {
        targetX = state.StarActorX;
        targetY = state.StarActorY;
        mode = "starActor";
    }
    else if (droppedStarUsable)
    {
        targetX = self.DroppedStarX;
        targetY = self.DroppedStarY;
        mode = "droppedStar";
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
    const bool evadingOpponent =
        opponentTrackable &&
        self.BattleStars > other.BattleStars &&
        absOpponentDx < config.CloseRange;
    const bool starTargetVisible = starActorUsable || starCandidateUsable || droppedStarUsable;
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

    NsmbMvlNetplay::InputState input = NeutralInputPreservingTouch(fallback);
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
