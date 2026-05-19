# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード `Mario vs Luigi` を、最終的に WAN 越しの `1 melonDS instance * 2PC` で遊べる形にする。

現在の主方針は、`1 melonDS instance * 2PC` を維持しつつ、NSMB がローカル通信時に扱う MvL packet / 接続 state-machine を解析し、WAN 越しでも同じ入力通信へ到達できる adapter または ROM 側パッチを作ること。


## 採用しない方針

- savestate 同期方式: 表示座標だけでは stage/object/通信状態が一致せず、対戦基盤として弱い。
- DropMP / 途中WAN切り替え: 切り替え時に NSMB 側の接続状態が壊れやすい。
- `2インスタンス * 2PC` の状態同期: 重く、最終形から遠い。検証用としてのみ使う。
- スクリーンショットだけの成功判定: 死亡演出や黒画面を誤判定しやすいため、game-state trace / actor presence / packet trace を優先する。


## 実装済みの検証基盤

- 入力スクリプト再生
- screenshot / framebuffer dump
- MainRAM dump
- game-state trace
- call/write/random trace
- packet capture / packet bridge trace
- Big Star actor ID `0x00D2` 周辺 probe
- smoke test の厳格判定: `playerActor0Found`, `playerActor1Found`, `vsStarActorFound`
- `run-nsmb-mvl-lan-route-smoke.ps1` の LAN / NoLanMP / PacketBridge 検証オプション
- WAN packet adapter の下層MPフック実験
- `ForceNetReady` / `ForceLoadGameSM` 補助フック
- LAN MP control trace / reply timestamp slack / old regular drop 検証
- game-state trace の開始/終了フレーム指定


## 重要アドレス

- `Net::getConsoleKeys(u16)`: `0x0200E700`
- `Net::getPacketByte(u16,u32)`: `0x0200E978`
- `Net::getPacketTick(u16)`: `0x0200E9BC`
- `Net::getPacketAction(u16)`: `0x0200E9DC`
- `Net::Core::transferPacket`: `0x0200F98C`
- lower MP status probe: `0x0204619C`
- lower MP hasPacket: `0x0204622C`
- lower MP getPacket: `0x02046480`
- packet tick/key/action buffer: `0x02087F00`
- net state base: `0x02087E00`
- LocalMP packet slot status: `0x0208AE50`
- LocalMP packet buffer: `0x0208B040 + player * 0x3E`
- VSConnect load-game:
  - create: `0x021515B4`
  - update: `0x021512B8`
  - render: `0x0215125C`
- CourseSelect 関連:
  - create: `0x0214F830`
  - factory/request: `0x020130A8`
  - scene request apply: `0x02007ACC`
  - scene transition: `0x02011CE8`


## 現在の到達点

最終形は `1 melonDS instance * 2PC`。直近では、NSMBの接続処理を途中から奪うのではなく、melonDS既存の `MPInterface_LAN` を「最初からWAN transport」として使う方向を検証した。

`MPInterface_LAN` は LocalMP と同じ `SendCmd` / `SendReply` / `RecvHostPacket` API を ENet で運ぶ実装なので、NSMB側のローカル通信state-machineを最も壊しにくい。自動テストでは、遅延なしのLAN経路は `LanStartAttempts` 付きで `3600` frame まで成功し、`stageGroup=9`, `vsMode=1`, player actors, star actor を確認できる。

一方、WAN想定の単純な設定変更は失敗している。

- `MELONDS_NSML_LAN_WAN_MODE` / reliable / 長いtimeout: 遅延なしでも接続探索段階で失敗する。
- `MELONDS_NSML_LAN_MP_STALE_MS=1000`: hostが「ルイージをさがしています」で止まる。
- `MELONDS_NSML_LAN_MP_SEND_DELAY_MS=5`: hostが探索中、clientが「melonDSマリオがあらわれました / たいせんしますか？」で止まる。
- clientのA入力を長時間パルスするWAN待ち入力スクリプトでも、5ms送信遅延ではclientが承諾後に「しばらくおまちください」へ進み、その後「melonDSマリオがいなくなりました」で落ちる。
- `MELONDS_NSML_LAN_MP_REPLY_TIMESTAMP_SLACK_US=20000` を追加し、遅延時の `reply-skip` は減らせた。ただし 1ms client送信遅延でも `action 0x02` 止まり、`stageGroup=9` へは進まない。

このため、単にtimeoutやstale window、reply timestamp slackを伸ばすだけでは不十分。NSMBの探索/承諾UIとMP frameの鮮度管理が強く結びついているため、`MPInterface_LAN` をそのままWAN化するだけで安定対戦まで行く見込みは低くなった。

NoLanMP + PacketBridge / ForceLoadGameSM / SafeCall 系は、接続途中から状態を作る実験としては有用だったが、自然なCourseSelect生成や実ステージactor生成には届いていない。今後の本筋からは外し、必要な解析補助としてだけ使う。


## 新しい重要な知見

通常LANの pre-game packet capture では、接続段階の packet action は以下のように遷移する。

- host: `0x00 -> 0x01 -> 0x02 -> 0x03`
- client: `0x00 -> 0x01 -> 0x02 -> 0x03`

代表的な遷移:

- host action `0x01`: frame `1303`, tick `0x0001`
- host action `0x02`: frame `1803`, tick `0x01F0`
- host action `0x03`: frame `1812`, tick `0x01F5`
- client action `0x01`: frame `1542`, tick `0x00B3`
- client action `0x02`: frame `1761`, tick `0x0189`
- client action `0x03`: frame `1767`, tick `0x01F5`

つまり接続段階では host/client の packet tick は常に同一ではない。action `0x03` 付近で揃う。したがって、接続開始から強制的に共通 tick にする実験は NSMB の handshake 条件を壊す可能性がある。最終的には「接続段階はNSMBの自然なtick/action遷移を尊重し、gameplay開始後に入力同期用のtick管理へ寄せる」設計が必要そう。

通常LANの write trace では、CourseSelect生成前に以下の状態へ進むことを確認した。

- host: `VSConnect +0x144=7`, `+0x148=0x30`, `+0x154=0x00030000`
- client: `VSConnect +0x144=7`, `+0x148=0x27`, `+0x154=0x00030001`

このため `ForceLoadGameSM` の補助値を通常LAN相当に修正し、client 側では `localPlayerID=1` も補正するようにした。

追加で確認したこと:

- `SafeUpdateLoadGameCall` は、host/client別の安定PC (`0200E700` / `02010810`) で呼ぶとabortは避けられるが、NoLan状態ではCourseSelect生成へ進まない。
- 正常LAN packet captureをreplayしても、NoLan + ForceLoadGameSM ではLoadGameSMが自然遷移しない。接続初期化の副作用が足りない。
- `CourseSelectFactory` 直接呼び出しはhostでCourseSelect生成まで進む場合があるが、clientでは同じ引数でも生成されない。呼び出し文脈依存が強い。
- `Game::loadLevel()` をSafeCallで直接呼ぶと `stageGroup=9` と `READY!` 画面までは到達するが、player actors / star actor が生成されない。つまり「面IDだけを切り替える」だけでは試合開始状態として不十分。
- `run-nsmb-mvl-lan-route-smoke.ps1` に接続ダイアログ検出を追加した。「通信が切断されました」だけでなく、「相手がいなくなりました」「相手を探しています」「たいせんしますか？」で止まる画面も失敗扱いにする。
- WAN待ち用入力スクリプト `tests/nsmb_mario_vs_luigi_wan_wait.inputs` を追加した。
- `LAN.cpp` に control event trace と `MELONDS_NSML_LAN_MP_DROP_OLD_REGULAR` を追加した。古いregular frameを捨てても5ms遅延はまだ突破できない。
- 5ms遅延失敗時のtraceでは、正常時に出る `type=1` CMD / `type=65538` reply 段階へ入る前に止まる。clientはhost regular frameを受け、承諾後に少数のregular frameを送るが、host側は探索中のままになる。
- 片方向遅延の切り分けでは、host送信だけ遅延するとhostがclient regular frameを受け取れず、client送信だけ遅延するとCMD/reply段階へ入る場合がある。ただしNSMBのactionは `0x03` へ進まず、試合開始には届かない。
- host側 `RecvReplies` は返信timestampが現在timestampより32us以上古いと `reply-skip` する。遅延時は正しい返信でもここで落ちるため、`MELONDS_NSML_LAN_MP_REPLY_TIMESTAMP_SLACK_US` を追加した。しかしこれは必要条件であって十分条件ではなかった。
- 正常LANの高密度traceでは、hostはおおよそ frame `1840` で `vsMode=1` と LoadGameSM 関数ポインタへ切り替わり、`+0x144` が `3 -> 5 -> 6 -> 7`、`+0x154` が `0x10000 -> 0x30000`、frame `1960` で CourseSelect が生成される。
- clientはhostより少し早く同様の遷移を行い、正常時の最終値は `localPlayerID=1`, `VSConnect +0x148=0x27`, `+0x154=0x30001`。DirectBoot LoadGameSM側にもこのrole別補正を追加した。
- NoLanMP上でLoadGameSMへ直行すると、role別補正後もCourseSelectは生成されない。step7へ直行するだけでは足りず、step3/5/6の途中副作用が必要。
- ForceLoadGameSMをstep3から走らせる実験では、timerが増え続けてstep5付近で止まる。`ForceNetReady`の常時上書きは状態遷移を潰すため、開始/終了フレーム指定を追加した。
- `RunUpdateAll`でhost/client両方からLoadGameSM updateを強制呼び出しするとARM9 data abortが出るため、現在のtrampoline呼び出し方式はhost側では不安定。
- `VSConnectUpdateLoadGameSM` の逆アセンブルで、step5は `VSConnect +0x156` のbit0/bit1、つまり `+0x154` の `0x30000` 相当が揃うまで進まないことを確認した。
- ForceLoadGameSMにflags指定を追加し、`step=5`, `timer=35`, `flags=0x30000` で開始すると、hostはNoLanMPでも `CourseSelectFound=1` を経て `stageGroup=9` へ到達する。
- clientはrole環境変数を維持する修正により `localPlayerID=1`, `+0x154=0x30001`, step7 までは進む。ただしCourseSelectが生成されず、`stageGroup=9` へ未到達。client側には `CourseSelectFactory` 前後の追加グローバル条件が残っている。


## 現在のブロッカー

`MPInterface_LAN` を最初から使うルートは正常LANでは動くが、WAN想定の遅延・長い待ち・長いstale window・reply slackを入れると接続探索/承諾段階で止まる。

特に、NSMB側の接続UIは「ローカル無線の短い応答テンポ」を前提にしており、ENetでMPInterfaceを遠隔化するだけでは、発見、承諾、CMD/reply、load-game遷移が同じタイミングで成立しない。最終目標を考えると、次はNSMB側の接続段階を短絡または置換し、gameplay中の入力packet通信へ直接入る設計を優先する。


## 次にやること

1. client側で `CourseSelectFactory(0x020130A8)` が呼ばれない/生成されない条件を追う。正常LANのclientとNoLanMP強制clientで、`0200E658`, `0200E664`, `0200E5F4`, `0200EAD8`, `0201DAF4` 周辺の戻り値・グローバル差分を見る。
2. UI操作を経ずに host/client 両方で `CourseSelectFound=1`、その後 `stageGroup=9` へ入る ROM/メモリ側パッチ候補を作る。
3. gameplay到達後、NSMBの入力packetをWAN adapterで交換する。ここではNSMB既存の入力同期処理を使い、LocalMPの探索/承諾UIは通さない。
4. 乱数固定は `Net::getRandom()` の戻り列を同期する方針を維持する。Big Starだけでなく、8コインアイテムやランダムステージ選択も対象にする。
5. `MPInterface_LAN` の遅延耐性検証は補助に下げる。reply slackなどの知見は使うが、最終ルートの本筋にはしない。


## 必要なもの

- 検証にはユーザー提供の `roms/nsmb.nds` を使う。
- ROM patch化へ進む場合は、日本版 `A2DJ` 向けの patch 生成/適用手順を別途作る。
