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
移植先は改行を正規化しているため、同一ファイルハッシュではなく下記の操作一致でも確認した。
他の2種類は2026-09-20の保存版をソース・地形のハッシュ付きで保持する。
改行もハッシュ検査の対象なので、保存パッケージはGitの改行変換を無効にしている。

研究用の学習・集計処理は移植せず、ラップ座標とジャンプ物理の必要な関数を抽出。
制御判断は6フレームごとで、間は直前の入力を保持する。起動時と世代変更時の
初期化は従来の手動対戦と同じ。IPCの標準出力はREADYまたはframe/heldだけに限定。
オンライン対戦のゲーム更新境界で入力を固定するmainの修正は維持する。

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
