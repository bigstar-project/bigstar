import { describe, expect, test, vi } from 'vitest';
import { render } from 'vitest-browser-react';
import { Tabs } from '../components/ui';
import { initialForm } from '../form';
import { BattleView } from './BattleView';
import type {
  BattleMatchRecord,
  DiagnosticsState,
  LauncherActions,
  LauncherSummary,
  MatchmakingRoomsState,
} from './types';

const summary: LauncherSummary = {
  connectionActive: false,
  courseNote: '起動時にコース列と各試合の seed を確定します。',
  currentRomPath: 'C:\\roms\\host.nds',
  romPreparation: '再利用',
  romsConfigured: true,
  selectedStageLabel: '土管',
  updateRequired: false,
};

const romIdentity = {
  client_rom_sha256:
    '2222222222222222222222222222222222222222222222222222222222222222',
  generator_id:
    '3333333333333333333333333333333333333333333333333333333333333333',
  host_rom_sha256:
    '1111111111111111111111111111111111111111111111111111111111111111',
  rom_pair_id:
    '4444444444444444444444444444444444444444444444444444444444444444',
  bridge_sha256:
    '5555555555555555555555555555555555555555555555555555555555555555',
};

function actions(overrides: Partial<LauncherActions> = {}) {
  return {
    checkForUpdate: vi.fn(async () => {}),
    cancelHostedRoom: vi.fn(async () => {}),
    cleanupDetailedLogs: vi.fn(async () => {}),
    copyRoomCode: vi.fn(async () => {}),
    createLogArchive: vi.fn(async () => {}),
    createRoom: vi.fn(async () => {}),
    joinRoom: vi.fn(async () => {}),
    openLogDir: vi.fn(async () => {}),
    openMelonds: vi.fn(async () => {}),
    openMelondsInputConfig: vi.fn(async () => {}),
    preflightCheck: vi.fn(async () => {}),
    prepareRoms: vi.fn(async () => {}),
    refreshRooms: vi.fn(async () => {}),
    savePlayerName: vi.fn(async () => {}),
    selectBaseRomAndPrepare: vi.fn(async () => {}),
    selectRomPath: vi.fn(async () => {}),
    setStartupEnabled: vi.fn(async () => {}),
    startMatch: vi.fn(async () => {}),
    stopMatch: vi.fn(async () => {}),
    uploadLogArchive: vi.fn(async () => {}),
    ...overrides,
  };
}

const rooms: MatchmakingRoomsState = {
  busy: false,
  error: null,
  loading: false,
  refreshDisabled: false,
  hostedRoomId: null,
  rooms: [
    {
      can_join: true,
      created_at: 1,
      expires_at: Date.now() + 600_000,
      host_name: 'Host Player',
      peer_count: 1,
      room_id: 'room12345',
      rom_identity: romIdentity,
      settings: {
        big_stars: 10,
        course_mode: 'random',
        course_stages: [0, 1, 2, 3, 4],
        input_delay_frames: 4,
        input_max_frame_lead: 4,
        lives: '3',
        match_seed: '123',
        rng_seeds: ['123', '124', '125', '126', '127'],
        rollback_enabled: false,
        wins: 3,
      },
      status: 'open',
      updated_at: 1,
    },
  ],
};

const currentMatch: BattleMatchRecord = {
  id: 'C:\\logs\\run1',
  logDir: 'C:\\logs\\run1',
  playerNames: {
    mario: 'Alice',
    luigi: 'Bob',
  },
  playerIds: {
    mario: '11111111-1111-4111-8111-111111111111',
    luigi: '22222222-2222-4222-8222-222222222222',
  },
  role: 'host',
  roomCode: 'test-room',
  settings: {
    big_stars: 5,
    course_mode: 'select',
    course_stages: [2, 3, 4],
    input_delay_frames: 4,
    input_max_frame_lead: 4,
    lives: '3',
    match_seed: '123',
    rng_seeds: ['123', '124', '125'],
    rollback_enabled: false,
    wins: 2,
  },
  stages: [
    {
      frame: 4320,
      game_index: 1,
      line: 'NSMB MvL auto restart: result inst=0 frame=4320 winner=0 stars=5/0 displayed=5/0 collected=5/0 lives=3/2 deaths=0/1 dead=0/0 matchWins=1/0 target=2',
      luigi: {
        collected_stars: 0,
        dead: false,
        deaths: 1,
        displayed_stars: 0,
        lives: 2,
        stars: 0,
      },
      luigi_match_wins: 0,
      mario: {
        collected_stars: 5,
        dead: false,
        deaths: 0,
        displayed_stars: 5,
        lives: 3,
        stars: 5,
      },
      mario_match_wins: 1,
      resolved: true,
      stage: 2,
      target_wins: 2,
      winner: 0,
    },
  ],
  startedAt: '2026-06-20T12:00:00.000Z',
  status: 'running',
};

async function renderBattleView(
  props: {
    actionOverrides?: Partial<LauncherActions>;
    diagnostics?: DiagnosticsState;
    currentMatch?: BattleMatchRecord | null;
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
        diagnostics={
          props.diagnostics ?? {
            bridgeDiagnostics: null,
            gameStateMismatch: null,
          }
        }
        form={{
          ...initialForm,
          baseRomPath: 'C:\\roms\\base.nds',
          hostRomPath: 'C:\\roms\\host.nds',
          matchSeed: '123',
          roomCode: 'test-room',
          signalUrl: 'ws://127.0.0.1:8787/session',
        }}
        matchmakingRooms={props.matchmakingRooms ?? rooms}
        currentMatch={props.currentMatch ?? null}
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
          'room12345 / Course=random[0/1/2/3/4] Wins=3 Star=10 Lives=3 Delay=4 Lead=4 RB=off',
        ),
      )
      .toBeVisible();
    await screen.getByRole('button', { name: '参加' }).click();

    expect(launcherActions.joinRoom).toHaveBeenCalledWith('room12345');
  });

  test('現在の対戦状況にステージ結果を表示する', async () => {
    const { screen } = await renderBattleView({ currentMatch });
    const startedTime = new Date(currentMatch.startedAt).toLocaleTimeString(
      'ja-JP',
      {
        hour: '2-digit',
        minute: '2-digit',
      },
    );
    const startedDate = new Date(currentMatch.startedAt).toLocaleDateString(
      'ja-JP',
      {
        day: '2-digit',
        month: '2-digit',
      },
    );

    await expect.element(screen.getByText('現在の対戦状況')).toBeVisible();
    await expect.element(screen.getByText('対戦中')).toBeVisible();
    await expect.element(screen.getByText('1 - 0')).toBeVisible();
    await expect.element(screen.getByText(startedTime)).not.toBeInTheDocument();
    await expect.element(screen.getByText(startedDate)).not.toBeInTheDocument();
    await expect
      .element(screen.getByTestId('stage-dots'))
      .not.toBeInTheDocument();
    await expect.element(screen.getByText('ゲーム')).toBeVisible();
    await expect.element(screen.getByText('雪')).toBeVisible();
    await expect.element(screen.getByText('Game 1')).not.toBeInTheDocument();
    await expect.element(screen.getByText('未プレイ')).not.toBeInTheDocument();
    await expect.element(screen.getByText('勝者')).toBeVisible();
    const headers = [...document.querySelectorAll('th')].map((header) =>
      header.textContent?.trim(),
    );
    expect(headers.slice(3, 5)).toEqual(['Alice', 'Bob']);
    await expect.element(screen.getByText('C:\\logs\\run1')).toBeVisible();
    await expect.element(screen.getByText('5 / 0')).not.toBeInTheDocument();
    await expect.element(screen.getByText('3 / 2')).not.toBeInTheDocument();
    await expect.element(screen.getByText(/死亡/)).not.toBeInTheDocument();
  });

  test('参加側でも自身を左、相手を右に表示する', async () => {
    const { screen } = await renderBattleView({
      currentMatch: { ...currentMatch, role: 'client' },
    });

    await expect.element(screen.getByText('0 - 1')).toBeVisible();
    await expect.element(screen.getByText('1 - 0')).not.toBeInTheDocument();

    const leftPlayer = document.querySelector('[data-player-position="left"]');
    const rightPlayer = document.querySelector(
      '[data-player-position="right"]',
    );
    expect(leftPlayer?.textContent).toContain('Bob');
    expect(rightPlayer?.textContent).toContain('Alice');

    const headers = [...document.querySelectorAll('th')].map((header) =>
      header.textContent?.trim(),
    );
    expect(headers.slice(3, 5)).toEqual(['Bob', 'Alice']);
  });

  test('公開ルームを手動更新する', async () => {
    const { launcherActions, screen } = await renderBattleView();

    await screen.getByRole('button', { name: '更新' }).click();

    expect(launcherActions.refreshRooms).toHaveBeenCalledTimes(1);
  });

  test('部屋作成ダイアログを開いて作成処理に送信する', async () => {
    const { launcherActions, screen } = await renderBattleView();

    await screen.getByRole('button', { name: '部屋を作る' }).click();
    await expect.element(screen.getByRole('dialog')).toBeVisible();
    await expect
      .element(screen.getByLabelText('プレイヤーネーム'))
      .not.toBeInTheDocument();
    await screen.getByRole('combobox', { name: 'コース' }).click();
    const openSelect = document.querySelector<HTMLElement>(
      '[data-scope="select"][data-part="content"][data-state="open"]',
    );
    const dialog = document.querySelector<HTMLElement>(
      '[data-scope="dialog"][data-part="content"]',
    );
    expect(openSelect).not.toBeNull();
    expect(dialog).not.toBeNull();
    if (!openSelect || !dialog) throw new Error('浮遊レイヤーが見つかりません');
    const selectRect = openSelect.getBoundingClientRect();
    const topElement = document.elementFromPoint(
      selectRect.left + selectRect.width / 2,
      selectRect.top + selectRect.height / 2,
    );
    expect(openSelect.contains(topElement)).toBe(true);
    await screen.getByRole('combobox', { name: 'コース' }).click();
    await screen.getByRole('button', { name: '作成して待機' }).click();

    expect(launcherActions.createRoom).toHaveBeenCalledTimes(1);
  });

  test('部屋作成後の待機状態で部屋コード操作を表示する', async () => {
    const { launcherActions, screen } = await renderBattleView({
      matchmakingRooms: {
        ...rooms,
        hostedRoomId: 'host-room-1',
        rooms: [],
      },
    });

    await expect
      .element(screen.getByText('参加者を待っています'))
      .toBeVisible();
    await expect.element(screen.getByText('host-room-1')).toBeVisible();

    await screen.getByRole('button', { name: '部屋コードをコピー' }).click();
    await screen.getByRole('button', { name: '部屋を閉じる' }).click();

    expect(launcherActions.copyRoomCode).toHaveBeenCalledTimes(1);
    expect(launcherActions.cancelHostedRoom).toHaveBeenCalledTimes(1);
  });

  test('GUI更新が必要なときは公開ルームの作成と参加を無効化する', async () => {
    const { screen } = await renderBattleView({
      summaryOverride: { updateRequired: true, updateVersion: '0.4.0' },
    });

    await expect
      .element(screen.getByText('GUI の更新が必要です'))
      .toBeVisible();
    await expect
      .element(
        screen.getByText(
          'v0.4.0 に更新するまで、部屋の作成・参加はできません。画面左下の更新ボタンから更新してください。',
        ),
      )
      .toBeVisible();
    await expect
      .element(screen.getByRole('button', { name: '部屋を作る' }))
      .toBeDisabled();
    await expect
      .element(screen.getByRole('button', { name: '参加' }))
      .toBeDisabled();
  });

  test('接続中の取得エラーでは参加操作を表示しない', async () => {
    const { screen } = await renderBattleView({
      matchmakingRooms: {
        ...rooms,
        error: 'room is not joinable',
      },
      summaryOverride: { connectionActive: true },
    });

    await expect
      .element(
        screen.getByText(
          '公開ルームを取得できませんでした。更新をお試しください。',
        ),
      )
      .toBeVisible();
    await expect
      .element(screen.getByRole('button', { name: '参加' }))
      .not.toBeInTheDocument();
    await expect
      .element(screen.getByRole('button', { name: '停止' }))
      .toBeVisible();
  });

  test('ゲーム状態ミスマッチを警告として表示する', async () => {
    const { screen } = await renderBattleView({
      diagnostics: {
        bridgeDiagnostics: null,
        gameStateMismatch: {
          basic_matches: false,
          frame: 240,
          instance: 0,
          line: 'NSMB MvL Netplay: game state mismatch inst=0 frame=240 local=00000000000000AA remote=00000000000000BB basic=0 playerGlobal=1 wifiCandidate=0 renderCandidate=1',
          local_hash: '00000000000000AA',
          player_global_matches: true,
          remote_hash: '00000000000000BB',
          render_candidate_matches: true,
          wifi_candidate_matches: false,
        },
      },
      summaryOverride: { connectionActive: true },
    });

    await expect
      .element(screen.getByText('ゲーム状態ミスマッチ', { exact: true }))
      .toBeVisible();
    await expect
      .element(screen.getByText(/NSMB MvL Netplay: game state mismatch/))
      .toBeVisible();
  });
});
