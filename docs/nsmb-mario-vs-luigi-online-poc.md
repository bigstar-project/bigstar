# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` でWAN越しに遊べる形へ持っていく。

これまで試した `melonDS LocalMP 2インスタンス * 2プロセス`、savestate/dropmp、試合開始後にWANへ切り替える方式は、desync、切断判定、低FPS、stage/model初期化不一致が重く、主方針から外した。

## 現在の方針

1. **NSMBが使うローカル通信境界をWAN adapterへ置き換える**
   - NSMBのMvL同期ロジックはできるだけそのまま使う。
   - 対象は試合中packetだけでなく、接続、検索、peer/user info、session state、packet availability、packet sequencerまで含める。
   - `Net::start*`全体を丸ごとバイパスするより、`Wifi::*`下位APIを差し替えて `Net::Core` の状態機械を自然に走らせる方向を優先する。

2. **直接stage開始は診断用に限定する**
   - `StageStartSM`やstage sceneへ直接入れるフックは、必要条件の切り分け用。
   - 最終形では、actor座標やrender/model stateを後付け同期する方向へは寄せない。

3. **試合中はNSMBのinput packet同期へ寄せる**
   - NSMB Centralの解析どおり、試合中が入力packet中心なら、そこをWAN packet bridgeに接続する。
   - 乱数は個別actorを書き換えず、`Net::random.value` / `Net::getRandom()` の共有seedと消費順一致で扱う。

## 現在の状態

- `Wifi::startChildScan` / `Wifi::startParent` / `Wifi::connectToParent` 相当をWAN adapter側で成功扱いにし、`0204619C` lower status probeを `1` にすると、host/clientとも `VSConnect::LoadGameSM` の継続更新に入る。
- `PacketBridgeFakePeerInfo` により、clientは `getOpponentNickname()` 経由で「Marioがみつかりました」系の確認待ちへ進む。
- `MaintainSessionPeers` は早すぎると接続探索を壊すため、現在は frame 1472 以降の診断として使う。
- `PacketBridgeLiveFallbackLatestBefore + window=180` で、clientが要求する現在tickにhost packetがまだ無い場合も、直近の過去packetを渡せるようになった。
- 診断用 `ClientConfirmToStageStart` により、clientのhost確認待ちタイムアウト時に `LoadGameSM` へ戻らず `StageStartSM` へ進ませるところまで確認した。
- 最新の到達点は、host/clientとも `StageStartSM` へ入るが、まだstage scene開始には到達しない状態。
  - `StageStartReadyProbe` で `Net state1C/state20/state24` と `Game::vsMode/localPlayerID` をStageStart中だけ維持できる。
  - それでも `StageStartSM` step 2 以降のロード準備関数が自然成立せず、`stageGroup=9` / gameplay actor生成には未到達。
  - 次の境界は `StageStartSM` 内の `02046260`、`0200EAD8`、`02046C7C`、`02004BFC`、`02004B74`、`020109E0` のどこで待っているか。

## 実装済みの診断フック

- game-state CSV拡張
  - Net globals: `localAid`, `expectedConsoleCount`, `sessionState`, `moduleState` など。
  - VSConnect fields: `+0x13C`, `+0x140`, `+0x144`, `+0x148`, `+0x153..0x158` など。
- `PacketBridgeFakePeerInfo`
  - env: `MELONDS_NSML_PACKET_BRIDGE_FAKE_PEER_INFO`
  - script option: `-PacketBridgeFakePeerInfo`
  - `02001050`: fake `NicknameInfo` を返す。
  - `0200102C`: fake stable 6-byte identityを返す。
- `PacketBridgeBypassStartConnection`
  - env: `MELONDS_NSML_PACKET_BRIDGE_BYPASS_START_CONNECTION`
  - script option: `-PacketBridgeBypassStartConnection`
  - `Net::startChildScan` / `Net::startParentBroadcast` wrapperを成功返しにする古い診断。現在は主方針ではない。
- `PacketBridgeBypassWifiStart`
  - env: `MELONDS_NSML_PACKET_BRIDGE_BYPASS_WIFI_START`
  - script option: `-PacketBridgeBypassWifiStart`
  - `Wifi::connectToParent` / `Wifi::startChildScan` / `Wifi::startParent` 相当だけを成功返しにする新しい診断。
- `MaintainSessionPeers`
  - peer表、connected count、expected count、session completeを維持する診断。
- `PacketBridgeLowerStatusResult`
  - env: `MELONDS_NSML_PACKET_BRIDGE_LOWER_STATUS_RESULT`
  - script option: `-PacketBridgeLowerStatusResult`
  - `0204619C` lower status probeの戻り値を固定する。host側は `1` にしないと `LoadGameSM` 更新が続かない。
- `PacketBridgeClientConfirmToStageStart`
  - env: `MELONDS_NSML_PACKET_BRIDGE_CLIENT_CONFIRM_TO_STAGE_START`
  - script option: `-PacketBridgeClientConfirmToStageStart`
  - clientの確認待ちが `LoadGameSM` に戻る直前の `scheduleSubMenuChange(loadGameSM)` を、診断用に `StageStartSM` へ差し替える。
- `PacketBridgeStageStartReadyProbe`
  - env: `MELONDS_NSML_PACKET_BRIDGE_STAGE_START_READY_PROBE`
  - script option: `-PacketBridgeStageStartReadyProbe`
  - `StageStartSM` 中だけ、下位Net ready状態と `Game::vsMode/localPlayerID` を維持する診断。

## 主要検証ログ

- `logs/nsmvl-ui-wan-wifi-status-one-script-trace-20260524`
  - `0204619C -> 1` でhostも `02152018 -> 02150F70` に到達し、`LoadGameSM` が継続更新されることを確認。
- `logs/nsmvl-ui-wan-wifi-status-one-maintain-both1472-20260524`
  - frame 1472以降のsession peer維持でhostはStageStartSMへ進むが、clientは確認待ちから再スキャンへ戻る。
- `logs/nsmvl-ui-wan-wifi-fallback180-20260524`
  - live packet fallbackでclientがhost packetを読めるようになったが、host確認待ちはまだ自然成立しない。
- `logs/nsmvl-ui-wan-client-confirm-stage-20260524`
  - client確認待ちの戻り先を診断用にStageStartSMへ差し替え、clientもStageStartSMへ入ることを確認。
- `logs/nsmvl-ui-wan-stage-step-trace-20260524`
  - `StageStartSM` step 2で止まる。次はロード準備系の関数境界をさらに絞る。

## 次にやること

1. `StageStartSM` step 2以降の待ち条件を特定する。
   - 優先トレース対象: `02046260`, `0200EAD8`, `02046C7C`, `02004BFC`, `02004B74`, `020109E0`。
2. StageStartの診断フックを、できるだけ下位Net/Wifi境界のadapterへ戻す。
   - `ClientConfirmToStageStart` は最終形ではなく、host確認packet境界を特定するための一時フック。
3. stage sceneに到達したら、host/clientの `stageGroup`, `localPlayerID`, player actor, star/RNG seed, packet tickを比較する。
4. その後、試合中input packet同期に入り、local actor stateの直接書き換えに戻らず安定化する。

## 重要アドレス

- `Net::Core::startChildScan`: `0x0200F55C`
- `Net::Core::childScanState`: `0x0200FD90`
- `Net::connectToParent`: `0x0200F060`
- `Net::startChildScan`: `0x020108E8`
- `Net::startParentBroadcast`: `0x0201090C`
- `Wifi::connectToParent` candidate: `0x020465C4`
- `Wifi::startChildScan` candidate: `0x02046788`
- `Wifi::startParent` candidate: `0x020469A4`
- `VSConnect::updateLoadGameSM`: `0x02151E94`
- `VSConnect::getOpponentNickname` helper: `0x02150F70`
- `VSConnect::createLoadGameSM`: `0x021520A0`
- `VSConnect::createStageStartSM`: `0x021515B4`
- `Net::packet buffer`: `0x02087F00`
- `Net state base`: `0x02087E00`
- `Game::stageGroup`: `0x02085058`
- `Game::localPlayerID`: `0x020850BC`
- `Game::vsMode`: `0x020850C4`
- Big Star actor ID: `0x0022`

## 検証に必要なもの

- ユーザー提供の `roms/nsmb.nds` を使用する。
- ROM本体や商用素材はリポジトリに含めない。

## 参考

- NSMB Central: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi
- `external/NSMB-Code-Reference`
