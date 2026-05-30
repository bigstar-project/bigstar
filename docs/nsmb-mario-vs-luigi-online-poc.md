# NSMB Mario vs Luigi Online PoC

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

## 完了済み

- US版ROM patch tooling:
  - `tools/nsmb_us_rom_tool.py`
  - `tools/nsmb_us_rom_patch.py`
- stable direct MvL entry ROM:
  - host: `roms/nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-netaid.tmp.nds`
  - client: `roms/nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-netaid.tmp.nds`
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

## 注意

- `docs/nsmb-mvl-rollback-design-notes.md` は rollback 議論の保存先。肥大化させず、rollback再開時だけ参照する。
- `logs/` はROMコピーを含むため肥大化しやすい。検証結果はdocsに要約し、古い `logs/codex-*` は削除する。
- final response 前には、このファイルの古い「次にやること」や解決済みblockerが残っていないか確認する。
