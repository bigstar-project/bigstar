// AI observation/logging implementation extracted from NsmbNetplayPoC.cpp.
// This file is intentionally included from NsmbNetplayPoC.cpp inside its anonymous namespace.
// Do not add it as a separately compiled translation unit unless the shared internal types are moved to a real header.

const char* AIObjectCategory(melonDS::u16 objectID, melonDS::u32 settings);

using RuntimeHazardThreat = NsmbRuleAI::RuntimeHazardThreat;

bool AIPlayerContactGround(melonDS::u32 collisionFlag)
{
    return NsmbRuleAI::PlayerContactGround(collisionFlag);
}

RuntimeHazardThreat MostDangerousRuntimeHazard(
    const GameStateObjectScanCache& objectScanCache,
    melonDS::u32 selfX,
    melonDS::u32 selfY,
    melonDS::u32 selfVelX,
    std::int64_t horizontalRange,
    std::int64_t verticalRange,
    std::int64_t closeRange)
{
    return NsmbRuleAI::FindRuntimeHazard(
        {
            G.AI.Rule.HorizontalWrapWidth,
            horizontalRange,
            verticalRange,
            closeRange,
        },
        objectScanCache,
        selfX,
        selfY,
        selfVelX,
        AIObjectCategory);
}

const char* AIPowerupCandidateName(melonDS::u32 value)
{
    switch (value)
    {
    case 0:
        return "small_or_none";
    case 1:
        return "super_candidate";
    case 2:
        return "fire_candidate";
    case 3:
        return "mini_candidate";
    case 4:
        return "shell_candidate";
    case 5:
        return "mega_candidate";
    default:
        return "unknown";
    }
}

void WriteAIPowerupCandidateJson(std::ostream& out, const char* key, melonDS::u32 value, melonDS::u32 shellState = 0)
{
    out << "\"" << key << "\":{\"raw\":" << value
        << ",\"name\":\"" << AIPowerupCandidateName(value) << "\""
        << ",\"mapped\":" << (value <= 5 ? 1 : 0)
        << ",\"mappingVerified\":0"
        << ",\"isPoweredUpCandidate\":" << (value != 0 ? 1 : 0)
        << ",\"canShootFireCandidate\":" << (value == 2 ? 1 : 0)
        << ",\"isMiniCandidate\":" << (value == 3 ? 1 : 0)
        << ",\"isShellCandidate\":" << (value == 4 || shellState != 0 ? 1 : 0)
        << ",\"isMegaCandidate\":" << (value == 5 ? 1 : 0)
        << "}";
}

melonDS::u32 AIVisualPowerupKindCandidate(
    melonDS::u32 powerup,
    melonDS::u32 inventoryPowerup,
    melonDS::u32 actorPowerupState,
    melonDS::u32 actorPowerupFormState,
    melonDS::u32 shellState)
{
    if (powerup == 2 || actorPowerupState == 2 || actorPowerupFormState == 2)
        return 2;
    if (powerup == 4 || shellState != 0)
        return 4;
    if (actorPowerupState == 5 || actorPowerupFormState == 5)
        return 4;
    if (actorPowerupState == 3 || actorPowerupFormState == 3)
        return 5;
    if (actorPowerupState == 4 || actorPowerupFormState == 4)
        return 3;
    if (powerup == 1 || actorPowerupState == 1 || actorPowerupFormState == 1)
        return 1;
    if (powerup == 5)
        return 5;
    if (powerup != 0)
        return powerup;
    return 0;
}

// Stable, backfillable body-size category derived from the visual form.
// 0=mini, 1=small, 2=large (super/fire/shell), 3=mega.
melonDS::u32 AIPlayerBodySizeClass(melonDS::u32 visualPowerupKind)
{
    if (visualPowerupKind == 3)
        return 0;
    if (visualPowerupKind == 5)
        return 3;
    if (visualPowerupKind == 1 || visualPowerupKind == 2 || visualPowerupKind == 4)
        return 2;
    return 1;
}

melonDS::u32 AIVisualPowerupSourceMask(
    melonDS::u32 powerup,
    melonDS::u32 inventoryPowerup,
    melonDS::u32 actorPowerupState,
    melonDS::u32 actorPowerupFormState,
    melonDS::u32 shellState)
{
    (void)inventoryPowerup;
    melonDS::u32 mask = 0;
    if (powerup != 0)
        mask |= 1;
    if (actorPowerupState != 0)
        mask |= 4;
    if (actorPowerupFormState != 0)
        mask |= 8;
    if (shellState != 0)
        mask |= 16;
    return mask;
}

bool AIStarInvincibleCandidate(
    melonDS::u32 inventoryPowerup,
    melonDS::u32 actorPowerupState,
    melonDS::u32 actorPowerupFormState,
    melonDS::u32 shellState)
{
    return inventoryPowerup == 2 && actorPowerupState == 0 && actorPowerupFormState == 0 && shellState == 0;
}

std::int64_t AIWrappedDeltaX(std::int64_t x, std::int64_t origin)
{
    std::int64_t dx = x - origin;
    const std::int64_t wrapWidth = G.AI.Rule.HorizontalWrapWidth;
    if (wrapWidth <= 0)
        return dx;
    while (dx < -(wrapWidth / 2))
        dx += wrapWidth;
    while (dx > wrapWidth / 2)
        dx -= wrapWidth;
    return dx;
}

int AIFireballOwnerCandidateStateless(const GameStateSample& sample, int slotIndex, int& confidence, int& heuristic)
{
    confidence = 0;
    heuristic = 0;
    if (slotIndex < 0 || slotIndex >= kAIFireballSlotCount || sample.FireballSlotActive[slotIndex] == 0)
        return -1;

    const int kind = static_cast<int>(sample.FireballSlotKind[slotIndex]);
    if (kind == 0 || kind == 1)
    {
        confidence = 100;
        heuristic = 100;
        return kind;
    }
    if (kind == 2 || kind == 3)
    {
        confidence = 100;
        heuristic = 101;
        return -1;
    }

    const std::int64_t fireX = SignedU32(sample.FireballSlotPosX[slotIndex]);
    const std::int64_t fireY = SignedU32(sample.FireballSlotPosY[slotIndex]);
    const std::int64_t velX = SignedU32(sample.FireballSlotVelX[slotIndex]);
    const std::int64_t p0dx = AIWrappedDeltaX(fireX, SignedU32(sample.PlayerActor0PosX));
    const std::int64_t p0dy = fireY - SignedU32(sample.PlayerActor0PosY);
    const std::int64_t p1dx = AIWrappedDeltaX(fireX, SignedU32(sample.PlayerActor1PosX));
    const std::int64_t p1dy = fireY - SignedU32(sample.PlayerActor1PosY);
    const std::int64_t p0Dist2 = p0dx * p0dx + p0dy * p0dy;
    const std::int64_t p1Dist2 = p1dx * p1dx + p1dy * p1dy;
    const int closest = p0Dist2 <= p1Dist2 ? 0 : 1;
    const std::int64_t closestDist2 = closest == 0 ? p0Dist2 : p1Dist2;
    const std::int64_t otherDist2 = closest == 0 ? p1Dist2 : p0Dist2;
    constexpr std::int64_t kNearOwner = 96ll * 4096ll;
    constexpr std::int64_t kOwnerSeparation = 48ll * 4096ll;
    const bool closestNear = closestDist2 <= kNearOwner * kNearOwner;
    const bool closestSeparated = otherDist2 > closestDist2 + kOwnerSeparation * kOwnerSeparation;

    int velocityOwner = -1;
    if (velX > 0)
    {
        if (p0dx >= 0 && p1dx < 0)
            velocityOwner = 0;
        else if (p1dx >= 0 && p0dx < 0)
            velocityOwner = 1;
    }
    else if (velX < 0)
    {
        if (p0dx <= 0 && p1dx > 0)
            velocityOwner = 0;
        else if (p1dx <= 0 && p0dx > 0)
            velocityOwner = 1;
    }

    if (closestNear && velocityOwner == closest)
    {
        confidence = closestSeparated ? 80 : 65;
        heuristic = 3;
        return closest;
    }
    if (closestNear && closestSeparated)
    {
        confidence = 55;
        heuristic = 1;
        return closest;
    }
    if (velocityOwner >= 0)
    {
        confidence = 40;
        heuristic = 2;
        return velocityOwner;
    }
    return -1;
}

int AIFireballOwnerCandidate(
    int instanceID,
    const GameStateSample& sample,
    int slotIndex,
    int& confidence,
    int& heuristic,
    int& statelessOwner,
    int& statelessConfidence,
    int& statelessHeuristic,
    bool& tracked)
{
    tracked = false;
    statelessOwner = AIFireballOwnerCandidateStateless(sample, slotIndex, statelessConfidence, statelessHeuristic);
    confidence = statelessConfidence;
    heuristic = statelessHeuristic;
    if (slotIndex < 0 || slotIndex >= kAIFireballSlotCount || sample.FireballSlotActive[slotIndex] == 0)
        return statelessOwner;
    if (sample.FireballSlotKind[slotIndex] <= 3)
        return statelessOwner;
    if (instanceID < 0 || instanceID >= 16)
        return statelessOwner;

    return G.AIObservationRuntime.ResolveFireballOwner(
        instanceID,
        slotIndex,
        statelessOwner,
        statelessConfidence,
        statelessHeuristic,
        confidence,
        heuristic,
        tracked);
}

const char* AIObjectCategory(melonDS::u16 objectID, melonDS::u32 settings)
{
    if (objectID == kPlayerObjectID)
        return "player";
    if (objectID == kVsBattleStarActorObjectID && settings == kVsBattleStarActorSettings)
        return "big_star_actor";
    if (objectID == kVsBattleStarActorObjectID && IsVsDroppedStarActorSettings(settings))
        return "dropped_star_item";
    if (objectID == kVsBattleStarRelatedObjectID)
        return "big_star_related";
    if (objectID == kVsBattleStarCandidateObjectID)
        return "big_star_marker";
    if (objectID == kVsWorldItemObjectID && settings == kVsWorldItemSettings)
        return "world_item";
    if (objectID == kVsWorldItemObjectID && settings == kVsNeutralWorldItemSettings)
        return "neutral_item";
    if (objectID == kVsWorldItemObjectID && settings == kVsDroppedStarItemSettings)
        return "coin_item";
    if (objectID == kVsWorldItemObjectID)
        return "item";
    if (objectID == kCoinObjectID)
        return "coin";
    if (objectID == kGoombaObjectID ||
        objectID == kGoombaBigObjectID ||
        objectID == kGoombaMegaObjectID)
        return "enemy_goomba";
    if (objectID == kVsKoopaTroopaObjectID ||
        objectID == kKoopaTroopaAltObjectID)
        return "enemy_koopa";
    if (objectID == kVsMovingHazardObjectID && settings == kVsMovingHazardSettings)
        return "moving_hazard";
    if (objectID == kBulletBillObjectID ||
        objectID == kBulletBillAltObjectID ||
        objectID == kBulletBillBlasterObjectID ||
        objectID == kBulletBillBlasterAltObjectID ||
        objectID == kThwompObjectID ||
        objectID == kThwompAltObjectID ||
        objectID == kFirebarObjectID ||
        objectID == kBobOmbObjectID)
        return "hazard";
    if (objectID == kDonutLiftObjectID ||
        objectID == kTrampolineObjectID ||
        objectID == kSpinBlockObjectID ||
        objectID == kSpinBlockAltObjectID ||
        objectID == kSpinBlockFinalObjectID)
        return "platform";
    if (objectID == kWarpEntranceObjectID)
        return "warp_entrance";
    if (objectID == kItemSpawnEffectObjectID)
        return "item_spawn_effect";
    if (objectID == kStageCameraObjectID)
        return "camera";
    if (objectID == kStageSceneObjectID)
        return "stage_scene";
    if (objectID == kStageFXObjectID)
        return "stage_fx";
    if (objectID == kStageActorManagerObjectID)
        return "stage_actor_manager";
    if (objectID == kStageControllerObjectID)
        return "stage_controller";
    if (objectID == kStageLayoutObjectID)
        return "stage_layout";
    if (objectID == kMvlObject267ID)
        return "mvl_object267";
    if (objectID == kVsConnectObjectID)
        return "vs_connect";
    if (objectID == kCourseSelectObjectID)
        return "course_select";
    return "object";
}

melonDS::u32 AIObjectCategoryMask(const char* category)
{
    if (std::strcmp(category, "player") == 0)
        return 1u << 0;
    if (std::strcmp(category, "big_star_actor") == 0 ||
        std::strcmp(category, "big_star_related") == 0)
        return 1u << 1;
    if (std::strcmp(category, "world_item") == 0 ||
        std::strcmp(category, "neutral_item") == 0 ||
        std::strcmp(category, "coin_item") == 0 ||
        std::strcmp(category, "dropped_star_item") == 0 ||
        std::strcmp(category, "item") == 0 ||
        std::strcmp(category, "coin") == 0)
        return 1u << 2;
    if (std::strcmp(category, "moving_hazard") == 0 ||
        std::strcmp(category, "enemy_goomba") == 0 ||
        std::strcmp(category, "enemy_koopa") == 0 ||
        std::strcmp(category, "hazard") == 0)
        return 1u << 3;
    if (std::strcmp(category, "platform") == 0 ||
        std::strcmp(category, "warp_entrance") == 0)
        return 1u << 6;
    if (std::strcmp(category, "item_spawn_effect") == 0)
        return 1u << 7;
    if (std::strcmp(category, "camera") == 0 ||
        std::strcmp(category, "big_star_marker") == 0 ||
        std::strcmp(category, "stage_scene") == 0 ||
        std::strcmp(category, "stage_fx") == 0 ||
        std::strcmp(category, "stage_actor_manager") == 0 ||
        std::strcmp(category, "stage_controller") == 0 ||
        std::strcmp(category, "stage_layout") == 0 ||
        std::strcmp(category, "mvl_object267") == 0 ||
        std::strcmp(category, "vs_connect") == 0 ||
        std::strcmp(category, "course_select") == 0)
        return 1u << 4;
    return 1u << 5;
}

std::int64_t DistanceSquared2D(
    melonDS::u32 ax,
    melonDS::u32 ay,
    melonDS::u32 bx,
    melonDS::u32 by)
{
    const std::int64_t dx = AIWrappedDeltaX(SignedU32(ax), SignedU32(bx));
    const std::int64_t dy = static_cast<std::int64_t>(SignedU32(ay)) - SignedU32(by);
    return dx * dx + dy * dy;
}

bool IsInCameraRect(
    melonDS::u32 x,
    melonDS::u32 y,
    melonDS::u32 cameraX,
    melonDS::u32 cameraY,
    melonDS::u32 cameraWidth,
    melonDS::u32 cameraHeight)
{
    std::int64_t sx = static_cast<std::int64_t>(SignedU32(x)) - SignedU32(cameraX);
    const std::int64_t wrapWidth = G.AI.Rule.HorizontalWrapWidth;
    if (wrapWidth > 0)
    {
        while (sx < 0)
            sx += wrapWidth;
        while (sx >= wrapWidth)
            sx -= wrapWidth;
    }
    const std::int64_t sy = static_cast<std::int64_t>(SignedU32(y)) - SignedU32(cameraY);
    return sx >= 0 && sy >= 0 && sx < SignedU32(cameraWidth) && sy < SignedU32(cameraHeight);
}

void WriteAIScreenJson(
    std::ostream& out,
    const char* name,
    melonDS::u32 x,
    melonDS::u32 y,
    melonDS::u32 cameraX,
    melonDS::u32 cameraY,
    melonDS::u32 cameraWidth,
    melonDS::u32 cameraHeight)
{
    std::int64_t screenX = static_cast<std::int64_t>(SignedU32(x)) - SignedU32(cameraX);
    const std::int64_t wrapWidth = G.AI.Rule.HorizontalWrapWidth;
    if (wrapWidth > 0)
    {
        while (screenX < 0)
            screenX += wrapWidth;
        while (screenX >= wrapWidth)
            screenX -= wrapWidth;
    }
    const std::int32_t screenY = SignedU32(y) - SignedU32(cameraY);
    const bool inViewX = screenX >= 0 && screenX < SignedU32(cameraWidth);
    const bool inViewY = screenY >= 0 && screenY < SignedU32(cameraHeight);
    out << "\"" << name << "\":{\"x\":" << screenX
        << ",\"y\":" << screenY
        << ",\"inViewX\":" << (inViewX ? 1 : 0)
        << ",\"inViewY\":" << (inViewY ? 1 : 0)
        << ",\"inView\":" << (IsInCameraRect(x, y, cameraX, cameraY, cameraWidth, cameraHeight) ? 1 : 0)
        << "}";
}

void WriteAIInputJson(std::ostream& out, const char* name, melonDS::u32 held, melonDS::u32 pressed)
{
    out << "\"" << name << "\":{\"held\":" << held << ",\"heldHex\":";
    WriteJsonHex(out, held, 3);
    out << ",\"pressed\":" << pressed << ",\"pressedHex\":";
    WriteJsonHex(out, pressed, 3);
    out << "}";
}

void WriteAIAppliedInputJson(std::ostream& out, int instanceID, int player)
{
    out << "\"appliedPlayer" << player << "\":{";
    const AIObservation::Runtime::AppliedInputRecord* record =
        G.AIObservationRuntime.AppliedInput(instanceID, player);
    if (!record)
    {
        out << "\"valid\":0}";
        return;
    }

    const InputState& input = record->Input;
    const melonDS::u32 held = (~input.KeyMask) & 0x0FFF;
    out << "\"valid\":1"
        << ",\"frame\":" << record->Frame
        << ",\"keyMask\":";
    WriteJsonHex(out, input.KeyMask, 3);
    out << ",\"held\":" << held << ",\"heldHex\":";
    WriteJsonHex(out, held, 3);
    out << ",\"touching\":" << (input.Touching ? 1 : 0)
        << ",\"touchX\":" << input.TouchX
        << ",\"touchY\":" << input.TouchY
        << "}";
}

void WriteAIVec3Json(std::ostream& out, const char* name, melonDS::u32 x, melonDS::u32 y, melonDS::u32 z)
{
    out << "\"" << name << "\":{\"x\":" << SignedU32(x)
        << ",\"y\":" << SignedU32(y)
        << ",\"z\":" << SignedU32(z) << "}";
}

void WriteAIContactJson(std::ostream& out, melonDS::u32 collisionFlag, melonDS::u32 environmentFlag)
{
    auto bit = [](melonDS::u32 value, melonDS::u32 mask) { return (value & mask) ? 1 : 0; };
    const int ground =
        bit(collisionFlag, 0x00000001) ||
        bit(collisionFlag, 0x00002000) ||
        bit(collisionFlag, 0x00008000) ||
        bit(collisionFlag, 0x08000000);
    const int wallLeft =
        bit(collisionFlag, 0x00000008) ||
        bit(collisionFlag, 0x00000400) ||
        bit(collisionFlag, 0x20000000);
    const int wallRight =
        bit(collisionFlag, 0x00000010) ||
        bit(collisionFlag, 0x00000800) ||
        bit(collisionFlag, 0x40000000);
    const int submerged =
        bit(collisionFlag, 0x00400000) ||
        bit(environmentFlag, 0x00000002) ||
        bit(environmentFlag, 0x00000200);
    out << "{\"ground\":" << ground
        << ",\"tileGround\":" << bit(collisionFlag, 0x00000001)
        << ",\"hoverTileGround\":" << bit(collisionFlag, 0x00002000)
        << ",\"colliderGround\":" << bit(collisionFlag, 0x00008000)
        << ",\"predictGround\":" << bit(collisionFlag, 0x08000000)
        << ",\"ceiling\":" << bit(collisionFlag, 0x00000002)
        << ",\"pushWall\":" << bit(collisionFlag, 0x00000004)
        << ",\"wallLeft\":" << wallLeft
        << ",\"wallRight\":" << wallRight
        << ",\"edgeGrab\":" << bit(collisionFlag, 0x00001000)
        << ",\"slipperyGround\":" << bit(collisionFlag, 0x00004000)
        << ",\"water\":" << bit(collisionFlag, 0x00000020)
        << ",\"liquid\":" << bit(collisionFlag, 0x00400000)
        << ",\"submerged\":" << submerged
        << ",\"quicksandTop\":" << bit(collisionFlag, 0x00010000)
        << ",\"quicksand\":" << bit(collisionFlag, 0x00020000)
        << ",\"rope\":" << bit(collisionFlag, 0x00040000)
        << ",\"tightrope\":" << bit(collisionFlag, 0x00800000)
        << ",\"ledge\":" << bit(collisionFlag, 0x01000000)
        << ",\"pole\":" << bit(collisionFlag, 0x10000000)
        << ",\"spikesLeft\":" << bit(collisionFlag, 0x20000000)
        << ",\"spikesRight\":" << bit(collisionFlag, 0x40000000)
        << ",\"slowGround\":" << bit(environmentFlag, 0x00000001)
        << ",\"conveyorLeft\":" << bit(environmentFlag, 0x00000008)
        << ",\"conveyorRight\":" << bit(environmentFlag, 0x00000010)
        << ",\"snowyGround\":" << bit(environmentFlag, 0x00000020)
        << ",\"sandyGround\":" << bit(environmentFlag, 0x00000040)
        << ",\"destroyedGround\":" << bit(environmentFlag, 0x00000100)
        << ",\"climbableBottom\":" << bit(environmentFlag, 0x00000400)
        << ",\"climbableTop\":" << bit(environmentFlag, 0x00000800)
        << ",\"destroyedCeiling\":" << bit(environmentFlag, 0x00001000)
        << ",\"wrapLeft\":" << bit(environmentFlag, 0x00002000)
        << ",\"wrapRight\":" << bit(environmentFlag, 0x00004000)
        << "}";
}

void WriteAITileTypeJson(std::ostream& out, melonDS::u32 tileType)
{
    auto bit = [tileType](melonDS::u32 mask) { return (tileType & mask) ? 1 : 0; };
    out << "{\"solid\":" << bit(0x00010000)
        << ",\"coin\":" << bit(0x00020000)
        << ",\"questionBlock\":" << bit(0x00040000)
        << ",\"breakableBlock\":" << bit(0x00080000)
        << ",\"brickBlock\":" << bit(0x00100000)
        << ",\"slope\":" << bit(0x00200000)
        << ",\"ceilingSlope\":" << bit(0x00400000)
        << ",\"scanSolid\":" << bit(0x00800000)
        << ",\"entrance\":" << bit(0x01000000)
        << ",\"water\":" << bit(0x02000000)
        << ",\"climbable\":" << bit(0x04000000)
        << ",\"partialSolid\":" << bit(0x08000000)
        << ",\"harmful\":" << bit(0x10000000)
        << ",\"invisibleBlock\":" << bit(0x20000000)
        << ",\"solidOnBottom\":" << bit(0x40000000)
        << ",\"solidOnTop\":" << bit(0x80000000)
        << ",\"modifier\":" << ((tileType & 0x0000F000u) >> 12)
        << ",\"lowType\":" << (tileType & 0x000000FFu)
        << ",\"storageContents\":" << (tileType & 0x00000C3Fu)
        << "}";
}

void WriteAITileBlockStateJson(std::ostream& out, melonDS::u32 tileID, melonDS::u32 tileType)
{
    const int questionBlock = (tileType & 0x00040000u) ? 1 : 0;
    const int breakableBlock = (tileType & 0x00080000u) ? 1 : 0;
    const int brickBlock = (tileType & 0x00100000u) ? 1 : 0;
    const int invisibleBlock = (tileType & 0x20000000u) ? 1 : 0;
    const melonDS::u32 storageContents = tileType & 0x00000C3Fu;
    const int anyBlock = questionBlock || breakableBlock || brickBlock || invisibleBlock;
    const int hasStorageContents = anyBlock && storageContents != 0;
    const int itemBox = hasStorageContents;
    const int storageBreakableCandidate = anyBlock && breakableBlock && hasStorageContents;
    const int hiddenOrRescueCandidate = anyBlock && invisibleBlock;
    const int visibleStorageBreakableCandidate = storageBreakableCandidate && !invisibleBlock;
    const int visibleSolidCandidate = anyBlock && (questionBlock || brickBlock || (breakableBlock && !invisibleBlock));
    out << "{\"any\":" << anyBlock
        << ",\"itemBox\":" << itemBox
        << ",\"question\":" << questionBlock
        << ",\"breakable\":" << breakableBlock
        << ",\"brick\":" << brickBlock
        << ",\"invisible\":" << invisibleBlock
        << ",\"hasStorageContents\":" << hasStorageContents
        << ",\"storageContents\":" << storageContents
        << ",\"modifier\":" << ((tileType & 0x0000F000u) >> 12)
        << ",\"currentTileId\":" << tileID
        << ",\"currentBehavior\":";
    WriteJsonHex(out, tileType);
    out << ",\"storageBreakableCandidate\":" << storageBreakableCandidate
        << ",\"hiddenOrRescueCandidate\":" << hiddenOrRescueCandidate
        << ",\"visibleStorageBreakableCandidate\":" << visibleStorageBreakableCandidate
        << ",\"visibleSolidCandidate\":" << visibleSolidCandidate;
    out << "}";
}

bool AITileBehaviorSolidish(melonDS::u32 tileType)
{
    return (tileType & (
        0x08990000u | // CollisionMgr::scanSolidTile solid mask
        0x00040000u | // question block
        0x00200000u | // slope
        0x40000000u | // solid on bottom
        0x80000000u)) != 0;
}

void WriteAITileProbePointJson(std::ostream& out, const AITileProbeSample& sample)
{
    out << "{\"name\":\"" << sample.Name
        << "\",\"found\":" << sample.Found
        << ",\"status\":" << sample.Status
        << ",\"offsetX\":" << SignedU32(sample.OffsetX)
        << ",\"offsetY\":" << SignedU32(sample.OffsetY);
    if (!sample.Found)
    {
        out << ",\"worldX\":" << SignedU32(sample.WorldX)
            << ",\"worldY\":" << SignedU32(sample.WorldY)
            << ",\"pixelX\":" << sample.PixelX
            << ",\"pixelY\":" << sample.PixelY
            << ",\"chunkId\":" << sample.ChunkID
            << ",\"tileId\":" << sample.TileID
            << ",\"chunkPtr\":";
        WriteJsonHex(out, sample.ChunkPtr);
        out << ",\"behaviorTable\":";
        WriteJsonHex(out, sample.BehaviorTable);
        out << "}";
        return;
    }

    out << ",\"worldX\":" << SignedU32(sample.WorldX)
        << ",\"worldY\":" << SignedU32(sample.WorldY)
        << ",\"pixelX\":" << sample.PixelX
        << ",\"pixelY\":" << sample.PixelY
        << ",\"chunkId\":" << sample.ChunkID
        << ",\"tileId\":" << sample.TileID
        << ",\"chunkPtr\":";
    WriteJsonHex(out, sample.ChunkPtr);
    out << ",\"behaviorTable\":";
    WriteJsonHex(out, sample.BehaviorTable);
    out << ",\"behavior\":";
    WriteJsonHex(out, sample.Behavior);
    out << ",\"tile\":";
    WriteAITileTypeJson(out, sample.Behavior);
    out << ",\"block\":";
    WriteAITileBlockStateJson(out, sample.TileID, sample.Behavior);
    out << ",\"solidish\":" << (AITileBehaviorSolidish(sample.Behavior) ? 1 : 0)
        << "}";
}

void WriteAITileGridCellJson(std::ostream& out, const AITileGridSample& cell)
{
    out << "{\"row\":" << cell.Row
        << ",\"col\":" << cell.Col
        << ",\"relTileX\":" << SignedU32(cell.RelTileX)
        << ",\"relTileY\":" << SignedU32(cell.RelTileY)
        << ",\"tileX\":" << SignedU32(cell.TileX)
        << ",\"tileY\":" << SignedU32(cell.TileY);
    const AITileProbeSample& tile = cell.Tile;
    out << ",\"found\":" << tile.Found
        << ",\"status\":" << tile.Status
        << ",\"pixelX\":" << tile.PixelX
        << ",\"pixelY\":" << tile.PixelY
        << ",\"tileId\":" << tile.TileID;
    if (tile.Found)
    {
        out << ",\"behavior\":";
        WriteJsonHex(out, tile.Behavior);
        out << ",\"tile\":";
        WriteAITileTypeJson(out, tile.Behavior);
        out << ",\"block\":";
        WriteAITileBlockStateJson(out, tile.TileID, tile.Behavior);
        out << ",\"solidish\":" << (AITileBehaviorSolidish(tile.Behavior) ? 1 : 0);
    }
    out << "}";
}

int AITileProbeSolidishValue(const AIPlayerTileProbeSample& probe, const char* name);

int AIObservationV2EntityCategoryID(const char* category)
{
    if (std::strcmp(category, "player") == 0)
        return 1;
    if (std::strcmp(category, "big_star_actor") == 0)
        return 2;
    if (std::strcmp(category, "world_item") == 0)
        return 3;
    if (std::strcmp(category, "neutral_item") == 0)
        return 4;
    if (std::strcmp(category, "coin_item") == 0)
        return 5;
    if (std::strcmp(category, "dropped_star_item") == 0)
        return 6;
    if (std::strcmp(category, "coin") == 0)
        return 7;
    if (std::strcmp(category, "moving_hazard") == 0)
        return 8;
    if (std::strcmp(category, "hazard") == 0)
        return 9;
    if (std::strcmp(category, "projectile") == 0)
        return 10;
    if (std::strcmp(category, "player_fireball") == 0)
        return 11;
    if (std::strcmp(category, "enemy_fireball") == 0)
        return 12;
    if (std::strcmp(category, "enemy_goomba") == 0)
        return 13;
    if (std::strcmp(category, "enemy_koopa") == 0)
        return 14;
    if (std::strcmp(category, "platform") == 0)
        return 15;
    return 0;
}

melonDS::u32 AIObservationV2TerrainMask(const AITileGridSample& cell)
{
    if (!cell.Tile.Found)
        return 0;
    const melonDS::u32 t = cell.Tile.Behavior;
    const int question = (t & 0x00040000u) ? 1 : 0;
    const int breakable = (t & 0x00080000u) ? 1 : 0;
    const int brick = (t & 0x00100000u) ? 1 : 0;
    const int invisible = (t & 0x20000000u) ? 1 : 0;
    const melonDS::u32 storage = t & 0x00000C3Fu;
    const int anyBlock = question || breakable || brick || invisible;
    const int hasStorage = anyBlock && storage != 0;
    const int storageBreakable = anyBlock && breakable && hasStorage;
    melonDS::u32 mask = 0;
    auto bit = [&mask](int index, bool value) {
        if (value)
            mask |= 1u << index;
    };
    bit(0, AITileBehaviorSolidish(t) || (t & 0x00010000u));
    bit(1, t & 0x00020000u);
    bit(2, question);
    bit(3, breakable);
    bit(4, brick);
    bit(5, t & 0x00200000u);
    bit(6, t & 0x00800000u);
    bit(7, t & 0x01000000u);
    bit(8, t & 0x02000000u);
    bit(9, t & 0x08000000u);
    bit(10, t & 0x10000000u);
    bit(11, invisible);
    bit(12, anyBlock && hasStorage);
    bit(13, anyBlock && invisible);
    bit(14, storageBreakable && !invisible);
    bit(15, anyBlock && (question || brick || (breakable && !invisible)));
    return mask;
}

bool AITerrainMaskPhysicalSolid(melonDS::u32 mask)
{
    const bool solid = (mask & ((1u << 0) | (1u << 15))) != 0;
    const bool hidden = (mask & ((1u << 11) | (1u << 13))) != 0;
    return solid && !hidden;
}

const AITileGridSample* FindAITileGridCellByRel(const AIPlayerTileProbeSample& probe, int relTileX, int relTileY)
{
    const int col = relTileX - kAITileGridMinRelX;
    const int row = relTileY - kAITileGridMinRelY;
    if (row < 0 || row >= kAITileGridHeight || col < 0 || col >= kAITileGridWidth)
        return nullptr;
    return &probe.Grid[row * kAITileGridWidth + col];
}

bool AITerrainGridCellPhysicalSolid(const AIPlayerTileProbeSample& probe, int relTileX, int relTileY)
{
    const AITileGridSample* cell = FindAITileGridCellByRel(probe, relTileX, relTileY);
    return cell && probe.Found && AITerrainMaskPhysicalSolid(AIObservationV2TerrainMask(*cell));
}

bool AITerrainGridAnyPhysicalSolid(
    const AIPlayerTileProbeSample& probe,
    int relXMin,
    int relXMax,
    int relYMin,
    int relYMax)
{
    if (!probe.Found)
        return false;
    for (int relY = relYMin; relY <= relYMax; relY++)
    {
        for (int relX = relXMin; relX <= relXMax; relX++)
        {
            if (AITerrainGridCellPhysicalSolid(probe, relX, relY))
                return true;
        }
    }
    return false;
}

int AITerrainGridSupportRow(const AIPlayerTileProbeSample& probe)
{
    if (!probe.Found)
        return 3;
    for (int relY = 0; relY <= 6; relY++)
    {
        if (AITerrainGridAnyPhysicalSolid(probe, -1, 1, relY, relY))
            return relY;
    }
    return 3;
}

bool AITerrainGridFloorAt(const AIPlayerTileProbeSample& probe, int relX, int supportRow)
{
    return AITerrainGridAnyPhysicalSolid(probe, relX, relX, supportRow, supportRow + 3);
}

int DivRoundNearest(std::int64_t value, std::int64_t divisor)
{
    if (divisor <= 0)
        return 0;
    if (value < 0)
        return -static_cast<int>((-value + divisor / 2) / divisor);
    return static_cast<int>((value + divisor / 2) / divisor);
}

bool AITerrainTargetHasFloorBelow(
    const AIPlayerTileProbeSample& probe,
    melonDS::u32 selfX,
    melonDS::u32 selfY,
    melonDS::u32 targetX,
    melonDS::u32 targetY)
{
    if (!probe.Found)
        return true;

    constexpr std::int64_t kTileFixed = 16 * 4096;
    const std::int64_t dx = AIWrappedDeltaX(SignedU32(targetX), SignedU32(selfX));
    const std::int64_t dy = static_cast<std::int64_t>(SignedU32(targetY)) - SignedU32(selfY);
    const int relTileX = DivRoundNearest(dx, kTileFixed);
    const int relTileY = DivRoundNearest(-dy, kTileFixed);
    const int maxRelX = kAITileGridMinRelX + kAITileGridWidth - 1;
    const int maxRelY = kAITileGridMinRelY + kAITileGridHeight - 1;
    if (relTileX < kAITileGridMinRelX || relTileX > maxRelX)
        return true;

    const int minRelY = std::max(relTileY, kAITileGridMinRelY);
    const int maxSearchRelY = std::min(relTileY + 6, maxRelY);
    for (int relY = minRelY; relY <= maxSearchRelY; relY++)
    {
        if (AITerrainGridCellPhysicalSolid(probe, relTileX, relY))
            return true;
    }
    return false;
}

int AITerrainGridFirstFloorGap(
    const AIPlayerTileProbeSample& probe,
    int direction,
    int supportRow,
    int maxSteps)
{
    for (int step = 1; step <= maxSteps; step++)
    {
        const bool floor = AITerrainGridFloorAt(probe, direction * step, supportRow);
        if (!floor)
            return step;
    }
    return 0;
}

AITerrainDerivedSummary DeriveAITerrainSummaryFromGrid(
    const AIPlayerTileProbeSample& probe,
    bool contactGround,
    bool contactWallLeft,
    bool contactWallRight)
{
    AITerrainDerivedSummary out {};
    const int direction = SignedU32(probe.Direction) < 0 ? -1 : 1;
    const bool found = probe.Found != 0;
    const int supportRow = AITerrainGridSupportRow(probe);
    const int bodyMinY = supportRow - 3;
    const int bodyMaxY = supportRow - 1;
    out.GroundBelowSolid = found && AITerrainGridAnyPhysicalSolid(probe, -1, 1, supportRow, supportRow + 3) ? 1 : 0;
    out.BlockedAhead = found && AITerrainGridAnyPhysicalSolid(
        probe,
        std::min(direction, direction * 2),
        std::max(direction, direction * 2),
        bodyMinY,
        bodyMaxY) ? 1 : 0;
    out.BlockedLeft = contactWallLeft || (found && AITerrainGridAnyPhysicalSolid(probe, -1, -1, bodyMinY, bodyMaxY)) ? 1 : 0;
    out.BlockedRight = contactWallRight || (found && AITerrainGridAnyPhysicalSolid(probe, 1, 1, bodyMinY, bodyMaxY)) ? 1 : 0;

    const bool aheadFloor1 = found && AITerrainGridFloorAt(probe, direction, supportRow);
    const bool aheadFloor2 = found && AITerrainGridFloorAt(probe, direction * 2, supportRow);
    const bool leftFloor1 = found && AITerrainGridFloorAt(probe, -1, supportRow);
    const bool leftFloor2 = found && AITerrainGridFloorAt(probe, -2, supportRow);
    const bool rightFloor1 = found && AITerrainGridFloorAt(probe, 1, supportRow);
    const bool rightFloor2 = found && AITerrainGridFloorAt(probe, 2, supportRow);
    out.HoleAhead = found && !aheadFloor1 && !aheadFloor2 ? 1 : 0;
    out.HoleLeft = found && !leftFloor1 && !leftFloor2 ? 1 : 0;
    out.HoleRight = found && !rightFloor1 && !rightFloor2 ? 1 : 0;
    out.FarHoleLeft = found && AITerrainGridFirstFloorGap(probe, -1, supportRow, 12) > 0 ? 1 : 0;
    out.FarHoleRight = found && AITerrainGridFirstFloorGap(probe, 1, supportRow, 12) > 0 ? 1 : 0;
    out.EffectiveGroundBelowSolid = out.GroundBelowSolid || contactGround ? 1 : 0;
    out.HoleSuppressedByContact = contactGround && !out.GroundBelowSolid ? 1 : 0;
    out.EffectiveHoleAhead = out.HoleAhead && !out.HoleSuppressedByContact ? 1 : 0;
    out.EffectiveHoleLeft = out.HoleLeft && !out.HoleSuppressedByContact ? 1 : 0;
    out.EffectiveHoleRight = out.HoleRight && !out.HoleSuppressedByContact ? 1 : 0;
    return out;
}

void WriteAIObservationV2Vec3Json(std::ostream& out, const char* name, melonDS::u32 x, melonDS::u32 y, melonDS::u32 z)
{
    WriteAIVec3Json(out, name, x, y, z);
}

void WriteAIObservationV2ScreenJson(
    std::ostream& out,
    const GameStateSample& sample,
    melonDS::u32 x,
    melonDS::u32 y)
{
    out << "\"screen\":{";
    WriteAIScreenJson(
        out,
        "camera0",
        x,
        y,
        sample.StageCameraGlobalX0,
        sample.StageCameraGlobalY0,
        sample.StageCameraGlobalWidth0,
        sample.StageCameraGlobalHeight0);
    out << ",";
    WriteAIScreenJson(
        out,
        "camera1",
        x,
        y,
        sample.StageCameraGlobalX1,
        sample.StageCameraGlobalY1,
        sample.StageCameraGlobalWidth1,
        sample.StageCameraGlobalHeight1);
    out << "}";
}

void WriteAIObservationV2TerrainJson(std::ostream& out, const AIPlayerTileProbeSample& probe)
{
    out << "\"terrain\":{\"encoding\":\"sparse_channel_mask_v2\""
        << ",\"width\":" << kAITileGridWidth
        << ",\"height\":" << kAITileGridHeight
        << ",\"minRelTileX\":" << kAITileGridMinRelX
        << ",\"minRelTileY\":" << kAITileGridMinRelY
        << ",\"channels\":[\"solid\",\"coin\",\"question\",\"breakable\",\"brick\",\"slope\",\"scanSolid\",\"entrance\",\"water\",\"partialSolid\",\"harmful\",\"invisible\",\"itemBox\",\"hiddenOrRescue\",\"visibleStorageBreakable\",\"visibleSolid\"]"
        << ",\"omittedCellFound\":" << (probe.Found ? 1 : 0)
        << ",\"omittedCellStatus\":0"
        << ",\"cells\":[";
    bool first = true;
    if (probe.Found)
    {
        for (int i = 0; i < kAITileGridCount; i++)
        {
            const AITileGridSample& cell = probe.Grid[i];
            const melonDS::u32 mask = AIObservationV2TerrainMask(cell);
            const bool shouldLog = mask != 0 || !cell.Tile.Found || cell.Tile.Status != 0;
            if (!shouldLog)
                continue;
            if (!first)
                out << ",";
            first = false;
            out << "{\"r\":" << cell.Row
                << ",\"c\":" << cell.Col
                << ",\"rx\":" << SignedU32(cell.RelTileX)
                << ",\"ry\":" << SignedU32(cell.RelTileY)
                << ",\"found\":" << cell.Tile.Found
                << ",\"status\":" << cell.Tile.Status
                << ",\"mask\":" << mask
                << ",\"tileId\":" << cell.Tile.TileID
                << ",\"behavior\":" << cell.Tile.Behavior
                << "}";
        }
    }
    out << "]}";
}

void WriteAIObservationV2TileSummaryJson(
    std::ostream& out,
    const AIPlayerTileProbeSample& probe,
    bool contactGround,
    bool contactWallLeft,
    bool contactWallRight)
{
    const int groundBelow = AITileProbeSolidishValue(probe, "below");
    const int aheadBody = AITileProbeSolidishValue(probe, "aheadBody");
    const int aheadFeet = AITileProbeSolidishValue(probe, "aheadFeet");
    const int aheadBelow = AITileProbeSolidishValue(probe, "aheadBelow");
    const int ahead2Below = AITileProbeSolidishValue(probe, "ahead2Below");
    const int leftBody = AITileProbeSolidishValue(probe, "leftBody");
    const int leftBelow = AITileProbeSolidishValue(probe, "leftBelow");
    const int left2Below = AITileProbeSolidishValue(probe, "left2Below");
    const int rightBody = AITileProbeSolidishValue(probe, "rightBody");
    const int rightBelow = AITileProbeSolidishValue(probe, "rightBelow");
    const int right2Below = AITileProbeSolidishValue(probe, "right2Below");
    const int probeHoleAhead = probe.Found && !aheadBelow && !ahead2Below ? 1 : 0;
    const int probeHoleLeft = probe.Found && !leftBelow && !left2Below ? 1 : 0;
    const int probeHoleRight = probe.Found && !rightBelow && !right2Below ? 1 : 0;
    const int probeSuppressHole = contactGround && !groundBelow ? 1 : 0;
    const int probeAmbiguousSideBody =
        contactGround && leftBody && rightBody && !contactWallLeft && !contactWallRight ? 1 : 0;
    const int probeBlockedAhead = aheadBody || aheadFeet ? 1 : 0;
    const int probeBlockedLeft = contactWallLeft || (leftBody && !probeAmbiguousSideBody) ? 1 : 0;
    const int probeBlockedRight = contactWallRight || (rightBody && !probeAmbiguousSideBody) ? 1 : 0;
    const AITerrainDerivedSummary grid =
        DeriveAITerrainSummaryFromGrid(probe, contactGround, contactWallLeft, contactWallRight);
    out << "\"tileSummary\":{\"source\":\"terrain_grid\""
        << ",\"groundBelowSolid\":" << grid.GroundBelowSolid
        << ",\"aheadBodySolid\":" << aheadBody
        << ",\"aheadFeetSolid\":" << aheadFeet
        << ",\"aheadBelowSolid\":" << aheadBelow
        << ",\"ahead2BelowSolid\":" << ahead2Below
        << ",\"leftBodySolid\":" << leftBody
        << ",\"leftBelowSolid\":" << leftBelow
        << ",\"left2BelowSolid\":" << left2Below
        << ",\"rightBodySolid\":" << rightBody
        << ",\"rightBelowSolid\":" << rightBelow
        << ",\"right2BelowSolid\":" << right2Below
        << ",\"probeGroundBelowSolid\":" << groundBelow
        << ",\"probeBlockedAhead\":" << probeBlockedAhead
        << ",\"probeBlockedLeft\":" << probeBlockedLeft
        << ",\"probeBlockedRight\":" << probeBlockedRight
        << ",\"probeHoleAhead\":" << probeHoleAhead
        << ",\"probeHoleLeft\":" << probeHoleLeft
        << ",\"probeHoleRight\":" << probeHoleRight
        << ",\"blockedAhead\":" << grid.BlockedAhead
        << ",\"holeAhead\":" << grid.HoleAhead
        << ",\"blockedLeft\":" << grid.BlockedLeft
        << ",\"holeLeft\":" << grid.HoleLeft
        << ",\"blockedRight\":" << grid.BlockedRight
        << ",\"holeRight\":" << grid.HoleRight
        << ",\"contactGround\":" << (contactGround ? 1 : 0)
        << ",\"ambiguousSideBody\":" << probeAmbiguousSideBody
        << ",\"effectiveGroundBelowSolid\":" << grid.EffectiveGroundBelowSolid
        << ",\"holeSuppressedByContact\":" << grid.HoleSuppressedByContact
        << ",\"probeHoleSuppressedByContact\":" << probeSuppressHole
        << ",\"effectiveHoleAhead\":" << grid.EffectiveHoleAhead
        << ",\"effectiveHoleLeft\":" << grid.EffectiveHoleLeft
        << ",\"effectiveHoleRight\":" << grid.EffectiveHoleRight
        << "}";
}

void WriteAIPlayerHitboxJson(std::ostream& out, const PlayerHitboxSample& hitbox)
{
    out << "{\"found\":" << hitbox.Found;
    if (!hitbox.Found)
    {
        out << "}";
        return;
    }
    const std::int64_t centerX = SignedU32(hitbox.CenterOffsetX);
    const std::int64_t centerY = SignedU32(hitbox.CenterOffsetY);
    const std::int64_t halfWidth = SignedU32(hitbox.HalfWidth);
    const std::int64_t halfHeight = SignedU32(hitbox.HalfHeight);
    out << ",\"fixedPointShift\":12"
        << ",\"centerOffsetX\":" << centerX
        << ",\"centerOffsetY\":" << centerY
        << ",\"halfWidth\":" << halfWidth
        << ",\"halfHeight\":" << halfHeight
        << ",\"width\":" << (halfWidth * 2)
        << ",\"height\":" << (halfHeight * 2)
        << ",\"minOffsetX\":" << (centerX - halfWidth)
        << ",\"maxOffsetX\":" << (centerX + halfWidth)
        << ",\"minOffsetY\":" << (centerY - halfHeight)
        << ",\"maxOffsetY\":" << (centerY + halfHeight)
        << "}";
}

void WriteAICollisionSensorJson(std::ostream& out, const PlayerCollisionMgrSample::Sensor& sensor)
{
    out << "{\"found\":" << sensor.Found;
    if (!sensor.Found)
    {
        out << "}";
        return;
    }
    out << ",\"base\":";
    WriteJsonHex(out, sensor.Base);
    out << ",\"type\":" << sensor.Type
        << ",\"fixedPointShift\":12"
        << ",\"value1\":" << SignedU32(sensor.Value1)
        << ",\"value2\":" << SignedU32(sensor.Value2)
        << ",\"value3\":" << SignedU32(sensor.Value3)
        << "}";
}

void WriteAICollisionSensorsJson(std::ostream& out, const PlayerCollisionMgrSample& collisionMgr)
{
    out << "{\"bottom\":";
    WriteAICollisionSensorJson(out, collisionMgr.BottomSensor);
    out << ",\"top\":";
    WriteAICollisionSensorJson(out, collisionMgr.TopSensor);
    out << ",\"side\":";
    WriteAICollisionSensorJson(out, collisionMgr.SideSensor);
    out << ",\"line\":";
    WriteAICollisionSensorJson(out, collisionMgr.LineSensor);
    out << "}";
}

void WriteAIObservationV2PlayerJson(std::ostream& out, int index, const GameStateSample& sample, bool exactHitboxSchema)
{
    const bool p0 = index == 0;
    auto v = [p0](melonDS::u32 a, melonDS::u32 b) { return p0 ? a : b; };
    const melonDS::u32 collision = v(sample.PlayerActor0CollisionFlag, sample.PlayerActor1CollisionFlag);
    const melonDS::u32 environment = v(sample.PlayerActor0EnvironmentFlag, sample.PlayerActor1EnvironmentFlag);
    const melonDS::u32 posX = v(sample.PlayerActor0PosX, sample.PlayerActor1PosX);
    const melonDS::u32 posY = v(sample.PlayerActor0PosY, sample.PlayerActor1PosY);
    const melonDS::u32 powerup = v(sample.Player0Powerup, sample.Player1Powerup);
    const melonDS::u32 inventoryPowerup = v(sample.Player0InventoryPowerup, sample.Player1InventoryPowerup);
    const melonDS::u32 actorPowerupState = p0 ? sample.PlayerActor0PowerupState : sample.PlayerActor1PowerupState;
    const melonDS::u32 actorPowerupFormState = p0 ? sample.PlayerActor0PowerupFormState : sample.PlayerActor1PowerupFormState;
    const melonDS::u32 shellState = p0 ? sample.PlayerActor0ShellState : sample.PlayerActor1ShellState;
    const melonDS::u32 visualPowerup =
        AIVisualPowerupKindCandidate(powerup, inventoryPowerup, actorPowerupState, actorPowerupFormState, shellState);
    const melonDS::u32 visualSource =
        AIVisualPowerupSourceMask(powerup, inventoryPowerup, actorPowerupState, actorPowerupFormState, shellState);
    const bool damagePhysicsGuard =
        (v(sample.PlayerActor0PhysicsFlag, sample.PlayerActor1PhysicsFlag) & 0x80000000u) != 0;
    const melonDS::u32 damageCooldown = p0 ? sample.PlayerActor0DamageCooldown : sample.PlayerActor1DamageCooldown;
    const melonDS::u32 damageGuardFlag = p0 ? sample.PlayerActor0DamageGuardFlag : sample.PlayerActor1DamageGuardFlag;
    const melonDS::u32 damageGuardTimer = p0 ? sample.Player0DamageGuardTimer : sample.Player1DamageGuardTimer;
    const bool starInvincible = AIStarInvincibleCandidate(inventoryPowerup, actorPowerupState, actorPowerupFormState, shellState);
    const bool damageInvulnerable = damageGuardTimer != 0 || damageCooldown != 0 || damageGuardFlag != 0 || damagePhysicsGuard;
    const AIPlayerTileProbeSample& tileProbe = p0 ? sample.PlayerActor0TileProbe : sample.PlayerActor1TileProbe;
    const PlayerHitboxSample& hitbox = p0 ? sample.PlayerActor0Hitbox : sample.PlayerActor1Hitbox;
    const PlayerCollisionMgrSample& collisionMgr =
        p0 ? sample.PlayerActor0CollisionMgr : sample.PlayerActor1CollisionMgr;
    const bool contactGround = AIPlayerContactGround(collision);
    const bool contactWallLeft = (collision & (0x00000008u | 0x00000400u | 0x20000000u)) != 0;
    const bool contactWallRight = (collision & (0x00000010u | 0x00000800u | 0x40000000u)) != 0;
    out << "{\"found\":" << v(sample.PlayerActor0Found, sample.PlayerActor1Found)
        << ",";
    WriteAIObservationV2Vec3Json(
        out,
        "pos",
        posX,
        posY,
        v(sample.PlayerActor0PosZ, sample.PlayerActor1PosZ));
    out << ",";
    WriteAIObservationV2Vec3Json(
        out,
        "vel",
        v(sample.PlayerActor0VelX, sample.PlayerActor1VelX),
        v(sample.PlayerActor0VelY, sample.PlayerActor1VelY),
        v(sample.PlayerActor0VelZ, sample.PlayerActor1VelZ));
    out << ",";
    WriteAIObservationV2ScreenJson(out, sample, posX, posY);
    out << ",\"contact\":";
    WriteAIContactJson(out, collision, environment);
    if (exactHitboxSchema)
    {
        out << ",\"bodySize\":{\"class\":" << AIPlayerBodySizeClass(visualPowerup)
            << ",\"source\":\"visualPowerupKind\"}";
        out << ",\"hitbox\":";
        WriteAIPlayerHitboxJson(out, hitbox);
        out << ",\"collisionSensors\":";
        WriteAICollisionSensorsJson(out, collisionMgr);
    }
    out << ",\"visual\":{\"powerupKind\":" << visualPowerup
        << ",\"sourceMask\":" << visualSource
        << ",\"fire\":" << (visualPowerup == 2 ? 1 : 0)
        << ",\"mini\":" << (visualPowerup == 3 ? 1 : 0)
        << ",\"shell\":" << (visualPowerup == 4 ? 1 : 0)
        << ",\"mega\":" << (visualPowerup == 5 ? 1 : 0)
        << ",\"starInvincible\":" << (starInvincible ? 1 : 0)
        << ",\"invincible\":" << (starInvincible || damageInvulnerable ? 1 : 0)
        << ",\"actorPowerupState\":" << actorPowerupState
        << ",\"actorPowerupFormState\":" << actorPowerupFormState
        << ",\"shellState\":" << shellState
        << "}"
        << ",\"battleStars\":" << v(sample.Player0BattleStars, sample.Player1BattleStars)
        << ",\"coins\":" << v(sample.Player0Coins, sample.Player1Coins)
        << ",\"dead\":" << v(sample.Player0Dead, sample.Player1Dead)
        << ",";
    WriteAIObservationV2TileSummaryJson(out, tileProbe, contactGround, contactWallLeft, contactWallRight);
    out << ",";
    WriteAIObservationV2TerrainJson(out, tileProbe);
    out << "}";
}

const AITileProbeSample* FindAITileProbePoint(const AIPlayerTileProbeSample& probe, const char* name)
{
    for (const AITileProbeSample& sample : probe.Samples)
    {
        if (std::strcmp(sample.Name, name) == 0)
            return &sample;
    }
    return nullptr;
}

const AITileGridSample* FindAITileGridCell(const AIPlayerTileProbeSample& probe, int row, int col)
{
    if (row < 0 || row >= kAITileGridHeight || col < 0 || col >= kAITileGridWidth)
        return nullptr;
    return &probe.Grid[row * kAITileGridWidth + col];
}

int AITileProbeSolidishValue(const AIPlayerTileProbeSample& probe, const char* name)
{
    const AITileProbeSample* sample = FindAITileProbePoint(probe, name);
    return sample && sample->Found && AITileBehaviorSolidish(sample->Behavior) ? 1 : 0;
}

bool AITileTypeFeature(melonDS::u32 tileType, const std::string& name, double& out);

bool AITileProbeSampleFeature(const AITileProbeSample& sample, const std::string& field, double& out)
{
    if (field == "found") out = sample.Found;
    else if (field == "status") out = sample.Status;
    else if (field == "tile_id") out = sample.TileID;
    else if (field == "behavior") out = sample.Behavior;
    else if (field == "solidish") out = sample.Found && AITileBehaviorSolidish(sample.Behavior) ? 1 : 0;
    else if (field == "pixel_x") out = sample.PixelX;
    else if (field == "pixel_y") out = sample.PixelY;
    else if (field.rfind("block_", 0) == 0)
    {
        const std::string blockField = field.substr(6);
        const int question = (sample.Behavior & 0x00040000u) ? 1 : 0;
        const int breakable = (sample.Behavior & 0x00080000u) ? 1 : 0;
        const int brick = (sample.Behavior & 0x00100000u) ? 1 : 0;
        const int invisible = (sample.Behavior & 0x20000000u) ? 1 : 0;
        const melonDS::u32 storage = sample.Behavior & 0x00000C3Fu;
        const int any = question || breakable || brick || invisible;
        const int storageBreakable = any && breakable && storage != 0 ? 1 : 0;
        if (blockField == "any") out = any;
        else if (blockField == "itemBox") out = any && storage != 0 ? 1 : 0;
        else if (blockField == "question") out = question;
        else if (blockField == "breakable") out = breakable;
        else if (blockField == "brick") out = brick;
        else if (blockField == "invisible") out = invisible;
        else if (blockField == "hasStorageContents") out = any && storage != 0 ? 1 : 0;
        else if (blockField == "storageContents") out = storage;
        else if (blockField == "modifier") out = (sample.Behavior & 0x0000F000u) >> 12;
        else if (blockField == "currentTileId") out = sample.TileID;
        else if (blockField == "currentBehavior") out = sample.Behavior;
        else if (blockField == "storageBreakableCandidate") out = storageBreakable;
        else if (blockField == "hiddenOrRescueCandidate") out = any && invisible ? 1 : 0;
        else if (blockField == "visibleStorageBreakableCandidate") out = storageBreakable && !invisible ? 1 : 0;
        else if (blockField == "visibleSolidCandidate") out = any && (question || brick || (breakable && !invisible)) ? 1 : 0;
        else return false;
    }
    else if (!AITileTypeFeature(sample.Behavior, field, out))
    {
        return false;
    }
    return true;
}

bool AITileGridCellShouldLog(const AITileGridSample& cell)
{
    const AITileProbeSample& tile = cell.Tile;
    if (!tile.Found)
        return true;
    const melonDS::u32 t = tile.Behavior;
    const bool meaningful =
        AITileBehaviorSolidish(t) ||
        (t & (0x00020000u | // coin
              0x00040000u | // question block
              0x00080000u | // breakable block
              0x00100000u | // brick block
              0x00200000u | // slope
              0x01000000u | // entrance
              0x02000000u | // water
              0x04000000u | // climbable
              0x08000000u | // partial solid
              0x10000000u | // harmful
              0x20000000u)) != 0; // invisible block
    return meaningful;
}

void WriteAIPlayerTileProbeJson(std::ostream& out,
                                const AIPlayerTileProbeSample& probe,
                                bool contactGround,
                                bool contactWallLeft,
                                bool contactWallRight)
{
    const int groundBelow = AITileProbeSolidishValue(probe, "below");
    const int aheadBody = AITileProbeSolidishValue(probe, "aheadBody");
    const int aheadFeet = AITileProbeSolidishValue(probe, "aheadFeet");
    const int aheadBelow = AITileProbeSolidishValue(probe, "aheadBelow");
    const int ahead2Below = AITileProbeSolidishValue(probe, "ahead2Below");
    const int leftBody = AITileProbeSolidishValue(probe, "leftBody");
    const int leftBelow = AITileProbeSolidishValue(probe, "leftBelow");
    const int left2Below = AITileProbeSolidishValue(probe, "left2Below");
    const int rightBody = AITileProbeSolidishValue(probe, "rightBody");
    const int rightBelow = AITileProbeSolidishValue(probe, "rightBelow");
    const int right2Below = AITileProbeSolidishValue(probe, "right2Below");
    const int wallAhead = aheadBody || aheadFeet ? 1 : 0;
    const int holeAhead = probe.Found && !aheadBelow && !ahead2Below ? 1 : 0;
    const int wallLeft = leftBody || contactWallLeft ? 1 : 0;
    const int holeLeft = probe.Found && !leftBelow && !left2Below ? 1 : 0;
    const int wallRight = rightBody || contactWallRight ? 1 : 0;
    const int holeRight = probe.Found && !rightBelow && !right2Below ? 1 : 0;
    const int effectiveGroundBelow = groundBelow || contactGround ? 1 : 0;
    const int suppressHoleByContact = contactGround && !groundBelow ? 1 : 0;
    const int ambiguousSideBody =
        contactGround && leftBody && rightBody && !contactWallLeft && !contactWallRight ? 1 : 0;
    const AITerrainDerivedSummary grid =
        DeriveAITerrainSummaryFromGrid(probe, contactGround, contactWallLeft, contactWallRight);
    const int blockedAhead = grid.BlockedAhead;
    const int blockedLeft = contactWallLeft || (leftBody && !ambiguousSideBody) ? 1 : 0;
    const int blockedRight = contactWallRight || (rightBody && !ambiguousSideBody) ? 1 : 0;
    out << "{\"found\":" << probe.Found
        << ",\"stageLayout\":";
    WriteJsonHex(out, probe.StageLayout);
    out << ",\"wrapX\":" << SignedU32(probe.WrapX)
        << ",\"direction\":" << SignedU32(probe.Direction)
        << ",\"summary\":{\"source\":\"terrain_grid\""
        << ",\"groundBelowSolid\":" << grid.GroundBelowSolid
        << ",\"aheadBodySolid\":" << aheadBody
        << ",\"aheadFeetSolid\":" << aheadFeet
        << ",\"aheadBelowSolid\":" << aheadBelow
        << ",\"ahead2BelowSolid\":" << ahead2Below
        << ",\"wallAhead\":" << wallAhead
        << ",\"holeAhead\":" << grid.HoleAhead
        << ",\"wallLeft\":" << wallLeft
        << ",\"holeLeft\":" << grid.HoleLeft
        << ",\"wallRight\":" << wallRight
        << ",\"holeRight\":" << grid.HoleRight
        << ",\"blockedAhead\":" << blockedAhead
        << ",\"blockedLeft\":" << grid.BlockedLeft
        << ",\"blockedRight\":" << grid.BlockedRight
        << ",\"probeHoleAhead\":" << holeAhead
        << ",\"probeHoleLeft\":" << holeLeft
        << ",\"probeHoleRight\":" << holeRight
        << ",\"probeBlockedLeft\":" << blockedLeft
        << ",\"probeBlockedRight\":" << blockedRight
        << ",\"probeGroundBelowSolid\":" << groundBelow
        << ",\"ambiguousSideBody\":" << ambiguousSideBody
        << ",\"contactGround\":" << (contactGround ? 1 : 0)
        << ",\"probeEffectiveGroundBelowSolid\":" << effectiveGroundBelow
        << ",\"effectiveGroundBelowSolid\":" << grid.EffectiveGroundBelowSolid
        << ",\"holeSuppressedByContact\":" << grid.HoleSuppressedByContact
        << ",\"probeHoleSuppressedByContact\":" << suppressHoleByContact
        << ",\"effectiveHoleAhead\":" << grid.EffectiveHoleAhead
        << ",\"effectiveHoleLeft\":" << grid.EffectiveHoleLeft
        << ",\"effectiveHoleRight\":" << grid.EffectiveHoleRight
        << "},\"samples\":[";
    for (int i = 0; i < kAITileProbeCount; i++)
    {
        if (i != 0)
            out << ",";
        WriteAITileProbePointJson(out, probe.Samples[i]);
    }
    int loggedGridCells = 0;
    for (int i = 0; i < kAITileGridCount; i++)
        if (AITileGridCellShouldLog(probe.Grid[i]))
            loggedGridCells++;
    out << "],\"grid\":{\"encoding\":\"sparse_non_empty\""
        << ",\"width\":" << kAITileGridWidth
        << ",\"height\":" << kAITileGridHeight
        << ",\"minRelTileX\":" << kAITileGridMinRelX
        << ",\"minRelTileY\":" << kAITileGridMinRelY
        << ",\"totalCells\":" << kAITileGridCount
        << ",\"loggedCells\":" << loggedGridCells
        << ",\"omittedCellFound\":1"
        << ",\"omittedCellStatus\":0"
        << ",\"cells\":[";
    bool firstGridCell = true;
    for (int i = 0; i < kAITileGridCount; i++)
    {
        if (!AITileGridCellShouldLog(probe.Grid[i]))
            continue;
        if (!firstGridCell)
            out << ",";
        firstGridCell = false;
        WriteAITileGridCellJson(out, probe.Grid[i]);
    }
    out << "]}}";
}

void WriteAIPlayerCollisionMgrJson(std::ostream& out, const PlayerCollisionMgrSample& collisionMgr)
{
    out << "{\"found\":" << collisionMgr.Found;
    if (!collisionMgr.Found)
    {
        out << "}";
        return;
    }
    out << ",\"base\":";
    WriteJsonHex(out, collisionMgr.Base);
    out << ",\"deltaX\":" << SignedU32(collisionMgr.DeltaX)
        << ",\"deltaY\":" << SignedU32(collisionMgr.DeltaY);
    out << ",\"collisionResult\":";
    WriteJsonHex(out, collisionMgr.CollisionResult);
    out << ",\"groundCollision\":";
    WriteJsonHex(out, collisionMgr.GroundCollision);
    out << ",\"attachedTileX\":" << collisionMgr.AttachedTileX
        << ",\"attachedTileY\":" << collisionMgr.AttachedTileY
        << ",\"bottomModifierTileType\":";
    WriteJsonHex(out, collisionMgr.BottomModifierTileType);
    out << ",\"bottomModifierTile\":";
    WriteAITileTypeJson(out, collisionMgr.BottomModifierTileType);
    out << ",\"topModifierTileType\":";
    WriteJsonHex(out, collisionMgr.TopModifierTileType);
    out << ",\"topModifierTile\":";
    WriteAITileTypeJson(out, collisionMgr.TopModifierTileType);
    out << ",\"sideModifierTileTypeLeft\":";
    WriteJsonHex(out, collisionMgr.SideModifierTileTypeLeft);
    out << ",\"sideModifierTileLeft\":";
    WriteAITileTypeJson(out, collisionMgr.SideModifierTileTypeLeft);
    out << ",\"sideModifierTileTypeRight\":";
    WriteJsonHex(out, collisionMgr.SideModifierTileTypeRight);
    out << ",\"sideModifierTileRight\":";
    WriteAITileTypeJson(out, collisionMgr.SideModifierTileTypeRight);
    out << ",\"bottomSlopeType\":" << static_cast<int>(static_cast<std::int8_t>(collisionMgr.BottomSlopeType))
        << ",\"topSlopeType\":" << static_cast<int>(static_cast<std::int8_t>(collisionMgr.TopSlopeType))
        << ",\"byteA4\":" << collisionMgr.ByteA4
        << ",\"byteA5\":" << collisionMgr.ByteA5
        << ",\"previousByteA4\":" << collisionMgr.PreviousByteA4
        << ",\"previousByteA5\":" << collisionMgr.PreviousByteA5
        << ",\"flagsA8\":" << collisionMgr.FlagsA8
        << ",\"tileByteAB\":" << static_cast<int>(static_cast<std::int8_t>(collisionMgr.TileByteAB))
        << ",\"modifierState\":" << collisionMgr.ModifierState
        << ",\"unknownB1\":" << static_cast<int>(static_cast<std::int8_t>(collisionMgr.UnknownB1))
        << ",\"sensors\":";
    WriteAICollisionSensorsJson(out, collisionMgr);
    out << "}";
}

void WriteAIPlayerTileDamageJson(std::ostream& out, melonDS::u32 flags, melonDS::u32 type)
{
    out << "{\"flags\":" << flags
        << ",\"type\":" << static_cast<int>(static_cast<std::int8_t>(type))
        << ",\"active\":" << (flags != 0 ? 1 : 0)
        << "}";
}

void WriteAIPlayerCameraJson(
    std::ostream& out,
    const GameStateSample& sample,
    melonDS::u32 x,
    melonDS::u32 y,
    melonDS::u32 velY)
{
    const std::int32_t screenY0 = SignedU32(y) - SignedU32(sample.StageCameraGlobalY0);
    const std::int32_t screenY1 = SignedU32(y) - SignedU32(sample.StageCameraGlobalY1);
    const std::int32_t bottomDistance0 =
        SignedU32(sample.StageCameraGlobalY0) + SignedU32(sample.StageCameraGlobalHeight0) - SignedU32(y);
    const std::int32_t bottomDistance1 =
        SignedU32(sample.StageCameraGlobalY1) + SignedU32(sample.StageCameraGlobalHeight1) - SignedU32(y);
    constexpr std::int32_t kNearBottomThreshold = 32 * 4096;

    out << "\"screen\":{";
    WriteAIScreenJson(
        out,
        "camera0",
        x,
        y,
        sample.StageCameraGlobalX0,
        sample.StageCameraGlobalY0,
        sample.StageCameraGlobalWidth0,
        sample.StageCameraGlobalHeight0);
    out << ",";
    WriteAIScreenJson(
        out,
        "camera1",
        x,
        y,
        sample.StageCameraGlobalX1,
        sample.StageCameraGlobalY1,
        sample.StageCameraGlobalWidth1,
        sample.StageCameraGlobalHeight1);
    out << "},\"fallRisk\":{\"screenY0\":" << screenY0
        << ",\"screenY1\":" << screenY1
        << ",\"cameraBottomDistance0\":" << bottomDistance0
        << ",\"cameraBottomDistance1\":" << bottomDistance1
        << ",\"nearCameraBottom0\":"
        << (bottomDistance0 >= 0 && bottomDistance0 <= kNearBottomThreshold ? 1 : 0)
        << ",\"nearCameraBottom1\":"
        << (bottomDistance1 >= 0 && bottomDistance1 <= kNearBottomThreshold ? 1 : 0)
        << ",\"belowCamera0\":" << (screenY0 > SignedU32(sample.StageCameraGlobalHeight0) ? 1 : 0)
        << ",\"belowCamera1\":" << (screenY1 > SignedU32(sample.StageCameraGlobalHeight1) ? 1 : 0)
        << ",\"velY\":" << SignedU32(velY)
        << ",\"velYPositive\":" << (SignedU32(velY) > 0 ? 1 : 0)
        << ",\"velYNegative\":" << (SignedU32(velY) < 0 ? 1 : 0)
        << "}";
}

void WriteAIPlayerJson(std::ostream& out, int index, const GameStateSample& sample)
{
    const bool p0 = index == 0;
    auto v = [p0](melonDS::u32 a, melonDS::u32 b) { return p0 ? a : b; };
    const melonDS::u32 collisionFlag = v(sample.PlayerActor0CollisionFlag, sample.PlayerActor1CollisionFlag);
    const melonDS::u32 environmentFlag = v(sample.PlayerActor0EnvironmentFlag, sample.PlayerActor1EnvironmentFlag);
    const melonDS::u32 posX = v(sample.PlayerActor0PosX, sample.PlayerActor1PosX);
    const melonDS::u32 posY = v(sample.PlayerActor0PosY, sample.PlayerActor1PosY);
    const melonDS::u32 velY = v(sample.PlayerActor0VelY, sample.PlayerActor1VelY);
    const PlayerCollisionMgrSample& collisionMgr =
        p0 ? sample.PlayerActor0CollisionMgr : sample.PlayerActor1CollisionMgr;
    const AIPlayerTileProbeSample& tileProbe =
        p0 ? sample.PlayerActor0TileProbe : sample.PlayerActor1TileProbe;
    const melonDS::u32 tileDamageFlags =
        p0 ? sample.PlayerActor0TileDamageFlags : sample.PlayerActor1TileDamageFlags;
    const melonDS::u32 tileDamageType =
        p0 ? sample.PlayerActor0TileDamageType : sample.PlayerActor1TileDamageType;
    const melonDS::u32 damageCooldown =
        p0 ? sample.PlayerActor0DamageCooldown : sample.PlayerActor1DamageCooldown;
    const melonDS::u32 damageState =
        p0 ? sample.PlayerActor0DamageState : sample.PlayerActor1DamageState;
    const melonDS::u32 powerupAuxState =
        p0 ? sample.PlayerActor0PowerupAuxState : sample.PlayerActor1PowerupAuxState;
    const melonDS::u32 powerupState =
        p0 ? sample.PlayerActor0PowerupState : sample.PlayerActor1PowerupState;
    const melonDS::u32 powerupFormState =
        p0 ? sample.PlayerActor0PowerupFormState : sample.PlayerActor1PowerupFormState;
    const melonDS::u32 powerupSubState =
        p0 ? sample.PlayerActor0PowerupSubState : sample.PlayerActor1PowerupSubState;
    const melonDS::u32 damageGuardFlag =
        p0 ? sample.PlayerActor0DamageGuardFlag : sample.PlayerActor1DamageGuardFlag;
    const melonDS::u32 powerupApplyLock =
        p0 ? sample.PlayerActor0PowerupApplyLock : sample.PlayerActor1PowerupApplyLock;
    const melonDS::u32 shellActorPtr =
        p0 ? sample.PlayerActor0ShellActorPtr : sample.PlayerActor1ShellActorPtr;
    const melonDS::u32 shellState =
        p0 ? sample.PlayerActor0ShellState : sample.PlayerActor1ShellState;
    const melonDS::u32 powerup = v(sample.Player0Powerup, sample.Player1Powerup);
    const melonDS::u32 inventoryPowerup = v(sample.Player0InventoryPowerup, sample.Player1InventoryPowerup);
    const melonDS::u32 damageGuardTimer =
        p0 ? sample.Player0DamageGuardTimer : sample.Player1DamageGuardTimer;
    const bool damagePhysicsGuard =
        (v(sample.PlayerActor0PhysicsFlag, sample.PlayerActor1PhysicsFlag) & 0x80000000u) != 0;
    const bool starInvincibleCandidate =
        AIStarInvincibleCandidate(inventoryPowerup, powerupState, powerupFormState, shellState);
    const bool damageInvulnerableCandidate =
        damageGuardTimer != 0 || damageCooldown != 0 || damageGuardFlag != 0 || damagePhysicsGuard;
    const bool invincibleCandidate =
        starInvincibleCandidate || damageInvulnerableCandidate;
    out << "{\"index\":" << index
        << ",\"found\":" << v(sample.PlayerActor0Found, sample.PlayerActor1Found)
        << ",\"guid\":";
    WriteJsonHex(out, v(sample.PlayerActor0GUID, sample.PlayerActor1GUID));
    out << ",\"base\":";
    WriteJsonHex(out, v(sample.PlayerActor0Base, sample.PlayerActor1Base));
    out << ",\"settings\":";
    WriteJsonHex(out, v(sample.PlayerActor0Settings, sample.PlayerActor1Settings));
    out << ",\"stateType\":" << v(sample.PlayerActor0StateType, sample.PlayerActor1StateType)
        << ",\"flags\":";
    WriteJsonHex(out, v(sample.PlayerActor0Flags, sample.PlayerActor1Flags));
    out << ",";
    WriteAIVec3Json(
        out,
        "pos",
        posX,
        posY,
        v(sample.PlayerActor0PosZ, sample.PlayerActor1PosZ));
    out << ",";
    WriteAIVec3Json(
        out,
        "prev",
        v(sample.PlayerActor0PrevX, sample.PlayerActor1PrevX),
        v(sample.PlayerActor0PrevY, sample.PlayerActor1PrevY),
        v(sample.PlayerActor0PrevZ, sample.PlayerActor1PrevZ));
    out << ",";
    WriteAIVec3Json(
        out,
        "vel",
        v(sample.PlayerActor0VelX, sample.PlayerActor1VelX),
        velY,
        v(sample.PlayerActor0VelZ, sample.PlayerActor1VelZ));
    out << ",";
    WriteAIPlayerCameraJson(out, sample, posX, posY, velY);
    out << ",\"actionFlag\":" << v(sample.PlayerActor0ActionFlag, sample.PlayerActor1ActionFlag)
        << ",\"subActionFlag\":" << v(sample.PlayerActor0SubActionFlag, sample.PlayerActor1SubActionFlag)
        << ",\"physicsFlag\":" << v(sample.PlayerActor0PhysicsFlag, sample.PlayerActor1PhysicsFlag)
        << ",\"transitionFlag\":" << v(sample.PlayerActor0TransitionFlag, sample.PlayerActor1TransitionFlag)
        << ",\"collisionFlag\":" << collisionFlag
        << ",\"environmentFlag\":" << environmentFlag
        << ",\"contact\":";
    WriteAIContactJson(out, collisionFlag, environmentFlag);
    out << ",\"collisionMgr\":";
    WriteAIPlayerCollisionMgrJson(out, collisionMgr);
    out << ",\"hitbox\":";
    WriteAIPlayerHitboxJson(out, p0 ? sample.PlayerActor0Hitbox : sample.PlayerActor1Hitbox);
    out << ",\"tileProbe\":";
    WriteAIPlayerTileProbeJson(
        out,
        tileProbe,
        AIPlayerContactGround(collisionFlag),
        (collisionFlag & (0x00000008u | 0x00000400u | 0x20000000u)) != 0,
        (collisionFlag & (0x00000010u | 0x00000800u | 0x40000000u)) != 0);
    out << ",\"tileDamage\":";
    WriteAIPlayerTileDamageJson(out, tileDamageFlags, tileDamageType);
    out
        << ",\"updateLocked\":" << v(sample.PlayerActor0UpdateLocked, sample.PlayerActor1UpdateLocked)
        << ",\"visible\":" << v(sample.PlayerActor0VisibleFlag, sample.PlayerActor1VisibleFlag)
        << ",\"defeated\":" << v(sample.PlayerActor0DefeatedFlag, sample.PlayerActor1DefeatedFlag)
        << ",\"transitioning\":" << v(sample.PlayerActor0TransitioningFlag, sample.PlayerActor1TransitioningFlag)
        << ",\"damageCooldown\":" << damageCooldown
        << ",\"powerup\":" << powerup
        << ",\"inventoryPowerup\":" << inventoryPowerup
        << ",\"visualState\":{";
    WriteAIPowerupCandidateJson(out, "powerup", powerup, shellState);
    out << ",";
    WriteAIPowerupCandidateJson(out, "inventoryPowerup", inventoryPowerup);
    const melonDS::u32 visualPowerupKind =
        AIVisualPowerupKindCandidate(powerup, inventoryPowerup, powerupState, powerupFormState, shellState);
    const melonDS::u32 visualPowerupSource =
        AIVisualPowerupSourceMask(powerup, inventoryPowerup, powerupState, powerupFormState, shellState);
    out << ",\"hasReserveItemCandidate\":"
        << (inventoryPowerup != 0 ? 1 : 0)
        << ",\"visualPowerupKindCandidate\":" << visualPowerupKind
        << ",\"visualPowerupSourceMask\":" << visualPowerupSource
        << ",\"isFireVisualCandidate\":" << (visualPowerupKind == 2 ? 1 : 0)
        << ",\"isMiniVisualCandidate\":" << (visualPowerupKind == 3 ? 1 : 0)
        << ",\"isMegaVisualCandidate\":" << (visualPowerupKind == 5 ? 1 : 0)
        << ",\"canShootFireVisualCandidate\":"
        << ((powerup == 2 || powerupState == 2 || powerupFormState == 2) ? 1 : 0)
        << ",\"actorPowerupState\":" << powerupState
        << ",\"actorPowerupFormState\":" << powerupFormState
        << ",\"actorPowerupAuxState\":" << powerupAuxState
        << ",\"actorPowerupSubState\":" << powerupSubState
        << ",\"damageState\":" << damageState
        << ",\"damageCooldown\":" << damageCooldown
        << ",\"damageGuardFlag\":" << damageGuardFlag
        << ",\"damageGuardTimer\":" << damageGuardTimer
        << ",\"damagePhysicsGuard\":" << (damagePhysicsGuard ? 1 : 0)
        << ",\"powerupApplyLock\":" << powerupApplyLock
        << ",\"shellState\":" << shellState
        << ",\"shellActorPtr\":";
    WriteJsonHex(out, shellActorPtr);
    out << ",\"invincibleKnown\":1"
        << ",\"invincibleCandidate\":" << (invincibleCandidate ? 1 : 0)
        << ",\"starInvincibleCandidate\":" << (starInvincibleCandidate ? 1 : 0)
        << ",\"damageInvulnerableCandidate\":" << (damageInvulnerableCandidate ? 1 : 0)
        << ",\"powerupMappingVerified\":0"
        << ",\"notes\":\"powerup enum names are tentative; damage guard timer and shell state are disassembly-backed\""
        << "}"
        << ",\"dead\":" << v(sample.Player0Dead, sample.Player1Dead)
        << ",\"lives\":" << v(sample.Player0Lives, sample.Player1Lives)
        << ",\"battleStars\":" << v(sample.Player0BattleStars, sample.Player1BattleStars)
        << ",\"coins\":" << v(sample.Player0Coins, sample.Player1Coins)
        << ",\"score\":" << v(sample.Player0Score, sample.Player1Score)
        << ",\"displayedStars\":" << v(sample.Player0DisplayedStars, sample.Player1DisplayedStars)
        << ",\"deaths\":" << v(sample.Player0Deaths, sample.Player1Deaths)
        << ",\"collectedStars\":" << v(sample.Player0CollectedStars, sample.Player1CollectedStars)
        << "}";
}

void WriteAIObjectJson(std::ostream& out, const GameStateObjectScanEntry& entry, const GameStateSample& sample)
{
    const char* category = AIObjectCategory(entry.ObjectID, entry.Actor.Settings);
    out << "{\"category\":\"" << AIObjectCategory(entry.ObjectID, entry.Actor.Settings)
        << "\",\"objectId\":";
    WriteJsonHex(out, entry.ObjectID, 3);
    out << ",\"settings\":";
    WriteJsonHex(out, entry.Actor.Settings);
    out << ",\"guid\":";
    WriteJsonHex(out, entry.Actor.GUID);
    out << ",\"base\":";
    WriteJsonHex(out, entry.Actor.Base);
    out << ",\"offset\":";
    WriteJsonHex(out, entry.Offset);
    out << ",\"vtable\":";
    WriteJsonHex(out, entry.VTable);
    out << ",\"lifecycle\":" << static_cast<unsigned>(entry.LifecycleState)
        << ",\"type\":" << static_cast<unsigned>(entry.Type)
        << ",\"skipFlags\":" << static_cast<unsigned>(entry.SkipFlags)
        << ",\"stateType\":" << entry.Actor.StateType
        << ",\"flags\":";
    WriteJsonHex(out, entry.Actor.Flags);
    out << ",";
    WriteAIVec3Json(out, "pos", entry.Actor.PosX, entry.Actor.PosY, entry.Actor.PosZ);
    out << ",";
    WriteAIVec3Json(out, "vel", entry.Actor.VelX, entry.Actor.VelY, entry.Actor.VelZ);
    out << ",\"relative\":{\"p0dx\":" << AIWrappedDeltaX(SignedU32(entry.Actor.PosX), SignedU32(sample.PlayerActor0PosX))
        << ",\"p0dy\":" << (SignedU32(entry.Actor.PosY) - SignedU32(sample.PlayerActor0PosY))
        << ",\"p1dx\":" << AIWrappedDeltaX(SignedU32(entry.Actor.PosX), SignedU32(sample.PlayerActor1PosX))
        << ",\"p1dy\":" << (SignedU32(entry.Actor.PosY) - SignedU32(sample.PlayerActor1PosY))
        << "}";
    out << ",\"screen\":{";
    WriteAIScreenJson(
        out,
        "camera0",
        entry.Actor.PosX,
        entry.Actor.PosY,
        sample.StageCameraGlobalX0,
        sample.StageCameraGlobalY0,
        sample.StageCameraGlobalWidth0,
        sample.StageCameraGlobalHeight0);
    out << ",";
    WriteAIScreenJson(
        out,
        "camera1",
        entry.Actor.PosX,
        entry.Actor.PosY,
        sample.StageCameraGlobalX1,
        sample.StageCameraGlobalY1,
        sample.StageCameraGlobalWidth1,
        sample.StageCameraGlobalHeight1);
    out << "},\"categoryMask\":" << AIObjectCategoryMask(category);
    out << "}";
}

void WriteAINearestObjectJson(
    std::ostream& out,
    const char* category,
    const GameStateObjectScanEntry* entry,
    melonDS::u32 selfX,
    melonDS::u32 selfY)
{
    out << "\"" << category << "\":{";
    if (!entry)
    {
        out << "\"found\":0}";
        return;
    }
    out << "\"found\":1,\"objectId\":";
    WriteJsonHex(out, entry->ObjectID, 3);
    out << ",\"settings\":";
    WriteJsonHex(out, entry->Actor.Settings);
    out << ",\"guid\":";
    WriteJsonHex(out, entry->Actor.GUID);
    out << ",\"dx\":" << AIWrappedDeltaX(SignedU32(entry->Actor.PosX), SignedU32(selfX))
        << ",\"dy\":" << (SignedU32(entry->Actor.PosY) - SignedU32(selfY))
        << ",\"dist2\":" << DistanceSquared2D(entry->Actor.PosX, entry->Actor.PosY, selfX, selfY)
        << "}";
}

void WriteAIVisualSummaryJson(
    std::ostream& out,
    const GameStateObjectScanCache& objectScanCache,
    const GameStateSample& sample)
{
    constexpr std::array<const char*, 12> categories {{
        "big_star_actor",
        "world_item",
        "neutral_item",
        "coin_item",
        "dropped_star_item",
        "item",
        "coin",
        "moving_hazard",
        "hazard",
        "enemy_goomba",
        "enemy_koopa",
        "platform",
    }};

    std::map<std::string, int> categoryCounts;
    int visibleCamera0 = 0;
    int visibleCamera1 = 0;
    int visibleCamera0X = 0;
    int visibleCamera1X = 0;
    for (const GameStateObjectScanEntry& entry : objectScanCache.Entries)
    {
        if (entry.LifecycleState != 1)
            continue;
        categoryCounts[AIObjectCategory(entry.ObjectID, entry.Actor.Settings)]++;
        if (IsInCameraRect(
                entry.Actor.PosX,
                entry.Actor.PosY,
                sample.StageCameraGlobalX0,
                sample.StageCameraGlobalY0,
                sample.StageCameraGlobalWidth0,
                sample.StageCameraGlobalHeight0))
            visibleCamera0++;
        const std::int64_t camera0X =
            (static_cast<std::int64_t>(SignedU32(entry.Actor.PosX)) - SignedU32(sample.StageCameraGlobalX0) +
                G.AI.Rule.HorizontalWrapWidth) % std::max(1, G.AI.Rule.HorizontalWrapWidth);
        if (camera0X >= 0 && camera0X < SignedU32(sample.StageCameraGlobalWidth0))
            visibleCamera0X++;
        if (IsInCameraRect(
                entry.Actor.PosX,
                entry.Actor.PosY,
                sample.StageCameraGlobalX1,
                sample.StageCameraGlobalY1,
                sample.StageCameraGlobalWidth1,
                sample.StageCameraGlobalHeight1))
            visibleCamera1++;
        const std::int64_t camera1X =
            (static_cast<std::int64_t>(SignedU32(entry.Actor.PosX)) - SignedU32(sample.StageCameraGlobalX1) +
                G.AI.Rule.HorizontalWrapWidth) % std::max(1, G.AI.Rule.HorizontalWrapWidth);
        if (camera1X >= 0 && camera1X < SignedU32(sample.StageCameraGlobalWidth1))
            visibleCamera1X++;
    }

    out << ",\"visualSummary\":{\"visibleCamera0\":" << visibleCamera0
        << ",\"visibleCamera1\":" << visibleCamera1
        << ",\"visibleCamera0X\":" << visibleCamera0X
        << ",\"visibleCamera1X\":" << visibleCamera1X
        << ",\"categoryCounts\":{";
    bool firstCount = true;
    for (const auto& [category, count] : categoryCounts)
    {
        if (!firstCount)
            out << ",";
        firstCount = false;
        out << "\"" << category << "\":" << count;
    }
    out << "},\"nearest\":[";

    const melonDS::u32 playerX[2] { sample.PlayerActor0PosX, sample.PlayerActor1PosX };
    const melonDS::u32 playerY[2] { sample.PlayerActor0PosY, sample.PlayerActor1PosY };
    for (int player = 0; player < 2; player++)
    {
        if (player != 0)
            out << ",";
        out << "{\"player\":" << player << ",\"categories\":{";
        for (std::size_t categoryIndex = 0; categoryIndex < categories.size(); categoryIndex++)
        {
            const char* category = categories[categoryIndex];
            const GameStateObjectScanEntry* nearest = nullptr;
            std::int64_t nearestDist2 = 0;
            for (const GameStateObjectScanEntry& entry : objectScanCache.Entries)
            {
                if (entry.LifecycleState != 1 ||
                    std::strcmp(AIObjectCategory(entry.ObjectID, entry.Actor.Settings), category) != 0)
                    continue;
                const std::int64_t dist2 =
                    DistanceSquared2D(entry.Actor.PosX, entry.Actor.PosY, playerX[player], playerY[player]);
                if (!nearest || dist2 < nearestDist2)
                {
                    nearest = &entry;
                    nearestDist2 = dist2;
                }
            }
            if (categoryIndex != 0)
                out << ",";
            WriteAINearestObjectJson(out, category, nearest, playerX[player], playerY[player]);
        }
        out << "}}";
    }
    out << "]}";
}

bool AITileTypeFeature(melonDS::u32 tileType, const std::string& name, double& out)
{
    auto bit = [tileType](melonDS::u32 mask) { return (tileType & mask) ? 1.0 : 0.0; };
    if (name == "solid") out = bit(0x00010000);
    else if (name == "coin") out = bit(0x00020000);
    else if (name == "questionBlock") out = bit(0x00040000);
    else if (name == "breakableBlock") out = bit(0x00080000);
    else if (name == "brickBlock") out = bit(0x00100000);
    else if (name == "slope") out = bit(0x00200000);
    else if (name == "ceilingSlope") out = bit(0x00400000);
    else if (name == "scanSolid") out = bit(0x00800000);
    else if (name == "entrance") out = bit(0x01000000);
    else if (name == "water") out = bit(0x02000000);
    else if (name == "climbable") out = bit(0x04000000);
    else if (name == "partialSolid") out = bit(0x08000000);
    else if (name == "harmful") out = bit(0x10000000);
    else if (name == "invisibleBlock") out = bit(0x20000000);
    else if (name == "solidOnBottom") out = bit(0x40000000);
    else if (name == "solidOnTop") out = bit(0x80000000);
    else if (name == "modifier") out = static_cast<double>((tileType & 0x0000F000u) >> 12);
    else if (name == "lowType") out = static_cast<double>(tileType & 0x000000FFu);
    else if (name == "storageContents") out = static_cast<double>(tileType & 0x00000C3Fu);
    else return false;
    return true;
}

bool AIContactFeature(melonDS::u32 collisionFlag, melonDS::u32 environmentFlag, const std::string& name, double& out)
{
    auto bit = [](melonDS::u32 value, melonDS::u32 mask) { return (value & mask) ? 1.0 : 0.0; };
    const double ground =
        bit(collisionFlag, 0x00000001) ||
        bit(collisionFlag, 0x00002000) ||
        bit(collisionFlag, 0x00008000) ||
        bit(collisionFlag, 0x08000000);
    const double wallLeft =
        bit(collisionFlag, 0x00000008) ||
        bit(collisionFlag, 0x00000400) ||
        bit(collisionFlag, 0x20000000);
    const double wallRight =
        bit(collisionFlag, 0x00000010) ||
        bit(collisionFlag, 0x00000800) ||
        bit(collisionFlag, 0x40000000);
    const double submerged =
        bit(collisionFlag, 0x00400000) ||
        bit(environmentFlag, 0x00000002) ||
        bit(environmentFlag, 0x00000200);
    if (name == "ground") out = ground;
    else if (name == "tileGround") out = bit(collisionFlag, 0x00000001);
    else if (name == "hoverTileGround") out = bit(collisionFlag, 0x00002000);
    else if (name == "colliderGround") out = bit(collisionFlag, 0x00008000);
    else if (name == "predictGround") out = bit(collisionFlag, 0x08000000);
    else if (name == "ceiling") out = bit(collisionFlag, 0x00000002);
    else if (name == "pushWall") out = bit(collisionFlag, 0x00000004);
    else if (name == "wallLeft") out = wallLeft;
    else if (name == "wallRight") out = wallRight;
    else if (name == "edgeGrab") out = bit(collisionFlag, 0x00001000);
    else if (name == "slipperyGround") out = bit(collisionFlag, 0x00004000);
    else if (name == "water") out = bit(collisionFlag, 0x00000020);
    else if (name == "liquid") out = bit(collisionFlag, 0x00400000);
    else if (name == "submerged") out = submerged;
    else if (name == "quicksandTop") out = bit(collisionFlag, 0x00010000);
    else if (name == "quicksand") out = bit(collisionFlag, 0x00020000);
    else if (name == "rope") out = bit(collisionFlag, 0x00040000);
    else if (name == "tightrope") out = bit(collisionFlag, 0x00800000);
    else if (name == "ledge") out = bit(collisionFlag, 0x01000000);
    else if (name == "pole") out = bit(collisionFlag, 0x10000000);
    else if (name == "spikesLeft") out = bit(collisionFlag, 0x20000000);
    else if (name == "spikesRight") out = bit(collisionFlag, 0x40000000);
    else if (name == "slowGround") out = bit(environmentFlag, 0x00000001);
    else if (name == "conveyorLeft") out = bit(environmentFlag, 0x00000008);
    else if (name == "conveyorRight") out = bit(environmentFlag, 0x00000010);
    else if (name == "snowyGround") out = bit(environmentFlag, 0x00000020);
    else if (name == "sandyGround") out = bit(environmentFlag, 0x00000040);
    else if (name == "destroyedGround") out = bit(environmentFlag, 0x00000100);
    else if (name == "climbableBottom") out = bit(environmentFlag, 0x00000400);
    else if (name == "climbableTop") out = bit(environmentFlag, 0x00000800);
    else if (name == "destroyedCeiling") out = bit(environmentFlag, 0x00001000);
    else if (name == "wrapLeft") out = bit(environmentFlag, 0x00002000);
    else if (name == "wrapRight") out = bit(environmentFlag, 0x00004000);
    else return false;
    return true;
}

bool RuntimePlayerFeature(
    const GameStateSample& sample,
    const std::string& name,
    int player,
    double& out)
{
    const bool p0 = player == 0;
    auto v = [p0](melonDS::u32 a, melonDS::u32 b) { return p0 ? a : b; };
    const melonDS::u32 found = v(sample.PlayerActor0Found, sample.PlayerActor1Found);
    const melonDS::u32 x = v(sample.PlayerActor0PosX, sample.PlayerActor1PosX);
    const melonDS::u32 y = v(sample.PlayerActor0PosY, sample.PlayerActor1PosY);
    const melonDS::u32 z = v(sample.PlayerActor0PosZ, sample.PlayerActor1PosZ);
    const melonDS::u32 vx = v(sample.PlayerActor0VelX, sample.PlayerActor1VelX);
    const melonDS::u32 vy = v(sample.PlayerActor0VelY, sample.PlayerActor1VelY);
    const melonDS::u32 vz = v(sample.PlayerActor0VelZ, sample.PlayerActor1VelZ);
    const melonDS::u32 collision = v(sample.PlayerActor0CollisionFlag, sample.PlayerActor1CollisionFlag);
    const melonDS::u32 environment = v(sample.PlayerActor0EnvironmentFlag, sample.PlayerActor1EnvironmentFlag);
    const melonDS::u32 powerup = v(sample.Player0Powerup, sample.Player1Powerup);
    const melonDS::u32 inventoryPowerup = v(sample.Player0InventoryPowerup, sample.Player1InventoryPowerup);
    const melonDS::u32 powerupState = v(sample.PlayerActor0PowerupState, sample.PlayerActor1PowerupState);
    const melonDS::u32 powerupFormState = v(sample.PlayerActor0PowerupFormState, sample.PlayerActor1PowerupFormState);
    const melonDS::u32 shellState = v(sample.PlayerActor0ShellState, sample.PlayerActor1ShellState);
    const melonDS::u32 visualPowerup =
        AIVisualPowerupKindCandidate(powerup, inventoryPowerup, powerupState, powerupFormState, shellState);
    const melonDS::u32 visualSource =
        AIVisualPowerupSourceMask(powerup, inventoryPowerup, powerupState, powerupFormState, shellState);
    const PlayerCollisionMgrSample& collisionMgr =
        p0 ? sample.PlayerActor0CollisionMgr : sample.PlayerActor1CollisionMgr;
    const AIPlayerTileProbeSample& tileProbe =
        p0 ? sample.PlayerActor0TileProbe : sample.PlayerActor1TileProbe;
    const melonDS::u32 tileDamageFlags =
        p0 ? sample.PlayerActor0TileDamageFlags : sample.PlayerActor1TileDamageFlags;
    const melonDS::u32 tileDamageType =
        p0 ? sample.PlayerActor0TileDamageType : sample.PlayerActor1TileDamageType;
    const melonDS::u32 damageCooldown =
        p0 ? sample.PlayerActor0DamageCooldown : sample.PlayerActor1DamageCooldown;
    const melonDS::u32 damageGuardTimer =
        p0 ? sample.Player0DamageGuardTimer : sample.Player1DamageGuardTimer;
    const melonDS::u32 damageGuardFlag =
        p0 ? sample.PlayerActor0DamageGuardFlag : sample.PlayerActor1DamageGuardFlag;
    const bool damagePhysicsGuard =
        (v(sample.PlayerActor0PhysicsFlag, sample.PlayerActor1PhysicsFlag) & 0x80000000u) != 0;
    const bool starInvincibleCandidate =
        AIStarInvincibleCandidate(inventoryPowerup, powerupState, powerupFormState, shellState);
    const bool damageInvulnerableCandidate =
        damageGuardTimer != 0 || damageCooldown != 0 || damageGuardFlag != 0 || damagePhysicsGuard;

    if (name == "found") out = found;
    else if (name == "x") out = SignedU32(x);
    else if (name == "y") out = SignedU32(y);
    else if (name == "z") out = SignedU32(z);
    else if (name == "vx") out = SignedU32(vx);
    else if (name == "vy") out = SignedU32(vy);
    else if (name == "vz") out = SignedU32(vz);
    else if (name == "action") out = v(sample.PlayerActor0ActionFlag, sample.PlayerActor1ActionFlag);
    else if (name == "sub_action") out = v(sample.PlayerActor0SubActionFlag, sample.PlayerActor1SubActionFlag);
    else if (name == "physics") out = v(sample.PlayerActor0PhysicsFlag, sample.PlayerActor1PhysicsFlag);
    else if (name == "collision") out = collision;
    else if (name == "environment") out = environment;
    else if (name == "powerup") out = powerup;
    else if (name == "inventory_powerup") out = inventoryPowerup;
    else if (name == "damage_cooldown") out = damageCooldown;
    else if (name == "has_reserve_item_candidate") out = inventoryPowerup != 0 ? 1 : 0;
    else if (name == "can_shoot_fire_candidate") out = powerup == 2 ? 1 : 0;
    else if (name == "visual_powerup_kind_candidate") out = visualPowerup;
    else if (name == "visual_powerup_source_mask") out = visualSource;
    else if (name == "is_fire_visual_candidate") out = visualPowerup == 2 ? 1 : 0;
    else if (name == "is_mini_visual_candidate") out = visualPowerup == 3 ? 1 : 0;
    else if (name == "is_mega_visual_candidate") out = visualPowerup == 5 ? 1 : 0;
    else if (name == "can_shoot_fire_visual_candidate") out = visualPowerup == 2 ? 1 : 0;
    else if (name == "is_mini_candidate") out = visualPowerup == 3 ? 1 : 0;
    else if (name == "is_shell_candidate") out = powerup == 4 || shellState != 0 ? 1 : 0;
    else if (name == "is_mega_candidate") out = visualPowerup == 5 ? 1 : 0;
    else if (name == "actor_powerup_state") out = powerupState;
    else if (name == "actor_powerup_form_state") out = powerupFormState;
    else if (name == "actor_powerup_aux_state") out = v(sample.PlayerActor0PowerupAuxState, sample.PlayerActor1PowerupAuxState);
    else if (name == "actor_powerup_sub_state") out = v(sample.PlayerActor0PowerupSubState, sample.PlayerActor1PowerupSubState);
    else if (name == "damage_state") out = v(sample.PlayerActor0DamageState, sample.PlayerActor1DamageState);
    else if (name == "damage_guard_flag") out = damageGuardFlag;
    else if (name == "damage_guard_timer") out = damageGuardTimer;
    else if (name == "damage_physics_guard") out = damagePhysicsGuard ? 1 : 0;
    else if (name == "powerup_apply_lock") out = v(sample.PlayerActor0PowerupApplyLock, sample.PlayerActor1PowerupApplyLock);
    else if (name == "shell_state") out = shellState;
    else if (name == "invincible_known") out = 1;
    else if (name == "invincible_candidate") out =
        starInvincibleCandidate || damageInvulnerableCandidate ? 1 : 0;
    else if (name == "star_invincible_candidate") out = starInvincibleCandidate ? 1 : 0;
    else if (name == "dead") out = v(sample.Player0Dead, sample.Player1Dead);
    else if (name == "battle_stars") out = v(sample.Player0BattleStars, sample.Player1BattleStars);
    else if (name == "coins") out = v(sample.Player0Coins, sample.Player1Coins);
    else if (name == "screen0_x")
        out = AIWrappedDeltaX(SignedU32(x), SignedU32(sample.StageCameraGlobalX0));
    else if (name == "screen0_y")
        out = SignedU32(y) - SignedU32(sample.StageCameraGlobalY0);
    else if (name == "screen0_in_view_x")
        out = IsInCameraRect(x, sample.StageCameraGlobalY0, sample.StageCameraGlobalX0, sample.StageCameraGlobalY0, sample.StageCameraGlobalWidth0, sample.StageCameraGlobalHeight0) ? 1 : 0;
    else if (name == "screen0_in_view_y")
        out = (SignedU32(y) - SignedU32(sample.StageCameraGlobalY0)) >= 0 &&
            (SignedU32(y) - SignedU32(sample.StageCameraGlobalY0)) < SignedU32(sample.StageCameraGlobalHeight0) ? 1 : 0;
    else if (name == "screen0_in_view")
        out = IsInCameraRect(x, y, sample.StageCameraGlobalX0, sample.StageCameraGlobalY0, sample.StageCameraGlobalWidth0, sample.StageCameraGlobalHeight0) ? 1 : 0;
    else if (name == "screen1_x")
        out = AIWrappedDeltaX(SignedU32(x), SignedU32(sample.StageCameraGlobalX1));
    else if (name == "screen1_y")
        out = SignedU32(y) - SignedU32(sample.StageCameraGlobalY1);
    else if (name == "screen1_in_view_x")
        out = IsInCameraRect(x, sample.StageCameraGlobalY1, sample.StageCameraGlobalX1, sample.StageCameraGlobalY1, sample.StageCameraGlobalWidth1, sample.StageCameraGlobalHeight1) ? 1 : 0;
    else if (name == "screen1_in_view_y")
        out = (SignedU32(y) - SignedU32(sample.StageCameraGlobalY1)) >= 0 &&
            (SignedU32(y) - SignedU32(sample.StageCameraGlobalY1)) < SignedU32(sample.StageCameraGlobalHeight1) ? 1 : 0;
    else if (name == "screen1_in_view")
        out = IsInCameraRect(x, y, sample.StageCameraGlobalX1, sample.StageCameraGlobalY1, sample.StageCameraGlobalWidth1, sample.StageCameraGlobalHeight1) ? 1 : 0;
    else if (name == "camera_bottom_distance0")
        out = SignedU32(sample.StageCameraGlobalY0) + SignedU32(sample.StageCameraGlobalHeight0) - SignedU32(y);
    else if (name == "camera_bottom_distance1")
        out = SignedU32(sample.StageCameraGlobalY1) + SignedU32(sample.StageCameraGlobalHeight1) - SignedU32(y);
    else if (name == "near_camera_bottom0")
    {
        const int d = SignedU32(sample.StageCameraGlobalY0) + SignedU32(sample.StageCameraGlobalHeight0) - SignedU32(y);
        out = d >= 0 && d <= 32 * 4096 ? 1 : 0;
    }
    else if (name == "near_camera_bottom1")
    {
        const int d = SignedU32(sample.StageCameraGlobalY1) + SignedU32(sample.StageCameraGlobalHeight1) - SignedU32(y);
        out = d >= 0 && d <= 32 * 4096 ? 1 : 0;
    }
    else if (name == "below_camera0")
        out = (SignedU32(y) - SignedU32(sample.StageCameraGlobalY0)) > SignedU32(sample.StageCameraGlobalHeight0) ? 1 : 0;
    else if (name == "below_camera1")
        out = (SignedU32(y) - SignedU32(sample.StageCameraGlobalY1)) > SignedU32(sample.StageCameraGlobalHeight1) ? 1 : 0;
    else if (name == "vel_y_positive") out = SignedU32(vy) > 0 ? 1 : 0;
    else if (name == "vel_y_negative") out = SignedU32(vy) < 0 ? 1 : 0;
    else if (name.rfind("contact_", 0) == 0)
        return AIContactFeature(collision, environment, name.substr(8), out);
    else if (name == "collision_mgr_found") out = collisionMgr.Found;
    else if (name == "collision_mgr_collision_result") out = collisionMgr.CollisionResult;
    else if (name == "collision_mgr_ground_collision") out = collisionMgr.GroundCollision;
    else if (name == "collision_mgr_delta_x") out = SignedU32(collisionMgr.DeltaX);
    else if (name == "collision_mgr_delta_y") out = SignedU32(collisionMgr.DeltaY);
    else if (name == "collision_mgr_bottom_modifier_tile_type") out = collisionMgr.BottomModifierTileType;
    else if (name == "collision_mgr_bottom_modifier_tile_sane") out = 1;
    else if (name == "collision_mgr_attached_tile_x") out = collisionMgr.AttachedTileX;
    else if (name == "collision_mgr_attached_tile_y") out = collisionMgr.AttachedTileY;
    else if (name == "collision_mgr_top_modifier_tile_type") out = collisionMgr.TopModifierTileType;
    else if (name == "collision_mgr_side_modifier_tile_type_left") out = collisionMgr.SideModifierTileTypeLeft;
    else if (name == "collision_mgr_side_modifier_tile_type_right") out = collisionMgr.SideModifierTileTypeRight;
    else if (name == "collision_mgr_bottom_slope_type") out = static_cast<int>(static_cast<std::int8_t>(collisionMgr.BottomSlopeType));
    else if (name == "collision_mgr_top_slope_type") out = static_cast<int>(static_cast<std::int8_t>(collisionMgr.TopSlopeType));
    else if (name == "collision_mgr_flags_a8") out = collisionMgr.FlagsA8;
    else if (name == "collision_mgr_tile_byte_ab") out = static_cast<int>(static_cast<std::int8_t>(collisionMgr.TileByteAB));
    else if (name == "collision_mgr_modifier_state") out = collisionMgr.ModifierState;
    else if (name.rfind("bottom_modifier_tile_", 0) == 0)
        return AITileTypeFeature(collisionMgr.BottomModifierTileType, name.substr(21), out);
    else if (name == "tile_damage_flags") out = tileDamageFlags;
    else if (name == "tile_damage_type") out = static_cast<int>(static_cast<std::int8_t>(tileDamageType));
    else if (name == "tile_damage_active") out = tileDamageFlags != 0 ? 1 : 0;
    else if (name == "tile_probe_found") out = tileProbe.Found;
    else if (name == "tile_probe_direction") out = SignedU32(tileProbe.Direction);
    else if (name.rfind("tile_probe_", 0) == 0)
    {
        const std::string suffix = name.substr(11);
        const bool contactGround = AIPlayerContactGround(collision);
        const int aheadBody = AITileProbeSolidishValue(tileProbe, "aheadBody");
        const int aheadFeet = AITileProbeSolidishValue(tileProbe, "aheadFeet");
        const int aheadBelow = AITileProbeSolidishValue(tileProbe, "aheadBelow");
        const int ahead2Below = AITileProbeSolidishValue(tileProbe, "ahead2Below");
        const int leftBody = AITileProbeSolidishValue(tileProbe, "leftBody");
        const int leftBelow = AITileProbeSolidishValue(tileProbe, "leftBelow");
        const int left2Below = AITileProbeSolidishValue(tileProbe, "left2Below");
        const int rightBody = AITileProbeSolidishValue(tileProbe, "rightBody");
        const int rightBelow = AITileProbeSolidishValue(tileProbe, "rightBelow");
        const int right2Below = AITileProbeSolidishValue(tileProbe, "right2Below");
        const bool contactWallLeft = (collision & (0x00000008u | 0x00000400u | 0x20000000u)) != 0;
        const bool contactWallRight = (collision & (0x00000010u | 0x00000800u | 0x40000000u)) != 0;
        const bool ambiguousSideBody = contactGround && leftBody && rightBody && !contactWallLeft && !contactWallRight;
        const AITerrainDerivedSummary grid =
            DeriveAITerrainSummaryFromGrid(tileProbe, contactGround, contactWallLeft, contactWallRight);
        if (suffix == "groundBelowSolid") out = grid.GroundBelowSolid;
        else if (suffix == "aheadBodySolid") out = aheadBody;
        else if (suffix == "aheadFeetSolid") out = aheadFeet;
        else if (suffix == "aheadBelowSolid") out = aheadBelow;
        else if (suffix == "ahead2BelowSolid") out = ahead2Below;
        else if (suffix == "leftBodySolid") out = leftBody;
        else if (suffix == "leftBelowSolid") out = leftBelow;
        else if (suffix == "left2BelowSolid") out = left2Below;
        else if (suffix == "rightBodySolid") out = rightBody;
        else if (suffix == "rightBelowSolid") out = rightBelow;
        else if (suffix == "right2BelowSolid") out = right2Below;
        else if (suffix == "blockedAhead" || suffix == "wallAhead") out = grid.BlockedAhead;
        else if (suffix == "holeAhead") out = grid.HoleAhead;
        else if (suffix == "blockedLeft" || suffix == "wallLeft") out = grid.BlockedLeft;
        else if (suffix == "holeLeft") out = grid.HoleLeft;
        else if (suffix == "blockedRight" || suffix == "wallRight") out = grid.BlockedRight;
        else if (suffix == "holeRight") out = grid.HoleRight;
        else if (suffix == "contactGround") out = contactGround ? 1 : 0;
        else if (suffix == "ambiguousSideBody") out = ambiguousSideBody ? 1 : 0;
        else if (suffix == "effectiveGroundBelowSolid") out = grid.EffectiveGroundBelowSolid;
        else if (suffix == "holeSuppressedByContact") out = grid.HoleSuppressedByContact;
        else if (suffix == "effectiveHoleAhead") out = grid.EffectiveHoleAhead;
        else if (suffix == "effectiveHoleLeft") out = grid.EffectiveHoleLeft;
        else if (suffix == "effectiveHoleRight") out = grid.EffectiveHoleRight;
        else
        {
            if (suffix.rfind("grid_r", 0) == 0)
            {
                const std::size_t cpos = suffix.find("_c", 6);
                if (cpos == std::string::npos)
                    return false;
                const std::size_t fieldPos = suffix.find('_', cpos + 2);
                if (fieldPos == std::string::npos)
                    return false;
                const int row = std::atoi(suffix.substr(6, cpos - 6).c_str());
                const int col = std::atoi(suffix.substr(cpos + 2, fieldPos - (cpos + 2)).c_str());
                const std::string field = suffix.substr(fieldPos + 1);
                const AITileGridSample* gridCell = FindAITileGridCell(tileProbe, row, col);
                if (!gridCell)
                    return false;
                if (field == "row") out = SignedU32(gridCell->Row);
                else if (field == "col") out = SignedU32(gridCell->Col);
                else if (field == "rel_tile_x") out = SignedU32(gridCell->RelTileX);
                else if (field == "rel_tile_y") out = SignedU32(gridCell->RelTileY);
                else if (field == "tile_x") out = SignedU32(gridCell->TileX);
                else if (field == "tile_y") out = SignedU32(gridCell->TileY);
                else return AITileProbeSampleFeature(gridCell->Tile, field, out);
                return true;
            }
            const std::size_t pos = suffix.find('_');
            if (pos == std::string::npos)
                return false;
            const std::string sampleName = suffix.substr(0, pos);
            const std::string field = suffix.substr(pos + 1);
            const AITileProbeSample* probeSample = FindAITileProbePoint(tileProbe, sampleName.c_str());
            if (!probeSample)
                return false;
            if (!AITileProbeSampleFeature(*probeSample, field, out))
                return false;
        }
    }
    else return false;
    return true;
}

const GameStateObjectScanEntry* NearestRuntimeObject(
    const GameStateObjectScanCache& objectScanCache,
    const char* category,
    melonDS::u32 selfX,
    melonDS::u32 selfY)
{
    const GameStateObjectScanEntry* nearest = nullptr;
    std::int64_t nearestDist2 = 0;
    for (const GameStateObjectScanEntry& entry : objectScanCache.Entries)
    {
        if (entry.LifecycleState != 1 ||
            std::strcmp(AIObjectCategory(entry.ObjectID, entry.Actor.Settings), category) != 0)
            continue;
        const std::int64_t dist2 = DistanceSquared2D(entry.Actor.PosX, entry.Actor.PosY, selfX, selfY);
        if (!nearest || dist2 < nearestDist2)
        {
            nearest = &entry;
            nearestDist2 = dist2;
        }
    }
    return nearest;
}

bool RuntimeObjectFeature(
    const GameStateObjectScanCache& objectScanCache,
    const std::string& name,
    const std::string& prefix,
    melonDS::u32 selfX,
    melonDS::u32 selfY,
    double& out)
{
    if (name.rfind(prefix, 0) != 0)
        return false;
    const std::string category = name.substr(prefix.size());
    const std::size_t fieldPos = category.rfind('_');
    if (fieldPos == std::string::npos)
        return false;
    const std::string categoryName = category.substr(0, fieldPos);
    const std::string field = category.substr(fieldPos + 1);
    const GameStateObjectScanEntry* nearest =
        NearestRuntimeObject(objectScanCache, categoryName.c_str(), selfX, selfY);
    if (field == "found") out = nearest ? 1 : 0;
    else if (!nearest) out = 0;
    else if (field == "dx") out = AIWrappedDeltaX(SignedU32(nearest->Actor.PosX), SignedU32(selfX));
    else if (field == "dy") out = SignedU32(nearest->Actor.PosY) - SignedU32(selfY);
    else if (field == "dist") out = static_cast<double>(std::llround(std::sqrt(
        static_cast<double>(DistanceSquared2D(nearest->Actor.PosX, nearest->Actor.PosY, selfX, selfY)))));
    else return false;
    return true;
}

bool RuntimeHazardFeature(
    const GameStateObjectScanCache& objectScanCache,
    const std::string& name,
    melonDS::u32 selfX,
    melonDS::u32 selfY,
    melonDS::u32 selfVelX,
    double& out)
{
    constexpr std::int64_t kFeatureHazardHorizontalRange = 0x40000;
    constexpr std::int64_t kFeatureHazardVerticalRange = 0x50000;
    constexpr std::int64_t kFeatureHazardCloseRange = 0x30000;
    constexpr const char* prefix = "runtime_hazard_";
    if (name.rfind(prefix, 0) != 0)
        return false;
    const std::string field = name.substr(std::strlen(prefix));
    const RuntimeHazardThreat threat = MostDangerousRuntimeHazard(
        objectScanCache,
        selfX,
        selfY,
        selfVelX,
        kFeatureHazardHorizontalRange,
        kFeatureHazardVerticalRange,
        kFeatureHazardCloseRange);

    if (field == "found") out = threat.Found ? 1 : 0;
    else if (!threat.Found) out = 0;
    else if (field == "closing") out = threat.Closing ? 1 : 0;
    else if (field == "very_close") out = threat.VeryClose ? 1 : 0;
    else if (field == "dx") out = threat.Dx;
    else if (field == "dy") out = threat.Dy;
    else if (field == "vx") out = threat.VelX;
    else if (field == "vy") out = threat.VelY;
    else if (field == "dist") out = static_cast<double>(std::llround(std::sqrt(
        static_cast<double>(threat.Dx * threat.Dx + threat.Dy * threat.Dy))));
    else if (field == "category") out = threat.CategoryID;
    else if (field == "object_id") out = threat.ObjectID;
    else if (field == "settings") out = threat.Settings;
    else return false;
    return true;
}

bool ApplyImitationAIHazardGuard(
    const GameStateSample& sample,
    const GameStateObjectScanCache& objectScanCache,
    int player,
    melonDS::u32& held,
    std::int64_t& outHazardDx,
    std::int64_t& outHazardDy)
{
    if (!G.AI.Imitation.HazardGuardEnabled || player < 0 || player > 1)
        return false;

    const bool p0 = player == 0;
    const melonDS::u32 found = p0 ? sample.PlayerActor0Found : sample.PlayerActor1Found;
    if (!found)
        return false;

    const melonDS::u32 selfX = p0 ? sample.PlayerActor0PosX : sample.PlayerActor1PosX;
    const melonDS::u32 selfY = p0 ? sample.PlayerActor0PosY : sample.PlayerActor1PosY;
    const melonDS::u32 selfVelX = p0 ? sample.PlayerActor0VelX : sample.PlayerActor1VelX;
    const melonDS::u32 collisionFlag = p0 ? sample.PlayerActor0CollisionFlag : sample.PlayerActor1CollisionFlag;
    const AIPlayerTileProbeSample& tileProbe = p0 ? sample.PlayerActor0TileProbe : sample.PlayerActor1TileProbe;
    outHazardDx = 0;
    outHazardDy = 0;
    const RuntimeHazardThreat threat = MostDangerousRuntimeHazard(
        objectScanCache,
        selfX,
        selfY,
        selfVelX,
        G.AI.Imitation.HazardGuardHorizontalRange,
        G.AI.Imitation.HazardGuardVerticalRange,
        G.AI.Imitation.HazardGuardCloseRange);
    if (!threat.Found)
        return false;
    outHazardDx = threat.Dx;
    outHazardDy = threat.Dy;
    if (!threat.Closing && !threat.VeryClose)
        return false;

    auto abs64 = [](std::int64_t value) {
        return value < 0 ? -value : value;
    };
    constexpr melonDS::u32 kHeldA = 1u << 0;
    constexpr melonDS::u32 kHeldRight = 1u << 4;
    constexpr melonDS::u32 kHeldLeft = 1u << 5;
    const melonDS::u32 before = held;
    const bool contactGround = AIPlayerContactGround(collisionFlag);
    const bool contactWallLeft = (collisionFlag & (0x00000008u | 0x00000400u | 0x20000000u)) != 0;
    const bool contactWallRight = (collisionFlag & (0x00000010u | 0x00000800u | 0x40000000u)) != 0;
    const AITerrainDerivedSummary terrainSummary =
        DeriveAITerrainSummaryFromGrid(tileProbe, contactGround, contactWallLeft, contactWallRight);
    const bool blockedLeft = terrainSummary.BlockedLeft != 0;
    const bool blockedRight = terrainSummary.BlockedRight != 0;
    const bool pushWall = (collisionFlag & 0x00000004u) != 0;
    const bool hazardOnLeft = outHazardDx < 0;
    const bool escapeBlocked = hazardOnLeft ? (blockedRight || pushWall) : (blockedLeft || pushWall);
    const bool close = abs64(outHazardDx) <= G.AI.Imitation.HazardGuardCloseRange;
    const bool movingTowardHazard =
        (hazardOnLeft && (held & kHeldLeft) != 0) ||
        (!hazardOnLeft && (held & kHeldRight) != 0);

    held |= kHeldA;
    if (close || movingTowardHazard)
    {
        if (hazardOnLeft)
            held &= ~kHeldLeft;
        else
            held &= ~kHeldRight;
    }
    if (!escapeBlocked)
    {
        if (hazardOnLeft)
        {
            held |= kHeldRight;
            held &= ~kHeldLeft;
        }
        else
        {
            held |= kHeldLeft;
            held &= ~kHeldRight;
        }
    }
    else
    {
        held &= ~(kHeldLeft | kHeldRight);
    }

    return held != before;
}

bool IsRuntimeItemCategory(const char* category)
{
    return std::strcmp(category, "world_item") == 0 ||
        std::strcmp(category, "neutral_item") == 0 ||
        std::strcmp(category, "coin_item") == 0 ||
        std::strcmp(category, "dropped_star_item") == 0 ||
        std::strcmp(category, "item") == 0;
}

enum RuntimeItemKindCandidate
{
    kRuntimeItemKindUnknown = 0,
    kRuntimeItemKindSuperMushroom = 1,
    kRuntimeItemKindFireFlower = 2,
    kRuntimeItemKindMiniMushroom = 3,
    kRuntimeItemKindShell = 4,
    kRuntimeItemKindMegaMushroom = 5,
    kRuntimeItemKindInvincibleStar = 6,
    kRuntimeItemKindCoin = 7,
    kRuntimeItemKindDroppedBattleStar = 8,
    kRuntimeItemKindUnknownItemVariant = 9,
};

enum RuntimeItemKindConfidence
{
    kRuntimeItemKindConfidenceNone = 0,
    kRuntimeItemKindConfidenceHeuristic = 1,
    kRuntimeItemKindConfidenceLogSupported = 2,
    kRuntimeItemKindConfidenceConfirmed = 3,
};

bool RuntimeItemSettingsIsFireConfirmed(melonDS::u32 settings)
{
    return settings == 0x00011089u;
}

bool RuntimeItemSettingsIsFireCandidate(melonDS::u32 settings)
{
    return RuntimeItemSettingsIsFireConfirmed(settings);
}

bool RuntimeItemSettingsIsContextualPowerup(melonDS::u32 settings)
{
    return settings == 0x00090000u;
}

bool RuntimeItemSettingsIsSuperMushroomCandidate(melonDS::u32 settings)
{
    return settings == 0x00011088u;
}

bool RuntimeItemSettingsIsMiniMushroomCandidate(melonDS::u32 settings)
{
    return settings == 0x00011099u;
}

bool RuntimeItemSettingsIsShellCandidate(melonDS::u32 settings)
{
    return settings == 0x0001108Bu;
}

bool RuntimeItemSettingsIsMegaMushroomCandidate(melonDS::u32 settings)
{
    return settings == 0x00011085u;
}

bool RuntimeItemSettingsIsInvincibleStarCandidate(melonDS::u32 settings)
{
    return settings == 0x00011081u;
}

bool RuntimeItemSettingsIsCoinItemCandidate(melonDS::u32 settings)
{
    return settings == 0x00090002u;
}

bool RuntimeItemSettingsIsUnknownItemVariantCandidate(melonDS::u32 settings)
{
    return settings == 0x000D0000u || settings == 0x000D0002u;
}

int RuntimeContextualPowerupItemKind(melonDS::u32 currentPowerupKind)
{
    if (currentPowerupKind == kRuntimeItemKindUnknown)
        return kRuntimeItemKindSuperMushroom;
    if (currentPowerupKind == kRuntimeItemKindSuperMushroom ||
        currentPowerupKind == kRuntimeItemKindFireFlower)
        return kRuntimeItemKindFireFlower;
    return kRuntimeItemKindUnknownItemVariant;
}

int RuntimeItemPowerupKindCandidate(melonDS::u32 settings, melonDS::u32 currentPowerupKind)
{
    if (RuntimeItemSettingsIsContextualPowerup(settings))
    {
        const int kind = RuntimeContextualPowerupItemKind(currentPowerupKind);
        return kind == kRuntimeItemKindSuperMushroom || kind == kRuntimeItemKindFireFlower ? kind : -1;
    }
    if (RuntimeItemSettingsIsSuperMushroomCandidate(settings))
        return kRuntimeItemKindSuperMushroom;
    if (RuntimeItemSettingsIsFireCandidate(settings))
        return kRuntimeItemKindFireFlower;
    if (RuntimeItemSettingsIsMiniMushroomCandidate(settings))
        return kRuntimeItemKindMiniMushroom;
    if (RuntimeItemSettingsIsShellCandidate(settings))
        return kRuntimeItemKindShell;
    if (RuntimeItemSettingsIsMegaMushroomCandidate(settings))
        return kRuntimeItemKindMegaMushroom;
    return -1;
}

std::pair<int, int> RuntimeItemKindAndConfidence(
    const GameStateObjectScanEntry& item,
    const char* category,
    melonDS::u32 currentPowerupKind)
{
    const melonDS::u32 settings = item.Actor.Settings;
    if (std::strcmp(category, "coin_item") == 0 ||
        (item.ObjectID == 0x001Fu && RuntimeItemSettingsIsCoinItemCandidate(settings)))
        return {kRuntimeItemKindCoin, kRuntimeItemKindConfidenceConfirmed};
    if (std::strcmp(category, "dropped_star_item") == 0 ||
        (item.ObjectID == kVsBattleStarActorObjectID && IsVsDroppedStarActorSettings(settings)))
        return {kRuntimeItemKindDroppedBattleStar, kRuntimeItemKindConfidenceConfirmed};
    if (RuntimeItemSettingsIsFireConfirmed(settings))
        return {kRuntimeItemKindFireFlower, kRuntimeItemKindConfidenceConfirmed};
    if (RuntimeItemSettingsIsContextualPowerup(settings))
        return {RuntimeContextualPowerupItemKind(currentPowerupKind), kRuntimeItemKindConfidenceConfirmed};
    if (RuntimeItemSettingsIsFireCandidate(settings))
        return {kRuntimeItemKindFireFlower, kRuntimeItemKindConfidenceLogSupported};
    if (RuntimeItemSettingsIsSuperMushroomCandidate(settings))
        return {kRuntimeItemKindSuperMushroom, kRuntimeItemKindConfidenceHeuristic};
    if (RuntimeItemSettingsIsMiniMushroomCandidate(settings))
        return {kRuntimeItemKindMiniMushroom, kRuntimeItemKindConfidenceConfirmed};
    if (RuntimeItemSettingsIsShellCandidate(settings))
        return {kRuntimeItemKindShell, kRuntimeItemKindConfidenceConfirmed};
    if (RuntimeItemSettingsIsMegaMushroomCandidate(settings))
        return {kRuntimeItemKindMegaMushroom, kRuntimeItemKindConfidenceConfirmed};
    if (RuntimeItemSettingsIsInvincibleStarCandidate(settings))
        return {kRuntimeItemKindInvincibleStar, kRuntimeItemKindConfidenceConfirmed};
    if (RuntimeItemSettingsIsUnknownItemVariantCandidate(settings))
        return {kRuntimeItemKindUnknownItemVariant, kRuntimeItemKindConfidenceHeuristic};
    return {kRuntimeItemKindUnknown, kRuntimeItemKindConfidenceNone};
}

const GameStateObjectScanEntry* NearestRuntimeItem(
    const GameStateObjectScanCache& objectScanCache,
    melonDS::u32 selfX,
    melonDS::u32 selfY,
    bool requirePlainItem)
{
    const GameStateObjectScanEntry* nearest = nullptr;
    std::int64_t nearestDist2 = 0;
    for (const GameStateObjectScanEntry& entry : objectScanCache.Entries)
    {
        if (entry.LifecycleState != 1)
            continue;
        const char* category = AIObjectCategory(entry.ObjectID, entry.Actor.Settings);
        if (!IsRuntimeItemCategory(category))
            continue;
        if (requirePlainItem && std::strcmp(category, "item") != 0)
            continue;
        const std::int64_t dist2 = DistanceSquared2D(entry.Actor.PosX, entry.Actor.PosY, selfX, selfY);
        if (!nearest || dist2 < nearestDist2)
        {
            nearest = &entry;
            nearestDist2 = dist2;
        }
    }
    return nearest;
}

bool RuntimeItemFeature(
    const GameStateObjectScanCache& objectScanCache,
    const GameStateSample& sample,
    const std::string& name,
    const std::string& prefix,
    melonDS::u32 selfX,
    melonDS::u32 selfY,
    melonDS::u32 currentPowerupKind,
    bool requirePlainItem,
    bool forceZero,
    double& out)
{
    if (name.rfind(prefix, 0) != 0)
        return false;
    const std::string field = name.substr(prefix.size());
    const GameStateObjectScanEntry* item =
        forceZero ? nullptr : NearestRuntimeItem(objectScanCache, selfX, selfY, requirePlainItem);
    if (field == "found") out = item ? 1 : 0;
    else if (!item) out = field == "powerup_kind_candidate" ? -1 : 0;
    else if (field == "dx") out = AIWrappedDeltaX(SignedU32(item->Actor.PosX), SignedU32(selfX));
    else if (field == "dy") out = SignedU32(item->Actor.PosY) - SignedU32(selfY);
    else if (field == "dist") out = static_cast<double>(std::llround(std::sqrt(
        static_cast<double>(DistanceSquared2D(item->Actor.PosX, item->Actor.PosY, selfX, selfY)))));
    else if (field == "object_id") out = item->ObjectID;
    else if (field == "settings") out = item->Actor.Settings;
    else if (field == "settings_low8") out = item->Actor.Settings & 0xFFu;
    else if (field == "vtable") out = item->VTable;
    else if (field == "vx") out = SignedU32(item->Actor.VelX);
    else if (field == "vy") out = SignedU32(item->Actor.VelY);
    else if (field == "screen1_x") out = AIWrappedDeltaX(SignedU32(item->Actor.PosX), SignedU32(sample.StageCameraGlobalX1));
    else if (field == "screen1_y") out = SignedU32(item->Actor.PosY) - SignedU32(sample.StageCameraGlobalY1);
    else if (field == "screen1_in_view") out =
        IsInCameraRect(item->Actor.PosX, item->Actor.PosY, sample.StageCameraGlobalX1, sample.StageCameraGlobalY1, sample.StageCameraGlobalWidth1, sample.StageCameraGlobalHeight1) ? 1 : 0;
    else if (field == "kind_candidate")
    {
        const char* category = AIObjectCategory(item->ObjectID, item->Actor.Settings);
        out = RuntimeItemKindAndConfidence(*item, category, currentPowerupKind).first;
    }
    else if (field == "kind_confidence")
    {
        const char* category = AIObjectCategory(item->ObjectID, item->Actor.Settings);
        out = RuntimeItemKindAndConfidence(*item, category, currentPowerupKind).second;
    }
    else if (field == "powerup_kind_candidate") out = RuntimeItemPowerupKindCandidate(item->Actor.Settings, currentPowerupKind);
    else if (field == "is_super_mushroom_candidate")
    {
        const char* category = AIObjectCategory(item->ObjectID, item->Actor.Settings);
        out = RuntimeItemKindAndConfidence(*item, category, currentPowerupKind).first == kRuntimeItemKindSuperMushroom ? 1 : 0;
    }
    else if (field == "is_fire_candidate")
    {
        const char* category = AIObjectCategory(item->ObjectID, item->Actor.Settings);
        out = RuntimeItemKindAndConfidence(*item, category, currentPowerupKind).first == kRuntimeItemKindFireFlower ? 1 : 0;
    }
    else if (field == "is_fire_flower_candidate")
    {
        const char* category = AIObjectCategory(item->ObjectID, item->Actor.Settings);
        out = RuntimeItemKindAndConfidence(*item, category, currentPowerupKind).first == kRuntimeItemKindFireFlower ? 1 : 0;
    }
    else if (field == "is_coin_item_candidate") out = RuntimeItemSettingsIsCoinItemCandidate(item->Actor.Settings) ? 1 : 0;
    else if (field == "is_dropped_star_candidate") out =
        item->ObjectID == kVsBattleStarActorObjectID && IsVsDroppedStarActorSettings(item->Actor.Settings) ? 1 : 0;
    else if (field == "is_suspected_mini_candidate") out = RuntimeItemSettingsIsMiniMushroomCandidate(item->Actor.Settings) ? 1 : 0;
    else if (field == "is_mini_mushroom_candidate") out = RuntimeItemSettingsIsMiniMushroomCandidate(item->Actor.Settings) ? 1 : 0;
    else if (field == "is_shell_candidate") out = RuntimeItemSettingsIsShellCandidate(item->Actor.Settings) ? 1 : 0;
    else if (field == "is_mega_mushroom_candidate") out = RuntimeItemSettingsIsMegaMushroomCandidate(item->Actor.Settings) ? 1 : 0;
    else if (field == "is_invincible_star_candidate") out = RuntimeItemSettingsIsInvincibleStarCandidate(item->Actor.Settings) ? 1 : 0;
    else if (field == "is_unknown_item_variant_candidate") out = RuntimeItemSettingsIsUnknownItemVariantCandidate(item->Actor.Settings) ? 1 : 0;
    else if (field == "avoid_candidate") out = RuntimeItemSettingsIsMiniMushroomCandidate(item->Actor.Settings) ? 1 : 0;
    else return false;
    return true;
}

bool RuntimeFeatureValue(
    const GameStateSample& sample,
    const GameStateObjectScanCache& objectScanCache,
    int instanceID,
    melonDS::u32 frame,
    bool inGameplay,
    int player,
    const std::string& name,
    double& out)
{
    const int opponent = player ^ 1;
    const melonDS::u32 selfX = player == 0 ? sample.PlayerActor0PosX : sample.PlayerActor1PosX;
    const melonDS::u32 selfY = player == 0 ? sample.PlayerActor0PosY : sample.PlayerActor1PosY;
    const melonDS::u32 selfVelX = player == 0 ? sample.PlayerActor0VelX : sample.PlayerActor1VelX;
    const melonDS::u32 selfPowerup = player == 0 ? sample.Player0Powerup : sample.Player1Powerup;
    const melonDS::u32 selfInventoryPowerup = player == 0 ? sample.Player0InventoryPowerup : sample.Player1InventoryPowerup;
    const melonDS::u32 selfActorPowerupState =
        player == 0 ? sample.PlayerActor0PowerupState : sample.PlayerActor1PowerupState;
    const melonDS::u32 selfActorPowerupFormState =
        player == 0 ? sample.PlayerActor0PowerupFormState : sample.PlayerActor1PowerupFormState;
    const melonDS::u32 selfShellState = player == 0 ? sample.PlayerActor0ShellState : sample.PlayerActor1ShellState;
    const melonDS::u32 selfVisualPowerup = AIVisualPowerupKindCandidate(
        selfPowerup, selfInventoryPowerup, selfActorPowerupState, selfActorPowerupFormState, selfShellState);

    if (name == "frame") out = frame;
    else if (name == "stage_id") out = sample.StageID;
    else if (name == "stage_group") out = sample.StageGroup;
    else if (name == "player") out = player;
    else if (name == "label_source") out = 2;
    else if (name == "in_gameplay") out = inGameplay ? 1 : 0;
    else if (name == "self_prev_coins") out = -1;
    else if (name == "self_coin_reward_recent") out = 0;
    else if (name == "self_coin_reward_age") out = -1;
    else if (name.rfind("self_", 0) == 0)
        return RuntimePlayerFeature(sample, name.substr(5), player, out);
    else if (name.rfind("opponent_", 0) == 0)
        return RuntimePlayerFeature(sample, name.substr(9), opponent, out);
    else if (name == "target_found") out = sample.VsStarActorFound ? 1 : (sample.VsStarFound ? 1 : 0);
    else if (name == "target_dx")
    {
        const melonDS::u32 targetX = sample.VsStarActorFound ? sample.VsStarActorPosX : sample.VsStarPosX;
        out = AIWrappedDeltaX(SignedU32(targetX), SignedU32(selfX));
    }
    else if (name == "target_dy")
    {
        const melonDS::u32 targetY = sample.VsStarActorFound ? sample.VsStarActorPosY : sample.VsStarPosY;
        out = SignedU32(targetY) - SignedU32(selfY);
    }
    else if (name == "target_dz")
    {
        const melonDS::u32 selfZ = player == 0 ? sample.PlayerActor0PosZ : sample.PlayerActor1PosZ;
        const melonDS::u32 targetZ = sample.VsStarActorFound ? sample.VsStarActorPosZ : sample.VsStarPosZ;
        out = SignedU32(targetZ) - SignedU32(selfZ);
    }
    else if (name == "camera_x0") out = SignedU32(sample.StageCameraGlobalX0);
    else if (name == "camera_y0") out = SignedU32(sample.StageCameraGlobalY0);
    else if (name == "camera_width0") out = SignedU32(sample.StageCameraGlobalWidth0);
    else if (name == "camera_height0") out = SignedU32(sample.StageCameraGlobalHeight0);
    else if (name == "visible_camera0" || name == "visible_camera1" ||
             name == "visible_camera0_x" || name == "visible_camera1_x")
    {
        int visible0 = 0, visible1 = 0, visible0x = 0, visible1x = 0;
        for (const GameStateObjectScanEntry& entry : objectScanCache.Entries)
        {
            if (entry.LifecycleState != 1)
                continue;
            if (IsInCameraRect(entry.Actor.PosX, entry.Actor.PosY, sample.StageCameraGlobalX0, sample.StageCameraGlobalY0, sample.StageCameraGlobalWidth0, sample.StageCameraGlobalHeight0))
                visible0++;
            const std::int64_t camera0X = (static_cast<std::int64_t>(SignedU32(entry.Actor.PosX)) - SignedU32(sample.StageCameraGlobalX0) + G.AI.Rule.HorizontalWrapWidth) % std::max(1, G.AI.Rule.HorizontalWrapWidth);
            if (camera0X >= 0 && camera0X < SignedU32(sample.StageCameraGlobalWidth0))
                visible0x++;
            if (IsInCameraRect(entry.Actor.PosX, entry.Actor.PosY, sample.StageCameraGlobalX1, sample.StageCameraGlobalY1, sample.StageCameraGlobalWidth1, sample.StageCameraGlobalHeight1))
                visible1++;
            const std::int64_t camera1X = (static_cast<std::int64_t>(SignedU32(entry.Actor.PosX)) - SignedU32(sample.StageCameraGlobalX1) + G.AI.Rule.HorizontalWrapWidth) % std::max(1, G.AI.Rule.HorizontalWrapWidth);
            if (camera1X >= 0 && camera1X < SignedU32(sample.StageCameraGlobalWidth1))
                visible1x++;
        }
        if (name == "visible_camera0") out = visible0;
        else if (name == "visible_camera1") out = visible1;
        else if (name == "visible_camera0_x") out = visible0x;
        else out = visible1x;
    }
    else if (name == "object_total") out = sample.ObjectScanTotal;
    else if (name == "object_active") out = sample.ObjectActiveCount;
    else if (name == "object_dead") out = sample.ObjectDeadCount;
    else if (name == "fireballs_active") out = sample.FireballsActiveCount;
    else if (name == "fireballs_active_slots")
    {
        int count = 0;
        for (int i = 0; i < kAIFireballSlotCount; i++)
            count += sample.FireballSlotActive[i] ? 1 : 0;
        out = count;
    }
    else if (name == "fireballs_slot_count") out = kAIFireballSlotCount;
    else if (name == "fireballs_handler_word0") out = sample.FireballsHandlerWords[0];
    else if (name == "projectiles_handler_word0") out = sample.ProjectilesHandlerWords[0];
    else if (RuntimeItemFeature(
        objectScanCache, sample, name, "nearest_item_", selfX, selfY, selfVisualPowerup, false, false, out))
    {
    }
    else if (RuntimeItemFeature(
        objectScanCache, sample, name, "coin_reward_item_", selfX, selfY, selfVisualPowerup, true, true, out))
    {
    }
    else if (RuntimeHazardFeature(objectScanCache, name, selfX, selfY, selfVelX, out))
    {
    }
    else if (name.rfind("count_", 0) == 0)
    {
        const std::string category = name.substr(6);
        int count = 0;
        for (const GameStateObjectScanEntry& entry : objectScanCache.Entries)
            if (entry.LifecycleState == 1 && category == AIObjectCategory(entry.ObjectID, entry.Actor.Settings))
                count++;
        out = count;
    }
    else if (name.rfind("nearest_fireball_", 0) == 0)
    {
        const std::string field = name.substr(17);
        int best = -1;
        std::int64_t bestDist2 = 0;
        for (int i = 0; i < kAIFireballSlotCount; i++)
        {
            if (!sample.FireballSlotActive[i])
                continue;
            const std::int64_t dx = AIWrappedDeltaX(SignedU32(sample.FireballSlotPosX[i]), SignedU32(selfX));
            const std::int64_t dy = SignedU32(sample.FireballSlotPosY[i]) - SignedU32(selfY);
            const std::int64_t dist2 = dx * dx + dy * dy;
            if (best < 0 || dist2 < bestDist2)
            {
                best = i;
                bestDist2 = dist2;
            }
        }
        if (field == "found") out = best >= 0 ? 1 : 0;
        else if (best < 0) out = (field == "owner_candidate" || field == "stateless_owner_candidate") ? -1 : 0;
        else
        {
            int confidence = 0, heuristic = 0, statelessOwner = -1, statelessConfidence = 0, statelessHeuristic = 0;
            bool tracked = false;
            const int owner = AIFireballOwnerCandidate(instanceID, sample, best, confidence, heuristic, statelessOwner, statelessConfidence, statelessHeuristic, tracked);
            if (field == "dx") out = AIWrappedDeltaX(SignedU32(sample.FireballSlotPosX[best]), SignedU32(selfX));
            else if (field == "dy") out = SignedU32(sample.FireballSlotPosY[best]) - SignedU32(selfY);
            else if (field == "dist2") out = bestDist2;
            else if (field == "dist") out = static_cast<double>(std::llround(std::sqrt(static_cast<double>(bestDist2))));
            else if (field == "kind") out = sample.FireballSlotKind[best];
            else if (field == "state") out = sample.FireballSlotState[best];
            else if (field == "facing") out = sample.FireballSlotFacing[best];
            else if (field == "owner_candidate") out = owner;
            else if (field == "owner_confidence") out = confidence;
            else if (field == "owner_heuristic") out = heuristic;
            else if (field == "owned_by_self_candidate") out = owner == player ? 1 : 0;
            else if (field == "owner_tracked") out = tracked ? 1 : 0;
            else if (field == "stateless_owner_candidate") out = statelessOwner;
            else if (field == "stateless_owner_confidence") out = statelessConfidence;
            else if (field == "stateless_owner_heuristic") out = statelessHeuristic;
            else if (field == "state_byte82") out = sample.FireballSlotStateBytes[best][2];
            else if (field == "state_byte84") out = sample.FireballSlotStateBytes[best][4];
            else if (field == "state_byte86") out = sample.FireballSlotStateBytes[best][6];
            else if (field == "debug_word0") out = sample.FireballSlotDebugWords[best][0];
            else if (field == "owner_verified") out = sample.FireballSlotKind[best] <= 3 ? 1 : 0;
            else if (field == "source_kind") out = sample.FireballSlotKind[best];
            else return false;
        }
    }
    else if (RuntimeObjectFeature(objectScanCache, name, "nearest_", selfX, selfY, out))
    {
    }
    else
    {
        return false;
    }
    return true;
}

bool BuildRuntimeImitationFeatures(
    const NsmbImitationAI::LinearPolicyModel& model,
    const GameStateSample& sample,
    const GameStateObjectScanCache& objectScanCache,
    int instanceID,
    melonDS::u32 frame,
    bool inGameplay,
    int player,
    std::vector<double>& features,
    int& filled,
    int& missing)
{
    features.assign(model.FeatureCount(), 0.0);
    filled = 0;
    missing = 0;
    std::unordered_set<std::string> missingNames;
    for (std::size_t i = 0; i < model.FeatureNames.size(); i++)
    {
        double value = 0.0;
        if (RuntimeFeatureValue(sample, objectScanCache, instanceID, frame, inGameplay, player, model.FeatureNames[i], value))
        {
            features[i] = value;
            filled++;
        }
        else
        {
            missing++;
            if (missingNames.size() < 12)
                missingNames.insert(model.FeatureNames[i]);
        }
    }
    if (G.AI.Imitation.WarnMissingFeatures && missing > 0 &&
        !G.ImitationAI.HasFeatureCoverage())
    {
        std::printf(
            "NSMB ImitationAI: feature coverage filled=%d missing=%d missingExamples=",
            filled,
            missing);
        bool first = true;
        for (const std::string& name : missingNames)
        {
            std::printf("%s%s", first ? "" : "|", name.c_str());
            first = false;
        }
        std::printf("\n");
    }
    G.ImitationAI.RecordFeatureCoverage(filled, missing);
    return filled > 0;
}

constexpr int kAICompactRuntimeLegacyScalarCount = 35;
constexpr int kAICompactRuntimeScalarCount = 47;
constexpr int kAICompactRuntimeTerrainChannels = 16;
constexpr int kAICompactRuntimeEntityFeatures = 14;

void AppendAICompactRuntimeScalars(
    std::vector<double>& features,
    const GameStateSample& sample,
    const GameStateObjectScanCache& objectScanCache,
    int player,
    int scalarCount)
{
    const bool p0 = player == 0;
    const int opponent = player ^ 1;
    auto v = [p0](melonDS::u32 a, melonDS::u32 b) { return p0 ? a : b; };
    auto ov = [opponent](melonDS::u32 a, melonDS::u32 b) { return opponent == 0 ? a : b; };
    const melonDS::u32 selfX = v(sample.PlayerActor0PosX, sample.PlayerActor1PosX);
    const melonDS::u32 selfY = v(sample.PlayerActor0PosY, sample.PlayerActor1PosY);
    const melonDS::u32 selfVelX = v(sample.PlayerActor0VelX, sample.PlayerActor1VelX);
    const melonDS::u32 selfVelY = v(sample.PlayerActor0VelY, sample.PlayerActor1VelY);
    const melonDS::u32 selfPowerup = v(sample.Player0Powerup, sample.Player1Powerup);
    const melonDS::u32 selfInventoryPowerup = v(sample.Player0InventoryPowerup, sample.Player1InventoryPowerup);
    const melonDS::u32 selfActorPowerupState = p0 ? sample.PlayerActor0PowerupState : sample.PlayerActor1PowerupState;
    const melonDS::u32 selfActorPowerupFormState = p0 ? sample.PlayerActor0PowerupFormState : sample.PlayerActor1PowerupFormState;
    const melonDS::u32 selfShellState = p0 ? sample.PlayerActor0ShellState : sample.PlayerActor1ShellState;
    const melonDS::u32 selfVisualPowerup = AIVisualPowerupKindCandidate(
        selfPowerup, selfInventoryPowerup, selfActorPowerupState, selfActorPowerupFormState, selfShellState);
    const melonDS::u32 opponentVisualPowerup = AIVisualPowerupKindCandidate(
        ov(sample.Player0Powerup, sample.Player1Powerup),
        ov(sample.Player0InventoryPowerup, sample.Player1InventoryPowerup),
        opponent == 0 ? sample.PlayerActor0PowerupState : sample.PlayerActor1PowerupState,
        opponent == 0 ? sample.PlayerActor0PowerupFormState : sample.PlayerActor1PowerupFormState,
        opponent == 0 ? sample.PlayerActor0ShellState : sample.PlayerActor1ShellState);
    const PlayerHitboxSample& selfHitbox = p0 ? sample.PlayerActor0Hitbox : sample.PlayerActor1Hitbox;
    const PlayerHitboxSample& opponentHitbox = p0 ? sample.PlayerActor1Hitbox : sample.PlayerActor0Hitbox;
    const AIPlayerTileProbeSample& tileProbe = p0 ? sample.PlayerActor0TileProbe : sample.PlayerActor1TileProbe;
    const melonDS::u32 collision = v(sample.PlayerActor0CollisionFlag, sample.PlayerActor1CollisionFlag);
    const bool contactGround = AIPlayerContactGround(collision);
    const bool contactWallLeft = (collision & (0x00000008u | 0x00000400u | 0x20000000u)) != 0;
    const bool contactWallRight = (collision & (0x00000010u | 0x00000800u | 0x40000000u)) != 0;
    const AITerrainDerivedSummary terrainSummary =
        DeriveAITerrainSummaryFromGrid(tileProbe, contactGround, contactWallLeft, contactWallRight);
    const RuntimeHazardThreat hazard = MostDangerousRuntimeHazard(
        objectScanCache,
        selfX,
        selfY,
        selfVelX,
        0x40000,
        0x50000,
        0x30000);
    const GameStateObjectScanEntry* nearestItem = NearestRuntimeItem(objectScanCache, selfX, selfY, false);
    int nearestItemKind = 0;
    int nearestItemAvoid = 0;
    std::int64_t nearestItemDx = 0;
    std::int64_t nearestItemDy = 0;
    if (nearestItem)
    {
        const char* itemCategory = AIObjectCategory(nearestItem->ObjectID, nearestItem->Actor.Settings);
        nearestItemKind = RuntimeItemKindAndConfidence(*nearestItem, itemCategory, selfVisualPowerup).first;
        nearestItemAvoid = RuntimeItemSettingsIsMiniMushroomCandidate(nearestItem->Actor.Settings) ? 1 : 0;
        nearestItemDx = AIWrappedDeltaX(SignedU32(nearestItem->Actor.PosX), SignedU32(selfX));
        nearestItemDy = SignedU32(nearestItem->Actor.PosY) - SignedU32(selfY);
    }
    const bool selfStarInvincible = AIStarInvincibleCandidate(selfInventoryPowerup, selfActorPowerupState, selfActorPowerupFormState, selfShellState);

    const double legacyValues[kAICompactRuntimeLegacyScalarCount] = {
        static_cast<double>(sample.StageID),
        static_cast<double>(sample.StageGroup),
        static_cast<double>(SignedU32(selfX)),
        static_cast<double>(SignedU32(selfY)),
        static_cast<double>(SignedU32(selfVelX)),
        static_cast<double>(SignedU32(selfVelY)),
        static_cast<double>(selfVisualPowerup),
        selfStarInvincible ? 1.0 : 0.0,
        selfStarInvincible ? 1.0 : 0.0,
        static_cast<double>(v(sample.Player0BattleStars, sample.Player1BattleStars)),
        static_cast<double>(v(sample.Player0Coins, sample.Player1Coins)),
        static_cast<double>(AIWrappedDeltaX(SignedU32(ov(sample.PlayerActor0PosX, sample.PlayerActor1PosX)), SignedU32(selfX))),
        static_cast<double>(SignedU32(ov(sample.PlayerActor0PosY, sample.PlayerActor1PosY)) - SignedU32(selfY)),
        static_cast<double>(opponentVisualPowerup),
        static_cast<double>(ov(sample.Player0BattleStars, sample.Player1BattleStars)),
        static_cast<double>(sample.VsStarActorFound),
        static_cast<double>(AIWrappedDeltaX(SignedU32(sample.VsStarActorPosX), SignedU32(selfX))),
        static_cast<double>(SignedU32(sample.VsStarActorPosY) - SignedU32(selfY)),
        nearestItem ? 1.0 : 0.0,
        static_cast<double>(nearestItemDx),
        static_cast<double>(nearestItemDy),
        static_cast<double>(nearestItemKind),
        static_cast<double>(nearestItemAvoid),
        hazard.Found ? 1.0 : 0.0,
        static_cast<double>(hazard.Dx),
        static_cast<double>(hazard.Dy),
        hazard.Closing ? 1.0 : 0.0,
        static_cast<double>(hazard.CategoryID),
        static_cast<double>(terrainSummary.GroundBelowSolid),
        static_cast<double>(terrainSummary.BlockedAhead),
        static_cast<double>(terrainSummary.BlockedLeft),
        static_cast<double>(terrainSummary.BlockedRight),
        static_cast<double>(terrainSummary.EffectiveHoleAhead),
        static_cast<double>(terrainSummary.EffectiveHoleLeft),
        static_cast<double>(terrainSummary.EffectiveHoleRight),
    };
    if (scalarCount == kAICompactRuntimeLegacyScalarCount)
    {
        features.insert(features.end(), std::begin(legacyValues), std::end(legacyValues));
        return;
    }

    // Keep the serialized scalar order: size follows the corresponding powerup kind.
    features.insert(features.end(), std::begin(legacyValues), std::begin(legacyValues) + 7);
    features.push_back(static_cast<double>(AIPlayerBodySizeClass(selfVisualPowerup)));
    if (scalarCount == kAICompactRuntimeScalarCount)
    {
        features.push_back(static_cast<double>(selfHitbox.Found));
        features.push_back(static_cast<double>(SignedU32(selfHitbox.CenterOffsetX)));
        features.push_back(static_cast<double>(SignedU32(selfHitbox.CenterOffsetY)));
        features.push_back(static_cast<double>(SignedU32(selfHitbox.HalfWidth)));
        features.push_back(static_cast<double>(SignedU32(selfHitbox.HalfHeight)));
    }
    features.insert(features.end(), std::begin(legacyValues) + 7, std::begin(legacyValues) + 14);
    features.push_back(static_cast<double>(AIPlayerBodySizeClass(opponentVisualPowerup)));
    if (scalarCount == kAICompactRuntimeScalarCount)
    {
        features.push_back(static_cast<double>(opponentHitbox.Found));
        features.push_back(static_cast<double>(SignedU32(opponentHitbox.CenterOffsetX)));
        features.push_back(static_cast<double>(SignedU32(opponentHitbox.CenterOffsetY)));
        features.push_back(static_cast<double>(SignedU32(opponentHitbox.HalfWidth)));
        features.push_back(static_cast<double>(SignedU32(opponentHitbox.HalfHeight)));
    }
    features.insert(features.end(), std::begin(legacyValues) + 14, std::end(legacyValues));
}

void AppendAICompactRuntimeTerrain(std::vector<double>& features, const AIPlayerTileProbeSample& probe)
{
    for (int row = 0; row < kAITileGridHeight; row++)
    {
        for (int col = 0; col < kAITileGridWidth; col++)
        {
            const AITileGridSample& cell = probe.Grid[row * kAITileGridWidth + col];
            const melonDS::u32 mask = probe.Found ? AIObservationV2TerrainMask(cell) : 0;
            for (int channel = 0; channel < kAICompactRuntimeTerrainChannels; channel++)
                features.push_back((mask & (1u << channel)) ? 1.0 : 0.0);
        }
    }
}

struct AICompactRuntimeEntity
{
    double Values[kAICompactRuntimeEntityFeatures] {};
    std::int64_t Dist2 = 0;
};

void AppendAICompactRuntimeEntities(
    std::vector<double>& features,
    const GameStateSample& sample,
    const GameStateObjectScanCache& objectScanCache,
    int instanceID,
    int player,
    int maxEntities)
{
    std::vector<AICompactRuntimeEntity> entities;
    entities.reserve(static_cast<std::size_t>(std::max(0, maxEntities)) + kAIFireballSlotCount);
    const melonDS::u32 selfX = player == 0 ? sample.PlayerActor0PosX : sample.PlayerActor1PosX;
    const melonDS::u32 selfY = player == 0 ? sample.PlayerActor0PosY : sample.PlayerActor1PosY;
    const melonDS::u32 visualPowerup = AIVisualPowerupKindCandidate(
        player == 0 ? sample.Player0Powerup : sample.Player1Powerup,
        player == 0 ? sample.Player0InventoryPowerup : sample.Player1InventoryPowerup,
        player == 0 ? sample.PlayerActor0PowerupState : sample.PlayerActor1PowerupState,
        player == 0 ? sample.PlayerActor0PowerupFormState : sample.PlayerActor1PowerupFormState,
        player == 0 ? sample.PlayerActor0ShellState : sample.PlayerActor1ShellState);

    int writtenObjects = 0;
    for (const GameStateObjectScanEntry& entry : objectScanCache.Entries)
    {
        if (entry.LifecycleState != 1)
            continue;
        if (writtenObjects >= G.Diagnostics.AIPlayLogMaxObjects)
            break;
        const char* category = AIObjectCategory(entry.ObjectID, entry.Actor.Settings);
        const auto kind = RuntimeItemKindAndConfidence(entry, category, visualPowerup);
        const std::int64_t dx = AIWrappedDeltaX(SignedU32(entry.Actor.PosX), SignedU32(selfX));
        const std::int64_t dy = SignedU32(entry.Actor.PosY) - SignedU32(selfY);
        AICompactRuntimeEntity entity {};
        entity.Values[0] = AIObservationV2EntityCategoryID(category);
        entity.Values[1] = kind.first;
        entity.Values[2] = -1;
        entity.Values[3] = static_cast<double>(dx);
        entity.Values[4] = static_cast<double>(dy);
        entity.Values[5] = SignedU32(entry.Actor.VelX);
        entity.Values[6] = SignedU32(entry.Actor.VelY);
        entity.Values[7] = entry.ObjectID;
        entity.Values[8] = entry.Actor.Settings;
        entity.Values[9] = entry.Actor.StateType;
        entity.Values[10] = entry.Actor.Flags;
        entity.Values[11] =
            (IsInCameraRect(entry.Actor.PosX, entry.Actor.PosY, sample.StageCameraGlobalX0, sample.StageCameraGlobalY0, sample.StageCameraGlobalWidth0, sample.StageCameraGlobalHeight0) ? 1 : 0) |
            (IsInCameraRect(entry.Actor.PosX, entry.Actor.PosY, sample.StageCameraGlobalX1, sample.StageCameraGlobalY1, sample.StageCameraGlobalWidth1, sample.StageCameraGlobalHeight1) ? 2 : 0);
        entity.Values[12] = kind.second;
        entity.Values[13] = 1;
        entity.Dist2 = dx * dx + dy * dy;
        entities.push_back(entity);
        writtenObjects++;
    }

    for (int slot = 0; slot < kAIFireballSlotCount; slot++)
    {
        if (sample.FireballSlotActive[slot] == 0)
            continue;
        int ownerConfidence = 0;
        int ownerHeuristic = 0;
        int statelessOwnerCandidate = -1;
        int statelessOwnerConfidence = 0;
        int statelessOwnerHeuristic = 0;
        bool ownerTracked = false;
        const int owner = AIFireballOwnerCandidate(instanceID, sample, slot, ownerConfidence, ownerHeuristic, statelessOwnerCandidate, statelessOwnerConfidence, statelessOwnerHeuristic, ownerTracked);
        const char* category = owner == 0 || owner == 1 ? "player_fireball" : "enemy_fireball";
        const std::int64_t dx = AIWrappedDeltaX(SignedU32(sample.FireballSlotPosX[slot]), SignedU32(selfX));
        const std::int64_t dy = SignedU32(sample.FireballSlotPosY[slot]) - SignedU32(selfY);
        AICompactRuntimeEntity entity {};
        entity.Values[0] = AIObservationV2EntityCategoryID(category);
        entity.Values[1] = sample.FireballSlotKind[slot];
        entity.Values[2] = owner;
        entity.Values[3] = static_cast<double>(dx);
        entity.Values[4] = static_cast<double>(dy);
        entity.Values[5] = SignedU32(sample.FireballSlotVelX[slot]);
        entity.Values[6] = SignedU32(sample.FireballSlotVelY[slot]);
        entity.Values[7] = 0;
        entity.Values[8] = 0;
        entity.Values[9] = sample.FireballSlotState[slot];
        entity.Values[10] = 0;
        entity.Values[11] = 0;
        entity.Values[12] = ownerConfidence;
        entity.Values[13] = 2;
        entity.Dist2 = dx * dx + dy * dy;
        entities.push_back(entity);
    }

    std::stable_sort(
        entities.begin(),
        entities.end(),
        [](const AICompactRuntimeEntity& a, const AICompactRuntimeEntity& b) { return a.Dist2 < b.Dist2; });
    for (int i = 0; i < maxEntities; i++)
    {
        if (i < static_cast<int>(entities.size()))
            features.insert(features.end(), std::begin(entities[i].Values), std::end(entities[i].Values));
        else
            features.insert(features.end(), kAICompactRuntimeEntityFeatures, 0.0);
    }
}

bool BuildCompactRuntimeImitationFeaturesForCount(
    std::size_t featureCount,
    const std::string& inputSchema,
    const GameStateSample& sample,
    const GameStateObjectScanCache& objectScanCache,
    int instanceID,
    int player,
    std::vector<double>& features)
{
    const int terrainFeatures = kAITileGridHeight * kAITileGridWidth * kAICompactRuntimeTerrainChannels;
    const int total = static_cast<int>(featureCount);
    const int legacyBaseFeatures = kAICompactRuntimeLegacyScalarCount + terrainFeatures * 2;
    const int baseFeatures = kAICompactRuntimeScalarCount + terrainFeatures * 2;
    const int selectedScalarCount = inputSchema == "nsmb_mvl_compact_observation_v3"
        ? kAICompactRuntimeScalarCount
        : (inputSchema == "nsmb_mvl_compact_observation_v2" ? kAICompactRuntimeLegacyScalarCount : 0);
    const int selectedBaseFeatures = selectedScalarCount == kAICompactRuntimeScalarCount
        ? baseFeatures
        : legacyBaseFeatures;
    if (selectedScalarCount == 0 || total < selectedBaseFeatures ||
        (total - selectedBaseFeatures) % kAICompactRuntimeEntityFeatures != 0)
        return false;
    const int maxEntities = (total - selectedBaseFeatures) / kAICompactRuntimeEntityFeatures;
    features.clear();
    features.reserve(featureCount);
    AppendAICompactRuntimeScalars(features, sample, objectScanCache, player, selectedScalarCount);
    AppendAICompactRuntimeTerrain(features, player == 0 ? sample.PlayerActor0TileProbe : sample.PlayerActor1TileProbe);
    AppendAICompactRuntimeTerrain(features, player == 0 ? sample.PlayerActor1TileProbe : sample.PlayerActor0TileProbe);
    AppendAICompactRuntimeEntities(features, sample, objectScanCache, instanceID, player, maxEntities);
    return features.size() == featureCount;
}

bool BuildCompactRuntimeImitationFeatures(
    const NsmbImitationAI::CompactActionPolicyModel& model,
    const GameStateSample& sample,
    const GameStateObjectScanCache& objectScanCache,
    int instanceID,
    int player,
    std::vector<double>& features)
{
    return BuildCompactRuntimeImitationFeaturesForCount(
        model.FeatureCount(),
        model.InputSchema,
        sample,
        objectScanCache,
        instanceID,
        player,
        features);
}

bool BuildCompactRuntimeImitationFeatures(
    const NsmbImitationAI::TorchCompactPolicyModel& model,
    const GameStateSample& sample,
    const GameStateObjectScanCache& objectScanCache,
    int instanceID,
    int player,
    std::vector<double>& features)
{
    return BuildCompactRuntimeImitationFeaturesForCount(
        model.FeatureCount(),
        model.InputSchema,
        sample,
        objectScanCache,
        instanceID,
        player,
        features);
}

bool CompactPredictionHasFirePress(
    const NsmbImitationAI::CompactActionPolicyModel& model,
    const NsmbImitationAI::CompactActionPrediction& prediction)
{
    for (std::size_t i = 0; i < model.Heads.size() && i < prediction.Actions.size(); i++)
    {
        const auto& head = model.Heads[i];
        if (head.Name != "fire")
            continue;
        const int action = prediction.Actions[i];
        if (action < 0 || action >= static_cast<int>(head.Classes.size()))
            return false;
        return head.Classes[static_cast<std::size_t>(action)] == "press";
    }
    return false;
}

bool CompactPredictionHasFirePress(
    const NsmbImitationAI::TorchCompactPolicyModel& model,
    const NsmbImitationAI::CompactActionPrediction& prediction)
{
    for (std::size_t i = 0; i < model.Heads.size() && i < prediction.Actions.size(); i++)
    {
        const auto& head = model.Heads[i];
        if (head.Name != "fire")
            continue;
        const int action = prediction.Actions[i];
        if (action < 0 || action >= static_cast<int>(head.Classes.size()))
            return false;
        return head.Classes[static_cast<std::size_t>(action)] == "press";
    }
    return false;
}

InputState BuildImitationAIInputFromHeld(const InputState& fallback, melonDS::u32 held)
{
    InputState input = NeutralInputPreservingTouch(fallback);
    input.KeyMask = (~held) & 0x0FFFu;
    return input;
}

InputState ApplyImitationAIInput(
    int instanceID,
    melonDS::u32 frame,
    melonDS::NDS* nds,
    int player,
    const InputState& fallback)
{
    auto traceFallback = [&](const char* reason) {
        if (G.AI.Imitation.TraceEnabled &&
            (G.AI.Imitation.TraceInterval <= 1 ||
                (frame % static_cast<melonDS::u32>(G.AI.Imitation.TraceInterval)) == 0))
        {
            std::printf(
                "NSMB ImitationAI: inst=%d frame=%u player=%d fallback=%s\n",
                instanceID,
                frame,
                player,
                reason);
        }
        return fallback;
    };
    if (!G.ImitationAI.IsEnabled() || !G.ImitationAI.HasModel() || frame < G.AI.Imitation.StartFrame || !nds || !nds->MainRAM)
        return traceFallback("disabled");
    if (G.AI.Imitation.HostOnly && G.NetRole != Role::Host)
        return traceFallback("hostOnly");
    if (G.AI.Imitation.ClientOnly && G.NetRole != Role::Client)
        return traceFallback("clientOnly");
    if (!ImitationAIProvidesInputForPlayer(player))
        return traceFallback("playerFilter");

    const bool inGameplay = IsMarioVsLuigiGameplay(nds);
    if (!inGameplay)
    {
        G.ImitationAI.ResetPlayer(instanceID, player);
        return traceFallback("notGameplay");
    }

    const NsmbImitationAI::Runtime::HeldRecord* cachedHeld =
        G.ImitationAI.CachedHeld(instanceID, player);
    if (G.ImitationAI.HasTorchCompactModel() &&
        G.AI.Imitation.InferInterval > 1 &&
        cachedHeld &&
        frame >= cachedHeld->Frame &&
        frame - cachedHeld->Frame <
            static_cast<melonDS::u32>(G.AI.Imitation.InferInterval))
    {
        const melonDS::u32 held = cachedHeld->Held & G.AI.Imitation.AllowedHeldMask;
        InputState input = BuildImitationAIInputFromHeld(fallback, held);
        if (G.AI.Imitation.TraceEnabled &&
            (G.AI.Imitation.TraceInterval <= 1 ||
                (frame % static_cast<melonDS::u32>(G.AI.Imitation.TraceInterval)) == 0))
        {
            std::printf(
                "NSMB ImitationAI: inst=%d frame=%u player=%d model=torchCompact cachedHeld=0x%03X keyMask=0x%03X inferInterval=%d cachedFrame=%u\n",
                instanceID,
                frame,
                player,
                held,
                input.KeyMask,
                G.AI.Imitation.InferInterval,
                cachedHeld->Frame);
        }
        return input;
    }

    const GameStateSample sample = ReadGameStateSample(nds);
    const GameStateObjectScanCache objectScanCache = BuildGameStateObjectScanCache(nds);
    if (G.ImitationAI.HasTorchCompactModel())
    {
        std::vector<double> features;
        if (!BuildCompactRuntimeImitationFeatures(
                G.ImitationAI.TorchCompactModel(),
                sample,
                objectScanCache,
                instanceID,
                player,
                features))
        {
            G.ImitationAI.ResetPlayer(instanceID, player);
            return traceFallback("torchCompactFeatures");
        }
        const NsmbImitationAI::CompactActionPrediction prediction =
            NsmbImitationAI::PredictTorchCompactPolicy(G.ImitationAI.TorchCompactModel(), features);
        melonDS::u32 held = prediction.Held & G.AI.Imitation.AllowedHeldMask;
        const bool firePressIntent = CompactPredictionHasFirePress(G.ImitationAI.TorchCompactModel(), prediction);
        std::int64_t guardHazardDx = 0;
        std::int64_t guardHazardDy = 0;
        const bool hazardGuardAdjusted =
            ApplyImitationAIHazardGuard(sample, objectScanCache, player, held, guardHazardDx, guardHazardDy);
        const char* fireTapPhase = "none";
        held = G.ImitationAI.ApplyFireTapRelease(
            instanceID,
            player,
            held,
            G.AI.Imitation.AllowedHeldMask,
            firePressIntent,
            fireTapPhase);
        bool neutralHoldAdjusted = false;
        held = G.ImitationAI.ApplyNeutralHold(
            instanceID,
            player,
            frame,
            held,
            G.AI.Imitation.NeutralHoldFrames,
            G.AI.Imitation.AllowedHeldMask,
            neutralHoldAdjusted);
        G.ImitationAI.CacheHeld(instanceID, player, frame, held);
        InputState input = BuildImitationAIInputFromHeld(fallback, held);

        if (G.AI.Imitation.TraceEnabled &&
            (G.AI.Imitation.TraceInterval <= 1 ||
                (frame % static_cast<melonDS::u32>(G.AI.Imitation.TraceInterval)) == 0))
        {
            std::printf(
                "NSMB ImitationAI: inst=%d frame=%u player=%d model=torchCompact held=0x%03X keyMask=0x%03X features=%zu hazardGuard=%d hazardDx=%lld hazardDy=%lld fireTap=%s neutralHold=%d inferInterval=%d actions=",
                instanceID,
                frame,
                player,
                held,
                input.KeyMask,
                features.size(),
                hazardGuardAdjusted ? 1 : 0,
                static_cast<long long>(guardHazardDx),
                static_cast<long long>(guardHazardDy),
                fireTapPhase,
                neutralHoldAdjusted ? 1 : 0,
                G.AI.Imitation.InferInterval);
            for (std::size_t i = 0; i < prediction.Actions.size() && i < G.ImitationAI.TorchCompactModel().Heads.size(); i++)
            {
                const auto& head = G.ImitationAI.TorchCompactModel().Heads[i];
                const int action = prediction.Actions[i];
                const char* className = action >= 0 && action < static_cast<int>(head.Classes.size())
                    ? head.Classes[static_cast<std::size_t>(action)].c_str()
                    : "?";
                const double confidence = i < prediction.Confidences.size() ? prediction.Confidences[i] : 0.0;
                std::printf(
                    "%s%s=%s(%.3f)",
                    i == 0 ? "" : ",",
                    head.Name.c_str(),
                    className,
                    confidence);
            }
            std::printf("\n");
        }

        return input;
    }
    if (G.ImitationAI.HasCompactModel())
    {
        std::vector<double> features;
        if (!BuildCompactRuntimeImitationFeatures(
                G.ImitationAI.CompactModel(),
                sample,
                objectScanCache,
                instanceID,
                player,
                features))
        {
            G.ImitationAI.ResetPlayer(instanceID, player);
            return traceFallback("compactFeatures");
        }
        const NsmbImitationAI::CompactActionPrediction prediction =
            NsmbImitationAI::PredictCompactActionPolicy(G.ImitationAI.CompactModel(), features);
        melonDS::u32 held = prediction.Held & G.AI.Imitation.AllowedHeldMask;
        const bool firePressIntent = CompactPredictionHasFirePress(G.ImitationAI.CompactModel(), prediction);
        std::int64_t guardHazardDx = 0;
        std::int64_t guardHazardDy = 0;
        const bool hazardGuardAdjusted =
            ApplyImitationAIHazardGuard(sample, objectScanCache, player, held, guardHazardDx, guardHazardDy);
        const char* fireTapPhase = "none";
        held = G.ImitationAI.ApplyFireTapRelease(
            instanceID,
            player,
            held,
            G.AI.Imitation.AllowedHeldMask,
            firePressIntent,
            fireTapPhase);
        InputState input = NeutralInputPreservingTouch(fallback);
        input.KeyMask = (~held) & 0x0FFFu;

        if (G.AI.Imitation.TraceEnabled &&
            (G.AI.Imitation.TraceInterval <= 1 ||
                (frame % static_cast<melonDS::u32>(G.AI.Imitation.TraceInterval)) == 0))
        {
            std::printf(
                "NSMB ImitationAI: inst=%d frame=%u player=%d model=compact held=0x%03X keyMask=0x%03X features=%zu hazardGuard=%d hazardDx=%lld hazardDy=%lld fireTap=%s actions=",
                instanceID,
                frame,
                player,
                held,
                input.KeyMask,
                features.size(),
                hazardGuardAdjusted ? 1 : 0,
                static_cast<long long>(guardHazardDx),
                static_cast<long long>(guardHazardDy),
                fireTapPhase);
            for (std::size_t i = 0; i < prediction.Actions.size() && i < G.ImitationAI.CompactModel().Heads.size(); i++)
            {
                const auto& head = G.ImitationAI.CompactModel().Heads[i];
                const int action = prediction.Actions[i];
                const char* className = action >= 0 && action < static_cast<int>(head.Classes.size())
                    ? head.Classes[static_cast<std::size_t>(action)].c_str()
                    : "?";
                const double confidence = i < prediction.Confidences.size() ? prediction.Confidences[i] : 0.0;
                std::printf(
                    "%s%s=%s(%.3f)",
                    i == 0 ? "" : ",",
                    head.Name.c_str(),
                    className,
                    confidence);
            }
            std::printf("\n");
        }

        return input;
    }

    std::vector<double> features;
    int filled = 0;
    int missing = 0;
    if (!BuildRuntimeImitationFeatures(
            G.ImitationAI.LinearModel(),
            sample,
            objectScanCache,
            instanceID,
            frame,
            inGameplay,
            player,
            features,
            filled,
            missing))
    {
        return traceFallback("linearFeatures");
    }

    const NsmbImitationAI::Prediction prediction =
        NsmbImitationAI::PredictLinearPolicy(G.ImitationAI.LinearModel(), features, G.AI.Imitation.Threshold);
    melonDS::u32 held = prediction.Held & G.AI.Imitation.AllowedHeldMask;
    auto keepHigherProbability = [&prediction, &held](int firstBit, int secondBit) {
        const melonDS::u32 firstMask = 1u << firstBit;
        const melonDS::u32 secondMask = 1u << secondBit;
        if ((held & firstMask) == 0 || (held & secondMask) == 0)
            return;
        const double firstProb =
            firstBit < static_cast<int>(prediction.Probabilities.size()) ? prediction.Probabilities[firstBit] : 0.0;
        const double secondProb =
            secondBit < static_cast<int>(prediction.Probabilities.size()) ? prediction.Probabilities[secondBit] : 0.0;
        if (firstProb >= secondProb)
            held &= ~secondMask;
        else
            held &= ~firstMask;
    };
    keepHigherProbability(4, 5);
    keepHigherProbability(6, 7);
    std::int64_t guardHazardDx = 0;
    std::int64_t guardHazardDy = 0;
    const bool hazardGuardAdjusted =
        ApplyImitationAIHazardGuard(sample, objectScanCache, player, held, guardHazardDx, guardHazardDy);
    InputState input = NeutralInputPreservingTouch(fallback);
    input.KeyMask = (~held) & 0x0FFFu;

    if (G.AI.Imitation.TraceEnabled &&
        (G.AI.Imitation.TraceInterval <= 1 ||
            (frame % static_cast<melonDS::u32>(G.AI.Imitation.TraceInterval)) == 0))
    {
        std::printf(
            "NSMB ImitationAI: inst=%d frame=%u player=%d held=0x%03X keyMask=0x%03X features=%d/%d threshold=%.3f hazardGuard=%d hazardDx=%lld hazardDy=%lld probs=",
            instanceID,
            frame,
            player,
            held,
            input.KeyMask,
            filled,
            filled + missing,
            G.AI.Imitation.Threshold,
            hazardGuardAdjusted ? 1 : 0,
            static_cast<long long>(guardHazardDx),
            static_cast<long long>(guardHazardDy));
        for (std::size_t i = 0; i < prediction.Probabilities.size() && i < G.ImitationAI.LinearModel().Buttons.size(); i++)
        {
            std::printf(
                "%s%s=%.3f",
                i == 0 ? "" : ",",
                G.ImitationAI.LinearModel().Buttons[i].c_str(),
                prediction.Probabilities[i]);
        }
        std::printf("\n");
    }

    return input;
}

void PrepareAIPlayLogFireballOwnerTracking(int instanceID, const GameStateSample& sample)
{
    if (instanceID < 0 || instanceID >= 16)
        return;
    G.AIObservationRuntime.UpdateFireballHandler(instanceID, sample.FireballsHandlerPtr);
    for (int i = 0; i < kAIFireballSlotCount; i++)
    {
        if (sample.FireballSlotActive[i] == 0)
            G.AIObservationRuntime.InvalidateFireballOwner(instanceID, i);
    }
}

void WriteAIObservationV2ButtonLabelsJson(std::ostream& out, melonDS::u32 held)
{
    out << "\"buttons\":{\"up\":" << ((held & (1u << 6)) ? 1 : 0)
        << ",\"down\":" << ((held & (1u << 7)) ? 1 : 0)
        << ",\"left\":" << ((held & (1u << 5)) ? 1 : 0)
        << ",\"right\":" << ((held & (1u << 4)) ? 1 : 0)
        << ",\"y\":" << ((held & (1u << 11)) ? 1 : 0)
        << ",\"b\":" << ((held & (1u << 1)) ? 1 : 0)
        << "}";
}

melonDS::u32 AIObservationV2AllowedHeld(melonDS::u32 held)
{
    return held & ((1u << 1) | (1u << 4) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 11));
}

void WriteAIObservationV2ActionLabelsJson(std::ostream& out, melonDS::u32 held, melonDS::u32 pressed, bool canFire)
{
    const bool left = (held & (1u << 5)) != 0;
    const bool right = (held & (1u << 4)) != 0;
    const bool up = (held & (1u << 6)) != 0;
    const bool down = (held & (1u << 7)) != 0;
    const bool yHeld = (held & (1u << 11)) != 0;
    const bool bHeld = (held & (1u << 1)) != 0;
    const bool yPressed = (pressed & (1u << 11)) != 0;
    const bool bPressed = (pressed & (1u << 1)) != 0;
    const int horizontalId = left && !right ? 1 : right && !left ? 2 : 0;
    const int verticalId = up && !down ? 1 : down && !up ? 2 : 0;
    const int jumpId = bPressed ? 1 : bHeld ? 2 : 0;
    const int runId = yHeld ? 1 : 0;
    const int fireId = canFire && yPressed ? 1 : 0;
    const char* horizontal = horizontalId == 1 ? "left" : horizontalId == 2 ? "right" : "neutral";
    const char* vertical = verticalId == 1 ? "up" : verticalId == 2 ? "down" : "neutral";
    const char* jump = jumpId == 1 ? "press" : jumpId == 2 ? "hold" : "none";
    const char* run = runId ? "on" : "off";
    const char* fire = fireId == 1 ? "press" : "off";
    out << "\"actions\":{\"horizontal\":\"" << horizontal << "\",\"horizontalId\":" << horizontalId
        << ",\"vertical\":\"" << vertical << "\",\"verticalId\":" << verticalId
        << ",\"jump\":\"" << jump << "\",\"jumpId\":" << jumpId
        << ",\"run\":\"" << run << "\",\"runId\":" << runId
        << ",\"fire\":\"" << fire << "\",\"fireId\":" << fireId
        << "}";
}

void WriteAIObservationV2LabelJson(std::ostream& out, const char* key, melonDS::u32 held, melonDS::u32 pressed, bool canFire)
{
    out << "\"" << key << "\":{\"valid\":1,\"held\":" << held
        << ",\"pressed\":" << pressed
        << ",\"allowedHeld\":" << AIObservationV2AllowedHeld(held) << ",";
    WriteAIObservationV2ButtonLabelsJson(out, held);
    out << ",";
    WriteAIObservationV2ActionLabelsJson(out, held, pressed, canFire);
    out << ",\"source\":\"player\"}";
}

void WriteAIObservationV2LabelsJson(std::ostream& out, const GameStateSample& sample)
{
    const melonDS::u32 p0Visual = AIVisualPowerupKindCandidate(sample.Player0Powerup, sample.Player0InventoryPowerup, sample.PlayerActor0PowerupState, sample.PlayerActor0PowerupFormState, sample.PlayerActor0ShellState);
    const melonDS::u32 p1Visual = AIVisualPowerupKindCandidate(sample.Player1Powerup, sample.Player1InventoryPowerup, sample.PlayerActor1PowerupState, sample.PlayerActor1PowerupFormState, sample.PlayerActor1ShellState);
    out << "\"labels\":{";
    WriteAIObservationV2LabelJson(out, "player0", sample.InputPlayer0Held, sample.InputPlayer0Pressed, p0Visual == 2);
    out << ",";
    WriteAIObservationV2LabelJson(out, "player1", sample.InputPlayer1Held, sample.InputPlayer1Pressed, p1Visual == 2);
    out << "}";
}

int AIObservationV2ScreenMask(const GameStateSample& sample, melonDS::u32 x, melonDS::u32 y)
{
    int mask = 0;
    if (IsInCameraRect(x, y, sample.StageCameraGlobalX0, sample.StageCameraGlobalY0, sample.StageCameraGlobalWidth0, sample.StageCameraGlobalHeight0))
        mask |= 1;
    if (IsInCameraRect(x, y, sample.StageCameraGlobalX1, sample.StageCameraGlobalY1, sample.StageCameraGlobalWidth1, sample.StageCameraGlobalHeight1))
        mask |= 2;
    return mask;
}

void WriteAIObservationV2ScalarFeaturesJson(
    std::ostream& out,
    const GameStateSample& sample,
    const GameStateObjectScanCache& objectScanCache,
    int player,
    bool exactHitboxSchema)
{
    const bool p0 = player == 0;
    const int opponent = player ^ 1;
    auto v = [p0](melonDS::u32 a, melonDS::u32 b) { return p0 ? a : b; };
    auto ov = [opponent](melonDS::u32 a, melonDS::u32 b) { return opponent == 0 ? a : b; };
    const melonDS::u32 selfX = v(sample.PlayerActor0PosX, sample.PlayerActor1PosX);
    const melonDS::u32 selfY = v(sample.PlayerActor0PosY, sample.PlayerActor1PosY);
    const melonDS::u32 selfVelX = v(sample.PlayerActor0VelX, sample.PlayerActor1VelX);
    const melonDS::u32 selfVelY = v(sample.PlayerActor0VelY, sample.PlayerActor1VelY);
    const melonDS::u32 selfPowerup = v(sample.Player0Powerup, sample.Player1Powerup);
    const melonDS::u32 selfInventoryPowerup = v(sample.Player0InventoryPowerup, sample.Player1InventoryPowerup);
    const melonDS::u32 selfActorPowerupState = p0 ? sample.PlayerActor0PowerupState : sample.PlayerActor1PowerupState;
    const melonDS::u32 selfActorPowerupFormState = p0 ? sample.PlayerActor0PowerupFormState : sample.PlayerActor1PowerupFormState;
    const melonDS::u32 selfShellState = p0 ? sample.PlayerActor0ShellState : sample.PlayerActor1ShellState;
    const melonDS::u32 selfVisualPowerup = AIVisualPowerupKindCandidate(selfPowerup, selfInventoryPowerup, selfActorPowerupState, selfActorPowerupFormState, selfShellState);
    const melonDS::u32 opponentVisualPowerup = AIVisualPowerupKindCandidate(
        ov(sample.Player0Powerup, sample.Player1Powerup),
        ov(sample.Player0InventoryPowerup, sample.Player1InventoryPowerup),
        opponent == 0 ? sample.PlayerActor0PowerupState : sample.PlayerActor1PowerupState,
        opponent == 0 ? sample.PlayerActor0PowerupFormState : sample.PlayerActor1PowerupFormState,
        opponent == 0 ? sample.PlayerActor0ShellState : sample.PlayerActor1ShellState);
    const PlayerHitboxSample& selfHitbox = p0 ? sample.PlayerActor0Hitbox : sample.PlayerActor1Hitbox;
    const PlayerHitboxSample& opponentHitbox = p0 ? sample.PlayerActor1Hitbox : sample.PlayerActor0Hitbox;
    const AIPlayerTileProbeSample& tileProbe = p0 ? sample.PlayerActor0TileProbe : sample.PlayerActor1TileProbe;
    const melonDS::u32 collision = v(sample.PlayerActor0CollisionFlag, sample.PlayerActor1CollisionFlag);
    const bool contactGround = AIPlayerContactGround(collision);
    const bool contactWallLeft = (collision & (0x00000008u | 0x00000400u | 0x20000000u)) != 0;
    const bool contactWallRight = (collision & (0x00000010u | 0x00000800u | 0x40000000u)) != 0;
    const AITerrainDerivedSummary terrainSummary =
        DeriveAITerrainSummaryFromGrid(tileProbe, contactGround, contactWallLeft, contactWallRight);
    const RuntimeHazardThreat hazard = MostDangerousRuntimeHazard(
        objectScanCache,
        selfX,
        selfY,
        selfVelX,
        0x40000,
        0x50000,
        0x30000);
    const GameStateObjectScanEntry* nearestItem = NearestRuntimeItem(objectScanCache, selfX, selfY, false);
    int nearestItemKind = 0;
    int nearestItemAvoid = 0;
    std::int64_t nearestItemDx = 0;
    std::int64_t nearestItemDy = 0;
    if (nearestItem)
    {
        const char* itemCategory = AIObjectCategory(nearestItem->ObjectID, nearestItem->Actor.Settings);
        nearestItemKind = RuntimeItemKindAndConfidence(*nearestItem, itemCategory, selfVisualPowerup).first;
        nearestItemAvoid = RuntimeItemSettingsIsMiniMushroomCandidate(nearestItem->Actor.Settings) ? 1 : 0;
        nearestItemDx = AIWrappedDeltaX(SignedU32(nearestItem->Actor.PosX), SignedU32(selfX));
        nearestItemDy = SignedU32(nearestItem->Actor.PosY) - SignedU32(selfY);
    }
    const bool selfStarInvincible = AIStarInvincibleCandidate(selfInventoryPowerup, selfActorPowerupState, selfActorPowerupFormState, selfShellState);
    out << "\"stage_id\":" << sample.StageID
        << ",\"stage_group\":" << sample.StageGroup
        << ",\"self_x\":" << SignedU32(selfX)
        << ",\"self_y\":" << SignedU32(selfY)
        << ",\"self_vx\":" << SignedU32(selfVelX)
        << ",\"self_vy\":" << SignedU32(selfVelY)
        << ",\"self_powerup_kind\":" << selfVisualPowerup;
    if (exactHitboxSchema)
    {
        out << ",\"self_body_size_class\":" << AIPlayerBodySizeClass(selfVisualPowerup)
            << ",\"self_hitbox_found\":" << selfHitbox.Found
            << ",\"self_hitbox_center_offset_x\":" << SignedU32(selfHitbox.CenterOffsetX)
            << ",\"self_hitbox_center_offset_y\":" << SignedU32(selfHitbox.CenterOffsetY)
            << ",\"self_hitbox_half_width\":" << SignedU32(selfHitbox.HalfWidth)
            << ",\"self_hitbox_half_height\":" << SignedU32(selfHitbox.HalfHeight);
    }
    out << ",\"self_invincible\":" << (selfStarInvincible ? 1 : 0)
        << ",\"self_star_invincible\":" << (selfStarInvincible ? 1 : 0)
        << ",\"self_battle_stars\":" << v(sample.Player0BattleStars, sample.Player1BattleStars)
        << ",\"self_coins\":" << v(sample.Player0Coins, sample.Player1Coins)
        << ",\"opponent_dx\":" << AIWrappedDeltaX(SignedU32(ov(sample.PlayerActor0PosX, sample.PlayerActor1PosX)), SignedU32(selfX))
        << ",\"opponent_dy\":" << (SignedU32(ov(sample.PlayerActor0PosY, sample.PlayerActor1PosY)) - SignedU32(selfY))
        << ",\"opponent_powerup_kind\":" << opponentVisualPowerup;
    if (exactHitboxSchema)
    {
        out << ",\"opponent_body_size_class\":" << AIPlayerBodySizeClass(opponentVisualPowerup)
            << ",\"opponent_hitbox_found\":" << opponentHitbox.Found
            << ",\"opponent_hitbox_center_offset_x\":" << SignedU32(opponentHitbox.CenterOffsetX)
            << ",\"opponent_hitbox_center_offset_y\":" << SignedU32(opponentHitbox.CenterOffsetY)
            << ",\"opponent_hitbox_half_width\":" << SignedU32(opponentHitbox.HalfWidth)
            << ",\"opponent_hitbox_half_height\":" << SignedU32(opponentHitbox.HalfHeight);
    }
    out << ",\"opponent_battle_stars\":" << ov(sample.Player0BattleStars, sample.Player1BattleStars)
        << ",\"target_found\":" << sample.VsStarActorFound
        << ",\"target_dx\":" << AIWrappedDeltaX(SignedU32(sample.VsStarActorPosX), SignedU32(selfX))
        << ",\"target_dy\":" << (SignedU32(sample.VsStarActorPosY) - SignedU32(selfY))
        << ",\"nearest_item_found\":" << (nearestItem ? 1 : 0)
        << ",\"nearest_item_dx\":" << nearestItemDx
        << ",\"nearest_item_dy\":" << nearestItemDy
        << ",\"nearest_item_kind\":" << nearestItemKind
        << ",\"nearest_item_avoid\":" << nearestItemAvoid
        << ",\"runtime_hazard_found\":" << (hazard.Found ? 1 : 0)
        << ",\"runtime_hazard_dx\":" << hazard.Dx
        << ",\"runtime_hazard_dy\":" << hazard.Dy
        << ",\"runtime_hazard_closing\":" << (hazard.Closing ? 1 : 0)
        << ",\"runtime_hazard_category\":" << hazard.CategoryID
        << ",\"tile_groundBelowSolid\":" << terrainSummary.GroundBelowSolid
        << ",\"tile_blockedAhead\":" << terrainSummary.BlockedAhead
        << ",\"tile_blockedLeft\":" << terrainSummary.BlockedLeft
        << ",\"tile_blockedRight\":" << terrainSummary.BlockedRight
        << ",\"tile_effectiveHoleAhead\":" << terrainSummary.EffectiveHoleAhead
        << ",\"tile_effectiveHoleLeft\":" << terrainSummary.EffectiveHoleLeft
        << ",\"tile_effectiveHoleRight\":" << terrainSummary.EffectiveHoleRight;
}

void WriteAIObservationV2ObjectEntityJson(std::ostream& out, const GameStateObjectScanEntry& entry, const GameStateSample& sample)
{
    const char* category = AIObjectCategory(entry.ObjectID, entry.Actor.Settings);
    const melonDS::u32 p0Visual = AIVisualPowerupKindCandidate(sample.Player0Powerup, sample.Player0InventoryPowerup, sample.PlayerActor0PowerupState, sample.PlayerActor0PowerupFormState, sample.PlayerActor0ShellState);
    const melonDS::u32 p1Visual = AIVisualPowerupKindCandidate(sample.Player1Powerup, sample.Player1InventoryPowerup, sample.PlayerActor1PowerupState, sample.PlayerActor1PowerupFormState, sample.PlayerActor1ShellState);
    const auto p0Kind = RuntimeItemKindAndConfidence(entry, category, p0Visual);
    const auto p1Kind = RuntimeItemKindAndConfidence(entry, category, p1Visual);
    out << "{\"source\":\"object\",\"category\":\"" << category
        << "\",\"categoryId\":" << AIObservationV2EntityCategoryID(category)
        << ",\"objectId\":" << entry.ObjectID
        << ",\"settings\":" << entry.Actor.Settings
        << ",\"kindByPlayer\":[{\"kind\":" << p0Kind.first << ",\"confidence\":" << p0Kind.second
        << "},{\"kind\":" << p1Kind.first << ",\"confidence\":" << p1Kind.second << "}]"
        << ",";
    WriteAIVec3Json(out, "pos", entry.Actor.PosX, entry.Actor.PosY, entry.Actor.PosZ);
    out << ",";
    WriteAIVec3Json(out, "vel", entry.Actor.VelX, entry.Actor.VelY, entry.Actor.VelZ);
    out << ",\"relative\":{\"player0\":{\"dx\":" << AIWrappedDeltaX(SignedU32(entry.Actor.PosX), SignedU32(sample.PlayerActor0PosX))
        << ",\"dy\":" << (SignedU32(entry.Actor.PosY) - SignedU32(sample.PlayerActor0PosY))
        << "},\"player1\":{\"dx\":" << AIWrappedDeltaX(SignedU32(entry.Actor.PosX), SignedU32(sample.PlayerActor1PosX))
        << ",\"dy\":" << (SignedU32(entry.Actor.PosY) - SignedU32(sample.PlayerActor1PosY))
        << "}}"
        << ",\"screenMask\":" << AIObservationV2ScreenMask(sample, entry.Actor.PosX, entry.Actor.PosY)
        << ",\"state\":" << entry.Actor.StateType
        << ",\"flags\":" << entry.Actor.Flags
        << "}";
}

void WriteAIObservationV2FireballEntityJson(std::ostream& out, int instanceID, const GameStateSample& sample, int slot)
{
    int ownerConfidence = 0;
    int ownerHeuristic = 0;
    int statelessOwnerCandidate = -1;
    int statelessOwnerConfidence = 0;
    int statelessOwnerHeuristic = 0;
    bool ownerTracked = false;
    const int owner = AIFireballOwnerCandidate(instanceID, sample, slot, ownerConfidence, ownerHeuristic, statelessOwnerCandidate, statelessOwnerConfidence, statelessOwnerHeuristic, ownerTracked);
    const bool ownerVerified = sample.FireballSlotKind[slot] <= 3;
    const char* category = owner == 0 || owner == 1 ? "player_fireball" : "enemy_fireball";
    out << "{\"source\":\"fireball\",\"category\":\"" << category
        << "\",\"categoryId\":" << AIObservationV2EntityCategoryID(category)
        << ",\"objectId\":0,\"settings\":0"
        << ",\"kind\":" << sample.FireballSlotKind[slot]
        << ",\"owner\":" << owner
        << ",\"ownerConfidence\":" << ownerConfidence
        << ",\"ownerVerified\":" << (ownerVerified ? 1 : 0)
        << ",";
    WriteAIVec3Json(out, "pos", sample.FireballSlotPosX[slot], sample.FireballSlotPosY[slot], sample.FireballSlotPosZ[slot]);
    out << ",";
    WriteAIVec3Json(out, "vel", sample.FireballSlotVelX[slot], sample.FireballSlotVelY[slot], sample.FireballSlotVelZ[slot]);
    out << ",\"relative\":{\"player0\":{\"dx\":" << AIWrappedDeltaX(SignedU32(sample.FireballSlotPosX[slot]), SignedU32(sample.PlayerActor0PosX))
        << ",\"dy\":" << (SignedU32(sample.FireballSlotPosY[slot]) - SignedU32(sample.PlayerActor0PosY))
        << "},\"player1\":{\"dx\":" << AIWrappedDeltaX(SignedU32(sample.FireballSlotPosX[slot]), SignedU32(sample.PlayerActor1PosX))
        << ",\"dy\":" << (SignedU32(sample.FireballSlotPosY[slot]) - SignedU32(sample.PlayerActor1PosY))
        << "}}"
        << ",\"screenMask\":0"
        << ",\"state\":" << sample.FireballSlotState[slot]
        << ",\"flags\":0}";
}

void WriteAIObservationV2Record(std::ostream& out, int instanceID, melonDS::u32 frame, const GameStateSample& sample, const GameStateObjectScanCache& objectScanCache, int localPlayer, bool exactHitboxSchema)
{
    // A checkpoint restore can run while a frame is being finalized. Build the
    // complete JSONL record before touching the file so it cannot leave a
    // structurally partial line after a stage transition.
    std::ostringstream record;
    record << "{\"schema\":\"nsmb_mvl_compact_observation_v" << (exactHitboxSchema ? 3 : 2) << "\""
        << ",\"sourceSchema\":\"nsmb_mvl_ai_play_log_v1_direct\""
        << ",\"recordingIndex\":0"
        << ",\"recordingFrameIndex\":" << frame
        << ",\"frame\":" << frame
        << ",\"instance\":" << instanceID
        << ",\"role\":\"" << (G.NetRole == Role::Host ? "host" : "client") << "\""
        << ",\"localPlayer\":" << localPlayer
        << ",\"stage\":{\"id\":" << sample.StageID
        << ",\"group\":" << sample.StageGroup
        << ",\"vsMode\":" << sample.VsMode
        << ",\"localPlayerIdMemory\":" << sample.LocalPlayerID
        << ",\"vsCoinCount\":" << sample.VsCoinCount
        << "},";
    WriteAIObservationV2LabelsJson(record, sample);
    record << ",\"players\":[";
    WriteAIObservationV2PlayerJson(record, 0, sample, exactHitboxSchema);
    record << ",";
    WriteAIObservationV2PlayerJson(record, 1, sample, exactHitboxSchema);
    record << "],\"scalarFeaturesByPlayer\":{\"player0\":{";
    WriteAIObservationV2ScalarFeaturesJson(record, sample, objectScanCache, 0, exactHitboxSchema);
    record << "},\"player1\":{";
    WriteAIObservationV2ScalarFeaturesJson(record, sample, objectScanCache, 1, exactHitboxSchema);
    record << "}},\"targets\":{\"bigStarActor\":{\"found\":" << sample.VsStarActorFound << ",";
    WriteAIVec3Json(record, "pos", sample.VsStarActorPosX, sample.VsStarActorPosY, sample.VsStarActorPosZ);
    record << "}},\"camera\":{\"found\":" << sample.StageCameraFound
        << ",\"globalX0\":" << SignedU32(sample.StageCameraGlobalX0)
        << ",\"globalX1\":" << SignedU32(sample.StageCameraGlobalX1)
        << ",\"globalY0\":" << SignedU32(sample.StageCameraGlobalY0)
        << ",\"globalY1\":" << SignedU32(sample.StageCameraGlobalY1)
        << ",\"width0\":" << SignedU32(sample.StageCameraGlobalWidth0)
        << ",\"width1\":" << SignedU32(sample.StageCameraGlobalWidth1)
        << ",\"height0\":" << SignedU32(sample.StageCameraGlobalHeight0)
        << ",\"height1\":" << SignedU32(sample.StageCameraGlobalHeight1)
        << "},\"objectSummary\":{\"total\":" << sample.ObjectScanTotal
        << ",\"active\":" << sample.ObjectActiveCount
        << ",\"dead\":" << sample.ObjectDeadCount
        << "},\"entities\":[";
    bool firstEntity = true;
    int writtenObjects = 0;
    for (const GameStateObjectScanEntry& entry : objectScanCache.Entries)
    {
        if (entry.LifecycleState != 1)
            continue;
        if (writtenObjects >= G.Diagnostics.AIPlayLogMaxObjects)
            break;
        if (!firstEntity)
            record << ",";
        firstEntity = false;
        WriteAIObservationV2ObjectEntityJson(record, entry, sample);
        writtenObjects++;
    }
    for (int slot = 0; slot < kAIFireballSlotCount; slot++)
    {
        if (sample.FireballSlotActive[slot] == 0)
            continue;
        if (!firstEntity)
            record << ",";
        firstEntity = false;
        WriteAIObservationV2FireballEntityJson(record, instanceID, sample, slot);
    }
    record << "]}\n";
    out << record.str();
}

void TraceAIPlayLog(int instanceID, melonDS::u32 frame, melonDS::NDS* nds)
{
    const bool writeV1 = !G.Diagnostics.AIPlayLogPath.empty() &&
        G.AIObservationRuntime.CanWriteLog(AIObservation::LogKind::V1);
    const bool writeV2 = !G.Diagnostics.AIObservationV2Path.empty() &&
        G.AIObservationRuntime.CanWriteLog(AIObservation::LogKind::V2);
    const bool writeV3 = !G.Diagnostics.AIObservationV3Path.empty() &&
        G.AIObservationRuntime.CanWriteLog(AIObservation::LogKind::V3);
    if ((!writeV1 && !writeV2 && !writeV3) || !nds || !nds->MainRAM)
        return;
    if (frame < G.Diagnostics.AIPlayLogStartFrame)
        return;
    if (G.Diagnostics.AIPlayLogEndFrame != 0 && frame > G.Diagnostics.AIPlayLogEndFrame)
        return;
    if ((frame % static_cast<melonDS::u32>(G.Diagnostics.AIPlayLogInterval)) != 0)
        return;

    const bool inGameplay = IsMarioVsLuigiGameplay(nds);
    if (G.Diagnostics.AIPlayLogGameplayOnly && !inGameplay)
        return;

    const GameStateSample sample = ReadGameStateSample(nds);
    const GameStateObjectScanCache objectScanCache = BuildGameStateObjectScanCache(nds);
    const int localPlayer = CurrentPacketBridgeLocalPlayer();
    PrepareAIPlayLogFireballOwnerTracking(instanceID, sample);

    const bool v2StageAllowed = G.Diagnostics.AIObservationV2StageFilter < 0 ||
        (sample.StageGroup == 9 && sample.StageID == static_cast<melonDS::u32>(G.Diagnostics.AIObservationV2StageFilter));
    const bool v3StageAllowed = G.Diagnostics.AIObservationV3StageFilter < 0 ||
        (sample.StageGroup == 9 && sample.StageID == static_cast<melonDS::u32>(G.Diagnostics.AIObservationV3StageFilter));

    if (writeV2 && v2StageAllowed)
    {
        WriteAIObservationV2Record(
            G.AIObservationRuntime.Log(AIObservation::LogKind::V2),
            instanceID, frame, sample, objectScanCache, localPlayer, false);
        G.AIObservationRuntime.RecordLogLine(
            AIObservation::LogKind::V2,
            G.Diagnostics.AIPlayLogFlushInterval);
    }

    if (writeV3 && v3StageAllowed)
    {
        WriteAIObservationV2Record(
            G.AIObservationRuntime.Log(AIObservation::LogKind::V3),
            instanceID, frame, sample, objectScanCache, localPlayer, true);
        G.AIObservationRuntime.RecordLogLine(
            AIObservation::LogKind::V3,
            G.Diagnostics.AIPlayLogFlushInterval);
    }

    if (!writeV1)
        return;

    std::ofstream& aiPlayLog =
        G.AIObservationRuntime.Log(AIObservation::LogKind::V1);

    aiPlayLog << "{\"schema\":\"nsmb_mvl_ai_play_log_v1\""
        << ",\"instance\":" << instanceID
        << ",\"frame\":" << frame
        << ",\"role\":\"" << (G.NetRole == Role::Host ? "host" : "client") << "\""
        << ",\"localPlayer\":" << localPlayer
        << ",\"inGameplay\":" << (inGameplay ? 1 : 0)
        << ",\"stage\":{\"id\":" << sample.StageID
        << ",\"group\":" << sample.StageGroup
        << ",\"vsMode\":" << sample.VsMode
        << ",\"localPlayerIdMemory\":" << sample.LocalPlayerID
        << ",\"vsCoinCount\":" << sample.VsCoinCount
        << "}";

    aiPlayLog << ",\"inputs\":{";
    WriteAIInputJson(aiPlayLog, "console0", sample.InputConsole0Held, sample.InputConsole0Pressed);
    aiPlayLog << ",";
    WriteAIInputJson(aiPlayLog, "console1", sample.InputConsole1Held, sample.InputConsole1Pressed);
    aiPlayLog << ",";
    WriteAIInputJson(aiPlayLog, "player0", sample.InputPlayer0Held, sample.InputPlayer0Pressed);
    aiPlayLog << ",";
    WriteAIInputJson(aiPlayLog, "player1", sample.InputPlayer1Held, sample.InputPlayer1Pressed);
    aiPlayLog << ",";
    WriteAIAppliedInputJson(aiPlayLog, instanceID, 0);
    aiPlayLog << ",";
    WriteAIAppliedInputJson(aiPlayLog, instanceID, 1);
    aiPlayLog << ",\"touchKnown\":0}";

    aiPlayLog << ",\"players\":[";
    WriteAIPlayerJson(aiPlayLog, 0, sample);
    aiPlayLog << ",";
    WriteAIPlayerJson(aiPlayLog, 1, sample);
    aiPlayLog << "]";

    aiPlayLog << ",\"targets\":{\"bigStarCandidate\":{\"found\":" << sample.VsStarFound
        << ",\"guid\":";
    WriteJsonHex(aiPlayLog, sample.VsStarGUID);
    aiPlayLog << ",\"base\":";
    WriteJsonHex(aiPlayLog, sample.VsStarBase);
    aiPlayLog << ",";
    WriteAIVec3Json(aiPlayLog, "pos", sample.VsStarPosX, sample.VsStarPosY, sample.VsStarPosZ);
    aiPlayLog << "},\"bigStarActor\":{\"found\":" << sample.VsStarActorFound
        << ",\"guid\":";
    WriteJsonHex(aiPlayLog, sample.VsStarActorGUID);
    aiPlayLog << ",\"base\":";
    WriteJsonHex(aiPlayLog, sample.VsStarActorBase);
    aiPlayLog << ",";
    WriteAIVec3Json(aiPlayLog, "pos", sample.VsStarActorPosX, sample.VsStarActorPosY, sample.VsStarActorPosZ);
    aiPlayLog << "}}";

    aiPlayLog << ",\"camera\":{\"found\":" << sample.StageCameraFound
        << ",\"globalX0\":" << SignedU32(sample.StageCameraGlobalX0)
        << ",\"globalX1\":" << SignedU32(sample.StageCameraGlobalX1)
        << ",\"globalY0\":" << SignedU32(sample.StageCameraGlobalY0)
        << ",\"globalY1\":" << SignedU32(sample.StageCameraGlobalY1)
        << ",\"width0\":" << SignedU32(sample.StageCameraGlobalWidth0)
        << ",\"width1\":" << SignedU32(sample.StageCameraGlobalWidth1)
        << ",\"height0\":" << SignedU32(sample.StageCameraGlobalHeight0)
        << ",\"height1\":" << SignedU32(sample.StageCameraGlobalHeight1)
        << "}";

    aiPlayLog << ",\"objectSummary\":{\"total\":" << sample.ObjectScanTotal
        << ",\"active\":" << sample.ObjectActiveCount
        << ",\"dead\":" << sample.ObjectDeadCount
        << ",\"notCreated\":" << sample.ObjectNotCreatedCount
        << ",\"skipUpdate\":" << sample.ObjectSkipUpdateCount
        << ",\"skipRender\":" << sample.ObjectSkipRenderCount
        << "}";

    aiPlayLog << ",\"specialObjects\":{\"fireballs\":{\"active\":" << sample.FireballsActiveCount
        << ",\"handler\":";
    WriteJsonHex(aiPlayLog, kFireballsHandlerAddr);
    aiPlayLog << ",\"handlerPtr\":";
    WriteJsonHex(aiPlayLog, sample.FireballsHandlerPtr);
    G.AIObservationRuntime.UpdateFireballHandler(instanceID, sample.FireballsHandlerPtr);
    int activeFireballSlots = 0;
    for (int i = 0; i < kAIFireballSlotCount; i++)
    {
        if (sample.FireballSlotActive[i] != 0)
            activeFireballSlots++;
        else
            G.AIObservationRuntime.InvalidateFireballOwner(instanceID, i);
    }
    aiPlayLog << ",\"activeSlots\":" << activeFireballSlots;
    aiPlayLog << ",\"words\":[";
    for (int i = 0; i < kAISpecialHandlerWordCount; i++)
    {
        if (i != 0)
            aiPlayLog << ",";
        WriteJsonHex(aiPlayLog, sample.FireballsHandlerWords[i]);
    }
    aiPlayLog << "],\"slots\":[";
    bool firstFireballSlot = true;
    for (int i = 0; i < kAIFireballSlotCount; i++)
    {
        if (sample.FireballSlotActive[i] == 0)
            continue;
        if (!firstFireballSlot)
            aiPlayLog << ",";
        firstFireballSlot = false;
        int ownerConfidence = 0;
        int ownerHeuristic = 0;
        int statelessOwnerCandidate = -1;
        int statelessOwnerConfidence = 0;
        int statelessOwnerHeuristic = 0;
        bool ownerTracked = false;
        const int ownerCandidate = AIFireballOwnerCandidate(
            instanceID,
            sample,
            i,
            ownerConfidence,
            ownerHeuristic,
            statelessOwnerCandidate,
            statelessOwnerConfidence,
            statelessOwnerHeuristic,
            ownerTracked);
        const bool sourceKindVerified = sample.FireballSlotKind[i] <= 3;
        aiPlayLog << "{\"index\":" << i
            << ",\"active\":" << sample.FireballSlotActive[i]
            << ",\"kind\":" << sample.FireballSlotKind[i]
            << ",\"sourceKind\":" << sample.FireballSlotKind[i]
            << ",\"kindName\":\""
            << (sample.FireballSlotKind[i] == 0 ? "player0" :
                    sample.FireballSlotKind[i] == 1 ? "player1" :
                    sample.FireballSlotKind[i] == 2 ? "piranha_plant" :
                    sample.FireballSlotKind[i] == 3 ? "fire_bro" : "unknown")
            << "\""
            << ",\"state\":" << sample.FireballSlotState[i]
            << ",\"facing\":" << sample.FireballSlotFacing[i]
            << ",\"ownerCandidate\":" << ownerCandidate
            << ",\"ownerConfidence\":" << ownerConfidence
            << ",\"ownerHeuristic\":" << ownerHeuristic
            << ",\"ownerTracked\":" << (ownerTracked ? 1 : 0)
            << ",\"ownerSource\":\"" << (sourceKindVerified ? "slotKind" : "positionVelocityHeuristic") << "\""
            << ",\"statelessOwnerCandidate\":" << statelessOwnerCandidate
            << ",\"statelessOwnerConfidence\":" << statelessOwnerConfidence
            << ",\"statelessOwnerHeuristic\":" << statelessOwnerHeuristic
            << ",\"ownerVerified\":" << (sourceKindVerified ? 1 : 0)
            << ",\"stateBytesOffset\":";
        WriteJsonHex(aiPlayLog, kAIFireballSlotActiveOffset, 2);
        aiPlayLog << ",\"stateBytes\":[";
        for (int j = 0; j < kAIFireballSlotStateByteCount; j++)
        {
            if (j != 0)
                aiPlayLog << ",";
            aiPlayLog << sample.FireballSlotStateBytes[i][j];
        }
        aiPlayLog << "],\"debugWordsOffset\":";
        WriteJsonHex(aiPlayLog, kAIFireballSlotDebugWordOffset, 2);
        aiPlayLog << ",\"debugWords\":[";
        for (int j = 0; j < kAIFireballSlotDebugWordCount; j++)
        {
            if (j != 0)
                aiPlayLog << ",";
            WriteJsonHex(aiPlayLog, sample.FireballSlotDebugWords[i][j]);
        }
        aiPlayLog << "]"
            << ",";
        WriteAIVec3Json(
            aiPlayLog,
            "pos",
            sample.FireballSlotPosX[i],
            sample.FireballSlotPosY[i],
            sample.FireballSlotPosZ[i]);
        aiPlayLog << ",";
        WriteAIVec3Json(
            aiPlayLog,
            "prev",
            sample.FireballSlotPrevX[i],
            sample.FireballSlotPrevY[i],
            sample.FireballSlotPrevZ[i]);
        aiPlayLog << ",";
        WriteAIVec3Json(
            aiPlayLog,
            "vel",
            sample.FireballSlotVelX[i],
            sample.FireballSlotVelY[i],
            sample.FireballSlotVelZ[i]);
        aiPlayLog << ",\"relative\":{\"p0dx\":"
            << AIWrappedDeltaX(SignedU32(sample.FireballSlotPosX[i]), SignedU32(sample.PlayerActor0PosX))
            << ",\"p0dy\":" << (SignedU32(sample.FireballSlotPosY[i]) - SignedU32(sample.PlayerActor0PosY))
            << ",\"p1dx\":" << AIWrappedDeltaX(SignedU32(sample.FireballSlotPosX[i]), SignedU32(sample.PlayerActor1PosX))
            << ",\"p1dy\":" << (SignedU32(sample.FireballSlotPosY[i]) - SignedU32(sample.PlayerActor1PosY))
            << "}}";
    }
    aiPlayLog << "]},\"projectiles\":{\"handler\":";
    WriteJsonHex(aiPlayLog, kProjectilesHandlerAddr);
    aiPlayLog << ",\"words\":[";
    for (int i = 0; i < kAISpecialHandlerWordCount; i++)
    {
        if (i != 0)
            aiPlayLog << ",";
        WriteJsonHex(aiPlayLog, sample.ProjectilesHandlerWords[i]);
    }
    aiPlayLog << "]}}";

    WriteAIVisualSummaryJson(aiPlayLog, objectScanCache, sample);

    aiPlayLog << ",\"objects\":[";
    int writtenObjects = 0;
    for (const GameStateObjectScanEntry& entry : objectScanCache.Entries)
    {
        if (entry.LifecycleState != 1)
            continue;
        if (writtenObjects >= G.Diagnostics.AIPlayLogMaxObjects)
            break;
        if (writtenObjects != 0)
            aiPlayLog << ",";
        WriteAIObjectJson(aiPlayLog, entry, sample);
        writtenObjects++;
    }
    aiPlayLog << "],\"hash\":";
    WriteJsonHex(aiPlayLog, static_cast<melonDS::u32>(sample.Hash & 0xFFFFFFFFull));
    aiPlayLog << "}\n";
    G.AIObservationRuntime.RecordLogLine(
        AIObservation::LogKind::V1,
        G.Diagnostics.AIPlayLogFlushInterval);
}

