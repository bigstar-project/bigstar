# NSMB Mario vs Luigi WAN Netplay Roadmap

## Current GUI/signaling note - 2026-05-31

- Real Tauri GUI host startup produced `bridge exited(1)`.
- The GUI logs are written under `%APPDATA%\dev.melonds.nsmb-mvl\logs\nsmb-mvl-gui-*`, with `bridge.stdout.txt`, `bridge.stderr.txt`, `melonds.stdout.txt`, and `melonds.stderr.txt` per run.
- The latest host bridge stderr showed `signaling server error: {"error":"peer is not connected","type":"error"}` after the host sent its offer while the answer peer was not connected yet.
- Local signaling server code now queues pending `sdp` / `candidate` messages for the absent role and flushes them when the opposite role joins. `corepack pnpm run ci` passes in `tools/nsmb-signaling-server`.
- The deployed Worker still needs to be updated before the default GUI signaling URL benefits from this fix.
- The Tauri GUI now displays the latest log directory in the launcher, so a failed bridge/melonDS run can be inspected without opening devtools.

## 目的

New Super Mario Bros. DS の `Mario vs Luigi` を、最終的に一般ユーザーがポート開放なしで WAN 越しに対戦できる形へ持っていく。

`docs/nsmb-mario-vs-luigi-online-poc.md` は melonDS/ROM patch/input sync のPoC履歴と検証状態を扱う。この文書は WAN transport、WebRTC sidecar、将来のGUI/バックエンド方針を扱う。

## 現在の採用方針

サーバーなしの手動コピー&ペースト signaling で WebRTC DataChannel が使えることは確認済み。
次は Cloudflare Workers + Durable Objects の最小 signaling server を追加し、手動コピー&ペーストなしで `nsmb-net-bridge` が SDP/ICE 接続情報を交換できる状態へ進める。
Cloudflare 側は WSL の `~/oji-driving-school-reserver` と同じく pnpm + TypeScript + Alchemy 形式を参考にする。Cloudflare へのデプロイ操作はユーザーが行い、こちらでは実行しない。

重要な設計判断:

- melonDS本体の入力同期実装はすぐに壊さない。
- まずは既存の ENet/UDP packet を opaque datagram として外側から運ぶ。
- `nsmb-net-bridge` は `melonDS <-> localhost UDP <-> WebRTC DataChannel <-> localhost UDP <-> melonDS` のsidecarとして動かす。
- WebRTCはTangoと同じく `libdatachannel` 系を使う。
- 初期のsignaling serverは2人部屋のWebSocket signalingに絞り、マッチメイキング、ランキング、GUI起動管理は後段で作る。

この方針なら、既存のLAN/手動peer対戦で成立しているゲーム同期ロジックを維持したまま、transportだけWAN向けに差し替えられる。

## 構成

```text
melonDS fork
  NSMB direct MvL entry
  input netplay adapter
  deterministic start barrier
  ENet/UDP transport

nsmb-net-bridge
  local UDP endpoint
  WebRTC DataChannel endpoint
  manual copy-paste SDP exchange
  Cloudflare signaling server support

current backend
  Cloudflare Worker + Durable Object signaling
  two-peer session rooms

future desktop launcher
  Tauri + TypeScript UI
  melonDS process management
  bridge process management
  logs/status display

future backend
  matchmaking/lobby
  account/ranking
  match result / replay upload
```

## Phase Status

### Phase 6: Tauri desktop launcher

状態: 初期実装済み。Windows ローカルで Tauri release exe と MSI/NSIS bundle 生成まで確認済み。ROM 生成は Rust crate へ移行し、Tauri command から呼べる状態。GUI backend の start_match コマンド組み立てと fake bridge/fake melonDS による実プロセス起動・停止は unit test で確認済み。起動前preflightで同梱/解決対象のbinary/resourceとbridge signaling smokeをGUIから確認できる。GitHub Actions 上のフル Windows bundle 実行は未確認。

実装:

- `tools/nsmb-mvl-gui`
- `tools/nsmb-mvl-rom`
- Tauri v2 + TypeScript + Vite + pnpm 構成
- GUI から指定できる項目:
  - role: ホスト / 参加
  - 部屋コード
  - シグナリングサーバー URL
  - UDP port
  - host/client ROM path
  - コース: ランダム / 毎回選ぶ
  - 勝利数: 1 / 2 / 3
  - ビッグスター: 3 / 5 / 10
  - 残機: 3 / 5 / 無限
  - マッチシード
- Rust ROM generator crate が stable direct MvL host/client ROM を生成する。
- `scripts/generate-nsmb-mvl-stable-roms.ps1` は Python patch script ではなく `tools/nsmb-mvl-rom` を呼ぶ。
- Tauri command `generate_roms` が base ROM と設定から host/client ROM を生成する。
- Tauri command `generate_roms` の既定出力先は Tauri app data 配下の `roms/` にし、インストール済みアプリでも開発ツリーの `roms/` へ書き込まない。
- GUI に `ROM生成` 操作を追加し、開始前に設定付きROMを作れる。
- GUI の `Course=random` はマッチシードから `stage = seed % 5` を算出し、ROM生成と melonDS 起動時の `MELONDS_NSML_MVL_STAGE` / `MELONDS_NSML_DIRECT_MVL_BOOT_STAGE` に同じ値を渡す。
- Tauri command が `nsmb-net-bridge` を `webrtc-offer` / `webrtc-answer` で起動する。
- Tauri command が melonDS を `MELONDS_NSML_*` 環境変数つきで起動する。
- Tauri command の起動処理は `start_match_resolved` に分離し、fake実行ファイルを使ったtestで bridge/melonDS 相当プロセスのspawn、session状態、停止、melonDS起動失敗時のsession未保存を確認する。
- Tauri command `preflight_check` とGUIの `起動前チェック` を追加し、melonDS binary、bridge binary、bootstrap input、symbols file の解決と、実bridgeの `webrtc-signaling-udp-pair-smoke` を開始前に確認できるようにした。古いbridgeが smoke subcommand を持たない場合も検出する。
- `nsmb-mvl-gui.exe --preflight` を追加し、GUIを開かずに同梱sidecar/resource解決とbridge signaling smokeを検証できるようにした。GitHub Actions の `tauri-windows` でもbundle build後にこのpreflightを実行する。
- Tauri release exe では同梱 sidecar を開発ツリーのbuild成果物より優先して探索する。
- `bundle.externalBin` で fork 済み `melonDS.exe` と `nsmb-net-bridge.exe` を Tauri bundle に同梱する。
- `bundle.resources` で bootstrap input と `tools/nsmb-mvl-rom/resources/symbols9.x` を同梱し、インストール済みアプリでもROM生成と起動用入力script解決ができるようにした。`external/` はGit管理外なので参照元から外した。
- `scripts/prepare-nsmb-mvl-tauri-sidecars.ps1` で Tauri sidecar 名へコピーする。
- `.github/workflows/nsmb-mvl-tauri.yml` を追加し、melonDS build、bridge build、Tauri bundle を分けてつなぐ。
- `.github/workflows/nsmb-mvl-tauri.yml` の `melon-windows` は `melonDS.exe` 自体を source/CMake/vcpkg hash keyed cacheに保存し、cache hit時はvcpkg/CMake buildを飛ばしてartifact uploadへ進む。Windows runner上のmelonDS buildが重すぎる場合の軽減策。
- `.github/workflows/nsmb-mvl-tauri.yml` の `bridge-windows` は Git管理外の `external/tango` に依存しないよう、Tango repositoryを固定commit `283dacf2894d5e47be95a6d7f19acdda63a773b0` で明示checkoutしてから `--features webrtc` buildを行う。`LIBCLANG_PATH` は runner 上の LLVM / Visual Studio BuildTools 候補から `libclang.dll` を探して設定する。release exe の `webrtc-signaling-udp-pair-smoke` も同jobで実行する。
- `.github/workflows/nsmb-mvl-tauri.yml` の `tauri-windows` は artifact sidecar 取り込み後に `cargo test --manifest-path tools/nsmb-mvl-gui/src-tauri/Cargo.toml` を実行し、GUI backend の command/env 組み立てを確認してから Tauri bundle を作る。
- `.github/workflows/nsmb-mvl-gui-local.yml` を追加し、Docker + `act` で軽量な GUI check をローカル実行できるようにした。
- `.github/workflows/nsmb-mvl-gui-local.yml` に `bridge-check` jobを追加し、Tango checkout + Ubuntu build dependencies + `cargo check --features webrtc` + `webrtc-signaling-udp-pair-smoke` をDocker `act` で確認できるようにした。
- `scripts/test-nsmb-mvl-gui-launch-smoke.ps1` を追加し、GUI backend の fake process launch tests、実 `nsmb-net-bridge` の signaling UDP pair smoke、任意の Tauri bundle build を1本で再実行できるようにした。古いbridge executableが `webrtc-signaling-udp-pair-smoke` を持たない場合は検出してdebug bridgeをbuildする。

ローカル確認:

```text
corepack pnpm install: pass
corepack pnpm typecheck: pass
corepack pnpm vite:build: pass
tools/nsmb-mvl-rom cargo check: pass
src-tauri cargo check: pass
cargo test --manifest-path tools/nsmb-mvl-gui/src-tauri/Cargo.toml: pass (10 tests; command/env + fake process launch/stop + preflight bridge smoke validation)
scripts/test-nsmb-mvl-gui-launch-smoke.ps1: pass
scripts/test-nsmb-mvl-gui-launch-smoke.ps1 -BuildTauriBundle: pass (debug/release nsmb-mvl-gui.exe --preflight included)
tools/nsmb-net-bridge/target/release/nsmb-net-bridge.exe webrtc-signaling-udp-pair-smoke: pass
tools/nsmb-mvl-gui/src-tauri/binaries/nsmb-net-bridge-x86_64-pc-windows-msvc.exe webrtc-signaling-udp-pair-smoke: pass
corepack pnpm build: pass
actionlint .github/workflows/nsmb-mvl-tauri.yml .github/workflows/nsmb-mvl-gui-local.yml: pass
act workflow_dispatch -W .github/workflows/nsmb-mvl-tauri.yml -j gui-check -P ubuntu-latest=catthehacker/ubuntu:act-latest: pass (rechecked 2026-05-31 on current diff)
act workflow_dispatch -W .github/workflows/nsmb-mvl-gui-local.yml -j gui-check -P ubuntu-latest=catthehacker/ubuntu:act-latest: pass (rechecked 2026-05-31 on current diff)
act workflow_dispatch -W .github/workflows/nsmb-mvl-gui-local.yml -j bridge-check -P ubuntu-latest=catthehacker/ubuntu:act-latest: pass (rechecked 2026-05-31 on current diff)
cargo run --manifest-path tools/nsmb-mvl-rom/Cargo.toml -- generate-stable ... default symbols path: pass
Tauri release resources/symbols9.x and bootstrap input placement: pass
Tauri sidecars refreshed via scripts/prepare-nsmb-mvl-tauri-sidecars.ps1 after rebuilding release bridge: pass
tools/nsmb-mvl-gui/src-tauri/target/release/nsmb-mvl-gui.exe --preflight: pass
```

生成物:

```text
tools/nsmb-mvl-gui/src-tauri/target/release/nsmb-mvl-gui.exe
tools/nsmb-mvl-gui/src-tauri/target/release/bundle/msi/NSMB Mario vs Luigi Online_0.1.0_x64_en-US.msi
tools/nsmb-mvl-gui/src-tauri/target/release/bundle/nsis/NSMB Mario vs Luigi Online_0.1.0_x64-setup.exe
```

現在の注意点:

- ROM はまだ bundle に同梱しない。ユーザーが ROM path を指定する。
- 既定の生成ROMとログは Tauri app data 配下へ保存する。開発ツリーに `roms/nsmb-us.nds` がある場合だけ base ROM の既定値として使う。
- `DEFAULT_SIGNAL_URL` は placeholder の `wss://example.workers.dev/session`。実運用 URL は GUI で変更するか `NSMB_MVL_SIGNAL_URL` で差し替える。
- full workflow は Windows runner と vcpkg/melonDS build を使うため、ローカル Docker `act` では frontend/ROM generator/bridge smoke の軽量workflowを検証対象にしている。Windows full workflowは実GitHub runnerでの確認が必要。現作業ツリーは未pushのため、実runner確認はpush/PRまたはworkflow_dispatch可能なremote branch作成後に行う。
- `Course=select` / 通常 MvL の `Choose Each Time` は direct route が CourseSelect を飛ばすため未対応。GUI/CLIでは選択肢として保持するが、実行時は fixed stage 扱いへ落とす。
- `Course=random` は起動前に選んだコースでROMを作る。現checkpoint restart方式では2ゲーム目以降も同じコースへ戻り、ゲームごとの再抽選は未対応。

次アクション:

- GitHub Actions の full Windows Tauri build を実 runner で確認する。
- GUI から実際に host/client を起動し、signaling server 経由で2PC接続を確認する。

### Phase 4: Cloudflare signaling server

状態: 実装済み、ローカル型チェック/リンティング確認済み。Cloudflare デプロイはユーザー側で完了。実サーバー経由の2PC接続確認は未実施。

実装:

- `tools/nsmb-signaling-server`
- pnpm standalone project
- TypeScript + Biome
- Alchemy based Cloudflare Worker definition
- Durable Object per `session`
- WebSocket endpoint:
  - `/session?session=<room_id>&role=offer`
  - `/session?session=<room_id>&role=answer`
- 1 room につき `offer` 1接続 + `answer` 1接続
- `sdp` / `candidate` JSON message relay
- `/health`
- GitHub Actions:
  - push/PR は typecheck + Biome
  - deploy は `main` push 時に自動実行する
  - deploy stage は `prod` を明示する: `pnpm run deploy --stage prod`

ローカル確認:

```powershell
cd tools\nsmb-signaling-server
corepack pnpm install
corepack pnpm typecheck
corepack pnpm format-and-lint
corepack pnpm format-and-lint:fix
corepack pnpm run ci
```

確認済み結果:

```text
tsc --noEmit: pass
biome check .: pass
biome check . --write: no fixes applied
```

注意:

- Cloudflare への deploy はユーザーが行う。こちらでは `pnpm deploy` / `alchemy deploy` を実行しない。
- `DEFAULT_ICE_SERVERS` は comma-separated STUN/TURN URI list。未指定時は `stun:stun.l.google.com:19302`。
- 初期実装は signaling のみ。matchmaking、account、ranking、result upload は未実装。

### Phase 5: nsmb-net-bridge signaling integration

状態: 実装済み、Rust通常check/WebRTC feature check確認済み。signaling対応を含むreleaseビルド作成済み。ローカルin-process signaling smokeで WebSocket signaling 経由の offer/answer SDP 交換と DataChannel payload 到達を確認済み。実サーバー経由の疎通確認は未実施。

実装:

- 既存の `webrtc-offer` / `webrtc-answer` 手動コピペモードは維持。
- `--signal URL --session ID` を指定した場合だけ WebSocket signaling を使う。
- signaling server から受け取った `iceServers` を、`--stun` 未指定時の WebRTC config として使う。
- SDP は base64 ではなく JSON string として server 経由で中継する。
- `webrtc-signaling-loopback-smoke` で、同一プロセス内のローカルWebSocket signaling serverを通して実際の offer/answer signaling path と DataChannel payload 受信を検証できる。
- `webrtc-signaling-udp-pair-smoke` で、同じ signaling path に加えて host/client 相当の2つの UDP socket から WebRTC tunnel 経由の双方向payload到達を検証できる。

起動例:

```powershell
.\tools\nsmb-net-bridge\target\debug\nsmb-net-bridge.exe webrtc-offer --local-bind 127.0.0.1:0 --local-target 127.0.0.1:8165 --signal wss://<worker-host>/session --session test-room
.\tools\nsmb-net-bridge\target\debug\nsmb-net-bridge.exe webrtc-answer --local-bind 127.0.0.1:8265 --signal wss://<worker-host>/session --session test-room
```

最新releaseビルド:

```powershell
.\tools\nsmb-net-bridge\target\release\nsmb-net-bridge.exe
```

最小ROM同梱テストkit:

```text
dist/nsmb-mvl-webrtc-test-kit-rom-20260531-054144.zip
```

- 目的: 別PCへ素早くコピーして WebRTC signaling 経由の対戦検証を行うための一時成果物。
- 内容: release `melonDS.exe`、release `nsmb-net-bridge.exe`、host/client安定ROM、起動用PowerShell wrapper、既存manual peer script。
- 注意: ROM同梱の自分用検証zip。第三者へ再配布しない。
- 確認: 同梱 `nsmb-net-bridge.exe webrtc-loopback-smoke` pass。

ローカル確認:

```powershell
cd tools\nsmb-net-bridge
$env:Path="$env:USERPROFILE\.cargo\bin;$env:Path"
$env:LIBCLANG_PATH="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin"
cargo fmt --check
cargo check
cargo check --features webrtc
cargo build --features webrtc
cargo run --features webrtc -- webrtc-signaling-udp-pair-smoke
```

確認済み結果:

```text
cargo fmt --check: pass
cargo check: pass
cargo check --features webrtc: pass
cargo build --features webrtc: pass
cargo build --release --features webrtc: pass
tools\nsmb-net-bridge\target\debug\nsmb-net-bridge.exe webrtc-signaling-udp-pair-smoke: pass
webrtc-signaling-udp-pair-smoke via Docker act bridge-check: pass
```

### Future: 本番WAN向け signaling / WebRTC hardening

状態: 未実装。現時点の Cloudflare signaling は、手動コピー&ペーストをなくすための軽い動作検証用 PoC として扱う。

本格的にWAN対戦へ進める場合は、Tango の設計に寄せて以下を強化する。

- signaling protocol:
  - JSON暫定protocolから、protobuf等の明確なwire protocolへ移行するか判断する。
  - protocol versionを持たせ、古いclient/新しいserverの不一致を明示的に拒否する。
  - abort reason / error codeを定義し、missing session、duplicate role、version mismatch、invalid packet、timeoutを区別する。
- connection lifecycle:
  - ping / pong intervalとread timeoutを入れる。
  - WebSocket切断、peer不在、answer未到着、WebRTC failed/disconnected/closedを区別してログに出す。
  - offer/answer交換後にsignaling socketを閉じるか、状態監視用に残すかを決める。
  - stale session / stale WebSocket cleanupを明示する。
- room/session semantics:
  - 現在の `role=offer|answer` 明示方式を継続するか、Tango のように最初の `start` をoffererとして扱う方式に寄せるか判断する。
  - duplicate接続時の挙動を明確化する。古い接続を落とすのか、新しい接続を拒否するのかを決める。
  - session idの長さ、文字種、有効期限、推測困難性を見直す。
- ICE/TURN:
  - Cloudflare TURN credential発行を追加する。
  - `DEFAULT_ICE_SERVERS` だけでなく、短命credentialつきTURNをserverから配布する。
  - TURN over TCPなど、`libdatachannel` 側で問題があるtransportをfilterする。
  - relay強制モード / STUN-onlyモードをbridge CLIから選べるようにする。
- bridge CLI / diagnostics:
  - signaling URL、session、role、ICE server、connection stateを整理してログ出力する。
  - machine-readableな接続結果ログを追加し、GUI/Tauriから状態を拾いやすくする。
  - packet stats、disconnect reason、WebRTC state transitionを保存する。
  - signaling server経由の自動smoke testを追加する。
- security / abuse:
  - sessionごとの最大接続数、message size上限、rate limitを入れる。
  - 必要なら簡易tokenや署名つきsessionを導入する。
  - SDPやICE情報を長期保存しない方針を明記する。
- deployment / operations:
  - GitHub Actionsのdeploy jobは `main` push 自動実行と `main` 手動dispatchを維持しつつ、必要なsecrets/varsをREADMEに明記する。
  - staging/productionを分けるか判断する。
  - Cloudflare logsで接続失敗理由を追えるようにする。

### Repository / branch policy

状態: 方針決定。`main` は `uniunitaro/nsmb-mvl-online` の本線として扱う。

推奨:

- `upstream/master`: melonDS公式追従用。基本的に直接変更しない。
- `main`: このfork/独自プロダクトの本線。`master` から作成し、NSMB online向け作業ブランチをmergeする。
- `feature/*` or `codex-*`: 個別作業ブランチ。
- 自分のGitHub remoteを `origin` (`https://github.com/uniunitaro/nsmb-mvl-online.git`)、公式melonDS remoteを `upstream` にする。
- GitHub Actionsのsignaling deployは `main` push と `main` 手動dispatchに限定する。PRではCIのみ。
- エージェントはユーザーがその都度明示的に依頼した場合だけ `git push` する。ローカルcommitとpushは分けて扱う。

理由:

- melonDS公式履歴に近い `master` を温存できる。
- `main` はNSMB online向けのアプリ、signaling server、bridge、docsを含む統合ブランチとして扱える。
- GitHub Actionsのsignaling deployは `main` push と `main` 手動dispatchに限定し、`upstream/master` 追従作業で誤deployしない。
- `main` pushでdeployが走るため、エージェントの自動pushを禁止して意図しないdeployを避ける。

## 1PC auto smoke FPS investigation

Status: investigated on 2026-05-30.

The 1PC automated WebRTC smoke is useful as a connectivity/regression smoke, but it is not a reliable FPS benchmark. The user's LAN 2PC WebRTC run reached normal 60fps, while the same-machine automated runs varied heavily depending on harness and machine load.

Observed results:

- Earlier direct ENet 1PC comparison: about 57.6fps total / 53.3fps active.
- Release WebRTC 1PC smoke with the original no-drain PowerShell harness: previously reproduced about 27fps total / 18.5fps active, but a later rerun was about 42.6fps total / 33.2fps active.
- Release WebRTC 1PC smoke with bridge stdout/stderr actively drained: about 52.9fps total / 39.8fps active.
- UDP sidecar bridge without WebRTC: about 40.5fps total / 34.9fps active.
- Direct ENet under the same Python/Start-Process orchestration during the investigation also fell to about 42fps total / 33-42fps active.

Interpretation:

- The original 27fps result was not caused by actual game input waiting. In the representative WebRTC runs, `remoteWaitCount` and `throttleCount` were often zero or small while FPS still dropped.
- Rust debug vs release was not the main cause; release WebRTC could still run slowly under the 1PC harness.
- The strongest cause is the 1PC automated harness itself: two melonDS processes, two sidecar bridge processes, PowerShell/Python orchestration, redirected output, hidden windows, and same-machine scheduler/GPU contention. Not draining bridge output made the measurement worse and less reproducible, but it is not the only factor.
- Because real LAN 2PC WebRTC reached 60fps, FPS decisions should be based on manual/LAN 2PC or a dedicated benchmark harness, not this 1PC auto smoke.

Policy:

- Keep 1PC WebRTC auto smoke for connection, start barrier, disconnect, timeout, and log regression checks.
- Do not treat 1PC auto smoke FPS as representative of real play.
- For FPS validation, use LAN 2PC WebRTC or a dedicated harness that:
  - actively drains bridge stdout/stderr,
  - uses unique ports,
  - kills stale melonDS/bridge processes before the run,
  - records CPU/process load,
  - stores bridge stats together with melonDS logs.

### Phase 1: Transport境界の整理

状態: 完了

- 既存のmelonDS側は、当面 ENet/UDP を使う。
- sidecarはmelonDSから見ると単なる相手UDP endpointとして振る舞う。
- これにより、melonDS本体にWebRTC依存を入れない。

### Phase 2: Rust bridge最小PoC

状態: 実装済み、ビルド確認済み

実装:

- `tools/nsmb-net-bridge`
- Rust CLI
- `udp` mode
- packet count/byte countログ
- local target自動学習

通常ビルド確認:

```powershell
cd tools\nsmb-net-bridge
$env:Path="$env:USERPROFILE\.cargo\bin;$env:Path"
cargo build
```

単体起動例:

```powershell
.\tools\nsmb-net-bridge\target\debug\nsmb-net-bridge.exe udp --local-bind 127.0.0.1:0 --local-target 127.0.0.1:8165 --bridge-bind 127.0.0.1:9001 --bridge-peer 127.0.0.1:9002
.\tools\nsmb-net-bridge\target\debug\nsmb-net-bridge.exe udp --local-bind 127.0.0.1:8265 --bridge-bind 127.0.0.1:9002 --bridge-peer 127.0.0.1:9001
```

### Phase 3: サーバーなしWebRTC接続

状態: 実装済み、ビルド確認済み。ローカルDataChannel smoke確認済み。1PC 2プロセスのWebRTC bridge + UDP往復確認済み。1PC上の `melonDS host -> WebRTC bridge -> melonDS client` 実プレイsmoke確認済み。実WAN検証は未実施。

実装:

- `webrtc-offer` mode
- `webrtc-answer` mode
- 手動コピー&ペーストのbase64 SDP交換
- STUN指定対応
- デフォルトSTUN: `stun:stun.l.google.com:19302`
- unreliable + unordered DataChannel
- local UDP <-> DataChannel relay
- `webrtc-loopback-smoke` による同一プロセス内DataChannel疎通確認

WebRTC feature付きビルド確認:

```powershell
cd tools\nsmb-net-bridge
$env:Path="C:\Strawberry\perl\bin;$env:USERPROFILE\.cargo\bin;$env:Path"
$env:LIBCLANG_PATH="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin"
cargo build --features webrtc
```

ローカルWebRTC smoke確認:

```powershell
.\tools\nsmb-net-bridge\target\debug\nsmb-net-bridge.exe webrtc-loopback-smoke
```

確認済み結果:

```text
nsmb-net-bridge webrtc: connection state Connected
nsmb-net-bridge webrtc: connection state Connected
nsmb-net-bridge webrtc: loopback smoke passed
```

1PC 2プロセスWebRTC bridge確認:

```text
offer bridge:
  local-bind 127.0.0.1:9101
  local-target 127.0.0.1:9103

answer bridge:
  local-bind 127.0.0.1:9102
  local-target 127.0.0.1:9104

UDP relay:
  127.0.0.1:9101 -> WebRTC -> 127.0.0.1:9104
  127.0.0.1:9102 -> WebRTC -> 127.0.0.1:9103
```

確認済み結果:

```text
WEBRTC_TWO_PROCESS_UDP_SMOKE=PASS
got1=offer-to-answer
got2=answer-to-offer
```

1PC WebRTC bridge経由のmelonDS実プレイsmoke:

```text
bridge:
  offer local-bind 127.0.0.1:9001
  offer local-target 127.0.0.1:8165
  answer local-bind 127.0.0.1:8265

melonDS:
  host   -Peer 127.0.0.1 -Port 8165 -Frames 1800
  client -Peer 127.0.0.1 -Port 8265 -Frames 1800
```

確認済みログ:

```text
host:   NSMB PoC: peer connected
client: NSMB PoC: peer connected
host:   NSMB InputNetplay: remote start ready accepted remoteFrame=870 localFrame=870
client: NSMB InputNetplay: remote start ready accepted remoteFrame=870 localFrame=870
host:   NSMB Test: frame limit reached at frame=1800
client: NSMB Test: frame limit reached at frame=1800
```

ログ位置:

```text
logs/webrtc-melonds-1pc-20260530-051459/host/host.stdout.txt
logs/webrtc-melonds-1pc-20260530-051459/client/client.stdout.txt
```

注意:

- この検証は自動bootstrap入力によるsmokeで、手動操作の快適性確認ではない。
- `remote input timeout` / `peer disconnected` は今回の該当ログでは出ていない。
- 1PC内にmelonDS host/clientとWebRTC bridge 2本を同居させた自動smokeでは約27fpsだった。直接ENet接続の同条件比較では全体約57fps、active約53fpsだったため、1PC集約時のbridge追加負荷/スケジューリング/検証ハーネス条件が疑わしい。
- ユーザーのLAN 2PC検証では、WebRTC bridge経由でも通常どおり60fpsが出た。したがって、1PC自動smokeの27fpsは実運用条件を代表していない可能性が高い。

直接ENet接続の比較ログ:

```text
logs/direct-melonds-1pc-fps-compare-20260530-051854/host/host.stdout.txt
logs/direct-melonds-1pc-fps-compare-20260530-051854/client/client.stdout.txt

host:   frame limit reached at frame=1200 elapsedMs=20809 fps=57.67
host:   active fps startFrame=990 frames=210 elapsedMs=3865 fps=54.33
client: frame limit reached at frame=1200 elapsedMs=20859 fps=57.53
client: active fps startFrame=990 frames=210 elapsedMs=3937 fps=53.34
```

必要ツール:

- Rustup/Rust
- Visual Studio 2022 Build Tools
- CMake
- Strawberry Perl
- `LIBCLANG_PATH` に VS Build Tools 付属の `libclang.dll` ディレクトリを指定

手動WebRTC起動例:

Offer側:

```powershell
.\tools\nsmb-net-bridge\target\debug\nsmb-net-bridge.exe webrtc-offer --local-bind 127.0.0.1:0 --local-target 127.0.0.1:8165
```

Answer側:

```powershell
.\tools\nsmb-net-bridge\target\debug\nsmb-net-bridge.exe webrtc-answer --local-bind 127.0.0.1:8265
```

使い方:

1. Offer側を起動して、表示されたoffer SDP base64をAnswer側へ貼る。
2. Answer側が表示したanswer SDP base64をOffer側へ貼る。
3. `connected` になったらmelonDSを起動する。
4. Answer側のmelonDSはbridgeに向けるため、1PC検証なら `-Peer 127.0.0.1 -Port 8265` で起動する。
5. 2PC検証ではPCが分かれるので、Answer側bridgeもmelonDSも標準の `8165` を使ってよい。

melonDS手動起動例:

```powershell
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role host -Peer 127.0.0.1
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role client -Peer 127.0.0.1 -Port 8265
```

注意:

- Phase 3時点ではsignaling serverなしだったため、接続確立は手動コピー&ペーストだった。現在は Phase 4/5 で Cloudflare signaling server と `--signal` mode を追加済み。
- STUNだけで直結できないNAT環境では接続できない可能性がある。
- TURN fallbackは未実装。
- まだWAN実測、jitter/loss統計、実プレイ安定性検証は未完了。

## 次にやること

1. ユーザー側で `tools/nsmb-signaling-server` を Cloudflare に deploy する。
2. deploy された Worker URL で `nsmb-net-bridge webrtc-offer/webrtc-answer --signal ... --session ...` を2PC実行し、手動コピー&ペーストなしで接続できるか確認する。
3. LAN 2PCで signaling server 経由の WebRTC bridge 手動対戦ログを取り、FPS/timeout/packet statsを正式に記録する。
4. WAN 2PCでSTUNのみの直結率、ping、jitter、packet lossを測る。
5. 必要なら `DEFAULT_ICE_SERVERS` に TURN を追加し、TURN fallback を検証する。
6. 接続確認後、Tauri launcher で melonDS/bridge process 管理とログ表示を作る。

## 将来方針

### Desktop GUI

Tauri + TypeScriptを本命にする。

- UIはTypeScriptで作る。
- Rust製bridgeを将来crateとして直接統合できる。
- 初期はCLIの`nsmb-net-bridge.exe`をspawnするだけでよい。

### Backend

Cloudflare Workers + Durable Objects + TypeScriptを本命にする。

初期:

- WebSocket signaling
- session_id による2人接続
- ICE server配布
- SDP offer/answer交換

将来:

- lobby/matchmaking
- account/ranking
- result upload
- replay/input log保存
- TURN relay region選択
