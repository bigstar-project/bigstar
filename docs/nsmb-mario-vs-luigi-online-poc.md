# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` でWAN越しに遊べる形へ持っていく。

過去に試した `LocalMP 2インスタンス * 2プロセス`、savestate、試合開始後のWAN切り替え、actor座標の外部同期は、desync、通信切断、低FPS、初期状態不一致が重く、最終形としては採用しない。

## 現在の方針

1. NSMBが使うローカル通信境界を下側からWAN adapter化する。
   - 試合中packetだけでなく、接続、検索、peer情報、session state、packet availability、packet tickを含めて置き換える。
   - NSMB側のMvL同期ロジックはできるだけそのまま使う。
   - 個別actor座標や描画状態を外から同期する方向には戻らない。
2. 必要に応じてROM/メモリpatchでロビーや開始処理を短絡する。
   - UI操作やLocalMP接続の完全再現にこだわらず、MvL開始へ自然に入る入口を探す。
   - ただし任意PCから `Game::loadLevel` を直接呼ぶ方式はARM9 abortしたため、現状は不採用。
3. 試合中はNSMB Centralの記述どおり、入力packet中心の同期に寄せる。
   - RNGは `Net::random.value` / `Net::getRandom()` の共有seedと消費順一致で扱う。
   - スター、8コインアイテム、ランダムステージなどは後続の検証対象。

## 現在の到達点

- `Wifi::startChildScan` / `Wifi::startParent` / `Wifi::connectToParent` 相当をWAN adapter側で成功扱いにする診断経路を追加済み。
- `0204619C` lower status probeを `1` にすると、host/clientとも `VSConnect::LoadGameSM` の継続更新に入ることを確認済み。
- `PacketBridgeFakePeerInfo` でclientが「Marioがみつかりました」系の確認待ちへ進むことを確認済み。
- `ClientConfirmToStageStart` により、client確認待ちの戻り先を診断用に `StageStartSM` へ差し替えられることを確認済み。
- `StageStartSM` で `Net state1C=3`, `state20=0`, `state24=1`, `state2C=0`, `state34=0` を維持すると、host/clientとも `updateStageStartSM (021512B8)` が自然に呼ばれるところまで到達した。
- `updateStageStartSM` の実命令をRAM dumpから逆アセンブルし、step 3は `02087E20` の下位byteが `2` になることと、`02004B74` の完了戻り値を待っていることを確認した。
- ただし `state20=2` を最初から維持するとStageStartのcreate/render直後に黒画面・低速化へ入り、`updateStageStartSM` が継続しない。
- `state20=0` でstep 3まで進め、step 3以降だけ `state20=2` にする診断フックも追加したが、現状は黒画面・低速化が残り、`stageGroup=9`、stage scene、player actor生成にはまだ到達していない。
- 以前の `Net state1C=6`, `state20=2`, `state24=2` では `updateStageStartSM` が呼ばれず、`しばらくおまちください` のFontRenderer描画待ちへ張り付くことを確認済み。
- `PacketBridgeStageStartPacketAction=3` 単独では改善しないことを確認済み。

## 現在のブロッカー

`StageStartSM` step 3から先へ進まない。`vsConnect+0x144=3` までは入るが、`02087E20=2` を見せるタイミングを間違えると黒画面・低速化へ入る。

次に見るべきものは、LocalMP時に `02087E20` が `2` へ変わる自然な条件と、`02004B74` が完了を返すまでのファイルロード/スレッド状態。単に値だけを強制すると、StageStart render/updateの順序が崩れる可能性が高い。

## 失敗済みまたは非採用の経路

- 既存LocalMPの2台状態を後からWANへ切り替える方式
  - 切り替え時の停止、desync、通信切断、低FPSが重く、最終形に向かない。
- savestate方式
  - LocalMP状態を保存・復帰しても通信切断や初期状態不一致を解決できない。
- actor座標やrender/model stateの外部同期
  - アニメーションや内部状態が揃わず、NSMBの同期ロジックを壊す。
- 任意フレームからの直接 `Game::loadLevel`
  - frame 933付近でARM9 abort。安全な呼び出し文脈ではない。
- `StageStartSM` / `VSConnect::onUpdate` の直接呼び出し
  - 再入文脈が不正でARM9 abort。診断用としても常用しない。

## 実装済みの主な診断フック

- game-state CSV拡張
  - Net globals、VSConnect fields、App sleep fields、ARM9 PC/LR/SP/CPSR。
- `PacketBridgeFakePeerInfo`
  - peer nickname / identityを安定した値として返す。
- `PacketBridgeBypassWifiStart`
  - Wifi start/connect系を成功扱いにする。
- `MaintainSessionPeers`
  - peer表、connected count、expected count、session completeを維持する。
- `PacketBridgeLowerStatusResult`
  - `0204619C` lower status probeの戻り値を固定する。
- `PacketBridgeClientConfirmToStageStart`
  - client確認待ちの戻り先を診断用に `StageStartSM` へ差し替える。
- `PacketBridgeStageStartReadyProbe`
  - StageStart中の下位Net ready状態と `Game::vsMode/localPlayerID` を維持する。
  - `state1C/state20/state24/state2C/state34` を環境変数・スクリプト引数から変更可能。
- `StageStartDispatchTrace`
  - `VSConnect` / `StageStartSM` 近辺のPC、LR、VSConnect fields、Net stateをCSVに出す。

## 次にやること

1. `updateStageStartSM (021512B8)` step 3の分岐条件を追う。
   - compact/full dispatch traceは追加済み。step 3条件は `02087E20 == 2` と `02004B74 != 0`。
   - 次はLocalMPまたはより下位のNet/Wifi遷移から、`02087E20` とロードスレッド完了が自然にどう変わるかを比較する。
2. step 3で待っているNet stateまたはpacket条件が分かったら、下位adapter側の状態遷移へ戻す。
   - 診断用の強制値で進めるだけでなく、最終的には自然なWAN packet adapterの状態機械として扱う。
3. stage sceneに到達したら、host/clientの `stageGroup`, `localPlayerID`, player actor, star/RNG seed, packet tickを比較する。
4. 試合中input packet同期へ接続する。
   - 死亡演出や通信切断を成功扱いしない。
   - スクリーンショットとstate traceで確認する。

## 検証条件

- ユーザー提供の `roms/nsmb.nds` を使用する。
- ROM本体や商用素材はリポジトリへ含めない。
- 「通信が切断されました」は失敗として扱う。
- スター取得やゲーム進行はスクリーンショットとstate traceで確認し、死亡や別演出を誤判定しない。

## 参照

- NSMB Central: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi
- `external/NSMB-Code-Reference`
