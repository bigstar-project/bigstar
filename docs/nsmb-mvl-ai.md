# NSMB Mario vs Luigi AI

## Current Goal - 2026-06-07

1人用MvLのCPU相手を、ルールベースAIだけで終わらせず、次の混合方式で育てる。

1. ルールベースAIで最低限ゲームを成立させる。
2. 人間プレイとルールベースAIのプレイログを集める。
3. 入力ラベル付きの状態ログから模倣学習する。
4. 模倣学習済みAIを初期値にして自己対戦で強化する。
5. 実プレイ向けに反応遅延、ミス率、探索制限で強さ調整する。

## Stage Scope

当面のAI実装・ログ検証・RuleAI調整は、stage 0（草原ステージ）だけを対象にする。tileProbeのoffset、solidish mask、穴/壁判定、ブロック/アイテム箱状態の検証もまずstage 0に固定し、他ステージ固有の地形やギミックは後回しにする。

## State Logging Requirements

最初の必須条件は、人間が画面を見て判断できる状態に近い情報をメモリから取得できること。学習前に、少なくとも次をフレーム単位で保存する。

- 入力ラベル: player別 held/pressed keys、console別 held/pressed keys、touch。
- プレイヤー: actor found、座標、前フレーム座標、速度、状態/アクション/物理/衝突/環境フラグ、死亡/遷移、powerup、所持/表示スター、Battle Stars、コイン、スコア。
- 目標物: Big Star actor、Big Star candidate、落下スター/アイテム、8コイン由来アイテム。
- ワールド: stage、vs mode、local player、カメラ矩形/ターゲット/位置、総コイン数。
- オブジェクト: active objectの object ID、settings、GUID/base、state、flags、lifecycle、skip flags、座標、速度。既知IDはカテゴリ名も付ける。
- 学習補助: frame、instance、role、inGameplay、hash、object counts。

## Implementation Status

- 完了: ルールベースAI本体を `src/frontend/qt_sdl/NsmbRuleAI.cpp` / `.h` に分離した。
- 完了: player actor、Big Star actor/candidate、Battle Starsを使ってremote CPU入力を生成できる。
- 完了: 左右ラップ幅 `MELONDS_NSML_RULE_AI_WRAP_WIDTH=0x400000` を考慮して目標へ向かう。
- 完了: `MELONDS_NSML_AI_PLAY_LOG=<path>` で `JSONL` のAIプレイログを出せるようにした。既存の巨大CSV game-state traceとは別に、学習入力として読みやすい1行1フレーム形式にする。
- 完了: `scripts/nsmb_mvl_ai_build_dataset.py` でJSONLから模倣学習用の固定列CSVを生成できるようにした。
- 完了: `scripts/nsmb_mvl_ai_train_imitation.py` で固定列CSVからnumpyのみの多ラベル模倣学習モデルを学習し、`.npz` に保存できるようにした。
- 完了: AI play logに `visualSummary` を追加し、カテゴリ別active object数、カメラX範囲内のobject数、player別の最近傍カテゴリ距離、objectごとのplayer相対座標とscreen Xを保存するようにした。
- 完了: `scripts/nsmb_mvl_ai_inspect_playlog.py` でJSONLを人間が読むための短い表へ変換できるようにした。
- 完了: `scripts/nsmb_mvl_ai_catalog_objects.py` でJSONL内のactive objectを object ID/settings/category ごとに集計できるようにした。
- 完了: playerの `collisionFlag` / `environmentFlag` を名前付き `contact` 状態へ展開し、接地、予測接地、天井、左右壁、水/液体/水没、流砂、ロープ/ポール、スパイク、コンベア、雪/砂/破壊地形、左右ラップをAI play logとCSV特徴量に保存するようにした。
- 完了: playerごとの `screen.camera0/1` と `fallRisk` をAI play logへ追加した。画面X/Y、カメラ内判定、カメラ底までの距離、下端近接、カメラ下抜け、Y速度符号を保存し、穴/落下判断の前段特徴としてCSVへ展開する。
- 完了: `screen.camera0/1` に `inViewY` を追加し、player/opponent/objectの画面内判定をX/Y/完全判定でログに出せるようにした。
- 完了: `scripts/nsmb_mvl_ai_render_playlog_svg.py` でJSONLの1フレームをplayer中心のSVGに描画できるようにした。表だけでなく、星、hazard、item、coin、敵、platform、unknown objectの相対配置を目視できる。
- 完了: SVGレンダラで `players[].tileProbe.samples` を小さな四角として描画するようにした。タイルサンプルの位置、tile id、behavior、solidish/coin/block/harmful分類をSVG上で目視確認できる。
- 完了: AI play logの `objects[]` に `offset` と `vtable` を追加した。object ID/settingsだけで意味が分からないactorも、vtableを手がかりに後から分類できる。
- 完了: `scripts/nsmb_mvl_ai_build_dataset.py` に `--label-source auto|applied|player|console` を追加した。ルールAIログは `appliedPlayerN`、人間プレイログはメモリ上の `playerN` / `consoleN` 入力を教師ラベルにできる。
- 完了: `scripts/nsmb_mvl_ai_predict_imitation.py` で学習済み `.npz` とdataset CSVからオフライン推論し、予測held入力、button別確率、ラベルとの一致率をCSV出力できるようにした。
- 完了: player actor内の `CollisionMgr` を読み、AI play logの `players[].collisionMgr` に collision result、ground collision、modifier tile、attached tile、raw state byteを保存するようにした。逆アセンブルで `CollisionMgr +0x7C` がcollision result、`+0x98/+0x9C/+0xA0` がbottom/top/side modifier tile typeであることを確認し、以前の暫定 `bottomTileType` 読み取りは廃止した。
- 完了: player本体の `+0xBB2/+0xBB3` を `players[].tileDamage` として保存するようにした。`Player::applyTileDamage` / `Player::updateCollision` の逆アセンブルで参照を確認した。
- 完了: `StageLayout::getTileBehavior` / `getChunkID` / `readTileBehaviour` を逆アセンブルし、AI play logの `players[].tileProbe` にプレイヤー周辺/前方/左右17点のタイルサンプルを保存するようにした。各点は actor座標からStageLayout pixel座標へ変換し、chunk id、tile id、tile behavior、solid/harmful/coin/block系カテゴリ、`solidish` を出す。summaryには `wallAhead`、`holeAhead`、`wallLeft`、`holeLeft`、`wallRight`、`holeRight`、`groundBelowSolid` などを保存する。
- 完了: RuleAIの内部 `FrameState` に `GroundBelowSolid` / `WallAhead` / `HoleAhead` / `WallLeft` / `HoleLeft` / `WallRight` / `HoleRight` を追加し、tileProbeが取れている場合は横移動入力の方向に応じた穴/壁候補でジャンプ入力を強めるようにした。
- 完了: `players[].tileProbe.samples[].block` を追加し、StageLayout上の現在tile id/behaviorから question block、breakable block、brick、invisible block、item box候補、storage contents、modifierを明示的に保存するようにした。CSV特徴量とinspect/SVG表示にも流す。
- 完了: stage 0実走で `feet/below/ahead*/left*/right*` の足元系offsetを少し下げ、RuleAI実操作経路のterrain traceを確認した。現時点ではStageLayout raw probeが地上接触中の `tileId=0x001 behavior=0x0000002A` をsolidish扱いできていないため、RuleAI側では接地contactがあるフレームの偽hole判定を抑える暫定フォールバックを入れた。
- 完了: AI play logの `tileProbe.summary` に `contactGround`、`effectiveGroundBelowSolid`、`holeSuppressedByContact`、`effectiveHoleAhead/Left/Right` を追加した。raw tileProbeの `hole*` は残しつつ、接地contactを融合した実操作向け/学習向けの地形判断もCSV特徴量に入る。
- 完了: `tileProbe.samples[]` に `status` と未取得時のworld/pixel/chunk情報を保存し、`tile.lowType` を追加した。`tileId=0x001 behavior=0x0000002A` のような低位tile typeをCSVから直接集計できる。
- 完了: `scripts/nsmb_mvl_ai_catalog_tiles.py` を追加し、AI play logのtileProbeを sample/status/tile id/behavior/lowType/solidish/block/contact/effective ground別に集計できるようにした。
- 完了: 人間プレイログ収集に向けて、AI play logを記録単位で扱う `recording.json` / `recordings-index.json` のmanifest形式を追加した。`scripts/nsmb_mvl_ai_create_recording_manifest.py` で1ログの統計、最終状態、イベント候補、label sourceを保存し、`scripts/nsmb_mvl_ai_make_recordings_index.py` で複数記録を束ねる。
- 完了: `scripts/nsmb_mvl_ai_build_dataset.py` が JSONL単体だけでなく、`recording.json` と `recordings-index.json` を入力にできるようにした。CSVには `recording_index` / `recording_frame_index` を保存し、`scripts/nsmb_mvl_ai_train_imitation.py --split-by-recording` で記録単位のvalidation holdoutを選べる。
- 完了: `scripts/nsmb_mvl_ai_verify_replay.py` を追加し、melonDSで再実行したreplayログを、最終frame/hash/player座標/powerup/dead/star/coin/object countで検証できるようにした。デフォルトは完全一致で、必要時だけ位置許容やhash/object count無視を指定する。
- 完了: `scripts/nsmb_mvl_ai_export_viewer_data.py` を追加し、JSONLを外部ビューア向けのコンパクトJSONへ変換できるようにした。
- 完了: `scripts/run-nsmb-mvl-manual-local.ps1` に host/client別の `HostAIPlayLog` / `ClientAIPlayLog` を追加し、手動2窓プレイでもAI play logを別々に出せるようにした。
- 完了: `scripts/run-nsmb-mvl-human-recording.ps1` を追加した。stage 0固定で手動ローカル記録を開始し、終了後にmanifest/index/datasetを作る後処理コマンドを `recording-session.json` に残す。
- 完了: `recording.json` に `replay` / `metadata` / `quality` を追加し、入力script replayに必要なframes、host/client input script、ROM、match seed、scenario、品質ステータスを保存できるようにした。
- 完了: `scripts/run-nsmb-mvl-split-local-input-smoke.ps1` が host/client別AI play log出力先を受け取り、split local input replay中にもAI play logを保存できるようにした。
- 完了: `scripts/run-nsmb-mvl-recording-replay.ps1` を追加した。`recording.json` からstage 0 input script replayの起動計画を復元し、完了後に `nsmb_mvl_ai_verify_replay.py` で検証する。`-DryRun` で起動前の解決パスと検証対象を確認できる。
- 完了: `scripts/nsmb_mvl_ai_verify_replay.py` に任意のcheckpoint frame比較を追加した。`--checkpoint-interval` を指定すると、最終frameに加えて途中frameのhash、player状態、object countを比較できる。
- 完了: `recording.json` の `summary.eventSamples` に、star/coin/powerup/death/block/item/projectile候補の代表frameを保存できるようにした。件数だけでなく、目視確認すべきframeをmanifestから辿れる。
- 完了: Tauri GUIに `AIログ` タブを追加した。ローカルのJSONLまたはviewer JSONをファイル選択で読み込み、player中心のSVG相当相対配置、P0/P1入力、可視object数、イベント候補、カテゴリ数を確認できる。
- 完了: Tauri GUIの `AIログ` タブが `recording.json` の `summary.eventSamples` を読み込み、frame付きの記録イベントtimelineを表示できるようにした。manifest単体でも、死亡、block候補、item/projectile候補などの目視確認対象frameを確認できる。
- 完了: `scripts/run-nsmb-mvl-human-recording.ps1` が、通常の入力/AI play logに加えてpacket captureを有効化し、終了後に `packet-replay.csv` へ変換する後処理コマンドを `recording-session.json` に残すようにした。不要な場合は `-NoPacketCapture` で無効化できる。
- 完了: `recording.json` が `packetReplayFile` / `hostPacketReplayFile` / `clientPacketReplayFile` と `packetCapture.host/client` を保存し、`replay.mode=packet_replay` を扱えるようになった。
- 完了: `scripts/run-nsmb-mvl-recording-replay.ps1` が `input_script` と `packet_replay` の両方を解決し、packet replay時は `run-nsmb-mvl-split-local-input-smoke.ps1` へ host/client のpacket replay fileを渡すようにした。現時点の確認はdry-runで、実際の人間記録からの完全再現は次の検証対象。
- 完了: `scripts/nsmb_mvl_ai_verify_replay.py --scan-frames` で全記録frameを比較し、最初にズレたframeを検出できるようにした。`--mismatch-report-json` / `--mismatch-report-csv` を指定すると、viewer/表確認向けの差分reportを保存する。
- 完了: `scripts/run-nsmb-mvl-recording-replay.ps1 -ScanFrames` が replay検証時に `replay-mismatch.json` / `replay-mismatch.csv` を出力する起動計画を作れるようになった。
- 完了: `scripts/run-nsmb-mvl-recording-postcommands.ps1` を追加し、`recording-session.json` に残した後処理コマンドをdry-run/途中再開つきで実行できるようにした。
- 完了: AI play logに `specialObjects.fireballs` / `specialObjects.projectiles` を追加した。通常actor object listでは拾えない `Fireballs::activeFireballs` と fireball/projectile handler先頭wordを保存し、Fire Mario実ログで発射frameを見つける足場にする。
- 完了: `specialObjects.fireballs.active` をdataset特徴量、viewer export、recording manifestの `summary.specialObjectFrames.fireballActive` / `eventSamples.fireballActive`、GUIの `AIログ` タブへ流すようにした。
- 完了: `scripts/nsmb_mvl_ai_audit_recordings.py` を追加した。`recording.json` または `recordings-index.json` を読み、rows、gameplay rows、player found率、label率、nonzero label、stage、packet replay、必須event数を学習前に検査できる。
- 完了: `scripts/run-nsmb-mvl-human-recording.ps1 -DryRun` を追加し、melonDSを起動せずに `recording-session.json` と後処理コマンドを確認できるようにした。通常後処理には `recording-audit.json` 生成も含める。

## AI Play Log

有効化env:

- `MELONDS_NSML_AI_PLAY_LOG=<path>`: JSONL出力先。
- `MELONDS_NSML_AI_PLAY_LOG_INTERVAL=1`: ログ間隔。学習データ収集は1、smoke/目視検査は30などでよい。
- `MELONDS_NSML_AI_PLAY_LOG_START_FRAME=0`
- `MELONDS_NSML_AI_PLAY_LOG_END_FRAME=0`: 0なら終了指定なし。
- `MELONDS_NSML_AI_PLAY_LOG_MAX_OBJECTS=32`: 1フレームに保存するactive object上限。
- `MELONDS_NSML_AI_PLAY_LOG_INCLUDE_NON_GAMEPLAY=1`: 通常はMvL gameplay中だけ保存する。これを指定すると非gameplayも含める。

JSONL schema `nsmb_mvl_ai_play_log_v1` は、各行に `inputs`、`players`、`targets`、`camera`、`objectSummary`、`specialObjects`、`objects` を持つ。`objects` はactive objectだけを保存し、既知IDには `category` を付ける。各objectには `objectId`、`settings`、`guid`、`base`、`offset`、`vtable`、state/flags、座標、速度、player相対座標、screen情報を保存する。

`inputs` にはメモリ上の `console0/1`、`player0/1` に加えて、PoCが実際にそのフレーム近辺へ注入した `appliedPlayer0/1` を保存する。模倣学習の教師ラベルはまず `appliedPlayerN.held` / `heldHex` を使う。

`players[].contact` には、数値の衝突/環境フラグから人間が目視で判断する地形接触に近いbitを保存する。代表項目は `ground`、`predictGround`、`ceiling`、`wallLeft`、`wallRight`、`edgeGrab`、`water`、`liquid`、`submerged`、`quicksand`、`rope`、`tightrope`、`pole`、`spikesLeft`、`spikesRight`、`conveyorLeft`、`conveyorRight`、`wrapLeft`、`wrapRight`。CSVには `self_contact_*` / `opponent_contact_*` として展開する。

`players[].collisionMgr` には、プレイヤーactor内のCollisionMgrから読んだ地形/接触結果を保存する。主な項目は `collisionResult`、`groundCollision`、`deltaX/Y`、`attachedTileX/Y`、`bottomModifierTileType`、`topModifierTileType`、`sideModifierTileTypeLeft/Right`。`bottomModifierTile` は modifier tile typeを `solid`、`coin`、`questionBlock`、`breakableBlock`、`brickBlock`、`slope`、`water`、`partialSolid`、`harmful`、`invisibleBlock`、modifier、storage contentsへ展開したもの。これは「足元そのもの」ではなく、CollisionMgrが保持するbottom modifier tileで、前方タイルや穴の完全判定ではない。CSV/inspectでは不自然に多数のtile category bitが立つ値をsanity checkで除外する。

`players[].tileDamage` には、Player本体に保存されるtile damage flags/typeを保存する。`active=1` のときはlava/poison/その他ダメージ地形などの接触候補として扱う。通常の床接地や壁接触は `contact` と `collisionMgr.collisionResult` を見る。

`players[].tileProbe` には、StageLayoutから直接読んだタイル地形サンプルを保存する。サンプル点は `center`、`feet`、`below`、`aheadBody`、`aheadFeet`、`aheadBelow`、`ahead2Feet`、`ahead2Below`、`above`、`leftBody`、`leftFeet`、`leftBelow`、`left2Below`、`rightBody`、`rightFeet`、`rightBelow`、`right2Below`。各点は `status`、`pixelX/Y`、`chunkId`、`tileId`、`behavior`、`tile`、`block`、`solidish` を持つ。`status=0` は取得成功、それ以外はStageLayout/chunk/behavior tableなどの未取得理由を示す。未取得時もworld/pixel/chunk候補は残す。`tile` は `solid`、`scanSolid`、`coin`、`questionBlock`、`breakableBlock`、`brickBlock`、`slope`、`water`、`partialSolid`、`harmful`、`lowType` などへ展開する。`block` は現在のStageLayout tileから `any`、`itemBox`、`question`、`breakable`、`brick`、`invisible`、`storageContents`、`modifier`、`currentTileId`、`currentBehavior` を保存する。`storageContents` / `itemBox` はblock flagが立っているtileでのみ意味のある値として扱う。叩いた後の箱はStageLayoutがtileを差し替えた後の `currentTileId/currentBehavior` として現れる想定なので、stage 0の実ログで照合する。`summary.holeAhead` は速度方向の前方下2点、`summary.holeLeft` / `holeRight` は左右下2点がsolidishでないraw穴判定。`summary.effectiveHoleAhead/Left/Right` は接地contactがある場合に偽holeを抑えた学習/操作向け判定。`summary.wallAhead` / `wallLeft` / `wallRight` はbody/feetがsolidishの暫定壁判定。CSVには `self_tile_probe_*` / `opponent_tile_probe_*` として展開する。

`players[].screen` / `players[].fallRisk` には、playerをカメラ座標へ投影した情報を保存する。`fallRisk` は `screenY0/1`、`cameraBottomDistance0/1`、`nearCameraBottom0/1`、`belowCamera0/1`、`velYPositive/Negative` を持つ。完全な穴判定ではないが、目視上の「下へ落ちている」「画面下端に近い」を学習データに入れるための暫定特徴。

`visualSummary` には、人間が画面を見て判断する情報に近づけるための要約を保存する。現時点ではY込みの `visibleCamera0/1`、左右ラップ込みX判定の `visibleCamera0X` / `visibleCamera1X`、カテゴリ別count、player別最近傍 `big_star_actor` / `moving_hazard` などを持つ。player/object個別にも `relative` と `screen.camera*.inViewX/inViewY/inView` を保存する。

`specialObjects` には、通常のactor object list外で管理されるものを保存する。現時点では `fireballs.active`、`fireballs.handler`、`fireballs.words[]`、`projectiles.handler`、`projectiles.words[]` を持つ。`fireballs.active` は `Fireballs::activeFireballs`、handler addressはNSMB symbol table上の `Fireballs::fireballHandler` / `Projectiles::projectileHandler` に対応する。まだslot別の座標/owner/寿命までは未確定なので、まずFire Mario実ログでactive数が増えるframeを拾う用途に使う。

現時点の既知カテゴリ:

- `player`
- `big_star_actor`
- `big_star_related`
- `big_star_candidate`
- `world_item`
- `neutral_item`
- `dropped_star_item`
- `item`
- `coin`
- `moving_hazard`
- `hazard`
- `enemy_goomba`
- `enemy_koopa`
- `platform`
- `warp_entrance`
- `item_spawn_effect`
- `camera`
- `stage_scene`
- `stage_fx`
- `stage_actor_manager`
- `stage_controller`
- `stage_layout`
- `mvl_object267`
- `vs_connect`
- `course_select`
- `object`

## Planned Pipeline

- `MELONDS_NSML_AI_PLAY_LOG=<path>` でAI/人間共通の観測ログを出す。
- 人間プレイを収集する場合は、まず `pwsh scripts\run-nsmb-mvl-human-recording.ps1` を使う。これはstage 0固定でhost/client別AI play logとpacket captureを出し、終了後に `recording-session.json` の後処理コマンドで `packet-replay.csv`、`recording.json`、`recordings-index.json`、datasetを作る。packet captureが不要な検証では `-NoPacketCapture` を指定する。
- 記録終了後は `pwsh scripts\run-nsmb-mvl-recording-postcommands.ps1 -Session <recording-session.json>` で、packet replay変換、manifest/index、dataset生成をまとめて実行する。手順確認だけなら `-DryRun`、途中から再開する場合は `-StartAt <index>` を使う。
- `python scripts\nsmb_mvl_ai_audit_recordings.py <recording.json|recordings-index.json> --stage 0 --min-player-found-ratio 0.5 --min-label-ratio 0.5` で、学習前の最低限の品質を確認する。packet replay完全再現を前提にする人間記録では `--require-packet-replay`、Fire Marioなど低頻度scenarioでは `--require-event fireballActive:1` のようにevent条件を追加する。
- `python scripts\nsmb_mvl_ai_create_recording_manifest.py <playlog.jsonl> <recording.json> --kind human --player <0|1> --label-source player --stage 0` で、1本の人間/AIログを記録manifest化する。
- `python scripts\nsmb_mvl_ai_make_recordings_index.py <recordings-index.json> <recording1.json> <recording2.json> ... --stage 0` で、複数記録を1つの学習入力に束ねる。
- `python scripts\nsmb_mvl_ai_build_dataset.py <playlog.jsonl> <dataset.csv> --player 1 --require-player-found` で固定長特徴量へ変換する。デフォルトの `--label-source auto` は `appliedPlayerN` があればそれを使い、なければ `playerN` を使う。人間ログだけを明示する場合は `--label-source player` を指定する。
- `python scripts\nsmb_mvl_ai_build_dataset.py <recording.json|recordings-index.json> <dataset.csv> --player 1 --label-source player --require-player-found` でも固定長特徴量へ変換できる。
- `python scripts\nsmb_mvl_ai_train_imitation.py <dataset.csv> <model.npz>` でキー入力の多ラベル分類モデルを学習する。複数記録を使う場合は `--split-by-recording` で記録単位validationにする。
- `python scripts\nsmb_mvl_ai_predict_imitation.py <model.npz> <dataset.csv> <predictions.csv>` で学習済みモデルのオフライン推論結果を確認する。
- `python scripts\nsmb_mvl_ai_verify_replay.py <expected recording.json|playlog.jsonl> <actual playlog.jsonl>` で、melonDS replayが完全再現できているかを最終状態で検証する。
- `pwsh scripts\run-nsmb-mvl-recording-replay.ps1 -RecordingManifest <recording.json>` で、manifest内の `replay.mode` に従ってinput script replayまたはpacket replayを起動し、host/client別AI play logを取り直して検証する。起動計画だけ確認する場合は `-DryRun` を使う。最初のズレを調べる場合は `-ScanFrames`、間引き確認は `-CheckpointInterval 30 -CheckpointStartFrame 900` などを指定する。
- `python scripts\nsmb_mvl_ai_export_viewer_data.py <playlog.jsonl> <viewer-data.json>` で、GUI/外部ビューア向けJSONを作る。GUIの `AIログ` タブはJSONLを直接読むこともできる。
- `python scripts\nsmb_mvl_ai_inspect_playlog.py <playlog.jsonl> --player 1` で、frame、入力、接地/壁/水などのcontact、player/相手/星/hazardの相対位置、可視X数、カテゴリ数を目視確認する。
- `python scripts\nsmb_mvl_ai_render_playlog_svg.py <playlog.jsonl> <frame.svg> --player 1 --frame <frame>` で、player中心の相対配置とtileProbeサンプル点をSVGとして目視確認する。
- `python scripts\nsmb_mvl_ai_catalog_objects.py <playlog.jsonl>` で、未知objectの出現頻度と代表的な相対位置を確認し、カテゴリ付けを増やす。
- `python scripts\nsmb_mvl_ai_catalog_tiles.py <playlog.jsonl> --player 1` で、StageLayout tileProbeのtile id / behavior / lowType / status / contact ground / effective groundを集計し、床/穴/箱/未取得tileの候補を調べる。
- その後、同じ観測schemaを使って自己対戦学習へ進む。
- 強さ調整は、推論時に入力反応遅延、ランダムミス、action hold制限、近傍探索幅制限を入れる。

## Imitation Learning Readiness Plan

模倣学習へ進む前の基準は、「人間がstage 0の画面を見て入力判断に使う情報」と「その時点の入力ラベル」が、同じframe番号で再現・目視検証・dataset化できること。学習モデルの高度化より先に、観測、replay、データ品質、推論戻し経路を固める。

1. 状態取得を目視同等へ近づける。
   - まずstage 0固定で、player、相手、Big Star、coin、item、enemy、hazard、platform、block/item box、camera、地形接触、落下危険、入力を1フレーム単位で揃える。
   - item boxは「叩く前の中身候補」「叩いた瞬間」「使用済みtileへの変化」「spawnしたitem/effect」を同じtimeline上で追えるようにする。
   - Fire Marioのfireball、敵/ステージ由来projectile、短命effectは object ID/vtable/settings/owner候補/速度/寿命をログへ出し、`projectile` / `player_fireball` / `enemy_projectile` のカテゴリへ分ける。
   - powerup変化、ダメージ、死亡、スター取得、コイン取得、item取得、ブロック破壊、敵撃破、落下復帰などをevent候補としてmanifest/viewerへ出す。
   - `tileId=0x001 behavior=0x0000002A lowType=42` や `tileId=0x110-0x113` の未解釈tileをstage 0実ログと逆アセンブルで潰し、raw `solidish` / `hole*` を接地contact補正なしでもなるべく正しくする。

2. 人間プレイ記録を完全再現できるreplay基盤にする。
   - 記録manifestには、ROM/build識別子、stage、seed、host/client role、local player、入力scriptまたはpacket replay、AI play log、viewer data、dataset、検証結果を保存する。
   - `recording.json` からmelonDSをreplay起動し、終了後に `nsmb_mvl_ai_verify_replay.py` まで自動実行する。専用ラッパーはinput script replayとpacket replayの起動計画を扱えるようになったので、次は実際の人間記録で完全一致を確認する。
   - 検証は最終frameだけでなく、checkpoint frameのhash、player座標/powerup/dead/star/coin、object category count、event列を比較できるように拡張する。
   - replayが完全一致しない場合でも、どのframeからズレたかをviewerとCSVで追えるようにする。現時点では `--scan-frames` と mismatch JSON/CSV reportで最初のズレframe、player状態、hash、object/category count差分を保存できる。

3. 外部ビューア/GUIでデータ品質を人間が確認できるようにする。
   - 現在のSVG相当表示に加えて、timeline、入力列、event列、object category filter、unknown object一覧、tileProbe/block状態、replay差分を表示する。
   - 「このフレームの人間入力がなぜ妥当か」を確認できるように、player中心相対配置、カメラ内可視状態、足元/前方地形、item/projectileの寿命変化を同時に見る。
   - 目視検査済み/破棄/要再分類などの品質タグをmanifestまたはindexへ残す。

4. dataset作成を人間ログ向けに強くする。
   - 低頻度イベントが消えないよう、通常走行、スター争奪、item取得、Fire Mario、敵/穴/ブロック接触、死亡前後をscenarioタグで分けて収集する。
   - `--split-by-recording` を標準にし、同じ連続プレイの隣接frameがtrain/validationに混ざらないようにする。
   - button別accuracyだけでなく、held完全一致、方向入力、jump/fire同時押し、イベント周辺window、危険回避windowの評価を出す。
   - 人間ログ、RuleAIログ、将来の自己対戦ログを混ぜられるよう、source/kind/player/role/stage/scenario/qualityを特徴量ではなくmetadataとして保持する。

5. 学習モデルをゲーム入力へ戻して閉ループ評価する。
   - まず既存 `.npz` の簡易モデルをPoCまたは外部sidecarから推論し、remote CPU入力へ戻す。
   - 推論時は直近数frameの状態、前回入力、action hold、反応遅延、ランダムミス率、無効入力抑制を入れる。
   - オフライン一致率だけでなく、stage 0で一定時間生存、Big Starへ近づく、落下しない、相手と戦う、itemを使う、というゲーム内指標で評価する。

6. その後に自己対戦へ進む。
   - 模倣学習済みモデルを初期値にし、RuleAIを相手/補助データ生成器として残す。
   - 報酬は勝敗だけにせず、スター差、死亡、相手/星への距離、item取得、危険回避などstage 0で観測済みのイベントを使う。
   - 学習が破綻した場合に人間ログへ戻せるよう、自己対戦ログも同じmanifest/viewer/dataset形式で保存する。

## Current Blockers / Unknowns

- object IDと画面上の意味の対応はまだ完全ではない。coin/item/enemy/platform/hazard の初期カテゴリは入り、block/item box候補はStageLayout tileProbeから取れるようになったが、stage 0の実ログを見ながらステージ固有ギミックの分類を増やす。
- 目視同等にするには、stage 0の実プレイ場面でタイルサンプルを増やし、穴/壁/床判定のoffsetとsolid maskを調整する必要がある。左右ラップ込みX判定、Y込みの完全可視判定、player接触地形、CollisionMgr接触結果、modifier tile、tile damage、playerの画面Y/カメラ底距離、StageLayout由来の前方/左右タイルサンプルは取れるようになった。
- StageLayout raw probeでは、stage 0の接地中に `tileId=0x001 behavior=0x0000002A lowType=42` が出る場面があり、現行solidish maskでは床として分類できない。RuleAIとCSVのeffective判定は接地contactで偽holeを抑えるが、学習用の目視同等ログとしては低位tile typeの意味を追加で詰める必要がある。
- ブロック/アイテム箱の「中身」はblock flagつきStageLayout tile behaviorのstorage contentsとして保存するようになった。叩いた後の状態は現在tile id/behaviorの変化として取れる想定だが、stage 0の実ログで `StageLayout::changeTile` / question block animation path と照合して詰める。
- 自己対戦に進む前に、ログschemaを実プレイログで増強し、学習済みモデルを入力へ戻す推論経路を作る必要がある。
- fireballなどのprojectile系objectは、通常actor object listではなく専用handlerで管理されるものがある。`specialObjects.fireballs.active` とhandler先頭wordは取れるようになったが、slot別の座標/owner/寿命はまだ未確定。Fire Marioでstage 0実ログを取り、handler内のslot構造を詰める必要がある。
- `run-nsmb-mvl-recording-replay.ps1` はinput script replayとpacket replayの起動計画を扱え、`-ScanFrames` で不一致reportも残せる。ただし実際の人間記録で完全一致することは、新規記録を取って `recording-session.json` のpacket変換後処理を実行してから確認する必要がある。現時点ではpacket replayのdry-run解決までで、melonDS実走の完全再現検証は未完了。

## Verification

- `cmake --build build\release-windows-x86_64 --config Release --target melonDS -j 4` pass。
- `logs/codex-ai-playlog-label-smoke-20260607`: rule AI remote + PacketBridge JIT helper + `MELONDS_NSML_AI_PLAY_LOG` で1600F pass。`ai-playlog.jsonl` は27行、frame 810-1590、player actorあり25行、`appliedPlayer1` valid 24行。
- 同ログでカテゴリ `big_star_actor`、`camera`、`moving_hazard`、`player`、`stage_actor_manager`、`stage_controller`、`stage_scene` を確認。frame 900 では `appliedPlayer1.heldHex=0x810` が出ており、remote CPUの入力ラベルが保存されている。
- `python scripts\nsmb_mvl_ai_build_dataset.py logs\codex-ai-playlog-label-smoke-20260607\ai-playlog.jsonl logs\codex-ai-playlog-label-smoke-20260607\ai-dataset-player1.csv --player 1 --require-player-found` pass。24行のCSVが生成され、`label_held`、button別 `label_*`、self/opponent/target/camera/object/nearestカテゴリ特徴を確認。
- `python scripts\nsmb_mvl_ai_train_imitation.py logs\codex-ai-playlog-label-smoke-20260607\ai-dataset-player1.csv logs\codex-ai-playlog-label-smoke-20260607\ai-imitation-player1.npz --epochs 200 --lr 0.05` pass。24行の小データで学習と `.npz` 保存が動作することを確認。これはパイプライン検証であり、強さ評価ではない。
- `logs/codex-ai-visual-wrapx-smoke-20260607`: visualSummary追加後のrule AI remote smoke 1600F pass。JSONL 27行、`visualSummary` 全行あり。frame 870で `visibleCamera0X=10`、`visibleCamera1X=11`、player objectの `screen.camera0.inViewX=1` を確認。
- 同ログから `python scripts\nsmb_mvl_ai_build_dataset.py ... --player 1 --require-player-found` pass、24行CSV生成。`visible_camera*_x` と `count_*` 特徴が追加された状態で `python scripts\nsmb_mvl_ai_train_imitation.py ... --epochs 200 --lr 0.05` pass。
- `python scripts\nsmb_mvl_ai_inspect_playlog.py logs\codex-ai-visual-wrapx-smoke-20260607\ai-playlog.jsonl --player 1 --limit 8` pass。frame 900で入力 `LY`、player1座標、Big Star相対 `696,104`、moving hazard相対 `127,-8`、カテゴリ数を表で確認。
- `logs/codex-ai-object-catalog-smoke-20260607`: object catalog追加後のrule AI remote smoke 1600F pass。`stage_fx`、`mvl_object267`、`vs_connect` のカテゴリ化を確認。未知のまま残る代表IDは当時 `0x021`、`0x145`。
- `logs/codex-ai-contact-smoke-20260607`: contact追加後のrule AI remote smoke 1600F pass。`python scripts\nsmb_mvl_ai_inspect_playlog.py ... --player 1 --limit 10` pass。frame 990でcontact `G`、frame 1020以降で `G+WR` を確認し、接地/壁接触が人間可読の表に出ることを確認。
- 同ログから `python scripts\nsmb_mvl_ai_build_dataset.py ... --player 1 --require-player-found` pass、24行CSV生成。`self_contact_*` / `opponent_contact_*` は各33列、`count_coin`、`count_hazard`、`count_enemy_goomba`、`count_platform`、`count_warp_entrance` の列追加を確認。
- `python scripts\nsmb_mvl_ai_train_imitation.py logs\codex-ai-contact-smoke-20260607\ai-dataset-player1.csv logs\codex-ai-contact-smoke-20260607\ai-imitation-player1.npz --epochs 200 --lr 0.05` pass。24行の小データで学習と `.npz` 保存が動作することを再確認。
- `logs/codex-ai-screenfall-smoke-20260607`: player screen/fallRisk追加後のrule AI remote smoke 1600F pass。`python scripts\nsmb_mvl_ai_inspect_playlog.py ... --player 1 --limit 10` pass。`y/bot` 列で screenY と camera bottom distance を確認し、frame 990 以降で `vy-` / `vy+` と接地状態が表に出ることを確認。
- 同ログから `python scripts\nsmb_mvl_ai_build_dataset.py ... --player 1 --require-player-found` pass、24行CSV生成。`self_screen0_x`、`self_screen0_y`、`self_camera_bottom_distance0`、`self_near_camera_bottom0`、`self_below_camera0`、`self_vel_y_positive/negative` の列追加を確認。
- `python scripts\nsmb_mvl_ai_train_imitation.py logs\codex-ai-screenfall-smoke-20260607\ai-dataset-player1.csv logs\codex-ai-screenfall-smoke-20260607\ai-imitation-player1.npz --epochs 200 --lr 0.05` pass。24行の小データで学習と `.npz` 保存が動作することを再確認。
- `logs/codex-ai-svg-smoke-20260607`: `item_spawn_effect` 分類とSVGレンダラ追加後のrule AI remote smoke 1600F pass。catalogで `0x0F0 settings=0x0109002F` が `item_spawn_effect` になることを確認。
- `python scripts\nsmb_mvl_ai_render_playlog_svg.py logs\codex-ai-svg-smoke-20260607\ai-playlog.jsonl logs\codex-ai-svg-smoke-20260607\frame-1020-player1.svg --player 1 --frame 1020` pass。`frame-1020-player1.svg` を生成し、player中心の相対配置を目視確認できる成果物が作れることを確認。
- 同ログから `python scripts\nsmb_mvl_ai_build_dataset.py ... --player 1 --require-player-found` pass、24行CSV生成。`python scripts\nsmb_mvl_ai_train_imitation.py ... --epochs 200 --lr 0.05` pass。
- `logs/codex-ai-vtable-smoke-20260607`: object `offset` / `vtable` 追加後のrule AI remote smoke 1600F pass。catalogで `sampleVTable` が出力され、unknown objectにもvtableが残ることを確認。`0x021` と `0x022` が同じ `0x021331E8` を持つことを確認。
- `logs/codex-ai-vtable-category-smoke-20260607`: `0x021` を `big_star_related` に分類後のrule AI remote smoke 1600F pass。catalogで `0x021 settings=0x00000000 big_star_related sampleVTable=0x021331E8` を確認。inspect表、SVG生成、dataset生成、最小imitation trainまでpass。
- 同ログで `python scripts\nsmb_mvl_ai_build_dataset.py ... --label-source auto|applied|player` を確認。`applied` は24行、`auto` と `player` は25行を生成。人間ログ相当の `playerN` ラベルでもCSV化できることを確認。`python -m py_compile` でAI関連Pythonスクリプト5本の構文確認pass。`auto` CSVから最小imitation train pass。
- `python scripts\nsmb_mvl_ai_predict_imitation.py logs\codex-ai-vtable-category-smoke-20260607\ai-imitation-player1-auto.npz logs\codex-ai-vtable-category-smoke-20260607\ai-dataset-player1-auto.csv logs\codex-ai-vtable-category-smoke-20260607\ai-predictions-player1-auto.csv --limit 10` pass。10行サンプルで `button_acc=0.975`、`exact=0.800`、予測CSV生成を確認。
- `logs/codex-ai-collisionmgr-smoke-20260607`: CollisionMgr由来の足元地形候補追加後に `cmake --build build\release-windows-x86_64 --config Release --target melonDS -j 4` pass。rule AI remote smoke 1600F pass。
- 同ログで `python scripts\nsmb_mvl_ai_inspect_playlog.py ... --player 1 --limit 10` pass。`terrain` 列に `D3`、`M2+D16`、sanity check外の `rawFFFFDA80` が表示され、疑わしいbottom tile値を誤ってtile flag展開しないことを確認。
- 同ログから `python scripts\nsmb_mvl_ai_build_dataset.py ... --player 1 --require-player-found --label-source auto` pass。25行CSV生成。`self_collision_mgr_bottom_tile_sane` は `0/1` 両方を含み、sanity check外のbottom tile category列は0へ落とすことを確認。
- `python -m py_compile scripts\nsmb_mvl_ai_build_dataset.py scripts\nsmb_mvl_ai_inspect_playlog.py scripts\nsmb_mvl_ai_predict_imitation.py` pass。
- `python scripts\nsmb_mvl_ai_train_imitation.py logs\codex-ai-collisionmgr-smoke-20260607\ai-dataset-player1-auto-sane.csv logs\codex-ai-collisionmgr-smoke-20260607\ai-imitation-player1-auto-sane.npz --epochs 200 --lr 0.05` pass。`python scripts\nsmb_mvl_ai_predict_imitation.py logs\codex-ai-collisionmgr-smoke-20260607\ai-imitation-player1-auto-sane.npz logs\codex-ai-collisionmgr-smoke-20260607\ai-dataset-player1-auto-sane.csv logs\codex-ai-collisionmgr-smoke-20260607\ai-predictions-player1-auto-sane.csv --limit 10` pass。10行サンプルで `button_acc=0.975`、`exact=0.800`。
- 逆アセンブル確認: `CollisionMgr::updatePlayer` / `updatePlayerVertical` / `getBottomModifierType` / `getTopModifierType` / `getSideModifierType` / `Player::applyTileDamage` / `Player::updateCollision` を `tools\nsmb_us_rom_tool.py disasm` で確認。`CollisionMgr +0x7C` collision result、`+0x80` ground collision、`+0x90/+0x92` attached tile、`+0x98/+0x9C/+0xA0` bottom/top/side modifier tile type、Player `+0xBB2/+0xBB3` tile damage flags/typeをログsourceに採用した。
- `logs/codex-ai-tiledamage-smoke-20260607`: CollisionMgr offset補正と `tileDamage` 追加後に `cmake --build build\release-windows-x86_64 --config Release --target melonDS -j 4` pass。rule AI remote smoke 1600F pass。
- 同ログで `python scripts\nsmb_mvl_ai_inspect_playlog.py ... --player 1 --limit 10` pass。`terrain` 列で `terrain0` / `C` が表示され、旧暫定offset由来の `rawFFFFDA80` は出なくなった。
- 同ログから `python scripts\nsmb_mvl_ai_build_dataset.py ... --player 1 --require-player-found --label-source auto` pass。25行CSV生成。`self_collision_mgr_ground_collision`、`self_collision_mgr_bottom_modifier_tile_type`、`self_tile_damage_flags/type/active`、`self_bottom_modifier_tile_*` の列追加を確認。
- `python scripts\nsmb_mvl_ai_train_imitation.py logs\codex-ai-tiledamage-smoke-20260607\ai-dataset-player1-auto.csv logs\codex-ai-tiledamage-smoke-20260607\ai-imitation-player1-auto.npz --epochs 200 --lr 0.05` pass。`python scripts\nsmb_mvl_ai_predict_imitation.py logs\codex-ai-tiledamage-smoke-20260607\ai-imitation-player1-auto.npz logs\codex-ai-tiledamage-smoke-20260607\ai-dataset-player1-auto.csv logs\codex-ai-tiledamage-smoke-20260607\ai-predictions-player1-auto.csv --limit 10` pass。10行サンプルで `button_acc=0.958`、`exact=0.700`。
- `python scripts\nsmb_mvl_ai_render_playlog_svg.py logs\codex-ai-tiledamage-smoke-20260607\ai-playlog.jsonl logs\codex-ai-tiledamage-smoke-20260607\frame-1050-player1.svg --player 1 --frame 1050` pass。
- `logs/codex-ai-screen-inviewy-smoke-20260607`: `screen.camera*.inViewY` 追加後に `cmake --build build\release-windows-x86_64 --config Release --target melonDS -j 4` pass。rule AI remote smoke 1600F pass。
- 同ログでJSON spot check pass。frame 870のplayer/object `screen.camera0` に `inViewX`、`inViewY`、`inView` が入り、player例は `inViewX=1` / `inViewY=0` / `inView=0`。
- 同ログから `python scripts\nsmb_mvl_ai_build_dataset.py ... --player 1 --require-player-found --label-source auto` pass。25行CSV生成。`self_screen0_in_view_y`、`self_screen1_in_view_y`、`opponent_screen0_in_view_y`、`opponent_screen1_in_view_y` の列追加を確認。
- `python scripts\nsmb_mvl_ai_train_imitation.py logs\codex-ai-screen-inviewy-smoke-20260607\ai-dataset-player1-auto.csv logs\codex-ai-screen-inviewy-smoke-20260607\ai-imitation-player1-auto.npz --epochs 200 --lr 0.05` pass。`python scripts\nsmb_mvl_ai_predict_imitation.py logs\codex-ai-screen-inviewy-smoke-20260607\ai-imitation-player1-auto.npz logs\codex-ai-screen-inviewy-smoke-20260607\ai-dataset-player1-auto.csv logs\codex-ai-screen-inviewy-smoke-20260607\ai-predictions-player1-auto.csv --limit 10` pass。10行サンプルで `button_acc=0.975`、`exact=0.800`。
- `python scripts\nsmb_mvl_ai_render_playlog_svg.py logs\codex-ai-screen-inviewy-smoke-20260607\ai-playlog.jsonl logs\codex-ai-screen-inviewy-smoke-20260607\frame-1050-player1.svg --player 1 --frame 1050` pass。
- `logs/codex-ai-stage-layout-category-smoke-20260607`: `0x145` を `stage_layout` に分類後、rule AI remote smoke 1600F pass。catalogで `0x145 settings=0x00000000 stage_layout sampleVTable=0x02123D6C` を確認。
- 同ログから `python scripts\nsmb_mvl_ai_build_dataset.py ... --player 1 --require-player-found --label-source auto` pass。25行CSV生成。`count_stage_layout=1`、`count_object=0` を確認。
- `python scripts\nsmb_mvl_ai_train_imitation.py logs\codex-ai-stage-layout-category-smoke-20260607\ai-dataset-player1-auto.csv logs\codex-ai-stage-layout-category-smoke-20260607\ai-imitation-player1-auto.npz --epochs 200 --lr 0.05` pass。`python scripts\nsmb_mvl_ai_predict_imitation.py logs\codex-ai-stage-layout-category-smoke-20260607\ai-imitation-player1-auto.npz logs\codex-ai-stage-layout-category-smoke-20260607\ai-dataset-player1-auto.csv logs\codex-ai-stage-layout-category-smoke-20260607\ai-predictions-player1-auto.csv --limit 10` pass。10行サンプルで `button_acc=0.958`、`exact=0.700`。
- `python scripts\nsmb_mvl_ai_render_playlog_svg.py logs\codex-ai-stage-layout-category-smoke-20260607\ai-playlog.jsonl logs\codex-ai-stage-layout-category-smoke-20260607\frame-1050-player1.svg --player 1 --frame 1050` pass。
- 逆アセンブル確認: `StageLayout::getTileBehavior` / `getChunkID` / `readTileBehaviour` / `CollisionMgr::scanSolidTile` / `getTileType` を `tools\nsmb_us_rom_tool.py --rom roms\nsmb-us.nds disasm ... --overlay-id 0` で確認。`Stage::stageLayout=0x020CAD40`、chunk pointer table `0x020CAFE0`、base tile behavior table `0x020C8484`、dynamic tile behavior table pointer `0x0208AF3C`、solid mask `0x08990000` をAI tile probe sourceに採用した。
- `logs/codex-ai-tileprobe-yfix-smoke-20260607`: StageLayout tile probe追加後に `cmake --build build\release-windows-x86_64 --config Release --target melonDS -j 4` pass。host単体の標準split smoke 1150F pass。AI play logは10行、frame 900以降で `players[1].tileProbe.found=1`、`center/below/aheadBody/aheadBelow` などの `tileId=0x001`、`behavior=0x0000002A` が保存されることを確認。
- 同ログで `python scripts\nsmb_mvl_ai_inspect_playlog.py logs\codex-ai-tileprobe-yfix-smoke-20260607\ai-playlog.jsonl --player 1 --limit 8` pass。`probe` 列に `hole:ab:001,ad:001,b:001` のように前方/下方向のtile idが表示されることを確認。
- 同ログから `python scripts\nsmb_mvl_ai_build_dataset.py ... --player 1 --label-source auto --require-player-found` pass。9行CSV生成。`self_tile_probe_found`、`self_tile_probe_holeAhead`、`self_tile_probe_below_tile_id`、`self_tile_probe_aheadBelow_tile_id` などの列追加を確認。
- `python scripts\nsmb_mvl_ai_train_imitation.py logs\codex-ai-tileprobe-yfix-smoke-20260607\ai-dataset-player1-auto.csv logs\codex-ai-tileprobe-yfix-smoke-20260607\ai-imitation-player1-auto.npz` pass。`python scripts\nsmb_mvl_ai_predict_imitation.py logs\codex-ai-tileprobe-yfix-smoke-20260607\ai-imitation-player1-auto.npz logs\codex-ai-tileprobe-yfix-smoke-20260607\ai-dataset-player1-auto.csv logs\codex-ai-tileprobe-yfix-smoke-20260607\ai-predictions-player1-auto.csv` pass。9行の小データで `button_acc=1.000`、`exact=1.000`。これはパイプライン検証であり、強さ評価ではない。
- `python -m py_compile scripts\nsmb_mvl_ai_build_dataset.py scripts\nsmb_mvl_ai_inspect_playlog.py scripts\nsmb_mvl_ai_train_imitation.py scripts\nsmb_mvl_ai_predict_imitation.py` pass。
- `python -m py_compile scripts\nsmb_mvl_ai_render_playlog_svg.py` pass。
- `python scripts\nsmb_mvl_ai_render_playlog_svg.py logs\codex-ai-tileprobe-yfix-smoke-20260607\ai-playlog.jsonl logs\codex-ai-tileprobe-yfix-smoke-20260607\frame-1020-player1-tileprobe.svg --player 1 --frame 1020` pass。SVG内に `tileProbe center/feet/below/aheadBody/aheadFeet/aheadBelow/ahead2Feet/ahead2Below/above` の矩形とtile id/behavior titleが出ることを確認。
- `logs/codex-ai-ruleai-tileprobe-smoke-20260607`: RuleAI `FrameState` への tileProbe summary接続後に `cmake --build build\release-windows-x86_64 --config Release --target melonDS -j 4` pass。`MELONDS_NSML_RULE_AI=1` / `MELONDS_NSML_RULE_AI_TRACE=1` つきのhost単体標準split smoke 1150F pass。起動時のRuleAI enabledログは確認したが、このharnessでは入力決定traceは出なかったため、実際のRuleAI操作経路でterrain jumpが発火するかは次回の対象。
- 同ログで `python scripts\nsmb_mvl_ai_inspect_playlog.py ... --player 1 --limit 8` pass、`python scripts\nsmb_mvl_ai_build_dataset.py ... --player 1 --label-source auto --require-player-found` pass。tileProbe列は引き続き生成され、既存tileProbeモデルで `python scripts\nsmb_mvl_ai_predict_imitation.py ... --limit 5` pass。
- `logs/codex-ai-tileprobe-lr-smoke-20260607`: 左右固定tileProbe追加後に `cmake --build build\release-windows-x86_64 --config Release --target melonDS -j 4` pass。host単体標準split smoke 1150F pass。AI play logは10行、frame 1020で `wallLeft=0`、`holeLeft=1`、`wallRight=0`、`holeRight=1`、`leftBody/leftBelow/rightBody/rightBelow` samplesが保存されることを確認。
- 同ログで `python scripts\nsmb_mvl_ai_inspect_playlog.py ... --player 1 --limit 6` pass。`probe` 列に `hole+HL+HR:ab:001,ad:001,lb:001,rb:001,b:001` のように左右穴候補と左右tile idが表示されることを確認。
- 同ログから `python scripts\nsmb_mvl_ai_build_dataset.py ... --player 1 --label-source auto --require-player-found` pass。9行CSV生成。`self_tile_probe_wallLeft`、`self_tile_probe_holeLeft`、`self_tile_probe_wallRight`、`self_tile_probe_holeRight`、`self_tile_probe_leftBelow_found`、`self_tile_probe_rightBelow_found` の列追加を確認。
- `python scripts\nsmb_mvl_ai_train_imitation.py logs\codex-ai-tileprobe-lr-smoke-20260607\ai-dataset-player1-auto.csv logs\codex-ai-tileprobe-lr-smoke-20260607\ai-imitation-player1-auto.npz` pass。`python scripts\nsmb_mvl_ai_predict_imitation.py logs\codex-ai-tileprobe-lr-smoke-20260607\ai-imitation-player1-auto.npz logs\codex-ai-tileprobe-lr-smoke-20260607\ai-dataset-player1-auto.csv logs\codex-ai-tileprobe-lr-smoke-20260607\ai-predictions-player1-auto.csv` pass。9行の小データで `button_acc=1.000`、`exact=1.000`。
- `logs/codex-ai-stage0-ruleai-groundfallback-block-smoke-20260607`: stage 0対象のRuleAI専用スモーク1700F pass。`cmake --build build\release-windows-x86_64 --config Release --target melonDS -j 4` pass後に実施し、`NSMB RuleAI: inst=... terrain=...` traceが実操作経路で出ることを確認。frame 960以降で `terrain=ground:1 ahead:0/0 left:0/0 right:0/0` となり、接地contactフォールバックで偽hole jumpが抑えられることを確認。
- 同ログで `players[].tileProbe.samples[].block` がJSONLに出力され、`scripts\nsmb_mvl_ai_build_dataset.py ... --player 1 --label-source auto --require-player-found` pass。28行CSV生成、`self_tile_probe_*_block_*` / `opponent_tile_probe_*_block_*` は合計374列。今回のseedでは実際のblock sampleは0件だったが、直前の `logs/codex-ai-stage0-ruleai-offset48-block-smoke-20260607` ではframe 1230の `leftBody` で `tileId=71`、`question=1`、`itemBox=1`、`storageContents=7` を確認。
- 同ログから `python scripts\nsmb_mvl_ai_train_imitation.py ... --epochs 200 --lr 0.05` pass。28行の小データで `train_button_acc=0.992`、`val_button_acc=0.833`。`python scripts\nsmb_mvl_ai_predict_imitation.py ... --limit 10` pass、`button_acc=0.975`、`exact=0.900`。`python scripts\nsmb_mvl_ai_render_playlog_svg.py ... --frame 1200` pass。
- `logs/codex-ai-stage0-effective-tileprobe-smoke-20260607`: effective tileProbe summaryとsample `status` / `lowType` 追加後に `cmake --build build\release-windows-x86_64 --config Release --target melonDS -j 4` pass、`python -m py_compile scripts\nsmb_mvl_ai_build_dataset.py scripts\nsmb_mvl_ai_inspect_playlog.py scripts\nsmb_mvl_ai_render_playlog_svg.py` pass。RuleAI専用スモーク1700F pass。
- 同ログで `python scripts\nsmb_mvl_ai_inspect_playlog.py ... --player 1 --limit 14` pass。frame 960以降の地上接触中は `probe` 列が `ground+suppress` になり、raw `hole*` を `effectiveHole*=0` へ抑えたことを確認。JSON spot checkで `summary.contactGround=1`、`effectiveGroundBelowSolid=1`、`holeSuppressedByContact=1`、`effectiveHoleAhead/Left/Right=0`、sample `feet.lowType=42` を確認。
- 同ログから `python scripts\nsmb_mvl_ai_build_dataset.py ... --player 1 --label-source auto --require-player-found` pass。28行CSV生成。`self_tile_probe_effectiveGroundBelowSolid`、`self_tile_probe_holeSuppressedByContact`、`self_tile_probe_effectiveHoleAhead`、`self_tile_probe_feet_status`、`self_tile_probe_feet_lowType`、`self_tile_probe_below_status`、`self_tile_probe_below_lowType` の列追加を確認。
- 同ログから `python scripts\nsmb_mvl_ai_train_imitation.py ... --epochs 200 --lr 0.05` pass。28行の小データで `train_button_acc=0.992`、`val_button_acc=0.958`。`python scripts\nsmb_mvl_ai_predict_imitation.py ... --limit 10` pass、`button_acc=0.975`、`exact=0.900`。`python scripts\nsmb_mvl_ai_render_playlog_svg.py ... --frame 1200` pass。
- `logs/codex-ai-stage0-tile-catalog-smoke-20260607`: tile catalogスクリプト追加と失敗時tileID/behaviorTable保存後に `cmake --build build\release-windows-x86_64 --config Release --target melonDS -j 4` pass、`python -m py_compile scripts\nsmb_mvl_ai_catalog_tiles.py scripts\nsmb_mvl_ai_build_dataset.py scripts\nsmb_mvl_ai_inspect_playlog.py scripts\nsmb_mvl_ai_render_playlog_svg.py` pass。RuleAI専用スモーク1300F pass。
- 同ログで `python scripts\nsmb_mvl_ai_catalog_tiles.py ... --player 1 --limit 25` pass。接地中の成功サンプルは `tileId=0x001 behavior=0x0000002A lowType=0x2A solid=0 contactGround=1 effectiveGround=1 suppressed=1` が中心。`below/aheadBelow/*Below` の未取得は `status=6` かつ `tileId=0x110-0x113`、`behaviorTable=0x00000000` として見えるようになった。
- 同ログから `python scripts\nsmb_mvl_ai_inspect_playlog.py ... --player 1 --limit 10` pass、`python scripts\nsmb_mvl_ai_build_dataset.py ... --player 1 --label-source auto --require-player-found` pass。15行CSV生成。`python scripts\nsmb_mvl_ai_train_imitation.py ... --epochs 200 --lr 0.05` pass、`python scripts\nsmb_mvl_ai_predict_imitation.py ... --limit 10` pass、`button_acc=0.975`、`exact=0.900`。`python scripts\nsmb_mvl_ai_render_playlog_svg.py ... --frame 1080` pass。
- `python -m py_compile scripts\nsmb_mvl_ai_build_dataset.py scripts\nsmb_mvl_ai_train_imitation.py scripts\nsmb_mvl_ai_create_recording_manifest.py scripts\nsmb_mvl_ai_make_recordings_index.py scripts\nsmb_mvl_ai_verify_replay.py scripts\nsmb_mvl_ai_export_viewer_data.py` pass。
- `logs/codex-ai-stage0-tile-catalog-smoke-20260607` の既存JSONLから `recording.json` 作成、`viewer-data.json` export、`recording.json` と同一JSONLの `nsmb_mvl_ai_verify_replay.py` pass。最終frame 1290一致。
- 同ログの `recording.json` / `recordings-index.json` から `nsmb_mvl_ai_build_dataset.py` pass。manifest/index経由で15行CSV生成。
- `python scripts\nsmb_mvl_ai_train_imitation.py logs\codex-ai-stage0-tile-catalog-smoke-20260607\ai-dataset-from-index-player1.csv logs\codex-ai-stage0-tile-catalog-smoke-20260607\ai-imitation-from-index-player1.npz --epochs 50 --lr 0.05 --split-by-recording` pass。1記録なので通常splitへフォールバックし、`train=12 val=3`。
- `scripts\run-nsmb-mvl-recording-replay.ps1` / `scripts\run-nsmb-mvl-split-local-input-smoke.ps1` / `scripts\run-nsmb-mvl-human-recording.ps1` のPowerShell Parser構文確認pass。
- `python -m py_compile scripts\nsmb_mvl_ai_create_recording_manifest.py scripts\nsmb_mvl_ai_verify_replay.py` pass。
- `python scripts\nsmb_mvl_ai_verify_replay.py logs\codex-ai-stage0-tile-catalog-smoke-20260607\recording.json logs\codex-ai-stage0-tile-catalog-smoke-20260607\ai-playlog.jsonl --checkpoint-interval 30 --checkpoint-start-frame 900 --max-checkpoints 3` pass。最終frame 1290と途中checkpoint 3件を同一ログで検証。
- `python scripts\nsmb_mvl_ai_create_recording_manifest.py ... --host-input-script ... --client-input-script ... --frames 1290` で、input script replay情報入りのテストmanifestを生成できることを確認。
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\run-nsmb-mvl-recording-replay.ps1 -RecordingManifest logs\codex-ai-stage0-tile-catalog-smoke-20260607\recording-replay-inputscript.json -DryRun -LogDir logs\codex-ai-recording-replay-dryrun-20260607` pass。manifestからhost/client input script、ROM、AI play log出力先、検証対象playlogを解決できることを確認。これは既存RuleAIログへ手動入力scriptを後付けしたdry-runであり、完全再現の実走検証ではない。
- `python scripts\nsmb_mvl_ai_create_recording_manifest.py logs\codex-ai-stage0-tile-catalog-smoke-20260607\ai-playlog.jsonl ... --max-event-samples 5` pass。`summary.eventSamples` に `playerDeath` frame 1200 と `blockCandidateVisible` frame 1230 / `leftBody` / `tileId=71` / `storageContents=7` が出ることを確認。
- `scripts\run-nsmb-mvl-manual-local.ps1` / `scripts\run-nsmb-mvl-human-recording.ps1` / `scripts\run-nsmb-mvl-split-local-input-smoke.ps1` / `scripts\run-nsmb-mvl-recording-replay.ps1` のPowerShell Parser構文確認pass。
- `python -m py_compile scripts\nsmb_mvl_ai_create_recording_manifest.py` pass。
- tempの `packet-replay.csv` とpacket replay用 `recording.json` を作り、`powershell -NoProfile -ExecutionPolicy Bypass -File scripts\run-nsmb-mvl-recording-replay.ps1 -RecordingManifest <temp-recording.json> -DryRun -LogDir logs\codex-ai-packet-replay-dryrun-20260607` pass。`replayMode=packet_replay`、host/client packet replay file、split起動引数を解決できることを確認。これはメタデータ/dry-run確認であり、melonDS実走の完全再現検証ではない。
- `python scripts\nsmb_mvl_ai_verify_replay.py logs\codex-ai-stage0-tile-catalog-smoke-20260607\recording.json logs\codex-ai-stage0-tile-catalog-smoke-20260607\ai-playlog.jsonl --scan-frames --mismatch-report-json ... --mismatch-report-csv ...` pass。同一ログの全frame scanで不一致なし。
- tempの改変AI play logで `python scripts\nsmb_mvl_ai_verify_replay.py <expected> <actual-mismatch> --scan-frames --mismatch-report-json <temp-diff.json> --mismatch-report-csv <temp-diff.csv>` を実行し、`first mismatch frame=900 ... hash` を検出。CSVには `frame,field,kind,expected,actual,message` が出ることを確認。
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\run-nsmb-mvl-recording-replay.ps1 -RecordingManifest logs\codex-ai-stage0-tile-catalog-smoke-20260607\recording-replay-inputscript.json -DryRun -ScanFrames -LogDir logs\codex-ai-replay-scan-dryrun-20260607` pass。replay起動計画に `scanFrames=true` と `replay-mismatch.json/csv` 出力先が入ることを確認。
- `scripts\run-nsmb-mvl-recording-postcommands.ps1` のPowerShell Parser構文確認pass。一時 `recording-session.json` で `-DryRun` pass、postCommandsの順序と件数を解決できることを確認。
- `logs/codex-ai-specialobjects-smoke-20260607`: `specialObjects` 追加後に `cmake --build build\release-windows-x86_64 --config Release --target melonDS -j 4` pass。RuleAI smoke 1300F pass。17行JSONLすべてに `specialObjects` があり、今回の通常走行では `fireballs.active=0`、handler wordsは `fireballs.words[0]=0x021C29A0`、`projectiles.words[0]=0x021C6D0C` として取得できることを確認。
- 同ログから `python scripts\nsmb_mvl_ai_build_dataset.py ... --player 1 --label-source auto --require-player-found` pass。CSVに `fireballs_active`、`fireballs_handler_word0`、`projectiles_handler_word0` が出ることを確認。
- 同ログから `python scripts\nsmb_mvl_ai_export_viewer_data.py ...` pass。viewer JSONに `specialObjects` が残ることを確認。`python scripts\nsmb_mvl_ai_create_recording_manifest.py ... --max-event-samples 5` pass、`summary.specialObjectFrames.fireballActive=0` と `eventSamples.fireballActive=[]` を確認。
- `tools/nsmb-mvl-gui`: `pnpm run typecheck` pass。`pnpm biome check src/launcher/AIReplayViewer.tsx` pass。`pnpm vitest --config vitest.browser.config.ts run src/launcher/AIReplayViewer.browser.test.tsx` pass（1 file / 2 tests）。`pnpm run ci` は従来通り変更外ファイルを含むCRLF整形差分でBiome停止。
- `python -m py_compile scripts\nsmb_mvl_ai_audit_recordings.py scripts\nsmb_mvl_ai_create_recording_manifest.py scripts\nsmb_mvl_ai_build_dataset.py` pass。
- `python scripts\nsmb_mvl_ai_audit_recordings.py logs\codex-ai-specialobjects-smoke-20260607\recording.json --stage 0 --min-rows 1 --min-gameplay-rows 1 --min-player-found-ratio 0.5 --min-label-ratio 0.5 --min-nonzero-label-rows 1 --output ...` pass。statusは `pass`、警告としてquality unreviewedとplayer missing rowsを出す。
- 同じrecordingに `--require-event fireballActive:1` を指定すると、`event fireballActive count 0 < 1` でfailすることを確認。低頻度scenarioの取り忘れ検出に使える。
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\run-nsmb-mvl-human-recording.ps1 -LogDir logs\codex-ai-human-recording-dryrun-20260607 -Frames 120 -DryRun` pass。`recording-session.json` のpostCommandsにpacket変換、host/client manifest、index、dataset、`nsmb_mvl_ai_audit_recordings.py --require-packet-replay` が入ることを確認。
- `tools/nsmb-mvl-gui`: `pnpm run typecheck` pass。`pnpm biome check src/launcher/AIReplayViewer.tsx src/launcher/AIReplayViewer.browser.test.tsx` pass。`pnpm vitest --config vitest.browser.config.ts run src/launcher/AIReplayViewer.browser.test.tsx` pass（recording manifestのevent sample表示テストを含む）。
- `tools/nsmb-mvl-gui`: `pnpm vitest --config vitest.config.ts run` pass（4 files / 14 tests）。`pnpm vitest --config vitest.browser.config.ts run` pass（4 files / 13 tests）。`pnpm playwright test` pass（3 tests）。
- `tools/nsmb-mvl-gui`: `pnpm run ci` は `tsc --noEmit` 後の `biome check .` で停止。原因は変更外の既存ファイルを含むCRLF整形差分。変更ファイル単位のBiomeと全テストはpass。

## Next Actions

- StageLayout tile probeをstage 0の実プレイで増やし、`tileId=0x001 behavior=0x0000002A lowType=42` など低位tile typeの意味を特定して、raw `solidish` / `hole*` を接地contact補正なしでも目視に近づける。
- dynamic tile behavior table pointerが0のまま `tileId=0x110-0x113` を引く場面を、StageLayout初期化/`changeTile`/question block animation pathと照合する。
- RuleAIをstage 0の実際のCPU操作経路でさらに長く走らせ、スター取得、敵回避、落下復帰、箱接触時の入力変化を確認する。
- ブロック/アイテム箱の中身と叩いた後の状態をstage 0の `StageLayout::changeTile` / question block animation path と実ログで照合し、`tileProbe.samples[].block` の解釈を詰める。
- Fire Marioでstage 0ログを取り、`specialObjects.fireballs.active` が増えるframeを起点に、handler内slotの座標/owner/速度/寿命offsetを特定する。通常actorに出るprojectileがあれば object ID/vtable/category も併せて分類する。
- 人間プレイ記録を実際に複数本取り、`recordings-index.json` 経由のdataset、`--split-by-recording`、GUIの `AIログ` タブで、入力ラベルと目視相当状態が期待通り読めるか確認する。
- packet capture付きの新規人間記録を取り、`run-nsmb-mvl-recording-postcommands.ps1` で `packet-replay.csv` / `recording.json` / `recordings-index.json` / dataset / `recording-audit.json` を生成する。
- `run-nsmb-mvl-recording-replay.ps1 -ScanFrames` でpacket replayの実走を行い、最終frame、checkpoint frame、最初の不一致reportを確認する。必要ならinput script replayとの差分も比較する。
- 落下死ラインとステージ境界をメモリから取り、`fallRisk` と `tileProbe.holeAhead` を統合した危険判定を作る。
- 学習済み `.npz` をPoCまたは外部sidecarから推論して入力へ戻す経路を作る。
- object categoryをログ実例で検証し、ステージ固有objectの意味を詰める。`0x021` は実ログでBig Star actorと同じvtableだったため `big_star_related` に分類した。`0x0F0` はrollback notes上のItem付随短命effectとして `item_spawn_effect` に分類した。`0x145` は既存RAM probeの名前表に基づいて `stage_layout` に分類した。
