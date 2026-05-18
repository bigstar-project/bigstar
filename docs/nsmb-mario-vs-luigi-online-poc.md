# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード `Mario vs Luigi` を、最終的に WAN 越しの `1 melonDS instance * 2PC` で遊べる形にする。

## 現在の方針

`LocalMP 2インスタンス * 2プロセス`、savestate同期、DropMP、試合途中WAN切替は本筋から外す。現実的な本命は次のどちらか。

1. MvsLの接続/ロビー段階から、LocalMPの代わりにWAN packet adapterを使う。
2. NSMB側をさらに解析し、UI操作やLocalMPを経ずにMvsL開始状態を作る。

今は1を優先している。理由は、NSMB側にはローカル通信時の入力/packet同期処理があり、それを可能な限り流用できれば、melonDS側で2台分のゲーム状態を無理に同期するより最終形に近いから。

## 採用しない方針

- savestate方式: 表示座標は揃ってもstage/object全体が一致せず、対戦基盤として弱い。
- DropMP方式: 途中でLocalMPから切り替えるとNSMB側の接続状態やobject状態が壊れる。
- raw NiFi/MP frameの単純WAN転送: 60ms程度の遅延でDS Wi-Fi channel scanがずれ、`received frame but bad channel` が出て接続段階を越えにくい。
- 死亡演出や目視スクショだけで成功判定すること: host/clientで一致しない表示があり得るため、内部状態、packet trace、game-state traceを優先する。

## 実装済み

- 自動検証フック
  - 入力スクリプト再生
  - screenshot/framebuffer dump
  - MainRAM dump
  - game-state trace
  - packet trace / packet bridge trace
- NSMB packet系hook
  - `Net::getConsoleKeys()`
  - `Net::getPacketByte()`
  - `Net::getPacketTick()`
  - `Net::getPacketAction()`
  - `Net::Core::transferPacket()`
  - disconnect/reset bypass検証
- RNG検証hook
  - `Net::getRandom()` 固定patch
  - Big Star actor ID `0x00D2` 周辺probe
- LAN/WAN adapter検証
  - `MPInterface_LAN` のWAN mode
  - recv/stale/reliable/send delayのenv化
  - `scripts/run-nsmb-mvl-lan-route-smoke.ps1` のLAN/NoLAN/PacketBridge検証オプション
- NoLAN PacketBridge検証
  - pre-game packet context対応
  - role別 `LOCAL_INSTANCE` 修正
  - `ggid=0x42` 以前/以後のpacket bridge許可
  - `Net ready` 強制フック
  - VSConnect load-game state machine強制フック
  - DirectBoot系envをテストスクリプトから渡せるようにした
- game-state trace拡張
  - Net global: `0x02087E14/1C/20/24/5C`
  - packet buffer: `0x02087F00/02/04..07`
  - VSConnect object主要フィールド
  - CourseSelect object検出

## 重要アドレス

- `Game::loadLevel`: `0x020068A8`
- `VSConnect::createLoadGameSM`: `0x021515B4`
- `VSConnect::updateLoadGameSM`: `0x021512B8`
- `VSConnect::renderLoadGameSM`: `0x0215125C`
- VSConnect完了処理候補: `0x0214E0C0`
- CourseSelect生成候補: `0x0214F830`
- object manager create: `0x0204BF8C`
- `Net::getConsoleKeys(u16)`: `0x0200E700`
- `Net::getPacketByte(u16,u32)`: `0x0200E978`
- `Net::getPacketTick(u16)`: `0x0200E9BC`
- `Net::getPacketAction(u16)`: `0x0200E9DC`
- packet tick/key buffer: `0x02087F00`
- LocalMP packet slot status: `0x0208AE50`
- LocalMP packet buffer: `0x0208B040 + player * 0x3E`

## 直近の検証結果

- 通常LANルートは4200フレームまで到達し、`stageGroup=0x9, vsMode=0x1` に入る。
- raw MP WAN modeは無遅延なら通る場合があるが、`LanMPSendDelayMs=60` でchannel scanが崩れてMvsL状態に入れない。
- NoLAN + pre-game PacketBridgeは、host/client間でMvsL packetを交換できる。
- NoLAN + PacketBridgeだけでは `ggid=0x42` までは入るが、hostは `netState1C=3`、clientは `netState1C=4` 付近で止まり、`vsMode/stageGroup` が進まない。
- `Net ready` 強制により `vsMode=1` までは進むが、`stageGroup=9` には入らない。
- 成功LANルートでは、VSConnectが `word078/07C=3`、関数ポインタが `createLoadGameSM/updateLoadGameSM/renderLoadGameSM`、`word144=7, word148=0x30, word154=0x30000` になった後、CourseSelectが生成され、`stageGroup=9` に進む。
- NoLANでVSConnect load-game state machineを強制しても、CourseSelectはまだ生成されない。
- DirectBoot trampolineで `updateLoadGameSM` や object manager CourseSelect生成を直接呼ぶと、現状はARM9 abortし、安定ルートにはなっていない。

## 現在のblocker

NoLAN/WAN adapterルートで、NSMBの接続状態は `ggid=0x42` と `Net ready` まで作れるが、成功LANルートで起きるVSConnectからCourseSelect生成への遷移が再現できていない。

## 次にやること

1. 成功LANルートのRAM dumpを、VSConnectがload-game state machineへ変わる前後とCourseSelect生成前後で取る。
2. NoLAN + ForceNetReady/ForceLoadGameSMルートの同フレーム帯RAM dumpと比較する。
3. VSConnect以外にCourseSelect生成条件になっているglobal/object/list/resource状態を特定する。
4. DirectBoot trampolineは復帰PC/CPU状態が危険なので、継続するなら呼び出し規約を見直す。安定しない場合はROM patch側で本来の分岐/呼び出しを差し替える方向に寄せる。
5. CourseSelect生成後に `stageGroup=9` へ入れたら、PacketBridgeの入力packet同期と内部状態traceで、死亡演出ではなくゲーム状態が一致するか確認する。

## 必要なもの

- 検証にはユーザー提供の `roms/nsmb.nds` を使用する。
- ROM patch化へ進む場合は、日本版 `A2DJ` 向けのpatch生成/適用手順を別途作る。
