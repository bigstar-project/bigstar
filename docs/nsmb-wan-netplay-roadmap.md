# NSMB Mario vs Luigi WAN Netplay Roadmap

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

### Phase 4: Cloudflare signaling server

状態: 実装済み、ローカル型チェック/リンティング確認済み。Cloudflare デプロイと実サーバー経由の2PC接続確認は未実施。

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
  - deploy stage は `prod` を明示する: `pnpm run deploy -- --stage prod`

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

状態: 実装済み、Rust通常check/WebRTC feature check確認済み。実サーバー経由の疎通確認は未実施。

実装:

- 既存の `webrtc-offer` / `webrtc-answer` 手動コピペモードは維持。
- `--signal URL --session ID` を指定した場合だけ WebSocket signaling を使う。
- signaling server から受け取った `iceServers` を、`--stun` 未指定時の WebRTC config として使う。
- SDP は base64 ではなく JSON string として server 経由で中継する。

起動例:

```powershell
.\tools\nsmb-net-bridge\target\debug\nsmb-net-bridge.exe webrtc-offer --local-bind 127.0.0.1:0 --local-target 127.0.0.1:8165 --signal wss://<worker-host>/session --session test-room
.\tools\nsmb-net-bridge\target\debug\nsmb-net-bridge.exe webrtc-answer --local-bind 127.0.0.1:8265 --signal wss://<worker-host>/session --session test-room
```

ローカル確認:

```powershell
cd tools\nsmb-net-bridge
$env:Path="$env:USERPROFILE\.cargo\bin;$env:Path"
$env:LIBCLANG_PATH="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin"
cargo fmt --check
cargo check
cargo check --features webrtc
```

確認済み結果:

```text
cargo fmt --check: pass
cargo check: pass
cargo check --features webrtc: pass
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
