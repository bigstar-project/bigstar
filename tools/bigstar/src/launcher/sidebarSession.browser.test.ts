import { describe, expect, test } from 'vitest';
import type { SoloTestStatus } from '../bindings';
import { previewCurrentMatch } from '../previewData';
import { sidebarSession } from './sidebarSession';
import type { ConnectionStatusState } from './types';

const connected: ConnectionStatusState = {
  active: true,
  kind: 'ok',
  text: '接続済み',
};

const soloIdle: SoloTestStatus = {
  active: false,
  client_pid: null,
  config: null,
  error: null,
  host_pid: null,
  log_dir: null,
  preparing: false,
};

describe('サイドバーに出す対戦', () => {
  test('進行中の対戦は相手と勝ち数を出す', () => {
    expect(
      sidebarSession({
        connectionStatus: connected,
        currentMatch: previewCurrentMatch('live'),
        hostedRoom: null,
        soloStatus: soloIdle,
      }),
    ).toEqual({
      kind: 'match',
      opponentName: 'Rival',
      opponentWins: 0,
      selfWins: 1,
    });
  });

  test('対戦の記録がまだなければ接続中として出す', () => {
    expect(
      sidebarSession({
        connectionStatus: connected,
        currentMatch: null,
        hostedRoom: null,
        soloStatus: soloIdle,
      }),
    ).toEqual({ kind: 'connecting' });
  });

  test.each([
    ['中止した直後', 'stopped'],
    ['勝敗が付いた後', 'finished'],
  ] as const)('%s は、セッションが続いていても出さない', (_, scenario) => {
    expect(
      sidebarSession({
        connectionStatus: connected,
        currentMatch: previewCurrentMatch(scenario),
        hostedRoom: null,
        soloStatus: soloIdle,
      }),
    ).toBeNull();
  });

  test('勝敗が付いた後に相手が抜けても、再接続中として出さない', () => {
    expect(
      sidebarSession({
        connectionStatus: { ...connected, recoveryDeadlineMs: 1_000 },
        currentMatch: previewCurrentMatch('finished'),
        hostedRoom: null,
        soloStatus: soloIdle,
      }),
    ).toBeNull();
  });
});
