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

## 現在の課題

- 既存 `LAN` MPInterfaceはDSローカル無線フレーム相当をENetで運ぶため、WAN遅延にそのまま耐える保証はない。
- ただし、1 EmuInstance * 2プロセスでMario vs Luigiへ到達できる自動検証基盤としては有効。
- LAN経路上でもNSMBの52 byte packetは観測済み。
- 次はDS無線フレーム全体ではなく52 byte packetだけを交換する専用bridge/hookへ進む。

## 次にやること

1. capture hookとreplay hookをENetでつなぎ、LAN/LocalMP payloadを経由しないpacket専用bridge PoCを作る。
2. replay hookのmiss分類を整理し、player 0/1についてstrictにできる条件を詰める。
3. WAN向けにpacketのframe/tick基準、入力遅延、受信buffer、timeout処理を設計する。
4. packet bridge単独でMario vs Luigi状態が維持できるか、既存LAN通信を切って検証する。

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

# setPacketByte traceとLocal MP payloadの対応確認
python tools\nsmb_packet_trace_probe.py logs\route-combined-setpacket-localmp-2925\nsmb-mvl-route.call-trace.csv --localmp logs\route-combined-setpacket-localmp-2925.csv
```

## ユーザー依存

- ROMは `roms/nsmb.nds` に配置済みの日本版 `A2DJ` を前提にしている。
- WAN実機検証へ進む段階では、相手PC側にも同じmelonDSビルド、同じROM、同じ基本設定、別MACが必要。

## 運用ルール

- 実装状況、ブロッカー、次の作業はこのファイルを更新する。
- 古い「次にやること」や解決済みブロッカーは残さず、現在の状態に合わせて整理する。
