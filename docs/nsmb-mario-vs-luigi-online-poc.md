# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` で WAN 越しに対戦できる形へ持っていく。

過去に試した `LocalMP 2インスタンス * 2プロセス`、savestate共有、試合開始後のWAN切り替え、Actor/state強制同期は、切断、desync、不自然な内部状態、低FPSの問題が大きいため最終方針から外す。

## 現在の方針

US版 ROM `roms/nsmb-us.nds` (`A2DE`) を主対象にする。`external/NSMB-Code-Reference` がUS版のシンボルを持つため、ROM patch と通信/ゲーム開始解析を進めやすい。

最終ルートは `host localPlayerID=0` / `client localPlayerID=1`。

理由:

- `client localPlayerID=0` hybrid は短期PoCと比較基盤としては有効だが、clientが内部的にはMario側のままになる。
- その場合、カメラ、死亡演出、サウンド、ストックアイテム、勝敗判定、描画cullingなどを個別にpatchし続ける必要があり、最終的な対戦実装として筋が悪い。
- `client localPlayerID=1` でNSMB自身が期待する正常なMario vs Luigi開始状態を作れれば、Luigi視点/UI/死亡/勝敗などはゲーム側の既存処理を使える可能性が高い。

当面は、UIや結果表示を補正するのではなく、`direct entry / VSConnect / StageStart / Game::loadLevel / StageObject activation / collision-liquid map initialization` を解析し、raw `client localPlayerID=1` の開始状態を host local0 と同じ simulation 条件へ近づける。

## 直近の重要な発見

2026-05-28 時点の raw direct local1 比較:

- host local0 と client local1 は、入力同期前から stage actor / physics 状態が一致していない。
- client local1 のGoomba欠落は、StageObject list欠落ではなかった。StageObject list自体はhost/clientで一致し、欠けていたGoombaは `objectID=0x53`, `settings=0x0`, StageObject `0x0229A9EC`。
- Goomba spawn欠落の主因は `Game::wrapX` 未初期化。host local0 は `0x02085AA4/0x02085AA8 = 0x003FFFFF` だが、raw client local1 は `0xFFFFFFFF` のままだった。
- 診断patch `stage-object-activation-force-wrap-x --wrap-x 0x003FFFFF` でGoomba spawnとactive維持は戻る。
- ただしwrapX補正だけではGoombaの実移動速度がhostと一致しなかった。

追加解析で判明したこと:

- Goomba AI側の意図速度 `velocity.x` は host/client とも `-0x800` で一致していた。
- 実際に座標へ適用される `lastStepX` が host `-0x800`、client `-0x400` に分岐していた。
- 分岐点は `StageEntity::updateLiquidCollision`。frame 888でhostは「液体外」、client local1は「液体内」と判定され、clientだけ `accelV=-0x100`, `velocityLimitY=-0x1000` 系の液体物理に入っていた。
- 診断patch `stage-entity-liquid-check-result` で液体判定を強制的に「液体外」にすると、client local1 のGoomba位置/速度は host local0 と frame 960 まで一致した。
- つまりGoomba速度差の直接原因は、Goomba自身ではなく、local1開始状態での collision/liquid map または関連初期化の不一致。
- ただし同じ診断patch後も player environment flags はclient側で `0x202` のまま残るため、液体判定を潰すだけでは根本解決ではない。

## 現在の実装状況

実装済み:

- US ROM patch tooling: `tools/nsmb_us_rom_tool.py`, `tools/nsmb_us_rom_patch.py`
- direct MvL entry ROM patch
- PacketBridge の下位 packet API hook
  - `Net::getConsoleKeys`
  - `Net::getPacketByte`
  - `Net::getPacketTick`
  - `Net::getPacketAction`
- 自動検証
  - screenshot dump
  - game-state trace
  - extended game-state trace
  - packet bridge trace
  - call trace
  - RAM dump
- active object list trace
- moving hazard / Goomba physics field trace
- host/client split 実行ランナー
- local0 hybrid の比較基盤
- RNG seed固定と初期スター座標一致の検証
- 診断patch
  - `stage-object-activation-force-wrap-x`
  - `stage-entity-liquid-check-result`

未解決:

- raw `client localPlayerID=1` の正常開始状態がまだ作れていない。
- `Game::wrapX` と collision/liquid map の初期化が、raw direct local1 ではhost local0と同じ経路を通っていない。
- client local1でplayer environment flagsが `0x202` になり、プレイヤー側も液体/環境状態がhostと違う。
- RNG状態もhost/clientでまだ一致していないログがある。スター、8コインアイテム、ランダムステージ選択などに影響するため後続で再固定が必要。
- JITなし検証は約10-12fpsで遅い。JIT有効化はPacketBridge系hookの正しさに注意が必要。

## 次にやること

1. `StageEntity::updateLiquidCollision` の液体判定が client local1 だけ真になる根本原因を追う。
   - `0x020A6E70` / `0x020A9CF4` の戻り値と参照するcollision/tile mapを比較する。
   - host/clientで同じ座標なのに液体tile flagが違うのか、参照しているlayer/view/bufferが違うのかを切り分ける。
2. host local0で `Game::wrapX` と collision/liquid map がどの開始処理から初期化されるか特定し、raw client local1でも同じ初期化を自然に通す。
3. 診断patchで補正するのではなく、direct entry / StageStart 初期化側の最小patchへ落とす。
4. raw local1で Goomba spawn、速度、位置、player environment flags、スター/RNGがhost local0と一致するか確認する。
5. その後、Luigi camera/UI/stock item/death/win判定が local1 の自然処理で動くか検証する。

## 成功条件

`frame limit reached` だけでは成功扱いにしない。少なくとも次を確認する。

- data abort / fatal / undefined がない。
- 「通信が切断されました」画面がない。
- host/client で stage actor set が一致する。
- Goomba、スター、8コインアイテムなどランダム/動的要素が一致する。
- player0/player1 の actor座標、死亡状態、残機、スター数が一致する。
- client local1 でLuigi側のカメラ、UI、ストックアイテム、死亡/復帰、勝敗判定が自然に動く。
- screenshot が MvL stage として読める。

## 代表的な検証コマンド

host local0 単体:

```powershell
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 `
  -RunRole host `
  -Frames 960 `
  -Exe build\release-windows-x86_64\melonDS.exe `
  -Rom roms\nsmb-us-direct-mvl-entry-stable-host.tmp.nds `
  -HostRom roms\nsmb-us-direct-mvl-entry-stable-host.tmp.nds `
  -InputScript tests\nsmb_us_direct_mvl_both_different.inputs `
  -GameStateTrace `
  -GameStateTraceExtended `
  -GameStateTraceInterval 30 `
  -ScreenshotInterval 960 `
  -NoHashLog `
  -SkipDisconnectScreenshotCheck `
  -SkipBlankScreenshotCheck `
  -SkipMvlStateCheck `
  -SkipGameplayActorCheck `
  -SkipArmAbortCheck `
  -LogDir logs\...
```

client local1 + wrapX診断:

```powershell
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 `
  -RunRole client `
  -Frames 960 `
  -Exe build\release-windows-x86_64\melonDS.exe `
  -Rom roms\nsmb-us-direct-mvl-entry-stable-client-local1-forcewrapx2.tmp.nds `
  -ClientRom roms\nsmb-us-direct-mvl-entry-stable-client-local1-forcewrapx2.tmp.nds `
  -InputScript tests\nsmb_us_direct_mvl_both_different.inputs `
  -GameStateTrace `
  -GameStateTraceExtended `
  -GameStateTraceInterval 30 `
  -ScreenshotInterval 960 `
  -NoHashLog `
  -SkipDisconnectScreenshotCheck `
  -SkipBlankScreenshotCheck `
  -SkipMvlStateCheck `
  -SkipGameplayActorCheck `
  -SkipArmAbortCheck `
  -LogDir logs\...
```

## 運用ルール

- ROM生成物、savestate、巨大ログは git に含めない。
- docs は古い追記を残し続けず、現在の方針、成果済み、課題、次作業が上から読める形に保つ。
- 最終応答前に docs の古い情報や矛盾を確認し、必要なら整理する。
