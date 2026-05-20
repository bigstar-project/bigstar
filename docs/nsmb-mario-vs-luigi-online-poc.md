# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦モード `Mario vs Luigi` を、最終的に `1 melonDS instance * 2PC` で WAN 越しに遊べる形にする。

現在の本命方針は、DS LocalMP 全体を WAN に流すのではなく、NSMB の MvL が使う接続 state-machine と入力 packet を解析し、LocalMP の代わりに WAN adapter から必要な packet/state を渡す方式。

## 採用しない方針

- `2 instances * 2PC` を最終形にする方針: LocalMP 間の actor/object 差分が残り、重く、検証も遅い。
- savestate 同期を最終形にする方針: 通信状態と actor lifecycle が揃わず、切断やズレが残る。
- 試合途中で LocalMP から WAN に切り替える方針: 切り替え時の停止で host/client state が壊れやすい。
- `MPInterface_LAN` を単純に WAN 化する方針: LAN 前提の遅延で接続/開始段階が崩れる。

## 現在の到達点

- 通常 LocalMP LAN ルートでは `stageGroup=9`, `vsMode=1`, player actors, Big Star actor まで到達できる。
- NSMB Central の情報と実測から、MvL gameplay packet は主に `tick`, `keys`, `action` を中心に構成されることを確認済み。
- NoLanMP + PacketBridge from start で host/client 双方の packet 送受信自体は可能。
- `VSConnect::updateLoadGameSM` 周辺の hotpatch と trace により、host 側は WAN adapter 経路から `Game::loadLevel` まで自然到達できる。
- `ForceLoadGameSMBaselineFlags` により、通常 LAN 相当の `step`, `timer`, `vs_flags`, net sequence fields を再現できる。
- 2026-05-20 の追加検証で、`SafeStartLoadCall` を `0206B83C` から呼ぶと client 側も `VSStageIntro` scene `0x0F` の `READY!` 画面まで到達する。
- `SafeStartLoadCall` と別 safe-call を同時に使えるようにし、`localPlayerID` / `stageGroup` / `vsMode` を startLoad 前後で保持する補正を追加済み。
- stage scene `0x03` の factory 呼び出しを追加し、host/client 双方で `sceneCurrentSceneID=0x3` までは到達する。

## 現在のブロッカー

stage scene `0x03` 生成後に gameplay actor が生成されない。

具体的には、`SafeStartLoadCall` で `READY!` 画面、`SafeStageSceneFactoryCall` または scene request 方式で `sceneCurrentSceneID=0x3` までは進むが、その直後に ARM9 data abort (`pc=0205545C`, `lr=02044388`) が発生し、player actors / Big Star actor は出ない。

2026-05-20 の追加検証で、単純な stage scene factory 直呼びだけでなく、`sceneNext=0x3` を立てて NSMB 本来の scene manager に遷移させる方式でも同じ abort になることを確認した。現在の有力仮説は、Stage scene 生成前に必要な MvL リソースロードまたは loadGameSM state がまだ自然 LAN と一致していないこと。

## 直近の検証結果

- `logs/nsmvl-safecreate-pregame-step1-baselineflags-20260520`
  - host は `createLoadGameSM`, `loadMvsLFilesThread`, `Game::loadLevel` に到達。
  - client は `LoadGameSM step=7` 相当まで補正できるが、自然な level load には入らない。
- `logs/nsmvl-client-safeupdate-no-safecreate-20260520`
  - client だけ `updateLoadGameSM` を safe-call すると step は進むが、呼び続けると data abort になる。
- `logs/nsmvl-client-directload-after-host-loadsm-20260520`
  - host は actor / Big Star actor 生成まで到達。
  - client は `stageGroup=9`, `localPlayerID=1` になるが、actor は生成されず「マリオをさがしています」で停止。
- `logs/nsmvl-client-directload-forcetick-20260520`
  - `PacketBridgeForceTick` を足しても client の `netPacketTick` は `0x1` のまま。
- `logs/nsmvl-loadsm-step7-preload-20260520`
  - `loadMvsLFilesThread` の強制preloadは発火するが、target step 7 + preload だけでは host/client とも開始に進まない。
- `logs/nsmvl-safestart-localplayer-fix-20260520`
  - client は `SafeStartLoadCall` 後に `stageGroup=9`, `localPlayerID=1`, `sceneCurrentSceneID=0xF` へ到達し、スクリーンショット上も `READY!` 画面になる。
- `logs/nsmvl-both-safestart-stagefactory-20260520`
  - host/client 双方で `SafeStageSceneFactoryCall` により `sceneCurrentSceneID=0x3` へ到達。
  - ただし両側とも `pc=0205545C` で data abort し、actor 生成までは進まない。
- `logs/nsmvl-vsstageintro-system-probe-mode1f-20260520`
  - `SafeCallProbeMode=0x1F` で VSStageIntro 中の System mode 候補 PC を取得。
  - ただし System mode から stage factory を呼んでも `pc=0205545C` の data abort は解消しない。
- `logs/nsmvl-set-scene-request-only-20260520`
  - `scenePrevious=0x5`, `sceneNext=0x3` を設定し、外部から `tryChangeScene` を直接呼ばずに NSMB の scene manager に任せる方式を検証。
  - host/client とも `sceneCurrentSceneID=0x3` へ進むが、`sceneIsSceneActive=0` のまま actor は生成されず、同じ data abort が出る。
- `logs/nsmvl-loadsm-preload-setscene-20260520`
  - `ForceLoadGameSMBaselineFlags + Preload` と scene request 方式の組み合わせを検証。
  - `VSConnect` state は `3/3` に近づくが、scene 6 から進まず prefetch abort になり、この組み合わせは現状不採用。

## 実装済みの主なテストフック

- 入力スクリプト再生
- screenshot / framebuffer dump
- game-state trace
- call trace / write trace
- MainRAM dump
- packet capture / packet replay / packet bridge trace
- Big Star actor ID `0x00D2` 周辺 probe
- `PacketBridgeForceTick`
- `ForceNetReady`, `ForceLoadGameSM`, `ForceTransferResult`
- role scoped `ForceNetReady`
- `ForceLoadGameSMBaselineFlags`
- `SafeStartLoadCall`, `SafeCourseSelectCall`, `SafeCourseSelectFactoryCall`
- `SafeStageSceneFactoryCall`
- `SafeCallProbe`, `SafeCallProbeOnly`, CPSR mode フィルタ
- `SafeTryChangeSceneTarget`, `SafeTryChangeSceneSetOnly`
- `SafeUpdateLoadGameCall`, `SafeCreateLoadGameSM`
- role scoped `DirectMvlBoot`
- 黒画面検出、切断待機画面検出、gameplay actor 検証

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
- `Game::stageGroup`: `0x02085058`
- `Game::localPlayerID`: `0x020850BC`
- `Game::vsMode`: `0x020850C4`
- `VSConnect::createLoadGameSM`: `0x021515B4`
- `VSConnect::updateLoadGameSM`: `0x021512B8`
- `VSConnect::startLoadLevel`: `0x0214E0C0`
- `Game::loadLevel`: `0x020068A8`
- `CourseSelectFactory`: `0x020130A8`
- `Scene::tryChangeScene`: `0x020131DC`

## 次にやること

1. `pc=0205545C` / `lr=02044388` data abort の参照元を特定する。特に `Random` 初期化、Stage scene のリソース未ロード、object profile 参照のどれが null になっているかを切り分ける。
2. 通常 LAN と PacketBridge ルートの Stage 遷移直前 RAM を比較し、scene globals 以外に不足している loadGameSM/resource state を特定する。
3. `SafeStartLoadCall` の直呼びから離れ、`VSConnect::createLoadGameSM/updateLoadGameSM/loadMvsLFilesThread/startLoadLevel` の自然な順序を再現する。
4. host/client 両方で player actors / Big Star actor が出るまで進め、黒画面・切断画面ではないことをスクリーンショットで確認する。
5. actor 生成後に gameplay packet の `tick`, `keys`, `action` 同期検証へ戻す。

## 必要なもの

- 検証にはユーザー提供の `roms/nsmb.nds` を使う。
- ROM パッチへ進む場合も差分パッチとして管理し、元 ROM は含めない。

## 参考

- NSMB Central: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi
