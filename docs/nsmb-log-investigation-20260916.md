# Bigstar Insiders 実対戦ログ調査 — 2026-09-16

## 現在の結論 — 2026-09-19 修正・ローカル検証完了

- 当日の同期ずれ2件を旧exe/ROM/saveと両ボタン列で再現し、最初のcritical frameと両hashが元ログと一致した。入力番号を画面frameに結び付け、ゲーム更新途中でもscratchを上書きしていたことが原因。実WANや別PCを使わなくても発生する。
- 同期ずれの修正をC++側へ実装し、元の48500/66500-frame再生でcriticalは両roleとも0。全9試合の共通game tick 46789 / 64506件で、両playerの位置・速度、乱数値/呼出し回数、console/player入力の押下/保持18 fieldがすべて一致した。画面末尾の一時的な採取差は判定から分離した。
- 別件の約10.8秒フリーズは未解決。手元の停滞中に相手が入力待ち・ENet切断へ進み、相手のbridge周期記録は継続していたことまで判明。PC負荷・ログI/O・OS実行待ちの確定には再発時の性能記録が必要。
- 修正対象は今回の通常lockstep (`rollback=0`, delay=3)。ROM-loop rollbackの既存訂正経路は維持している。インストール版・GUI同梱sidecar・配布版は差し替えておらず、両側を更新した実WAN対戦は未確認。push・外部送信なし。追加ROM/ツール導入不要。

## 原因の証拠と修正

### 同じゲーム更新が別の入力を消費した

`session0-boundary` でゲーム内部counter (`0x0208B668`)、処理stage、入力scratchを計測した。

| 境界 | host | client |
| --- | --- | --- |
| game 12001開始 | raw 46471 | raw 46471 |
| game 12001終了 | raw 46472 | raw 46471 |
| game 12002の入力scratch | raw 46473、tick 11384、Luigi keys 0x800 | raw 46472、tick 11383、Luigi keys 0x810 |
| game 12002終了のLuigi X | 1880127 | 1880415 |

復活処理が片側だけ画面frameを跨ぎ、その次のゲーム更新で右ボタンを離すタイミングが変わった。初めの1frameの採取差そのものではなく、同じgame 12002の終了状態に差が生じたことが確定根拠。タイムスタンプの時計差や、相手側の手動停止を推測した結論ではない。

末尾-5も `session5-boundary-baseline` で同じ問題を確認。game 20889の終了がhost raw 64678 / client raw 64677へ分かれ、game 20902ではhostがtick 20284 / Luigi keys 0x810、clientがtick 20283 / 0x800を使った。ゲーム更新後のheld入力も異なる。診断版でも最初のcriticalは当日と同じ21480、両hashも一致した。fixture固有actorアドレスの記録は末尾-5の座標比較には使用していない。

### 実装

- `NsmbPacketBridgeRuntime` に、ボタンだけでなくtouch座標/遷移も保持する入力ペアFIFOを追加。ゲーム更新ごとに1組を消費し、display frameの進行による重複・欠落を拒否する。容量2048、異常時は理由をstderrへ出して終了する。
- `NsmbPacketBridgeIntegration` が既存GTP2 gateの署名・InputUpdate呼出し先・marker literalを検査し、入力境界markerをInputUpdate直前へ移す。4命令を実行時RAM上で並べ替え、JIT cacheを無効化する。ROMファイル・saveファイルは変更しない。適用済みcheckpointからの復元にも対応。
- `NDS` がROM処理境界をfrontendへ通知。通常lockstepのfrontendは入力開始でFIFOを消費し、処理途中の画面frameでは入力scratchを維持。gameplay終了で、消費した入力番号を使って従来の状態hash検査を行う。
- 再戦時は世代単位でqueueをリセット。入力送信を止める結果画面ではcallbackを解除する。既存ROM-loop rollbackには新queueを適用しない。
- `MELONDS_NSML_GAME_TICK_TRACE=<prefix>` を指定すると、世代別の `<prefix>-gN.csv` をgameplay終了境界で出す。`frame` は消費したlogical input番号。通常のdisplay-frame CSVは従来どおり。未指定時に追加CSV I/Oはない。
- fixture固有actorアドレスを読む一時診断コードは除去した。診断コードの記録と診断exeは隔離した検証フォルダーにのみ残す。

### 最終検証

| run | display frame上限 | 共通game tick数 | 18 fieldの差 | critical / role | 終了 |
| --- | --- | --- | --- | --- | --- |
| session0-fixed-final | 48500 | 46789 | 0 | 0 | 両側0 |
| session5-fixed-final | 66500 | 64506 | 0 | 0 | 両側0 |

- 各runの最後はclientのみ追加の1 game tickを完了しており、その片側分は比較対象外。共通範囲は欠番なしで全件比較。全メモリの一致を主張するものではない。
- 元ボタン列をそのまま使用し、両roleで元ログとの共通送信キーの値差・欠番0。全再戦ready frameも元ログと一致。最初の1500-frame試験は旧版のplayer主要28 field・乱数2 fieldとも22標本すべて一致した。修正後も通常区間を維持し、分岐後の誤った軌道を修正した。
- 最終版のhost/clientともstderr 0、frame limit到達・exit 0。CMake Release build成功、CTest **17/17 pass**。追加回帰は、片側だけの更新遅延、入力を離す境界、touch座標保持、再戦reset、欠番/重複、容量上限、gateの呼出し先/literal保持、異なるコードの拒否を検査する。
- 成果物は `logs/codex-desync-replay-20260919/`。`variant.py`で修正版exeを指定し、`verify_fixed.py`でゲーム更新境界の18 fieldを比較する。各最終runの `game-tick-verification.json` が機械判定結果。旧版再現については以下の記録を参照。
- 修正binary: `build/release-windows-x86_64/melonDS.exe`。SHA256 `326d2f1c4163072453b5a6b78689d783d93b50c06911ebedc7aa4164dc9c3015`で両最終runのexeと一致。利用時は対戦両側の更新が必要。今回の自動試験は同一PC・localhost直接UDPであり、更新版同士の実WAN確認は残る。

## 相手側Discord自動送信ログの追加照合 — 2026-09-19

### 提供物と確認範囲

Downloadsの以下4 ZIPを直接読み取った。展開・実行はしていない。

| ZIP | 手元の対応セッション | 相手のrole |
| --- | --- | --- |
| bigstar-feedback-2388-bigstar-1789485779356-2388-0.zip | 末尾-0 | host |
| bigstar-feedback-2388-bigstar-1789486649986-2388-1.zip | 末尾-1 | host |
| bigstar-feedback-2388-bigstar-1789488564664-2388-3.zip | 末尾-3 | client |
| bigstar-feedback-2388-bigstar-1789488680890-2388-4.zip | 末尾-4 | client |

- 各ZIPはfeedback-summary.json、session-events.jsonl、bridge.stdout.tail.txt、bridge.stderr.tail.txt、melonds.stdout.tail.txtの5ファイル。melonDS stdoutは各524288 byteで、先頭が途中から切れている。game-state、詳細events、phase、watchdog、性能履歴は含まれない。
- GUI 0.11.6、melonDS SHA256 `18578dae57f56b8f57d08f07155ab344090379fb5db2ac56802ebb4fb6d19444`、bridge SHA256 `3c9bf08a493bc1d834808665690ab3f18662a3bc7b576284024b6376e6b6a465`、input scriptのSHA256が手元と一致。session.romも対応する手元launcher.rom_identityと4件とも一致。host/clientは役割別ROMなので、起動ROM単体のhashが異なること自体は異常ではない。
- 相手のJIT config_enabledはfalseだが、runtime identityの実効enabledはtrue。手元もtrueなので、設定値だけを見てJIT有効/無効の不一致とは判定しない。rollback=0、delay=3、max lead=4も一致。
- summaryのperformance欠落を性能正常の証拠にしない。またcategory=crashだけでOSクラッシュと判断しない。末尾-0はmelonDS exited(0)、末尾-4は明示された入力timeoutによるexited(70)。

### フリーズの順序が両側から判明

末尾-3/-4は、同じframe・keysのsent/recv時刻を双方向に照合した。時計差が短い対象区間で一定、送信記録が対応受信に先行する前提で、相手時計の進みはそれぞれ1297〜1305 ms（1094/1099組）、1291〜1303 ms（1075/1079組）に収まる。以下の相手時刻は中点1301/1297 msを引いた推定値。絶対時刻の精密校正ではない。

| 対象 | 手元の処理停滞（JST） | 相手の入力待ち開始（補正後） | 相手のENet切断（補正後） |
| --- | --- | --- | --- |
| 末尾-3 | 01:10:42.622〜53.467 | 01:10:42.683、frame 4565 | 01:10:48.694 |
| 末尾-4 | 01:15:13.962〜24.766 | 01:15:14.028、frame 13849 | 01:15:21.223 |

- 相手のbridge周期ログは両停滞区間とも11標本あり、提供された周期ログ全体の最大間隔は末尾-3が1014 ms、末尾-4が1013 ms。相手の入力待ち経過・再送の記録も続く。相手プロセス全体が同じ10秒間止まったという仮説は支持されない。
- 両方とも手元の停滞から約60 ms後に相手が入力待ちへ入り、手元が復帰する前に相手ENetは切断済み。手元復帰後に入力待ちが解消しない順序を説明できる。手元の停滞そのものをCPU、ディスク、ログmutexなどのどれが起こしたかは、このZIPにも測定がなく未確定。
- 末尾-4の相手stdout 3905行目は `remote input timeout tUnixMs=1789488975325 frame=13849 waitedMs=60000 actualElapsedMs=60000 peer=0`。session-eventsは `1789488975726` にmelonDS `exited(70)`を記録。従来はソースから推定していた「切断後も待ち続け、60秒で終了」が相手側で実際に発生したと確認できた。
- 末尾-1は相手の周期記録も継続し、最終lastSent=7938 / lastRecv=7934。手元はlastRecv=7930なので、相手が入力を送ったと記録した後にも手元への入力到着が止まっていた。通信経路・bridge/ENetのどこで止まったか、相手の短い処理停滞が先行したかは抜粋だけでは確定しない。末尾-3/-4と同じ原因と一括りにしない。

### 同期ずれについて増えた証拠と限界

- 末尾-0は両側でgeneration 3、最初のcritical frame 12300が一致。手元local=`6B7A87629895F27F` / remote=`E9CDA7FAD69FC957`、相手local=`E9CDA7FAD69FC957` / remote=`6B7A87629895F27F`。frame 16920までのplayerGlobal不一致58件すべてで、この逆対応を確認した。
- 実行ファイル・ROMペアの取り違えと、片側だけの警告という説明は今回の資料に合わない。実効JITモードの不一致もない。ただし同じ実行ファイルでも実行時状態・タイミングまで等しくなる保証ではなく、ZIP単独では原因を確定できなかった。その後のローカル再生で入力適用境界の不備を確定した（上記）。
- ZIPには相手のplayer詳細snapshotや完全入力がないため、スター取得や接触などの最初の分岐を遡るには不足。01:36の末尾-5は今回の4 ZIPに含まれず、MarioのXの1.5px差を相手詳細snapshotと再照合する作業は未実施。

## 入力ログによるローカル再生 — 2026-09-19、2件とも再現

- ユーザー提案を受け、同期ずれ対象の末尾-0/-5を初戦からgeneration別に再集計。各generationのsentはlogical 843から、recvは840から記録される。最初のcritical検出（末尾-0はgeneration 3 / 12300、末尾-5はgeneration 4 / 21480）まで、記録対象範囲のボタン値に欠番・同じframeの値の衝突はない。末尾-0のgeneration 3全体には1件のsent欠番があるが、最初のcritical検出より後。
- launcherにはコース順・各試合の乱数seed・勝利条件・delay=3・rollback=0が保存されている。現在インストールされているmelonDS.exe、host/client ROM、両roleのsaveのSHA256を再計算し、当日の識別情報と一致することを確認した。
- `scripts/run-nsmb-mvl-manual-local.ps1` はHostInputScript/ClientInputScriptを受け付け、`NsmbInputTimeline`はframeごとのボタンと任意のtouch座標を再生できる。したがって既存の2-processローカル実行経路を使った調査は可能。ただしstdoutそのものをそのまま入力ファイルにはできず、roleの対応、generationごとのframe巻き戻り、入力遅延と採取frameへの変換が必要。
- sent/recvの通常行はkeysだけでTouching/X/Yを保存していない。Xボタンは実行時の `ConvertStockXToTouch()` が固定座標のストック操作へ変換するため、その操作は記録キーから再生成できる。一方、直接のタッチ操作やXと同時の別座標touchは、ボタン列だけから復元できない。直接touchがなかったとは未確認。
- 再戦前の履歴が影響する可能性を残し、初戦から同じコース順・seed・入力列で走らせ、元ログの同じlogical frameの状態と照合する方針が妥当。同期ずれ前まで一致していることを確認してから、最初に分岐する処理を絞る。
- **実行方法:** ユーザーの試行指示を受け、`logs/codex-desync-replay-20260919/replay.py` でexe/ROM/saveをrole別に複製し、元launcherのゲーム設定を継承する専用runnerを作成。キー列を `raw = localReady + logical - epoch - delay` で再生scriptへ変換し、範囲圧縮後に全キーのround-trip一致を検査した。既存製品exeのscript機能を使い、製品ソースは変更していない。起動processは今回のrunnerが所有するものに限定し、正常終了を確認した。
- **予備試験:** 末尾-0の1500-frame再生は両role exit 0、ready=858で元ログと一致、再送信キー差0。元ログと比較できる22標本でplayer位置・速度・主要統計と乱数状態の差0。本再生は以下の通り完了した。

| fixture | 再生frame数 | 最初のcritical（元ログと同じ） | 共通critical標本の両hash一致 | 元の手元roleとの状態比較 |
| --- | --- | --- | --- | --- |
| session0-full | 48500 | generation 3 / logical 12300 | 13/13 | 1588/1588標本でplayer主要28 field・乱数2 field一致 |
| session5-full | 66500 | generation 4 / logical 21480 | 11/11 | 2188/2188標本で同じ30 field一致 |

- 本再生は各1回。両roleの送信キーを元のsent/recv列とgeneration別に照合し、到達範囲の共通記録に値の差・欠番とも0。初戦とすべての再戦のready frameも元と一致した。session0-fullは159秒、session5-fullは222秒で、各host/clientともframe limit到達・exit 0。
- 最初のcritical hashは末尾-0がclient `6B7A87629895F27F` / host `E9CDA7FAD69FC957`、末尾-5がhost `C85FEBAE730077BE` / client `EC4A01D79FD12088`で当日と一致。元ログに存在する後続の共通検出点でもhashの差は0。再生では追加採取点もあるため、検出件数そのもの（25/21件）を元ログの件数と直接比較しない。
- 毎frame CSVを両roleで比較すると、末尾-0の城ではraw 36914 / logical 2666に1frameだけのY差があり次frameで再一致。その後raw 46471 / logical 12223からLuigiの座標・速度差が828frame継続する。最初の1frame差を恒久破綻の原因とは断定しない。
- 末尾-5の城でも先行する単発差があり、raw 64677 / logical 21111からLuigiのY差が84frame、raw 64762 / 21196からMarioのY差が54frame続く。raw 64819 / 21253からMarioのX・速度差が生じ、差のある区間が572frame続く。この記録で21480の警告より前の調査区間を絞った。続くゲーム更新境界での調査・修正結果は上記を参照。
- **成果物:** 同rootの`README.md`に再実行方法、`replay.py`に抽出・変換・起動、`analyze.py`に入力/状態/hash比較、`scan_divergence.py`に毎frameの差区間集計を保存。各runにfixture、role別script/env/実行ファイル/ROM/save、stdout、状態CSV、comparison.json、player-difference-runs.jsonがある。相手の完全ログがなくても、両roleの再現後状態を取得できた。
- **再生条件の限界:** 元WANの入力到着時刻、端末差、未記録touchまで同一ではない。試験はlocalhostの直接UDP、同一PCの2-processで、現在の設定ファイルを複製して音量0・速度制限なしにしている。診断ログは出力先を検証フォルダーに変更し、phase/watchdog/AI観測などを省き、状態CSVは毎frame採取する。相手完全ログは原因比較を深める追加材料であり、再生試験の着手を妨げる必須条件ではない。

## 対象

- ログルート: `%APPDATA%/Bigstar Insiders/logs/`
- GUI: Bigstar Insiders `0.11.6`。
- 全6件のmelonDS runtime commit: `b0250422e759f0a951aace94c1e201de6b4d1e80`。JIT有効。
- `MELONDS_NSML_TEST=1`、`MELONDS_NSML_WAIT_TIMEOUT_MS=60000`、`MELONDS_NSML_REMOTE_INPUT_TIMEOUT_FATAL=1`。
- 全6件の最終launcherイベントは `session_stopped_by_user`。これだけで正常終了とは判定しない。
- 時刻は日本時間。stdoutの同期ずれ時刻は直近の `tUnixMs` を使用しており、厳密な発火時刻ではなく検出付近の時刻。

| セッション名 | 起動〜終了操作 | role | 主な観測 |
| --- | --- | --- | --- |
| bigstar-1789485777930-46700-0 | 00:22:57〜00:37:18 | client | 第4ゲーム、城。00:36:00付近からplayerGlobal不一致58件。切断記録なし |
| bigstar-1789486648528-46700-1 | 00:37:28〜00:55:31 | client | 00:54:51以降に入力受信停滞。00:54:59切断。最終入力待ち38秒。playerGlobal不一致なし |
| bigstar-1789487761614-46700-2 | 00:56:01〜01:08:55 | client | 3勝の結果まで進行。playerGlobal不一致・切断記録なし |
| bigstar-1789488563167-46700-3 | 01:09:23〜01:11:13 | host | 01:10:42〜53にローカル停滞。01:11:00切断。最終入力待ち19秒。hang dumpあり |
| bigstar-1789488679387-46700-4 | 01:11:19〜01:15:46 | host | 01:15:13〜24にローカル停滞。01:15:33切断。最終入力待ち20秒 |
| bigstar-1789489061602-46700-5 | 01:17:41〜01:37:06 | host | 第5ゲーム、城。01:36:00付近からplayerGlobal不一致48件。01:25には1.17秒・1.81秒の入力待ちも発生し復帰 |

## フリーズ: 証拠と実装

### 01:10:42〜01:10:53（末尾-3）

- 展開した `melonds-phase-events.jsonl.gz` の94821行目は `tUnixMs=1789488642622`, `begin/enet-service`, raw frame 4581。次行の `end/enet-service` は `1789488653467`。差は **10845 ms**。
- `bridge-events.jsonl.gz` の80〜81行は `1789488642974 → 1789488653473`、**10499 ms**の空白。その後、1秒周期の処理が短い間隔で続く。
- watchdogは同じ `enet-service` のage増加を毎秒記録している。85行目で `stalled=1`, `phaseAgeMs=8181`, `dumpWritten=1`。
- この85行目のphaseは `enet-send-input` になっているが、それを停止関数と断定しない。`NsmbNetplayDiagnostics.cpp` の `RunWatchdog()` は時刻/ageを採取してからdumpを書き、後でphaseや他のatomic値を読むため、単一行の状態に時点差がある。実際に当該行の最終送受信時刻は行頭時刻より新しい。
- `melonds-hang.dmp` は存在し、minidumpのモジュール・17スレッドのcontextを読めることを確認。追加解析で名前付きEmuThreadの命令位置はOSのスリープ処理と判明した（下記）。対応するアプリのデバッグシンボルがなく、アプリ関数単位のスタック確定は未実施。dump採取中に実行が再開している可能性もあるため、停止開始時の完全なスナップショットとは扱わない。
- 復帰後も入力4569が届かず、stdout 13875行目、`1789488660422`でENet切断。最後まで `lastRecv=4568`, `lastSent=4572`。

### 01:15:13〜01:15:24（末尾-4）

- 展開したphaseログ302393行目 `1789488913962`, `begin/world-trace`, raw frame 13865から次行の `1789488924766`, `begin/game-state-trace`まで **10804 ms**。
- bridgeの周期ログは `1789488914362 → 1789488924777`で **10415 ms**空く。
- watchdogも232〜233行の間が `1789488914225 → 1789488925766`、**11541 ms**空く。`stalled=1`がないことは停止がなかった証拠にならない。
- 復帰後は入力13853待ち。stdout 43874行目 `1789488933417`でENet切断、`lastRecv=13852`, `lastSent=13856`。

### 00:54:51以降（末尾-1）

- 入力7927待ちが2034 msでいったん復帰し、直後に7931待ちへ入る。
- ローカルwatchdogとbridgeの周期記録は継続。bridgeではこちらからの再送が続く一方、相手からの受信に空白が生じる。
- stdout 204328行目 `1789487699689`でENet切断。最終記録の待ち時間は38000.834 ms。相手の実行停止と通信経路の問題を手元のログだけでは区別できない。

### 長い待ちを生む共通の実装

- `NsmbNetplaySession.cpp:317` の `PumpLocked()` は切断を処理してpeerを解除する。
- **自動復帰の確認:** 現行 `NsmbNetplayTransport.cpp:110` の `HandleDisconnected()` はPeer/ConnectingPeerを解除するだけ。`enet_host_connect()` は同ファイル60行の初期化経路にだけあり、対戦中の切断後に再接続して続きを再開する経路はない。接続を維持したままの一時的な入力遅延は、必要な入力が届けば待ちを抜けられる。GUIのロビー/部屋通知の再購読は対戦通信の復旧とは別。
- 同ファイル `WaitForRemoteInput()`（700行付近）は、入力がない場合にループし、切断だけでは即座に抜けない。790行付近の設定タイムアウトで終了する。
- 今回の期限は60秒。手元は19〜38秒でユーザーが停止した。9月19日に受領した相手の末尾-4では、切断済みのまま60秒待ってexited(70)となる実例を確認した。**切断後も画面の進行が止まったまま待つ挙動**はソースと実ログの双方で確認済み。無期限待ちとは判定しない。
- bridgeの最終 `connected` / DataChannel `open` と、melonDSのENet切断は異なる層の状態。前者だけで通信正常とは判断しない。`dropped_no_local_target=0`はローカル転送先不明のdropがない意味であり、WANのpacket lossゼロを意味しない。

## 同期ずれ: 城で発生した2件

### 00:36（末尾-0、generation 3）

- 最初のplayerGlobal不一致はstdout 150212行、logical frame **12300**、直前の時刻 `1789486560518`。受信入力は12302まで到達、`hasRemote=1`, `predicted=0`, `waitMs=0`。
- 最初の診断eventでLuigiのスター数が local 1 / remote 2。続くframe 13560、13860以降で座標差が大きくなり、スター数・死亡回数の差も残る。
- 不一致は58件。最初の検出後にplayerGlobal一致を挟み、frame 13560から再び不一致となるため、「最初から58件すべて連続」とは扱わない。
- `nextGame=4 requestedStage=4`、診断snapshot `stageID=4, stageGroup=9`。GUIのstage index 4は城。

### 01:36（末尾-5、generation 4）

- 最初のplayerGlobal不一致はstdout 211078行、logical frame **21480**、直前の時刻 `1789490160824`。受信入力21482まで到達、`hasRemote=1`, `predicted=0`, `waitMs=0`。
- frame 22380以降ではMarioの位置やスター数、さらに双方のスター数が食い違う。不一致は48件。途中にplayerGlobal一致を挟む。
- `nextGame=5 requestedStage=4`、同じ `stageID=4, stageGroup=9`。
- この同期ずれは01:25の短い入力待ちから約10分後。入力待ちが直接の発火原因だという証拠はない。

### 解釈上の注意

- `basic=0, playerGlobal=1`だけの記録を全件実同期破綻として数えていない。
- mismatch eventの `latestLocal` は受信時点の最新snapshotであり、remote hashと厳密に同一tickのfield比較とは限らない。最初の小さな位置差だけから発火点を断定しない。後続の大きな座標・得点・死亡回数の差を合わせて実同期破綻と判断した。
- remote診断packetに含まれないfieldは既定値の0を取り得る。remoteのinventory/lives等の0を、そのまま実ゲーム値として比較しない。
- 再戦handshakeの失敗、特定actor、ログI/O、JIT、環境差のいずれも現段階では根本原因として確定していない。

### 同期ずれ追加調査: 入力と採取時点の照合

- 末尾-0 / generation 3のlogical 843〜12300、末尾-5 / generation 4の843〜21480について、stdoutのsent/recvボタン値をframeごとに集計した。両方とも送信・受信とも欠番0、同じframeで異なるキー値の記録0。末尾-0のsend-gapは00:37:06 / frame 16191で、最初の不一致より後。末尾-5はsend-gapなし。
- これは手元で記録されたボタン値の整合性だけを検証したもの。相手が実際に消費した入力、touch座標、同じgame tickでの処理順序は相手の完全入力・詳細状態ログなしには確定できない。9月19日提供のstdout末尾抜粋だけでは不足する。
- mismatch eventの `latestLocal` は受信時点のsnapshotなので、末尾-5の最初のeventはraw 65047を表示している。直前のInputHealth summaryがlogical 21480 / raw 65046という対応を示すため、保存ringからraw 65046を選び直した。
- そのsnapshotでLuigiのX/Y/VX/VYおよび記録されたスター・コイン・score・死亡回数はremote sampleと一致する。MarioはY/VX/VYおよび同じ主要統計が一致する一方、Xがlocal `0x0039BF70` / remote `0x0039A770`で **0x1800（1.5px）**異なる。単一の共通時刻ずれを全playerに適用するだけでは残差を説明できない。
- 末尾-5のMarioはraw 65043でジャンプに移り、65046はジャンプ中。直前の約6秒には死亡後の再出現、raw 64854の在庫放出、Luigiのraw 64934のスター増加がある。ただしこれらが原因とは判定しない。対象の比較位置は最初の警告sampleであり、最初の座標分岐がもっと前の可能性がある。
- 末尾-0の最初のeventではMarioは再出現中で静止し、その位置・速度・記録された主要統計はremote sampleと同じ。Luigiには移動状態とスター数1対2の差がある。6秒の過去ringにremoteのplayerGlobal hashと一致するsnapshotはなく、単純な過去sampleの再利用では説明できない。ただしfuture側や未保存fieldまで含めた完全一致検査ではない。
- 全セッションのstage列は `[1,0,2,4]`, `[1,0,3,2,4]`, `[2,0,1]`, `[3]`, `[3]`, `[0,2,1,3,4]`。城は3回あり、実同期ずれは2回。もう1回はlogical 7931で通信停止しており、「城なら必ずずれる」「城以外なら長時間安全」とは結論しない。
- 今日のAI観測ログは記録開始/停止区間が限られ、末尾-0ではraw 8031〜18740だけで、問題の城の区間を含まない。したがってファイルが存在することを、問題区間の全actor状態が保存済みという意味に取らない。

### 診断コイン差の注意点

- `NsmbNetplayDiagnostics.cpp:543`付近の `remoteSampleDiffs` はlocalの `VsCoinCount` にplayer 0/1のcoins合計を代入する一方、remote側は `NsmbGameStateReader.cpp:444` で読むRAM word `0x0208B37C`を使っている。
- 同じ定義の値を比較していないため、ここに出る **vsCoinCount差だけは同期ずれの証拠から除外**する。比較方式の不備はゲーム状態そのものの修正ではない。後続の大きな座標差、スター数、死亡回数の差に基づく実同期破綻の判断は維持する。
- この段階では衝突・スター取得・在庫放出そのものを原因とする根拠はなかった。続く境界計測で入力対応の不備を確定し、上記の修正を実施した。

## 追加調査: PC全体負荷・ログI/O・ダンプ

### 既存のWindows記録

- 2026-09-16 **00:15〜01:45 JST**のSystem、Application、Resource-Exhaustion-Detector/Resolver、Windows Defender、Storage-ClassPnP、StorageVolumeを照会した。
- 停止付近にディスク障害、メモリ枯渇、該当アプリのクラッシュ・ハングを裏付けるイベントは見つからない。Resource-Exhaustionおよび照会したStorageログはこの時間範囲で0件。
- Defenderは01:07:53の正常性報告2件だけで、今回の時間帯のスキャン開始・終了イベントはない。これはリアルタイム検査による個別I/O遅延を否定する証拠ではない。
- Systemに01:42:57のTcpip 4231（TCP動的ポート割当失敗）があるが、対象停止後であり、今回のUDP/ENet経路の停止原因と結び付ける証拠はない。
- WERのReportArchive/ReportQueueにも当日のBigstar/melonDSの該当reportは見つからない。
- `logman query`にData Collector Setなし、`wpr -status`は記録していない状態、`C:/PerfLogs`に性能記録ファイルなし。現在の状態だけで過去に一切計測されていなかったと証明はできないが、今回の時間帯のCPU全体使用率、ディスク応答時間、スケジューラ待ちを復元できる既存記録は見つけられていない。
- Windowsの通常イベントログは継続的なCPU/I/O性能サンプルではない。「異常イベントがない」から「負荷・I/O遅延がなかった」へは一般化しない。

### 保存済み性能ログ

- `melonds-performance.jsonl`には **previous_log_write_ms** が既にある。ただし測定対象は性能ログ自身の前回書き込みであり、stdout・phaseログ・bridgeログの全I/Oではない。
- 全6件のsummary計2051件を集計し、同fieldの最大は **0.405 ms**。末尾-3の最大0.166 ms、末尾-4の最大0.386 ms。
- 停止直前の最後のsummaryは、末尾-3が01:10:42.272 / 60fps / 前回書き込み0.033 ms、末尾-4が01:15:12.892 / 60fps / 0.031 ms。run_frame平均は約2.86 / 2.95 ms。
- 従って停止直前まで継続的にエミュレーション速度が落ちていた、または性能ログ書き込みが常時遅かった証拠はない。停止を含む次のsummaryが残っていないので、瞬間的な10秒のI/O待ち・実行待ちは除外できない。
- 01:10の停止中もwatchdogは毎秒動いている。少なくともその区間を「PCの全スレッドが10秒まったく実行されなかった」とする説明には合わない。CPU負荷が高く一部のスレッドだけ遅れた可能性まで否定するものではない。

### ログが停止を伝播させ得る実装

- `NsmbTraceOutput.cpp`の一部stdout出力は上限4MiBの非同期キューだが、全出力が非同期化されているわけではない。
- `NsmbNetplayDiagnostics.cpp:1851`の `TracePhase()` はゲーム側からLogMutexを取得し、`WritePhaseEvent()`がファイルへ書いて毎行 `flush()`する。watchdogも同じLogMutexを取る。ファイル書き込みが詰まればゲーム側とwatchdogの両方へ波及し得る。
- 展開phaseログは末尾-3が119503行/107.623秒（約1110行/秒、25.34MB）、末尾-4が329689行/264.056秒（約1249行/秒、71.29MB）。これはストリーム書き込み頻度であり、同回数の物理ディスク永続化やディスク帯域飽和を実測したという意味ではない。
- `webrtc.rs:170`の `append_bridge_event()` は通信側の経路でディレクトリ作成確認・ファイルopen/append・writeを同期実行する。bridge側にもI/O待ちが通信処理へ波及する経路がある。
- `processes.rs:982`以降のcapture threadも、stdout/stderr pipeから読んだ後にファイルへ同期書き込みする。読み手が止まるとpipeを経由して書き手へ待ちが伝わり得る。
- 以上は**原因になり得る構造の確認**。実際の停止時のWriteFile開始/完了時間や待ちスタックがないため、今回の根本原因としての確定証拠ではない。

### ダンプ追加解析

- minidumpのThreadNames streamからthread 55764を **EmuThread** と識別した。
- 同threadの保存された命令位置は `ntdll.dll+0x161444`。ローカルOSモジュールのexportに照合すると `ZwDelayExecution+0x14`。stack領域にも `SleepEx` 呼出しに対応するアドレスがある。stack領域のアドレス候補抽出であり、完全なunwind済みcall stackではない。
- 少なくとも採取時点のEmuThreadは `WriteFile` で止まっている状態ではなく、sleep中。復帰後の入力待ちループ（1ms sleep）と整合するが、アプリ側の正確な呼出し元は未確定。
- 配布melonDS.exeにはdebug directoryがなく、`nm`もシンボルなし。ダンプは停止を8秒検出した後に生成され、その間にphaseが進んでいるため、今回の停止原因を直接示すダンプとは評価しない。

### 次回の判定に必要なもの

- WPRにはCPU、DiskIO、FileIO、MinifilterのプロファイルがこのPCで利用可能。再発前から記録し、CPU実行待ち、ファイル操作の開始/完了、待ちスタックを同時に取る。過去の空白を現在の性能測定で埋め合わせない。
- アプリ側に計測を加える場合は、各ログ経路の書き込み時間とmutex待ち時間を別々に測り、遅延情報を同じ詰まったログへ即書く設計は避ける。
- 本追加調査では監視の常駐・性能トレースの開始・設定変更・実装変更は行っていない。

## 残る確認・別件

1. 更新版同士の実WAN対戦を確認する。ローカル再現2件の原因特定・修正・回帰検証は完了している。
2. 01:10/01:15の手元停滞のCPU/I/O原因と、00:54:51の通信停滞箇所は未特定。再発時の性能トレースで切り分ける。
3. 切断を受けた際の入力待ち中断・UI通知・自動再接続は別件の改善候補。今回の同期ずれ修正には含めない。
