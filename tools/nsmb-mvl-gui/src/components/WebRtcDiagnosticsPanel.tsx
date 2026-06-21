import { css } from 'styled-system/css';
import type { BridgeDiagnostics } from '../types';
import { SummaryItem } from './SummaryItem';

export function WebRtcDiagnosticsPanel({
  compact = false,
  diagnostics,
}: {
  compact?: boolean;
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
    <div
      className={css(
        compact
          ? {
              display: 'grid',
              gap: '2',
            }
          : {
              borderColor: 'gray.surface.border',
              borderTopWidth: '1px',
              display: 'grid',
              gap: '2',
              mt: '1',
              pt: '3',
            },
      )}
    >
      {compact ? null : (
        <h2
          className={css({
            color: 'fg.default',
            fontWeight: 'black',
            textStyle: 'md',
          })}
        >
          WebRTC 診断
        </h2>
      )}
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
