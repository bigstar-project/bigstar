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
  - `MELONDS_NSML_PACKET_REPLAY_OPS`
  - `MELONDS_NSML_PACKET_REPLAY_RETURN_LOOKUP_TICK`
- smoke script の視覚検証を強化済み。
  - post-start screenshot から黒背景 + 白文字の「通信が切断されました」画面を検出し、成功扱いしない。
  - `-ScreenshotInterval` でスクリーンショット間隔を細かくできる。
  - `-SkipDisconnectScreenshotCheck` は調査用にだけ使う。

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

MP drop 後の packet bridge 継続検証として以前使っていたコマンド:

```powershell
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 7200 -HostStartupDelayMs 50 -LogDir logs\lan-route-7200-live-packet-bridge-dropmp3100-forcetick-direct-throttle4-fallback8 -GameStateTrace -GameStateTraceInterval 120 -PacketBridge -PacketBridgeTrace -PacketBridgeStartFrame 3000 -HostPacketBridgeReplayTickOffset 3 -ClientPacketBridgeReplayTickOffset 2 -PacketBridgeStrictRemote -PacketBridgeStrictRequireLead 3 -PacketBridgeLiveFallbackWindow 8 -DropMPAfterFrame 3100 -PacketBridgeDirectCapture -PacketBridgeForceTick -PacketBridgeForceTickStartFrame 3100 -PacketBridgeMaxTickLead 4 -PacketBridgeThrottleTimeoutMs 5000
```

現在の評価:

- この検証は、視覚検証を入れる前は pass 扱いになっていたが、実際には「通信が切断されました」画面になっていたため成功ではない。
- 細かい screenshot 検証では、`PacketBridgeStrictRemote` を有効にすると host/client とも frame 3060 で切断画面に入る。
- `DropMPAfterFrame 3100` より前に切断しているため、現時点の主因は MP drop ではなく strict replay の remote packet 差し替え。
- packet bridge を動かすだけで strict replay しない場合は、3300 frame まで切断しない。ただしその場合は `getPacket*` の実戻り値は元のLAN処理に任せているため、bridgeがゲーム状態へ影響しているわけではない。
- `getPacketTick()` にpacket内tickではなくlookup tickを返す実験も、frame 3060 の切断を解消しなかった。
- op別切り分け結果:
  - `keys` のみ strict replay: 3300 frame まで切断なし。
  - `byte` のみ strict replay: 3300 frame まで切断なし。
  - `action` のみ strict replay: 3300 frame まで切断なし。
  - `tick` のみ strict replay: frame 3060 で切断。
  - `tick` のみ + lookup tick返却: frame 3060 で切断。
  - `keys,byte,action` strict replay、tickは元処理: 4200 frame まで切断なし。
  - `keys,byte,action` strict replay + `DropMPAfterFrame 3100`: frame 3360 で切断。

## 現在の課題

- strict replay で差し替えている live bridge packet が、NSMB が実LAN時に remote slot として読んでいる値と一致していない可能性が高い。
- `Net::getPacketTick()` は単純な戻り値hookでは代替できていない。tick helperの戻り値だけでなく、LAN/recv sequencer側の内部状態更新が通信継続判定に関わっている可能性が高い。
- MP drop後は、tickをhookしない構成でも切断する。LAN側のrecv副作用、最終受信時刻、ack/sequence更新などを別途再現する必要がある。
- `stageGroup=0x9` / `vsMode=0x1` は切断画面でもRAMに残ることがあるため、成功判定には使えるが十分ではない。スクリーンショット検証が必須。
- direct capture + force tick はまだ実験的。NSMB 本来の `Net::updatePacket()` / `processSendPacket()` / `processRecvPacket()` の副作用を完全に代替できているかは未確定。
- 現在は LAN MPInterface で対戦状態に入った後に packet replay を試している。次は「live bridge packet」と「実LANでhost/clientが見ているremote slot」の一致確認が必要。
- Star / 8コイン item / ランダム stage などのランダム要素は、最終的には同じ seed と同じ packet/tick 進行で一致させる必要がある。

## 次にやること

1. `Net::getPacketTick()` の呼び出し元と戻り値利用箇所を追い、どの条件で切断判定に入るか特定する。
2. `processRecvPacket()` / recv sequencer / ack / last receive tick らしき状態を追い、MP drop後に止まる必須副作用を見つける。
3. strict replay時のlive bridge packetと、実LAN時に `getPacket*` が読むremote slot値を比較する。
4. 切断画面検出を常に通した上で、LAN MPInterface依存を減らす packet bridge route を再設計する。

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
