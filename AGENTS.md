# リポジトリの作業ルール

## パッケージマネージャー

別途指示がない限り、Node.jsプロジェクトではpnpmを使用する。Codex環境では、プロジェクトで管理するpnpmよりも同梱のpnpmが`PATH`上で優先される場合がある。このリポジトリでは、`corepack pnpm ...`のように、Corepack経由でpnpmコマンドを実行する。

## Git操作

自動でpushしない。現在の会話でユーザーが明示的にpushを依頼した場合だけ、`git push`を実行する。

完了した作業の保存に役立つ場合は、ローカルでコミットしてよい。ただし、リモートへのpushには毎回ユーザーの明示的な依頼が必要である。

## 進捗の記録

このフォークでは、ROM解析、ROMパッチ、melonDS／入力同期PoCの進捗を`docs/`で管理する。

ROM解析、ROMパッチ、melonDS／入力同期PoCの作業を開始したとき、方針を変更したとき、意味のある段階を完了したとき、または作業を妨げる問題が発生したときは、応答を終える前に該当するMarkdownファイルを更新する。GUI、WebRTC／WAN通信、サイドカー、バックエンド、マッチメイキング、ランキングなど、ROM解析以外の作業では、Markdownの進捗文書を更新しない。進捗文書が1つだけだと決めつけず、作業に対応する文書を使う。

- `docs/nsmb-mario-vs-luigi-online-poc.md`：melonDS／ROMパッチ／入力同期PoCの状況と検証結果。
- `docs/nsmb-wan-netplay-roadmap.md`：WAN通信、WebRTCサイドカー、デスクトップGUI、バックエンド、マッチメイキング、ランキングのロードマップ。
- `docs/nsmb-mvl-rollback-design-notes.md`：後から参照するためのロールバック設計メモ。

PowerShellで日本語のMarkdownやその他のUTF-8テキストを読むときは、文字コードを指定しない`Get-Content`や`-Encoding Default`を使わない。この環境では文字化けする場合があるため、次の方法を使う。

- 全文または範囲を指定した読み取り：`Get-Content -LiteralPath <path> -Encoding UTF8`
- 検索や行番号付きの読み取り：`rg -n "<pattern>" <path>`

それでも日本語が文字化けする場合は、まず出力のデコードの問題として扱い、UTF-8を明示して再試行する。確認せずにファイルの内容が壊れていると判断しない。

該当する進捗文書には、次の内容を記載する。

- 完了した作業
- 現在の阻害要因
- 次に行うこと
- 検証状況
- ROMの用意やツールのインストールなど、ユーザー側で必要な準備

記録対象の実装進捗をチャットだけに残さない。

上記のルールでMarkdownの更新が必要な場合は、最終回答の前に該当文書を見直し、古い内容や矛盾がないか確認する。新しいメモを追記するだけでなく、既存の記述を整理・更新する。特に、次を守る。

- 完了済みの「次に行うこと」は削除または書き換える。
- 解消済みの阻害要因は完了・解決済みの記録に移すか、削除する。
- 現在の阻害要因と次に行うことを、文書の上部で見つけやすくする。
- 現状を簡潔にまとめた方が分かりやすい場合は、長い時系列の追記だけを続けない。

## UIの既存パターンとの整合性

新しいUIを作る前に、既存の画面やコンポーネントから、目的や操作の役割が似たUIを探して実装を確認する。同じコンポーネントを使うだけでなく、配置、余白、色、ボタンのvariant・サイズ、文言、状態表示、操作時の振る舞いなども確認し、類似するパターンがあればそれに倣う。

類似する既存パターンがない新規UIや、使いやすさ・操作の役割の違いなど、意図的に変える具体的な理由がある場合は、既存パターンに合わせなくてもよい。その場合は、変更理由を説明できるようにする。

## Tauri GUIのUIコンポーネント

`tools/bigstar`のTauri GUIにUIを追加するときは、新しいローカルコンポーネントを一から作る前に、既存のKiso UI構成を再利用する。コンポーネントは`src/components/ui`、レシピは`src/theme/recipes`にある。導入済みコンポーネントの一覧は`tools/bigstar/.kiso/installed.json`を確認する。

新しいUIコンポーネントが必要な場合は、まず[Kiso UI](https://github.com/uniunitaro/kiso-ui)に該当するものがあるか確認する。Kisoには公開済みパッケージがないため、ローカルにチェックアウトしたKisoリポジトリでCLIを実行し、`--target`にGUIパッケージを指定する。

```powershell
cd <kiso-ui checkout>
corepack pnpm ui list
corepack pnpm ui add <component-name> --target <path to tools/bigstar> --dry-run
corepack pnpm ui add <component-name> --target <path to tools/bigstar>
```

コンポーネント名は、`dialog`、`tabs`、`select`、`toast`、`collapsible`など、`ui list`に表示される名前を使う。CLIは依存パッケージをインストールしないため、追加したコンポーネントがimportするパッケージは、`tools/bigstar`で`corepack pnpm add`を使って追加する。追加後は`src/components/ui`と`src/theme/recipes`の生成ファイルを確認し、`panda.config.ts`は既定の`--panda-config=keep`で維持する。

アプリ固有の要件のためにKisoリポジトリを変更しない。アプリ固有のvariantやスタイルは、アプリ側の`src/theme/recipes`にあるコピー、または画面のコードに定義する。`ui diff`でKisoとの差分を確認でき、`ui update`はアプリ側で変更したファイルを保持する。

## コード品質の確認

Rustコードを変更した場合は、応答を終える前にフォーマットとClippyを実行する。対象のクレート／ワークスペースで`cargo fmt`を実行し、その後、警告をエラーとして扱うローカルの厳格なClippyエイリアス（通常は`cargo clippy-all`）を実行する。

TypeScriptコードを変更した場合は、応答を終える前にBiomeと型チェックを実行する。`corepack pnpm biome check`／`corepack pnpm biome format`、`corepack pnpm typecheck`など、パッケージにある既存のスクリプトをCorepack経由で使う。別のスクリプト名が定義されている場合は、リポジトリ内の対応するコマンドを使う。

pnpmで管理するパッケージを変更した場合は、応答を終える前に必ずそのパッケージで`corepack pnpm run ci`を実行する。`ci`スクリプトが存在しない場合やコマンドを実行できない場合は、その理由を最終回答で明示する。
