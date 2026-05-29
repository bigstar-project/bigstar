# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` で WAN 越しに対戦できる形へ持っていく。

現在の本筋は、LocalMP を WAN に流す方式ではなく、US版ROMへの direct MvL entry patch と、NSMB が試合中に読む入力境界への adapter を組み合わせる方式。

## 現在の方針

- 対象ROMは US版 `roms/nsmb-us.nds`。
- host は `localPlayerID=0`、client は `localPlayerID=1` の true local player 構成を維持する。
- direct MvL entry ROM patch でローカル通信UIを経由せず、Mario vs Luigi ステージへ直接入る。
- `Net::getConsoleKeys(u16)` / `Net::getConsoleTouchPad(u16)` を JIT helper patch で scratch memory 参照へ差し替え、WAN input を player0/player1 入力として渡す。
- `getPacketByte/getPacketTick/getPacketAction` まで差し替えるとステージ状態を壊しやすいため、現時点では keys/touch helper 限定。
- RNG は ROM側 `rng-constant --value 0x100` で固定している。
- 低遅延実用路線は `InputDelayFrames=4`、`InputMaxFrameLead=4`、unreliable input + bundle history 8。
- 高遅延向け rollback は別紙 `docs/nsmb-mvl-rollback-design-notes.md` に退避。現時点の本筋ではない。

## 完了済み

- US版ROM patch tooling:
  - `tools/nsmb_us_rom_tool.py`
  - `tools/nsmb_us_rom_patch.py`
- stable direct MvL entry ROM:
  - host: `roms/nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-rngconst-netaid.tmp.nds`
  - client: `roms/nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-rngconst-netaid.tmp.nds`
- client側を Luigi 視点/localPlayerID=1 として動かす基本経路。
- Luigi死亡時にステージ全体が止まる問題は、VS中だけ `PlayerBase::freezeStage()` / `PlayerBase::signalLocked()` を避ける ROM patch で改善。
- 手動入力を host/client で別々に受け取り、入力パケットとして相手へ送る input netplay mode。
- `InputMaxFrameLead` による片側先行制限。
- unreliable input packet + 過去入力 bundle による packet drop 耐性。
- active FPS と input wait/throttle の計測ログ。
- disabled trace/hook の JIT/ARM runtime overhead 削減。
- 一時ログ肥大化対策として `logs/codex-*` は削除済み。

## 現在の最優先課題

### 1. 60fps維持

ユーザー実測で、1PC 2窓でも LAN 2PC でも表示FPSが52前後に落ちる。

2026-05-29 の再調査結果:

- remote input wait は主因ではない。
- `MPInterface::Process()` も主因ではない。
- MvL gameplay中は `RunFrame()` 自体が約15-16msまで重くなる。
- 画面描画なしでは active fps が約62-64fps。
- 画面表示ありでは `SwapBuffers` / 画面提示が数ms乗り、active fps が約50fpsまで落ちる。
- `MELONDS_NSML_SWAPBUFFERS_INTERVAL=4` では負荷は下がるが、測定値は約55-60fpsで揺れる。表示更新頻度も落ちるため、快適な最終解ではない。

暫定手動プレイ設定:

- `scripts/run-nsmb-mvl-manual-peer.ps1` はデフォルトで `NoFrameLimit` 相当、`SwapBuffersInterval=4`、start frame 870 を使う。これは暫定の軽量化設定であり、60fps保証ではない。
- `UseFrameLimit` を付けると従来の frame limiter を使う。

次にやること:

- 画面提示を間引かずに60fpsへ戻す方法を探す。
- 候補:
  - input netplay時の描画経路を軽量化する。
  - OpenGL SwapBuffers を emulation thread から分離できるか調べる。
  - window/display更新頻度を落とさず、内部frameだけ60維持する別のpresent方式を検討する。
  - MvL中の `RunFrame()` 負荷をさらに削る。

### 2. direct entry起因の開始位相差

game-state比較では frame 900 で `movingHazardX` が host/client 不一致になる。

現在わかっていること:

- 入力は一致している。
- moving hazard は host 側が約6フレーム早く動き始める。
- 起動待ちや host/client 起動時間差ではなく、host ROM/client ROM の direct entry 経路差に見える。
- 目視プレイ上の主要挙動は改善しているが、完全同期を保証するには未解決。

次にやること:

- localPlayerID=1 client direct entry の StageStart/StageScene 作成経路をさらに追う。
- host/client の object spawn frame を揃える。
- 一時的な座標補正ではなく、試合開始状態の生成差を減らす。

## 手動起動

host:

```powershell
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role host
```

client:

```powershell
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role client -Peer <host-ip>
```

低遅延設定はデフォルトで `InputDelayFrames=4` / `InputMaxFrameLead=4`。描画間引き暫定策を無効化したい場合は `-SwapBuffersInterval 1` を付ける。

## 検証コマンド

速度だけを見る:

```powershell
.\scripts\run-nsmb-mvl-split-local-input-smoke.ps1 -LowDelayWan -AllowJit -Frames 3000 -NoGameStateTrace -SkipGameStateComparison -NoFrameLimit
```

同期比較を見る:

```powershell
.\scripts\run-nsmb-mvl-split-local-input-smoke.ps1 -UseLanMP -LowDelayWan -AllowJit -Frames 2600 -GameStateTraceInterval 30
```

現時点では同期比較は `movingHazardX` の開始位相差で失敗する可能性がある。

## 注意

- `docs/nsmb-mvl-rollback-design-notes.md` は rollback 議論の保存先。肥大化させず、rollback再開時だけ参照する。
- `logs/` はROMコピーを含むため肥大化しやすい。検証結果はdocsに要約し、古い `logs/codex-*` は削除する。
- final response 前にはこのファイルの古い「次にやること」や解決済みblockerが残っていないか確認する。
