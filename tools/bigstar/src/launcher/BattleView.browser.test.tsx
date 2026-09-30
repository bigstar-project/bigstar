import { describe, expect, test, vi } from 'vitest';
import { render } from 'vitest-browser-react';
import { Tabs } from '../components/park-ui';
import { initialForm } from '../form';
import { BattleView } from './BattleView';
import type {
  BattleMatchRecord,
  ConnectionStatusState,
  LauncherActions,
  LauncherSummary,
  MatchmakingRoomsState,
} from './types';

const summary: LauncherSummary = {
  connectionActive: false,
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
  save_sha256:
    '6666666666666666666666666666666666666666666666666666666666666666',
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
    uploadLogArchive: vi.fn(async () => null),
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

const connected: ConnectionStatusState = {
  active: true,
  kind: 'ok',
  text: '接続済み',
};

async function renderBattleView(
  props: {
    actionOverrides?: Partial<LauncherActions>;
    connectionStatus?: ConnectionStatusState;
    currentMatch?: BattleMatchRecord | null;
    formOverride?: Partial<typeof initialForm>;
    matchmakingRooms?: MatchmakingRoomsState;
    showMatch?: boolean;
    summaryOverride?: Partial<LauncherSummary>;
  } = {},
) {
  const launcherActions = actions(props.actionOverrides);
  const updateField = vi.fn();
  const onOpenHistory = vi.fn();
  const onReturnToLobby = vi.fn();
  const matchProp = props.currentMatch ?? null;

  const screen = await render(
    <Tabs.Root value="battle">
      <BattleView
        actions={launcherActions}
        form={{
          ...initialForm,
          baseRomPath: 'C:\\roms\\base.nds',
          hostRomPath: 'C:\\roms\\host.nds',
          matchSeed: '123',
          roomCode: 'test-room',
          signalUrl: 'ws://127.0.0.1:8787/session',
          ...props.formOverride,
        }}
        connectionStatus={
          props.connectionStatus ?? {
            active: false,
            kind: 'idle',
            text: '未接続',
          }
        }
        matchmakingRooms={props.matchmakingRooms ?? rooms}
        currentMatch={matchProp}
        onOpenHistory={onOpenHistory}
        onReturnToLobby={onReturnToLobby}
        showMatch={props.showMatch ?? matchProp !== null}
        summary={{ ...summary, ...props.summaryOverride }}
        updateField={updateField}
      />
    </Tabs.Root>,
  );

  return {
    launcherActions,
    onOpenHistory,
    onReturnToLobby,
    screen,
    updateField,
  };
}

/** 結果表の各行を、行見出しとセルの読み上げ名で返す */
function gameRows() {
  return [...document.querySelectorAll('tbody tr')].map((row) =>
    [...row.querySelectorAll('th, td')].map(
      (cell) => cell.getAttribute('aria-label') ?? cell.textContent?.trim(),
    ),
  );
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

  test('対戦中はロビーの代わりにスコアとゲームごとの結果を表示する', async () => {
    const { screen } = await renderBattleView({
      connectionStatus: connected,
      currentMatch,
      summaryOverride: { connectionActive: true },
    });

    await expect
      .element(screen.getByText('Host Player'))
      .not.toBeInTheDocument();
    await expect
      .element(screen.getByRole('status'))
      .toHaveTextContent('第2ゲーム 進行中');
    await expect.element(screen.getByText('コース指定')).toBeVisible();
    await expect.element(screen.getByText('2本先取')).toBeVisible();
    await expect
      .element(screen.getByRole('img', { name: '1 対 0' }))
      .toBeVisible();
    await expect
      .element(screen.getByRole('columnheader', { name: '第1ゲーム 雪' }))
      .toBeVisible();
    await expect
      .element(
        screen.getByRole('columnheader', { name: '第2ゲーム 土管 プレイ中' }),
      )
      .toBeVisible();
    await expect
      .element(screen.getByRole('columnheader', { name: '第3ゲーム' }))
      .toBeVisible();
    expect(gameRows()).toEqual([
      ['あなた', 'スター 5、残機 3、勝ち', 'プレイ中', '未実施'],
      ['Bob', 'スター 0、残機 2', 'プレイ中', '未実施'],
    ]);
    await expect
      .element(screen.getByText('melonDS のウィンドウでプレイしてください。'))
      .toBeVisible();
    await expect
      .element(screen.getByRole('button', { name: 'ロビーに戻る' }))
      .not.toBeInTheDocument();
  });

  test('参加側でも自身を左、相手を右に表示する', async () => {
    const { screen } = await renderBattleView({
      connectionStatus: connected,
      currentMatch: { ...currentMatch, role: 'client' },
    });

    await expect
      .element(screen.getByRole('img', { name: '0 対 1' }))
      .toBeVisible();
    expect(gameRows()).toEqual([
      ['あなた', 'スター 0、残機 2', 'プレイ中', '未実施'],
      ['Alice', 'スター 5、残機 3、勝ち', 'プレイ中', '未実施'],
    ]);
  });

  test('対戦を中止する', async () => {
    const { launcherActions, screen } = await renderBattleView({
      connectionStatus: connected,
      currentMatch,
      summaryOverride: { connectionActive: true },
    });

    await screen.getByRole('button', { name: '対戦を中止' }).click();

    expect(launcherActions.stopMatch).toHaveBeenCalledTimes(1);
  });

  test('再接続中は残り時間と通信待ちを表示する', async () => {
    const { screen } = await renderBattleView({
      connectionStatus: {
        active: true,
        kind: 'warn',
        text: '再接続中… 残り42秒',
        recoveryDeadlineMs: Date.now() + 42_000,
      },
      currentMatch,
      summaryOverride: { connectionActive: true },
    });

    await expect
      .element(screen.getByRole('status'))
      .toHaveTextContent('再接続中…');
    await expect
      .element(screen.getByRole('alert'))
      .toHaveTextContent('相手との通信が途切れました');
    await expect.element(screen.getByText(/^0:4[12]$/)).toBeVisible();
    // CSS を読み込まないテストではバーに大きさが無いので、値だけを確かめる
    await expect
      .element(screen.getByRole('progressbar', { name: '再接続の残り時間' }))
      .toHaveAttribute('aria-valuemax', '60');
    await expect
      .element(
        screen.getByRole('columnheader', { name: '第2ゲーム 土管 通信待ち' }),
      )
      .toBeVisible();
  });

  test('再接続がタイムアウトしたら中断として終了後の操作を表示する', async () => {
    const { onOpenHistory, onReturnToLobby, screen } = await renderBattleView({
      connectionStatus: {
        active: false,
        kind: 'error',
        text: '再接続がタイムアウトしました',
        recoveryTimedOut: true,
      },
      currentMatch: { ...currentMatch, status: 'stopped' },
    });

    await expect
      .element(screen.getByRole('alert'))
      .toHaveTextContent('再接続がタイムアウトしました');
    await expect
      .element(
        screen.getByRole('columnheader', { name: '第2ゲーム 土管 中断' }),
      )
      .toBeVisible();
    await expect
      .element(screen.getByRole('button', { name: '対戦を中止' }))
      .not.toBeInTheDocument();

    await screen.getByRole('button', { name: '履歴で詳しく見る' }).click();
    await screen.getByRole('button', { name: 'ロビーに戻る' }).click();

    expect(onOpenHistory).toHaveBeenCalledTimes(1);
    expect(onReturnToLobby).toHaveBeenCalledTimes(1);
  });

  test('中止した対戦では遊んでいたゲームを中断として残す', async () => {
    const { screen } = await renderBattleView({
      currentMatch: { ...currentMatch, status: 'stopped' },
    });

    await expect
      .element(screen.getByRole('status'))
      .toHaveTextContent('対戦を中断しました');
    await expect.element(screen.getByRole('alert')).not.toBeInTheDocument();
    expect(gameRows()).toEqual([
      ['あなた', 'スター 5、残機 3、勝ち', '中断', '未実施'],
      ['Bob', 'スター 0、残機 2', '中断', '未実施'],
    ]);
    await expect
      .element(screen.getByRole('button', { name: 'ロビーに戻る' }))
      .toBeVisible();
  });

  test('対戦が終わったら勝敗を表示し、遊んだゲームだけを並べる', async () => {
    const [firstGame] = currentMatch.stages;
    const { screen } = await renderBattleView({
      currentMatch: {
        ...currentMatch,
        stages: [
          ...currentMatch.stages,
          { ...firstGame, game_index: 2, mario_match_wins: 2, stage: 3 },
        ],
        status: 'completed',
      },
    });

    await expect
      .element(screen.getByRole('status'))
      .toHaveTextContent('対戦終了');
    await expect.element(screen.getByText('勝利')).toBeVisible();
    await expect
      .element(screen.getByRole('img', { name: '2 対 0' }))
      .toBeVisible();
    await expect
      .element(screen.getByRole('columnheader', { name: '第2ゲーム 土管' }))
      .toBeVisible();
    await expect
      .element(screen.getByRole('columnheader', { name: '第3ゲーム' }))
      .not.toBeInTheDocument();
  });

  test('閉じた対戦の代わりにロビーを表示する', async () => {
    const { screen } = await renderBattleView({
      currentMatch: { ...currentMatch, status: 'completed' },
      showMatch: false,
    });

    await expect.element(screen.getByText('Host Player')).toBeVisible();
    await expect
      .element(screen.getByRole('table', { name: 'ゲームごとの結果' }))
      .not.toBeInTheDocument();
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

  test('ロールバック時は旧Leadではなく予測上限7を表示する', async () => {
    const { screen } = await renderBattleView({
      formOverride: {
        inputDelayFrames: 2,
        inputMaxFrameLead: 0,
        rollbackEnabled: true,
      },
    });

    await screen.getByRole('button', { name: '部屋を作る' }).click();
    await expect
      .element(screen.getByLabelText('PredictionHorizonFrames'))
      .toHaveValue(7);
    await expect
      .element(screen.getByLabelText('PredictionHorizonFrames'))
      .toBeDisabled();
    await expect
      .element(screen.getByLabelText('InputMaxFrameLead'))
      .not.toBeInTheDocument();
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

  test('接続状況の診断パネルを表示しない', async () => {
    const { screen } = await renderBattleView();

    await expect
      .element(screen.getByText('接続状況', { exact: true }))
      .not.toBeInTheDocument();
  });
});
