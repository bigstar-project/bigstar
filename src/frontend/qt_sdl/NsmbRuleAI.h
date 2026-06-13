/*
    Rule-based NSMB Mario vs Luigi CPU input for the experimental PoC path.
*/

#ifndef NSMBRULEAI_H
#define NSMBRULEAI_H

#include "NsmbNetplayPoC.h"

#include <cstdint>
#include <string>

namespace NsmbRuleAI
{

struct Config
{
    bool Enabled = false;
    std::string PlayerSpec = "remote";
    melonDS::u32 StartFrame = 0;
    int HorizontalDeadzone = 0x4000;
    int HorizontalWrapWidth = 0x400000;
    int CloseRange = 0x22000;
    int HazardHorizontalRange = 0x40000;
    int HazardVerticalRange = 0x50000;
    int JumpInterval = 42;
    int JumpFrames = 9;
    int WallEscapeFrames = 36;
    int StuckFrames = 24;
    bool TraceEnabled = false;
    int TraceInterval = 60;
};

struct PlayerFrameState
{
    bool Found = false;
    melonDS::u32 X = 0;
    melonDS::u32 Y = 0;
    std::int32_t VelX = 0;
    bool Dead = false;
    melonDS::u32 BattleStars = 0;
    bool GroundBelowSolid = false;
    bool BlockedAhead = false;
    bool HoleAhead = false;
    bool BlockedLeft = false;
    bool HoleLeft = false;
    bool BlockedRight = false;
    bool HoleRight = false;
    bool HazardFound = false;
    bool HazardClosing = false;
    bool HazardVeryClose = false;
    std::int32_t HazardDx = 0;
    std::int32_t HazardDy = 0;
    std::int32_t HazardVelX = 0;
    std::int32_t HazardVelY = 0;
    int HazardCategoryID = 0;
};

struct FrameState
{
    bool InGameplay = false;
    PlayerFrameState Players[2] {};
    bool StarFound = false;
    melonDS::u32 StarX = 0;
    melonDS::u32 StarY = 0;
    bool StarActorFound = false;
    melonDS::u32 StarActorX = 0;
    melonDS::u32 StarActorY = 0;
    bool MovingHazardFound = false;
    melonDS::u32 MovingHazardX = 0;
    melonDS::u32 MovingHazardY = 0;
    melonDS::u32 MovingHazardVelX = 0;
    melonDS::u32 MovingHazardVelY = 0;
};

bool ControlsPlayer(const Config& config, int player, int localPlayer);

NsmbNetplayPoC::InputState DecideInput(
    const Config& config,
    const FrameState& state,
    int instanceID,
    melonDS::u32 frame,
    int player,
    int localPlayer,
    const NsmbNetplayPoC::InputState& fallback);

}

#endif
