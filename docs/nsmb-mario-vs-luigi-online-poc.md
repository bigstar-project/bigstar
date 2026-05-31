# NSMB Mario vs Luigi Online PoC

## Current GUI runtime note - 2026-05-31

- A real Tauri GUI host run failed with `bridge exited(1)`.
- Logs for each GUI run are under `%APPDATA%\dev.melonds.nsmb-mvl\logs\nsmb-mvl-gui-*`; inspect `bridge.stderr.txt` first for bridge exits.
- The observed failure was not a melonDS launch failure. `bridge.stdout.txt` showed the host connected and sent offer SDP; `bridge.stderr.txt` showed the deployed signaling server returned `{"error":"peer is not connected","type":"error"}`.
- Local signaling server code now queues early host/client signaling messages until the opposite role joins, and the GUI now shows the latest log directory directly in the launcher.
- Live GUI testing against the default signaling URL still requires redeploying the Worker with the queued-message fix.
- For manual local triage, use `scripts/run-nsmb-mvl-local-triage.ps1`. `-Mode DirectUdp` generates Rust-patched host/client ROMs and launches the old direct UDP pair without WebRTC. `-Mode WebRtc` uses the same Rust-patched ROM/settings but launches `nsmb-net-bridge` WebRTC without the Tauri GUI. This separates Rust ROM/runtime issues from WebRTC/bridge issues.
- 2026-05-31 DirectUdp triage reproduced the client green/bad-control symptom without WebRTC, then Python-generated ROMs passed the same test. Root cause was the Rust ROM generator treating ARM9 as one linear RAM block; NSMB ARM9 uses copy-table sections, so ARM9 patches such as `Wifi::getCommunicatingConsoleCount` and `Net/Game::getRandom` were written to the wrong offsets.
- Fixed in `tools/nsmb-mvl-rom`: ARM9 section parsing now follows the code settings copy table, and the Rust stable generator also applies the Python-equivalent RNG constant patch. Verification: `logs/codex-rust-arm9section-bothdiff-20260531` passed 3600 frames with host/client gameplay sync and movement inputs; `logs/codex-rust-arm9section-png-20260531` produced host/client PNG screenshots with normal MvL rendering at frames 900/1200.

## Current status - 2026-05-31

- MvL 設定外部化は、direct MvL route の起動前 ROM 生成と runtime env の両方で受け取れる状態。
- ユーザーが触る通常 MvL 設定として、`Wins=1|2|3`、`Big Star=3|5|10`、`Mario's Lives=3|5|endless`、`Course=random` を扱う。通常 MvL の `Choose Each Time` は CourseSelect を復帰させる必要があるため、現 direct route では未対応。
- stable ROM生成は Python script から Rust crate `tools/nsmb-mvl-rom` へ移行済み。`scripts/generate-nsmb-mvl-stable-roms.ps1` と Tauri GUI command `generate_roms` は Rust 実装を呼ぶ。
- 旧Python ROM toolingの既定symbols pathも `tools/nsmb-mvl-rom/resources/symbols9.x` へ寄せ、Git管理外の `external/` がない環境でも既定値で動かしやすくした。
- Tauri GUI から base ROM、host/client ROM出力先、通常MvL相当設定、signaling URL、部屋コードを指定して、ROM生成と対戦開始を行える初期経路を追加済み。
- Tauri GUI backend の `start_match` は、host/client別の `nsmb-net-bridge` signaling引数と melonDS 起動envを unit test で確認する。さらに fake bridge/fake melonDS を実際にspawnし、session状態、停止、melonDS起動失敗時にsessionを残さないことを確認する。
- Tauri GUI に `起動前チェック` を追加し、melonDS/bridge/input/symbols の解決と、実bridgeの `webrtc-signaling-udp-pair-smoke` を開始前に確認できるようにした。古いbridgeが新しいsignaling smoke subcommandを持たない場合も検出する。
- `nsmb-mvl-gui.exe --preflight` を追加し、GUIを開かずに同梱sidecar/resource解決とbridge signaling smokeを検証できるようにした。
- Tauri GUI の既定ROM出力先とログ保存先は app data 配下に移し、`tools/nsmb-mvl-rom/resources/symbols9.x` と bootstrap input は bundle resource からも解決できるようにした。これでインストール済みアプリが開発ツリーのパスへ書き込む前提を外した。
- Tauri GUI の `Course=random` は表示中のmatch seedから `stage = seed % 5` を計算し、ROM生成と起動時envに同じstageを渡す。空欄時はGUI側でseedを生成して表示する。
- 有限ライフ設定時に Luigi の初期スポーン土管が Mario 側へ重なる問題を修正。原因は direct route が通常 CourseSelect setup を飛ばすため、`loadMvsLFilesThread` の早い入口選択で lives byte `3/5` が entrance ID として使われていたこと。
- `tools/nsmb_us_rom_patch.py` で `loadMvsLFilesThread` の初期入口 temp 値を player0=0 / player1=1 に寄せ、土管生成前に通常 MvL の左右入口に近い状態へ戻す。
- `scripts/run-nsmb-mvl-lan-route-smoke.ps1 -RequireMvlInitialSpawnState` は player actor だけでなく、初期スポーン土管に対応する `0x10b` object 2個の `x=0x8000/0x58000` も検証する。
- 検証:
  - `cmake --build build\release-windows-x86_64 --config Release --target melonDS -j 4` 成功。
  - `logs/codex-mvl-initial-pipe-verified-v1`: `MvlLives=3`, `Course=random`, `seed=0`, host/client 両方で `playerActor0X/playerActor1X=0x8000/0x58000`、`mvlObject267LeftX/mvlObject267RightX=0x8000/0x58000`。`repaired initial player spawn` は出ていない。
  - 同ログの software renderer screenshot `frame000895` で、初期土管が左右2本に分離していることを目視確認。
  - `logs\codex-rust-settings-matrix-final\settings-matrix-summary.csv`: Rust生成ROMで `Course=random` / `Wins=1,2,3` / `Big Star=3,5,10` / `Lives=3,5,endless` の27通りがpass。
  - `logs\codex-rust-bigstar-thresholds-final\bigstar-threshold-summary.csv`: `Big Star=3/5/10` の結果しきい値6ケースがpass。
  - `logs\codex-rust-auto-restart-wins2-v2` / `logs\codex-rust-auto-restart-wins3-v2`: `Wins=2` は `nextGame=2`、`Wins=3` は `nextGame=2` と `nextGame=3` へのcheckpoint restartを確認。
  - GUI/Actions: `corepack pnpm typecheck`、`corepack pnpm vite:build`、`cargo check --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml`、`cargo test --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml` 10 tests、`corepack pnpm build`、`actionlint .github/workflows/nsmb-mvl-tauri.yml .github/workflows/nsmb-mvl-gui-local.yml`、`act workflow_dispatch -W .github/workflows/nsmb-mvl-tauri.yml -j gui-check -P ubuntu-latest=catthehacker/ubuntu:act-latest`、`act workflow_dispatch -W .github/workflows/nsmb-mvl-gui-local.yml -j gui-check -P ubuntu-latest=catthehacker/ubuntu:act-latest` がpass。両方の `gui-check` は2026-05-31に現差分で再確認済み。
  - `scripts/test-nsmb-mvl-gui-launch-smoke.ps1` を追加し、GUI backend の fake process launch tests と実bridgeの `webrtc-signaling-udp-pair-smoke` をまとめて確認できるようにした。`-BuildTauriBundle` 付きでもpass。
  - `scripts/test-nsmb-mvl-gui-launch-smoke.ps1 -BuildTauriBundle` は debug/release `nsmb-mvl-gui.exe --preflight` も実行し、release exe が `target\release` 直下の `melonDS.exe` / `nsmb-net-bridge.exe` / `resources\symbols9.x` / bootstrap input を解決できることを確認済み。
  - `act workflow_dispatch -W .github/workflows/nsmb-mvl-gui-local.yml -j bridge-check -P ubuntu-latest=catthehacker/ubuntu:act-latest` がpass。2026-05-31に現差分で再確認済み。Tango依存を固定commitでcheckoutし、Ubuntu上で `cargo check --features webrtc --manifest-path tools/nsmb-net-bridge/Cargo.toml` と `cargo run --features webrtc --manifest-path tools/nsmb-net-bridge/Cargo.toml -- webrtc-signaling-udp-pair-smoke` を通した。後者はローカルWebSocket signaling server経由で offer/answer SDP 交換、WebRTC DataChannel接続、host/client相当UDP socket間の双方向payload到達を確認する。
  - Windowsローカルでも `LIBCLANG_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin` を指定して `cargo check --features webrtc --manifest-path tools\nsmb-net-bridge\Cargo.toml` と `cargo build --features webrtc --manifest-path tools\nsmb-net-bridge\Cargo.toml` がpass。生成済み debug exe の `webrtc-signaling-udp-pair-smoke` もpass。
  - release `nsmb-net-bridge.exe` を再buildし、`scripts/prepare-nsmb-mvl-tauri-sidecars.ps1` で Tauri sidecar を更新済み。更新後の `tools\nsmb-mvl-gui\src-tauri\binaries\nsmb-net-bridge-x86_64-pc-windows-msvc.exe webrtc-signaling-udp-pair-smoke` もpass。
  - `tools/nsmb-mvl-rom/resources/symbols9.x` をGit管理対象配下へ置き、ROM generator CLIの既定symbols pathとTauri bundle resource参照をこのファイルへ変更。既定symbols pathでの `generate-stable` と、Tauri release resourcesへの配置を確認済み。
  - 旧Python toolingのsymbols既定値変更後、`python -m py_compile tools\nsmb_localplayer_ref_report.py tools\nsmb_us_rom_tool.py tools\nsmb_us_rom_patch.py` がpass。
  - `.github/workflows/nsmb-mvl-tauri.yml` はWindows runnerで melonDS / bridge / Tauri app を分けてbuildし、最終Tauri bundle artifactをuploadする。melonDS build時間対策として `melonDS.exe` のsource-hash cacheを追加済み。`bridge-windows` はrelease exeの `webrtc-signaling-udp-pair-smoke`、`tauri-windows` はGUI backend unit testを走らせてからbundleする。

## 目的

New Super Mario Bros. DS のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` で WAN 越しに対戦できる形へ持っていく。

現在の本筋は、LocalMPをWANへそのまま流す方式ではなく、US版ROM向けの direct MvL entry patch と、NSMBが試合中に読む入力境界への adapter を組み合わせる方式。

## 現在の方針

- 対象ROMは US版 `roms/nsmb-us.nds`。
- host は `localPlayerID=0`、client は `localPlayerID=1` の true local player 構成を維持する。
- direct MvL entry ROM patch でローカル通信UIを経由せず、Mario vs Luigi ステージへ直接入る。
- 試合中は `Net::getConsoleKeys(u16)` / `Net::getConsoleTouchPad(u16)` を JIT helper patch で差し替え、WAN入力を player0/player1 入力として渡す。
- `getPacketByte/getPacketTick/getPacketAction` まで差し替えるとステージ状態を壊しやすいため、現時点では keys/touch helper 限定。
- RNG はROM側で定数化しない。hostが試合ごとにmatch seedを生成し、clientへ配布して、両側の `Net::random.value` / `Game::random.value` に同じseedを注入する。
- 実用低遅延路線は `InputDelayFrames=4` / `InputMaxFrameLead=4` / unreliable input + bundle history 8。
- 入力netplay開始時は、host/client双方が `PacketBridgeStartFrame` に到達したことを reliable start-ready packet で確認してから試合入力を開始する。
- 手動対戦では、片側が先行しすぎた場合に5秒でタイムアウトせず、相手が追いつくまで待つ。自動テストだけ内部wait timeoutを使う。
- 高遅延向け rollback は別紙 `docs/nsmb-mvl-rollback-design-notes.md` に保留。現時点の本筋ではない。

## MvLゲーム設定の現状

- 通常LocalMPのMvL設定画面でユーザーが触る項目は、US版ROMの実画面で `Wins` / `Big Star` / `Mario's Lives` / `Course` と確認した。
  - 確認時のデフォルト表示は `Wins=2` / `Big Star=5` / `Mario's Lives=Endless` / `Course=Choose Each Time`。
  - `Course` はユーザー指摘どおり、通常画面では固定コースではなく `Choose Each Time` と `Random` の選択肢として扱う。
- direct MvL routeでは通常のCourseSelect/設定画面を飛ばしているため、GUI向けには「起動前に設定から一時ROMを生成する」経路を使う。
  - `tools/nsmb-mvl-rom` は Rust製のstable ROM generator。direct MvL entry patch、WiFi communicating count patch、scene settings、Luigi初期入口/土管補正、camera fallback、VS mode skipを生成ROMへ反映する。
  - `scripts/generate-nsmb-mvl-stable-roms.ps1` は `-MvlWins` / `-MvlBigStars` / `-MvlLives` / `-MvlCourseMode` と、raw override の `-MvlSceneSettings` を受け取り、Rust generatorでhost/client ROMを設定付き生成する。
  - `scripts/run-nsmb-mvl-lan-route-smoke.ps1` は `-GenerateMvlConfiguredRoms` / `-MvlCourseMode random` / `-MvlMatchSeed` と上記ユーザー向け設定を受け取り、起動前に一時host/client ROMを生成できる。
  - manual local/peer scriptも同じユーザー向け設定を受け取れる。Tauri GUI側は、`generate_roms` commandでこれらの値を渡して開始前に一時ROMを作れる。
  - Tauri GUI側も `Course=random` ではmatch seedからstageを算出し、Rust generatorとmelonDS起動envの両方へ同じstageを渡す。
- コース設定:
  - `random` は起動時に `matchSeed % 5` でコース0..4を決める。`logs/codex-mvl-settings-random-stage0` から `stage4` まで、5コースすべてで1300 frames smokeが通過し、期待stageID検証も通った。
  - 2ゲーム目以降は、現checkpoint restart方式では1ゲーム目の正常stage checkpointへ戻すため、起動時に選ばれた同じコースへ戻る。ゲームごとにrandomを振り直す処理は未対応。pre-direct checkpointからstageを差し替える実験はtimeout/ARM9 abortにつながったため外した。
  - `select` / `Choose Each Time` は、飛ばしているCourseSelect部分を復活させる必要があるため未対応。ユーザー要件どおり、難しければ未対応でよい枠として扱う。
  - `fixed` は通常MvLのユーザー向け設定ではない。現状はdirect route内部・検証用の表現としてだけ残す。
- `Wins` / `Big Star` / `Mario's Lives`:
  - raw `sceneSettings=0x00B4FF00` が上記デフォルト表示と対応することは確認済み。
  - ユーザー向け項目は `-MvlWins 1|2|3`、`-MvlBigStars 3|5|10`、`-MvlLives 3|5|endless` として外部指定できる。
  - direct routeでは通常の設定画面/結果後管理を飛ばすため、`Wins` はROM内 `sceneSettings` の高位nibbleへ無理に詰めず、runtime側のmatch targetとして扱う。`Big Star` / `Mario's Lives` / `Course=random` は起動前の一時ROM生成とruntime envへ反映する。
  - `scripts/test-nsmb-mvl-settings-matrix.ps1` を追加し、`Course=random` で `Wins` 3通り * `Big Star` 3通り * `Mario's Lives` 3通りを自動検証できるようにした。
  - `logs/codex-rust-settings-matrix-final` で、Rust生成ROMの27通りすべてが1200 frames smokeを通過し、期待 `stageID = matchSeed % 5`、期待 `stageSceneSettings`、player actor 2体、Vs star actor、StageScene activeを確認した。
  - `Big Star=5` は安定defaultの `0xB4xx00`、`Big Star=10` は `0xB8xx00` を使う。`logs/codex-mvl-bigstar-low-nibble-sweep/summary.csv` ではlow nibble `4..8` だけが実stage actorありで通り、`0..3` と `9..15` は不安定またはabortした。
  - `Big Star=3` はNSMB本来の3個勝利を使う。`Big Star=5/10` はruntime側で星数をlogical countとして追跡し、native 3-star resultをtarget到達まで抑制、target到達時に結果へ進める。
  - `logs/codex-rust-bigstar-thresholds-final` で、`Big Star=3` は2個では結果なし/3個で結果、`Big Star=5` は3個では結果なし/5個で結果、`Big Star=10` は9個では結果なし/10個で結果を確認した。
  - 以前の `Wins=3` 候補 `0x00F4FF00`、`Wins=3` / `Big Star=10` の `0x09xxxx`、暫定 `0x39xxxx` は、direct routeでの安定性または勝敗遷移の意味が弱いため採用しない。現行mappingは安定した `0xB?xx00` を使い、match winsはruntime側で管理する。
  - raw逃げ道として `-MvlSceneSettings` は引き続き外部指定可能。ユーザー向け項目より raw override を優先する。
- 複数ゲーム:
  - `tests/nsmb_us_direct_mvl_star_collect_left_continue.inputs` を追加し、結果画面後にAを連打する継続probeを作成した。
  - `logs/codex-mvl-settings-result-continue-probe` では、勝敗確定後 `sceneCurrentSceneID=0xa` の結果シーンに入り、その後9000 frameまで2回目の `sceneCurrentSceneID=0x3` MvL stageへ戻らないことを確認した。
  - 結果シーン中に `Game::loadLevel` を直接呼ぶ方式はARM9 abortしたため不採用。scene requestだけを書き換える方式も、scene IDは戻るがステージオブジェクトが死んだままになるため不採用。
  - 現在は1ゲーム目の正常なMvL stageを内部savestate checkpointとして保持し、結果後にcheckpointへ戻す方式。`logs/codex-mvl-auto-restart-wins2-checkpoint` で、`Wins=2` 相当の1勝後に2ゲーム目へ実ステージとして戻ることを確認済み。
  - `run-nsmb-mvl-lan-route-smoke.ps1` に `-RequireMvlGameCount` を追加した。ただしcheckpoint restartは新規stage entryとしては見えにくいため、最終確認はstdoutの `nextGame` restartログも併用する。
  - `logs/codex-mvl-auto-restart-wins1-checkpoint` で、`Wins=1` 相当では結果シーン到達後に自動再開しないことを確認済み。
  - `logs/codex-rust-auto-restart-wins2-v2` で、`Course=random` / `Wins=2` / `Big Star=5` / `Lives=endless` が結果後に `nextGame=2` へ復帰することを確認済み。
  - `logs/codex-rust-auto-restart-wins3-v2` で、2回勝敗を作ったあと `nextGame=2` と `nextGame=3` のcheckpoint restartまで到達することを確認済み。
  - checkpoint復帰のため、2ゲーム目以降のstageは1ゲーム目と同じ。stdout上の `requestedStage` は進むが、実際のcheckpoint stageは保存時のstageへ戻る。

## 完了済み

- US版ROM patch tooling:
  - `tools/nsmb_us_rom_tool.py`
  - `tools/nsmb_us_rom_patch.py`
- Rust stable ROM generator:
  - `tools/nsmb-mvl-rom`
  - `scripts/generate-nsmb-mvl-stable-roms.ps1` はこのRust実装を呼ぶ。
- stable direct MvL entry ROM:
  - host: `roms/nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-netaid.tmp.nds`
  - client: `roms/nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-netaid.tmp.nds`
- 通常MvL設定画面のユーザー向け項目のうち、`Wins` / `Big Star` / `Mario's Lives` / `Course=random` を外部指定し、起動前の一時ROM生成へ反映する経路。
- `Big Star=3/5/10` の勝敗しきい値をruntime側で検証済み。
- `Wins=2` の2ゲーム目復帰と `Wins=3` の3ゲーム目到達を、direct route のcheckpoint restart方式で確認。
- client側を Luigi 視点/localPlayerID=1 として動かす基本経路。
- Luigi死亡時にステージ全体が止まる問題は、通常MvLと比較しながら改善済み。
- 手動入力をhost/clientで別々に受け取り、入力パケットとして相手へ送る input netplay mode。
- `InputMaxFrameLead` による先行制限。
- unreliable input packet + 過去入力bundleによる packet drop 耐性。
- `PacketBridgeStartFrame` 到達時の二者 start-ready barrier。古い同一プロセス用の開始バリアは input netplay 時には使わない。
- 手動peer/local起動では `InternalWaitTimeoutMs=0` をデフォルト化。input frame lead が閾値を超えたときは、desync/終了ではなく同期待ちで止める。
- active FPS と input wait/throttle の計測ログ。
- trace/hook無効時にJITを使える経路。
- 2026-05-30 カメラ追従:
  - `--mvl-camera-lead-from-player-velocity` による独自先読み補正は、本来MvsLと違う挙動になるためstable ROMから外した。
  - 通常LocalMPをUS版ROMで実測し、Luigi右移動時に `playerActor1X - stageCameraGlobalX1 ~= 0x60FFF` へ寄ることを確認した。
  - direct entryではステージ初期化中に `0x020CA880` の bit `0x08` が残り、右移動時も `playerActor1X - stageCameraGlobalX1 ~= 0x80FFF` 付近に固定されることをwrite-traceで特定した。
  - `MELONDS_NSML_CLEAR_MVL_CAMERA_INIT_HOLD` を追加し、direct entryで残った初期化hold bitだけを試合開始前に一度クリアするようにした。`0x020AD784` の本来の `Stage::cameraX[player]` 書き込みや、通常の `0x10` runtime camera flag は変更しない。
  - hook有効後、direct entryでも右移動時に `playerActor1X - stageCameraGlobalX1 ~= 0x60FFF` へ戻ることを確認した。
- 不要な古い `logs/codex-*` は適宜削除済み。

## 60fps切り分け結果

ユーザー確認:

- 公式配布版melonDS + US版ROM + 通常LocalMPでは60fps張り付き。

こちらの確認:

- 2026-05-30 software renderer追加調査:
  - 根本原因の一つは Windows clang Release build cache の `CMAKE_C_FLAGS_RELEASE` / `CMAKE_CXX_FLAGS_RELEASE` が空で、software renderer が実質的に最適化なしでビルドされていたこと。重いMvL gameplay中に `RunFrame()` が16.67msを超え、50-55fps級の低下につながっていた。
  - `cmake/DefaultBuildFlags.cmake` と `CMakeLists.txt` で Windows clang Release に `-O3 -DNDEBUG` を明示し、既存buildも同フラグで再configure/rebuild済み。
  - 手動scriptは renderer 設定を正しいTOML sectionへ書くよう修正済み: `[Screen]`, `[3D]`, `[3D.Soft]`, `Instance0.Window0`。
  - PoC smoke/manual scriptで起動する melonDS process は既定で `AboveNormal` priority にする。1PC 2プロセス + software renderer のscheduler競合を減らすため。
  - 連続remote input条件の専用ベンチとして `scripts/run-nsmb-mvl-software-fps-benchmark.ps1` を追加。既定入力は `tests/nsmb_us_direct_mvl_both_different.inputs`。
  - 代表値: `.\scripts\run-nsmb-mvl-software-fps-benchmark.ps1 -Frames 3600 -LogDir logs\codex-software-fps-benchmark-noperf` で host active `59.63fps`, client active `59.56fps`。perf breakdownは小さく負荷を足すため、代表FPS測定では付けない。詳細な内訳が必要なときだけ `-PerfBreakdown` を使う。
  - WAN相当の送信遅延stress: `.\scripts\run-nsmb-mvl-software-fps-benchmark.ps1 -Frames 3600 -InputSendDelayFrames 2 -InputSendJitterFrames 1` で host/client active `59.25fps`。wait/throttleは増えるが、50-55fps級の崩れは再発しない。
  - 1PC 2プロセスでは OS scheduling と input lead制御により 59fps台で揺れる。実運用のLAN/WAN 2PCでは各PCが1プロセスだけなので、このベンチは保守的なstress条件として扱う。
- `C:\Users\Sugiyama\melon-ds-master-perf` に素の master worktree を作成し、release build 済み。
- フォーク側の通常LocalMP routeは、日本版ROM + 既存入力スクリプトで成立。
- フォーク側通常LocalMPの定常区間:
  - フレーム制限なし: active `85.18fps`
  - フレーム制限あり: active `60.02fps`
- US版ROM + 既存 `tests/nsmb_mario_vs_luigi.inputs` は日本版向け入力スクリプトなので、通常LocalMP routeの自動化には使えない。メニュー/接続待ちを測ってしまうため、FPS baselineとして採用しない。
- US PoC経路、同一PC2プロセス、SwapBuffers毎フレーム、input delay 4:
  - フレーム制限なし: host/client active 約`65.4fps`
  - フレーム制限あり: host `59.89fps`、client `59.86fps`
- US PoC経路、送信遅延2F + jitter1F、フレーム制限あり:
  - host/client active `59.86fps`
  - remote waitは小さいが、throttleは発生する。FPS低下ではなく先行制限として機能している。
- remote input wait は直近測定では主因ではない。フレーム制限あり測定では remote wait はごく小さく、60fpsを維持できた。
- ただし、手動peerログ `logs/nsmb-mvl-manual-peer-host-20260530-024135/host.stdout.txt` では `input frame throttle timeout frame=2538 ... lead=5 waitedMs=5000` を確認。これは現行の手動設定で、片側先行時に5秒で同期待ちを打ち切る問題として扱う。
- 2026-05-30のWebRTC 1PC自動smoke FPS低下調査:
  - ユーザーのLAN 2PC WebRTC実行では60fpsを確認済み。
  - 1PC自動smokeでは、WebRTC/UDP sidecar/直接ENetのいずれでも、起動ハーネスや同一PC上のプロセス競合によりactive FPSが大きく揺れた。
  - 以前の27fpsは実運用性能ではなく、1PC自動smoke環境特有の結果として扱う。接続確認には使うが、FPS評価はLAN 2PCまたは専用計測で行う。
  - 詳細は `docs/nsmb-wan-netplay-roadmap.md` の `1PC auto smoke FPS investigation` を参照。

結論:

- melonDS本体、フォーク全体、通常LocalMPが52fps程度に落ちているわけではない。
- 以前の52fps問題は、Release最適化フラグ欠落、手動起動scriptの表示設定、1PC 2プロセスのscheduler競合が主因。
- `scripts/run-nsmb-mvl-manual-peer.ps1` は、デフォルトを滑らかさ優先に変更した:
  - `SwapBuffersInterval=1`
  - frame limit有効
  - `ShowOSD=false`
  - OpenGL表示、VSync off、JIT enabled
  - JITを切って比較する場合は `-NoJit` を付ける
  - software renderer比較用に `-SoftwareRenderer` を追加
- software rendererでFPSを確認する場合:
  - hidden stress: `.\scripts\run-nsmb-mvl-software-fps-benchmark.ps1 -Frames 3600`
  - visible stress: `.\scripts\run-nsmb-mvl-software-fps-benchmark.ps1 -Frames 3600 -Visible`
  - 手動peer: `.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role host -SoftwareRenderer` / `.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role client -Peer <host-ip> -SoftwareRenderer`

## 現在の確認状態

- 開始同期は固定raw frameではなく、全ステージ共通の `StageScene active + StageController + player actor 2体` ready条件に寄せている。クリボーなどステージ固有enemy/objectはready条件に使わない。
- RNGはROM側定数化ではなく、match seed同期。seed未指定時はhost生成seedをclientへ配布し、host/client内では一致させる。
- 手動起動のデフォルトbootstrapは `tests\nsmb_us_direct_mvl_minimal_bootstrap.inputs`。試合中の手動入力を上書きしないよう、neutral範囲は `668-839` まで。
- stable ROMでは独自カメラ先読みpatchを使わず、NSMB本来の `Stage::cameraX[player]` 更新をそのまま使う。旧 `MELONDS_NSML_DYNAMIC_CAMERA_LEAD` 実行時hookは診断用で、手動peerではデフォルト無効。
- direct entryのカメラは、通常LocalMPとの実測比較に基づいて `0x020CA880` の初期化hold bit `0x08` だけを一度クリアする。手動peer/local起動では `ClearMvlCameraInitHold` をデフォルト有効にしている。
- MvL設定は `Wins` / `Big Star` / `Mario's Lives` / `Course=random` を外部指定できる。`Course=Choose Each Time` はdirect routeがCourseSelectを飛ばすため未対応、`Course=random` の2ゲーム目以降の再抽選も未対応。
- `Big Star=3/5/10` はGUI/CLI設定値として受け付け、起動マトリクスと勝敗しきい値probeを通過済み。5/10個勝利はruntime側のlogical countでnative 3-star resultを抑制して実現する。
- 残りの注意点: 自動smokeの終了条件はraw frame基準なので、動的start後は片側が先に終了してもう片側がthrottle timeoutすることがある。これは手動対戦の同期ズレとは別のテストハーネス問題として扱う。

## 手動起動

host:

```powershell
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role host
```

client:

```powershell
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role client -Peer <host-ip>
```

デフォルトは `InputDelayFrames=4` / `InputMaxFrameLead=4` / frame limit有効 / `SwapBuffersInterval=1` / start-ready barrier有効 / `InternalWaitTimeoutMs=0` / JIT有効。

カメラ追従は通常の手動起動では追加指定不要。旧実行時hookを診断目的で重ねる場合だけ `-RuntimeDynamicCameraLead` を付ける。

```powershell
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role host -RuntimeDynamicCameraLead
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role client -Peer <host-ip> -RuntimeDynamicCameraLead
```
開始バリアを一時的に無効化して比較する場合だけ `-NoStartBarrier` を付ける。
同期待ちの自動テスト用timeoutを明示的に戻す場合は `-InternalWaitTimeoutMs 5000` のように指定する。
JITなしで比較する場合は `-NoJit` を付ける。

JITなしで peer 起動する場合:

```powershell
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role host -NoJit
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role client -Peer <host-ip> -NoJit
```

WAN相当の遅延・jitterを試す場合:

```powershell
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role host -InputSendDelayFrames 2 -InputSendJitterFrames 1
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role client -Peer <host-ip> -InputSendDelayFrames 2 -InputSendJitterFrames 1
```

フレーム制限を切って余力を見る場合:

```powershell
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role host -NoFrameLimit
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role client -Peer <host-ip> -NoFrameLimit
```

melonDSデフォルト相当のsoftware rendererで比較する場合:

```powershell
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role host -SoftwareRenderer
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role client -Peer <host-ip> -SoftwareRenderer
```

同一PCで2窓起動する場合:

```powershell
.\scripts\run-nsmb-mvl-manual-local.ps1 -LowDelayWan -SoftwareRenderer -AllowJit
```

`run-nsmb-mvl-manual-local.ps1` は `-AllowJit` を明示したときだけJIT有効。`run-nsmb-mvl-manual-peer.ps1` は逆に、手動/LAN実用検証の速度優先でデフォルトJIT有効、`-NoJit` で無効化する。

## 検証コマンド

通常LocalMP baseline:

```powershell
.\scripts\run-nsmb-mvl-route-smoke.ps1 -Exe build\release-windows-x86_64\melonDS.exe -Rom roms\nsmb.nds -Frames 3600 -AllowJit -NoScreenshots -NoHashLog -NoRngPatch -QuietLog -ActiveFpsStartFrame 3000
```

US PoC 低遅延入力同期 baseline:

```powershell
$env:MELONDS_NSML_SWAPBUFFERS_INTERVAL='1'
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -RunRole both -Exe build\release-windows-x86_64\melonDS.exe -Rom roms\nsmb-us.nds -HostRom roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-netaid.tmp.nds -ClientRom roms\nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-netaid.tmp.nds -InputScript tests\nsmb_us_direct_mvl_minimal_bootstrap.inputs -Frames 3000 -ScreenshotInterval 0 -NoHashLog -SkipMvlStateCheck -SkipGameplayActorCheck -NoLanMP -InputNetplay -InputDelayFrames 4 -InputMaxFrameLead 4 -PacketBridgeJitHelperPatch -PacketBridgeJitHelperPatchFrame 840 -PacketBridgeStartFrame 840 -WaitForPeerAtNetplayStart -AllowJit -InputUnreliable -InputBundleHistory 8
Remove-Item Env:\MELONDS_NSML_SWAPBUFFERS_INTERVAL -ErrorAction SilentlyContinue
```

直近の検証:

- 通常LocalMP baseline: `logs\codex-camera-original-localmp-us-probe1` / `logs\codex-camera-original-localmp-dbgfields`。Luigi右移動時の `playerActor1X - stageCameraGlobalX1` は約 `0x60FFF`。
- direct entry + camera init hold clear: `logs\codex-camera-clear-init-hold-hook-screens`。Luigi右移動時の `playerActor1X - stageCameraGlobalX1` は約 `0x60FFF` へ戻る。
- `scripts\run-nsmb-mvl-manual-local.ps1 -Frames 1200 -LowDelayWan -AllowJit` で、手動local wrapperから `ClearMvlCameraInitHold` がhost/client両方に渡ることを確認。
- `-CheckHostClientGameplaySync` 失敗調査:
  - 直近の frame 880/1280 付近の失敗は、検証コマンド側で `PacketBridgeStartFrame` / `PacketBridgeJitHelperPatch` などの必須フラグを落としていたために発生していた。正しい低遅延入力同期フラグでは `tests\nsmb_us_direct_mvl_minimal_bootstrap.inputs` / 2200 frames がpass。
  - `tests\nsmb_us_direct_mvl_both_different.inputs` / 3600 frames では、player座標、スター、moving hazard、object数、入力状態などのゲームプレイ項目は全行一致したが、`netPacketTick` だけ frame 2100/2560 で一時的に差が出ていた。
  - `netPacketTick` はNSMBの通信packet作業領域で、反映済みゲーム状態そのものではないため、`-CheckHostClientGameplaySync` の必須比較から外した。必要な場合だけ `-CheckHostClientNetPacketTickSync` で別途検査する。
  - 修正後、`tests\nsmb_us_direct_mvl_both_different.inputs` / 3600 frames / `-CheckHostClientGameplaySync` はpass。`-CheckHostClientNetPacketTickSync` を明示した場合は従来どおり frame 2560 のtick差を検出する。
- MvL設定外部化:
  - `logs\codex-rust-settings-matrix-final\settings-matrix-summary.csv`: Rust生成ROMで `Course=random` / `Wins=1,2,3` / `Big Star=3,5,10` / `Lives=3,5,endless` の27通りがpass。各caseでstageID、sceneSettings、player actor、Vs star actor、StageScene activeを確認。
  - `logs\codex-rust-bigstar-thresholds-final\bigstar-threshold-summary.csv`: `Big Star=3/5/10` の結果しきい値6ケースがpass。
  - `logs\codex-rust-auto-restart-wins2-v2`: `Course=random` / `Wins=2` で結果後に `nextGame=2` へ復帰することを確認。checkpoint方式のため、2ゲーム目のcourseは1ゲーム目と同じ。
  - `logs\codex-rust-auto-restart-wins3-v2`: `Wins=3` で `nextGame=2` と `nextGame=3` へのcheckpoint restartを確認。
  - Rust ROM generator parity: Python生成ROMとRust生成ROMの主要patch領域 (`0x021577EC`, `0x020C5298`, `0x020A06DC`, `0x02013428`, `0x02159348`, `0x0200FAE0`) の一致を確認。

## 注意

- `docs/nsmb-mvl-rollback-design-notes.md` は rollback 議論の保存先。肥大化させず、rollback再開時だけ参照する。
- `logs/` はROMコピーを含むため肥大化しやすい。検証結果はdocsに要約し、古い `logs/codex-*` は削除する。
- final response 前には、このファイルの古い「次にやること」や解決済みblockerが残っていないか確認する。
