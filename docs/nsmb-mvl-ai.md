# NSMB Mario vs Luigi AI

## Current Goal - 2026-06-07

1人用MvLのCPU相手を、ルールベースAIだけで終わらせず、次の混合方式で育てる。

1. ルールベースAIで最低限ゲームを成立させる。
2. 人間プレイとルールベースAIのプレイログを集める。
3. 入力ラベル付きの状態ログから模倣学習する。
4. 模倣学習済みAIを初期値にして自己対戦で強化する。
5. 実プレイ向けに反応遅延、ミス率、探索制限で強さ調整する。

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

## AI Play Log

有効化env:

- `MELONDS_NSML_AI_PLAY_LOG=<path>`: JSONL出力先。
- `MELONDS_NSML_AI_PLAY_LOG_INTERVAL=1`: ログ間隔。学習データ収集は1、smoke/目視検査は30などでよい。
- `MELONDS_NSML_AI_PLAY_LOG_START_FRAME=0`
- `MELONDS_NSML_AI_PLAY_LOG_END_FRAME=0`: 0なら終了指定なし。
- `MELONDS_NSML_AI_PLAY_LOG_MAX_OBJECTS=32`: 1フレームに保存するactive object上限。
- `MELONDS_NSML_AI_PLAY_LOG_INCLUDE_NON_GAMEPLAY=1`: 通常はMvL gameplay中だけ保存する。これを指定すると非gameplayも含める。

JSONL schema `nsmb_mvl_ai_play_log_v1` は、各行に `inputs`、`players`、`targets`、`camera`、`objectSummary`、`objects` を持つ。`objects` はactive objectだけを保存し、既知IDには `category` を付ける。

`inputs` にはメモリ上の `console0/1`、`player0/1` に加えて、PoCが実際にそのフレーム近辺へ注入した `appliedPlayer0/1` を保存する。模倣学習の教師ラベルはまず `appliedPlayerN.held` / `heldHex` を使う。

`visualSummary` には、人間が画面を見て判断する情報に近づけるための要約を保存する。現時点では左右ラップ込みの `visibleCamera0X` / `visibleCamera1X`、カテゴリ別count、player別最近傍 `big_star_actor` / `moving_hazard` などを持つ。object個別にも `relative` と `screen.camera*.inViewX` を保存する。

現時点の既知カテゴリ:

- `player`
- `big_star_actor`
- `big_star_candidate`
- `world_item`
- `neutral_item`
- `dropped_star_item`
- `moving_hazard`
- `enemy_koopa`
- `camera`
- `stage_scene`
- `stage_actor_manager`
- `stage_controller`
- `object`

## Planned Pipeline

- `MELONDS_NSML_AI_PLAY_LOG=<path>` でAI/人間共通の観測ログを出す。
- `python scripts\nsmb_mvl_ai_build_dataset.py <playlog.jsonl> <dataset.csv> --player 1 --require-player-found` で固定長特徴量へ変換する。
- `python scripts\nsmb_mvl_ai_train_imitation.py <dataset.csv> <model.npz>` でキー入力の多ラベル分類モデルを学習する。
- その後、同じ観測schemaを使って自己対戦学習へ進む。
- 強さ調整は、推論時に入力反応遅延、ランダムミス、action hold制限、近傍探索幅制限を入れる。

## Current Blockers / Unknowns

- object IDと画面上の意味の対応はまだ完全ではない。既知IDからカテゴリ付けを始め、ログを見ながら coin/enemy/item/block/hazard の分類を増やす。
- 目視同等にするには、画面Y座標系への変換、地形/足場/穴、ブロック状態、アイテム箱状態が不足している。X方向は左右ラップ込みの可視判定まで入ったが、完全な `inView` はY側の対応を追加で詰める必要がある。
- 自己対戦に進む前に、ログschemaを実プレイログで増強し、学習済みモデルを入力へ戻す推論経路を作る必要がある。

## Verification

- `cmake --build build\release-windows-x86_64 --config Release --target melonDS -j 4` pass。
- `logs/codex-ai-playlog-label-smoke-20260607`: rule AI remote + PacketBridge JIT helper + `MELONDS_NSML_AI_PLAY_LOG` で1600F pass。`ai-playlog.jsonl` は27行、frame 810-1590、player actorあり25行、`appliedPlayer1` valid 24行。
- 同ログでカテゴリ `big_star_actor`、`camera`、`moving_hazard`、`player`、`stage_actor_manager`、`stage_controller`、`stage_scene` を確認。frame 900 では `appliedPlayer1.heldHex=0x810` が出ており、remote CPUの入力ラベルが保存されている。
- `python scripts\nsmb_mvl_ai_build_dataset.py logs\codex-ai-playlog-label-smoke-20260607\ai-playlog.jsonl logs\codex-ai-playlog-label-smoke-20260607\ai-dataset-player1.csv --player 1 --require-player-found` pass。24行のCSVが生成され、`label_held`、button別 `label_*`、self/opponent/target/camera/object/nearestカテゴリ特徴を確認。
- `python scripts\nsmb_mvl_ai_train_imitation.py logs\codex-ai-playlog-label-smoke-20260607\ai-dataset-player1.csv logs\codex-ai-playlog-label-smoke-20260607\ai-imitation-player1.npz --epochs 200 --lr 0.05` pass。24行の小データで学習と `.npz` 保存が動作することを確認。これはパイプライン検証であり、強さ評価ではない。
- `logs/codex-ai-visual-wrapx-smoke-20260607`: visualSummary追加後のrule AI remote smoke 1600F pass。JSONL 27行、`visualSummary` 全行あり。frame 870で `visibleCamera0X=10`、`visibleCamera1X=11`、player objectの `screen.camera0.inViewX=1` を確認。
- 同ログから `python scripts\nsmb_mvl_ai_build_dataset.py ... --player 1 --require-player-found` pass、24行CSV生成。`visible_camera*_x` と `count_*` 特徴が追加された状態で `python scripts\nsmb_mvl_ai_train_imitation.py ... --epochs 200 --lr 0.05` pass。

## Next Actions

- JSONLの先頭数行を人間が読める形で検査し、欠けている状態を追加する。
- camera Y / player display Y の対応を解析し、完全な画面内判定を入れる。
- 学習済み `.npz` をPoCまたは外部sidecarから推論して入力へ戻す経路を作る。
- object categoryを増やし、coin/block/item box/terrain/hole相当の状態を足す。
