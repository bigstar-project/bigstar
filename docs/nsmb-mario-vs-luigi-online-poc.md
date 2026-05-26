# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` で WAN 越しに対戦できる形へ持っていく。

過去に試した `LocalMP 2インスタンス * 2プロセス`、savestate 共有、試合開始後の WAN 切り替え、actor/state 強制同期は、通信切断、desync、内部状態不一致、低 FPS が大きく、最終方針としては採用しない。

## 現在の方針

US 版 ROM `roms/nsmb-us.nds` (`A2DE`) を主対象にする。`external/NSMB-Code-Reference` が US 版シンボルなので、ROM パッチと逆アセンブルの精度を優先する。

主方針は次の2本。

1. ROM パッチまたは低レベル adapter で、LocalMP UI/接続処理に依存しない MvL 専用入口を作る。
2. 試合中に NSMB が読む packet/input 境界を WAN adapter に差し替え、NSMB 側の同期処理をできるだけそのまま使う。

NSMB Central の解析では、MvL は接続時に RNG seed を同期し、試合中は主に入力情報を通信する。したがって、外から座標やスター状態を強制同期するより、`Net::getPacket` / `getConsoleKeys` / packet tick/action/byte を正しく差し替える方が本筋。

参考: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi

## 実装済み

- US 版 ROM 解析ツール `tools/nsmb_us_rom_tool.py`
  - `symbols9.x` のシンボル解決
  - ARM9/overlay 逆アセンブル
  - 重複 overlay 対策の `disasm --overlay-id`
- US 版 ROM パッチツール `tools/nsmb_us_rom_patch.py`
  - `rng-constant`
  - `direct-mvl-entry`
  - `fake-opponent`
  - `--force-confirm-load`
  - `--force-loadgame-progress`
  - `--mirror-packets`
  - `--fake-net-state-on-nickname`
  - `--force-transfer-result`
  - `direct-mvl-entry --force-ready-progress`
  - `direct-mvl-entry --force-transfer-result`
  - `--clear-actor-category-mask`
- 自動検証フック
  - 入力スクリプト
  - screenshot
  - RAM dump
  - game state trace
  - calltrace
  - object lifecycle summary
  - actor category mask 強制診断
- 主要 US アドレス確認
  - `Game::loadLevel = 0x0200696C`
  - `Scene::switchScene = 0x020131FC`
  - `Net::getPacket = 0x0200EB50`
  - `Net::getConsoleKeys = 0x0200E854`
  - `Net::getPacketByte = 0x0200EACC`
  - `Net::getPacketTick = 0x0200EB10`
  - `Net::getPacketAction = 0x0200EB30`
  - `Net::updatePacket = 0x0201031C`
  - `Net::Core::transferPacket = 0x0200FAE0`
  - `Stage::stageLayout = 0x020CAD40`
  - `Player::onUpdate = 0x020FD1D4` in overlay10
  - `Actor::preUpdate` category mask = `0x020CA850`
- US 版 PacketBridge の主要アドレス移植
  - MvL GGID は US/A2DE runtime では `0x00400150`。過去診断用の `0x42` と両方を MvL 判定として扱う。
  - Net/session 周辺の A2DJ アドレスを US/A2DE に移植済み。
  - US 下位 Wifi API hook を追加済み。
    - `Wifi::isConsoleCommunicating = 0x02046C44`
    - `Wifi::getSharedData = 0x02046E98`
    - `Wifi::updateSharedData = 0x02046ECC`
  - `MELONDS_NSML_PACKET_BRIDGE_ONLY=1` でも `MELONDS_NSML_WAIT_FOR_PEER=1` を尊重し、host が client 接続前に先行しすぎないようにした。

## 分かっていること

- `fake-opponent --force-confirm-load --force-loadgame-progress` ルートでは、1インスタンスで MvL のステージ/HUD/ミニマップ表示まで到達できる。
- `--mirror-packets` により `Net::getPacket(consoleID)` がローカル `sendPacket` を返す診断ルートは作成済み。
- `inputPlayer1Held` に 2P 入力相当の値が入ることは確認済み。ただし、2P actor はまだ自然には動かない。
- `Player::onUpdate` を method body として直接呼ぶ診断は abort する。自然な process/update 文脈なしに直接呼ぶのは不適切。
- fake-opponent 可視ルートで Player が動かなかった主因の一つは `Actor::preUpdate` の category mask。`0x020CA850` が `0x26` のままだと `Actor::preUpdate` が false を返し、`StageEntity::onUpdate` / `Player::onUpdate` に進まない。
- `MELONDS_NSML_FORCE_ACTOR_CATEGORY_MASK=1` かつ value `0` の診断では、`StageEntity::onUpdate` と `Player::onUpdate` が自然な vtable 経路で呼ばれ、player 座標/速度/死亡カウントが変化することを確認済み。
- write trace により、`loadMvsLFilesThread` overlay52 `0x02152E64-0x02152E74` が `0x020CA850` に `0x26` を書くことを確認済み。
- `fake-opponent --clear-actor-category-mask` ROM では、runtime force なしで `Player::onUpdate` が呼ばれ、player 座標/速度/死亡カウントが変化することを確認済み。
- `tests/nsmb_us_fake_opponent_gameplay_probe.inputs` で、mask 解除済み ROM 上の Mario/Luigi が画面内に出て動作し、敵接触/死亡まで進むことを screenshot で確認済み。
- state trace の player transform オフセットを修正済み。NSMB の `Vec3` は実メモリ上で 16 byte で、先頭4 byte が vtable、座標は `+4/+8/+12` にある。従来は `position.x` の代わりに `Vec3` vtable を読んでいた。
- 修正後の `logs/nsmvl-us-visible-clear-mask-gameplay-probe-fixed-transform-20260525` では、screenshot 上の横移動と CSV の `playerActor*X` が対応することを確認済み。
- `direct-mvl-entry` は入力スクリプト併用で `Ready!` 画面まで到達した。`--force-ready-progress` だけ、または `--force-ready-progress --force-transfer-result 8` では Select a Game へ戻るため、VSStageIntro の待ち以外にも自然な session/scene 状態が必要。
- 黒画面 session ルートは StageScene/process link が自然ルートと一致せず、現時点では補助診断扱い。
- US 版下位 Wifi PacketBridge は hook が発火する段階まで到達した。
  - single process smoke では `02046ECC` / `02046C44` / `02046E98` の lower hook 発火を確認済み。
  - two process smoke では ENet 経由の packet 送受信を確認済み。
  - host 側は local/remote 両 player の `hasPacket` / `getPacket` が成立するケースを確認済み。
  - client 側は remote packet 受信と `hasPacket(player=0)` 成立までは見えているが、同じ早期フレーム帯で `getPacket(player=0)` が安定して ptr を返すところまでは未確認。

## 現在の課題

1. client 側で remote packet が届いているのに `getPacket(player=0)` が安定して ptr を返さない。tick が完全一致していない、呼び出し順で受信が1フレーム遅い、または NSMB 側が `hasPacket` 後に別条件で `getPacket` へ進んでいない可能性がある。
2. lower Wifi API を WAN adapter に差し替える場合、exact tick 方式だけで足りるか、数 tick の受信バッファ/lookup delay が必要かを決める必要がある。
3. category mask `0x020CA850` が本来どの scene/session 条件で解除されるかは未解決。暫定 ROM patch では `--clear-actor-category-mask` で初期値を 0 にする。
4. UI 操作なし MvL 入口は未完成。`direct-mvl-entry` の単純な ready wait bypass では Select a Game へ戻るため、VSStageIntro/VSMenu の session 前提を追加で特定する。

## 次にやること

1. two process PacketBridge で、client 側 `hasPacket(player=0)` 成立後に `getPacket(player=0)` が進まない理由をログと counter で特定する。
2. 必要なら lower packet lookup に tick delay / nearest fallback / latest-before fallback を入れ、WAN 遅延を NSMB の下位 Wifi 境界で吸収できるか検証する。
3. 1360 frame 以降まで進め、通信切断表示なし、両側ステージ進行、screenshot/CSV 一致を確認する。
4. PacketBridge lower route が安定したら、ROM patch 側の UI なし MvL 入口と接続し、LocalMP 依存を減らす。
5. `0x020CA850` の自然な解除条件も継続して追い、`--clear-actor-category-mask` が恒久 patch として妥当か、それとも session 値を作るべきか判断する。

## 検証ルール

- `frame limit reached` だけでは成功扱いにしない。
- 黒画面、通信切断表示、片側だけ進行、HUD 不一致、actor 不一致は失敗扱いにする。
- screenshot と CSV/RAM/calltrace の両方で確認する。
- ROM 生成物、savestate、巨大ログは git に含めない。
- docs は古い追記を残し続けず、現在の方針、到達点、課題、次作業が分かる形に保つ。
