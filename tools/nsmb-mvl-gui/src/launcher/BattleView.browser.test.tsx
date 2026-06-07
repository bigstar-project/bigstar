import { describe, expect, test, vi } from 'vitest';
import { render } from 'vitest-browser-react';
import { Tabs } from '../components/ui';
import { initialForm } from '../form';
import { BattleView } from './BattleView';
import type {
  LauncherActions,
  LauncherSummary,
  MatchmakingRoomsState,
} from './types';

const summary: LauncherSummary = {
  connectionActive: false,
  courseNote: 'Match seed から stage 0-4 を決めます。',
  currentRomPath: 'C:\\roms\\host.nds',
  romPreparation: '再利用',
  romsConfigured: true,
  selectedStageLabel: '3',
};

function actions(overrides: Partial<LauncherActions> = {}) {
  return {
    checkForUpdate: vi.fn(async () => {}),
    copyRoomCode: vi.fn(async () => {}),
    createRoom: vi.fn(async () => {}),
    joinRoom: vi.fn(async () => {}),
    openLogDir: vi.fn(async () => {}),
    openMelonds: vi.fn(async () => {}),
    openMelondsInputConfig: vi.fn(async () => {}),
    pollStatus: vi.fn(async () => {}),
    preflightCheck: vi.fn(async () => {}),
    prepareRoms: vi.fn(async () => {}),
    refreshRooms: vi.fn(async () => {}),
    selectBaseRomAndPrepare: vi.fn(async () => {}),
    selectRomPath: vi.fn(async () => {}),
    startMatch: vi.fn(async () => {}),
    stopMatch: vi.fn(async () => {}),
    ...overrides,
  };
}

const rooms: MatchmakingRoomsState = {
  busy: false,
  error: null,
  loading: false,
  refreshDisabled: false,
  rooms: [
    {
      can_join: true,
      created_at: 1,
      expires_at: Date.now() + 600_000,
      host_name: 'Host Player',
      peer_count: 1,
      room_id: 'room12345',
      settings: {
        big_stars: 10,
        course_mode: 'random',
        input_delay_frames: 4,
        input_max_frame_lead: 4,
        lives: '3',
        match_seed: '123',
        rollback_enabled: false,
        wins: 3,
      },
      status: 'open',
      updated_at: 1,
    },
  ],
};

async function renderBattleView(
  props: {
    actionOverrides?: Partial<LauncherActions>;
    matchmakingRooms?: MatchmakingRoomsState;
    summaryOverride?: Partial<LauncherSummary>;
  } = {},
) {
  const launcherActions = actions(props.actionOverrides);
  const updateField = vi.fn();

  const screen = await render(
    <Tabs.Root value="battle">
      <BattleView
        actions={launcherActions}
        diagnostics={{ bridgeDiagnostics: null }}
        form={{
          ...initialForm,
          baseRomPath: 'C:\\roms\\base.nds',
          hostRomPath: 'C:\\roms\\host.nds',
          matchSeed: '123',
          roomCode: 'test-room',
          signalUrl: 'ws://127.0.0.1:8787/session',
        }}
        lastLogDir="C:\\logs\\run1"
        matchmakingRooms={props.matchmakingRooms ?? rooms}
        summary={{ ...summary, ...props.summaryOverride }}
        updateField={updateField}
      />
    </Tabs.Root>,
  );

  return { launcherActions, screen, updateField };
}

describe('対戦ビュー', () => {
  test('公開ルームを表示して選択した部屋 ID で参加する', async () => {
    const { launcherActions, screen } = await renderBattleView();

    await expect.element(screen.getByText('Host Player')).toBeVisible();
    await expect
      .element(
        screen.getByText(
          'room12345 / Course=random Wins=3 Star=10 Lives=3 Delay=4 Lead=4 RB=off',
        ),
      )
      .toBeVisible();
    await screen.getByRole('button', { name: '参加' }).click();

    expect(launcherActions.joinRoom).toHaveBeenCalledWith('room12345');
  });

  test('公開ルームを手動更新する', async () => {
    const { launcherActions, screen } = await renderBattleView();

    await screen.getByRole('button', { name: '更新' }).click();

    expect(launcherActions.refreshRooms).toHaveBeenCalledTimes(1);
  });

  test('部屋作成ダイアログを開いて作成処理に送信する', async () => {
    const { launcherActions, screen, updateField } = await renderBattleView();

    await screen.getByRole('button', { name: '部屋を作る' }).click();
    await expect.element(screen.getByRole('dialog')).toBeVisible();
    await screen.getByLabelText('ホスト名').fill('Alice');
    await screen.getByRole('button', { name: '作成して起動' }).click();

    expect(updateField).toHaveBeenCalledWith('hostName', 'Alice');
    expect(launcherActions.createRoom).toHaveBeenCalledTimes(1);
  });

  test('手動開始、ロール更新、ログ操作の処理を呼ぶ', async () => {
    const { launcherActions, screen, updateField } = await renderBattleView();

    await screen.getByText('部屋コードとロールを編集').click();
    await screen.getByRole('button', { name: 'answer側' }).click();
    await screen.getByRole('button', { name: '対戦を開始' }).click();
    await screen.getByRole('button', { name: 'ログを開く' }).click();

    expect(updateField).toHaveBeenCalledWith('role', 'client');
    expect(launcherActions.startMatch).toHaveBeenCalledTimes(1);
    expect(launcherActions.openLogDir).toHaveBeenCalledTimes(1);
  });

  test('接続中はマッチメイキングエラーを表示して参加を無効化する', async () => {
    const { screen } = await renderBattleView({
      matchmakingRooms: {
        ...rooms,
        error: 'room is not joinable',
      },
      summaryOverride: { connectionActive: true },
    });

    await expect
      .element(screen.getByText('room is not joinable'))
      .toBeVisible();
    await expect
      .element(screen.getByRole('button', { name: '参加' }))
      .toBeDisabled();
    await expect
      .element(screen.getByRole('button', { name: '停止' }))
      .toBeVisible();
  });
});
