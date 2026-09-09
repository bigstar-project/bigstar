# 2026-09-09 WAN・ひとり検証 rollback ログ調査

## 現在の結論

修正済み。原因は、ROM-loopが入力の現在frameを処理した後に、さらに次frameを実行する `depth + 2` の境界だった。再実行をrestoreからcurrentまでの閉区間（`depth + 1`）へ修正し、次frameのcheckpointは次の通常gateで採取する。片側の訂正回数が増えるほどゲーム内部時刻だけが進む不具合を解消した。旧版との記録入力A/Bで因果関係を確認した。

実際のWAN回線で双方を更新して対戦する確認は未実施だが、元WANのROMペアと両者の全キー入力を用いた再生、ひとり検証の記録入力、深度7、片側連続訂正、再戦、両playerの在庫放出を検証した。修正後の恒久的なplayer状態差は確認していない。WAN再生で残った移動床の1標本差は、9月10日の同一game tick・処理段階比較で採取位相差と確定した（次節）。raw標本の毎frame完全一致を同期判定の基準にはしない。

## 移動床1標本差の原因確定（2026-09-10）

結論: raw frame末尾の採取位相差。移動床の生成・最初の更新が論理game tickで先行した不具合ではない。通常実行のraw標本は描画処理の途中で、ROM-loop訂正後の標本は同じtickの描画終了・物体有効化・tick末尾まで通過している。`movingHazardFound` は有効な物体をプロセスリストから読むため、この境界をまたいだ比較で0/1差になった。9月9日の「生成が1frame早い」は正確ではなく、「採取時点が有効化の前後に分かれた」へ訂正する。

再現は元WANのROM・stage・seed・記録入力を維持し、client checkpoint 2699、current 2700、深度1、2 tick訂正を固定した。元と同じCSV raw 2721のclientだけFound=1/X=`0x2a8000`、raw 2722では両側X=`0x2a8800`になる差を再現した。通常側のNDS内部frame 2720はCSV raw 2721に対応する。

| 比較する処理段階 | 無訂正対照と訂正再実行の両方で観測した移動床 |
| --- | --- |
| game 2479、gameplay開始前 | 対象GUIDはまだ存在しない |
| game 2479、gameplay終了・render開始/終了 | GUID `0x33`、state 0、X `0x2a8000`。ID一覧のみ、execute/render一覧には未登録 |
| game 2480、tick-end境界 | 同GUID、state 1、同X。execute/render一覧へ登録済み |
| game 2480、最初のgameplay開始 | 同GUID、state 1、X `0x2a8000` |
| game 2480、gameplay終了・render開始 | X `0x2a8800`。最初の移動と描画対象になるtickは一致 |

raw 2721で通常側は上表2行目、訂正側は3行目を記録する。実際のゲーム更新前後・render前後など、同じgame counterとmarkerで揃えたgame 2478〜2482の各role 50標本、計100標本では、GUID・状態・flags・位置・各プロセス一覧と、存在する移動床の先頭256 byteに差は0。次のframeで偶然再一致したという推測ではなく、同じ処理段階での状態一致を確認した。

確認範囲はこの生成イベントの物体状態・実行/描画一覧・最初の更新であり、画面全pixelや全衝突条件の網羅試験ではない。ただしこのログ差を根拠にゲームの生成処理やrollback件数を変える必要はない。診断を改善するなら、同じlogical tick・同じ処理境界のsnapshotを比較する設計にする。raw標本だけから有効化前後の差を同期エラー扱いしない。

作業物は `logs/codex-hazard-phase-20260910/` の `diagnostic.patch`、診断専用exe、各stageのRAM dump、`parse.py`、`compare-aligned.py`、`aligned-comparison.json`。runログは `logs/codex-rollback-fix-20260909/hazard-phase-{candidate,control,probe}/`。最初の自然遅延runはdump負荷で対象訂正が消えたため原因判定には使わず、固定probeを使った。診断なしの既存製品exeでも、元の左入力bit `0x20` を使う固定probeで同じraw 2721の差を再現した（`hazard-phase-clean-probe`）。診断負荷だけが生んだ差ではない。診断コードは製品ソースから除去し、通常exeを再ビルドして診断文字列が残らないことを確認、CTest 17件も再passした。GUI用sidecarは今回変更していない。

## 修正と検証結果

- `src/NSMLGameRAMRollback.h`: 再実行数を深度+1へ訂正。12-entry履歴の算術上限は深度11だが、設定で受け付ける上限10とGUI既定7は維持する。
- `tests/nsmb_rollback_store_tests.cpp`: 未来の入力を実行しない契約と、異なる回数・深度の訂正を行う2peerが無訂正の状態遷移へ一致する回帰テストを追加した。
- `scripts/run-nsmb-mvl-manual-local.ps1`: role別の固定prediction probeを追加。既定では無効。今回のROMペアでは旧split起動経路が通常入力を消費しなかったため、製品と同じstart-ready経路へ試験を移した。旧split試験の結果は修正判定に使用していない。
- CMake ReleaseビルドとCTest 17件がpass。GUI packageの `corepack pnpm run ci` もpass（TypeScript、Biome、edition 15件、unit 37件、browser 73件、Playwright 2件）。既存のlinker警告（`-s`、WinMain/wWinMain）は残る。
- リポジトリ内のGUI用sidecar 2箇所にも修正版をコピーし、SHA-256 `3D1F176F035B44B0D1A2F0E1C951B00A24445AEAC5ABCC38BF865EE10E818B3A` の一致を確認した。配布版のインストール先やremoteは変更していない。

作業物の共通rootは `logs/codex-rollback-fix-20260909/`。表のframeはraw観測frame、訂正件数はhost/client。

| Run | Frames | 訂正件数 | 結果 |
| --- | ---: | ---: | --- |
| baseline-r1 | 1800 | 63 / 32 | 旧版でraw 930の元ログの座標差を再現。訂正完了後の主要状態差はhost 722 / client 659標本 |
| control-no-rollback-r1 | 1800 | 0 / 0 | raw 850〜1800の951標本、主要状態のpeer差0 |
| candidate-plus1-r1 | 1800 | 63 / 32 | 対応差220固定、訂正完了後の対照差0 |
| candidate-plus1-full | 3300 | 116 / 55 | 記録入力全体、対応差220固定、訂正完了後の対照差0 |
| candidate-plus1-depth7 | 3300 | 76 / 85 | 送信遅延8〜9、実深度1〜7、phase recovery有効。訂正完了後の対照差0 |
| manual-probe-client-depth5 | 2100 | 6 / 4 | client checkpoint 1798/1804/1810/1816を深度5で訂正、完了後の対照差0 |
| manual-probe-host-depth7 | 2100 | 10 / 0 | host固定4訂正を含む深度7、完了後の対照差0 |
| wan-input-candidate | 4300 | 112 / 95 | 元ROMペア、stage 0、seed、両roleの全キー入力を再現。対応差221固定、player状態の完了後差0。移動床検出1標本差は次frameで収束（9月10日に採取位相差と確定） |
| rematch-candidate | 4100 | 31 / 32 | frame 1957でgeneration 1へ移行、両generationで対応差220固定。再戦後も状態差は収束 |
| stock-depth7 | 2200 | 2 / 2 | 両方向の送信遅延8〜9。Mario/Luigiの在庫2→0が両peerでraw 1838/1938と対照に完全一致。1351標本で主要状態の対照差・peer差0 |

対照との比較はactor座標・速度・状態、powerup/在庫、coin/score、星、死亡/残機、Big Star、移動床を使用した。予測が未確定の区間（mismatch入力の適用から訂正completeの直前まで）と、完了後に残る差を分離した。旧版だけがこの確定後比較に大量の差を残す。performanceは今回の完了条件を同期に置いており、深度7で40〜78ms級の単発frameは残る。

固定訂正の再試験は通常のmanual-local引数に、例えば `-RollbackPredictionProbeRole client -RollbackPredictionProbeModulo 6 -RollbackPredictionProbeOffset 4 -RollbackPredictionProbeStartFrame 1798 -RollbackPredictionProbeEndFrame 1816 -RollbackPredictionProbeLimit 4 -RollbackPredictionProbeConfirmDelayFrames 5 -RollbackPredictionProbeKeyMask 0x10` を追加して行える。元入力・ROM・seedを維持した完全な起動fixtureと集計は共通rootの `run-solo.ps1`、`compare.py`、`check-confirmed.py`、各runのJSONに保存した。

## 対象と対応確認

- WAN client: Downloads 配下の実在フォルダー名は `feedback_2026-09-08_MZgHx9pj0y5bhbAcbqUZtUa-_bigstar-feedback-1788874597-bigstar-1788874472215-16356-4`。依頼文の階層区切りとは異なる。
- WAN host: `%APPDATA%/Bigstar Insiders/logs/bigstar-1788874471894-43376-4`。開始は相手と約352ms差。ROM pair IDが一致し、logical frame 3780 / 4080 / 4380 の local/remote playerGlobal hash が双方向に反転一致するため、対戦相手のログであることを確認した。
- WAN版はInsiders 0.11.5、両peerの記録されたemulator buildは `dd079ba9856aec6a8e5b6463d393f871616b6377`。hostはstage 0、seed `0x02D96F47`、入力遅延2、horizon 7、ROM-loop。
- ひとり検証: `%APPDATA%/Bigstar Insiders/logs/bigstar-1788887323729-41988-1/{host,client}`。stage 3、seed `0x8764C3BE`、入力遅延2、両方向の人工送信遅延2〜3、drop設定0、horizon 7、ROM-loop、JIT有効。
- ひとり検証はbuild commit未埋込み。調査開始時点のcheckoutとWAN版commitの間で、今回問題にする `NSMLGameRAMRollback.h`、`NsmbRollbackRuntime.cpp`、`NDS.cpp`、ROM builder本体に差分はない。ただし未埋込みバイナリ自体のcommitを確定したわけではない。

## 共通の時間軸異常

armログの `checkpoint` はgeneration内の入力論理フレーム、`gameFrame` はcheckpointに保存されたゲーム内部カウンター `0x0208B668` の値。以下はその差であり、raw描画フレームとの比較ではない。

| 対象 | arm / complete | 深度分布 | 最初→最後の checkpoint − gameFrame | 隣接arm間の変化 |
| --- | --- | --- | --- | --- |
| WAN host | 8 / 8 | 1:5件、3:2件、4:1件 | 221 → 214 | 7区間すべて −1 |
| solo host | 114 / 114 | 1:69件、2:42件、3:3件 | 220 → 120 | −1が100区間、0が13区間 |
| solo client | 116 / 116 | 1:94件、2:22件 | 220 → 116 | −1が104区間、0が11区間 |

- WAN host: checkpoint/gameFrame は `979/758`, `1027/807`, `1041/822`, `1269/1051` と進む。同じ入力時間軸に対してゲーム側が訂正を挟むごとに1刻み多く進む形になっている。
- soloは最初のcheckpoint 895が両peerともgameFrame 675。その後の共通checkpoint 1033はhost 816 / client 814、checkpoint 2995はhost 2875 / client 2879。同じ論理入力番号の保存状態が別のゲーム時刻になる。
- WAN hostはこの区間でgeneration resetなし。solo両peerもresetなし。再戦時のepoch切替による見かけの差ではない。
- 3本の完全stdoutでcannot-arm、checkpoint missing、failed scheduling、capped correction、prediction-horizon invariant failureは確認されない。全armがcompleteしたことと、状態が正しく直ったことは別である。

## ずれの観測内容

CSVは30 raw frame間隔。以下の最初の差は「保存された標本の最初」であり、厳密な発生frameではない。

- WAN: raw 990の対象座標標本までは一致し、1020で両player座標と移動床Xに差。host最初の訂正completeはraw 1001。logical 1080で最初のplayerGlobal警告。raw 1260ではplayer 1のコインがhost 1 / client 3、raw 1650では同1 / 7、星も0 / 1。後半は死亡・残機・在庫まで食い違う。通信末尾の停止より十分前から実ゲームの分岐がある。
- solo: raw 900の対象座標標本までは一致し、930で両playerのYが `0xFFE8A000 / 0xFFE89400` に分岐。hostはraw 917と927で訂正完了、clientの最初の完了は916。raw 990にはX/Y・速度も異なり、後続でも差が続く。
- soloの最初のplayerGlobal警告はlogical 1650。これは最初の座標差より大幅に遅い。raw 2400ではMarioのコイン3 / 2、raw 3000ではLuigiの死亡1 / 0、星2 / 3、コイン5 / 7。単なる描画位相差では説明できない。
- solo両peerのstart-readyはraw 859 / shared epoch 840で一致。WAN host記録でも両peer raw 860 / epoch 840が一致。開始ハンドシェイクずれを示す証拠はない。

## コードとの対応と判断の更新

以下は調査時点の修正前コード。現在の実装は冒頭を正とする。

1. `src/NSMLGameRAMRollback.h:RequiredHistoryCount` は `current - restore + 2` を返していた。
2. `src/frontend/qt_sdl/NsmbRollbackRuntime.cpp` はその件数を実際の入力履歴として作り、`current + 1` の入力まで含めてguestへ渡す。
3. `tools/bigstar-rom/src/lib.rs` のhistory gateはcountを実行件数に使い、追加loop要求を `count - 1` に設定する。末尾は単なるcheckpoint予約ではなくゲーム処理を実行する入力である。
4. `src/NDS.cpp:ApplyNSMLPendingGameRAMRestore` は旧gameFrameに基づく件数clampを廃止している。`CaptureNSMLGameRAMCheckpointAtGate` はその結果の内部カウンターをlogical labelとともに保存する。
5. 完了判定は要求件数以上のゲーム進行を確認するが、訂正前の時間軸へ正確に戻ったかは検査しない。修正前のunit testも `+2` の数式を期待値としており、peer間の時間軸維持の代わりにはならない。

このコードと「訂正を挟むと対応差が1減る」実測は整合する。8月26日の限定probe合格から `depth + 2` を一般に安全とした評価は撤回する。ただし旧境界にも連続訂正の既知不具合があるため、単に戻すだけで安全とは判断しない。PhaseRecoveryは待機時間・速度の調整であり、今回のgame counter/checkpoint対応を直接修復するものではない。

## 残る制約と次の作業

- WAN clientのeventsは12MiB、phase-eventsは4MiB、stdoutは512KiBの末尾切り出し。events先頭に途中JSONが1行あり、完全JSONのringはraw 3195以後。初期client側の訂正履歴がなく、両側の厳密な最初の分岐はこの提供物だけでは確定できない。
- solo診断ringはraw 1310以後。初期の930付近は30frameごとのCSVとstdoutしかなく、最初の1frame単位の分岐を断定できない。
- `recorded.inputs` は両ウィンドウで一部のキー切替が1frame異なる。別プロセスのローカル採取結果なので、それ自体はネット同期不良の証明ではない。再実行ではrole別記録を維持する。
- 記録入力と片側・非対称訂正の対照試験、再戦・在庫放出の回帰確認は上表まで完了した。以前の「対照fixtureを作る」「境界の修正案を試す」は完了済み。
- 次の任意確認は双方を修正版へ揃えた実WAN対戦。ログ入力再生は実回線のpacket到着時刻そのものを再現したわけではない。
- 追加ROM・ツール導入は不要。既知の音声取り消し不能や単発hitchは今回の時間軸修正とは別件。
集計用のローカル作業物は `logs/codex-rollback-log-analysis-20260909/` に保存した。元ログは変更していない。
