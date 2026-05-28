# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` で WAN 越しに遊べる形へ持っていく。

過去に試した `LocalMP 2インスタンス * 2プロセス`、savestate共有、試合開始後のWAN切り替え、actor/state強制同期は、切断、desync、内部状態不一致、低FPSの問題が大きいため本筋から外す。

現在の本筋は次の通り。

- US版ROM `roms/nsmb-us.nds` / `A2DE` を主対象にする。
- `host localPlayerID=0`、`client localPlayerID=1` を最終ルートにする。
- NSMBが本来持っているMario vs Luigiの同期処理をできるだけ使う。
- ローカル無線packet API、またはROM/RAM patchで、試合中の入力packetをWAN adapterへ差し替える。

## 現在の到達点

完了:

- US版ROM patch toolingを追加した。
  - `tools/nsmb_us_rom_tool.py`
  - `tools/nsmb_us_rom_patch.py`
- direct MvL entry系ROM patchを作った。
- `external/NSMB-Code-Reference` のUS版シンボルを参照して、主要なpacket helperを特定した。
  - `Net::getConsoleKeys`
  - `Net::getPacketByte`
  - `Net::getPacketTick`
  - `Net::getPacketAction`
- screenshot dump、game-state trace、extended trace、packet replay trace、RAM dumpなどの検証フックを追加した。
- `wifi-communicating-consoles --count 2` と `rng-constant --value 0x100` の診断patchで、初期ステージ状態、Goomba系moving hazard、液体collision、初期スター位置をhost/clientで揃えられることを確認した。
- runtime hookで `ForceWifiCommunicatingCount=2` と `ForceNetLocalAid=1` をframe 840以降に適用すると、client local1のローカル入力がplayer1へ流れることを確認した。
- ARM-only packet hookと `ScriptRemotePacket` 診断を追加し、JIT無効ではNSMBのpacket API経由でremote player入力を注入できる。
- `ScriptRemotePacket` がpeer側入力ファイルを別に読めるようにした。
  - host単体検証では `client.inputs` をremote player1 packetに使う。
  - client単体検証では `host.inputs` をremote player0 packetに使う。
- JIT無効のoffline検証で、host local0とclient local1が同じ2人分の入力を受け取り、frame 960/1020/1200/1260のplayer0/player1座標が一致した。
- single-role offline packet検証ではLAN startログが出ないため、`PacketBridgeArmOnly + ScriptRemotePacket` の場合はscript側のLAN start必須チェックを外した。
- JIT有効時にもremote入力を高速検証できるよう、`Net::getConsoleKeys` だけをscratch memory参照へ差し替える診断patchを追加した。

直近の重要結果:

- host local0 offline remote1:
  - log: `logs/codex-host-local0-offline-remote1-peerinput-trace-1300-20260528`
  - frame 960: `inputPlayer0Held=0x11`, `inputPlayer1Held=0x21`
  - frame 1200: `playerActor0X=0x847f0`, `playerActor1X=0xfffc8000`
- client local1 offline remote0:
  - log: `logs/codex-client-local1-offline-remote0-peerinput-trace-1300-20260528`
  - frame 960: `inputPlayer0Held=0x11`, `inputPlayer1Held=0x21`
  - frame 1200: `playerActor0X=0x847f0`, `playerActor1X=0xfffc8000`
- つまり、少なくとも短時間のoffline scripted packetでは、host/client simulationの主要player状態は一致している。
- JIT有効 + keys helper patchでもhost/clientは一致した。
  - logs:
    - `logs/codex-host-local0-offline-remote1-jit-keys-helper900-fix-1300-20260528`
    - `logs/codex-client-local1-offline-remote0-jit-keys-helper900-fix-1300-20260528`
  - frame 1200: host/clientとも `playerActor0X=0x70ff0`, `playerActor1X=0xfffc8000`
  - 約49から50fpsで完走した。
- 同一PC上のhost/client 2プロセスで、ENetの `WireInput` をkeys helper scratchへ接続した。
  - log: `logs/codex-both-waninput-jit-keys-helper900-wait-1300-20260528`
  - frame 960/1020/1200/1260でhost/clientのplayer0/player1入力と座標が一致した。
  - 初動でframe 0から2のremote input timeoutがまだ残り、実効fpsはhost約35fps、client約42fps。
  - `-NoLocalWait` を使うとtimeoutは消えるが、remote入力が間に合わないframeが出てhost/clientがズレたため、現時点では正しい検証には使わない。

## 未解決

- ARM-only packet hook自体はJIT有効時に踏まれない。
  - ただし `Net::getConsoleKeys` のscratch helper patchなら、JIT有効でもscripted remote入力の短時間一致検証は可能になった。
  - `getPacketByte/getPacketTick/getPacketAction` までpatchすると試合開始状態を壊したため、現時点のJIT helper patchはkeys限定にする。
- PacketBridge + keys helperのboth検証では、起動直後に既存lockstep側のremote input timeoutが残っている。最終的にはnetplay開始前にremote inputを待たないよう整理して、初動の15秒前後のロスを消す必要がある。
- runtime `DirectMvlBoot` / firstScene直行は、SND/heap周辺の初期化不足でdata abortまたは停止になりやすく、安定入口としては使わない。
- `wifi-communicating-consoles --count 2` はStageLayout後には有効だが、VSConnect初期化中に常時patchすると壊れることがある。最終的には起動前ROM patchへ落とす前に適用タイミングを詰める必要がある。
- 現在の一致確認はscripted inputの短時間検証であり、まだ実WAN adapter、遅延、packet loss、長時間対戦、スター再出現、8コインアイテム、ランダムステージ選択までは検証できていない。

## 次にやること

1. PacketBridge + keys helperの初動remote input timeoutを消す。
2. WAN adapterでhost/clientを同時起動し、ローカルネットワーク上で50fps前後の検証ループを安定化する。
3. 入力を長めに流して、playerが止まらず自然に移動し続けるか確認する。
4. 長時間の決定性確認を追加する。
   - player actor
   - active object set
   - Goomba / moving hazard
   - Big Star
   - 8コインアイテム
   - ランダムステージ
5. `getPacketByte/getPacketTick/getPacketAction` もWAN化が必要かを、実WAN adapter検証後に判断する。
6. Luigi側UI、カメラ、stock item、死亡/復帰、勝敗判定がlocalPlayerID=1の自然処理で動くかを検証する。

## 成功条件

`frame limit reached` だけでは成功扱いにしない。最低限、次を確認する。

- data abort / fatal / undefined がない。
- 「通信が切断されました」画面にならない。
- screenshotがMario vs Luigi stageとして読める。
- host/clientでstage actor setが一致する。
- host/clientでplayer0/player1 actor座標、死亡状態、残機、スター数が一致する。
- Goomba、Big Star、8コインアイテムなどの動的要素が一致する。
- client local1でLuigi側のカメラ、UI、stock item、死亡/復帰、勝敗判定が自然に動く。
- WAN adapter有効時に実用的なFPSで検証できる。

## 代表的な検証コマンド

host local0 + offline remote1:

```powershell
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 `
  -RunRole host `
  -Frames 1300 `
  -Exe build\release-windows-x86_64\melonDS.exe `
  -Rom roms\nsmb-us-direct-mvl-entry-stable-host-wificount2-rng100.tmp.nds `
  -HostRom roms\nsmb-us-direct-mvl-entry-stable-host-wificount2-rng100.tmp.nds `
  -InputScript tests\nsmb_us_direct_mvl_early_dual.inputs `
  -GameStateTrace `
  -GameStateTraceExtended `
  -GameStateTraceInterval 60 `
  -ScreenshotInterval 1300 `
  -NoHashLog `
  -SkipDisconnectScreenshotCheck `
  -SkipBlankScreenshotCheck `
  -SkipMvlStateCheck `
  -SkipGameplayActorCheck `
  -SkipArmAbortCheck `
  -ForceWifiCommunicatingCount 2 `
  -ForceWifiCommunicatingStartFrame 840 `
  -ForceNetLocalAid 0 `
  -ForceNetLocalAidStartFrame 840 `
  -PacketBridgeArmOnly `
  -PacketBridgeTrace `
  -PacketBridgeReplayOps keys,byte,tick,action `
  -PacketBridgeLiveFallbackWindow 180 `
  -PacketBridgeReplayReturnLookupTick `
  -ScriptRemotePacket `
  -ScriptRemotePacketPlayer 1 `
  -ScriptRemotePacketInputInstance 0 `
  -ScriptRemotePacketStartFrame 840 `
  -LogDir logs\...
```

client local1 + offline remote0:

```powershell
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 `
  -RunRole client `
  -Frames 1300 `
  -Exe build\release-windows-x86_64\melonDS.exe `
  -Rom roms\nsmb-us-direct-mvl-entry-stable-client-local1-wificount2-rng100.tmp.nds `
  -ClientRom roms\nsmb-us-direct-mvl-entry-stable-client-local1-wificount2-rng100.tmp.nds `
  -InputScript tests\nsmb_us_direct_mvl_early_dual.inputs `
  -GameStateTrace `
  -GameStateTraceExtended `
  -GameStateTraceInterval 60 `
  -ScreenshotInterval 1300 `
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
  -PacketBridgeArmOnly `
  -PacketBridgeTrace `
  -PacketBridgeReplayOps keys,byte,tick,action `
  -PacketBridgeLiveFallbackWindow 180 `
  -PacketBridgeReplayReturnLookupTick `
  -ScriptRemotePacket `
  -ScriptRemotePacketPlayer 0 `
  -ScriptRemotePacketInputInstance 0 `
  -ScriptRemotePacketStartFrame 840 `
  -LogDir logs\...
```

## 運用ルール

- ROM生成物、savestate、巨大ログはgitに含めない。
- docsは古い追記を残し続けず、現在の方針、完了、未解決、次作業が上から読める形に保つ。
- 最終応答前にdocsの古い情報や矛盾を確認し、必要なら整理する。
