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

2026-05-28時点の次の焦点は、`client localPlayerID=1` の開始状態で `Net::localAid` と通信相手数をいつ・どの層で2P扱いにするか。常時patchでは起動前のVSConnect側が壊れるため、StageLayout/試合開始後に効かせる最小patchへ寄せる。

## 直近の重要な発見

raw `client localPlayerID=1` は、入力同期前から stage actor / physics 状態がhost local0と一致していなかった。Goomba欠落はStageObject list欠落ではなく、`Game::wrapX` と液体/collision初期化の不一致が原因だった。

StageLayout初期化をtraceした結果、液体slot不一致の根本は `Wifi::getCommunicatingConsoleCount()` がdirect single-instance bootでは1を返し、`Stage::liquidPosition[1]` が初期化されないことだった。診断patch `wifi-communicating-consoles --count 2` を当てると、slot0/slot1の水面値、Goomba、player environment flags はhost local0と一致した。

RNGはdirect entryのseedだけでは一致しない。診断patch `rng-constant --value 0x100` をhost/client両方に当てると、初期スター座標は一致した。

`wificount2 + rng100` の比較では、frame 960時点で以下がhost/client一致した。

- `stageLiquidHeight0/1 = 0xFF000000`
- Goomba系 moving hazard の座標/速度/lastStep
- player actor 0/1 の座標と環境フラグ
- object active count
- 初期スター座標

JIT強制許可 `MELONDS_NSML_ALLOW_JIT` を追加し、trace検証速度は約45-50fpsまで改善した。

一方、client local1 でも `Net::localAid` が0のままだと、NSMBの `Net::getConsoleKeys(player)` はlocal入力をplayer0へ流す。Luigiを自然操作するには `Net::localAid=1` を正しいタイミングで成立させる必要がある。

runtime hookで `ForceWifiCommunicatingCount=2` と `ForceNetLocalAid=1` をframe 840以降に適用したところ、client local1 は2600frameまで約49fpsで完走し、trace上で `inputConsole1Held` が `inputPlayer1Held` へ流れ、player1 actorが移動した。host local0側は同条件で `Net::localAid=0` のまま `inputPlayer0Held` が立つ。入力前frame 1980では host/client とも `playerActor0X=0x8000`, `playerActor1X=0x58000` で一致しており、local1自然操作ルートの前提はかなり改善した。

さらにARM-only packet hookと `ScriptRemotePacket` 診断を追加し、JIT無効ではhost local0内でplayer0 local入力とplayer1 remote packet入力を同時に入れられることを確認した。client local1側でもremote player0 packetとlocal player1入力が入る。ただし、現時点ではlocal入力経路とremote packet経路のkey表現が完全には揃っておらず、host/clientの座標はまだ一致しない。次はこのkey/tick変換を詰める。

注意点:

- `wifi-communicating-consoles --count 2` をROMに常時patchすると、VSConnect起動直後の初期化でnull参照/data abortを起こす場合がある。
- runtime `DirectMvlBoot` / firstScene直行も、現状はSND/heap周辺の初期化不足でdata abortまたは極端な低速停止になりやすい。
- よって通信相手数とlocalAidは、起動直後ではなく、StageLayout/試合開始後に限定して成立させる方が筋が良い。

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
- stage liquid slot / liquid height trace
- host/client split 実行ランナー
- local0 hybrid の比較基盤
- RNG seed固定と初期スター座標一致の検証
- 診断patch
  - `stage-object-activation-force-wrap-x`
  - `stage-entity-liquid-check-result`
  - `wifi-communicating-consoles`
- runtime診断フック
  - `MELONDS_NSML_ALLOW_JIT`
  - `MELONDS_NSML_FORCE_NET_LOCAL_AID`
  - `MELONDS_NSML_FORCE_WIFI_COMMUNICATING_COUNT`
  - `MELONDS_NSML_PACKET_BRIDGE_ARM_ONLY`
  - `MELONDS_NSML_SCRIPT_REMOTE_PACKET`

未解決:

- raw `client localPlayerID=1` の正常開始状態がまだ作れていない。
- `wificount2` はStageLayout後には有効だが、VSConnect初期化中に常時2を返すと壊れる。適用タイミングを限定する必要がある。
- `Net::localAid=1` のruntime適用でclient local1の入力はplayer1へ流せた。ただし、最終的にはWAN adapter側でremote inputも双方へ入れる必要がある。
- ARM-only packet hookはJIT無効で効く。JIT有効時には現状hitしていない可能性があり、最終的に高速検証するにはJIT側hook対応か、packet hook対象をROM patchへ移す必要がある。
- local input経路とremote packet経路でkey表現がまだ完全一致していない。host/client双方に同じ2人分入力を入れても座標が一致しないため、ここが次の本筋。
- RNG状態は診断用定数化では一致するが、最終的にはhost/clientで同じ乱数列になるROM patchまたはseed同期が必要。
- runtime `DirectMvlBoot` / firstScene直行はまだ安定入口になっていない。

## 次にやること

1. `ScriptRemotePacket` のkey/tick変換を修正し、local入力経路とremote packet経路で同じ操作結果になるようにする。
2. JIT無効の短縮input scriptで、host local0 / client local1 のplayer0/player1座標が一致するか確認する。
3. ARM-only packet hookをJIT有効でも使えるようにするか、同等のpacket API差し替えをROM patchへ移す。
4. runtime診断で成立した `wifi communicating count` と `netLocalAid` を、起動前VSConnectを壊さない条件付きROM patchへ落とし込む。
5. RNGを定数化ではなくhost/clientで同じ列になる形へ寄せる。初期スターの後、8コインアイテムやランダムステージ選択も確認する。
6. Luigi camera/UI/stock item/death/win判定が local1 の自然処理で動くか検証する。

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

client local1 + runtime通信状態診断:

```powershell
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 `
  -RunRole client `
  -Frames 960 `
  -Exe build\release-windows-x86_64\melonDS.exe `
  -Rom roms\nsmb-us-direct-mvl-entry-stable-client-local1-rng100.tmp.nds `
  -ClientRom roms\nsmb-us-direct-mvl-entry-stable-client-local1-rng100.tmp.nds `
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
  -ForceWifiCommunicatingCount 2 `
  -ForceWifiCommunicatingStartFrame 840 `
  -ForceNetLocalAid 1 `
  -ForceNetLocalAidStartFrame 840 `
  -LogDir logs\...
```

このコマンドは検証用の雛形。`DirectMvlBoot` / firstScene直行は現状まだ不安定なので、実際の開始ルートに合わせてframeとROMを調整する。

## 運用ルール

- ROM生成物、savestate、巨大ログは git に含めない。
- docs は古い追記を残し続けず、現在の方針、成果済み、課題、次作業が上から読める形に保つ。
- 最終応答前に docs の古い情報や矛盾を確認し、必要なら整理する。
