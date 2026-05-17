# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード `Mario vs Luigi` を、最終的に WAN 越しの 2PC で遊べる形にする。

現在の本命方針は、melonDS の Local MP をそのまま WAN に流すことではない。NSMB が対戦中に扱うゲームレベルの packet / 接続状態 / 乱数 / 入力反映を特定し、ROM patch または melonDS 側 hook で置き換え、最終的には `1 EmuInstance * 2PC` に寄せる。

## 現在の結論

- Local MP を残したまま、ゲームが読む packet だけを strict bridge で置き換えると、movement script ありで 6600 frame まで host/client の player 座標と star 座標が一致する。
- Local MP を止めると、NSMB 側の接続状態と低レベル transfer 処理が失敗扱いになり、黒画面または「通信が切断されました」に落ちる。
- `Net::update()` の disconnect 分岐 `0x02010174` を skip すると黒画面は避けられるが、それだけでは `Net::getPacket*()` 呼び出しが 3840 frame 付近で止まる。
- `Net::Core::transferPacket(Net::PacketAction)` 候補 `0x0200F98C` を DropMP 後に成功値 `0x8` で返す hook を追加すると、DropMP 後も `Net::getPacket*()` 呼び出しが継続する。
- 上記の `disconnect skip + transferPacket result=8` ルートでは、黒画面・abort・明示的な切断表示なしで 4200/5520/6600 frame まで到達できる。
- `ForceTick` を RAM 書き込みだけでなく packet生成/replay 側の canonical tick として使う修正により、同一 frame で host/client が読む replay tick は一致するようになった。
- `-NetRandomValue 0x00000100 -NetRandomAuto` で両プロセスに同じ `Net::random.value` を入れられるようにした。固定RNG自体は star/RNG 一致に効く。
- `-PacketBridgeLookupTickDelay` を追加し、replay が現在tickではなく指定tick数前の packet を読む実験ができるようにした。
- ただし movement 入力を入れると host/client の player 座標や global hash はまだズレる。現時点の主 blocker は「黒画面」ではなく、DropMP 後に必要な remote packet が同じ simulation frame の replay までに届いている保証がないこと。

## 実装済み

- `scripts/run-nsmb-mvl-lan-route-smoke.ps1`
  - Mario vs Luigi 到達 smoke test。
  - screenshot / framebuffer dump。
  - game state trace。
  - packet bridge / replay。
  - Local MP drop: `-DropMPAfterFrame`。
  - disconnect 抑制・skip 実験: `-PacketBridgeSuppressDisconnect`, `-PacketBridgeBypassNetDisconnect`。
  - transfer 成功強制: `-PacketBridgeForceTransferResult`, `-PacketBridgeForceTransferStartFrame`, `-PacketBridgeForceTransferResultValue`。
  - 進行差制限: `-PacketBridgeMaxFrameLead`, `-PacketBridgeThrottleTimeoutMs`。
  - RNG固定: `-NetRandomValue`, `-NetRandomFrame`, `-NetRandomAuto`。
  - replay tick delay: `-PacketBridgeLookupTickDelay`。
- melonDS hook
  - `Net::getConsoleKeys()` / `Net::getPacketByte()` / `Net::getPacketTick()` / `Net::getPacketAction()` の packet replay。
  - `Net::update()` disconnect branch skip。
  - `Net::Core::transferPacket()` 候補の成功値強制。
  - packet bridge canonical tick。`MELONDS_NSML_PACKET_BRIDGE_FORCE_TICK_BASE` がある場合、RAM上の一時的な tick ではなく `base + frame - startFrame` を packet生成/replay に使う。
  - A2DJ Mario vs Luigi 判定、固定RTC、JIT無効化、状態trace。

## 重要アドレス

- `Net::getPacketTick(u16)`: `0x0200E9BC`
- `Net::getPacket(u16)`: `0x0200E9FC`
- `Net::Core::transferPacket(Net::PacketAction)` 候補: `0x0200F98C`
- `Net::update()` disconnect branch call: `0x02010174`
- LocalMP packet slot status: `0x0208AE50`
- LocalMP packet buffer: `0x0208B040 + player * 0x3E`
- NSMB disconnect flags 付近: `0x02087E5C`
- NSMB network state 付近: `0x02087E1C`

## 検証結果

### 成功寄り

- `logs\lan-route-6600-nodrop-strictbridge-movement5200-attempt2`
  - Local MP を残した strict packet bridge。
  - 6600 frame 到達。
  - host/client の `playerActor0X=0x128fff`, `playerActor1X=0xfffd8000`, `vsStarActorX=0x1a0000` が一致。
- `logs\lan-route-4200-dropmp3600-force-transfer8-trace-attempt1`
  - DropMP 3600 後に `disconnect skip + transferPacket result=8`。
  - 4200 frame 到達。
  - 黒画面・abort なし。
  - `Net::getPacket*()` 呼び出しが 4199 frame まで継続。
- `logs\lan-route-5520-dropmp3600-force-transfer8-movement-framelead60-attempt1`
  - movement 入力あり。
  - 5520 frame 到達、peer disconnect なし。
  - ただし player 座標は一致しない。
- `logs\lan-route-5520-dropmp3600-force-transfer8-canonicaltick-movement-framelead60-trace-attempt1`
  - canonical tick 導入後の movement trace。
  - 5520 frame 到達。
  - 5519 frame で host/client とも replay tick `0x1029`、keys は `p0=0x0010`, `p1=0x0020` まで一致。
  - それでも player 座標は一致しない。tick/keys だけでなく、packet到着タイミングまたは local/remote 更新順序のbarrierが必要。
- `logs\lan-route-4200-dropmp3600-force-transfer8-canonicaltick-fixedrng100-no-framelead-attempt1`
  - `-NetRandomValue 0x00000100 -NetRandomAuto` あり。
  - 4200 frame 到達、host/client の star と RNG は一致。
  - no framelead では host 側が client packet を replay 時点で受け取れず、host の player1 hit が 0 になる frame がある。
- `logs\lan-route-4200-dropmp3600-force-transfer8-canonicaltick-fixedrng100-lookupdelay240-attempt1`
  - lookup delay 240 tick あり。
  - 4200 frame 到達。
  - no framelead では host が client より大きく先行するため、delay しても host 側の player1 packet miss は解消しない。
- `logs\lan-route-6600-dropmp3600-force-transfer8-movement-framelead60-attempt1`
  - movement 入力あり。
  - 6600 frame 到達、黒画面・abort・peer disconnect なし。
  - star 座標は一致したが、player 座標と hash は一致しない。

### 失敗・棄却

- DropMP 後の `-PacketBridgeSuppressDisconnect` だけでは、黒画面検出で失敗する。
- `Net::update()` disconnect branch skip だけでは、packet 処理が 3840 frame 付近で実質止まる。
- `force-active` mode で `0x02087E20=0x0004` を戻すと、一部 packet 呼び出しは戻るが黒画面に落ちる。
- 単純な frame lead throttle は速度差を抑えられるが、同一 frame での状態一致までは保証しない。
- `fixedrng100 + framelead60` は client が 3578 frame 付近で止まり、host が frame throttle timeout を繰り返した。固定RNG自体ではなく、待つ位置と進行制御の設計が悪い可能性が高い。
- `fixedrng100 + framelead60/300` は trace有無に関係なく timeout しやすい。client が 3580 frame 前後で進まず、host は remoteFrame が更新されないまま throttle timeout を繰り返す。現行の framelead throttle は DropMP 後の同期制御として不適切。

## 次にやること

1. `WaitForNSMLPacketBridgeRemote()` / framelead throttle の待機位置を見直す。現在の before-frame wait は、同じ tick の packet を互いに待つ形にすると deadlock しやすい。
2. replay が参照する tick に対して、remote packet が届いていない場合の扱いを設計する。候補は「固定入力遅延 tick 参照」または「after-frame capture 後の次frame replayへずらす」。
3. `fixedrng100 + canonical tick + packet到着barrier` で 4200/5520 frame の player座標一致を確認する。
4. 成功した hook を ROM patch へ移す場合の候補として整理する。
5. LAN start retry の flake 原因も別途調べ、検証ループの安定性を上げる。

## 必要なもの

- 現在の検証はユーザー提供の `roms/nsmb.nds` を前提にしている。
- 最終的な ROM patch 検証では、同じ日本版 `A2DJ` ROM に対する patch 生成・適用手順が必要。
