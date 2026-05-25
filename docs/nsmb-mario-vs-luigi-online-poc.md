# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` でWAN越しに遊べる形へ持っていく。

2026-05-25時点で、実装PoCの主対象をUS版ROM `roms/nsmb-us.nds` に切り替える。理由は `external/NSMB-Code-Reference` がUS版を前提にしており、ROMパッチで必要な関数名、構造体、シンボルを直接使えるため。日本版 `A2DJ` で蓄積した実行時解析は、挙動理解と最終的な日本版移植の参照として残す。

過去に試した `LocalMP 2インスタンス * 2プロセス`、savestate共有、試合開始後のWAN切り替え、actor座標や描画状態の外部同期は、desync、通信切断、低FPS、内部状態不一致が重く、最終方式としては採用しない。

## 現在の方針

1. US版ROMで、LocalMP接続/UI再現に依存しない `Mario vs Luigi` 専用ROMパッチPoCを作る。
   - まずは `external/NSMB-Code-Reference` のUS版シンボルを使い、MvLステージ開始、Player生成、RNG固定、入力注入の境界を特定する。
   - 目標は「LocalMPなしに2PがいるMvL gameplayを起動し、相手側入力バッファを外部から動かせる」状態。
2. その後、相手プレイヤー入力をWAN adapterからROM/メモリ境界へ渡す。
   - NSMB本来のLocalMP接続/ロビー/StageStartを完全再現する方針は捨てる。
   - 試合中の同期は、可能ならNSMB Centralの記述どおり入力packet中心のロジックへ寄せる。
3. 日本版 `A2DJ` はUS版PoC成立後に移植する。
   - 既存のJPアドレス解析は移植時の対応表として使う。
4. 直接的なactor座標同期、描画状態同期、乱暴なfreeze解除は診断用に限定する。

## 到達済み

- `Wifi::startChildScan` / `Wifi::startParent` / `Wifi::connectToParent` 相当をWAN adapter側で成功扱いにする診断ルートを追加済み。
- host/clientとも fake peer を見つけた状態へ進められる。
- `VSConnect::updateStageStartSM` まで進めるための packet bridge 診断フックを追加済み。
- `CourseSelect -> Game::loadLevel -> sceneCurrent=0x3` まで到達できる。
- `Game::loadLevel` は host/client とも playerID=0 で呼び、StageScene ready後にclientの `localPlayerID` を1へ戻すと、両者とも地形ありのMvL画面まで到達する。
- StageScene本体は `objectID=0x0003`, `settings=0x00B5FF00`, `base=0x021B94CC` 付近に生成される。
- StageScene vtable は `0x020C5864`。主な関数:
  - onCreate: `0x020A2224`
  - update dispatcher: `0x020A1BAC`
  - render dispatcher: `0x020A1D60`
- StageScene update dispatcher は `StageScene + 0x5618` をstate indexとして使う。
  - state 0 update: `0x020A1B50`
  - state 1 update: `0x020A14D8`
  - state 2 update: `0x020A0C68`
- StageScene state/dispatch/関連グローバルを game-state CSV に出す診断列を追加済み。
- player transition状態 (`player+0xB2D`, `+0x75C`, `+0x910`, `0x0208A96C/970`) を game-state CSV に出す診断列を追加済み。
- game-state CSV の既存ヘッダー漏れ (`courseSelectWord088`) を修正済み。以後のCSVではStageScene列を正しい位置で読める。
- `ARM.cpp` にwrite trace拡張と bad jump trace を追加済み。
- US版ROM `roms/nsmb-us.nds` のヘッダーを確認済み。gamecodeは `A2DE`。
- `tools/nsmb_us_rom_tool.py` を追加し、US版 `symbols9.x` のシンボルをARM9/overlayの展開済みコードへ対応付け、逆アセンブルできるようにした。
  - ARM9本体は圧縮されているため、ROM上の単純オフセットをそのまま逆アセンブルすると壊れる。`ndspy.codeCompression` で展開して扱う。
- `tools/nsmb_us_rom_patch.py` を追加し、圧縮ARM9を展開、書換、再圧縮してROMを保存する最小パッチパイプラインを作った。
  - 検証用に `Net::getRandom()` と `Game::getRandom()` を `0x100` 固定返却へ差し替えた `roms/nsmb-us-rng100.nds` を生成し、Debug smokeで起動成功を確認した。
  - 生成ROMは `roms/` 配下なのでgitには含めない。

## 最新の重要な発見

- 通常のWAN adapter routeでは StageScene が state 0 から state 1 へ進んだあと、`Stage::actorFreezeFlag=0x26` のまま停止する。
- StageScene state:
  - state 1: `0x020A14D8`
  - state 2: `0x020A0C68`
  - state 3: `0x020A096C`
- state 1からstate 2へ進むには、`0x020C9280` のロック解除と `StageScene+0x5645` 相当の入力/開始ラッチが必要。
- state 2の `StageScene+0x5649` は確認/closeラッチ。late A/START入力でhost/client双方が `0x020A0E64` または近い分岐を踏めるが、結果はgameplay開始ではなく `StageScene+0x561C=2` / `StageScene+0x5618=1` へ戻るだけ。
- state 1からstate 3へ進む正規分岐は `0x020C92C0 & 3`。以前の `0x020C9280 & 3` という仮説は誤り。
- `logs/nsmvl-stage-state3-92c0-write-trace-20260525` では、`0x020C92C0` は `0x0214CA70` で初期化時に0を書かれるだけで、現在のWAN routeでは自然にbit0/bit1が立たない。
- 診断用に `MELONDS_NSML_FORCE_STAGE_SCENE_STATE3_GATE` を追加した。これは `0x020C92C0` へ値を書いて state1 -> state3 の分岐を踏ませるためのフック。
- `logs/nsmvl-stage-state3-gate-force-20260525` では、`0x020C92C0=1` を一発入れるとhost/clientとも最終的に `StageScene+0x5618=3` へ入る。
  - ただし `Stage::actorFreezeFlag=0x26` は残り、`player+0xB2D=1` のままPlayer更新は進まない。
  - つまり「state3へ入れた」だけでは試合開始成功ではない。
- `PlayerBase::signalLocked()` / `signalUnlocked()` について:
  - JP `PlayerBase::signalLocked()` 候補: `0x02126EDC`
  - JP `PlayerBase::signalUnlocked()` 候補: `0x02126E90`
  - 通常WAN routeでは `signalLocked()` は各Playerに2回ずつ呼ばれるが、`signalUnlocked()` は一度も呼ばれていない。
- `Player` の土管出口遷移候補 `0x02117C80` は `player+0xB2D=0 -> 1` にし、`0x0208A96C[playerID]=2` を待つ構造。
- `0x0208A96C/970` は後で `1 -> 2` へ進むが、Player側は `player+0xB2D=1` のまま残る。
- Player main update候補 `0x020F90D4` / 遷移更新呼び出し点 `0x020F91C8` / 遷移更新入口候補 `0x0211A56C` は現在のWAN route中に呼ばれていない。Player遷移関数以前にactor update側が止まっている。
- `Stage::actorFreezeFlag` (`0x020C9250`) は `0x0214C9B0` で `0x26` に設定される。直接0へ戻す診断は過去に `ARM9: prefetch abort pc=FFFFF004` を起こしており、最終方式としては使わない。
- game-state CSVに `0x020C92B4/92C0/92C8/92D0` を追加した。state3ゲートと周辺状態をCSVで追える。
- StageScene state 1 は `0x020C92D0` のイベントフラグを見て `Stage::actorFreezeFlag` を変更している。
  - `0x020C92D0 & 0x100`: `Stage::actorFreezeFlag |= 0x2e`
  - `0x020C92D0 & 0x200`: `Stage::actorFreezeFlag &= ~0x2e`
- 診断用に `MELONDS_NSML_FORCE_STAGE_SCENE_EVENT_FLAGS` を追加した。これは `0x020C92D0` に指定bitをORし、StageScene本来のイベント消費経路でfreeze解除を踏ませるためのフック。
- `logs/nsmvl-stage-event-92d0-clear-freeze-20260525` では、host側で `0x020C92D0 |= 0x200` が `0x020A18D4 -> 0x020A18F4` に消費され、直接freeze書き換えなしで `Stage::actorFreezeFlag=0` と `player+0xB2D=2` まで進むことを確認した。
- `logs/nsmvl-stage-event-92d0-no-state3-force-20260525` では、`0x020C92D0 |= 0x200` だけでもPlayer updateと `signalUnlocked()` は走るが、`0x020C92C0 & 3` が立たないため state3 へ自然には入らず、state1/state2 の循環へ戻ることを確認した。
- 追加の静的解析で、`0x020A07EC` が `0x020C92C0` の低bitを触る主要候補だと分かった。
  - `r0 & 2` のとき `0x020C92C0 |= 2` を行う。
  - 既知の直接call siteは `0x020A0C30`, `0x020A1248`, `0x02114EBC`, `0x02115AFC`, `0x02116D48`, `0x0212693C`。
  - ただし現時点で見えている直接callは `r0=0/1/4/9` で、`r0=2` を自然に渡す経路はまだ見えていない。
- このため、`0x020C92C0 & 3` を「試合開始に必須」と決め打ちするのは危険。`state1` でも `Stage::actorFreezeFlag` が0ならPlayer updateは走るため、まずは `0x020C92D0 & 0x200` 経由のfreeze解除後に通常操作へ入れるかを検証する。
- `0x020AD460` が `0x020C92D0 |= 0x200` を行う自然な書き込み元候補。
  - これは `0x020AD33C` の内部で、`mvlGlobal9670` (`0x020C9670`) が5のときに実行される。
  - `0x020AD33C` は `0x02006ED4` から呼ばれ、`0x020C96F0` が指すMvL manager本体を引数に取る。
  - `020AF1DC` は `mvlGlobal9670` を見て `020C9930` の状態関数テーブルを呼ぶ。通常routeでも `020AF1DC` 自体は呼ばれている。
  - 通常routeで進まない直接原因は、manager側のプレイヤーready halfword `manager+0x494/+0x4A0` が0のままなこと。ここが `0xFF00` でないと `020AF1DC` は状態関数へ入らない。
  - 診断用に `MELONDS_NSML_FORCE_MVL_PLAYER_READY` を追加し、`manager+0x494/+0x4A0=0xFF00` と `manager+0xA8EC=0xFF` を短時間だけ入れると、host/client双方で `mvlGlobal9670: 0 -> 1 -> 2 -> 3 -> 4 -> 5` が走り、`0x020AD460` による `0x020C92D0 |= 0x200` が自然発生することを確認した。
  - 細粒度traceでは、`0x020C92D0 |= 0x200` はStageScene state1で消費され、同フレームで `Stage::actorFreezeFlag=0` になる。ただし直後に `020AF5FC` が `0x020C92D0 |= 0x100` を再発行し、次フレームで `Stage::actorFreezeFlag=0x2e` に再ロックされる。
  - つまり現時点の主問題は「freeze解除イベントが起きない」ではなく、「MvL manager状態機械が開始直後に再ロック側へ戻る」こと。
- game-state CSVに `mvlGlobal965C/9670/9674/9694` と `mvlManagerBase`, `mvlManager+0x494/+0x4A0`, `mvlManager+0xA8CC..0xA8EC` の診断列を追加した。

## 現在のブロッカー

- `0x020C92C0 & 3` が本来どの局面で立つのか未特定。現時点では開始ゲートではなく、StageSceneのイベント/結果ゲートの可能性もある。
- `0x020C92D0 & 0x200` の自然発生経路は見えたが、現状は診断フックで `manager+0x494/+0x4A0` と `+0xA8EC` を補助している。最終方式では、これらを本来どの通信/session/開始処理がセットするのか特定する必要がある。
- `mvlGlobal9670=5` 到達後に `0x020C92D0 |= 0x200` は走るが、その直後に `0x020C92D0 |= 0x100` が走ってfreezeが再ロックされる。開始状態機械を無理に進めている副作用か、本来のhandshake/ready条件不足かを切り分ける必要がある。
- freeze解除後の通常操作確認より先に、`020AF5FC` が再ロックイベントを出す条件を特定する必要がある。
- StageScene state遷移、Player遷移、actor freeze解除の3つがまだ自然な順番で接続できていない。特に `0x020C92D0 & 0x200` の自然発生源を、下位通信/session境界から追う必要がある。
- デバッグビルドの実行が遅く、3300F前後のWAN route検証はタイムアウトしやすい。CSVの部分結果は使えるが、成功判定は厳密に見る必要がある。

## 次にやること

1. `manager+0x494/+0x4A0=0xFF00` を本来自然に立てる処理を特定する。候補はMvL manager初期化、Player土管出口遷移、StageStart/StageScene開始処理。
2. `manager+0xA8EC` が自然に変化しない原因を追う。診断では `0xFF` を一回入れると `020AF5FC` が進むため、ここは初回tick/ready差分の不足が疑わしい。
3. `020AF5FC` が `0x020C92D0 |= 0x100` を再発行する条件を特定する。`manager+0xA8EC` と tick bucket の差分、ready halfword、player transition状態を優先して見る。
4. US版ROMパッチPoCとして、まず `VSConnectScene` / `CourseSelect` / `Game::loadLevel` の既存経路を使い、LocalMPなしでMvLステージへ直接入る入口を作る。
5. 入口ができたら、2P生成、Player VSPipe状態、actor freeze、入力バッファの順に、試合中操作へ必要な最小状態をROMパッチ側で整える。
6. `0x020C92C0` 低bitは、開始必須条件ではなくイベント/結果ゲートとして扱い直し、必要になった時点で `0x020A07EC` の呼び出し条件を分類する。
7. その後、RNG seed/消費順、ビッグスター、8コインアイテム、ランダムステージを確認する。

## 検証ルール

- `通信が切断されました` は失敗。
- 黒画面、極端な低FPS、死亡演出、敵接触、片側だけの進行、HUDだけ一致、actorだけ一致は成功扱いしない。
- スクリーンショットと game-state CSV の両方で確認する。
- player actor、StageScene、packet tick、RNG、star/itemまで確認するまで「対戦開始成功」とは呼ばない。

## 主なログ

- `logs/nsmvl-stage-scene-dispatch-fixed-20260525`
  - StageScene state 1 の dispatch 実体を確認。
- `logs/nsmvl-stage-scene-state1-globals-20260525`
  - state 1 が `0x020C9280=0x18` で閉じていることを確認。
- `logs/nsmvl-stage-scene-input-latch-clear9280-20260525`
  - 診断的に state 2 へ進めることを確認。ただしfreeze解除は未達。
- `logs/nsmvl-stage-scene-9280-write-trace-20260525`
  - `0x020C9280` の書き込み元が `0x0214CA68` と `0x02126F04` であることを確認。
- `logs/nsmvl-stage-scene-input-latch-long-20260525`
  - `+0x5645/+0x5649` の長時間強制は state 1/2 往復になり、自然開始にはならないことを確認。
- `logs/nsmvl-gameplay-probe-unfreeze-playercount-20260525`
  - 直接freeze解除が危険であることを確認。
- `logs/nsmvl-stage-scene-lock-ramdump-20260525`
  - JP版実RAMから `PlayerBase::signalLocked()` / `signalUnlocked()` 相当の実装とリテラルを確認。
- `logs/nsmvl-player-unlock-stage-start-20260525`
  - `signalUnlocked()` 相当の解除だけではstate 1から進まないことを確認。
- `logs/nsmvl-player-unlock-two-gate-pulse-20260525`
  - state1/state2のラッチを一発ずつ入れても自然な試合開始にはならず、state machineが戻ることを確認。
- `logs/nsmvl-signal-calltrace-20260525`
  - WAN routeでは `signalLocked()` だけが呼ばれ、`signalUnlocked()` は呼ばれないことを確認。
- `logs/nsmvl-transition-table-trace-20260525`
  - Player遷移完了テーブル `0x0208A96C/970` は `2` になるが、Playerの `+0xB2D` は1のまま進まないことを確認。
- `logs/nsmvl-player-update-trace-20260525`
  - Player遷移更新入口候補 `0x0211A56C` がWAN route中に呼ばれていないことを確認。
- `logs/nsmvl-transition-fields-csv-20260525`
  - game-state CSVへPlayer遷移フィールドを追加し、host/client双方で `transitionStatus=2`, `transitionStep=1`, `signalLock=1` が観測できることを確認。
- `logs/nsmvl-player-main-update-trace-20260525`
  - Player main update候補 `0x020F90D4` / `0x020F91C8` が呼ばれず、Player遷移更新以前で止まっていることを確認。
- `logs/nsmvl-freeze-flag-write-trace-20260525`
  - `Stage::actorFreezeFlag=0x26` の書き込み元が `0x0214C9B0` であることを確認。
- `logs/nsmvl-csv-header-smoke-20260525`
  - game-state CSVのヘッダー/行の列数一致を確認。
- `logs/nsmvl-post-transition-gate-pulse-20260525`
  - Player遷移完了後に強制ラッチしても、state2から自然な試合開始へ収束しないことを確認。
- `logs/nsmvl-stage-scene-byte-write-trace-20260525`
  - StageScene state2の主要書き込み元と、clientだけが `0x020A0E64` で `+0x5649` を自然に立てることを確認。
- `logs/nsmvl-state2-late-confirm-20260525`
  - late A/START入力でhost/client双方がstate2確認分岐を踏めるが、state3へは進まずstate1/2を往復することを確認。
- `logs/nsmvl-stage-state3-92c0-write-trace-20260525`
  - state1からstate3へ進む正規分岐が `0x020C92C0 & 3` であること、現在のWAN routeでは `0x020C92C0` が自然に立たないことを確認。
- `logs/nsmvl-stage-state3-gate-force-20260525`
  - 診断的に `0x020C92C0=1` を入れるとstate3へ入るが、`Stage::actorFreezeFlag=0x26` が残りPlayer更新はまだ走らないことを確認。
- `logs/nsmvl-stage-event-92d0-clear-freeze-20260525`
  - `0x020C92D0 |= 0x200` がStageScene state1本来のイベント処理で消費され、host側でfreeze解除とPlayer遷移更新再開が起きることを確認。
- `logs/nsmvl-stage-event-92d0-no-state3-force-20260525`
  - freeze解除イベントだけではPlayer updateと `signalUnlocked()` は走るが、`0x020C92C0 & 3` が立たず、自然なstate3開始には到達しないことを確認。
- `logs/nsmvl-stage-event-movement-no-state3-20260525`
  - `0x020C92D0 |= 0x200` のみでhost側はfreeze解除し、Playerが土管出口遷移を進めていることを確認。ただしdebug実行が遅く、3000F以降の移動入力までは到達できていない。
- `logs/nsmvl-stage-event-movement-debug-allowjit-20260525`
  - 探索用に `-AllowJit` を使ったが、同じくタイムアウト。host側は2940Fまで到達し、freeze解除後にPlayerのY座標/速度が更新され、`0x020C92D0=0x4` が観測された。
- `logs/nsmvl-mvl-manager-state-trace-20260525`
  - `mvlGlobal9670` とMvL manager内部状態をCSVへ出すための短距離検証。host/clientとも2950Fまで到達したが、`mvlGlobal9670=0`、manager内部 `+0xA8CC..0xA8EC=0` のままで、manager状態機械が起動していないことを確認。
- `logs/nsmvl-mvl-natural-event-fine-trace-20260525`
  - `manager+0x494/+0x4A0=0xFF00` と `manager+0xA8EC=0xFF` の診断補助下で、hostは2964F、clientは2971Fに `mvlGlobal9670=5 -> 0`、`0x020C92D0 |= 0x200`、StageSceneによる `Stage::actorFreezeFlag=0` を確認。
  - 同じ開始シーケンスが直後に再開し、`020AF5FC` が `0x020C92D0 |= 0x100` を出すため、次フレームで `Stage::actorFreezeFlag=0x2e` に戻ることを確認。
- `logs/nsmvl-us-vanilla-debug-smoke-20260525`
  - US版vanilla ROM `roms/nsmb-us.nds` が既存Debug smokeで起動できることを確認。
- `logs/nsmvl-us-rng-rompatch-debug-smoke-20260525`
  - `Net::getRandom()` / `Game::getRandom()` を `0x100` 固定返却にしたUS版ROMパッチが起動できることを確認。

## 参考

- NSMB Central: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi
- `external/NSMB-Code-Reference`
