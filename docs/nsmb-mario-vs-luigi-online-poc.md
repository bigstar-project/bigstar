# NSMB Mario vs Luigi Online PoC

## 現在の最優先事項: rollback方式の成立性調査

WAN越しで入力遅延を小さくするには、現行の固定入力遅延/待ち方式だけでは限界がある。次の優先事項は、MvsLの入力同期をrollback形式に寄せられるかを調査・検証すること。

外部資料と既存実装から整理したrollbackの必須条件:

- 過去フレームの完全なゲーム状態を保存できること。
- remote inputが未着のフレームでは、前回remote inputを繰り返すなどの予測入力で先に進めること。
- 後から届いたremote inputが予測と違った場合、該当フレームのsavestateへ戻し、正しい入力列で現在フレームまで高速再実行できること。
- 同じ初期状態と同じ入力列なら、host/clientでMvsLの重要状態が決定論的に一致すること。

2026-05-29時点で追加した調査用hook:

- `MELONDS_NSML_ROLLBACK=1`: 入力netplay中にremote input未着でも停止せず、直近remote inputから予測して進めるprobe mode。
- `MELONDS_NSML_ROLLBACK_WINDOW=<frames>`: in-memory savestate ringの保持フレーム数。初期値20。
- `MELONDS_NSML_ROLLBACK_CHECKPOINT_INTERVAL=<frames>`: rollback checkpointの保存間隔。初期値1。intervalを広げると保持checkpoint数とsavestate保存回数を減らせるが、予測ミス時の再実行範囲は長くなる。
- `MELONDS_NSML_ROLLBACK_RESTORE_PROBE=1`: 予測ミスマッチ時に該当フレームのcheckpointを復元できるかだけを試す診断用。
- `MELONDS_NSML_ROLLBACK_RESIMULATE=1`: 予測ミスマッチ時にcheckpointへ戻り、保存済みlocal/remote入力履歴で現在フレームまで内部再実行する診断用。

現時点の判断:

- rollbackの第一関門である「予測しながら止まらず進める」「後着remote inputとのミスマッチを検出する」「過去フレームのsavestateをメモリ上に保持する」「checkpoint復元後に内部再実行する」土台を追加した。
- `logs/codex-rollback-restore-probe-980-20260529`: frame 900付近の予測ミスマッチ後、約19MBのsavestate checkpointを復元できることを確認。
- `logs/codex-rollback-resim-probe-980-20260529`: frame 900の予測ミスマッチ後、frame 900..955 を内部再実行できることを確認。
- `logs/codex-rollback-resim-throttle-2600-20260529`: rollback + frame lead throttle + checkpoint更新で、2600フレームの主要CSV比較は同一行では一致。検証wrapperはCSV間隔設定のため movement probe row不足で失敗。
- `logs/codex-rollback-resim-throttle-pass-2600-20260529`: CSV間隔30では、rollback補正直後の一時フレームでhost/clientの表示/actor状態が異なり、その後再収束する挙動を確認。従来の「全フレーム完全一致」検証はrollback方式には厳しすぎるため、rollback用には「一定settle frames後に収束しているか」を見る検証へ分ける必要がある。
- `logs/codex-rollback-resim-settle-2600-20260529`: `RollbackSettleFrames=30` の比較で2600フレーム通過。frame 1290/1950/2250/2370 の一時差分が、それぞれ30フレーム以内にhost/client一致へ戻ることを確認。
- `logs/codex-rollback-checkpoint-interval4-2600-20260529`: checkpoint interval 4で2600フレーム通過。保持checkpointは約31、30フレームsettle比較も通過。
- `logs/codex-rollback-checkpoint-interval8-2600-20260529`: checkpoint interval 8で2600フレーム通過。保持checkpointは約16、30フレームsettle比較も通過。
- `logs/codex-rollback-checkpoint-interval16-2600-20260529`: checkpoint interval 16で2600フレーム通過。保持checkpointは約8、30フレームsettle比較も通過。ただしFPSは35-37fps程度で大きく改善していないため、保持メモリよりも予測ミス後の再実行中savestate保存、同一PC 2プロセス実行、game-state traceが主な負荷候補。
- 既存のmelonDS savestateは使えるが、1 checkpointが約19MBあり、毎フレーム保存は重い。低頻度checkpointでも成立する見込みは出たが、快適なWAN対戦には、実プレイ時のtrace抑制、再実行中のcheckpoint保存削減、差分savestate、重要RAM限定snapshot、またはrollback window/intervalの自動調整が必要。

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
- 手動入力時の最優先課題は、host/clientの試合開始フレームと入力適用フレームを揃えること。入力送信開始フレームでhostをpeer接続待ちにし、開始後は相手入力が届くまで進めないロックステップ寄りにする。
- 手動入力では `-InputMaxFrameLead` で片方のプロセスが先行できるフレーム数を制限する。これにより、入力遅延ぶんだけ片方が先行し、同じ実時間のキー入力がhost/clientで別フレームに乗る問題を抑える。

## 完了したこと

- US版ROM patch toolingを追加。
  - `tools/nsmb_us_rom_tool.py`
  - `tools/nsmb_us_rom_patch.py`
- direct MvL entry系の検証ROMを生成済み。
  - host: `roms/nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-rngconst-netaid.tmp.nds`
  - client: `roms/nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-rngconst-netaid.tmp.nds`
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

最新の要約:

- 現在の本線は US版ROM + true `host localPlayerID=0` / true `client localPlayerID=1` のdirect MvL entry。旧hybrid local0 client経路、savestate共有、試合開始後WAN切り替え、actor/state強制同期は本線から外した。
- `Net::getConsoleKeys` / `Net::getConsoleTouchPad` のJIT helper patchで、host/clientの `WireInput` をplayer0/player1入力へ反映している。`getPacketByte/getPacketTick/getPacketAction` 差し替えはステージ状態を壊しやすいため現在は使わない。
- RNG固定、ROM側wifi count、`Net::localAid` patch、VS限定stage-lock skipにより、スター位置、死亡/復帰、Luigi視点、ストック表示、勝敗画面は主要smokeで通過済み。
- 結果画面到達と勝敗画像probeは `scripts/run-nsmb-mvl-split-local-result-smoke.ps1` で自動確認できる。
- 手動入力ルートでは、入力送信開始前にhostがpeer接続を待ち、remote input timeoutはデフォルトfatal。さらに `-InputMaxFrameLead` で片側プロセスの先行幅を制限する。
- host/clientで別々のローカル入力を流す検証は `scripts/run-nsmb-mvl-split-local-input-smoke.ps1` で自動確認できる。
- JIT有効でも、短時間の別ローカル入力同期、送信遅延/jitter付き別ローカル入力同期、6000フレーム結果画面到達split smokeが通過している。

直近の代表ログ:

- `logs/codex-split-local-input-script-2600-20260529`: JIT無効、hostのMario入力とclientのLuigi入力を別々に流し、2600フレームまで主要gameplay CSV比較通過。
- `logs/codex-split-local-input-script-jit-nodelay-2600-20260529`: JIT有効、人工遅延なし、別ローカル入力同期通過。host約43.7fps、client約44.8fps。
- `logs/codex-split-local-input-script-jit-nodelay-6000-20260529`: JIT有効、人工遅延なし、別ローカル入力後に6000フレームまで主要gameplay CSV比較通過。
- `logs/codex-split-local-input-script-jit-delay8-jitter4-2600-20260529`: JIT有効、入力遅延16 + 送信遅延8 + jitter最大4、別ローカル入力同期通過。
- `logs/codex-split-local-result-framelead2-jit-endfix-6000-20260529`: JIT有効、frame lead 2、人工遅延/jitter付き、6000フレーム結果画面到達とhost/client勝敗画像probe通過。
- `logs/codex-manual-launcher-params-smoke-1200-20260529`: 手動launcher短時間起動確認。JIT有効でhost約54fps、client約57fps。

重要な既存検証:

- `logs/codex-both-stable-wificount2-netaid-resultscene-probe-6000-20260529`: 6000フレーム結果画面到達、host/client gameplay sync、`localPlayerID`、`Net::localAid`、結果画面画像probe通過。
- `logs/codex-both-stable-wificount2-netaid-resultscene-probe-jitter-6000-20260529`: 入力遅延16 + 送信遅延8 + jitter最大4でも結果画面到達と勝敗画像probe通過。
- `logs/codex-both-manual-bootstrap-nojit-fatal-2400-20260529`: JIT無効 + manual bootstrap + fatal timeoutで2400フレームhost/client gameplay sync通過。

## 未解決・注意点

- 6000フレーム結果画面到達と、別ローカル入力後6000フレーム維持までは検証済み。実プレイとして十分な長時間安定性はまだ未確認。
- `Game::vsMode != 0` 条件付きstage-lock skipは全no-opより副作用が小さいが、タイムアップ、土管/ドア、8コインアイテムなど他transitionで問題がないかは未確認。
- リスポーン描画は短時間の目視とvisible flag検証では自然に見えるが、長時間プレイや別死亡条件での回帰は未確認。
- 現在の入力スクリプトは短い診断用で、8コインアイテム、ランダムステージ、死亡/復帰後の長時間継続まではまだ十分に検証していない。
- 詳細traceや結果画面スクリーンショット付きでは約39-44fps、traceなしの実用寄り設定では単体約54fps、同一PC 2プロセスでは約45-53fps。完全な60fpsには届いていないが、10fps台は主に重い診断設定由来。
- WANの遅延・ジッタを模した検証とlocalhostでのhost/client分割起動は通過。packet lossや実2PC分散は未実施。
- 手動入力は動作確認済み。peer待ち、fatal timeout、frame lead制限を入れたため、次は実キー入力を含む検証で見た目と操作感を確認する。
- `-InputMaxFrameLead 2` 後はJIT有効の別ローカル入力2600フレームと、結果画面到達6000フレームが通過している。ただし長時間の自由入力では未確認なので、JIT有効は段階的に検証を増やす。
- JIT無効の手動launcherは同期優先でかなり遅い。短いsmokeでは約12fps。JIT有効 + 人工遅延なしの短いmanual launcherは約54-57fps、split local-input smokeは約44fps。人工遅延/jitter付きresult smokeは意図的な待ちが入るため約15fps。

## 次にやること

1. 最優先: rollbackを「成立性probe」から「実用候補」へ寄せる。
   - checkpoint interval 4/8/16はいずれも30フレームsettle比較を通過したため、次はtraceを切った実プレイ寄り設定でFPSを測る。
   - 予測ミス後の再実行中checkpoint保存が重い可能性が高い。再実行中は必要最小限のcheckpointだけ保存する方式、または低頻度checkpoint + 入力到着バッファの設計を検証する。
   - rollback時の一時差分は許容し、一定settle frames後に収束するかを見る検証を標準化する。
2. 手動入力時のhost/client同期を、rollbackあり・なしの両方で比較する。
   - 既存の固定入力遅延/待ち方式は同期確認用のbaselineとして残す。
   - rollback方式では `InputDelayFrames=0` でもカクつかず進むこと、後着入力で再収束すること、操作感が改善することを見る。
3. true local1 + RNG固定 + VS限定stage-lock skip + ROM側wifi count + Net::localAid patchを本線として、長時間の死亡/復帰・勝敗・スター取得まで壊れないか確認する。
   - client local1カメラ、Big Star位置、localPlayerIDは自動チェックで守る。
   - 片方死亡中に相手プレイヤー・敵・ブロック・ステージ進行が止まらないことは、`-CheckNoPlayerUpdateLock` と `-CheckMovingHazardProgressDuringDeath` で継続確認する。
   - 土管復帰前後の表示は `-CheckVsPipeRespawnVisibility` で継続確認する。
4. traceを減らした実用寄り設定、または2PC分散でFPSが60fpsに近づくか確認する。
5. Luigi側操作の検証を増やす。
   - カメラ追従
   - 死亡/復帰
   - 勝敗判定は結果画面到達とhost/clientのwin/lose表示まで、通常条件・遅延/jitter条件・localhost split条件で自動確認済み。次はより自由な入力列と長時間検証へ広げる。
6. 8コインアイテム、2個目以降のBig Star、ランダムステージなど、乱数由来イベントを固定RNG + 入力同期で再現できるか確認する。
7. 残るruntime hook依存をROM patchへ寄せ、起動から試合開始までをより自然なdirect entryにする。
8. 実2PCまたは同一LANで、host/clientを別マシン相当の起動コマンドに分けて検証する。

localhost split検証:

```powershell
.\scripts\run-nsmb-mvl-split-local-result-smoke.ps1 `
  -LogDir logs\codex-split-local-script-result2-6000-20260529
```

手動プレイ用localhost起動:

短縮コマンド:

```powershell
.\scripts\run-nsmb-mvl-manual-local.ps1 `
  -LogDir logs\manual-local
```

JITを有効にする場合は `-AllowJit` を付ける。`-InputMaxFrameLead 2` 追加後は短時間splitと結果画面到達splitが通過しているため、操作感確認ではJIT有効も試せる。ただし長時間自由入力は未確認。

個別起動する場合:

host側:

```powershell
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 `
  -RunRole host `
  -Frames 999999 `
  -WaitTimeoutMs 86400000 `
  -Exe build\release-windows-x86_64\melonDS.exe `
  -Rom roms\nsmb-us.nds `
  -HostRom roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-rngconst-netaid.tmp.nds `
  -InputScript tests\nsmb_us_direct_mvl_manual_bootstrap.inputs `
  -ScreenshotInterval 0 `
  -NoHashLog `
  -SkipMvlStateCheck `
  -SkipGameplayActorCheck `
  -InputNetplay `
  -InputDelayFrames 16 `
  -InputMaxFrameLead 2 `
  -PacketBridgeJitHelperPatch `
  -PacketBridgeJitHelperPatchFrame 900 `
  -PacketBridgeStartFrame 900 `
  -LogDir logs\manual-host
```

client側:

```powershell
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 `
  -RunRole client `
  -Peer 127.0.0.1 `
  -Frames 999999 `
  -WaitTimeoutMs 86400000 `
  -Exe build\release-windows-x86_64\melonDS.exe `
  -Rom roms\nsmb-us.nds `
  -ClientRom roms\nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-rngconst-netaid.tmp.nds `
  -InputScript tests\nsmb_us_direct_mvl_manual_bootstrap.inputs `
  -ScreenshotInterval 0 `
  -NoHashLog `
  -SkipMvlStateCheck `
  -SkipGameplayActorCheck `
  -InputNetplay `
  -InputDelayFrames 16 `
  -InputMaxFrameLead 2 `
  -PacketBridgeJitHelperPatch `
  -PacketBridgeJitHelperPatchFrame 900 `
  -PacketBridgeStartFrame 900 `
  -LogDir logs\manual-client
```

手動プレイ用コマンドで決定性を最優先する場合は `-AllowJit` を付けない。操作感を優先して試す場合は `-AllowJit` を付ける。

2PC分散検証のコマンド雛形:

host側PC:

```powershell
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 `
  -RunRole host `
  -Frames 6000 `
  -WaitTimeoutMs 420000 `
  -AllowJit `
  -Exe build\release-windows-x86_64\melonDS.exe `
  -HostRom roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-rngconst-netaid.tmp.nds `
  -InputScript tests\nsmb_us_direct_mvl_star_collect_left.inputs `
  -GameStateTrace `
  -GameStateTraceExtended `
  -GameStateTraceInterval 120 `
  -ScreenshotInterval 6000 `
  -NoHashLog `
  -SkipDisconnectScreenshotCheck `
  -SkipBlankScreenshotCheck `
  -SkipMvlStateCheck `
  -SkipGameplayActorCheck `
  -InputNetplay `
  -InputDelayFrames 24 `
  -InputSendDelayFrames 12 `
  -InputSendJitterFrames 8 `
  -PacketBridgeJitHelperPatch `
  -PacketBridgeJitHelperPatchFrame 900 `
  -PacketBridgeStartFrame 900 `
  -RequireResultScene `
  -RequireHostResultWinScreenshot `
  -RequireHostLocalPlayerID 0 `
  -RequireHostNetLocalAid 0 `
  -RequireNetLocalAidStartFrame 900 `
  -LogDir logs\mvl-host-2pc-result
```

client側PC:

```powershell
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 `
  -RunRole client `
  -Peer <host-ip-address> `
  -Frames 6000 `
  -WaitTimeoutMs 420000 `
  -AllowJit `
  -Exe build\release-windows-x86_64\melonDS.exe `
  -ClientRom roms\nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-rngconst-netaid.tmp.nds `
  -InputScript tests\nsmb_us_direct_mvl_star_collect_left.inputs `
  -GameStateTrace `
  -GameStateTraceExtended `
  -GameStateTraceInterval 120 `
  -ScreenshotInterval 6000 `
  -NoHashLog `
  -SkipDisconnectScreenshotCheck `
  -SkipBlankScreenshotCheck `
  -SkipMvlStateCheck `
  -SkipGameplayActorCheck `
  -InputNetplay `
  -InputDelayFrames 24 `
  -InputSendDelayFrames 12 `
  -InputSendJitterFrames 8 `
  -PacketBridgeJitHelperPatch `
  -PacketBridgeJitHelperPatchFrame 900 `
  -PacketBridgeStartFrame 900 `
  -RequireResultScene `
  -RequireClientResultLoseScreenshot `
  -RequireClientLocalPlayerID 1 `
  -RequireClientNetLocalAid 1 `
  -RequireNetLocalAidStartFrame 900 `
  -LogDir logs\mvl-client-2pc-result
```

## 代表テストコマンド

入力netplayの現行代表検証:

```powershell
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 `
  -RunRole both `
  -Frames 2250 `
  -AllowJit `
  -Exe build\release-windows-x86_64\melonDS.exe `
  -Rom roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-rngconst-netaid.tmp.nds `
  -HostRom roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-rngconst-netaid.tmp.nds `
  -ClientRom roms\nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-rngconst-netaid.tmp.nds `
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
  -RequireHostNetLocalAid 0 `
  -RequireClientNetLocalAid 1 `
  -RequireNetLocalAidStartFrame 900 `
  -LogDir logs\codex-both-stable-wificount2-netaid-vspipecheck-2250-20260529
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
