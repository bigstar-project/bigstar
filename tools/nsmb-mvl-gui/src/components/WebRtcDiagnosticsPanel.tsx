import type { BridgeDiagnostics } from '../types';
import { SummaryItem } from './SummaryItem';

export function WebRtcDiagnosticsPanel({
  diagnostics,
}: {
  diagnostics: BridgeDiagnostics | null;
}) {
  const pair = diagnostics?.selected_candidate_pair;
  const stats = diagnostics?.stats;
  const route = pair?.route ?? '未確定';
  const routeLabel: Record<string, string> = {
    local: 'local: 同一 LAN / host candidate',
    direct: 'direct: 公開アドレスで直接接続',
    stun: 'stun: NAT 越し P2P',
    'turn-relay': 'turn-relay: TURN 中継',
    unknown: 'unknown: 判定不能',
  };
  return (
    <div className="mt-1 grid gap-3 border-t border-slate-200 pt-4">
      <h2 className="text-lg font-bold text-slate-950">WebRTC 診断</h2>
      <SummaryItem label="phase" value={diagnostics?.phase ?? '未起動'} />
      <SummaryItem
        label="ICE / WebRTC state"
        value={`${diagnostics?.ice_state ?? '-'} / ${diagnostics?.connection_state ?? '-'}`}
      />
      <SummaryItem label="選択経路" value={routeLabel[route] ?? route} />
      <SummaryItem
        label="candidate type"
        value={`${pair?.local_type ?? '-'} -> ${pair?.remote_type ?? '-'}`}
      />
      <SummaryItem
        label="selected address"
        value={`${pair?.local_address ?? '-'} -> ${pair?.remote_address ?? '-'}`}
      />
      <SummaryItem
        label="ICE server"
        value={diagnostics?.ice_servers?.join(', ') || '-'}
      />
      <SummaryItem
        label="packets app -> rtc / rtc -> app"
        value={`${stats?.app_to_webrtc_packets ?? 0} / ${stats?.webrtc_to_app_packets ?? 0}`}
      />
      {diagnostics?.last_error ? (
        <SummaryItem label="last error" value={diagnostics.last_error} />
      ) : null}
    </div>
  );
}
