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
- `PacketBridgeWaitStartFrame` / `PacketBridgeThrottleStartFrame` を追加し、pre-game区間でwait/throttleが誤発火して検証を止める問題を避けられるようにした。
- smoke scriptに `HostFrames` / `ClientFrames` を追加し、片方だけwaitで遅れる検証でも相手プロセスを長く走らせられるようにした。
- A2DJの下位Net packet関数をCallTraceのデフォルト対象へ追加した。`processRecvPacket/processSendPacket/advancePacketSequencer/checkAllPacketBits` などを明示的なaddr指定なしで追える。
- clientがstage遷移前に `Net::Core::transferPacket` 内部の完了待ちで詰まり、`Net::updatePacket/processRecvPacket/processSendPacket` を呼ばなくなることを確認した。
- client限定 `PacketBridgeForceTransferResult` により、clientもstage sceneへ入り、player actorとBig Star actor生成まで到達することを確認した。
- `Net::random.value=0x00000100` のstage開始時固定を組み合わせると、host/clientのBig Star初期位置が一致することを確認した。
- stage後入力検証用の `tests/nsmb_mario_vs_luigi_stage_move.inputs` を追加した。

## 現在のブロッカー

- 直接stage開始診断ルートでは、client限定 `transferPacket` 結果強制でstage sceneへ入れるようになった。ただしこれはまだ診断フックで、最終的には下位MP adapterまたはROM patchへ落とす必要がある。
- host/clientともstage scene、player actor、Big Star actorまでは生成できるが、試合中の入力packet同期はまだ本実装ではない。
- stage後にhost側 `RIGHT` 入力を入れると、client側ではplayer0が動く兆候がある一方、host側のplayer0は同じように動かず、clientは後半でdata abortした。remote packet注入だけでは、まだ双方の試合状態は同期していない。
- `transferPacket` 結果強制はclient限定ならstage到達に効くが、hostにも適用するとactor生成自体が壊れる。host側は自然なtransfer経路を残す必要がある。
- `Net::random.value` 固定でBig Star初期位置は一致したが、以後のBig Star再生成、8コインアイテム、ランダムステージ選択では、`Net::randomCallCount` と呼び出し順が一致し続けることを別途確認する必要がある。
- `PacketBridgeWait` / throttleで2プロセスの実時間進行差を吸収しようとすると、host/clientの片側だけが極端に遅くなり、検証時間が破綻する。WAN本番の快適性にも直結しないため、これは補助診断に留める。
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
- `logs/nsmvl-force-stage-packet-words-clientonly-livefallback-20260521`
  - 近傍tick fallbackでhostが古い/未来のclient packetを拾えるか試したが、host側がstage開始途中で壊れ、clientはload sceneに残った。tick不一致を雑に許容するだけでは安定しない。
- `logs/nsmvl-force-stage-packet-words-clientonly-waitstart-20260521`
  - 1850f以降だけexact waitを有効化。pre-game待ちは避けられ、hostはstage scene/Big Starまで進むが、clientは `sceneCurrent=0x0F` のまま。hostはclient packetが届かないtickでwait timeoutを連発する。
- `logs/nsmvl-force-stage-packet-words-clientonly-waitstart-clientlong-20260521`
  - clientだけ長く走らせても、実時間進行差が大きく、hostがstage前のtick待ちでほぼ停止する。waitで外側から足並みを揃える方式はフィードバックループが遅すぎる。
- `logs/nsmvl-lower-net-default-calltrace-20260521`
  - clientは1980fで `Net::Core::transferPacket` に入った後、`Net::updatePacket/processRecvPacket/processSendPacket` が止まり、`onPacketPollingDefault` だけを呼び続ける。clientがstage sceneへ進まない主因は、transfer完了待ちで止まることに寄っている可能性が高い。
- `logs/nsmvl-client-transfer-bypass-20260521`
  - client限定 `PacketBridgeForceTransferResult` でclientもstage sceneへ進む。2120f以降、client側にもplayer actorとBig Star actorが出る。ただしRNG未固定ではBig Star座標がhost/clientで異なる。
- `logs/nsmvl-client-transfer-bypass-netrandom-20260521`
  - `Net::random.value=0x00000100` をstage開始時に両側へpatchすると、2140f以降のBig Star座標がhost/clientで `x=0x370000, y=0xFFEF0000, z=0x180000` に一致する。
- `logs/nsmvl-stage-move-host-right-20260521`
  - stage後にhost inputで `RIGHT` を入れる検証。client側player0は2420f以降にX座標が動くが、host側player0は動かず、clientは2600f付近でdata abort。入力packetは一部伝わっている可能性があるが、local/remote双方のpacket消費とgameplay状態がまだ揃っていない。
- `logs/nsmvl-stage-move-host-right-packettrace-20260521`
  - client側では `player=0 keys=0x0010` がhitし、hostのRIGHT入力packetを読めている。host側は `netPacketKeys=0x10` を持つが、packet replay hook上はhitせず、player0 actorも動かない。hostのlocal入力消費経路とstage playable状態の追加確認が必要。
- `logs/nsmvl-stage-move-host-right-transfer-both-20260521`
  - `transferPacket` 結果強制をhost/client両方に適用すると、player/star actorが検出されずstage生成が壊れる。client限定bypassを維持する。

## 次にやること

1. client限定 `transferPacket` 結果強制を、現在の診断フックから「WAN adapterが返すべき完了条件」として整理する。戻り値 `8` が正しい完了値なのか、他の戻り値やflagsが必要かを確認する。
2. stage開始後の入力packet同期を修正する。`RIGHT` 入力検証でclient側だけplayer0が動くため、host側がなぜ自分の入力をgameplayへ反映しないか、client側がなぜ後半でdata abortするかを `getConsoleKeys/getPacket*` traceで切り分ける。
3. RNGについて、Big Star初期位置以外に、Big Star再生成、8コインアイテム、ランダムステージ選択で `Net::random` 呼び出し順が一致するかをtraceする。
4. 直接stage開始診断ルートの依存フックを縮小し、最終的にROM patch入口または下位MP adapterへ置き換える。
5. 安定した単位でコミットする。

## 重要アドレス

- `Net::getConsoleKeys(u16)`: `0x0200E700`
- `Net::getPacketByte(u16,u32)`: `0x0200E978`
- `Net::getPacketTick(u16)`: `0x0200E9BC`
- `Net::getPacketAction(u16)`: `0x0200E9DC`
- `Net::Core::transferPacket`: `0x0200F98C`
- `Net::Core::processRecvPacket`: `0x02011360`
- `Net::Core::processSendPacket`: `0x02011428`
- `Net::Core::checkAllPacketBits`: `0x020110E4`
- `Net::Core::advancePacketSequencer`: `0x0201122C`
- packet tick/key/action buffer: `0x02087F00`
- `Net::packetFreeBytesRecvBitmap`: `0x020880A4`
- `Net::packetFreeBytes`: `0x020880B4`
- `Net::packetSequenceBuilder`: `0x020880D4`
- `Net::packetSequencers`: `0x020880FC`
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
