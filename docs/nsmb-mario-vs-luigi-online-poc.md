# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` で WAN 越しに対戦できる形へ持っていく。

過去に試した `LocalMP 2インスタンス * 2プロセス`、savestate共有、試合開始後のWAN切り替え、actor/state強制同期は、速度・安定性・ゲーム状態の自然さの問題が大きいため本筋から外した。現在は US版ROMを主対象に、NSMB本来のMario vs Luigi処理をできるだけ使い、試合中の入力同期だけをWAN adapterへ差し替える方針。

## 現在の方針

- 主対象ROMは US版 `roms/nsmb-us.nds` / `A2DE`。
- 最終形は `host localPlayerID=0`、`client localPlayerID=1`。
- clientはLuigi側として自然に動かす。カメラ、ストックアイテム、死亡/復帰、勝敗判定をlocalPlayerID=1の通常処理に任せる。
- direct MvL entry ROM patchで、ローカル通信UIを経由せずMario vs Luigiステージへ入る。現在の本線は true `host localPlayerID=0` / true `client localPlayerID=1`。
- `Net::getConsoleKeys(u16)` と `Net::getConsoleTouchPad(u16)` をJIT helper patchでscratch memory参照へ差し替え、host/client間の `WireInput` をplayer0/player1入力へ反映する。
- `getPacketByte/getPacketTick/getPacketAction` まで差し替えるとステージ状態を壊しやすいため、現時点ではkeys/touch helper限定。
- 死亡時停止対策は全no-opではなく、`Game::vsMode != 0` のときだけ `PlayerBase::freezeStage()` / `PlayerBase::signalLocked()` をskipする条件付きROM patchへ寄せる。

## 完了したこと

- US版ROM patch toolingを追加。
  - `tools/nsmb_us_rom_tool.py`
  - `tools/nsmb_us_rom_patch.py`
- direct MvL entry系の検証ROMを生成済み。
  - host: `roms/nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-rngconst.tmp.nds`
  - client: `roms/nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-rngconst.tmp.nds`
- `external/NSMB-Code-Reference` を参照し、主要なNet helperを特定。
  - `Net::getConsoleKeys`
  - `Net::getConsoleTouchPad`
  - `Net::getPacketByte`
  - `Net::getPacketTick`
  - `Net::getPacketAction`
- screenshot dump、framebuffer dump、game-state trace、extended trace、packet replay trace、RAM dumpなどの検証フックを追加。
- `wifi-communicating-consoles --count 2` と `rng-constant --value 0x100` の診断patchで、初期ステージ状態、moving hazard、液体collision、初期スター位置をhost/clientで揃えられることを確認。
- offline scripted remote packet検証で、host local0 / client local1 のplayer0/player1入力とactor座標が短時間一致することを確認。
- JIT有効時でも `Net::getConsoleKeys` keys helper patchだけなら、offline検証でhost/clientが一致することを確認。
- `-InputNetplay` modeを追加し、PacketBridge本体を使わず `WireInput` だけをkeys helper scratchへ接続できるようにした。
- JIT helper patchで `Net::getConsoleTouchPad` もscratch packet参照へ差し替え、touch入力をplayer別に反映できるようにした。
- 入力netplay専用モードでは通常lockstepへ入らず、`frame + delay` の入力を事前送信し、`frame` の入力を適用するようにした。
- `-InputDelayFrames` を追加し、検証スクリプトから入力遅延フレーム数を切り替えられるようにした。
- `-InputSendDelayFrames` / `-InputSendJitterFrames` を追加し、`WireInput` の人工配送遅延・jitterを検証できるようにした。
- 入力netplay専用モードでは自動match seedによる `Net::random.value` 書き換えを止め、ROM側の固定RNGを使うようにした。
- `-CheckHostClientGameplaySync` を追加し、host/clientの重要game-state差分を自動検出できるようにした。
- `PlayerBase::signalLocked()` をno-op化するUS ROM patchを追加し、Luigi死亡時に相手PlayerBaseの `updateLocked` が立って進行が止まる経路を診断できるようにした。
- `PlayerBase::freezeStage()` をno-op化するUS ROM patchを追加し、死亡時に敵/移動ハザードなどのstage actor更新が止まる経路を診断できるようにした。
- `PlayerBase::freezeStage()` / `PlayerBase::signalLocked()` をVS中だけskipする `--player-stage-lock-vsmode-noop` を追加した。
- direct MvL entry ROM生成フローを true local1 + `rng-constant --value 0x100` + `--player-stage-lock-vsmode-noop` に更新した。旧hybrid local0 client UI経路は本線から外す。
- `-CheckNoPlayerUpdateLock` を追加し、死亡前後などの指定フレーム範囲で `playerActor0UpdateLocked` / `playerActor1UpdateLocked` が立ったら自動失敗にできるようにした。
- `-CheckMovingHazardProgressDuringDeath` を追加し、死亡中にmoving hazardのX座標が進まない場合を自動失敗にできるようにした。
- `-RequireHostLocalPlayerID` / `-RequireClientLocalPlayerID` を追加し、clientが実際にはlocal0へ戻ってしまう回帰を自動検出できるようにした。

## 直近の検証結果

代表ログ:

- `logs/codex-both-inputnetplay-delay-armcheck-1300-20260528`
- `logs/codex-both-inputnetplay-delay-long-2400-20260528`
- `logs/codex-both-inputnetplay-synccheck-2400-20260528`
- `logs/codex-both-inputnetplay-synccheck-4800-20260528`
- `logs/codex-both-inputnetplay-delay12-synccheck-2400-20260528`
- `logs/codex-both-inputnetplay-senddelay4-jitter2-delay12-2400-20260528`
- `logs/codex-both-inputnetplay-senddelay8-jitter4-delay12-2400-20260528`
- `logs/codex-both-inputnetplay-senddelay10-jitter6-delay16-2400-20260528`
- `logs/codex-both-inputnetplay-touch-helper-stock-strong-synccheck2-2600-20260528`
- `logs/codex-both-inputnetplay-touch-helper-regression-2400-20260528`
- `logs/codex-both-inputnetplay-stock-touch-screenshotcheck-3600-20260528`
- `logs/codex-both-luigi-death-updatelock-writetrace-1700-20260528`
- `logs/codex-both-inputnetplay-luigi-death-siglocknoop-3600-20260528`
- `logs/codex-both-inputnetplay-luigi-death-siglocknoop-synccheck-2400-20260528`
- `logs/codex-both-luigi-death-baseline-updatelockcheck-1700-20260528-2`
- `logs/codex-both-luigi-death-siglocknoop-updatelockcheck-1800-20260528-2`
- `logs/codex-both-luigi-only-death-siglocknoop-freeze-probe-2700-20260528`
- `logs/codex-both-luigi-only-death-stagefreeze-calltrace-2250-20260528`
- `logs/codex-both-luigi-only-death-freezenoop-probe-2700-20260528`
- `logs/codex-both-luigi-only-death-freezenoop-progresscheck-2250-20260528`
- `logs/codex-both-luigi-only-death-siglocknoop-progresscheck-expectedfail-2250-20260528`
- `logs/codex-both-inputnetplay-freezenoop-current-2250-20260528`
- `logs/codex-both-true-local1-rootcmp-start-1500-20260529`
- `logs/codex-both-true-local1-rngconst-start-1500-20260529`
- `logs/codex-both-true-local1-rngconst-death-lockcheck-expectedfail-2250-20260529`
- `logs/codex-both-true-local1-vslockskip-rngconst-deathcheck-2400-20260529`
- `logs/codex-both-stable-true-local1-vslockskip-rngconst-deathcheck-2400-20260529`
- `logs/codex-both-stable-true-local1-vslockskip-rngconst-idcheck-2400-20260529`

結果:

- 1300フレーム検証で、host/clientの `netPacketTick`、player0/player1入力、Mario/Luigi actor座標、残機が一致。
- 2400フレーム検証でも、通信切断、remote input timeout、ARM abort検出なし。
- 2400フレーム時点の実効速度は host 約49.45fps、client 約50.50fps。
- `-CheckHostClientGameplaySync` 付きの2400/4800フレーム検証が通過。入力、Mario/Luigi actor座標、残機、ストック、スターactor、moving hazard、一部object countのhost/client一致を自動確認済み。
- 4800フレーム時点の実効速度は host 約50.97fps、client 約51.54fps。
- 入力遅延12フレーム設定でも2400フレーム同期チェックが通過。WAN向けに遅延量を上げる検証ルートができた。
- 入力遅延12フレーム + 人工送信遅延4フレーム + jitter最大2フレームでも2400フレーム同期チェックが通過。
- 入力遅延12フレーム + 人工送信遅延8フレーム + jitter最大4フレーム、入力遅延16フレーム + 人工送信遅延10フレーム + jitter最大6フレームでも2400フレーム同期チェックが通過。
- touch helper追加後も既存移動スクリプトが2400フレーム同期チェックを通過。
- Luigi側ストックアイテム用の長押しtouchスクリプトで、`player1InventoryPowerup` が `0x1 -> 0x0` に変化し、host/clientで一致することを確認。
- ストック使用スクリプトで3600フレーム検証も通過。3000フレーム以降のスクリーンショット切断/blank検出も有効にした状態で問題なし。
- screenshot上、hostはMario視点、clientはLuigi視点になっている。上画面カメラ差はlocalPlayerID差として想定内。
- ストック表示はhostがplayer0、clientがplayer1を表示しており、CSV上も `player0InventoryPowerup=0x0`、`player1InventoryPowerup=0x1` でhost/client一致。Luigi側UIとして自然に動いている可能性が高い。
- Luigi死亡時の停止原因をwrite traceで確認。`PlayerBase::signalLocked()` が相手PlayerBaseの `updateLocked` を1にし、`signalUnlocked()` が後で戻していた。
- `PlayerBase::signalLocked()` no-op ROMでは、Luigi死亡時に `playerActor0UpdateLocked` / `playerActor1UpdateLocked` が立たず、2400フレームのhost/client gameplay sync checkも通過。
- `-CheckNoPlayerUpdateLock` は未patched ROMで期待通り失敗し、`signalLocked()` no-op ROMで通過することを確認。
- ただし `signalLocked()` no-opだけでは敵/移動ハザードはまだ止まっていた。`movingHazardX` がLuigi死亡中に `0x62800` で固定されることを確認。
- call traceで、Luigiのdamage/death経路から `PlayerBase::freezeStage()` が呼ばれていることを確認。
- `PlayerBase::freezeStage()` no-op ROMでは、同じ死亡区間で `movingHazardX` が継続して変化し、敵/移動ハザード停止が解消することを確認。
- `-CheckMovingHazardProgressDuringDeath` は旧 `signalLocked()` no-op ROMで期待通り失敗し、`freezeStage()` no-op入りROMで通過することを確認。
- ただしこれは成功扱いではない。`freezeStage()` no-opは死亡時停止症状の一部を抑えただけで、通常のMario vs Luigi開始状態との差分が残っている可能性が高い。死亡/復帰、敵、カメラ、勝敗判定まで含めた自然な挙動は未確認。
- 目視確認では、`freezeStage()` no-op入り検証でもまだ不自然な挙動がある。
  - client側がLuigi視点ではなくMario視点になる場合がある。
  - Big Star位置がhost/clientでずれる場合がある。
  - リスポーン前に死亡プレイヤー姿が見えるなど、死亡/復帰描画が通常のMario vs Luigiと異なる。
- 調査の結果、clientがMario視点になる問題は、旧ROM生成フローが名前にlocal1を含みつつ実際の `Game::localPlayerID` は0のhybrid経路になっていたことが主因。true `localPlayerID=1` direct entryではclient側カメラはLuigi側へ戻る。
- true local1でも `rng-constant` を明示適用しない場合、Big Star座標がhost `0x30000` / client `0x3c0000` のようにずれる。`Net::getRandom()` / `Game::getRandom()` を `0x100` 固定にするとhost/clientとも `vsStarActorX=0x90000` で一致。
- true local1 + RNG固定だけでは、死亡時に `playerActor0UpdateLocked` が立ち、moving hazardも停止する。`Game::vsMode != 0` のときだけ `freezeStage()` / `signalLocked()` をskipする条件付きpatchで、死亡中のplayer update-lockとmoving hazard停止は解消。
- `Player::beginDeathTransition()` は標準死亡transitionへ入り、その後 `viewTransitState` → `vsPipeTransitState` → `defaultTransitState` へ進むことを確認。VSPipe復帰自体には移っているが、死亡/復帰描画が通常MvsLとして完全に自然かは引き続き確認が必要。
- 細かいスクリーンショット確認で、true local1 + RNG固定 + VS限定stage-lock skipの死亡/土管復帰描画は通常動作に見えることを確認。`-CheckVsPipeRespawnVisibility` を追加し、土管復帰前フェーズで死亡プレイヤーが表示される回帰を自動検出できるようにした。
- `wifi-communicating-consoles --count 2` をstable ROM生成フローへ組み込み、`ForceWifiCommunicatingCount=2` runtime hookなしでも同じ死亡/復帰チェックが通ることを確認。
- 現行stable ROMで4800フレームのhost/client gameplay syncが通過。`ForceWifiCommunicatingCount` runtime hookなしで、主要game-stateの一致を確認。
- 入力遅延16フレーム + 人工送信遅延8フレーム + jitter最大4フレームでも3600フレーム同期チェックが通過。
- Luigiが固定RNGのBig Starを取得する入力スクリプト `tests/nsmb_us_direct_mvl_luigi_star_right.inputs` を追加。`-RequireStarPickup -RequireStarPickupPlayer 1` で、通常条件と遅延/jitter条件の両方でスター取得を自動確認。

## 未解決・注意点

- 2400フレームまでの短時間検証であり、実プレイとして十分な長時間安定性は未確認。
- `Game::vsMode != 0` 条件付きstage-lock skipは全no-opより副作用が小さいが、タイムアップ、土管/ドア、8コインアイテムなど他transitionで問題がないかは未確認。
- リスポーン描画は短時間の目視とvisible flag検証では自然に見えるが、長時間プレイや別死亡条件での回帰は未確認。
- 現在の入力スクリプトは短い診断用で、8コインアイテム、ランダムステージ、死亡/復帰後の長時間継続まではまだ十分に検証していない。
- 詳細trace付きでは約43-44fps、traceなしの実用寄り設定では約54-55fps。完全な60fpsには届いていないが、10fps台は主に重い診断設定由来。
- WANの遅延・ジッタを模した検証は一部通過。packet lossや実2PC分散は未実施。現状は同一PC上のhost/client 2プロセス検証。

## 次にやること

1. 最優先: true local1 + RNG固定 + VS限定stage-lock skip + ROM側wifi count patchを本線として、長時間の死亡/復帰・勝敗・スター取得まで壊れないか確認する。
   - client local1カメラ、Big Star位置、localPlayerIDは自動チェックで守る。
   - 片方死亡中に相手プレイヤー・敵・ブロック・ステージ進行が止まらないことは、`-CheckNoPlayerUpdateLock` と `-CheckMovingHazardProgressDuringDeath` で継続確認する。
   - 土管復帰前後の表示は `-CheckVsPipeRespawnVisibility` で継続確認する。
2. さらにtraceを減らした実用寄り設定、または2PC分散でFPSが60fpsに近づくか確認する。
3. Luigi側操作の検証を増やす。
   - カメラ追従
   - 死亡/復帰
   - 勝敗判定
4. 8コインアイテム、2個目以降のBig Star、ランダムステージなど、乱数由来イベントを固定RNG + 入力同期で再現できるか確認する。
5. 残るruntime hook依存をROM patchへ寄せ、起動から試合開始までをより自然なdirect entryにする。
6. 同一LANまたは擬似遅延付きの2プロセス検証へ進む。

## 代表テストコマンド

入力netplayの現行代表検証:

```powershell
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 `
  -RunRole both `
  -Frames 2250 `
  -AllowJit `
  -Exe build\release-windows-x86_64\melonDS.exe `
  -Rom roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-rngconst.tmp.nds `
  -HostRom roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-rngconst.tmp.nds `
  -ClientRom roms\nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-rngconst.tmp.nds `
  -InputScript tests\nsmb_us_direct_mvl_client_stock_touch_strong.inputs `
  -GameStateTrace `
  -GameStateTraceExtended `
  -GameStateTraceInterval 30 `
  -ScreenshotInterval 0 `
  -NoHashLog `
  -SkipDisconnectScreenshotCheck `
  -SkipBlankScreenshotCheck `
  -SkipMvlStateCheck `
  -SkipGameplayActorCheck `
  -InputNetplay `
  -InputDelayFrames 12 `
  -PacketBridgeJitHelperPatch `
  -PacketBridgeJitHelperPatchFrame 900 `
  -PacketBridgeStartFrame 900 `
  -CheckHostClientGameplaySync `
  -CheckNoPlayerUpdateLock `
  -CheckNoPlayerUpdateLockStartFrame 1840 `
  -CheckNoPlayerUpdateLockEndFrame 2220 `
  -CheckMovingHazardProgressDuringDeath `
  -CheckMovingHazardProgressStartFrame 1840 `
  -CheckMovingHazardProgressEndFrame 2220 `
  -CheckMovingHazardProgressMinUniqueX 3 `
  -CheckVsPipeRespawnVisibility `
  -CheckVsPipeRespawnVisibilityStartFrame 1840 `
  -CheckVsPipeRespawnVisibilityEndFrame 2250 `
  -RequireHostLocalPlayerID 0 `
  -RequireClientLocalPlayerID 1 `
  -LogDir logs\codex-both-stable-wificount2-rompatch-vspipecheck-2250-20260529
```

診断trace付きで入力netplay内部を見る場合は `-InputNetplayTrace` を追加する。

## 成功条件

`frame limit reached` だけでは成功扱いにしない。最低限、次を確認する。

- data abort / fatal / undefined がない。
- 「通信が切断されました」画面にならない。
- screenshotがMario vs Luigi stageとして読める。
- host/clientでplayer0/player1 actor座標、死亡状態、残機、スター数、ストックアイテムが一致する。
- Goomba、Big Star、8コインアイテムなどの動的要素が一致する。
- client local1でLuigi側カメラ、UI、ストックアイテム、死亡/復帰、勝敗判定が自然に動く。
- WAN adapter有効時に実用的なFPSで検証できる。

## 運用ルール

- ROM生成物、savestate、巨大ログはgitに含めない。
- docsは古い追記を残し続けず、現在の方針、完了、未解決、次作業が上から読める形に保つ。
- 最終応答前にdocsの古い情報や矛盾を確認し、必要なら整理する。
