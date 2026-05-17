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

## 現在の課題

- 既存 `LAN` MPInterfaceはDSローカル無線フレーム相当をENetで運ぶため、WAN遅延にそのまま耐える保証はない。
- ただし、1 EmuInstance * 2プロセスでMario vs Luigiへ到達できる自動検証基盤としては有効。
- LAN経路上でもNSMBの52 byte packetは観測済み。
- 次はDS無線フレーム全体ではなく52 byte packetだけを交換する専用bridge/hookへ進む。

## 次にやること

1. `Net::getConsoleKeys()` / `Net::getPacketByte()` / `Net::setPacketByte()` 付近に、NSMB packet capture/replay用の最小hookを作る。
2. まずはLAN traceから抽出したpacket列をreplayできるか、片側プロセスで検証する。
3. Local MP/LANを経由しない、NSMB packet専用bridgeのPoCへ進む。
4. WAN向けにpacketのframe/tick基準、入力遅延、受信buffer、timeout処理を設計する。

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

# setPacketByte traceとLocal MP payloadの対応確認
python tools\nsmb_packet_trace_probe.py logs\route-combined-setpacket-localmp-2925\nsmb-mvl-route.call-trace.csv --localmp logs\route-combined-setpacket-localmp-2925.csv
```

## ユーザー依存

- ROMは `roms/nsmb.nds` に配置済みの日本版 `A2DJ` を前提にしている。
- WAN実機検証へ進む段階では、相手PC側にも同じmelonDSビルド、同じROM、同じ基本設定、別MACが必要。

## 運用ルール

- 実装状況、ブロッカー、次の作業はこのファイルを更新する。
- 古い「次にやること」や解決済みブロッカーは残さず、現在の状態に合わせて整理する。
