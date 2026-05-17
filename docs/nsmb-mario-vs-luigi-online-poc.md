# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード「Mario vs Luigi」を、最終的に WAN 越しの 2PC で遊べる形にする。

現在の本命方針は、melonDS の Local MP をそのまま WAN 越しに流すことではなく、NSMB が対戦中に扱うゲームレベルの 52 byte packet を特定し、1 EmuInstance * 2PC の構成でその packet を WAN 向けに中継・再生する方式。

## 現在の方針

1. 既存 melonDS の LAN MPInterface は、Mario vs Luigi 到達と packet 解析のためのテストハーネスとして使う。
2. 最終形では 2 EmuInstance * 2PC ではなく、1 EmuInstance * 2PC を目指す。
3. DS ローカル無線フレーム全体ではなく、NSMB の 52 byte gameplay packet を bridge する。
4. MP 通信を途中で止めても、NSMB 側の packet tick と remote packet replay が進み続けるかを検証する。

## 実装済み

- 日本版 `A2DJ` 向けの主要シンボルを移植済み。
  - `Net::getRandom()` / `Net::getRandom12()` / `Net::syncRandom*()`
  - `Net::getConsoleKeys()` / `Net::getPacketByte()` / `Net::getPacketTick()` / `Net::getPacketAction()`
  - `Net::Core::processSendPacket()` / `processRecvPacket()` 周辺
- 1 EmuInstance * 2プロセスで LAN MPInterface を使い、Mario vs Luigi へ自動到達する smoke script を追加済み。
  - `scripts/run-nsmb-mvl-lan-route-smoke.ps1`
- LAN MP payload trace から NSMB 52 byte packet を抽出する tooling を追加済み。
  - `tools/nsmb_localmp_packet_extract.py`
  - `tools/nsmb_packet_stream_compare.py`
- packet replay hook を追加済み。
  - `Net::getConsoleKeys()`
  - `Net::getPacketByte()`
  - `Net::getPacketTick()`
  - `Net::getPacketAction()`
- live packet bridge を追加済み。
  - `MELONDS_NSML_PACKET_BRIDGE=1`
  - `MELONDS_NSML_PACKET_BRIDGE_ONLY=1`
  - `MELONDS_NSML_PACKET_BRIDGE_REPLAY_TICK_OFFSET`
  - `MELONDS_NSML_PACKET_REPLAY_STRICT_PLAYERS`
  - `MELONDS_NSML_PACKET_REPLAY_STRICT_REQUIRE_LEAD`
- MP drop 実験フックを追加済み。
  - `MELONDS_NSML_DROP_MP_AFTER_FRAME`
- 今回追加した bridge 単独化向けフック。
  - `NSML_BuildMarioVsLuigiLocalPacket()`
  - `MELONDS_NSML_PACKET_BRIDGE_DIRECT_CAPTURE`
  - `MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK`
  - `MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK_START_FRAME`
  - `MELONDS_NSML_PACKET_BRIDGE_MAX_TICK_LEAD`
  - `MELONDS_NSML_PACKET_BRIDGE_THROTTLE_TIMEOUT_MS`
  - `MELONDS_NSML_PACKET_REPLAY_LIVE_FALLBACK_WINDOW`

## 直近の検証結果

ビルド:

```powershell
cmake --build build\debug-windows-x86_64 --target melonDS --config Debug
```

通常 smoke:

```powershell
.\scripts\run-nsmb-smoke.ps1 -Frames 600 -LogDir logs\single-smoke-after-packet-bridge-throttle
```

結果: pass。

MP drop 後の packet bridge 継続検証:

```powershell
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 7200 -HostStartupDelayMs 50 -LogDir logs\lan-route-7200-live-packet-bridge-dropmp3100-forcetick-direct-throttle4-fallback8 -GameStateTrace -GameStateTraceInterval 120 -PacketBridge -PacketBridgeTrace -PacketBridgeStartFrame 3000 -HostPacketBridgeReplayTickOffset 3 -ClientPacketBridgeReplayTickOffset 2 -PacketBridgeStrictRemote -PacketBridgeStrictRequireLead 3 -PacketBridgeLiveFallbackWindow 8 -DropMPAfterFrame 3100 -PacketBridgeDirectCapture -PacketBridgeForceTick -PacketBridgeForceTickStartFrame 3100 -PacketBridgeMaxTickLead 4 -PacketBridgeThrottleTimeoutMs 5000
```

結果:

- smoke pass。
- frame 3100 以降に MP send/recv を止めても、packet tick は停止せず 7200 frame まで進行。
- 終盤 20000 replay log 行で remote player の replay miss は host/client とも 0。
  - host remote player 1: hits 4865 / miss 0
  - client remote player 0: hits 4849 / miss 0
- host/client とも最終 frame 7200 で `stageGroup=0x9`, `vsMode=0x1` を維持。

重要な解釈:

- 以前の `DropMPAfterFrame 3100` では画面状態は維持しても `0x02087F00` packet tick が止まっていた。
- 今回の direct capture + force tick + tick lead throttle + live fallback により、少なくともテストハーネス上では MP 停止後も remote packet replay が継続できる状態まで進んだ。
- ただし、まだ LAN MPInterface で Mario vs Luigi 到達後に MP を drop する検証であり、完全な LAN-free 起動・WAN 対戦完成ではない。

## 現在の課題

- direct capture + force tick はまだ実験的。NSMB 本来の `Net::updatePacket()` / `processSendPacket()` / `processRecvPacket()` の副作用を完全に代替できているかは未確定。
- replay fallback は短時間の jitter/offset 吸収には効くが、packet が古すぎる場合の挙動や実プレイ入力での影響は未検証。
- 現在は LAN MPInterface で対戦状態に入った後に MP を drop している。次は「LAN MP なしで対戦状態を成立・維持できるか」を切り分ける必要がある。
- Star / 8コイン item / ランダム stage などのランダム要素は、最終的には同じ seed と同じ packet/tick 進行で一致させる必要がある。

## 次にやること

1. MP drop 後の replay hit だけでなく、remote player の座標・アニメーション・スター取得など、ゲーム上重要な状態が自然に進むかを検証する。
2. `processRecvPacket()` 側の副作用候補を追い、現在の replay hook だけで不足しているゲーム状態更新がないか調べる。
3. LAN MP で到達した直後に drop する段階から、drop frame をさらに早めて、どの初期化処理が必須かを特定する。
4. 最終的に、1 EmuInstance * 2PC で LAN MPInterface に依存しない packet bridge test route を作る。

## よく使うコマンド

```powershell
# build
cmake --build build\debug-windows-x86_64 --target melonDS --config Debug

# single smoke
.\scripts\run-nsmb-smoke.ps1 -Frames 600 -LogDir logs\single-smoke

# LAN route smoke
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 5100 -HostStartupDelayMs 50 -LogDir logs\lan-route-5100 -GameStateTrace -GameStateTraceInterval 60

# packet bridge + MP drop current best experiment
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 7200 -HostStartupDelayMs 50 -LogDir logs\lan-route-7200-live-packet-bridge-dropmp3100-forcetick-direct-throttle4-fallback8 -GameStateTrace -GameStateTraceInterval 120 -PacketBridge -PacketBridgeTrace -PacketBridgeStartFrame 3000 -HostPacketBridgeReplayTickOffset 3 -ClientPacketBridgeReplayTickOffset 2 -PacketBridgeStrictRemote -PacketBridgeStrictRequireLead 3 -PacketBridgeLiveFallbackWindow 8 -DropMPAfterFrame 3100 -PacketBridgeDirectCapture -PacketBridgeForceTick -PacketBridgeForceTickStartFrame 3100 -PacketBridgeMaxTickLead 4 -PacketBridgeThrottleTimeoutMs 5000
```

## ユーザー依存

- `roms/nsmb.nds` に日本版 `A2DJ` ROM が配置済み。
- WAN 実機検証に進む段階では、相手PC側にも同じ melonDS build、同じ ROM、同じ基本設定、別 MAC が必要。
