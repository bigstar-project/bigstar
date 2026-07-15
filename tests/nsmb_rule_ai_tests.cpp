#include "NsmbRuleAI.h"

#include <cstdio>
#include <cstring>

namespace
{

int Failures = 0;
int TerrainSummaryCalls = 0;
bool FirstContactGround = false;
bool FirstContactWallLeft = false;
bool FirstContactWallRight = false;

void Check(bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::fprintf(stderr, "line %d: CHECK failed: %s\n", line, expression);
    Failures++;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

const char* TestObjectCategory(melonDS::u16 objectID, melonDS::u32)
{
    if (objectID == 1)
        return "enemy_goomba";
    if (objectID == 2)
        return "dropped_star_item";
    return "other";
}

NsmbMvlNetplay::GameStateModel::AITerrainDerivedSummary TestTerrainSummary(
    const NsmbMvlNetplay::GameStateModel::AIPlayerTileProbeSample&, bool contactGround,
    bool contactWallLeft, bool contactWallRight)
{
    if (TerrainSummaryCalls == 0)
    {
        FirstContactGround = contactGround;
        FirstContactWallLeft = contactWallLeft;
        FirstContactWallRight = contactWallRight;
    }
    TerrainSummaryCalls++;
    NsmbMvlNetplay::GameStateModel::AITerrainDerivedSummary summary {};
    summary.EffectiveGroundBelowSolid = 1;
    summary.BlockedAhead = 1;
    summary.EffectiveHoleAhead = 1;
    summary.BlockedLeft = 1;
    summary.EffectiveHoleLeft = 1;
    summary.FarHoleLeft = 1;
    summary.BlockedRight = 1;
    summary.EffectiveHoleRight = 1;
    summary.FarHoleRight = 1;
    return summary;
}

bool TestTargetHasFloorBelow(const NsmbMvlNetplay::GameStateModel::AIPlayerTileProbeSample&,
                             melonDS::u32, melonDS::u32, melonDS::u32 targetX, melonDS::u32)
{
    return targetX != 0xDEAD;
}

NsmbRuleAI::FrameStateServices TestServices()
{
    NsmbRuleAI::FrameStateServices services {};
    services.ObjectCategory = TestObjectCategory;
    services.DeriveTerrainSummary = TestTerrainSummary;
    services.TargetHasFloorBelow = TestTargetHasFloorBelow;
    return services;
}

void TestFrameStateMapsGameSampleAndObjectScan()
{
    using namespace NsmbMvlNetplay;
    GameStateModel::GameStateSample sample {};
    sample.PlayerActor0Found = 1;
    sample.PlayerActor0PosX = 0x10000;
    sample.PlayerActor0PosY = 0x20000;
    sample.PlayerActor0VelX = 0x800;
    sample.PlayerActor0CollisionFlag = 0x00000001u | 0x00000008u;
    sample.Player0BattleStars = 2;
    sample.PlayerActor1Found = 1;
    sample.PlayerActor1PosX = 0x30000;
    sample.PlayerActor1PosY = 0x20000;
    sample.PlayerActor1VelX = static_cast<melonDS::u32>(-0x400);
    sample.Player1Dead = 1;
    sample.Player1BattleStars = 1;
    sample.VsStarFound = 1;
    sample.VsStarPosX = 0x40000;
    sample.VsStarPosY = 0x21000;
    sample.VsStarActorFound = 1;
    sample.VsStarActorPosX = 0xDEAD;
    sample.VsStarActorPosY = 0x22000;
    sample.MovingHazardFound = 1;
    sample.MovingHazardPosX = 0x50000;
    sample.MovingHazardPosY = 0x60000;
    sample.MovingHazardVelX = 0x700;
    sample.MovingHazardVelY = 0x800;

    GameStateReader::GameStateObjectScanCache cache {};
    GameStateReader::GameStateObjectScanEntry hazard {};
    hazard.LifecycleState = 1;
    hazard.ObjectID = 1;
    hazard.Actor.Found = 1;
    hazard.Actor.Settings = 0x12345678;
    hazard.Actor.PosX = 0x3F0000;
    hazard.Actor.PosY = 0x20000;
    hazard.Actor.VelX = 0x2000;
    hazard.Actor.VelY = static_cast<melonDS::u32>(-0x100);
    cache.Entries.push_back(hazard);
    GameStateReader::GameStateObjectScanEntry droppedStar {};
    droppedStar.LifecycleState = 1;
    droppedStar.ObjectID = 2;
    droppedStar.Actor.Found = 1;
    droppedStar.Actor.PosX = 0x18000;
    droppedStar.Actor.PosY = 0x20000;
    cache.Entries.push_back(droppedStar);

    NsmbRuleAI::Config config {};
    config.HorizontalWrapWidth = 0x400000;
    config.HazardHorizontalRange = 0x40000;
    config.HazardVerticalRange = 0x50000;
    const NsmbRuleAI::RuntimeHazardThreat sharedHazard = NsmbRuleAI::FindRuntimeHazard(
        {
            config.HorizontalWrapWidth,
            config.HazardHorizontalRange,
            config.HazardVerticalRange,
            0x30000,
        },
        cache, sample.PlayerActor0PosX, sample.PlayerActor0PosY, sample.PlayerActor0VelX,
        TestObjectCategory);
    CHECK(sharedHazard.Found);
    CHECK(sharedHazard.ObjectID == 1);
    CHECK(sharedHazard.Settings == 0x12345678);
    TerrainSummaryCalls = 0;
    const NsmbRuleAI::FrameState state =
        NsmbRuleAI::BuildFrameState(config, sample, cache, true, TestServices());

    CHECK(state.InGameplay);
    CHECK(state.Players[0].Found);
    CHECK(state.Players[0].X == 0x10000);
    CHECK(state.Players[0].Y == 0x20000);
    CHECK(state.Players[0].VelX == 0x800);
    CHECK(state.Players[0].BattleStars == 2);
    CHECK(!state.Players[0].Dead);
    CHECK(state.Players[1].Dead);
    CHECK(state.Players[1].VelX == -0x400);
    CHECK(TerrainSummaryCalls == 2);
    CHECK(FirstContactGround);
    CHECK(FirstContactWallLeft);
    CHECK(!FirstContactWallRight);
    CHECK(state.Players[0].GroundBelowSolid);
    CHECK(state.Players[0].BlockedAhead);
    CHECK(state.Players[0].HoleAhead);
    CHECK(state.Players[0].BlockedLeft);
    CHECK(state.Players[0].HoleLeft);
    CHECK(state.Players[0].FarHoleLeft);
    CHECK(state.Players[0].BlockedRight);
    CHECK(state.Players[0].HoleRight);
    CHECK(state.Players[0].FarHoleRight);
    CHECK(state.StarFound);
    CHECK(state.StarX == 0x40000);
    CHECK(state.StarActorFound);
    CHECK(!state.Players[0].StarActorFloorSupported);
    CHECK(state.Players[0].StarCandidateFloorSupported);
    CHECK(state.Players[0].DroppedStarFound);
    CHECK(state.Players[0].DroppedStarX == 0x18000);
    CHECK(state.Players[0].DroppedStarFloorSupported);
    CHECK(state.Players[0].HazardFound);
    CHECK(state.Players[0].HazardClosing);
    CHECK(state.Players[0].HazardDx == -0x20000);
    CHECK(state.Players[0].HazardDy == 0);
    CHECK(state.Players[0].HazardVelX == 0x2000);
    CHECK(state.Players[0].HazardVelY == -0x100);
    CHECK(state.Players[0].HazardCategoryID == 3);
    CHECK(state.MovingHazardFound);
    CHECK(state.MovingHazardX == 0x50000);
    CHECK(state.MovingHazardVelY == 0x800);
}

void TestOwnFireballIsNotAThreat()
{
    using namespace NsmbMvlNetplay;
    GameStateModel::GameStateSample sample {};
    sample.PlayerActor0Found = 1;
    sample.PlayerActor0PosX = 0x100000;
    sample.PlayerActor0PosY = 0x200000;
    sample.FireballSlotActive[0] = 1;
    sample.FireballSlotKind[0] = 0;
    sample.FireballSlotPosX[0] = 0x101000;
    sample.FireballSlotPosY[0] = 0x200000;
    sample.FireballSlotActive[1] = 1;
    sample.FireballSlotKind[1] = 1;
    sample.FireballSlotPosX[1] = 0x102000;
    sample.FireballSlotPosY[1] = 0x200000;
    sample.FireballSlotVelX[1] = static_cast<melonDS::u32>(-0x1000);

    NsmbRuleAI::Config config {};
    GameStateReader::GameStateObjectScanCache cache {};
    const NsmbRuleAI::FrameState state =
        NsmbRuleAI::BuildFrameState(config, sample, cache, true, TestServices());
    CHECK(state.Players[0].HazardFound);
    CHECK(state.Players[0].HazardDx == 0x2000);
    CHECK(state.Players[0].HazardClosing);
    CHECK(state.Players[0].HazardCategoryID == 0);
}

void TestDecisionUsesMappedPlayerPositions()
{
    NsmbRuleAI::Config config {};
    config.Enabled = true;
    config.PlayerSpec = "0";
    NsmbRuleAI::FrameState state {};
    state.InGameplay = true;
    state.Players[0].Found = true;
    state.Players[0].GroundBelowSolid = true;
    state.Players[1].Found = true;
    state.Players[1].X = 0x20000;
    state.Players[1].Y = 0;
    NsmbMvlNetplay::InputState fallback {};

    const NsmbMvlNetplay::InputState input =
        NsmbRuleAI::DecideInput(config, state, 15, 1000, 0, 0, fallback);
    CHECK((input.KeyMask & (1u << 4)) == 0);
    CHECK((input.KeyMask & (1u << 5)) != 0);
    CHECK((input.KeyMask & (1u << 11)) == 0);

    config.Enabled = false;
    const NsmbMvlNetplay::InputState disabled =
        NsmbRuleAI::DecideInput(config, state, 15, 1001, 0, 0, fallback);
    CHECK(disabled.KeyMask == fallback.KeyMask);
}

} // namespace

int main()
{
    TestFrameStateMapsGameSampleAndObjectScan();
    TestOwnFireballIsNotAThreat();
    TestDecisionUsesMappedPlayerPositions();
    if (Failures != 0)
    {
        std::fprintf(stderr, "nsmb rule AI tests failed: %d\n", Failures);
        return 1;
    }
    std::printf("nsmb rule AI tests passed\n");
    return 0;
}
