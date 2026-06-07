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

## AI Play Log

有効化env:

- `MELONDS_NSML_AI_PLAY_LOG=<path>`: JSONL出力先。
- `MELONDS_NSML_AI_PLAY_LOG_INTERVAL=1`: ログ間隔。学習データ収集は1、smoke/目視検査は30などでよい。
- `MELONDS_NSML_AI_PLAY_LOG_START_FRAME=0`
- `MELONDS_NSML_AI_PLAY_LOG_END_FRAME=0`: 0なら終了指定なし。
- `MELONDS_NSML_AI_PLAY_LOG_MAX_OBJECTS=32`: 1フレームに保存するactive object上限。
- `MELONDS_NSML_AI_PLAY_LOG_INCLUDE_NON_GAMEPLAY=1`: 通常はMvL gameplay中だけ保存する。これを指定すると非gameplayも含める。

JSONL schema `nsmb_mvl_ai_play_log_v1` は、各行に `inputs`、`players`、`targets`、`camera`、`objectSummary`、`objects` を持つ。`objects` はactive objectだけを保存し、既知IDには `category` を付ける。各objectには `objectId`、`settings`、`guid`、`base`、`offset`、`vtable`、state/flags、座標、速度、player相対座標、screen情報を保存する。

`inputs` にはメモリ上の `console0/1`、`player0/1` に加えて、PoCが実際にそのフレーム近辺へ注入した `appliedPlayer0/1` を保存する。模倣学習の教師ラベルはまず `appliedPlayerN.held` / `heldHex` を使う。

`players[].contact` には、数値の衝突/環境フラグから人間が目視で判断する地形接触に近いbitを保存する。代表項目は `ground`、`predictGround`、`ceiling`、`wallLeft`、`wallRight`、`edgeGrab`、`water`、`liquid`、`submerged`、`quicksand`、`rope`、`tightrope`、`pole`、`spikesLeft`、`spikesRight`、`conveyorLeft`、`conveyorRight`、`wrapLeft`、`wrapRight`。CSVには `self_contact_*` / `opponent_contact_*` として展開する。

`players[].collisionMgr` には、プレイヤーactor内のCollisionMgrから読んだ地形/接触結果を保存する。主な項目は `collisionResult`、`groundCollision`、`deltaX/Y`、`attachedTileX/Y`、`bottomModifierTileType`、`topModifierTileType`、`sideModifierTileTypeLeft/Right`。`bottomModifierTile` は modifier tile typeを `solid`、`coin`、`questionBlock`、`breakableBlock`、`brickBlock`、`slope`、`water`、`partialSolid`、`harmful`、`invisibleBlock`、modifier、storage contentsへ展開したもの。これは「足元そのもの」ではなく、CollisionMgrが保持するbottom modifier tileで、前方タイルや穴の完全判定ではない。CSV/inspectでは不自然に多数のtile category bitが立つ値をsanity checkで除外する。

`players[].tileDamage` には、Player本体に保存されるtile damage flags/typeを保存する。`active=1` のときはlava/poison/その他ダメージ地形などの接触候補として扱う。通常の床接地や壁接触は `contact` と `collisionMgr.collisionResult` を見る。

`players[].tileProbe` には、StageLayoutから直接読んだタイル地形サンプルを保存する。サンプル点は `center`、`feet`、`below`、`aheadBody`、`aheadFeet`、`aheadBelow`、`ahead2Feet`、`ahead2Below`、`above`、`leftBody`、`leftFeet`、`leftBelow`、`left2Below`、`rightBody`、`rightFeet`、`rightBelow`、`right2Below`。各点は `pixelX/Y`、`chunkId`、`tileId`、`behavior`、`tile`、`block`、`solidish` を持つ。`tile` は `solid`、`scanSolid`、`coin`、`questionBlock`、`breakableBlock`、`brickBlock`、`slope`、`water`、`partialSolid`、`harmful` などへ展開する。`block` は現在のStageLayout tileから `any`、`itemBox`、`question`、`breakable`、`brick`、`invisible`、`storageContents`、`modifier`、`currentTileId`、`currentBehavior` を保存する。`storageContents` / `itemBox` はblock flagが立っているtileでのみ意味のある値として扱う。叩いた後の箱はStageLayoutがtileを差し替えた後の `currentTileId/currentBehavior` として現れる想定なので、stage 0の実ログで照合する。`summary.holeAhead` は速度方向の前方下2点、`summary.holeLeft` / `holeRight` は左右下2点がsolidishでない暫定穴判定。`summary.wallAhead` / `wallLeft` / `wallRight` はbody/feetがsolidishの暫定壁判定。CSVには `self_tile_probe_*` / `opponent_tile_probe_*` として展開する。

`players[].screen` / `players[].fallRisk` には、playerをカメラ座標へ投影した情報を保存する。`fallRisk` は `screenY0/1`、`cameraBottomDistance0/1`、`nearCameraBottom0/1`、`belowCamera0/1`、`velYPositive/Negative` を持つ。完全な穴判定ではないが、目視上の「下へ落ちている」「画面下端に近い」を学習データに入れるための暫定特徴。

`visualSummary` には、人間が画面を見て判断する情報に近づけるための要約を保存する。現時点ではY込みの `visibleCamera0/1`、左右ラップ込みX判定の `visibleCamera0X` / `visibleCamera1X`、カテゴリ別count、player別最近傍 `big_star_actor` / `moving_hazard` などを持つ。player/object個別にも `relative` と `screen.camera*.inViewX/inViewY/inView` を保存する。

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
- `python scripts\nsmb_mvl_ai_build_dataset.py <playlog.jsonl> <dataset.csv> --player 1 --require-player-found` で固定長特徴量へ変換する。デフォルトの `--label-source auto` は `appliedPlayerN` があればそれを使い、なければ `playerN` を使う。人間ログだけを明示する場合は `--label-source player` を指定する。
- `python scripts\nsmb_mvl_ai_train_imitation.py <dataset.csv> <model.npz>` でキー入力の多ラベル分類モデルを学習する。
- `python scripts\nsmb_mvl_ai_predict_imitation.py <model.npz> <dataset.csv> <predictions.csv>` で学習済みモデルのオフライン推論結果を確認する。
- `python scripts\nsmb_mvl_ai_inspect_playlog.py <playlog.jsonl> --player 1` で、frame、入力、接地/壁/水などのcontact、player/相手/星/hazardの相対位置、可視X数、カテゴリ数を目視確認する。
- `python scripts\nsmb_mvl_ai_render_playlog_svg.py <playlog.jsonl> <frame.svg> --player 1 --frame <frame>` で、player中心の相対配置とtileProbeサンプル点をSVGとして目視確認する。
- `python scripts\nsmb_mvl_ai_catalog_objects.py <playlog.jsonl>` で、未知objectの出現頻度と代表的な相対位置を確認し、カテゴリ付けを増やす。
- その後、同じ観測schemaを使って自己対戦学習へ進む。
- 強さ調整は、推論時に入力反応遅延、ランダムミス、action hold制限、近傍探索幅制限を入れる。

## Current Blockers / Unknowns

- object IDと画面上の意味の対応はまだ完全ではない。coin/item/enemy/platform/hazard の初期カテゴリは入り、block/item box候補はStageLayout tileProbeから取れるようになったが、stage 0の実ログを見ながらステージ固有ギミックの分類を増やす。
- 目視同等にするには、stage 0の実プレイ場面でタイルサンプルを増やし、穴/壁/床判定のoffsetとsolid maskを調整する必要がある。左右ラップ込みX判定、Y込みの完全可視判定、player接触地形、CollisionMgr接触結果、modifier tile、tile damage、playerの画面Y/カメラ底距離、StageLayout由来の前方/左右タイルサンプルは取れるようになった。
- StageLayout raw probeでは、stage 0の接地中に `tileId=0x001 behavior=0x0000002A` が出る場面があり、現行solidish maskでは床として分類できない。RuleAIは接地contactで偽holeを抑えるが、学習用の目視同等ログとしては低位tile typeの意味を追加で詰める必要がある。
- ブロック/アイテム箱の「中身」はblock flagつきStageLayout tile behaviorのstorage contentsとして保存するようになった。叩いた後の状態は現在tile id/behaviorの変化として取れる想定だが、stage 0の実ログで `StageLayout::changeTile` / question block animation path と照合して詰める。
- 自己対戦に進む前に、ログschemaを実プレイログで増強し、学習済みモデルを入力へ戻す推論経路を作る必要がある。

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

## Next Actions

- StageLayout tile probeをstage 0の実プレイで増やし、`tileId=0x001 behavior=0x0000002A` など低位tile typeの意味を特定して、学習ログ側の `solidish` / `hole*` を接地contactフォールバックなしでも目視に近づける。
- RuleAIをstage 0の実際のCPU操作経路でさらに長く走らせ、スター取得、敵回避、落下復帰、箱接触時の入力変化を確認する。
- ブロック/アイテム箱の中身と叩いた後の状態をstage 0の `StageLayout::changeTile` / question block animation path と実ログで照合し、`tileProbe.samples[].block` の解釈を詰める。
- 落下死ラインとステージ境界をメモリから取り、`fallRisk` と `tileProbe.holeAhead` を統合した危険判定を作る。
- 学習済み `.npz` をPoCまたは外部sidecarから推論して入力へ戻す経路を作る。
- object categoryをログ実例で検証し、ステージ固有objectの意味を詰める。`0x021` は実ログでBig Star actorと同じvtableだったため `big_star_related` に分類した。`0x0F0` はrollback notes上のItem付随短命effectとして `item_spawn_effect` に分類した。`0x145` は既存RAM probeの名前表に基づいて `stage_layout` に分類した。
