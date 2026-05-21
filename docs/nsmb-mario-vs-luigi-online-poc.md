# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` でWAN越しに遊べる形へ持っていく。

現在の主方針は、melonDSのLocalMPをWANへそのまま伸ばすことではなく、NSMB側のMvL接続・同期・試合中packet処理をできるだけ活かし、必要箇所だけをWAN adapter / PacketBridge / ROMまたはメモリpatchで差し替えること。

## 現在の方針

1. **下位MP API / packet境界のadapter化**
   - `Net::getConsoleKeys/getPacketByte/getPacketTick/getPacketAction` だけでなく、packet availability、session/peer状態、packet sequencer完了条件まで含めてNSMBが期待する状態を再現する。
   - 既知入口は `0204619C` / `0204622C` / `02046480` / `Net::Core::transferPacket`。ここで足りなければさらに下位の送受信/状態APIを追う。
2. **MvL開始状態を直接作る診断ルート**
   - UI操作、LocalMPロビー、CourseSelect、StageStartSMを外から完全再現するのは重いので、診断として `Game::loadLevel`、MvLファイルロード、scene factoryを呼び、試合中packet同期へ入れる最小状態を探す。
   - 最終実装では、この診断で分かった必要条件をROM patchまたはより狭いメモリpatchへ落とす。
3. **試合中packet同期の検証**
   - NSMB Centralの情報どおり試合中が入力packet中心なら、stage開始後のpacketだけをWAN adapterへ流す。
   - Big Star、8コインアイテム、ランダムステージなどの乱数要素は `Net::getRandom()` 系またはMvL開始時seedを同期する方向で固定する。

## 完了したこと

- 自動検証用の入力スクリプト、スクリーンショット、framebuffer/black screen検出、game-state trace、packet capture/replay/bridge traceを追加済み。
- `NoLanMP + PacketBridge from start` の検証ルートを作成済み。
- PacketBridgeのpre-game/StageStart packetを52 byte全体で流すよう修正済み。marker byte `0x29` 欠落は解消済み。
- `PacketBridgeStageStartReadyProbe` 診断により、host側は `VSConnect::startLoadLevel`、`Game::loadLevel`、stage scene、player actor、Big Star actor生成まで到達できることを確認済み。
- `SafeLoadLevelCall`、`SafeTryChangeSceneCall`、`SafeStageSceneFactoryCall` の診断フックを整理し、複数SafeCall併用時の優先順位バグを修正済み。
- `SafeLoadLevelCall + SafeStageSceneFactoryCall` により、hostは直接stage sceneへ入り、player actorとBig Star actor生成まで到達できることを確認済み。
- host成功状態とclient停止状態のMainRAM dump / object dumpを取得済み。hostは stage scene object `0x0003`、player actor `0x0015`、Big Star actor `0x0022` を持つ一方、clientは `VSConnect 0x0006` と load scene `0x000F` に残る。
- 古い検証ログを削除し、直近の判断に必要なログだけ残した。
- `PacketBridgeForceMvlLoadThread` / `PacketBridgeForceMvlFileCache` が `ForceLoadGameSM` なしでも効くよう、検証スクリプトのenv設定を修正した。
- `SafeMvlLoadThreadCall` を追加し、client側でも `loadMvsLFilesThread` 入口 `02152E04` から完了地点 `02152E1C` まで通せることを確認した。
- `SafeStageSceneFactoryCreateObject` / `SafeStageSceneFactorySceneSwitch` 診断フックを追加し、client側でstage scene生成を直接押した場合の失敗条件を切り分けた。
- `PacketBridgeForceStagePacketWords` をrole限定で試せるようにし、clientだけstage packet wordを `action=0x03` に固定できることを確認した。

## 現在のブロッカー

- clientは `loadMvsLFilesThread` 完了後も、`sceneCurrent=0x0F`、`sceneNext=0x03` のままstage sceneへ進まない。
- hostはstage request後に自然に `02013588 -> 020131A0/020131A4 -> 02013218/0201321C` へ進み、stage scene objectとplayer/star actorを生成する。
- clientは同じstage requestを投げても、stage factory後にこの自然なscene遷移へ進まない。
- clientで `0204BF8C(objectID=0x0003)` を直接呼んでも、stage scene objectはMainRAM上のobject listに残らない。単純なobject spawnではなく、load sceneのdestroy完了、scene manager内部状態、またはStageStart/LoadGameSM由来の追加状態が必要。
- client local packetを `action=0x03` にしても、hostから見るclient packet tickが遅れているとremote packetとして使われない。packet actionだけでなくtick/進行同期も必要。
- 単純な `sceneActive=0` 強制は危険。全体に適用するとhost側でdata abortした。role限定・タイミング限定でないと使えない。

## 直近の検証結果

- `logs/nsmvl-direct-loadlevel-safe-client-loadthread-mainsp-20260521`
  - client-only `SafeMvlLoadThreadCall`、`minSP=0x027E3000`、`mode=0x1F` で `02152E04 -> 02152E1C` 到達。
  - clientも `SafeLoadLevelCall` と `SafeStageSceneFactoryCall` は発火。
  - ただし clientは `playerActor0Found=0`、`vsStarActorFound=0` のまま。
- `logs/nsmvl-direct-loadlevel-safe-client-loadthread-autoclear-20260521`
  - `SceneAutoActiveClear` を全体適用するとhostでdata abort。単純なscene active強制は採用しない。
- `logs/nsmvl-direct-loadlevel-safe-client-loadthread-dumps-20260521`
  - 1938/1980/1997/2000/2020fのRAM dumpを取得。1980fのload scene object自体はhost/clientでほぼ一致し、差分は主に `0208B040` と小さなtimer差分。
- `logs/nsmvl-direct-loadlevel-safe-client-loadthread-patchb040-20260521`
  - clientにhost 1980f由来の `0208B040` をpatchしてもstage scene生成には進まない。
- `logs/nsmvl-direct-loadlevel-safe-client-loadthread-client-active0-20260521`
  - clientだけ `sceneActive=0` にしてもstage scene生成には進まない。
- `logs/nsmvl-direct-loadlevel-safe-client-loadthread-client-sceneheader-20260521`
  - clientだけsceneヘッダをhost 2000f相当にしてもstage scene/player/star actorは生成されない。
- `logs/nsmvl-direct-loadlevel-client-create-stageobject-20260521`
  - clientだけ `0204BF8C(objectID=0x0003, settings=0x00B5FF00)` を直接呼ぶ診断を追加。call trace上は呼べるが、player/star actorは生成されない。
- `logs/nsmvl-direct-loadlevel-client-create-stageobject-dumps-20260521`
  - direct create後のRAM dumpを確認。host 2020fにはstage scene object `0x0003` が残るが、client側には同じpatternのobjectが残らない。
- `logs/nsmvl-direct-loadlevel-client-sceneswitch-20260521`
  - `020131A0` を通常関数のように直接呼ぶとclientの `sceneCurrent` が `0x0208` へ壊れる。ここは関数入口ではなく、scene factory内部の分岐先に近いため直接呼び出し対象から外す。
- `logs/nsmvl-direct-loadlevel-client-stagefactory-late-20260521`
  - client-onlyでstage factory/direct createを2020fへ遅らせてもstage scene objectは残らない。
- `logs/nsmvl-direct-loadlevel-client-stagefactory-late-patchedheader-20260521`
  - stage factory直前にclientへhost 2000f相当のscene headerをpatchしてもstage scene objectは残らない。
- `logs/nsmvl-direct-loadlevel-client-player0-20260521`
  - clientの `localPlayerID` を0に寄せてもstage scene生成には進まない。player ID差分だけが原因ではない。
- `logs/nsmvl-direct-loadscene-state-timer-patch-20260521`
  - load scene objectの `state word(+0x0C)` と `timer/step(+0x64)` をhost 1997f相当にpatchしてもdestroyは発火しない。
- `logs/nsmvl-force-stage-packet-words-clientonly-20260521`
  - client local packetの `action=0x03` 固定には成功。ただしhost側はclient packetをtick不一致で拾えず、clientもstage sceneへ進まない。
- `logs/nsmvl-force-stage-packet-words-clientonly-forcedtick-20260521`
  - tick基準を固定しても、host/clientの進行速度差でremote packetがlookup tickに届かず、host側は `player=1` を `action=0xFF` として扱う場面が残る。
- `logs/nsmvl-force-stage-packet-words-clientonly-throttle-20260521`
  - 既存tick throttleはpre-game tick差にも反応し、stage同期前の1290f付近で長時間待機する。throttle開始フレームをstage同期区間に限定する必要がある。

## 次にやること

1. PacketBridgeのtick/進行同期をstage同期区間だけで制御できるようにする。既存throttleはpre-gameで効きすぎるため、開始フレームまたはcontext条件を追加する。
2. hostがclient packetをremote packetとして拾える状態を作り、client load scene destroyが進むか確認する。
3. それでも進まない場合、hostで `02013588` がload scene object `0x021BEAB0` に対して呼ばれる直前条件を、object state、destroy flag、pending flags、scene graph link、LoadGameSM/StageStartSM状態まで広げて比較する。
4. 直接stage開始診断ルートのpatch範囲が大きくなりすぎる場合は、下位MP API adapter化またはROM patch入口作成へ戻す。
5. 安定した単位でコミットする。

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

## 参考

- NSMB Central: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi
- `external/NSMB-Code-Reference`
