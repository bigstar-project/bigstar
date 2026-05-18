# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード `Mario vs Luigi` を、最終的に WAN 越しの `1 EmuInstance * 2PC` で遊べる形にする。

現在の方針は、melonDS の Local MP をそのまま WAN に流す方式ではなく、NSMB が対戦中に扱うゲーム側の通信・乱数・接続状態を解析し、melonDS hook または ROM patch で置き換える方向。

## 現在の判断

- Local MP を残したままなら、既存の2台通信で Mario vs Luigi は進行できる。
- Local MP を途中で止めると、NSMB 側の接続状態と低レベル transfer 処理が失敗扱いになり、黒画面または「通信が切断されました」に落ちる。
- `Net::update()` の disconnect 分岐 `0x02010174` を skip し、`Net::Core::transferPacket()` 候補 `0x0200F98C` を成功値 `0x8` で返すと、DropMP 後も `Net::getPacket*()` 呼び出しは継続する。
- `Net::random` を固定し、packet replay 側で canonical tick を使うと、Big Star の出現位置などの乱数要素は揃えられる。
- ただし、DropMP 後に host/client のプレイヤー座標・スター数・hash はまだ一致しない。
- packet capture の結果、Mario vs Luigi gameplay packet はほぼ `tick / keys / action` で、座標やスター所有権そのものは含まれていない。したがって「ローカル無線packetをWANに流すだけ」で全状態が揃う前提は弱い。
- 現在の主 blocker は、乱数ではなく、ローカルプレイヤーは即時シミュレーションされ、相手プレイヤーは packet 経由で遅延反映されることによる非対称。スター取得・スコア・アイテムなどは追加の権威同期が必要になる可能性が高い。

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
- melonDS hook
  - `Net::getConsoleKeys()` / `Net::getPacketByte()` / `Net::getPacketTick()` / `Net::getPacketAction()` の packet replay
  - `Net::update()` disconnect branch skip
  - `Net::Core::transferPacket()` 候補の成功値強制
  - packet bridge canonical tick
  - A2DJ Mario vs Luigi 判定、固定 RTC、JIT 無効化、状態 trace

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

## 直近の検証

- `logs\lan-route-4200-dropmp3600-force-transfer8-canonicaltick-fixedrng100-framelead60-remotegate-trace-attempt1`
  - 4200 frame まで完走。
  - Big Star と RNG は一致。
  - host 側で client packet が replay 時点に間に合わず、player1 hit が 0 になる frame がある。
- `logs\lan-route-4200-dropmp3600-force-transfer8-canonicaltick-fixedrng100-lookupdelay240-framelead60-pump8-trace-attempt1`
  - pump上限を下げても、host が client packet を十分に受け取れない非対称は解消しなかった。
- `logs\lan-route-4800-dropmp4200-force-transfer8-canonicaltick-fixedrng100-framelead60-pump8-attempt2`
  - DropMP を 4200 に遅らせると、4200時点では host/client のプレイヤー座標とスター数が一致。
  - その後、入力なしでも 4800時点で player座標・スター数が diverge。
- `logs\lan-route-4440-dropmp4200-force-transfer8-canonicaltick-fixedrng100-framelead60-pump8-trace-attempt1`
  - DropMP後の packet replay は両プレイヤーとも hit する。
  - それでも状態がずれるため、単純な packet 到着漏れだけでは説明できない。
- `logs\lan-route-4320-dropmp4200-packetcapture-attempt2`
  - gameplay packet payload はほぼ zero で、主に tick/key/action を運んでいる。
  - 座標やスター取得状態は packet そのものには入っていない。
- `logs\lan-route-5520-nodrop-packetcapture-movement-attempt1`
  - 通常LocalMPの移動中 packet capture。
  - `scripts/analyze-nsmb-packet-capture.ps1` で 5200-5520 frame を集計。
  - host は keys `0x0010`、client は keys `0x0020` が主な変化。
  - payload側の非zeroは offset 40 の固定値 `0x03` のみで、座標・速度・スター所有権らしい可変値は見えない。

## 次にやること

1. DropMP 後のズレを「packet input delay不足」と「ゲーム状態の権威不足」に分けて検証する。
2. `Net::getPacket*()` の replay だけでなく、スター取得・スコア・8コインアイテムなどのゲーム上重要な状態を host authoritative にする hook 候補を探す。
3. packet payload の 44byte 拡張領域がスター取得・8コイン時に非zeroになるかを追加で確認する。
4. 切断画面検出は既に smoke test に入っているため、検出漏れがないかスクリーンショット実例で閾値を調整する。
5. 使えると判断した hook は、melonDS hook のまま検証し、安定したものから ROM patch 化候補として整理する。

## 必要なもの

- 現在の検証は、ユーザー提供の `roms/nsmb.nds` を前提にしている。
- 最終的な ROM patch 検証では、同じ日本版 `A2DJ` ROM に対する patch 生成・適用手順を用意する。
