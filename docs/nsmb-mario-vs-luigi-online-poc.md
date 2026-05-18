# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード `Mario vs Luigi` を、最終的に WAN 越しの `1 EmuInstance * 2PC` で遊べる形にする。

## 現在の方針

最終形から `LocalMP 2インスタンス * 2プロセス`、savestate同期、DropMP、途中WAN切替を外す。次は、NSMBが本来持っているローカル通信の同期処理を活かし、melonDSのMP transportを最初からWAN向けに差し替える方針を主軸にする。

優先順位:

1. `MPInterface_LAN` を起動直後から使う2プロセス構成をWAN前提で安定化する。
2. 受信packetの16ms stale破棄や25ms待ちなど、LAN前提の短すぎるタイムアウトをPoC用に可変化する。
3. それでもMvL開始経路がUI/接続状態に依存して不安定なら、NSMB側をさらに解析してUI操作なしでMvL開始状態を作る。

## 採用しない方針

- savestate方式: 表面上の座標が一致しても、stage objectや内部状態が一致せず、対戦の土台にならない。
- DropMP方式: LocalMP由来の接続/transfer状態を途中で止めると、NSMB側の接続状態やobject状態が崩れる。
- 途中までLocalMPで進めてWANへ切り替える方式: 切替時の停止や状態差分がゲーム上重要なズレになる。
- 死亡演出や見た目だけの一致を成功判定にしない。判定は接続状態、stage/player/object状態、packet trace、game-state traceを主に見る。

## 実装済み

- 自動検証フック
  - 入力スクリプト再生
  - screenshot / framebuffer dump
  - MainRAM dump
  - game-state trace
  - ARM call trace
  - A2DJ向けメモリprobe
- NSMB packet系hook
  - `Net::getConsoleKeys()`
  - `Net::getPacketByte()`
  - `Net::getPacketTick()`
  - `Net::getPacketAction()`
  - `Net::Core::transferPacket()`
  - disconnect branch skip検証
- RNG検証hook
  - `Net::getRandom()`固定patch
  - Big Star actor ID `0x00D2` 周辺dump/probe
- DirectBoot検証hook
  - `Game::loadLevel` 直接呼び出し
  - VSConnect load-game submenu状態patch
  - VSConnect完了処理 `0x0214E0C0` 呼び出し
  - CourseSelect生成候補 `0x0214F830` 呼び出し
  - object manager `0x0204BF8C(id=5,r1=0,r2=1,r3=1)` 呼び出し
- 2プロセスLAN自動起動
  - `MELONDS_NSML_MP_INTERFACE=lan`
  - `MELONDS_NSML_LAN_ROLE=host/client`
  - `scripts/run-nsmb-mvl-lan-route-smoke.ps1`

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

- `0x0214E0C0` を呼ぶと `Game::loadLevel` には到達するが、CourseSelect/Gameplay object生成までは成立しない。
- 実LocalMP経路では CourseSelect は `0x0204BF8C(5,0,1,1)` から生成され、その後 `0x0214F830` に入る。
- `0x0204BF8C(5,0,1,1)` をDirectBootから強制呼び出しすると object は作られ始めるが、`ARM9 data abort (02064508)` で落ちる。つまりobject manager呼び出しだけでは、scene/list/resource状態が足りない。
- この結果から、DirectBootでUIを飛ばす方向はまだ重い。先に「最初からWAN transport」の実装と検証を優先する。

## 現在のblocker

melonDSの既存 `LAN` MPInterface は同一LAN前提で、MP packet受信キューのstale判定が約16ms、MP reply待ちが25msになっている。WAN相当の遅延ではNSMBに届く前にpacketを捨てる可能性が高い。

## 次にやること

1. `LAN` MPInterfaceにPoC用WAN設定を追加する。
   - stale破棄時間を可変化する。
   - MP recv timeoutを可変化する。
   - 必要ならMP data channelのreliable送信を切り替え可能にする。
2. `run-nsmb-mvl-lan-route-smoke.ps1` にWAN modeオプションを追加する。
3. 2プロセスを最初からLAN/WAN MPInterfaceで起動し、接続切断画面ではなくgame-state traceとpacket traceで成功/失敗を判定する。
4. これでロビー/接続段階が通らなければ、NSMB側の接続開始処理をさらに解析し、UI操作なしのMvL開始patchへ戻る。

## 必要なもの

- 検証はユーザー提供の `roms/nsmb.nds` を前提にする。
- ROM patch化する段階では、日本版 `A2DJ` 向けのpatch生成/適用手順を別途作る。
