# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦モード `Mario vs Luigi` を、最終的に WAN 越しの `1 melonDS instance * 2PC` で遊べる形にする。

現在の主方針は、DS LocalMP全体をWANへ伸ばすことではなく、NSMBのMvLが使う入力中心packetと接続state-machineを解析し、LocalMPの代わりにWAN adapterで必要packetを渡す方向。

## 採用しない方針

- `2 instances * 2PC` の決定論同期: 重く、LocalMP間でもactor/objectのズレが残る。
- savestate同期: 表示や座標だけ合わせても通信状態とactor生成状態が一致しない。
- DropMP / 試合途中WAN切替: 切替瞬間の停止でhost/client状態が壊れやすい。
- `MPInterface_LAN` の単純WAN化: LANなら成功するが、WAN相当の遅延で接続/承認段階が崩れる。
- UI操作なしの単純 `loadLevel` / `StartLoadLevel` 呼び出し: `stageGroup=9` までは作れるが、player actor/star actorが生成されず黒画面扱いになる。

## 現在の到達点

- 通常LAN経路は `stageGroup=9`, `vsMode=1`, player actors, Big Star actor まで到達できる。
- NSMB Centralの記述どおり、gameplay中のMvL packetは入力中心に見える。通常LAN captureでは `byte0-1=tick`, `byte2-3=keys`, `byte4=action`, `byte5=0`, `byte6-7=0xFFFF` が安定している。
- packet captureをreplay CSVへ変換する `scripts/convert-nsmb-packet-capture-to-replay.ps1` を追加した。
- 通常LAN上でのreplay検証では、full packet replayは接続状態を壊すが、`keys` のみのreplayは4200フレーム通過した。つまり、入力置換は有効だが、tick/action/周辺byteを丸ごと上書きするのは危険。
- NoLanMP + packet bridge from start は、host/client双方でpacket送受信できるが、自然には接続完了へ進まない。
- `ForceNetReady` により `vsMode=1`, player ID, `netState14=1`, `netState1C=6`, `netState20=2` までは作れる。
- `PacketBridgeForceTick` により、client側のpacket tick停止は解消できた。
- `ForceLoadGameSM` のstep 3初期値を通常LAN寄りに修正した。flags/timer/actionの不自然な値は減ったが、まだ自然なstage loadには進まない。
- safe `StartLoadLevel` 呼び出しでhost/clientとも `stageGroup=9` までは到達できる。ただし player actors と Big Star actor は生成されないため、成功扱いにしない。

## 現在の主要ブロッカー

NoLanMP + WAN packet bridge from start では、NSMBの接続完了後に必要な「gameplay開始前の内部状態」がまだ不足している。

具体的には、通常LANでは `VSConnect::updateLoadGameSM` が `step 3 -> 5 -> 7` へ進み、その後 `StartLoadLevel(0x0214E0C0)` と `Game::loadLevel(0x020068A8)` に到達する。bridge強制ルートでは、この遷移を手で作っても `stageGroup=9` だけが立ち、actor生成まで進まない。

## 実装済みフック

- 入力スクリプト再生
- screenshot / framebuffer dump
- game-state trace
- call trace / write trace
- MainRAM dump
- packet capture / packet replay / packet bridge trace
- Big Star actor ID `0x00D2` 周辺probe
- `ForceNetReady`, `ForceLoadGameSM`, `ForceTransferResult`
- `PacketBridgeForceTick`
- `SafeStartLoadCall`, `SafeCourseSelectFactoryCall`
- 黒画面検出と通信切断風画面検出の分離

## 重要アドレス

- `Net::getConsoleKeys(u16)`: `0x0200E700`
- `Net::getPacketByte(u16,u32)`: `0x0200E978`
- `Net::getPacketTick(u16)`: `0x0200E9BC`
- `Net::getPacketAction(u16)`: `0x0200E9DC`
- `Net::Core::transferPacket`: `0x0200F98C`
- lower MP status probe: `0x0204619C`
- lower MP getPacket: `0x02046480`
- packet tick/key/action buffer: `0x02087F00`
- net state base: `0x02087E00`
- `Game::vsMode`: `0x020850C4`
- local player ID: `0x020850BC`
- `VSConnect::createLoadGameSM`: `0x021515B4`
- `VSConnect::updateLoadGameSM`: `0x021512B8`
- `VSConnect::startLoadLevel`: `0x0214E0C0`
- `Game::loadLevel`: `0x020068A8`
- `CourseSelectFactory`: `0x020130A8`

## 直近の検証ログ

- 通常LAN packet capture: `logs/nsmvl-baseline-packetcapture-gameplay-inputs-20260519-215929-attempt1`
- keys-only replay成功: `logs/nsmvl-replay-keys-only-baseline-20260519-continue2`
- bridge from start + ForceNetReady: `logs/nsmvl-bridge-from-start-force-netready-20260519-continue`
- bridge from start + ForceTick + ForceLoadGameSM: `logs/nsmvl-bridge-start-forcetick-loadgamesm-patched-20260519-continue`
- bridge from start + safe StartLoadLevel: `logs/nsmvl-bridge-start-step7-safe-startload-20260519-continue`

## 次にやること

1. 通常LANの `VSConnect::updateLoadGameSM` 周辺で、actor生成に必要な追加状態を書き出す。
2. `stageGroup=9` 直後に不足しているglobal/scene/player生成条件を、通常LANとbridge強制ルートで比較する。
3. `SafeStartLoadCall` でstageGroupだけ立つ原因を特定し、player actor/star actor生成に必要な状態を最小限で補う。
4. actor生成まで到達したら、keys-only WAN packet bridgeと結合して、入力が双方で反映されるか確認する。

## 必要なもの

- 検証にはユーザー提供の `roms/nsmb.nds` を使う。
- ROMパッチ生成へ進む場合は、日本版 `A2DJ` 専用の差分パッチとして、元ROMを含めない形で管理する。

## 参考

- NSMB Central: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi
