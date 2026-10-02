# Bigstar CPU対戦ランタイム

GUIの対戦相手はクリボー (`beginner`)、ノコノコ (`combat_v2`)、カロン
(`development`) の3種類。草原専用、マリオを人間・ルイージをCPUが操作する。
名前は難度の目安であり、人間相手の段位や勝率を表すものではない。

## ビルド

Windows x64、Python 3.10以上、MSVCの標準ライブラリを利用できるclang++が必要。
Pythonはビルド時だけ使用し、利用者のPCには不要。

```powershell
python -m venv .venv-rule-build
.venv-rule-build/Scripts/python.exe -m pip install -r tools/rule-cpu/requirements-build.txt
./tools/rule-cpu/build.ps1 -Python "$PWD/.venv-rule-build/Scripts/python.exe" -Compiler 'C:/Program Files/LLVM/bin/clang++.exe'
cmake --build build/release-windows-x86_64 --config Release
cd tools/bigstar
corepack pnpm build:local:insiders
```

`build/rule-cpu/bigstar-rule-cpu.exe` はPython、地形、3種類の制御、探索用DLLを
内包する。GUIのsidecar同期とインストーラーに含まれる。起動前の `--check` は
全プロフィールを初期化し、保存版と高速化DLLの整合性を検査する。
DLL欠落・不一致時は低速な代替へ黙って切り替えず、対戦開始前に失敗を通知する。

## 移植範囲

元は `codex/rl-runtime-feasibility-20260911` の `9ee7acc0` 時点。
カロンの制御本体は採用済みソース由来（移植前SHA256:
`2aeb6a1f763daefa256f953865dd777d64dc465f4c60ff6af7363c32b159fee6`）。
2026-09-29の改善ブランチでは、近接戦を保護しつつ壁キック後の着地を完了する
限定修正を追加。検証範囲と未解決の弱点は `docs/nsmb-mvl-rule-ai-improvements-20260929.md`。
2026-10-01の改善ブランチでは、登場・土管退出中の横移動不能を詰まりと誤判定し、
操作可能直後に逆方向へ逃げる不具合を修正。通常開始の相手無操作試験で箱反応と
キノコ取得を126フレーム短縮。通常の詰まり検出は保持する。
同日の追加改善では、制御の初期化後、最初に装備を得るまでのキノコ回収に
左右の箱端から登る候補を追加した。通常開始の一条件で343→294fに短縮し、
78fの移動予測と実測が一致。死亡後の再装備まで一括で適用すると対戦成績が
悪化したため、一度装備を得た後は既存の回収方法を使う。
さらに初回装備・箱まで32px未満に限り中央を狙う跳躍を追加し、通常開始で隣の箱を
同時に叩く接触を避け、取得287fを確認。予測専用の地形記憶を分離し、
経路を採用しなかった場合の既存経路への副作用を防ぐ。
星へ向かう場面では、天井へぶつかる新しいジャンプを、接地と近い危険物への
到達範囲を確認して延期する。ダッシュと方向入力は保持。通常開始の一例で
最初の星460→421f、最初の14fの地上移動は実測一致。
詳細は `docs/nsmb-mvl-rule-ai-basics-20261001.md`。
移植先は改行を正規化しているため、同一ファイルハッシュではなく下記の操作一致でも確認した。
他の2種類は2026-09-20の保存版をソース・地形のハッシュ付きで保持する。
改行もハッシュ検査の対象なので、保存パッケージはGitの改行変換を無効にしている。

研究用の学習・集計処理は移植せず、ラップ座標とジャンプ物理の必要な関数を抽出。
制御判断は6フレームごとで、間は直前の入力を保持する。起動時と世代変更時の
初期化は従来の手動対戦と同じ。IPCの標準出力はREADYまたはframe/heldだけに限定。
オンライン対戦のゲーム更新境界で入力を固定するmainの修正は維持する。

## 任意の詳細記録

`MELONDS_NSML_RULE_CAPTURE` に未作成の `.jsonl.gz` ファイルの絶対パスを指定すると、
CPUへ渡った全フレームの観測、直前入力、選択入力、最後の判断理由を記録する。
親ディレクトリは先に作成する。既存ファイルは上書きせず起動に失敗する。
環境変数を指定しない通常対戦には記録処理を追加しない。記録時は圧縮・保存の負荷と
容量を使うので、診断終了後は環境変数を解除する。判断の周期は従来どおり6フレーム。

記録は `audit_recording.py` で操作を再検証し、`compare_recordings.py` で2回の再生を
比較できる。記録された過去の観測で操作が変わっても、その後の新しい試合結果を
予測したことにはならない。変更後の実機再生と分けて評価する。

## 検証

- GUI: `tools/bigstar` の `corepack pnpm run ci`。
- Rust: `tools/bigstar/src-tauri` の `cargo test`、`cargo clippy-all`。
- 実機: `solo_test::tests::real_cpu_profiles_and_peer_cleanup` を必要なROM・worker・ログ先の
  `BIGSTAR_SOLO_TEST_*` / `BIGSTAR_CPU_WORKER` 環境変数付きで実行する。
  任意の初期化済みセーブではなく、GUIで準備したhost/client ROMと対応するsavを使う。
- 2026-09-29: 同梱カロンへ以前の手動対戦9598フレームを順に入力し、操作差0件。
  3種類を各40秒、GUIと同じ起動処理で実行し、CPU準備・操作・片側終了時の両側停止を確認。
  これは移植・起動経路の検証であり、強さの追加評価ではない。
- デスクトップGUIからカロン戦を開始・終了し、人間側1画面だけが表示されることを確認。
  CPU側はQtの画面非表示属性を使用し、描画・エミュレーション自体は継続する。
