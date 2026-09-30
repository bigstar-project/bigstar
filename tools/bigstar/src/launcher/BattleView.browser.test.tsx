import { describe, expect, test, vi } from 'vitest';
import type { Locator } from 'vitest/browser';
import { render } from 'vitest-browser-react';
import * as Tabs from '@/components/ui/tabs';
import {
  initialForm,
  rollbackInputDelayFrames,
  rollbackInputMaxFrameLead,
} from '../form';
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
  hostedRoom: null,
  hostedRoomId: null,
  rooms: [
    {
      can_join: true,
      created_at: Date.now() - 2 * 60_000,
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

// ブラウザテストでは CSS を読み込まないので、Base UI がダイアログの背後に敷く
// 固定配置の要素が、配置されていないダイアログの上に重なる。ダイアログ内は DOM から直接押す
function clickInDialog(locator: Locator) {
  (locator.element() as HTMLElement).click();
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
  test('公開ルームをルールの要約付きで表示して選択した部屋 ID で参加する', async () => {
    const { launcherActions, screen } = await renderBattleView();

    const list = screen.getByRole('region', { name: '公開ルーム' });
    await expect.element(list).toHaveTextContent('募集中 1 件');
    await expect.element(list.getByText('Host Player')).toBeVisible();
    await expect
      .element(list.getByRole('listitem'))
      .toHaveTextContent('ランダム·3本先取·スター10·残機3');
    await expect.element(list.getByText('2分前')).toBeVisible();
    await screen
      .getByRole('button', { name: 'Host Player の部屋に参加' })
      .click();

    expect(launcherActions.joinRoom).toHaveBeenCalledWith('room12345');
  });

  test('募集中の部屋が無いときは、部屋を作って待つよう案内する', async () => {
    const { screen } = await renderBattleView({
      matchmakingRooms: { ...rooms, rooms: [] },
    });

    await expect
      .element(screen.getByText('いま募集中の部屋はありません'))
      .toBeVisible();
    await screen.getByRole('button', { name: '部屋を作って待つ' }).click();
    await expect
      .element(screen.getByRole('dialog', { name: '部屋を作る' }))
      .toBeVisible();
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

    await screen.getByRole('button', { name: '一覧を再読み込み' }).click();

    expect(launcherActions.refreshRooms).toHaveBeenCalledTimes(1);
  });

  test('部屋作成ダイアログでルールを選んで作成処理に送信する', async () => {
    const { launcherActions, screen, updateField } = await renderBattleView();

    await screen.getByRole('button', { name: '部屋を作る' }).click();
    const dialog = screen.getByRole('dialog', { name: '部屋を作る' });
    await expect.element(dialog).toBeVisible();
    await expect
      .element(screen.getByLabelText('プレイヤーネーム'))
      .not.toBeInTheDocument();
    await expect
      .element(
        dialog
          .getByRole('group', { name: '先取数' })
          .getByRole('button', { name: '3' }),
      )
      .toHaveAttribute('aria-pressed', 'true');

    clickInDialog(
      dialog
        .getByRole('group', { name: 'コース' })
        .getByRole('button', { name: '事前に選ぶ' }),
    );
    clickInDialog(
      dialog
        .getByRole('group', { name: '残機' })
        .getByRole('button', { name: '無限' }),
    );
    expect(updateField).toHaveBeenCalledWith('courseMode', 'select');
    expect(updateField).toHaveBeenCalledWith('lives', 'endless');

    clickInDialog(dialog.getByRole('button', { name: '作成して待機' }));

    expect(launcherActions.createRoom).toHaveBeenCalledTimes(1);
    await expect.element(dialog).not.toBeInTheDocument();
  });

  test('コースを事前に選ぶときは、ゲームごとのステージをダイアログの上で選べる', async () => {
    const { screen, updateField } = await renderBattleView({
      formOverride: { courseMode: 'select', courseStages: [0, 1, 2, 3, 4] },
    });

    await screen.getByRole('button', { name: '部屋を作る' }).click();
    clickInDialog(screen.getByRole('combobox', { name: 'ゲーム 2' }));
    await expect.element(screen.getByRole('listbox')).toBeVisible();
    clickInDialog(screen.getByRole('option', { name: '城' }));

    expect(updateField).toHaveBeenCalledWith('courseStages', [0, 4, 2, 3, 4]);
  });

  test('ロールバック時は先行フレーム上限の代わりに予測フレーム7を表示する', async () => {
    const { screen } = await renderBattleView({
      formOverride: {
        inputDelayFrames: 2,
        inputMaxFrameLead: 0,
        rollbackEnabled: true,
      },
    });

    await screen.getByRole('button', { name: '部屋を作る' }).click();
    const details = screen.getByRole('button', { name: /通信の詳細設定/ });
    await expect
      .element(details)
      .toHaveTextContent('遅延 2F · 予測 7F · RB 有効');
    clickInDialog(details);

    await expect
      .element(screen.getByRole('textbox', { name: '入力遅延' }))
      .toHaveValue('2');
    await expect.element(screen.getByText('予測フレーム')).toBeVisible();
    await expect.element(screen.getByText('7 F')).toBeVisible();
    await expect
      .element(screen.getByText('先行フレーム上限'))
      .not.toBeInTheDocument();
    await expect
      .element(screen.getByRole('switch', { name: 'ロールバック' }))
      .toBeChecked();
  });

  test('ロールバックを切り替えると遅延と先行フレームを推奨値に戻す', async () => {
    const { screen, updateField } = await renderBattleView();

    await screen.getByRole('button', { name: '部屋を作る' }).click();
    clickInDialog(screen.getByRole('button', { name: /通信の詳細設定/ }));
    const rollback = screen.getByRole('switch', { name: 'ロールバック' });
    await expect.element(rollback).not.toBeChecked();
    clickInDialog(rollback);

    expect(updateField).toHaveBeenCalledWith('rollbackEnabled', true);
    expect(updateField).toHaveBeenCalledWith(
      'inputDelayFrames',
      rollbackInputDelayFrames,
    );
    expect(updateField).toHaveBeenCalledWith(
      'inputMaxFrameLead',
      rollbackInputMaxFrameLead,
    );
  });

  test('部屋を公開中は相手待ちのカードと部屋コード操作を表示する', async () => {
    const { launcherActions, screen } = await renderBattleView({
      formOverride: { hostName: 'Me' },
      matchmakingRooms: {
        ...rooms,
        hostedRoom: {
          createdAtMs: Date.now() - 65_000,
          form: { ...initialForm, bigStars: 5, lives: 'endless', wins: 2 },
          roomId: 'host-room-1',
        },
        hostedRoomId: 'host-room-1',
      },
    });

    const card = screen.getByRole('region', { name: 'あなたの部屋' });
    await expect.element(card.getByText('相手を待っています')).toBeVisible();
    await expect.element(card.getByText('Me', { exact: true })).toBeVisible();
    await expect.element(card.getByText('host-room-1')).toBeVisible();
    await expect.element(card).toHaveTextContent('経過 1:05');
    await expect.element(card).toHaveTextContent('2本先取');
    await expect.element(card).toHaveTextContent('残機無限');
    // 自分の部屋を公開している間は、ほかの部屋に参加させない
    await expect
      .element(screen.getByText(/ほかに 1 部屋が募集中です。/))
      .toBeVisible();
    await expect
      .element(screen.getByRole('button', { name: /の部屋に参加/ }))
      .not.toBeInTheDocument();
    await expect
      .element(screen.getByRole('button', { name: '部屋を作る' }))
      .not.toBeInTheDocument();

    await card.getByRole('button', { name: '部屋コードをコピー' }).click();
    await card.getByRole('button', { name: '部屋を閉じる' }).click();

    expect(launcherActions.copyRoomCode).toHaveBeenCalledTimes(1);
    expect(launcherActions.cancelHostedRoom).toHaveBeenCalledTimes(1);
  });

  test('GUI更新が必要なときは公開ルームの作成と参加を無効化し、その場で更新できる', async () => {
    const { launcherActions, screen } = await renderBattleView({
      summaryOverride: { updateRequired: true, updateVersion: '0.4.0' },
    });

    const notice = screen.getByRole('alert');
    await expect.element(notice).toHaveTextContent('v0.4.0 への更新が必要です');
    await expect
      .element(notice)
      .toHaveTextContent(
        '更新するまで部屋の作成・参加はできません。更新後は自動で再起動します。',
      );
    await expect
      .element(screen.getByRole('button', { name: '部屋を作る' }))
      .toBeDisabled();
    await expect
      .element(screen.getByRole('button', { name: 'Host Player の部屋に参加' }))
      .toBeDisabled();

    await notice.getByRole('button', { name: '更新して再起動' }).click();
    expect(launcherActions.checkForUpdate).toHaveBeenCalledTimes(1);
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
      .element(screen.getByRole('alert'))
      .toHaveTextContent('公開ルームを取得できませんでした');
    await expect
      .element(screen.getByRole('button', { name: /の部屋に参加/ }))
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
