# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` でWAN越しに遊べる形へ持っていく。

現在はmelonDS LocalMPをWANへ伸ばす方針ではなく、NSMBが使うMvL通信境界を特定し、必要な部分だけをWAN adapter / PacketBridge / ROMまたはメモリpatchで差し替える方針。

## 現在の方針

1. **下位MP API / packet境界をadapter化する**
   - `Net::getConsoleKeys/getPacketByte/getPacketTick/getPacketAction` だけでなく、packet availability、session/peer状態、packet sequencer完了条件まで含めてNSMBが期待する状態を再現する。
   - 既知入口は `Net::Core::transferPacket`、`Net::updatePacket`、`Net::Core::processRecvPacket/processSendPacket`、`Net::Core::checkAllPacketBits/advancePacketSequencer`。

2. **MvL開始状態を直接作る診断ルートを使う**
   - UI操作、LocalMPロビー、CourseSelect、StageStartSMの自然再現は重い。
   - 診断では `loadMvsLFilesThread`、`Game::loadLevel`、stage scene factoryを安全呼び出しし、試合中packet同期へ入る最小条件を探す。
   - 最終実装では、ここで見つけた条件をROM patchまたはより狭いメモリpatchへ落とす。

3. **試合中packet同期に寄せる**
   - NSMB Centralの解析どおり、試合中は入力packet中心の同期で進める想定。
   - 乱数は個別actor座標を無理に合わせるのではなく、`Net::random.value` / `Net::getRandom()` 系の共有シードで合わせる。

## 実装済み

- 自動検証用の入力スクリプト、スクリーンショット、framebuffer/hash、RAM dump、game-state CSV、packet capture/replay/bridge traceを追加。
- `NoLanMP + PacketBridge from start` の検証ルートを作成。
- A2DJ向けに主要Net関数・グローバルを移植。
- `Net::random.value` のmatch seed配布と自動注入で、初期Big Star位置がhost/clientで一致することを確認。
- `PacketBridgeForceTransferResult` client限定で、client側もstage scene、player actor、Big Star actor生成まで到達可能にした。
- game-state CSVに以下を追加済み。
  - player actor base/state/flags/prev/vel
  - stage scene base/state/flags/word154/word160
  - `Input::consoleKeys` / `Input::playerKeysHeld` / `Input::playerKeysPressed` 候補値
  - `Stage::actorFreezeFlag` A2DJ候補値
- 診断用env / script optionを追加済み。
  - `ForcePlayerCount`
  - `ForceStageSceneRuntimeWords`
  - `ForceStageActorFreezeFlag`
  - `PacketBridgeLiveFallbackLatestBefore` / `PacketBridgeLiveFallbackStartFrame`
  - `SafeMvlLoadThreadCall`
  - `SafeLoadLevelCall`
  - `SafeStageSceneFactoryCall`
- `scripts/run-nsmb-mvl-lan-route-smoke.ps1` は、デフォルトでARM data/prefetch abortを検出して失敗扱いにする。
- ARM data/prefetch abortログにframe番号を出すようにした。ARM9 data abort時はDTCM-awareなstack/ref診断も出す。

## 現在わかっていること

- host/clientともstage scene、player actor、Big Star actorを生成できる。
- `Net::random.value=0x00000100` などで初期Big Star位置は一致させられる。
- `playerCount=2` と stage scene runtime words `word154=1, word160=0xDA` は、試合中player更新を動かす有力条件。
- ただし現在の直接stage開始ルートでは、hostのlocal player0が動かない。
- 2026-05-22の再検証では、host/clientとも `inputPlayer0Held=0x10` まで入っているため、問題は入力注入ではない。
- client側のremote player0は同じ入力で動く。
  - client 2500f: `playerActor0X=0x1FD70`, `playerActor0VelX=0x14B0`
- host側のlocal player0は同じ入力でも動かない。
  - host 2500f: `playerActor0X=0x8000`, `playerActor0VelX=0`
- write traceではclientのplayer0座標/速度に `PC=0209FD70/0209FD88` から毎フレーム書き込みがある。
- `0209FD18` 周辺の逆アセンブルでは、`0x020C9250` のbyte値とactor側freeze maskのANDで早期returnする。
- `0x020C9250` はUS `Stage::actorFreezeFlag` 相当のA2DJ候補。hostでは `0x26`、clientでは `0x00` になっていた。
- host限定で `Stage::actorFreezeFlag` 候補を `0` に固定すると、hostのlocal player0は動く。
  - `logs/nsmvl-force-actor-freeze-flag-hostonly-20260522-attempt1`
  - host 2300f: `playerActor0X=0x35FF0`, `playerActor0VelX=0x1800`
- PacketBridge trace上、hostは `keys=0x0010` のplayer0 packetを送信し、clientも受信できている。
- client側の問題は、受信そのものではなく、`getPacket` lookup tickが受信済みtickより先へ進み `action=0xFF` になること。
- `PacketBridgeLiveFallbackLatestBefore` をstage生成後の2300fから有効化すると、client側もhostの右入力を消費し、remote player0が動く。
  - `logs/nsmvl-freeze-flag-latest-before-start2300-20260522-attempt1`
  - client 2400f: `inputPlayer0Held=0x10`, `playerActor0X=0x8070`
  - client 2500f: `playerActor0X=0x1FD70`, `playerActor0VelX=0x14B0`
- ただし同じrunでclient側が `pc=01FF8C28` のARM9 data abortを起こす。これはplayer更新が走った後に、まだ不足しているscene/player初期化条件へ到達している可能性が高い。
- `Stage::actorFreezeFlag` 診断writeはstageGroup 9 / vsMode 1中だけに制限した。stage外で同じアドレスを触るとclientのstage遷移を壊す可能性があるため。
- 両側で `Stage::actorFreezeFlag=0`、latest-before fallbackをstage後に有効化すると、host local player0とclient remote player0が動くrunはある。ただしclient側 `pc=01FF8C28` abortが再現するrunもある。

## 直近の検証ログ

- `logs/nsmvl-input-global-trace-20260522-attempt1`
  - host/clientのInputグローバル候補をCSVで確認。
  - host/clientとも `inputPlayer0Held=0x10`。
  - actor更新はclientだけ進む。
- `logs/nsmvl-player-write-trace-20260522-attempt1`
  - client player0の座標/速度writeを確認。
  - host player0の座標/速度writeはなし。
- `logs/nsmvl-player-actor-dump-20260522-attempt1`
  - 2300f/2500fのRAM dumpを取得。
  - host/clientのactor0構造体差分を比較可能。
- `logs/nsmvl-host-remote-player0-probe-20260522-attempt1`
  - host側 `localPlayerID=1` 診断はhost player0移動にはつながらず、client actor生成も崩れる。
- `logs/nsmvl-force-actor-freeze-flag-hostonly-20260522-attempt1`
  - host限定で `Stage::actorFreezeFlag` 候補を0固定。
  - host local player0の移動更新が走ることを確認。
- `logs/nsmvl-freeze-flag-packettrace-20260522-attempt1`
  - freeze flag修正後もhostは `keys=0x0010` のpacketを送信できる。
  - clientも受信するが、lookup tick先行で試合中packetとして消費できない。
- `logs/nsmvl-freeze-flag-fallback-20260522-attempt1`
  - 近傍tick fallback window 8ではclient消費はまだ改善せず。
- `logs/nsmvl-freeze-flag-latest-before-start2300-20260522-attempt1`
  - 2300f以降のみ、受信済み最新packetをlookup tickへ正規化して返す。
  - host local player0とclient remote player0が同じ右入力で動くことを確認。
  - clientでARM9 data abortが発生するため、まだ成功扱いにはしない。
- `logs/nsmvl-both-freeze0-guarded-latest-before-20260522-attempt2`
  - 両側stage中のみ `Stage::actorFreezeFlag=0`。
  - client abortは `frame=2430 pc=01FF8C28`。abort直前にplayer0が動き始めている。
- `logs/nsmvl-abort-near2430-dump-retry-20260522-attempt1`
  - 2440fまでabortなしでhost/client双方のplayer移動を確認したrun。
  - 同条件でも再現性に揺れがあるため、直接stage開始ルートの不足初期化条件がまだ残っている。

## 現在のブロッカー

直接stage開始ルートでhost local player actorを動かす条件は `Stage::actorFreezeFlag` 候補まで絞れた。

次のブロッカーは、packet消費を動かした後にclient側で `pc=01FF8C28` のARM9 data abortが出ること、および同じ条件でもclientがstage遷移に失敗するrunがあること。host local player0とclient remote player0の移動までは到達したので、次は直接stage開始ルートの不足初期化条件を潰す。

## 次にやること

1. client側 `pc=01FF8C28` data abortの直前状態を、DTCM-aware abort stack/ref、RAM dump、call trace/write traceで切り分ける。
2. clientがstageGroup 9 / player actor生成へ安定して入るために必要な初期化fieldを特定する。
3. abort原因が不足初期化条件なら、scene/player/packet sequencerのどのfieldが必要かを特定して最小patch化する。
4. ARM abortなしでhost local player0とclient remote player0が同じ入力で動くrunを作る。
5. 条件が固まったら、診断patchをROM patchまたは下位MP adapterへ縮小する。

## 重要アドレス

- `Net::getConsoleKeys(u16)`: `0x0200E700`
- `Net::getPacketByte(u16,u32)`: `0x0200E978`
- `Net::getPacketTick(u16)`: `0x0200E9BC`
- `Net::getPacketAction(u16)`: `0x0200E9DC`
- `Net::Core::transferPacket`: `0x0200F98C`
- `Net::updatePacket`: `0x020101E4`
- `Net::Core::processRecvPacket`: `0x02011360`
- `Net::Core::processSendPacket`: `0x02011428`
- `Net::Core::checkAllPacketBits`: `0x020110E4`
- `Net::Core::advancePacketSequencer`: `0x0201122C`
- packet buffer: `0x02087F00`
- net state base: `0x02087E00`
- `Game::stageGroup`: `0x02085058`
- `Game::localPlayerID`: `0x020850BC`
- `Game::vsMode`: `0x020850C4`
- `Game::playerCount`: `0x0208A988`
- `Input::consoleKeys` A2DJ候補: `0x02086C90`
- `Input::playerKeysHeld` A2DJ候補: `0x02086CA0`
- `Input::playerKeysPressed` A2DJ候補: `0x02086CA4`
- `Stage::actorFreezeFlag` A2DJ候補: `0x020C9250`
- `Game::loadLevel`: `0x020068A8`
- `VSConnect::startLoadLevel`: `0x0214E0C0`
- `createStageStartSM`: `0x021515B4`
- `loadMvsLFilesThread`: `0x02152E04`
- stage scene object ID: `0x0003`, settings `0x00B5FF00`
- player actor object ID: `0x0015`
- Big Star actor ID: `0x0022`

## 検証に必要なもの

- ユーザー提供の `roms/nsmb.nds` を使う。
- ROM本体や商用素材はリポジトリに含めない。

## 参考

- NSMB Central: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi
- `external/NSMB-Code-Reference`
