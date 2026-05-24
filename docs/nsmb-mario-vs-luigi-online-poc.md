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

- `NoLanMP + PacketBridge + AllowPreGame` で、hostはCourseSelectからstageへ進めるが、clientは `VSConnect::LoadGameSM` で止まる。
- `Net::startChildScan` 全体をclientだけ成功返しにする診断では、clientが「Marioがみつかりました」確認画面まで進む。
  - `02001050` は `NicknameInfo` として読まれるため、fake peerは構造体ポインタではなくnickname直返しが必要だった。
  - ただしhost側とsession/confirm状態が一致せず、clientは後で「Marioがいなくなりました」へ落ちる。
- `Wifi::startChildScan` / `Wifi::startParent` / `Wifi::connectToParent` 相当だけを成功返しにする診断では、host/clientとも `LoadGameSM` に入り、`ggid=0x42`、`vsMode=1`、`localAid=0/1` までは揃う。
  - `Net::sessionState=2` とpeer表維持を追加しても、画面は「ルイージ/マリオをさがしています」のまま。
  - つまり、下位Wifiの開始成功だけでは足りず、peer発見/confirm/packet sequencerのどこかがまだ不足している。

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

## 主要検証ログ

- `logs/nsmvl-ui-wan-fake-peer-nickname-20260524`
  - clientが「Marioがみつかりました」画面まで進む。
  - その後、host/session confirmと噛み合わず消失扱いになる。
- `logs/nsmvl-ui-wan-fake-peer-nickname-wait-20260524`
  - client側A連打を入れても、hostが同じ接続状態を共有していないためstage開始へは進まない。
- `logs/nsmvl-ui-wan-wifi-start-bypass-20260524`
  - `Net::start*`丸ごとではなく下位Wifiだけを成功返しにした検証。
  - host/clientとも検索画面に残る。
- `logs/nsmvl-ui-wan-wifi-start-session-complete-20260524`
  - session completeとpeer表維持を追加しても検索画面から抜けない。
- `logs/nsmvl-ui-wan-wifi-session-calltrace-20260524`
  - `updateLoadGameSM`初回更新とstart routeの到達を確認。次はpeer発見/confirm系の分岐到達をもっと細かく追う。

## 次にやること

1. `VSConnect::updateLoadGameSM` の `word144=1` 後の分岐を、call traceまたは専用traceで追う。
   - 特に `02152018 -> 02152028 -> 02150F70` に入らない理由を確認する。
2. peer発見/confirmに必要な下位状態を特定する。
   - host側: `Net::sessionState==Complete` だけでなく、`consoleGameInfos` / `consoleStates` / peer confirmation flag のどれを見ているか。
   - client側: `02001050/0200102C` が呼ばれる条件と、呼ばれない条件。
3. `Net::Core::transferPacket` / `processRecvPacket` / `processSendPacket` / packet sequencerを、接続UI段階からWAN packet bridgeへ寄せる。
4. stage直接開始フックは、packet同期境界の検証に必要なときだけ使う。

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
