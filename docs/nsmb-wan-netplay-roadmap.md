# NSMB Mario vs Luigi WAN Netplay Roadmap

## Current work: bridge hash match identity - 2026-07-01

- User request: the matchmaking path already checks the generated ROM pair identity before launch; add bridge binary hash exchange/checking to the same pre-match identity check.
- Implemented:
  - Tauri ROM preparation now computes the current `nsmb-net-bridge` sidecar SHA-256 and stores it as `rom_identity.bridge_sha256`.
  - `rom_pair_id` now includes `generator_id`, host ROM SHA-256, client ROM SHA-256, and bridge SHA-256, so bridge mismatch fails the existing room join identity check before melonDS/bridge launch.
  - Existing reusable ROMs do not need to be regenerated just because the bridge changed. The launcher recomputes the identity from current ROM hashes plus current bridge hash and refreshes the sidecar marker manifest.
  - The signaling server schema now requires `bridge_sha256` in `rom_identity`, and join mismatch errors now report `match identity mismatch`.
  - GUI mismatch copy now says `ROMまたはbridgeが相手と一致しません`.
- Verification passed:
  - `cargo fmt --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml`
  - `cargo test --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml`
  - `cargo clippy --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml --all-targets -- -D warnings`
  - `corepack pnpm run ci` in `tools\nsmb-signaling-server`
  - `corepack pnpm run ci` in `tools\nsmb-mvl-gui`
- Current blocker: deployed clients and signaling server must be updated together because new room payloads require `bridge_sha256`.
- Next actions:
  - Rebuild/sync GUI sidecars and deploy the signaling Worker before relying on this check in real matchmaking.

## Current work: Tauri CI bridge/Tauri build speedup - 2026-06-03

- User request: real GitHub Actions history shows `bridge` and `tauri` are long; optimize the completed-run bottlenecks, not the currently running action.
- Actions measurement, converted from GitHub UTC timestamps to JST:
  - Completed `NSMB MvL Tauri` run `26806041965` ran from 2026-06-02 16:47:47 JST to 2026-06-02 17:11:44 JST, about 24 minutes.
  - `bridge Windows x86_64` ran from 16:47:50 JST to 17:00:32 JST, about 12m42s. The `Build bridge` step was 16:48:27 JST to 17:00:25 JST, about 11m58s.
  - `Tauri Windows x86_64` ran from 17:00:35 JST to 17:11:43 JST, about 11m08s. `Test Tauri backend` was about 3m05s and `Build Tauri app` was about 6m36s.
  - In `Build Tauri app`, the release Rust build itself was about 5m28s, followed by WiX/NSIS downloads and bundling.
- Implemented CI changes in `.github/workflows/nsmb-mvl-tauri.yml`:
  - Added `Swatinem/rust-cache@v2` for `tools/nsmb-net-bridge -> target` so bridge dependency/native rebuilds, especially `datachannel-sys`, `openssl-src`, and their Rust dependency graph, can be reused across runs.
  - Added `Swatinem/rust-cache@v2` for `tools/nsmb-mvl-gui/src-tauri -> target` so Tauri/Rust dependencies can be reused across runs.
  - Changed `Test Tauri backend` to `cargo test --release` so the test step warms the same release target used by `pnpm tauri build` instead of spending about 3 minutes on a separate debug target.
  - Added an `actions/cache@v4` cache for `~\AppData\Local\tauri` to avoid repeated WiX/NSIS helper downloads during Tauri bundling.
  - Removed the stale Tango checkout from `bridge-windows`; the bridge now uses tracked `tools/nsmb-net-bridge/datachannel-wrapper` through its Cargo path dependency.
- Expected effect:
  - First remote run mainly seeds caches, so the biggest improvement should appear on the second run with cache hits.
  - Bridge should drop the most because most of its 11m58s is dependency/native compilation.
  - Tauri should drop by reusing release dependencies and avoiding the debug/release split. Bundling and local app crate rebuild still remain.
- Verification status:
  - Workflow YAML parsed successfully locally.
  - `cargo test --release --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml` passed locally.
  - `cargo metadata --manifest-path tools\nsmb-net-bridge\Cargo.toml --features webrtc --no-deps --format-version 1` confirmed the bridge resolves `datachannel-wrapper` from tracked `tools\nsmb-net-bridge\datachannel-wrapper`.
  - A local `cargo check --release --features webrtc --manifest-path tools\nsmb-net-bridge\Cargo.toml` was stopped after 5 minutes because it entered the same long native dependency build this CI change is meant to cache.
  - Remote runtime improvement still needs a pushed cache-seeding run and a following cache-hit run. Do not push automatically without explicit user request.
- Current blocker: remote cache-hit timings cannot be measured until the change is pushed and at least two relevant Tauri CI runs complete.
- Next actions:
  - After user-approved push, compare `bridge Windows x86_64` and `Tauri Windows x86_64` step durations on the first and second runs.

## Current work: WebRTC WAN diagnostics and connection audit - 2026-06-03

- Reported symptom: 2PC on the same Wi-Fi had connected successfully, but a WAN test with a remote friend stopped after `nsmb-net-bridge signaling: {"peerCount":2,"role":"answer","type":"peer-joined"}`. This proves the signaling WebSocket reached the room join step; the likely failure area is subsequent SDP/ICE setup or NAT traversal.
- Fixed a bridge lifecycle bug: the established `PeerConnection` was dropped when the bridge moved only its `DataChannel` into the UDP tunnel. The bridge now retains the `PeerConnection` and continues consuming WebRTC state events for the full tunnel lifetime.
- Added a tracked local `tools/nsmb-net-bridge/datachannel-wrapper` derived from the Tango wrapper so the bridge can expose ICE state events, selected candidate pair, selected local/remote addresses, and larger diagnostic event queues without depending on the gitignored `external/tango` checkout at runtime.
- Added verbose bridge diagnostics:
  - native libdatachannel debug logging through `env_logger`
  - signaling phase changes, SDP text/base64, ICE servers and their source, ICE/WebRTC state transitions, gathered candidate types, selected candidate pair, selected route, UDP packet statistics, and terminal errors
  - optional `--status-file PATH` machine-readable snapshot output; Tauri launches write `bridge-status.json` in the per-run log directory
- Added route classification for GUI and logs:
  - `local`: private/loopback/link-local `host -> host`
  - `direct`: public address `host -> host`, including public IPv6 direct connections
  - `stun`: selected `srflx` / `prflx` candidate path
  - `turn-relay`: selected `relay` candidate path
- Fixed the STUN fallback mismatch:
  - signaling server `parseIceServers()` now returns `stun:stun.l.google.com:19302` when `DEFAULT_ICE_SERVERS` is unset or blank
  - bridge signaling mode independently falls back to the same STUN URI if an older deployed server returns an empty `iceServers` list
  - a live WebSocket probe confirmed that the currently deployed Worker still returns `iceServers: []`; redeploying is still required to activate the Worker-side fallback and new server logs
- Added Cloudflare signaling logs for room join, queued relay, direct relay, queued-signal flush, and close events without logging full SDP payloads server-side.
- Tauri GUI now shows WebRTC phase, ICE/WebRTC state, selected route, candidate types, selected addresses, configured ICE server list, packet counters, and last bridge error. It also has a `ログを開く` button that opens the selected app-owned run directory in Explorer.
- Fixed the GUI layout regression caused by the taller right-side WebRTC diagnostics panel stretching the left connection form. The left form now stays content-sized instead of expanding to the right column height.
- Tauri run logs now include `launcher.json`, `bridge-status.json`, `bridge.stdout.txt`, `bridge.stderr.txt`, `melonds.stdout.txt`, and `melonds.stderr.txt`.
- Audit conclusion for the reported WAN failure:
  - Highest-probability existing bug was missing STUN: the deployed server currently sends `iceServers: []`, so the old bridge had only host candidates available. Same-Wi-Fi tests could pass while IPv4 WAN NAT traversal failed.
  - After STUN is active, some NAT combinations still cannot connect peer-to-peer. TURN credentials/server support are not implemented yet, so symmetric NAT, carrier-grade NAT, strict firewall, or UDP-blocked paths can still fail. The new diagnostics will show gathered `srflx` candidates and whether a selected pair is ever established.
- Verification passed:
  - `cargo check --manifest-path tools\nsmb-net-bridge\Cargo.toml --features webrtc`
  - `cargo test --manifest-path tools\nsmb-net-bridge\Cargo.toml --features webrtc` (route classification)
  - debug and release `nsmb-net-bridge.exe webrtc-signaling-udp-pair-smoke`
  - `cargo test --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml` (14 tests)
  - GUI `corepack pnpm run ci` and `corepack pnpm run vite:build`
  - signaling server `corepack pnpm run ci`
  - `scripts\test-nsmb-mvl-gui-launch-smoke.ps1 -BuildTauriBundle`
- Current blocker: actual WAN success still needs a new 2PC remote run. If logs show STUN candidates but no selected pair, TURN support is the next transport requirement.
- 2PC follow-up logs from 2026-06-03:
  - Same Wi-Fi run `nsmb-mvl-gui-1780417439` connected. The answer SDP contained a LAN host candidate `192.168.0.48` and an IPv4 srflx candidate `133.200.159.5:21911`; ICE reached connected/completed and packets flowed.
  - Tethering run `nsmb-mvl-gui-1780417655` failed. Signaling and SDP exchange completed, STUN was active, and both sides gathered srflx candidates, but ICE stayed in checking until `WebRTC connect timed out`. The answer SDP no longer had a `192.168.0.x` candidate, only tethering-side private candidates `10.5.0.2` / `10.149.31.198` and srflx `133.200.159.5:9626`.
  - Current interpretation: this is now a STUN-only NAT traversal failure rather than a signaling failure. The most likely causes are tethering carrier/NAT behavior, lack of NAT hairpin/loopback for the shared srflx public address, endpoint-dependent filtering, or another UDP restriction. TURN relay is the next implementation requirement for this class of failure.
  - Resolved diagnostics gap: the connected same-Wi-Fi run repeatedly logged `candidate_pair: BadString ... interior nul byte` when the Rust `datachannel` crate tried to decode a fixed C buffer as a full Rust string. The bridge no longer calls that API for GUI diagnostics; it records remote SDP candidates and infers the selected candidate pair from `local_address` / `remote_address`, removing the BadString spam and restoring GUI route display.
- Next actions:
  - Deploy the updated signaling Worker so Cloudflare-side diagnostics and server-side STUN defaults are active.
  - Run the rebuilt Tauri GUI on both WAN PCs, use `ログを開く`, and compare each `bridge-status.json`, `bridge.stdout.txt`, and `bridge.stderr.txt`.
  - Add TURN credential issuance and `relay` support if STUN-only WAN traversal still fails.

## Current work: Tauri GUI usability polish - 2026-06-02

- Goal: make the launcher easier to operate by replacing direct ROM path entry with file picker buttons, persisting selected ROM paths across app restarts, increasing the default window size, and making process-exit status read as an error.
- Implemented: the React ROM fields now show read-only path displays with `参照` buttons for host ROM, client ROM, and base ROM selection.
- Implemented: Tauri commands `select_rom_file` and `save_rom_paths` were added. ROM paths are stored in app data as `launcher-settings.json` and loaded by `get_defaults`; empty saved values fall back to the existing app-data ROM defaults.
- Implemented: the default window height changed from 720 to 1000 so the launcher opens with more vertical room.
- Implemented: the status pill now includes an explicit state label, stronger error styling, and treats `melonDS` or `bridge` `exited(...)` status as an error.
- Verification: `corepack pnpm install`, `corepack pnpm typecheck`, `cargo fmt --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml --check`, and `corepack pnpm build` passed in the local main worktree. The build produced `target\release\nsmb-mvl-gui.exe`, MSI, and NSIS installer outputs.
- Verification: release `nsmb-mvl-gui.exe --preflight` passed with sidecar/resource resolution and bridge signaling UDP pair smoke.
- Current blocker: none for producing the local Tauri build. Manual GUI confirmation of the Windows file picker, persisted ROM path restore, and WebRTC diagnostics panel is still useful before packaging this as a user-facing release.

## Current work: reusable launcher ROMs - 2026-06-01

- Goal: stop regenerating host/client ROMs whenever the Tauri game settings change. ROM generation should happen only for initial setup or when the reusable ROM format changes.
- Implemented: generated direct-entry ROMs now read course stage, scene settings, Big Star selector, and lives configuration from a small melonDS-populated runtime configuration block when its magic is present. Baked ROM values remain the fallback for non-launcher use.
- Implemented: the Tauri launcher writes a reusable-ROM format marker beside host/client ROMs. Match start reuses current ROMs and auto-generates only when files are missing or stale; the GUI keeps an explicit `共通ROM再準備` action for recovery/upgrades.
- Current reusable-ROM format marker is `nsmb-mvl-reusable-runtime-config-v3`; this forces one regeneration after removing the stale ROM-side RNG constant patch while preserving the runtime config, camera, and stage-lock patches.
- Verification passed: Rust ROM generator tests, 12 GUI backend tests, TypeScript/Biome checks, frontend build, local release melonDS rebuild, reused-ROM gameplay smokes, and seed-varying RNG gameplay smokes.
- Resolved during verification: the first reused-ROM settings smoke aborted because the runtime magic check reused `r0` after the load-level scene argument had been prepared. Moving the check before argument setup restored the call contract.
- Reused-ROM gameplay verification passed with one host/client ROM pair generated once using fallback `stage=0`, `Big Star=5`, `lives=endless`:
  - `logs/codex-reusable-rom-runtime-stage4-w3-s10-l5-v2-20260601`: runtime-only override to `stage=4`, `Wins=3`, `Big Star=10`, `lives=5`.
  - `logs/codex-reusable-rom-runtime-stage1-s3-result-20260601`: runtime-only override to `stage=1`, `Big Star=3`, `lives=endless`; forcing 3 stars reaches results.
  - `logs/codex-reusable-rom-runtime-stage2-s10-noresult-20260601`: runtime-only override to `stage=2`, `Big Star=10`, `lives=3`; forcing 3 stars does not reach results.
- GUI DOM render smoke passed against local Vite: the launcher shows `共通ROM再準備`, runtime-only course guidance, `起動 stage`, and reusable-ROM status.
- `scripts/prepare-nsmb-mvl-tauri-sidecars.ps1` refreshed the rebuilt melonDS sidecar. It now accepts absolute sidecar source paths as well as repo-relative paths.
- `scripts/test-nsmb-mvl-gui-launch-smoke.ps1 -BuildTauriBundle` now refreshes release sidecars before Tauri build, verifies `target\release\melonDS.exe` matches `build\release-windows-x86_64\melonDS.exe`, and passes with 12 GUI backend tests, bridge signaling UDP pair smoke, debug/release preflight, and regenerated MSI/NSIS bundles.
- Release GUI follow-up: the user-visible `target\release\nsmb-mvl-gui.exe` had been rebuilt while `target\release\melonDS.exe` remained an older sidecar. That stale melonDS binary explained why the GUI still showed entrance-normalization and visual regressions after the main release melonDS build looked fixed. The final rebuilt sidecar hash prefix is `8BA24DCEE1FE6B15` in the build output, Tauri binaries directory, and Tauri target release directory.
- GUI melonDS launches now remove inherited `MELONDS_NSML_*` variables before applying GUI-owned settings, so external diagnostic force/trace flags cannot leak into normal release GUI matches.
- RNG follow-up: generated reusable ROMs no longer patch `Net::getRandom()` / `Game::getRandom()` to constant `0x100`; direct `Game::loadLevel` now receives rngSeed `0xffffffff` so the runtime match seed controls in-game randomness. Verification logs `codex-rng-vary-seed-12345678-20260601` and `codex-rng-vary-seed-87654321-20260601` show host/client synchronized per seed, with different RNG timelines and different initial Big Star X positions across seeds.
- During bundle verification, the smoke wrapper was fixed to run GUI-subsystem `nsmb-mvl-gui.exe --preflight` through `Start-Process -Wait`; direct PowerShell invocation did not reliably wait for the release GUI process or expose its exit code.
- Current blocker: none for reusable-ROM launch settings.

## GitHub Actions package status - 2026-06-01

- `NSMB MvL Tauri` run `26714302373` completed successfully on GitHub Actions for commit `b5769a84c6de0ac8b99b4fb896ccaec7f598de05`.
- The run produced non-expired artifacts: `nsmb-mvl-tauri-windows-x86_64` (~36 MB), `melonDS-windows-x86_64` (~15 MB), and `nsmb-net-bridge-windows-x86_64` (~3.6 MB).
- The Windows pipeline covered melonDS build, bridge build, bridge `webrtc-signaling-udp-pair-smoke`, GUI backend tests, Tauri bundle build, packaged app `--preflight`, and artifact upload.
- `.github/workflows/nsmb-mvl-tauri.yml` now also runs on `push` to `main` / `master` using the same `paths` filter as `pull_request`.
- No rerun was requested for later `main` commits because the user accepted the successful package-producing workflow run as sufficient for this step.

## Current GUI/signaling note - 2026-05-31

- Tauri GUI frontend was migrated from vanilla TypeScript DOM rendering to React + Tailwind CSS. Current frontend verification: `corepack pnpm typecheck` pass, `corepack pnpm vite:build` pass, and Vite browser render smoke shows the launcher controls.
- Tauri GUI frontend now uses Biome 2.3.12 with signaling-server-style `format-and-lint`, `format-and-lint:fix`, and `ci` scripts. Current verification: `corepack pnpm format-and-lint` pass and `corepack pnpm typecheck` pass.
- The default GUI signaling URL is now `wss://nsmb-mvl-signaling-signaling-prod.uniunitaro.workers.dev/session`. `NSMB_MVL_SIGNAL_URL` can still override it.
- Main worktree sidecars are present under `tools\nsmb-mvl-gui\src-tauri\binaries\`; `cargo test --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml` passes locally.
- `corepack pnpm build` passes on the main worktree with the React/Tailwind GUI and produced fresh release outputs at `tools\nsmb-mvl-gui\src-tauri\target\release\nsmb-mvl-gui.exe`, `bundle\msi\NSMB Mario vs Luigi Online_0.1.0_x64_en-US.msi`, and `bundle\nsis\NSMB Mario vs Luigi Online_0.1.0_x64-setup.exe`. The rebuilt release exe `--preflight` also passes.
- The startup command prompt was caused by the release `nsmb-mvl-gui.exe` being built as Windows CUI. The GUI now sets `windows_subsystem = "windows"` for non-debug Windows builds, and spawned sidecars use `CREATE_NO_WINDOW`. Verification: rebuilt `nsmb-mvl-gui.exe` PE subsystem is `Windows GUI`; release `--preflight` still passes.
- Real Tauri GUI host startup produced `bridge exited(1)`.
- The GUI logs are written under `%APPDATA%\dev.melonds.nsmb-mvl\logs\nsmb-mvl-gui-*`, with `bridge.stdout.txt`, `bridge.stderr.txt`, `melonds.stdout.txt`, and `melonds.stderr.txt` per run.
- The latest host bridge stderr showed `signaling server error: {"error":"peer is not connected","type":"error"}` after the host sent its offer while the answer peer was not connected yet.
- Local signaling server code now queues pending `sdp` / `candidate` messages for the absent role and flushes them when the opposite role joins. `corepack pnpm run ci` passes in `tools/nsmb-signaling-server`.
- A live WebSocket probe confirmed that the deployed Worker still returns `iceServers: []`. It needs to be updated before the default GUI signaling URL benefits from queued-signal server logging and server-owned STUN defaults. The rebuilt bridge now independently falls back to Google STUN when an older deployed Worker returns an empty list.
- The Tauri GUI now displays the latest log directory in the launcher, so a failed bridge/melonDS run can be inspected without opening devtools.
- `scripts/run-nsmb-mvl-local-triage.ps1` was added for 1PC manual isolation. `DirectUdp` tests Rust-generated ROMs and melonDS input netplay without WebRTC; `WebRtc` keeps Rust-generated ROMs/settings but replaces GUI process management with direct `nsmb-net-bridge` WebRTC launch.
- DirectUdp reproduced the green/bad-control client symptom without WebRTC. Python-generated ROMs passed the same movement/sync test, which isolated the regression to Rust ROM generation. The Rust generator now resolves ARM9 patch addresses through the NSMB code settings copy table and applies the Python-equivalent RNG constant patch.
- Verification after the Rust ROM fix: `logs/codex-rust-arm9section-bothdiff-20260531` passed a 3600-frame DirectUdp host/client gameplay sync test with movement inputs, and `logs/codex-rust-arm9section-png-20260531` produced host/client screenshots at frames 900/1200 showing normal MvL rendering instead of a green client screen.

## 目的

New Super Mario Bros. DS の `Mario vs Luigi` を、最終的に一般ユーザーがポート開放なしで WAN 越しに対戦できる形へ持っていく。

`docs/nsmb-mario-vs-luigi-online-poc.md` は melonDS/ROM patch/input sync のPoC履歴と検証状態を扱う。この文書は WAN transport、WebRTC sidecar、将来のGUI/バックエンド方針を扱う。

## 現在の採用方針

サーバーなしの手動コピー&ペースト signaling と、Cloudflare Workers + Durable Objects の signaling server 経由で WebRTC DataChannel が使えることはローカル smoke で確認済み。
現在は、実 WAN 2PC 接続を診断し、STUN-only で越えられない NAT に備えて TURN 対応要否を判断する段階。
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

current desktop launcher
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

状態: 初期実装済み。Windows ローカルで Tauri release exe と MSI/NSIS bundle 生成まで確認済み。ROM 生成は Rust crate へ移行し、初回または形式更新時だけ共通 host/client ROM を準備する。ゲーム設定変更時は ROM を再生成せず melonDS 起動時に注入する。GUI backend の start_match コマンド組み立てと fake bridge/fake melonDS による実プロセス起動・停止は unit test で確認済み。起動前preflightで同梱/解決対象のbinary/resourceとbridge signaling smokeをGUIから確認できる。

実装:

- `tools/nsmb-mvl-gui`
- `tools/nsmb-mvl-rom`
- Tauri v2 + React + TypeScript + Vite + Tailwind CSS + pnpm 構成
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
- Rust ROM generator crate が再利用可能な stable direct MvL host/client ROM を生成する。ROM 内の baked 設定は非 launcher 利用時の fallback として残す。
- `scripts/generate-nsmb-mvl-stable-roms.ps1` は Python patch script ではなく `tools/nsmb-mvl-rom` を呼ぶ。
- Tauri command `ensure_roms` が version marker を確認し、初回または共通 ROM 形式更新時だけ base ROM から host/client ROM を生成する。`generate_roms` は明示的な再準備操作として残す。
- Tauri command `generate_roms` の既定出力先は Tauri app data 配下の `roms/` にし、インストール済みアプリでも開発ツリーの `roms/` へ書き込まない。
- GUI の開始操作は共通 ROM が未生成または旧形式の場合だけ自動準備する。通常は生成済み ROM を再利用し、`共通ROM再準備` は明示的な recovery 操作として扱う。
- GUI の `Course=random` はマッチシードから `stage = seed % 5` を算出し、melonDS 起動時の `MELONDS_NSML_MVL_STAGE` / `MELONDS_NSML_DIRECT_MVL_BOOT_STAGE` に渡す。
- melonDS は起動時 env から course stage、scene settings、Big Star selector、lives 初期値/mode selector を共有 RAM 設定ブロックへ書き、runtime-aware direct-entry stub がそれを読む。
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
- `.github/workflows/nsmb-mvl-tauri.yml` の `bridge-windows` は tracked `tools/nsmb-net-bridge/datachannel-wrapper` を使って `--features webrtc` buildを行う。`LIBCLANG_PATH` は runner 上の LLVM / Visual Studio BuildTools 候補から `libclang.dll` を探して設定する。release exe の `webrtc-signaling-udp-pair-smoke` も同jobで実行する。
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
cargo test --manifest-path tools/nsmb-mvl-gui/src-tauri/Cargo.toml: pass (11 tests; command/env + reusable ROM marker + fake process launch/stop + preflight bridge smoke validation)
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
- `DEFAULT_SIGNAL_URL` は `wss://nsmb-mvl-signaling-signaling-prod.uniunitaro.workers.dev/session`。GUI で変更するか `NSMB_MVL_SIGNAL_URL` で差し替える。
- full workflow は Windows runner と vcpkg/melonDS build を使うため、ローカル Docker `act` では frontend/ROM generator/bridge smoke の軽量workflowを検証対象にしている。Windows full workflowは実GitHub runnerでの確認が必要。現作業ツリーは未pushのため、実runner確認はpush/PRまたはworkflow_dispatch可能なremote branch作成後に行う。
- `Course=select` / 通常 MvL の `Choose Each Time` は direct route が CourseSelect を飛ばすため未対応。GUI/CLIでは選択肢として保持するが、実行時は fixed stage 扱いへ落とす。
- `Course=random` は起動前に選んだコースを runtime 設定として渡す。現checkpoint restart方式では2ゲーム目以降も同じコースへ戻り、ゲームごとの再抽選は未対応。

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
- `DEFAULT_ICE_SERVERS` は comma-separated STUN/TURN URI list。未指定時は signaling server と bridge の両方で `stun:stun.l.google.com:19302` へフォールバックする。
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
  - 完了: signaling URL、session、role、ICE server、connection state、candidate pair、packet stats、disconnect reasonをログと `bridge-status.json` に保存する。
  - 完了: GUI/Tauri から machine-readable 状態を読み、選択経路を表示する。
  - 完了: signaling server経由の自動smoke testを維持する。
  - 今後: WAN 実測ログを見て、タイムアウト値、エラー分類、TURN 強制モードの UI を調整する。
- security / abuse:
  - sessionごとの最大接続数、message size上限、rate limitを入れる。
  - 必要なら簡易tokenや署名つきsessionを導入する。
  - SDPやICE情報を長期保存しない方針を明記する。
- deployment / operations:
  - GitHub Actionsのdeploy jobは `main` push 自動実行と `main` 手動dispatchを維持しつつ、必要なsecrets/varsをREADMEに明記する。
  - staging/productionを分けるか判断する。
  - 完了: Cloudflare logs に join、queue、relay、flush、close を出す。
  - 今後: deploy 後の Cloudflare logs と各 PC の `bridge-status.json` を突き合わせる運用を確認する。

### Repository / branch policy

状態: 方針決定。`main` は `bigstar-project/bigstar` の本線として扱う。

推奨:

- `upstream/master`: melonDS公式追従用。基本的に直接変更しない。
- `main`: このfork/独自プロダクトの本線。`master` から作成し、NSMB online向け作業ブランチをmergeする。
- `feature/*` or `codex-*`: 個別作業ブランチ。
- Bigstar Project のGitHub remoteを `origin` (`https://github.com/bigstar-project/bigstar.git`)、公式melonDS remoteを `upstream` にする。
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
host:   NSMB MvL Netplay: peer connected
client: NSMB MvL Netplay: peer connected
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
