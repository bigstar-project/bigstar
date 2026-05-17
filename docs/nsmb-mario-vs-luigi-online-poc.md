# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード「Mario vs Luigi」を、最終的にWAN越しの2PCで遊べる形にする。

現時点の最有力方針は、melonDSの2インスタンスを各PCで同期する方式ではなく、NSMBがローカル無線上で送受信しているゲームレベルpacketを特定し、1 EmuInstance * 2PC の構成でWAN向けに中継・バッファリングする方式。

## 現在の方針

1. 2 EmuInstance + Local MP は解析・比較用に使う。
2. 最終形は 1 EmuInstance * 2PC を目指す。
3. 既存melonDSの `LAN` MPInterfaceを使い、まず1DS同士でMario vs Luigiへ到達できる自動検証を作る。
4. その上で、DS無線フレーム全体ではなく、NSMBの52 byteゲームpacketを抽出・交換する専用bridgeへ寄せる。

2 EmuInstance * 2プロセス + StateApply は、プレイヤー・スター・スコアなど一部の状態を揃えられても、土管や画面内オブジェクトなどの局所状態が残り、実装が重く遅くなったため、最終路線から外した。

## 完了したこと

- Debugビルドのクラッシュ原因を特定・修正済み。
  - 原因は `LocalMP::SendPacketGeneric()` でreply packetの上位16bit AIDを落としたこと。
- NSMB A2DJ向け主要シンボルを移植・追加済み。
  - `Net::getRandom()` / `Net::getRandom12()` / `Net::syncRandom*()`
  - `Net::getConsoleKeys()` / `Net::getPacketByte()` / `Net::setPacketByte()` / `Net::getPacket*()`
  - `Net::Core::processSendPacket()` / `processRecvPacket()` / packet sequencer系
- ARM9 call traceを追加済み。
  - `r0..r3` とMainRAM pointer dumpをCSV出力できる。
  - 複数EmuInstance同時書き込みでCSVが壊れないようmutex化済み。
- `NDS::Reset()` で `NumFrames` などを初期化し、call traceの巨大な未初期化frame値を修正済み。
- Local MP payload traceを追加済み。
- `tools/nsmb_packet_trace_probe.py` を追加済み。
  - `Net::setPacketByte()` traceから44 byte payloadを再構築できる。
- `tools/nsmb_localmp_packet_extract.py` を追加済み。
  - Local MP traceからtick/keys/action/payloadを含む52 byte NSMB packetを抽出できる。
- 52 byte packetの既知layoutを確認済み。
  - host cmd `type=1 len=302`: full packet offset `46` / `108`
  - client reply `type=65538 len=106`: full packet offset `38`
  - payloadはfull packetの8 byte後ろ
- 1 EmuInstance * 2プロセスのLAN自動起動フックを追加済み。
  - `MELONDS_NSML_MP_INTERFACE=lan`
  - `MELONDS_NSML_LAN_ROLE=host|client`
  - `MELONDS_NSML_LAN_HOST`
  - `MELONDS_NSML_LAN_PLAYER`
  - `MELONDS_NSML_LAN_PLAYERS`
- 1プロセス1インスタンス同士でMACが同一になる問題を修正済み。
  - `MELONDS_NSML_FIRMWARE_MAC` でテスト用firmware MACを上書き可能にした。
- `scripts/run-nsmb-mvl-lan-route-smoke.ps1` を追加済み。
  - 既存の `inst0/inst1` 入力スクリプトをhost/client用に分割生成する。
  - 1 EmuInstance * 2プロセスでLAN MPInterfaceを使ってMario vs Luigiへ到達する。
  - `-GameStateTrace` 指定時は `stageGroup=0x9`、`vsMode=0x1`、host `localPlayerID=0`、client `localPlayerID=1` を検証する。
- LAN MPInterface payload traceを追加済み。
  - `MELONDS_NSML_LANMP_TRACE` / `MELONDS_NSML_LANMP_TRACE_DUMP_LEN`
  - `scripts/run-nsmb-mvl-lan-route-smoke.ps1 -LanMPTrace` でhost/client別に `host.lanmp.csv` / `client.lanmp.csv` を出力できる。
  - 既存の `tools/nsmb_localmp_packet_extract.py` でLAN traceからも52 byte NSMB packetを抽出できる。
- `tools/nsmb_packet_stream_compare.py` を追加済み。
  - host/clientの抽出済みpacket CSVを比較し、host command slotとclient reply/recvの対応を検証できる。
- packet replay hookの最小版を追加済み。
  - `MELONDS_NSML_PACKET_REPLAY_FILE` で `tick,player,packet_hex` CSVを読み込む。
  - MvL状態 (`stageGroup=9`, `vsMode=1`, `ggid=0x42`) に入ってからだけ有効化する。
  - `Net::getConsoleKeys()` / `Net::getPacketByte()` / `Net::getPacketTick()` / `Net::getPacketAction()` の戻り値を、現在tick/playerに対応するpacketから返せる。
  - `MELONDS_NSML_PACKET_REPLAY_LOG` でhit/missをCSV出力する。
- `tools/nsmb_packet_replay_build.py` を追加済み。
  - 抽出済みpacket CSVからreplay hook用の `tick,player,packet_hex` CSVを生成できる。
- packet capture hookの最小版を追加済み。
  - `MELONDS_NSML_PACKET_CAPTURE_LOG` で、MvsL中の `Net::Core::processSendPacket()` 入口からローカル52 byte packetをCSV出力できる。
  - `Net::sendPacket` 周辺のtick/keys/payloadと、MvL gameplay action `0x03` からpacketを再構築する。
- `tools/nsmb_packet_capture_compare.py` を追加済み。
  - capture hook出力と、LAN/LocalMP payloadから抽出したpacket CSVが一致するか検証できる。
- live packet bridgeの最小版を追加済み。
  - `MELONDS_NSML_PACKET_BRIDGE=1` で、capture hookが取ったlocal 52 byte packetを既存ENet PoC経由で相手プロセスへ送れる。
  - 受信packetはlive replay bufferへ入り、`Net::getConsoleKeys()` / `Net::getPacketByte()` / `Net::getPacketTick()` から参照できる。
  - `MELONDS_NSML_PACKET_BRIDGE_ONLY=1` で、既存の入力lockstepを動かさずpacket bridgeだけを動かせる。
  - `MELONDS_NSML_PACKET_BRIDGE_REPLAY_TICK_OFFSET` で、受信packetを何tick後のreplay lookupに使うか実験できる。
  - `MELONDS_NSML_PACKET_BRIDGE_WAIT` で、対象tickのremote packetが揃うまで短時間pumpして待つ実験ができる。
- LAN route smokeにpacket bridge検証オプションを追加済み。
  - `-PacketBridge`
  - `-PacketBridgeTrace`
  - `-PacketBridgeStartFrame`
  - `-HostPacketBridgeReplayTickOffset`
  - `-ClientPacketBridgeReplayTickOffset`
  - `-PacketBridgeWait`
  - `-PacketBridgeWaitTimeoutMs`
- ARM側packet bridge設定の二重mutex取得による実行停止を修正済み。
- replay hook strictのmiss処理を修正済み。
  - strict miss時も戻り値を設定してLRへ戻るようにした。
  - `MELONDS_NSML_PACKET_REPLAY_STRICT_PLAYERS` でstrict対象をplayer 0/1の片側だけに絞れる。
  - `MELONDS_NSML_PACKET_REPLAY_STRICT_START_FRAME` でstrict開始frameを遅らせられる。
  - `MELONDS_NSML_PACKET_REPLAY_STRICT_REQUIRE_LEAD` で、live bufferが指定tick先まで溜まるまではstrict missをfallbackさせられる。
- `MELONDS_NSML_DROP_MP_AFTER_FRAME` を追加済み。
  - 指定frame以降のmelonDS MP送受信を落とし、LAN/LocalMP payloadなしに近い状態を作るためのテストフック。

## 直近の検証結果

ビルド:

```powershell
cmake --build build\debug-windows-x86_64 --target melonDS --config Debug
```

通過済み:

```powershell
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 600 -LogDir logs\lan-route-600
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 1800 -LogDir logs\lan-route-1800-fixed
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 4200 -LogDir logs\lan-route-4200
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 5100 -LogDir logs\lan-route-5100-validated -GameStateTrace -GameStateTraceInterval 60
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 5100 -LogDir logs\lan-route-5100-lanmp-trace -GameStateTrace -GameStateTraceInterval 60 -LanMPTrace
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 5100 -LogDir logs\lan-route-5100-packet-replay-hook-gated -GameStateTrace -GameStateTraceInterval 60 -HostPacketReplayFile logs\lan-route-5100-lanmp-trace\host.replay.csv -ClientPacketReplayFile logs\lan-route-5100-lanmp-trace\client.replay.csv
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 4900 -LogDir logs\lan-route-4900-packet-capture -GameStateTrace -GameStateTraceInterval 60 -LanMPTrace -PacketCapture
.\scripts\run-nsmb-smoke.ps1 -Frames 600 -LogDir logs\single-smoke-after-bridge-deadlock-fix
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 5100 -HostStartupDelayMs 50 -LogDir logs\lan-route-5100-after-bridge-deadlock-fix -GameStateTrace -GameStateTraceInterval 60
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 5100 -HostStartupDelayMs 50 -LogDir logs\lan-route-5100-live-packet-bridge-start3000-noseedwait -GameStateTrace -GameStateTraceInterval 60 -PacketBridge -PacketBridgeTrace -PacketBridgeStartFrame 3000
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 3400 -HostStartupDelayMs 50 -LogDir logs\lan-route-3400-live-packet-bridge-role-offsets -GameStateTrace -GameStateTraceInterval 60 -PacketBridge -PacketBridgeTrace -PacketBridgeStartFrame 3000 -HostPacketBridgeReplayTickOffset 3 -ClientPacketBridgeReplayTickOffset 2
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 3400 -HostStartupDelayMs 50 -LogDir logs\lan-route-3400-live-packet-bridge-wait5 -GameStateTrace -GameStateTraceInterval 60 -PacketBridge -PacketBridgeTrace -PacketBridgeStartFrame 3000 -HostPacketBridgeReplayTickOffset 3 -ClientPacketBridgeReplayTickOffset 2 -PacketBridgeWait -PacketBridgeWaitTimeoutMs 5
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 3400 -HostStartupDelayMs 50 -LogDir logs\lan-route-3400-live-packet-bridge-strict-remote-start3100 -GameStateTrace -GameStateTraceInterval 60 -PacketBridge -PacketBridgeTrace -PacketBridgeStartFrame 3000 -HostPacketBridgeReplayTickOffset 3 -ClientPacketBridgeReplayTickOffset 2 -PacketBridgeStrictRemote -PacketBridgeStrictStartFrame 3100
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 3400 -HostStartupDelayMs 50 -LogDir logs\lan-route-3400-live-packet-bridge-strict-lead3 -GameStateTrace -GameStateTraceInterval 60 -PacketBridge -PacketBridgeTrace -PacketBridgeStartFrame 3000 -HostPacketBridgeReplayTickOffset 3 -ClientPacketBridgeReplayTickOffset 2 -PacketBridgeStrictRemote -PacketBridgeStrictRequireLead 3
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 5100 -HostStartupDelayMs 50 -LogDir logs\lan-route-5100-live-packet-bridge-dropmp3100 -GameStateTrace -GameStateTraceInterval 60 -PacketBridge -PacketBridgeTrace -PacketBridgeStartFrame 3000 -HostPacketBridgeReplayTickOffset 3 -ClientPacketBridgeReplayTickOffset 2 -PacketBridgeStrictRemote -PacketBridgeStrictRequireLead 3 -DropMPAfterFrame 3100
```

重要な確認:

- `logs\lan-route-5100-validated` で、1 EmuInstance * 2プロセスがMario vs Luigi試合状態へ到達した。
- host側trace終端: `stageGroup=0x9`, `vsMode=0x1`, `localPlayerID=0x0`
- client側trace終端: `stageGroup=0x9`, `vsMode=0x1`, `localPlayerID=0x1`
- MACを分けない場合は、両DSが互いを見つけられず検索画面に残る。したがって1DS * 2PC構成ではMAC差別化が必須。
- `logs\lan-route-5100-lanmp-trace` でLAN MP payloadから52 byte NSMB packetを抽出できることを確認済み。
  - host側: `host.packets.csv` 10556 rows
  - client側: `client.packets.csv` 10666 rows
  - MvL中は `action=0x03`、tick増加、keys反映あり。
- `tools\nsmb_packet_stream_compare.py` で `action=0x03` の3226 tickを比較し、errors=0を確認済み。
  - host `send type=1 slot0` == client `recv type=1 slot0`
  - host `send type=1 slot1` == client `recv type=1 slot1`
  - host `replies type=65538 slot0` == client `send type=65538 slot0`
  - host command slot1 == client reply
- `logs\lan-route-5100-packet-replay-hook-gated` で、実LAN通信を残したままpacket helper戻り値をreplay CSVで上書きしてもMario vs Luigi状態へ到達することを確認済み。
  - pre-matchで同じtick番号に誤爆しないよう、MvL状態gateが必要だった。
  - replay hookのmissは主にplayer 2/3やCSV範囲外で、現状は非strict fallbackで元処理へ戻す。
- `logs\lan-route-4900-packet-capture` で、ゲーム側capture hookから作った52 byte packetがLANに実際に出たpacketと一致することを確認済み。
  - host capture vs host `send type=1 slot0`: 2288 ticks, errors=0
  - client capture vs client `send type=65538 slot0`: 2289 ticks, errors=0
- `logs\single-smoke-after-bridge-deadlock-fix` で、packet bridge追加後も単体smokeが通ることを確認済み。
- `logs\lan-route-5100-after-bridge-deadlock-fix` で、packet bridge追加後も既存LAN routeがMario vs Luigiへ到達することを確認済み。
- `logs\lan-route-5100-live-packet-bridge-start3000-noseedwait` で、LAN routeを残したままlive packet bridgeをframe 3000から動かし、host/client間で52 byte packetを送受信できることを確認済み。
- `logs\lan-route-3400-live-packet-bridge-role-offsets` で、role別tick offsetを入れるとlive bridge由来packetがreplay hookでhitすることを確認済み。
  - host側はremote player 1の `byte` が12682 hits、`keys` が373 hits。
  - client側はremote player 0の `byte` が13328 hits、`keys` が392 hits。
  - 現状はLAN通信を残した補助検証であり、packet bridge単独ではまだない。
- `logs\lan-route-3400-live-packet-bridge-wait5` で、短時間wait hookを試した。
  - route自体はMario vs Luigiへ到達した。
  - wait timeoutが序盤に24回出た。
  - client側hit数は固定offsetのみの検証より減ったため、単純な「現在tickを待つ」だけでは不十分。次は開始時のwarmup/遅延tick基準を分ける必要がある。
- `logs\lan-route-3400-live-packet-bridge-strict-remote-start3100` で、remote playerだけをstrict対象にし、strict開始をframe 3100まで遅らせるとMario vs Luigi状態を維持できることを確認済み。
  - strictをframe 3000直後から有効化すると、初期packet不足でhost側が試合状態から落ちた。
  - strictにもwarmup/開始条件が必要。
- `logs\lan-route-3400-live-packet-bridge-strict-lead3` で、strict開始frameではなくbuffer lead 3 tickを条件にした場合もMario vs Luigi状態を維持できることを確認済み。
  - hit数はrole別offset検証と同程度。
  - これは固定frameより良い開始条件だが、LANなし成立の保証にはまだ足りない。
- `logs\lan-route-5100-live-packet-bridge-dropmp3100` で、frame 3100以降にMP送受信を落としても、最終frameの `stageGroup=0x9` / `vsMode=0x1` は維持した。
  - ただし、replay log上のNSMB packet tickはhost `0x067C`、client `0x067D` で止まっていた。
  - つまり現状は「試合状態から落ちない」だけで、LANなしで対戦が進行しているわけではない。
  - packet bridge単独化には、`0x02087F00` 付近のtick進行、`processSendPacket()` / `processRecvPacket()` の副作用、またはrecv sequencer側のhookが追加で必要。

## 現在の課題

- 既存 `LAN` MPInterfaceはDSローカル無線フレーム相当をENetで運ぶため、WAN遅延にそのまま耐える保証はない。
- ただし、1 EmuInstance * 2プロセスでMario vs Luigiへ到達できる自動検証基盤としては有効。
- live packet bridgeは送受信とhook hitまで確認できたが、packetが同tickの `getPacket*` 呼び出し後に届く場合がある。
- そのため、LANなしに進む前に、tick/frame基準の待ち、遅延、buffer、timeoutを設計する必要がある。単純に現在tickを待つだけでは、片側の進行を止めて相手のpacket生成も遅らせるため不十分。
- player 2/3向けの `getPacket*` missは2P MvLでは不要な可能性が高いので、strict化時はplayer 0/1に限定する。
- strict remoteはbuffer lead条件ならLAN補助ありで維持できる。
- MP送受信を止めると試合画面は維持されるがNSMB packet tickが止まる。次はtick進行とrecv/send side effectを補う必要がある。

## 次にやること

1. packet bridgeの固定offset/現在tick waitを、開始warmupつきの「N tick遅れのremote packetを使う」制御へ整理する。
2. LAN通信を残したまま、packet bridgeがゲーム状態へ与える影響を比較する。
3. MP drop後に止まる `0x02087F00` tickの更新元を追う。
4. packet bridge単独で不足するpacket helper / recv side effect / handshake処理を特定する。

## よく使うコマンド

```powershell
# 2 EmuInstance Local MP route smoke
.\scripts\run-nsmb-mvl-route-smoke.ps1 -Frames 4200

# 1 EmuInstance * 2プロセス LAN route smoke
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 5100 -LogDir logs\lan-route-5100-validated -GameStateTrace -GameStateTraceInterval 60

# LAN MP payload trace付き
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 5100 -LogDir logs\lan-route-5100-lanmp-trace -GameStateTrace -GameStateTraceInterval 60 -LanMPTrace

# Local MP traceから52 byte NSMB packetを抽出
python tools\nsmb_localmp_packet_extract.py logs\route-combined-setpacket-localmp-2925.csv --out logs\route-combined-setpacket-localmp-2925.packets.csv

# LAN MP traceから52 byte NSMB packetを抽出
python tools\nsmb_localmp_packet_extract.py logs\lan-route-5100-lanmp-trace\host.lanmp.csv --out logs\lan-route-5100-lanmp-trace\host.packets.csv
python tools\nsmb_localmp_packet_extract.py logs\lan-route-5100-lanmp-trace\client.lanmp.csv --out logs\lan-route-5100-lanmp-trace\client.packets.csv

# 抽出済みhost/client packet streamの対応確認
python tools\nsmb_packet_stream_compare.py logs\lan-route-5100-lanmp-trace\host.packets.csv logs\lan-route-5100-lanmp-trace\client.packets.csv

# replay hook用CSVを生成
python tools\nsmb_packet_replay_build.py logs\lan-route-5100-lanmp-trace\host.packets.csv --out logs\lan-route-5100-lanmp-trace\host.replay.csv --event send --action 0x03
python tools\nsmb_packet_replay_build.py logs\lan-route-5100-lanmp-trace\client.packets.csv --out logs\lan-route-5100-lanmp-trace\client.replay.csv --event recv --action 0x03

# replay hook検証
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 5100 -LogDir logs\lan-route-5100-packet-replay-hook-gated -GameStateTrace -GameStateTraceInterval 60 -HostPacketReplayFile logs\lan-route-5100-lanmp-trace\host.replay.csv -ClientPacketReplayFile logs\lan-route-5100-lanmp-trace\client.replay.csv

# capture hook検証
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 4900 -LogDir logs\lan-route-4900-packet-capture -GameStateTrace -GameStateTraceInterval 60 -LanMPTrace -PacketCapture
python tools\nsmb_packet_capture_compare.py logs\lan-route-4900-packet-capture\host.packet-capture.csv logs\lan-route-4900-packet-capture\host.packets.csv --event send --type 1 --slot 0
python tools\nsmb_packet_capture_compare.py logs\lan-route-4900-packet-capture\client.packet-capture.csv logs\lan-route-4900-packet-capture\client.packets.csv --event send --type 65538 --slot 0

# live packet bridge検証（LAN routeを残した補助検証）
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 3400 -HostStartupDelayMs 50 -LogDir logs\lan-route-3400-live-packet-bridge-role-offsets -GameStateTrace -GameStateTraceInterval 60 -PacketBridge -PacketBridgeTrace -PacketBridgeStartFrame 3000 -HostPacketBridgeReplayTickOffset 3 -ClientPacketBridgeReplayTickOffset 2

# packet bridge wait実験
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 3400 -HostStartupDelayMs 50 -LogDir logs\lan-route-3400-live-packet-bridge-wait5 -GameStateTrace -GameStateTraceInterval 60 -PacketBridge -PacketBridgeTrace -PacketBridgeStartFrame 3000 -HostPacketBridgeReplayTickOffset 3 -ClientPacketBridgeReplayTickOffset 2 -PacketBridgeWait -PacketBridgeWaitTimeoutMs 5

# remote player strict実験
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 3400 -HostStartupDelayMs 50 -LogDir logs\lan-route-3400-live-packet-bridge-strict-remote-start3100 -GameStateTrace -GameStateTraceInterval 60 -PacketBridge -PacketBridgeTrace -PacketBridgeStartFrame 3000 -HostPacketBridgeReplayTickOffset 3 -ClientPacketBridgeReplayTickOffset 2 -PacketBridgeStrictRemote -PacketBridgeStrictStartFrame 3100

# buffer lead条件つきstrict実験
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 3400 -HostStartupDelayMs 50 -LogDir logs\lan-route-3400-live-packet-bridge-strict-lead3 -GameStateTrace -GameStateTraceInterval 60 -PacketBridge -PacketBridgeTrace -PacketBridgeStartFrame 3000 -HostPacketBridgeReplayTickOffset 3 -ClientPacketBridgeReplayTickOffset 2 -PacketBridgeStrictRemote -PacketBridgeStrictRequireLead 3

# MP drop実験
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -Frames 5100 -HostStartupDelayMs 50 -LogDir logs\lan-route-5100-live-packet-bridge-dropmp3100 -GameStateTrace -GameStateTraceInterval 60 -PacketBridge -PacketBridgeTrace -PacketBridgeStartFrame 3000 -HostPacketBridgeReplayTickOffset 3 -ClientPacketBridgeReplayTickOffset 2 -PacketBridgeStrictRemote -PacketBridgeStrictRequireLead 3 -DropMPAfterFrame 3100

# setPacketByte traceとLocal MP payloadの対応確認
python tools\nsmb_packet_trace_probe.py logs\route-combined-setpacket-localmp-2925\nsmb-mvl-route.call-trace.csv --localmp logs\route-combined-setpacket-localmp-2925.csv
```

## ユーザー依存

- ROMは `roms/nsmb.nds` に配置済みの日本版 `A2DJ` を前提にしている。
- WAN実機検証へ進む段階では、相手PC側にも同じmelonDSビルド、同じROM、同じ基本設定、別MACが必要。

## 運用ルール

- 実装状況、ブロッカー、次の作業はこのファイルを更新する。
- 古い「次にやること」や解決済みブロッカーは残さず、現在の状態に合わせて整理する。
