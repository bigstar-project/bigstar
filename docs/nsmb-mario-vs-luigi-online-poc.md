# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` で WAN 越しに対戦できる形へ持っていく。

過去に試した `LocalMP 2インスタンス * 2プロセス`、savestate 共有、試合開始後の WAN 切り替え、actor/state 強制同期は、切断、desync、不自然な内部状態、低 FPS の問題が大きいため最終方針から外す。

## 現在の方針

US 版 ROM `roms/nsmb-us.nds` (`A2DE`) を主対象にする。`external/NSMB-Code-Reference` が US 版のシンボルを持つため、ROM patch と通信 API 解析を進めやすい。

最終ルートは `host localPlayerID=0` / `client localPlayerID=1` を目指す。

理由:

- `client localPlayerID=0` hybrid は短期PoCと比較基盤としては有効だが、clientが内部的にはMario側のままになる。
- その場合、カメラ、死亡演出、サウンド、ストックアイテム使用、勝敗判定、描画culling、画面外判定などを個別にpatchし続ける必要があり、最終的な対戦実装として筋が悪い。
- `client localPlayerID=1` でNSMB自身が期待する正常なMario vs Luigi開始状態を作れれば、Luigi視点/UI/死亡/勝敗などはゲーム側の既存処理を使える可能性が高い。

したがって当面は、UIや結果表示を補正するのではなく、`direct entry / VSConnect / StageStart / Game::loadLevel / StageObject activation` まわりを解析し、raw `client localPlayerID=1` の開始状態を host local0 と同じ simulation 条件へ近づける。

## 直近の重要な発見

2026-05-28 時点の raw direct local1 比較:

- host local0 と client local1 は、入力同期以前に stage actor set が一致していない。
- frame 870 付近で host local0 には Goomba がいるが、client local1 にはいない。
- 欠けている actor は `objectID=0x53`, `settings=0x0`。`external/NSMB-Code-Reference` では Goomba。
- host/client の StageObject list 自体は同一。`StageBlocks.stageObjs` は両方 `0x0229A9EC` で、先頭に `stageObjectID=0x94, x=13, y=16` があり、`Stage::objectIDTable[0x94] == 0x53`。
- host は frame 855 に `0x0209BF10` へ `r0=0x0229A9EC` を渡し、その後 `Actor::spawnActor(0x53, 0)` と `Goomba::onCreate` を呼ぶ。
- client local1 は同じ StageObject を持っているが、`0x0229A9EC` を `0x0209BF10` に渡さない。`0xc3` / `0xc4` の stage edge 系 object だけを spawn している。

結論:

- local1 の Goomba 欠落は「stage data が違う」「spawn後に消えている」ではない。
- StageObject activation の範囲/視点/slot 判定が local1 では別経路になり、先頭 Goomba を spawn 対象から外している。
- `stage-range-localplayer-literal-alias` と、試作した `stage-object-activation-player-id --player-id 0` だけでは Goomba spawn は戻らなかった。より上流または別 activation path を追う必要がある。
- 追加traceで、実際のGoomba spawn経路は `0x0209B040` ではなく `0x0209B320` 側だと分かった。hostは `0x0209B628 -> 0x0209BF10` へ進むが、raw client local1は同じStageObjectで `r7=0` のまま `0x0209B604` でskipする。
- 主な差分は `Game::wrapX`。host local0 は `0x02085AA4/0x02085AA8 = 0x003FFFFF` だが、raw client local1 は両方 `0xFFFFFFFF` のまま。これにより `0x0209B320` のwrap幅計算が0になり、左側Goombaがactivation範囲外扱いになる。
- 診断patch `stage-object-activation-wrap-width --width 0x400` でGoomba spawnは戻ったが、spawn後にdespawn/update不一致が残った。
- 診断patch `stage-object-activation-force-wrap-x --wrap-x 0x003FFFFF` で `Game::wrapX` を書き戻すと、client local1でもGoombaはactive維持される。ただしGoombaの実移動速度がhostとまだ一致しない。frame 900 RAM dumpではhostのGoomba内部X速度が `-0x800`、client local1は `-0x400`。

## 現在の実装状態

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
  - `objectActiveIdN`
  - `objectActiveSettingsN`
  - `objectActiveBaseN`
- host/client split 実行ラッパー
- local0 hybrid の比較基盤
- RNG seed 固定と初期スター座標一致の検証

未解決:

- raw `client localPlayerID=1` の正常開始状態がまだ作れていない。
- client local1 のGoomba spawn欠落は `Game::wrapX` 未初期化が主因。ただし `wrapX` を診断的に補正してもGoomba速度がhostと一致せず、simulation一致にはまだ届いていない。
- JIT + PacketBridge は高速だが、現状では入力hookがゲームロジックに正しく反映されないため成功判定には使えない。
- JITなしの検証は約10-12fpsで遅い。

## 次にやること

1. host local0で `Game::wrapX` がどの開始処理から `0x003FFFFF` へ初期化されるか特定し、raw client local1のdirect entryでも同じ初期化を自然に通す。
2. `Game::wrapX` 補正後もGoomba速度が `-0x400` になる原因を追う。Goomba onCreate/update、StageEntity速度、wrap/collision関数、MvL manager state のどれがhostと違うか比較する。
3. patchを足す場合は、表示や死亡カウンタの結果補正ではなく、開始状態の初期化漏れを埋める最小patchに限定する。
4. raw local1で Goomba spawn、速度、位置、stateType が host local0 と一致するか確認する。
5. その後、Luigi camera/UI/stock item/death/win判定が local1 の自然処理で動くか検証する。

## 成功条件

`frame limit reached` だけでは成功扱いにしない。少なくとも次を確認する。

- data abort / fatal / undefined がない。
- 「通信が切断されました」画面がない。
- host/client で stage actor set が一致する。
- Goomba、スター、8コインアイテムなどランダム/動的要素が一致する。
- player0/player1 の actor 座標、死亡状態、残機、スター数が一致する。
- client local1 でLuigi側のカメラ、UI、ストックアイテム、死亡/復帰、勝敗判定が自然に動く。
- screenshot が MvL stage として読める。

## 代表的な検証コマンド

host local0 単体:

```powershell
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 `
  -RunRole host `
  -Frames 2040 `
  -Exe build\release-windows-x86_64\melonDS.exe `
  -Rom roms\nsmb-us-direct-mvl-entry-stable-host.tmp.nds `
  -HostRom roms\nsmb-us-direct-mvl-entry-stable-host.tmp.nds `
  -InputScript tests\nsmb_us_direct_mvl_both_different.inputs `
  -GameStateTrace `
  -GameStateTraceExtended `
  -GameStateTraceInterval 30 `
  -ScreenshotInterval 2040 `
  -NoHashLog `
  -SkipDisconnectScreenshotCheck `
  -SkipBlankScreenshotCheck `
  -SkipMvlStateCheck `
  -SkipGameplayActorCheck `
  -SkipArmAbortCheck `
  -LogDir logs\...
```

client raw local1 単体:

```powershell
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 `
  -RunRole client `
  -Frames 2040 `
  -Exe build\release-windows-x86_64\melonDS.exe `
  -Rom roms\nsmb-us-direct-mvl-entry-stable-client-local1.tmp.nds `
  -ClientRom roms\nsmb-us-direct-mvl-entry-stable-client-local1.tmp.nds `
  -InputScript tests\nsmb_us_direct_mvl_both_different.inputs `
  -GameStateTrace `
  -GameStateTraceExtended `
  -GameStateTraceInterval 30 `
  -ScreenshotInterval 2040 `
  -NoHashLog `
  -SkipDisconnectScreenshotCheck `
  -SkipBlankScreenshotCheck `
  -SkipMvlStateCheck `
  -SkipGameplayActorCheck `
  -SkipArmAbortCheck `
  -LogDir logs\...
```

## 運用ルール

- ROM 生成物、savestate、巨大ログは git に含めない。
- docs は古い追記を残し続けず、現在の方針、達成済み、課題、次作業が上から読める形に保つ。
- 最終応答前に docs の古い情報や矛盾を確認し、必要なら整理する。
