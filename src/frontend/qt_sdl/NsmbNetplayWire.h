#ifndef NSMBNETPLAYWIRE_H
#define NSMBNETPLAYWIRE_H

#include "types.h"

#include <cstddef>

namespace NsmbNetplayPoC::WireProtocol {

constexpr melonDS::u32 kMagic = 0x4C4D534E; // "NSML", little endian
constexpr melonDS::u32 kVersion = 1;
constexpr melonDS::u32 kWireKindState = 0x54415453;  // "STAT", little endian
constexpr melonDS::u32 kWireKindPacket = 0x4B434150; // "PACK", little endian
constexpr melonDS::u32 kWireKindPlayerState =
    0x41545350; // "PSTA", little endian
constexpr melonDS::u32 kWireKindWorldState =
    0x41545357; // "WSTA", little endian
constexpr melonDS::u32 kWireKindMovingHazardState =
    0x415A4148; // "HAZA", little endian
constexpr melonDS::u32 kWireKindWorldEffectState =
    0x54434645; // "EFCT", little endian
constexpr melonDS::u32 kWireKindWorldActorSnapshot =
    0x54434157; // "WACT", little endian
constexpr std::size_t kMaxWorldMovingHazards = 4;
constexpr std::size_t kMaxWorldEffects = 4;
constexpr std::size_t kMaxWorldActorSnapshots = 16;
constexpr std::size_t kWorldEffectWordCount = 43;

struct WireNSMLPacket {
  melonDS::u32 Magic;
  melonDS::u32 Version;
  melonDS::u32 Kind;
  melonDS::u32 Frame;
  melonDS::u32 Player;
  melonDS::u32 Tick;
  melonDS::u8 Packet[52];
};

static_assert(sizeof(WireNSMLPacket) == 76);

struct WireGameState {
  melonDS::u32 Magic;
  melonDS::u32 Version;
  melonDS::u32 Kind;
  melonDS::u32 Frame;
  melonDS::u32 Instance;
  melonDS::u32 StageID;
  melonDS::u32 StageGroup;
  melonDS::u32 VsMode;
  melonDS::u32 LocalPlayerID;
  melonDS::u32 GGID;
  melonDS::u32 NetRandomValue;
  melonDS::u32 NetRandomCallCount;
  melonDS::u32 NetRandomBranchAddress;
  melonDS::u32 VsStarFound;
  melonDS::u32 VsStarGUID;
  melonDS::u32 VsStarBase;
  melonDS::u32 VsStarSettings;
  melonDS::u32 VsStarStateType;
  melonDS::u32 VsStarFlags;
  melonDS::u32 VsStarPosX;
  melonDS::u32 VsStarPosY;
  melonDS::u32 VsStarPosZ;
  melonDS::u32 VsStarActorFound;
  melonDS::u32 VsStarActorGUID;
  melonDS::u32 VsStarActorBase;
  melonDS::u32 VsStarActorSettings;
  melonDS::u32 VsStarActorStateType;
  melonDS::u32 VsStarActorFlags;
  melonDS::u32 VsStarActorPosX;
  melonDS::u32 VsStarActorPosY;
  melonDS::u32 VsStarActorPosZ;
  melonDS::u32 PlayerActor0Found;
  melonDS::u32 PlayerActor0GUID;
  melonDS::u32 PlayerActor0Settings;
  melonDS::u32 PlayerActor0PosX;
  melonDS::u32 PlayerActor0PosY;
  melonDS::u32 PlayerActor0PosZ;
  melonDS::u32 PlayerActor0PrevX;
  melonDS::u32 PlayerActor0PrevY;
  melonDS::u32 PlayerActor0PrevZ;
  melonDS::u32 PlayerActor0VelX;
  melonDS::u32 PlayerActor0VelY;
  melonDS::u32 PlayerActor0VelZ;
  melonDS::u32 PlayerActor1Found;
  melonDS::u32 PlayerActor1GUID;
  melonDS::u32 PlayerActor1Settings;
  melonDS::u32 PlayerActor1PosX;
  melonDS::u32 PlayerActor1PosY;
  melonDS::u32 PlayerActor1PosZ;
  melonDS::u32 PlayerActor1PrevX;
  melonDS::u32 PlayerActor1PrevY;
  melonDS::u32 PlayerActor1PrevZ;
  melonDS::u32 PlayerActor1VelX;
  melonDS::u32 PlayerActor1VelY;
  melonDS::u32 PlayerActor1VelZ;
  melonDS::u32 PlayerCount;
  melonDS::u32 Player0BattleStars;
  melonDS::u32 Player1BattleStars;
  melonDS::u32 Player0Coins;
  melonDS::u32 Player1Coins;
  melonDS::u32 Player0Score;
  melonDS::u32 Player1Score;
  melonDS::u32 Player0DisplayedStars;
  melonDS::u32 Player1DisplayedStars;
  melonDS::u32 Player0Deaths;
  melonDS::u32 Player1Deaths;
  melonDS::u32 Player0CollectedStars;
  melonDS::u32 Player1CollectedStars;
  melonDS::u32 VsCoinCount;
  melonDS::u32 StageCameraFound;
  melonDS::u32 StageCameraWord190;
  melonDS::u32 StageCameraWord194;
  melonDS::u32 StageCameraWord19C;
  melonDS::u32 StageCameraWord1A0;
  melonDS::u32 StageSceneFound;
  melonDS::u32 StageSceneWord154;
  melonDS::u32 StageSceneWord160;
  melonDS::u32 MovingHazardFound;
  melonDS::u32 MovingHazardGUID;
  melonDS::u32 MovingHazardSettings;
  melonDS::u32 MovingHazardStateType;
  melonDS::u32 MovingHazardFlags;
  melonDS::u32 MovingHazardPosX;
  melonDS::u32 MovingHazardPosY;
  melonDS::u32 MovingHazardPosZ;
  melonDS::u32 MovingHazardVelX;
  melonDS::u32 MovingHazardVelY;
  melonDS::u32 BasicHashLo;
  melonDS::u32 BasicHashHi;
  melonDS::u32 PlayerGlobalHashLo;
  melonDS::u32 PlayerGlobalHashHi;
  melonDS::u32 WifiCandidateHashLo;
  melonDS::u32 WifiCandidateHashHi;
  melonDS::u32 RenderCandidateHashLo;
  melonDS::u32 RenderCandidateHashHi;
};

static_assert(sizeof(WireGameState) == 380);

struct WirePlayerState {
  melonDS::u32 Magic;
  melonDS::u32 Version;
  melonDS::u32 Kind;
  melonDS::u32 Frame;
  melonDS::u32 Instance;
  melonDS::u32 Player;
  melonDS::u32 Found;
  melonDS::u32 GUID;
  melonDS::u32 Settings;
  melonDS::u32 StateType;
  melonDS::u32 Flags;
  melonDS::u32 PosX;
  melonDS::u32 PosY;
  melonDS::u32 PosZ;
  melonDS::u32 PrevX;
  melonDS::u32 PrevY;
  melonDS::u32 PrevZ;
  melonDS::u32 VelX;
  melonDS::u32 VelY;
  melonDS::u32 VelZ;
  melonDS::u32 ActionFlag;
  melonDS::u32 SubActionFlag;
  melonDS::u32 PhysicsFlag;
  melonDS::u32 DamageCooldown;
  melonDS::u32 TransitionFlag;
  melonDS::u32 CollisionFlag;
  melonDS::u32 EnvironmentFlag;
  melonDS::u32 RuntimeFlags0;
  melonDS::u32 RuntimeFlags1;
  melonDS::u32 PlayerCount;
  melonDS::u32 Powerup;
  melonDS::u32 InventoryPowerup;
  melonDS::u32 Dead;
  melonDS::u32 Character;
  melonDS::u32 TransitionStatus;
  melonDS::u32 Lives;
  melonDS::u32 BattleStars;
  melonDS::u32 Coins;
  melonDS::u32 Score;
  melonDS::u32 DisplayedStars;
  melonDS::u32 Deaths;
  melonDS::u32 CollectedStars;
};

static_assert(sizeof(WirePlayerState) == 168);

struct WireWorldActorState {
  melonDS::u32 Found;
  melonDS::u32 GUID;
  melonDS::u32 Settings;
  melonDS::u32 StateType;
  melonDS::u32 Flags;
  melonDS::u32 PosX;
  melonDS::u32 PosY;
  melonDS::u32 PosZ;
  melonDS::u32 PrevX;
  melonDS::u32 PrevY;
  melonDS::u32 PrevZ;
  melonDS::u32 VelX;
  melonDS::u32 VelY;
  melonDS::u32 VelZ;
  melonDS::u32 LastStepX;
  melonDS::u32 LastStepY;
  melonDS::u32 LastStepZ;
  melonDS::u32 VelH;
  melonDS::u32 TargetVelH;
  melonDS::u32 AccelV;
  melonDS::u32 TargetVelV;
  melonDS::u32 AccelH;
  melonDS::u32 TargetVelX;
  melonDS::u32 TargetVelY;
  melonDS::u32 TargetVelZ;
};

struct WireWorldState {
  melonDS::u32 Magic;
  melonDS::u32 Version;
  melonDS::u32 Kind;
  melonDS::u32 Frame;
  melonDS::u32 Instance;
  WireWorldActorState Star;
  WireWorldActorState NeutralItem;
  WireWorldActorState Item;
  WireWorldActorState DroppedStarItem;
  WireWorldActorState MovingHazard;
};

static_assert(sizeof(WireWorldActorState) == 100);
static_assert(sizeof(WireWorldState) == 520);

struct WireMovingHazardState {
  melonDS::u32 Magic;
  melonDS::u32 Version;
  melonDS::u32 Kind;
  melonDS::u32 Frame;
  melonDS::u32 Instance;
  melonDS::u32 Count;
  WireWorldActorState Actors[kMaxWorldMovingHazards];
};

static_assert(sizeof(WireMovingHazardState) == 424);

struct WireWorldObjectActorState {
  melonDS::u32 ObjectID;
  WireWorldActorState Actor;
};

struct WireWorldActorSnapshotState {
  melonDS::u32 Magic;
  melonDS::u32 Version;
  melonDS::u32 Kind;
  melonDS::u32 Frame;
  melonDS::u32 Instance;
  melonDS::u32 Count;
  WireWorldObjectActorState Actors[kMaxWorldActorSnapshots];
};

static_assert(sizeof(WireWorldObjectActorState) == 104);
static_assert(sizeof(WireWorldActorSnapshotState) == 1688);

struct WireWorldEffectSlot {
  melonDS::u32 Found;
  melonDS::u32 Base;
  melonDS::u32 VTable;
  melonDS::u32 Words[kWorldEffectWordCount];
};

struct WireWorldEffectState {
  melonDS::u32 Magic;
  melonDS::u32 Version;
  melonDS::u32 Kind;
  melonDS::u32 Frame;
  melonDS::u32 Instance;
  melonDS::u32 Count;
  WireWorldEffectSlot Effects[kMaxWorldEffects];
};

static_assert(sizeof(WireWorldEffectSlot) == 184);
static_assert(sizeof(WireWorldEffectState) == 760);

} // namespace NsmbNetplayPoC::WireProtocol

#endif
