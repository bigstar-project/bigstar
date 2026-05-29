# NSMB Mario vs Luigi WAN Netplay Roadmap

## 目的

New Super Mario Bros. DS の `Mario vs Luigi` を、最終的に一般ユーザーがポート開放なしで WAN 越しに対戦できる形へ持っていく。

既存の `docs/nsmb-mario-vs-luigi-online-poc.md` は melonDS/ROM patch/input sync のPoC履歴と検証状態を管理する。この文書は、WAN実用化、WebRTC sidecar、デスクトップGUI、Cloudflare backend を含む今後の製品寄り方針を管理する。

## 採用方針

当面の推奨構成は次の通り。

```text
Cloudflare backend
  signaling
  matchmaking
  lobby
  account/ranking
  match result / replay upload

Desktop GUI
  Tauri + TypeScript UI
  Rust backend commands
  melonDS起動管理
  net bridge起動管理
  logs/status表示

Net bridge
  Rust CLI / Rust crate
  WebRTC DataChannel
  STUN/TURN
  local UDP or IPC bridge
  ping/jitter/loss/relay stats

melonDS fork
  NSMB direct MvL entry
  input netplay adapter
  deterministic start barrier
  ENet or local bridge transport for development
```

## 責務分離

- `melonDS fork`: ゲーム開始、入力同期、ROM patch、状態検証を担当する。
- `nsmb-net-bridge`: WAN transportを担当する。melonDSからは localhost の相手として見えるようにする。
- `nsmb-launcher`: GUI、設定、マッチメイキング、melonDS/bridgeの起動管理を担当する。
- `Cloudflare backend`: signaling、matchmaking、room、ranking、result保存を担当する。

重要なのは、ゲーム同期の問題とWAN接続の問題を分けること。既存PoCで動いている入力同期経路を壊さず、transportだけを差し替えられる形にする。

## 技術選定

### Net Bridge

Rustで作る。

理由:

- TangoがRust + libdatachannel構成で参考にしやすい。
- Windows向け単体exe配布がしやすい。
- tokioでWebSocket signaling、local UDP/IPC、WebRTC event loopを扱いやすい。
- バイナリpacket処理、低遅延処理、ログ出力に向いている。
- 後でTauri backendへcrateとして統合できる。

### Desktop GUI

Tauri + TypeScriptを本命にする。

理由:

- UIはTypeScriptで作れる。
- Rust製bridgeとの相性が良い。
- Electronより軽い。
- 最初は `nsmb-net-bridge.exe` をspawnし、後でRust crateとして直接組み込む選択肢を残せる。

Electronは、Web UI開発速度や既存Node資産を強く優先する場合の候補。ただし今回のWebRTC/DataChannel実装はRust bridgeに寄せるため、Electronを使う積極的な理由は現時点では弱い。

### Backend

Cloudflare Workers + Durable Objects + TypeScriptを本命にする。

初期:

- session_id による2者matching
- WebSocket signaling
- ICE server配布
- SDP offer/answer交換

将来:

- D1: users, ratings, match results
- R2: replay/input log保存
- Durable Objects: lobby, room, active match
- Queues: result validation
- Analytics: ping, region, TURN relay率

## WebRTC方針

Tangoと同様に、WebRTC DataChannelを使う。

ICE server方針:

- 通常はSTUNでP2P直結を狙う。
- 直結できない場合はTURN relayにfallbackする。
- 実用化時は日本国内/近隣regionにTURNを用意する。TURN relayは遅延が増えるため、対戦品質表示に反映する。

DataChannelは用途別に分ける。

```text
reliable ordered channel:
  handshake
  ROM hash
  patch version
  match seed
  start-ready
  settings sync
  disconnect reason

unreliable unordered channel:
  per-frame input
  input bundle history
  ping
  jitter/loss/frame lead stats
```

毎フレーム入力は reliable に寄せない。古いpacket再送で新しい入力まで詰まるのを避けるため、unreliable + unordered + 過去入力bundleを基本にする。

## 段階的実装

### Phase 1: Transport境界を整理

- melonDS側の現在のENet依存を整理し、transport境界を明確にする。
- 既存ENet経路はLAN/デバッグ用に残す。
- localhost bridge向けの送受信口を追加する。
- packet formatを文書化する。

### Phase 2: Rust bridgeの最小PoC

- `nsmb-net-bridge` CLIを作る。
- まず localhost relay として動作させる。
- melonDS同士を `melonDS -> bridge -> bridge -> melonDS` でつなぎ、ENet直結と同じ結果になるか確認する。
- ping、packet count、loss、jitter、frame leadをログに出す。

### Phase 3: WebRTC接続

- Tangoの `datachannel-wrapper` / `tango-signaling` 構成を参考にする。
- signaling serverでsession_id接続する。
- STUNのみでP2P直結を確認する。
- DataChannel上でPhase 2と同じpacket relayを行う。

### Phase 4: TURN fallback

- Cloudflare TURNまたはcoturn等を検証する。
- `relay=auto/off/force` を持たせる。
- P2P直結時とTURN relay時のping/jitterを計測する。
- `InputDelayFrames=4` で国内WANが現実的か確認する。

### Phase 5: Tauri launcher

- ROM/patch確認。
- melonDSとbridgeの起動管理。
- session作成/参加。
- 接続状態、P2P/TURN、ping/jitter、入力遅延、FPSを表示。
- 対戦終了後にログ/結果を保存する。

### Phase 6: Matchmaking / Ranking

- accountまたは軽量identityを導入する。
- lobby/matchmakingを作る。
- result uploadとrating更新を作る。
- 入力ログ、ROM/patch hash、match seed、簡易state hashを保存し、最低限の検証可能性を持たせる。

## 現在の優先順位

1. 既存PoCの低遅延入力同期を壊さない。
2. melonDS内の通信経路をtransport抽象化し、ENetとlocalhost bridgeを差し替え可能にする。
3. Rust bridgeのlocalhost relay PoCを作る。
4. WebRTC DataChannelをbridge間に入れる。
5. WAN実測で4F delayの実用性を見る。

## 未決事項

- melonDSとbridge間のlocal transportを UDP にするか TCP/pipe にするか。
- WebRTC crateはTangoと同じ `datachannel` / libdatachannel を使うか、別候補を使うか。
- signaling serverをTangoの構造からfork/流用するか、新規で作るか。
- TURN providerをCloudflareにするか、自前coturnにするか。
- Tauriへbridgeをいつ統合するか。初期は必ずCLI bridgeとして独立させる。

## 次にやること

- `nsmb-net-bridge` の最小仕様を決める。
- melonDS側の現在のENet packet送受信箇所を整理し、bridge transportに必要なAPIを洗い出す。
- localhost relayのみで既存LAN相当の手動対戦が動くかを最初の実装目標にする。
