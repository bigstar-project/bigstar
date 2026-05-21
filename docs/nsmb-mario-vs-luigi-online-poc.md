# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` でWAN越しに遊べる形へ持っていく。

現時点の中心方針は、melonDSのLocalMPをWANへそのまま伸ばすことではない。NSMB側が持っているMvLの接続・同期・試合中packet処理をできるだけ活かし、ローカル無線境界をWAN adapter / PacketBridgeへ差し替える。必要に応じて、ロビーや開始処理はROM/メモリpatchで短絡し、試合中の入力packet同期へ早く到達する。

## 現在の方針

1. **下位MP API / packet境界のadapter化**
   - `Net::getConsoleKeys/getPacketByte/getPacketTick/getPacketAction` だけでなく、packet availability、session/peer状態、packet sequencer完了条件まで含めて、NSMBが期待する下位通信状態を再現する。
   - 既知の入口は `0204619C` / `0204622C` / `02046480` / `Net::Core::transferPacket`。ここで足りなければさらに下の送受信/状態APIを特定する。

2. **MvL開始状態を直接作る診断ルート**
   - UI操作、LocalMPロビー、CourseSelect、StageStartSMを外から完全再現するのは重いので、診断として `Game::loadLevel` や scene factory を呼び、試合中packet同期へ入れる最小状態を探す。
   - 最終実装は、この診断で分かった必要条件をROM patchまたはより狭いメモリpatchに落とす。

3. **試合中packet同期の検証**
   - NSMB Centralの情報どおり、試合中が入力packet中心なら、stage開始後のpacketだけをWAN adapterに流す。
   - Big Star、8コインアイテム、ランダムステージなどの乱数要素は、`Net::getRandom()` 系またはMvL開始時seedを同期させる方向で固定する。

## 完了したこと

- 自動検証用の入力スクリプト、スクリーンショット、framebuffer/black screen検出、game-state trace、packet capture/replay/bridge traceを追加済み。
- `NoLanMP + PacketBridge from start` の検証ルートを作成済み。
- PacketBridgeのpre-game/StageStart packetを52 byte全体で流すよう修正済み。marker byte `0x29` 欠落は解消済み。
- `PacketBridgeStageStartReadyProbe` 診断により、host側は `VSConnect::startLoadLevel`、`Game::loadLevel`、stage scene、player actor、Big Star actor生成まで到達できることを確認済み。
- PacketBridgeのremote waitを追加したが、広い20ms waitは重すぎてpost-load付近で止まるため、phase/packet種別を絞る必要があると判定済み。
- `SafeLoadLevelCall`、`SafeTryChangeSceneCall`、`SafeStageSceneFactoryCall` の診断フックを整理し、複数SafeCall併用時の優先順位バグを修正済み。
- `SafeLoadLevelCall + SafeStageSceneFactoryCall` により、hostは直接stage sceneへ入り、player actorとBig Star actor生成まで到達できることを再確認済み。

## 現在のブロッカー

- client側は `SafeLoadLevelCall + SafeStageSceneFactoryCall` でも `sceneCurrent=0x0F`、`sceneNext=0x03`、`VSConnect` 残存のまま止まり、player actor / Big Star actorが生成されない。
- `SafeStageSceneFactoryInactive` で `sceneActive=0` を強制するとhostも壊れるため、単純なactive clearでは足りない。
- `SafeLoadLevelSessionReady` でhost型のNet状態をloadLevel前後に書くと、clientは逆にVSConnect sceneから進まなくなる。常時/広域のstate維持ではなく、より正確な開始条件の特定が必要。

## 次にやること

1. hostがactor生成に成功した `SafeLoadLevelCall + SafeStageSceneFactoryCall` の成功経路と、clientが止まる経路をMainRAM dumpで比較する。
2. 特に `VSConnect` オブジェクト、scene transition関連、object manager / scene factory引数、packet sequencer stateを比較し、clientが `scene 0x0F -> 0x03` を完了できない最小差分を特定する。
3. 差分が小さければ、診断フックを「状態丸ごと同期」ではなく「開始時に必要な少数の条件補正」に絞る。
4. 差分が大きければ、直接stage開始ルートは診断専用に戻し、下位MP API adapter化の解析へ戻る。
5. 実装がまとまった単位でコミットする。

## 重要アドレス

- `Net::getConsoleKeys(u16)`: `0x0200E700`
- `Net::getPacketByte(u16,u32)`: `0x0200E978`
- `Net::getPacketTick(u16)`: `0x0200E9BC`
- `Net::getPacketAction(u16)`: `0x0200E9DC`
- `Net::Core::transferPacket`: `0x0200F98C`
- packet tick/key/action buffer: `0x02087F00`
- net state base: `0x02087E00`
- `Game::stageGroup`: `0x02085058`
- `Game::localPlayerID`: `0x020850BC`
- `Game::vsMode`: `0x020850C4`
- `VSConnect::scheduleSubMenuChange`: `0x021528A0`
- `VSConnect::startLoadLevel`: `0x0214E0C0`
- `Game::loadLevel`: `0x020068A8`
- `createStageStartSM`: `0x021515B4`
- `updateStageStartSM`: `0x021512B8`
- `loadMvsLFilesThread`: `0x02152E04`
- `Scene::tryChangeScene`: `0x0201314C`

## 検証に必要なもの

- ユーザー提供の `roms/nsmb.nds` を使用する。
- ROM本体や商用素材はリポジトリに含めない。

## 参照

- NSMB Central: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi
- `external/NSMB-Code-Reference`
