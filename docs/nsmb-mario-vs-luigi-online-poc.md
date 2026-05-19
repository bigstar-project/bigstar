# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦モード `Mario vs Luigi` を、最終的に WAN 越しの `1 melonDS instance * 2PC` で遊べる形にする。

現在の主方針は、melonDS 側で DS LocalMP 全体を WAN へ伸ばすのではなく、NSMB が MvL 中に扱う入力/同期 packet と接続 state-machine を解析し、必要なら ROM/メモリ側パッチまたは melonDS のWAN adapterで置き換えること。

## 採用しない方針

- `2 instances * 2PC` の決定論同期: 重く、LocalMP間でもズレが残るため本筋から外す。
- savestate同期: 表示座標だけ合わせても通信/actor/object状態が一致せず、対戦基盤として弱い。
- DropMP / 試合途中WAN切替: 切替瞬間の停止でNSMB側状態が壊れやすい。
- `MPInterface_LAN` をそのままWAN化: 通常LANなら成功するが、1ms程度の遅延や長い stale window でも接続/承認段階で崩れる。

## 現在わかっていること

- 通常LAN経路は、複数attemptで `stageGroup=9`, `vsMode=1`, player actors, star actor まで到達できる。
- WAN想定遅延を `MPInterface_LAN` に入れると、接続検索/CMD/reply/LoadGameSM遷移のどこかで止まる。
- NoLanMP + `ForceLoadGameSM` は host 側で CourseSelect要求と `stageGroup=9` までは作れるが、黒画面で player actors も出ない。成功扱いにしない。
- client側でも `CourseSelectFactory(0x020130A8)` は呼ばれ、`0x02084FB4=0x42` などの要求は書かれる。ただし要求が消費されず、CourseSelect/ステージ遷移に進まない。
- `ForceLoadGameSMRunUpdate` を step7 到達後も呼び続けると `ARM9 data abort (023C0008)` を起こすため、step7未満の補助に限定した。
- `0x02087E10=1` をhost/client両方へ常時書くとhostのCourseSelect生成が壊れる。必要な場合だけ、client限定で試せるフックにした。
- スクリーンショット判定は、通信切断風画面と黒画面を分けた。`-SkipDisconnectScreenshotCheck` を使っても黒画面は検出する。
- NSMB Centralの解析情報どおり、MvL gameplay中の通常packetは入力中心に見える。通常LAN packet captureでは `byte0-1=tick`, `byte2-3=keys`, `byte4=action`, `byte5=0`, `byte6-7=0xFFFF` が安定しており、RIGHT/LEFT/B入力は `keys` にそのまま出る。
- 通常LANのlevel loadは `StartLoadLevel(0x0214E0C0)` -> `Game::loadLevel(0x020068A8)` で、主要引数は `r0=0x0F`, `r1=1`, `r2=9`, `r3=0`。stack引数も既存の直接呼び出しと概ね一致している。黒画面原因はloadLevel引数そのものより、直前のscene/connect状態不足が濃い。

## 実装済みの検証フック

- 入力スクリプト再生
- screenshot / framebuffer dump
- game-state trace
- call trace / write trace
- MainRAM dump
- packet capture / packet bridge trace
- Big Star actor ID `0x00D2` 周辺probe
- `ForceNetReady`, `ForceLoadGameSM`, `ForceTransferResult`
- `ForceLoadGameSMRunUpdate` のstep7停止
- `ForceNetReadyState10` と client-only 指定
- `ForceTransferResult` の client-only 指定
- 黒画面検出を通信切断検出から分離
- gameplay入力付きpacket capture用 `tests/nsmb_mario_vs_luigi_gameplay_inputs.inputs`

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
- LocalMP packet slot status: `0x0208AE50`
- LocalMP packet buffer: `0x0208B040 + player * 0x3E`
- `VSConnect::updateLoadGameSM`: `0x021512B8`
- `CourseSelectFactory`: `0x020130A8`
- scene request/current object globals: `0x02084FB4`, `0x0203B480`, `0x02088554`, `0x02088558`

## 参考情報

- NSMB Central: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi

## 次にやること

1. NoLanMP強制起動ルートは補助解析に下げる。黒画面になるため、これを最終経路にしない。
2. 通常LAN成功ログを基準に、接続開始から gameplay 開始までの packet/action/tick/scene request の差分をさらに狭める。
3. NSMBのMvL packetが入力同期中心である前提に立ち、接続UIを無理に再現するより、gameplay中の packet 送受信をWAN adapterへ差し替える最小条件を探す。
4. ROM/メモリパッチ候補として、接続完了済み状態、player ID、RNG seed、stage選択、packet tick の初期値を固定する箇所を整理する。
5. 8コインアイテムなどBig Star以外のランダム要素も、最終的には `Net::getRandom()` 系の戻り値/seed同期で扱う。

## 必要なもの

- 検証にはユーザー提供の `roms/nsmb.nds` を使う。
- ROMパッチ生成へ進む場合は、日本版 `A2DJ` 専用のパッチ適用/復元手順を別ファイルに分ける。
