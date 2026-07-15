#include "NsmbGameStateSync.h"

#include "NsmbGameStateReader.h"
#include "NsmbGameStateWriter.h"
#include "NsmbRollbackRuntime.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#include "NDS.h"

namespace NsmbMvlNetplay::GameStateSync {
namespace {

constexpr melonDS::u32 kMainRAMBase = 0x02000000;
constexpr melonDS::u32 kNetRandomValueAddr = 0x02088A68;
constexpr melonDS::u32 kNetStateBaseAddr = 0x020887E8;
constexpr melonDS::u32 kGamePlayerGlobalBlockAddr = 0x0208B324;
constexpr melonDS::u32 kGameCandidateWifiBlockAddr = 0x0208B7A0;
constexpr melonDS::u32 kGameCandidateRenderBlockAddr = 0x023F8300;
constexpr melonDS::u16 kMovingHazardObjectID = 0x0053;
constexpr melonDS::u32 kMovingHazardSettings = 0x00000000;
constexpr melonDS::u16 kKoopaTroopaObjectID = 0x005E;
constexpr melonDS::u16 kBattleStarCandidateObjectID = 0x010C;
constexpr melonDS::u32 kEffectVTableStart = 0x02126A24;
constexpr melonDS::u32 kEffectVTablePtr = 0x02126A2C;
constexpr melonDS::u32 kWorldEffectSlotBase = 0x021C3268;
constexpr melonDS::u32 kWorldEffectSlotStride = 0x1D4;
constexpr melonDS::u32 kWorldEffectSlotCount = 32;

unsigned long long NowUnixMs() {
  return static_cast<unsigned long long>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

const char *RoleName(Context context) {
  return context.Client ? "client" : "host";
}

void TraceMovingHazards(Context context, int instanceID, melonDS::u32 frame,
                        melonDS::NDS *nds) {
  if (!context.StateSync.WorldTraceMovingHazards || instanceID < 0 ||
      instanceID >= 16 || frame % 60 != 0 ||
      context.Runtime.LastTracedWorldMovingHazardsFrame[instanceID] == frame)
    return;
  context.Runtime.LastTracedWorldMovingHazardsFrame[instanceID] = frame;
  const std::vector<GameStateReader::ObjectScanSample> actors =
      GameStateReader::FindActiveObjectsByIDAndSettings(
          nds, kMovingHazardObjectID, kMovingHazardSettings);
  std::printf("NSMB WorldHazards: role=%s inst=%d frame=%u count=%zu",
              RoleName(context), instanceID, frame, actors.size());
  for (std::size_t index = 0; index < actors.size(); index++)
    std::printf(" slot%zu=%u/%08X/%08X", index, actors[index].GUID,
                actors[index].PosX, actors[index].PosY);
  std::printf("\n");
}

bool ShouldTraceActorInternals(melonDS::u16 objectID, melonDS::u32 vtable) {
  return objectID == kMovingHazardObjectID ||
         objectID == kKoopaTroopaObjectID ||
         objectID == kBattleStarCandidateObjectID ||
         vtable == kEffectVTablePtr || vtable == kEffectVTableStart;
}

void PrintActorInternalWords(Context context, const char *prefix,
                             int instanceID, melonDS::u32 frame,
                             melonDS::NDS *nds, melonDS::u32 base,
                             melonDS::u32 guid, melonDS::u16 objectID,
                             melonDS::u32 settings, melonDS::u32 vtable) {
  std::printf("%s: role=%s inst=%d frame=%u guid=%u object=%03X "
              "settings=%08X vtable=%08X base=%08X words=",
              prefix, RoleName(context), instanceID, frame, guid, objectID,
              settings, vtable, base);
  for (melonDS::u32 offset = 0; offset <= 0x1FC;
       offset += sizeof(melonDS::u32)) {
    melonDS::u32 value = 0;
    RollbackRuntime::ReadMainRAMAddressU32(nds, base + offset, value);
    std::printf("%s%02X:%08X", offset == 0 ? "" : "/", offset, value);
  }
  std::printf("\n");
}

bool IsWorldTraceFrame(Context context, melonDS::u32 frame) {
  return frame >= context.StateSync.WorldTraceObjectLifecyclesStartFrame &&
         (context.StateSync.WorldTraceObjectLifecyclesEndFrame == 0 ||
          frame <= context.StateSync.WorldTraceObjectLifecyclesEndFrame) &&
         frame % static_cast<melonDS::u32>(
                     context.StateSync.WorldTraceObjectLifecyclesInterval) ==
             0;
}

void TraceEffects(Context context, int instanceID, melonDS::u32 frame,
                  melonDS::NDS *nds) {
  if (!context.StateSync.WorldTraceEffects || !nds || !nds->MainRAM ||
      instanceID < 0 || instanceID >= 16 ||
      !IsWorldTraceFrame(context, frame) ||
      context.Runtime.LastTracedWorldEffectsFrame[instanceID] == frame)
    return;
  context.Runtime.LastTracedWorldEffectsFrame[instanceID] = frame;

  melonDS::u32 count = 0;
  for (melonDS::u32 slotIndex = 0; slotIndex < kWorldEffectSlotCount;
       slotIndex++) {
    const melonDS::u32 base =
        kWorldEffectSlotBase + slotIndex * kWorldEffectSlotStride;
    GameStateReader::WorldEffectSlotSample slot{};
    if (!GameStateReader::ReadWorldEffectSlot(nds, base, slot))
      continue;
    PrintActorInternalWords(context, "NSMB WorldEffectInternals", instanceID,
                            frame, nds, base, 0, 0, 0, slot.VTable);
    if (++count >= 16)
      break;
  }
  if (count == 0)
    std::printf("NSMB WorldEffectInternals: role=%s inst=%d frame=%u count=0\n",
                RoleName(context), instanceID, frame);
}

void TraceObjectLifecycles(Context context, int instanceID, melonDS::u32 frame,
                           melonDS::NDS *nds) {
  if (!context.StateSync.WorldTraceObjectLifecycles || !nds || !nds->MainRAM ||
      instanceID < 0 || instanceID >= 16 ||
      !IsWorldTraceFrame(context, frame) ||
      context.Runtime.LastTracedWorldObjectLifecyclesFrame[instanceID] == frame)
    return;
  context.Runtime.LastTracedWorldObjectLifecyclesFrame[instanceID] = frame;

  struct LifecycleActor {
    melonDS::u32 VTable = 0;
    melonDS::u32 Base = 0;
    melonDS::u32 GUID = 0;
    melonDS::u32 Settings = 0;
    melonDS::u32 PosX = 0;
    melonDS::u32 PosY = 0;
    melonDS::u32 PosZ = 0;
    melonDS::u16 ObjectID = 0;
    melonDS::u8 State = 0;
    melonDS::u8 Type = 0;
    melonDS::u8 SkipFlags = 0;
  };

  std::vector<LifecycleActor> actors;
  const GameStateReader::GameStateObjectScanCache cache =
      GameStateReader::BuildGameStateObjectScanCache(nds);
  actors.reserve(cache.Entries.size());
  for (const GameStateReader::GameStateObjectScanEntry &entry : cache.Entries) {
    LifecycleActor actor{
        entry.VTable,         entry.Actor.Base, entry.Actor.GUID,
        entry.Actor.Settings, entry.Actor.PosX, entry.Actor.PosY,
        entry.Actor.PosZ,     entry.ObjectID,   entry.LifecycleState,
        entry.Type,           entry.SkipFlags};
    if (context.StateSync.WorldTraceActorInternals &&
        ShouldTraceActorInternals(entry.ObjectID, entry.VTable))
      PrintActorInternalWords(context, "NSMB WorldActorInternals", instanceID,
                              frame, nds, actor.Base, actor.GUID,
                              actor.ObjectID, actor.Settings, actor.VTable);
    if (actor.State != 0 && actor.State <= 2 && actor.Type <= 2)
      actors.push_back(actor);
  }
  std::sort(actors.begin(), actors.end(),
            [](const LifecycleActor &lhs, const LifecycleActor &rhs) {
              if (lhs.State != rhs.State)
                return lhs.State < rhs.State;
              if (lhs.ObjectID != rhs.ObjectID)
                return lhs.ObjectID < rhs.ObjectID;
              return lhs.GUID < rhs.GUID;
            });
  std::printf("NSMB WorldObjects: role=%s inst=%d frame=%u count=%zu",
              RoleName(context), instanceID, frame, actors.size());
  for (const LifecycleActor &actor : actors)
    std::printf(" actor=%u/%03X/%08X/%u/%u/%02X/%08X/%08X/%08X/%08X/%08X",
                actor.GUID, actor.ObjectID, actor.Settings, actor.State,
                actor.Type, actor.SkipFlags, actor.VTable, actor.Base,
                actor.PosX, actor.PosY, actor.PosZ);
  std::printf("\n");
}

} // namespace

GameStateModel::GameStateSample ReadSample(melonDS::NDS *nds,
                                           melonDS::u32 stageSceneSettings) {
  using namespace GameStateModel;
  GameStateSample sample;
  if (!nds || !nds->MainRAM)
    return sample;

  const GameStateReader::GameStateObjectScanCache objectScanCache =
      GameStateReader::BuildGameStateObjectScanCache(nds);
  const GameStateReader::ScopedGameStateObjectScanCache scopedObjectScanCache(
      objectScanCache);
  GameStateReader::ReadCoreState(nds, sample);
  GameStateReader::ReadBattleStarState(nds, sample);

  const GameStateReader::PlayerActorScanSample players =
      GameStateReader::FindPlayerActors(nds);
  GameStateReader::CopyPlayerActor(players.Actor0, sample, 0);
  sample.PlayerActor0CollisionMgr =
      GameStateReader::ReadPlayerCollisionMgrSample(nds, players.Actor0);
  sample.PlayerActor0Hitbox =
      GameStateReader::ReadPlayerHitboxSample(nds, players.Actor0);
  sample.PlayerActor0TileProbe =
      GameStateReader::ReadAIPlayerTileProbeSample(nds, players.Actor0);
  GameStateReader::ReadPlayerTileDamage(nds, players.Actor0, sample, 0);
  GameStateReader::CopyPlayerActor(players.Actor1, sample, 1);
  sample.PlayerActor1CollisionMgr =
      GameStateReader::ReadPlayerCollisionMgrSample(nds, players.Actor1);
  sample.PlayerActor1Hitbox =
      GameStateReader::ReadPlayerHitboxSample(nds, players.Actor1);
  sample.PlayerActor1TileProbe =
      GameStateReader::ReadAIPlayerTileProbeSample(nds, players.Actor1);
  GameStateReader::ReadPlayerTileDamage(nds, players.Actor1, sample, 1);
  GameStateReader::ReadPlayerTransitionState(nds, players.Actor0, sample, 0);
  GameStateReader::ReadPlayerTransitionState(nds, players.Actor1, sample, 1);
  GameStateReader::ReadPlayerBaseRuntimeState(nds, players.Actor0, sample, 0);
  GameStateReader::ReadPlayerBaseRuntimeState(nds, players.Actor1, sample, 1);
  GameStateReader::ReadPlayerAndCameraGlobals(nds, sample);
  GameStateReader::ReadStageObjectState(nds, stageSceneSettings, sample);
  sample.Hash = ComputeBasicGameStateHash(sample);
  return sample;
}

void HandleReceivedPacketLocked(Context context, const Hooks &hooks,
                                const void *data, std::size_t size) {
  WireProtocol::WireGameState packet;
  std::memcpy(&packet, data, size);
  GameStateModel::DecodedGameState decoded;
  if (!GameStateModel::DecodeWireGameState(packet, decoded))
    return;
  const auto mismatch = context.Runtime.RecordRemoteGameState(decoded);
  if (mismatch)
    hooks.ReportMismatchLocked(*mismatch);
}

void ApplyRemote(Context context, const Hooks &hooks, int instanceID,
                 melonDS::u32 frame, melonDS::NDS *nds) {
  if (!context.Enabled || !context.StateSync.GameApplyEnabled ||
      (!context.StateSync.GameApplyRemotePlayerOnly && !context.Client) ||
      instanceID < 0 || instanceID >= 16 || !nds || !nds->MainRAM ||
      frame < context.Connection.StartFrame)
    return;

  GameStateModel::GameStateSample sample;
  melonDS::u32 sampleFrame = 0;
  {
    std::lock_guard<std::mutex> lock(context.Mutex);
    hooks.PumpNetworkLocked();
    if (!context.Runtime.RemoteState.FindLatestGameState(instanceID, frame,
                                                         sample, sampleFrame))
      return;
  }

  GameStateWriter::GameStateApplyOptions options;
  options.RemotePlayerOnly = context.StateSync.GameApplyRemotePlayerOnly;
  if (options.RemotePlayerOnly)
    options.RemotePlayer = hooks.CurrentLocalPlayer() ^ 1;
  options.CriticalGlobals = context.StateSync.GameApplyCriticalGlobals;
  options.StageObjects = context.StateSync.GameApplyStageObjects;
  options.StarObjects = context.StateSync.GameApplyStarObjects;
  options.PlayerActors = context.StateSync.GameApplyPlayerActors;
  options.StageSceneSettings = context.StageSceneSettings;
  const GameStateWriter::GameStateApplyResult result =
      GameStateWriter::ApplyGameState(nds, sample, options);

  const bool traceFrame =
      context.Bootstrap.InputTraceInterval <= 1 ||
      frame % static_cast<melonDS::u32>(context.Bootstrap.InputTraceInterval) ==
          0;
  if (options.RemotePlayerOnly) {
    if ((context.Bootstrap.InputTraceEnabled || context.Input.NetplayTrace) &&
        traceFrame) {
      std::printf("NSMB MvL Netplay: applied remote-player snapshot inst=%d frame=%u "
                  "sampleFrame=%u remotePlayer=%d applied=%d\n",
                  instanceID, frame, sampleFrame, options.RemotePlayer,
                  result.RemotePlayerApplied ? 1 : 0);
    }
    return;
  }
  if (context.Bootstrap.InputTraceEnabled && traceFrame)
    std::printf("NSMB MvL Netplay: applied remote game state inst=%d frame=%u "
                "sampleFrame=%u\n",
                instanceID, frame, sampleFrame);
}

void UpdateHangSnapshot(Context context, int instanceID, melonDS::u32 frame,
                        melonDS::NDS *nds) {
  if (!context.Diagnostics.HangDiagnosticsEnabled || !nds || !nds->MainRAM ||
      instanceID < 0 || instanceID >= 16)
    return;
  context.DiagnosticsRuntime.UpdateGameSnapshot(
      instanceID, frame, ReadSample(nds, context.StageSceneSettings),
      NowUnixMs());
}

void Trace(Context context, int instanceID, melonDS::u32 frame,
           melonDS::NDS *nds) {
  if (context.Diagnostics.GameStateTracePath.empty() || !nds || !nds->MainRAM ||
      instanceID < 0 || instanceID >= 16 ||
      frame < context.Diagnostics.GameStateTraceStartFrame ||
      (context.Diagnostics.GameStateTraceEndFrame != 0 &&
       frame > context.Diagnostics.GameStateTraceEndFrame) ||
      frame % static_cast<melonDS::u32>(
                  context.Diagnostics.GameStateTraceInterval) !=
          0 ||
      (kNetRandomValueAddr - kMainRAMBase) + sizeof(melonDS::u32) >
          nds->MainRAMMask + 1)
    return;

  const GameStateModel::GameStateSample sample =
      ReadSample(nds, context.StageSceneSettings);
  std::lock_guard<std::mutex> lock(context.Mutex);
  if (!context.TraceWriter.IsOpen())
    return;

  GameStateModel::GameStateTraceHashes traceHashes;
  const GameStateModel::GameStateTraceHashes *extendedHashes = nullptr;
  if (context.Diagnostics.GameStateTraceExtended) {
    traceHashes.PlayerGlobal = GameStateReader::HashMainRAMRange(
        nds, kGamePlayerGlobalBlockAddr, 0xC0);
    traceHashes.WifiCandidate = GameStateReader::HashMainRAMRange(
        nds, kGameCandidateWifiBlockAddr, 0x2200);
    traceHashes.RenderCandidate = GameStateReader::HashMainRAMRange(
        nds, kGameCandidateRenderBlockAddr, 0x240);
    traceHashes.NetState =
        GameStateReader::HashMainRAMRange(nds, kNetStateBaseAddr, 0x180);
    extendedHashes = &traceHashes;
  }
  context.TraceWriter.Write(instanceID, frame, sample, extendedHashes);
}

void TraceWorld(Context context, int instanceID, melonDS::u32 frame,
                melonDS::NDS *nds) {
  TraceMovingHazards(context, instanceID, frame, nds);
  TraceObjectLifecycles(context, instanceID, frame, nds);
  TraceEffects(context, instanceID, frame, nds);
}

void Sync(Context context, const Hooks &hooks, int instanceID,
          melonDS::u32 frame, melonDS::NDS *nds) {
  if (!context.Enabled || !context.StateSync.GameEnabled || !nds ||
      instanceID < 0 || instanceID >= 16 ||
      frame < context.Connection.StartFrame ||
      frame % static_cast<melonDS::u32>(context.StateSync.GameInterval) != 0)
    return;

  const GameStateModel::GameStateSample sample =
      ReadSample(nds, context.StageSceneSettings);
  GameStateModel::GameStateSyncHashes hashes;
  hashes.Basic = sample.Hash;
  if (context.StateSync.GameExtended) {
    hashes.PlayerGlobal = GameStateReader::HashMainRAMRange(
        nds, kGamePlayerGlobalBlockAddr, 0xC0);
    hashes.WifiCandidate = GameStateReader::HashMainRAMRange(
        nds, kGameCandidateWifiBlockAddr, 0x2200);
    hashes.RenderCandidate = GameStateReader::HashMainRAMRange(
        nds, kGameCandidateRenderBlockAddr, 0x240);
  }

  std::lock_guard<std::mutex> lock(context.Mutex);
  if (!context.Runtime.BeginGameStateSync(instanceID, frame))
    return;
  const auto mismatch =
      context.Runtime.RecordLocalGameStateHashes(instanceID, frame, hashes);
  if (mismatch)
    hooks.ReportMismatchLocked(*mismatch);
  if (!context.Transport.IsConnected())
    return;

  const WireProtocol::WireGameState packet =
      GameStateModel::EncodeWireGameState(
          frame, static_cast<melonDS::u32>(instanceID), sample, hashes);
  context.Transport.Send(&packet, sizeof(packet), ENET_PACKET_FLAG_RELIABLE,
                         false);
}

} // namespace NsmbMvlNetplay::GameStateSync
