# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード `Mario vs Luigi` を、最終的に WAN 越しの 2PC で遊べる形にする。

本命方針は、melonDS の Local MP をそのまま WAN に流すのではなく、NSMB が対戦中に扱うゲームレベルのpacketと接続状態を置換し、`1 EmuInstance * 2PC` で再現すること。

## 現在の結論

- 画面が `通信が切断されました` になっている場合は失敗扱い。RAM上の `stageGroup=9` / `vsMode=1` だけでは成功判定に使わない。
- 完全な黒画面も失敗扱い。以前の `DropMPAfterFrame 3600` + `PacketBridgeSuppressDisconnect` の 6000/6600 frame通過は、黒画面を検出できていなかったため成功扱いしない。
- `Net::getPacketTick()` をremote slotだけでstrict replayすると、player間tick比較が崩れてframe 3060付近で切断画面になる。
- LAN harness中の `getPacket(0/1)` はLocalMP受信slotを読むため、両player slotをbridge replay対象にする必要がある。
- 両player slotをstrict対象にし、`getPacketTick()` にlookup tickを返すと、LocalMPが生きている間は切断せずに進む。
- LocalMPを停止すると、NSMB内部のLocalMP packet slot statusが0になり、disconnect state/flagsが立つ。
- bridge中にdisconnect state/flagsを抑制する検証フックだけでは不十分。`DropMPAfterFrame 3600` 後、host/clientの進行フレーム差が大きくなり、片側が必要なremote packetを受け取れず黒画面へ落ちる。
- `DropMPAfterFrame 3600` 後はpacket slot/statusを維持しても、NSMBの低レベルNet/描画状態がリセットされる。MainRAM上のplayer/starは残るが、render/display側が黒画面またはHUDのみへ落ちる。
- `Net::update()` の切断分岐は A2DJ `0x02010174` 付近。ここをskipしてepilogueへ戻すmelonDS hookでは、DropMP後も黒画面/abortなしで4200 frameまで通る。ただし `Net::getPacket*()` 呼び出しは3840 frame付近で実質止まり、5200 frame以降の移動入力は反映されないため、対戦成立にはまだ足りない。
- `0x02087E20` を `0x0004` に戻して切断分岐前の判定を通す `force-active` 実験では、`getConsoleKeys()` は一部戻るが黒画面が再発する。現時点では成功ルートではなく、低レベルtransfer失敗の切り分け用。
- LocalMPを接続維持用に残し、ゲームが読むpacketだけをstrict bridgeで置換する経路では、movement script込みで 6600 frame まで通り、host/clientのplayer座標とstar座標は一致した。
- packet traceではmovement開始後、host local packet keys=`0x0010`、client local packet keys=`0x0020` が毎frame送受信されている。入力packetはbridge上を流れている。
- `-PacketBridgeMaxFrameLead` は同期精度と速度のトレードオフ。`4` は同期は強いが遅すぎる。`20` / `60` は完走するが、同一frame番号でのplayer座標は一時的にズレ、数百frame後に収束する。
- つまり「ゲームレベルpacket置換」は前進している。未解決の本丸は、`1 EmuInstance * 2PC` 向けにLocalMPなしでもNSMBの接続シェルを維持するROM/hook設計。

## 実装済み

- `scripts/run-nsmb-mvl-lan-route-smoke.ps1`
  - post-start screenshotから黒背景+白文字の切断画面を検出する。
  - 完全な黒画面を検出し、失敗扱いにする。
  - `-ScreenshotInterval` でスクリーンショット間隔を調整できる。
  - `-SkipDisconnectScreenshotCheck` は調査用のみ。成功判定には使わない。
  - `-LanStartAttempts` でLAN start、未到達、未完走時だけ自動retryできる。
  - retry時は `LogDir-attemptN` に分けて出力する。
- packet bridge / replay hook
  - `Net::getConsoleKeys()`
  - `Net::getPacketByte()`
  - `Net::getPacketTick()`
  - `Net::getPacketAction()`
- bridge補助
  - local packetもlive replay表へ登録する。
  - `-PacketBridgeStrictPlayers "0,1"` で両slotをstrict対象にできる。
  - `-PacketBridgeReplayReturnLookupTick` で `getPacketTick()` にlookup tickを返せる。
  - `NSML_RefreshMarioVsLuigiPacketSlots()` でbridge packetをNSMBのLocalMP packet slotへ反映する。
  - `-PacketBridgeSuppressDisconnect` でLocalMP停止時のdisconnect state/flagsを抑制する。
  - `-PacketBridgeForceTickBase` でDropMP後のpacket tick基準値を固定できる。
  - `-PacketBridgeMaxFrameLead` でpacket bridgeの進行フレーム先行量を制限できる。
  - `-PacketBridgePreserveNetPointers` / `-PacketBridgeSuppressBlackout` は失敗切り分け用の実験フック。現時点ではDropMP後の正常描画維持には不十分。
  - `-PacketBridgeBypassNetDisconnect` で `Net::update()` の切断分岐をskipできる。
  - `-PacketBridgeBypassNetDisconnectMode skip` は黒画面回避用の現行実験ルート。
  - `-PacketBridgeBypassNetDisconnectMode force-active` は `0x02087E20=0x0004` を強制する切り分け用。黒画面が再発するため成功扱いしない。
- 検証補助
  - 黒画面時にDISPCNT/BLD/Net stateをstdoutへ出す。
  - `MELONDS_NSML_SCREENSHOT_REG_TRACE=1` でスクリーンショット保存時の表示レジスタを出せる。
  - `MELONDS_NSML_WATCH_*` のwatchログにLRも出す。

## 重要アドレス

- `Net::getPacketTick(u16)`: `0x0200E9BC`
- `Net::getPacket(u16)`: `0x0200E9FC`
- LocalMP packet slot status: `0x0208AE50`
- LocalMP packet buffer: `0x0208B040 + player * 0x3E`
- NSMB disconnect flags候補: `0x02087E5C`
- NSMB network state候補: `0x02087E1C`

## 検証結果

ビルド:

```powershell
cmake --build build\debug-windows-x86_64 --target melonDS --config Debug
```

LocalMPあり、両slot strict replay:

```powershell
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 4200 -HostStartupDelayMs 50 -ScreenshotInterval 60 -LogDir logs\lan-route-4200-strict-both-allops-slotwrite-nodrop -GameStateTrace -GameStateTraceInterval 60 -PacketBridge -PacketBridgePort 8191 -PacketBridgeStartFrame 3000 -HostPacketBridgeReplayTickOffset 3 -ClientPacketBridgeReplayTickOffset 2 -PacketBridgeStrictPlayers "0,1" -PacketBridgeStrictStartFrame 3120 -PacketBridgeStrictRequireLead 0 -PacketBridgeLiveFallbackWindow 8 -PacketBridgeReplayReturnLookupTick
```

DropMP後の失敗例。切断文字列は抑制できるが、黒画面検出により失敗:

```powershell
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -LanStartAttempts 6 -Frames 4800 -HostStartupDelayMs 1000 -ScreenshotInterval 60 -LogDir logs\lan-route-4800-neutralfix-dropmp3600-forcebase08aa -GameStateTrace -GameStateTraceInterval 60 -PacketBridge -PacketBridgePort 8213 -PacketBridgeStartFrame 3000 -HostPacketBridgeReplayTickOffset 3 -ClientPacketBridgeReplayTickOffset 2 -PacketBridgeStrictPlayers "0,1" -PacketBridgeStrictStartFrame 3120 -PacketBridgeStrictRequireLead 0 -PacketBridgeLiveFallbackWindow 8 -PacketBridgeReplayReturnLookupTick -DropMPAfterFrame 3600 -PacketBridgeDirectCapture -PacketBridgeForceTick -PacketBridgeForceTickStartFrame 3600 -PacketBridgeForceTickBase 0x08AA -PacketBridgeSuppressDisconnect
```

DropMP後movementありの以前の検証は、黒画面検出前の結果なので成功扱いしない:

```powershell
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -LanStartAttempts 4 -Frames 6600 -HostStartupDelayMs 50 -ScreenshotInterval 120 -InputScript tests\nsmb_mario_vs_luigi_afterdrop_movement.inputs -LogDir logs\lan-route-6600-dropmp3600-suppressdisconnect-movement5200-attempted -GameStateTrace -GameStateTraceInterval 120 -PacketBridge -PacketBridgePort 8204 -PacketBridgeStartFrame 3000 -HostPacketBridgeReplayTickOffset 3 -ClientPacketBridgeReplayTickOffset 2 -PacketBridgeStrictPlayers "0,1" -PacketBridgeStrictStartFrame 3120 -PacketBridgeStrictRequireLead 0 -PacketBridgeLiveFallbackWindow 8 -PacketBridgeReplayReturnLookupTick -DropMPAfterFrame 3600 -PacketBridgeDirectCapture -PacketBridgeForceTick -PacketBridgeForceTickStartFrame 3600 -PacketBridgeSuppressDisconnect
```

LocalMPを残したまま、ゲームpacket読みにstrict bridgeを適用したmovement検証:

```powershell
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -LanStartAttempts 6 -Frames 6600 -HostStartupDelayMs 1000 -ScreenshotInterval 120 -InputScript tests\nsmb_mario_vs_luigi_afterdrop_movement.inputs -LogDir logs\lan-route-6600-nodrop-strictbridge-movement5200 -GameStateTrace -GameStateTraceInterval 120 -GameStateTraceExtended -PacketBridge -PacketBridgePort 8228 -PacketBridgeStartFrame 3000 -HostPacketBridgeReplayTickOffset 3 -ClientPacketBridgeReplayTickOffset 2 -PacketBridgeStrictPlayers "0,1" -PacketBridgeStrictStartFrame 3120 -PacketBridgeStrictRequireLead 0 -PacketBridgeLiveFallbackWindow 512 -PacketBridgeReplayReturnLookupTick -PacketBridgeDirectCapture -PacketBridgeForceTick -PacketBridgeForceTickStartFrame 3600 -PacketBridgeForceTickBase 0x08AA
```

結果:

- `logs\lan-route-6600-nodrop-strictbridge-movement5200-attempt2`
- 6600 frame完走。
- host/clientで `playerActor0X=0x128fff`, `playerActor1X=0xfffd8000`, `vsStarActorX=0x1a0000` が一致。
- 画面のカメラ位置はlocal player基準なのでhost/clientで見た目は異なるが、ゲーム状態としてのplayer/star座標は一致している。

短縮trace:

- `logs\lan-route-5520-nodrop-strictbridge-movement-tracekeys-attempt2`
- host送信: player0 `keys=0x0010`
- client送信: player1 `keys=0x0020`
- 5520 frame時点でhost/clientの `playerActor0X=0x128fff`, `playerActor1X=0xfffd8000` が一致。
- 5280/5400 frameでは同一frame番号の座標に一時差が出る。これはpacket bridgeがまだ完全なロックステップではなく、プロセス間に数十frameの進行差が残るため。

frame lead制限:

- `logs\lan-route-5520-nodrop-strictbridge-movement-framelead60-attempt1`: 118.7秒で完走。中間座標差は残るが5520で収束。
- `logs\lan-route-5520-nodrop-strictbridge-movement-framelead20-attempt1`: 165.5秒で完走。中間座標差は残るが5520で収束。
- `logs\lan-route-5520-nodrop-strictbridge-movement-framelead4-attempt1`: 同期は強いが遅すぎて既定timeout内に5520まで届かなかった。

失敗/未解決:

- `-PacketBridgeSuppressDisconnect` なしでLocalMPを停止すると、frame 3900付近で切断画面になる。
- `-PacketBridgeSuppressDisconnect` ありでも、黒画面検出を入れるとhost frame 3900付近、client frame 3840付近で失敗する。
- `-PacketBridgeLiveFallbackWindow 512` でpacket slot/statusを維持しても黒画面は解消しない。
- `-PacketBridgeSuppressBlackout` でDISPCNTを通常値に戻しても、地形/キャラクター描画は戻らずHUDだけになる。
- `-PacketBridgeBypassNetDisconnect -PacketBridgeBypassNetDisconnectMode skip -PacketBridgeBypassNetDisconnectStartFrame 3600`
  - `logs\lan-route-4200-dropmp3600-bypass-netdisconnect-skip-attempt1`
  - host frame 3842 / client frame 3819 で `0x02010174` の切断分岐をskipし、4200 frameまで黒画面/切断表示/abortなしで完走。
  - ただしpacket replay traceは3840 frame付近で止まり、DropMP後の継続的な入力packet処理には戻っていない。
- `-PacketBridgeBypassNetDisconnectMode force-active`
  - `logs\lan-route-4200-dropmp3600-force-netactive-start3600-trace-attempt1`
  - `0x02087E20` を `0x0004` へ戻すと、`getConsoleKeys()` 呼び出しは3840 frame付近で見えるが、frame 3960付近で黒画面に落ちる。
- watch結果:
  - Net内部状態はhost frame 3842 / client frame 3808付近で `0x0200EE5C` などから0化される。
  - render tableはhost frame 3840 / client frame 3815付近で汎用memset `0x0200B288` と描画登録処理 `0x0200D3A0` により黒画面向けの内容へ変わる。
- LAN route smokeはまだ `LAN client start ok=0` や未到達で失敗することがある。`-LanStartAttempts` で実用上はretryできるが、根本原因は未解消。

## 次にやること

1. `Net::Core::transferPacket()` / `Net::Core::processRecvPacket()` 側で、LocalMP停止後に `Net::getPacket*()` が止まる原因を特定する。
2. `skip` modeで黒画面を避けたまま、transfer成功/受信済みpacketありに見せるhookを追加し、3840 frame以降もpacket処理が継続するか確認する。
3. 3840 frame以降もpacket処理が継続したら、movement scriptで5200 frame以降のRIGHT/LEFTがplayer座標へ反映されるか検証する。
4. 抑制できたら、同じ考え方をROM patch候補として整理する。
5. packet bridgeのフレーム同期は、単純なsleep throttleではなく、送信済みpacket frameを使った軽量barrierまたは固定入力遅延方式へ寄せる。
6. LAN start flakeの根本原因を調べ、検証のretry依存を減らす。
