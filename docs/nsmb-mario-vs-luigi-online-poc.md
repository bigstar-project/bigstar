# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` でWAN越しに遊べる形へ持っていく。

LocalMPをWANへそのまま伸ばす方針、melonDS 2インスタンス * 2プロセスの同期方針、試合開始後にWANへ切り替えるsavestate/dropmp方針は、desync・切断・低FPS・不自然なstage/model初期化が重く、主方針から外した。

## 現在の方針

1. **接続/ロビー段階から下位MP API / packet境界をWAN adapter化する**
   - NSMBのMvL同期ロジック自体はできるだけそのまま使う。
   - 対象は `Net::getPacket*` だけでなく、packet availability、session/peer状態、packet sequencer、peer/user info、stage開始条件まで含める。
   - 既知入口は `Net::Core::transferPacket`、`Net::updatePacket`、`Net::Core::processRecvPacket/processSendPacket`、`Net::Core::checkAllPacketBits/advancePacketSequencer`、lower MP status/getPacket系。

2. **MvL開始状態を直接作るルートは診断専用**
   - `loadMvsLFilesThread`、`Game::loadLevel`、stage scene factory、render pointer guardなどは、試合中packet同期の先を見るための診断道具。
   - 最終実装としては、個別actor座標やrender/model状態を後付け同期する方向には進めない。

3. **試合中はNSMBのinput packet同期に寄せる**
   - NSMB Centralの解析どおり、試合中は入力packet中心の同期で進む想定。
   - 乱数は個別actorを直接合わせず、`Net::random.value` / `Net::getRandom()` 系の共有シードと消費順を合わせる。

## 現在のブロッカー

- `NoLanMP + PacketBridge + AllowPreGame` でhostはCourseSelectからstageへ進めるが、clientは `VSConnect::updateLoadGameSM` に入り、`stageGroup=0` / VSConnect sceneのまま止まる。
- `localAid=1` はclientをsearchSMからloadGameSMへ進める重要条件だが、それだけではstage開始に到達しない。
- clientを直接 `StageStartSM` へ入れる診断ではCourseSelect生成までは進むが、stage sceneへ遷移せず、処理が重くなってtimeoutする。
- `LoadGameSM` 周辺のcall traceでは、clientが `VSConnect::updateLoadGameSM -> func_02151074 -> 020108E8` までは到達する。想定したpeer info境界 `02001050/0200102C` はまだclientで捕まっていない。

## 直近で実装/検証したこと

- game-state CSVにVSConnect詳細とNetグローバルを追加。
  - `Net::localAid/currentLanguage/expectedConsoleCount/sessionState/moduleState/maxSessionChildren/maxConsoleCount`
  - VSConnect `+0x0E2/+0x106/+0x138/+0x13C/+0x140/+0x144/+0x148/+0x153..0x158`
- game-state CSVヘッダの `playerActor0VelZ/playerActor1VelZ` 欠落を修正。これ以前の拡張CSVはVSConnect列が2列ずれて読める可能性がある。
- `PacketBridgeFakePeerInfo` 診断フックを追加。
  - env: `MELONDS_NSML_PACKET_BRIDGE_FAKE_PEER_INFO`
  - script option: `-PacketBridgeFakePeerInfo`
  - LocalMPが返すpeer/user infoの最小スタブを試すためのもの。
- 短いcall traceで、clientのLoadGameSM初回更新が `020108E8` を呼ぶことを確認。

## 主要な検証ログ

- `logs/nsmvl-ui-wan-extended-vsconnect-fixed-20260524`
  - hostは `StageStartSM -> CourseSelect` へ進む。
  - clientは `LoadGameSM` のまま。`vsConnectWord140=2`, `vsConnectWord144=1`, `vsConnectWord154=1`。
- `logs/nsmvl-ui-wan-client-stage-direct-20260524`
  - clientを直接 `StageStartSM` へ入れるとCourseSelect生成までは進む。
  - ただしstage sceneへ遷移せず、clientが遅くなってtimeout。
- `logs/nsmvl-ui-wan-fake-peer-info-20260524`
  - fake peer infoを入れてもclientはLoadGameSMから抜けない。
  - fake peer info hook自体のログが出ていないため、まだ狙った境界に到達していない。
- `logs/nsmvl-ui-wan-fake-peer-calltrace-20260524`
  - client初回LoadGameSM更新で `020108E8` 呼び出しを確認。

## 実装済み基盤

- 自動検証用の入力スクリプト、スクリーンショット、framebuffer/hash、RAM dump、game-state CSV、packet capture/replay/bridge trace。
- `NoLanMP + PacketBridge from start` 検証ルート。
- A2DJ向け主要Net関数・グローバル移植。
- `Net::random.value` match seed配布と自動注入。
- ARM data/prefetch abort検出、ITCM/DTCM aware dump、call/write trace。
- 診断用の直接stage開始、MvL file load、stage scene factory、player/render guard系フック。

## 次にやること

1. `020108E8` / `0201090C` / `020108C0` 周辺を解析し、LoadGameSMの「peer/session ready」判定を下位MP adapterとして再現できるか確認する。
2. `VSConnect::updateLoadGameSM` step 1で本来呼ばれるpeer/user info経路を、clientで確実にtraceできる条件を作る。
3. hostからclientへstage選択/開始情報がどのpacket byte / sequencerで渡るかを、`Net::PacketBuffer` / `PacketSequencer` 側から追う。
4. 直接StageStartSMやrender guardは、必要条件の確認にだけ使い、最終形へは持ち込まない。

## 重要アドレス

- `Net::getConsoleKeys(u16)`: `0x0200E700`
- `Net::getPacketByte(u16,u32)`: `0x0200E978`
- `Net::getPacketTick(u16)`: `0x0200E9BC`
- `Net::getPacketAction(u16)`: `0x0200E9DC`
- `Net::Core::transferPacket`: `0x0200F98C`
- `Net::updatePacket`: `0x020101E4`
- `Net::Core::processRecvPacket`: `0x02011360`
- `Net::Core::processSendPacket`: `0x02011428`
- packet buffer: `0x02087F00`
- net state base: `0x02087E00`
- `Game::stageGroup`: `0x02085058`
- `Game::localPlayerID`: `0x020850BC`
- `Game::vsMode`: `0x020850C4`
- `VSConnect::updateLoadGameSM`: `0x02151E94`
- `VSConnect::createLoadGameSM`: `0x021520A0`
- `VSConnect::updateStageStartSM`: `0x021512B8`
- `VSConnect::createStageStartSM`: `0x021515B4`
- `VSConnect::loadMvsLFilesThread`: `0x02152E04`
- Big Star actor ID: `0x0022`

## 検証に必要なもの

- ユーザー提供の `roms/nsmb.nds` を使う。
- ROM本体や商用素材はリポジトリに含めない。

## 参考

- NSMB Central: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi
- `external/NSMB-Code-Reference`
