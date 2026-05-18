# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード `Mario vs Luigi` を、最終的に WAN 越しの `1 EmuInstance * 2PC` で遊べる形にする。

現在の方針は、melonDS の Local MP / savestate / DropMP に依存しない形へ切り替える。NSMB 側の Mario vs Luigi 初期化を解析し、UI 操作や LocalMP 接続を使わずに gameplay state を直接作る hook または ROM patch を目指す。

## 現在の判断

- NSMB Central の解析と packet capture は一致しており、Mario vs Luigi の gameplay packet はほぼ `tick / keys / action` だけを運んでいる。
- そのため、本命は座標やスター状態を継続的に強制同期する方式ではなく、NSMB が本来持っている入力同期モデルを使い、transport だけ WAN に差し替える方式。
- 低レベル Local MP を途中で止める DropMP 方式は打ち切る。NSMB 側の接続状態・動的 object・transfer 状態が崩れ、長期安定に向かない。
- savestate 開始方式も打ち切る。表面上の player 座標が一致しても、object `0x0053` などの動的 stage object が host/client でズレ、片側だけ死亡・diverge する。
- `Net::getPacket*()` への direct replay で remote input を読ませる部品は有効なので、これは最終方式にも残す。
- 判定基準は framebuffer の host/client 一致ではない。死亡演出・カメラ・ローカル役割の表示は一致しないことがあるため、検証では「切断なし」「remote key hit」「actor のゲーム内座標・主要カウンタ」を見る。
- 次の主 blocker は、LocalMP が作っていた「MvL gameplay state」を、NSMB memory/hook/patch 側で直接作れるかどうか。

## 実装済み

- `scripts/run-nsmb-mvl-lan-route-smoke.ps1`
  - Mario vs Luigi 到達 smoke test
  - screenshot / framebuffer dump
  - game state trace
  - packet capture / packet replay / packet bridge
  - Local MP drop: `-DropMPAfterFrame`
  - disconnect 抑制: `-PacketBridgeSuppressDisconnect`, `-PacketBridgeBypassNetDisconnect`
  - transfer 成功強制: `-PacketBridgeForceTransferResult`
  - RNG 固定: `-NetRandomValue`, `-NetRandomFrame`, `-NetRandomAuto`
  - canonical tick: `-PacketBridgeForceTick`, `-PacketBridgeForceTickStartFrame`, `-PacketBridgeForceTickBase`
  - replay tick delay: `-PacketBridgeLookupTickDelay`
  - frame lead throttle: `-PacketBridgeMaxFrameLead`, `-PacketBridgeThrottleTimeoutMs`
  - network pump上限: `-PacketBridgeMaxPumpEvents`
- `scripts/verify-nsmb-mvl-lan-result.ps1`
  - host/client stdout の切断・blank marker 検出
  - `host.game-state.csv` / `client.game-state.csv` の actor 座標・主要カウンタ照合
  - packet replay log の remote non-zero key hit 確認
- `scripts/analyze-nsmb-object-dump.ps1`
  - MainRAM dump から NSMB object らしい構造を抽出
  - savestate 方式の破綻原因として、player 以外の dynamic object 差分を確認するために追加
- melonDS hook
  - `Net::getConsoleKeys()` / `Net::getPacketByte()` / `Net::getPacketTick()` / `Net::getPacketAction()` の packet replay
  - `Net::update()` disconnect branch skip
  - `Net::Core::transferPacket()` 候補の成功値強制
  - packet bridge canonical tick
  - A2DJ Mario vs Luigi 判定、固定 RTC、JIT 無効化、状態 trace
  - dynamic object `0x0053` の trace/sync 候補

## 重要アドレス

- `Net::getConsoleKeys(u16)`: `0x0200E700`
- `Net::getPacketByte(u16,u32)`: `0x0200E978`
- `Net::getPacketTick(u16)`: `0x0200E9BC`
- `Net::getPacketAction(u16)`: `0x0200E9DC`
- `Net::Core::transferPacket(Net::PacketAction)` 候補: `0x0200F98C`
- `Net::update()` disconnect branch call: `0x02010174`
- packet tick/key buffer: `0x02087F00`
- LocalMP packet slot status: `0x0208AE50`
- LocalMP packet buffer: `0x0208B040 + player * 0x3E`
- NSMB disconnect flags 付近: `0x02087E5C`
- NSMB network state 付近: `0x02087E1C`
- MvL の moving hazard 候補 object ID: `0x0053`

## 直近の検証

- `logs\lan-route-5520-nodrop-packetcapture-movement-attempt1`
  - 通常 LocalMP の移動中 packet capture。
  - payload 側の可変値は見えず、主に tick/key/action を運んでいる。座標・速度・スター所有権を packet に載せる設計ではなさそう。
- `logs\lan-route-loadstate3600-localmp-adapter-replayops-escape-attempt1-attempt1`
  - LocalMP 由来の frame 3600 state から packet bridge/replay で 1600 frame 完走。
  - remote key は hit していたが、host 側だけ player1 が死亡し、その後 actor 座標が diverge。
  - これは死亡演出の見た目差ではなく、開始 state が移動済みで悪く、host/client のゲーム内状態が一致していない問題。
- `logs\localmp-state-source-3200-clean-attempt1`
  - `tests/nsmb_mario_vs_luigi.inputs` でクリーンな LocalMP 試合開始 state を frame 3200 に保存。
  - 保存時点の actor0/actor1 座標は inst0/inst1 で一致。
- `logs\lan-route-loadstate3200-localmp-clean-adapter-replayops-escape-attempt1`
  - クリーン state を host/client に分割して、LocalMP を frame 1 で止め、WAN adapter + direct `getPacket*()` replay + slot refresh で実行。
  - 1600 frame 完走し、切断 marker なし。
  - `scripts/verify-nsmb-mvl-lan-result.ps1 -FromFrame 200 -RequireRemoteInputHits` が PASS。
  - frame 200 以降、actor0/actor1 の X/Y と主要カウンタが host/client で一致。
- `logs\localmp-state-source-3200-ramdump-a` / `logs\localmp-state-source-3200-ramdump-b`
  - 同じ route でも MainRAM object dump 上の object `0x0053` 位置が host/client/run 間で異なる。
  - player 座標だけ一致しても、動的 stage object が揃っていなければ片側だけ死亡する。
  - この結果により、savestate 方式は最終方針から外す。

## 次にやること

1. LocalMP 接続後の MvL gameplay memory を RAM dump/object dump から整理し、UI/LocalMPなしで最低限必要な global・player・stage object・Net 状態を特定する。
2. melonDS hook で「MvL direct boot」モードを作る。通常起動後、指定 frame で MvL gameplay state を構築し、`Net::getPacket*()` adapter を最初から使わせる。
3. direct boot が重すぎる場合は ROM patch 側へ寄せ、NSMB の menu/Net 初期化関数を直接呼ぶ、または MvL scene 遷移を短絡する。
4. 成功判定は、LocalMPなしの `1 EmuInstance * 2 process` で「通信切断なし」「remote key hit」「actor/object 状態が機械判定で一致」。

## 必要なもの

- 現在の検証は、ユーザー提供の `roms/nsmb.nds` を前提にしている。
- 最終的な ROM patch 検証では、同じ日本版 `A2DJ` ROM に対する patch 生成・適用手順を用意する。
