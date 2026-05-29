# NSMB Mario vs Luigi WAN Netplay Roadmap

## 目的

New Super Mario Bros. DS の `Mario vs Luigi` を、最終的に一般ユーザーがポート開放なしで WAN 越しに対戦できる形へ持っていく。

`docs/nsmb-mario-vs-luigi-online-poc.md` は melonDS/ROM patch/input sync のPoC履歴と検証状態を扱う。この文書は WAN transport、WebRTC sidecar、将来のGUI/バックエンド方針を扱う。

## 現在の採用方針

当面は GUI とマッチメイキングサーバーを後回しにし、サーバーなしの手動コピー&ペースト signaling で WebRTC DataChannel が使えるかを確認する。

重要な設計判断:

- melonDS本体の入力同期実装はすぐに壊さない。
- まずは既存の ENet/UDP packet を opaque datagram として外側から運ぶ。
- `nsmb-net-bridge` は `melonDS <-> localhost UDP <-> WebRTC DataChannel <-> localhost UDP <-> melonDS` のsidecarとして動かす。
- WebRTCはTangoと同じく `libdatachannel` 系を使う。
- マッチメイキング、ランキング、GUI起動管理は後段で作る。

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
  later: signaling server support

future desktop launcher
  Tauri + TypeScript UI
  melonDS process management
  bridge process management
  logs/status display

future backend
  signaling
  matchmaking/lobby
  account/ranking
  match result / replay upload
```

## Phase Status

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

状態: 実装済み、ビルド確認済み。ローカルDataChannel smoke確認済み。実WAN/手動プレイ検証は未実施。

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

- 現時点ではsignaling serverなしなので、接続確立は手動コピー&ペースト。
- STUNだけで直結できないNAT環境では接続できない可能性がある。
- TURN fallbackは未実装。
- まだWAN実測、jitter/loss統計、実プレイ安定性検証は未完了。

## 次にやること

1. `nsmb-net-bridge` のWebRTC手動接続を1PC内2プロセスで確認する。
2. 1PC内で `melonDS -> bridge -> WebRTC -> bridge -> melonDS` の疎通を確認する。
3. LAN 2PCでWebRTC bridge経由の手動対戦を確認する。
4. WAN 2PCでSTUNのみの直結率、ping、jitter、packet lossを測る。
5. 必要ならTURN fallbackを追加する。
6. 実用化段階でsignaling server、matchmaking、launcherへ進む。

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
