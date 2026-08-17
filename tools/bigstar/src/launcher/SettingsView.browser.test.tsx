import { afterEach, describe, expect, test, vi } from 'vitest';
import { render } from 'vitest-browser-react';
import { Tabs } from '../components/ui';
import { initialForm } from '../form';
import { SettingsView } from './SettingsView';
import type { LauncherActions } from './types';

afterEach(() => {
  vi.unstubAllGlobals();
});

async function renderSettingsView(
  configurableSignalServer = true,
  edition: 'insiders' | 'public' = 'insiders',
) {
  vi.stubGlobal('__BIGSTAR_EDITION_CONFIG__', {
    badge: edition === 'insiders' ? 'Insiders' : 'Public',
    displayName: edition === 'insiders' ? 'Bigstar Insiders' : 'Bigstar',
    edition,
  });
  vi.stubGlobal('__BIGSTAR_RUNTIME_CAPABILITIES__', {
    aiDevTools: true,
    automaticUnresolvedSessionReport: true,
    configurableSignalServer,
    feedbackSubmission: true,
    notifyOwnRooms: true,
  });
  const launcherActions = {
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
  } satisfies LauncherActions;
  const updateField = vi.fn();

  const screen = await render(
    <Tabs.Root value="settings">
      <SettingsView
        actions={launcherActions}
        form={{
          ...initialForm,
          baseRomPath: 'C:\\roms\\base.nds',
          hostName: 'Player',
          hostRomPath: 'C:\\roms\\host.nds',
          roomCode: 'test-room',
          signalUrl: 'ws://127.0.0.1:8787/session',
        }}
        startup={{ enabled: false, loading: false }}
        updateField={updateField}
      />
    </Tabs.Root>,
  );

  return { launcherActions, screen, updateField };
}

describe('設定ビュー', () => {
  test('設定セクションを指定された順番で表示する', async () => {
    await renderSettingsView();

    const headings = Array.from(document.querySelectorAll('h2')).map(
      (heading) => heading.textContent?.trim(),
    );

    expect(headings.slice(0, 5)).toEqual([
      'プロフィール',
      '常駐・通知',
      'melonDS設定',
      'ROM設定',
      '接続設定',
    ]);
  });

  test('プレイヤーネームを更新して保存する', async () => {
    const { launcherActions, screen, updateField } = await renderSettingsView();

    await screen.getByLabelText('プレイヤーネーム').fill('Alice');
    await screen.getByRole('button', { name: '保存' }).click();

    expect(updateField).toHaveBeenCalledWith('hostName', 'Alice');
    expect(launcherActions.savePlayerName).toHaveBeenCalledTimes(1);
  });

  test('localではシグナリングサーバーとUDPポートを更新する', async () => {
    const { screen, updateField } = await renderSettingsView();

    await screen
      .getByLabelText('シグナリングサーバー')
      .fill('wss://match.test/session');
    await screen.getByLabelText('UDP ポート').fill('9000');
    expect(updateField).toHaveBeenCalledWith(
      'signalUrl',
      'wss://match.test/session',
    );
    expect(updateField).toHaveBeenCalledWith('port', 9000);
  });

  test('Public distributionではシグナリングサーバー設定を表示しない', async () => {
    const { screen } = await renderSettingsView(false, 'public');

    expect(document.body.textContent).not.toContain('シグナリングサーバー');
    expect(document.body.textContent).not.toContain('接続確認');
    expect(document.body.textContent).not.toContain(
      'ws://127.0.0.1:8787/session',
    );
    await expect.element(screen.getByLabelText('UDP ポート')).toBeVisible();
  });

  test('Insiders distributionではシグナリングサーバーを更新できる', async () => {
    const { screen, updateField } = await renderSettingsView(true, 'insiders');

    await screen
      .getByLabelText('シグナリングサーバー')
      .fill('wss://insiders.test/session');

    expect(updateField).toHaveBeenCalledWith(
      'signalUrl',
      'wss://insiders.test/session',
    );
  });

  test.each([
    false,
    true,
  ])('Publicでは内部ログ管理を表示しない', async (configurableSignalServer) => {
    await renderSettingsView(configurableSignalServer, 'public');

    expect(document.body.textContent).not.toContain('パフォーマンスログ');
    expect(document.body.textContent).not.toContain('古い詳細ログを削除');
  });

  test('Public distributionでは詳細診断設定を表示しない', async () => {
    await renderSettingsView(false, 'public');

    expect(document.body.textContent).not.toContain('診断イベントログ');
    expect(document.body.textContent).not.toContain(
      '入力、通信、画面状態のログを増やして原因調査しやすくします',
    );
    expect(document.body.textContent).not.toContain('AI用プレイログ');
  });

  test('ロムとmelonDS関連処理を実行する', async () => {
    const { launcherActions, screen } = await renderSettingsView();

    await screen.getByRole('button', { name: '参照' }).click();
    await screen.getByRole('button', { name: 'melonDS を開く' }).click();
    await screen.getByRole('button', { name: '入力設定を開く' }).click();

    expect(launcherActions.selectRomPath).toHaveBeenCalledWith('baseRomPath');
    expect(launcherActions.openMelonds).toHaveBeenCalledTimes(1);
    expect(launcherActions.openMelondsInputConfig).toHaveBeenCalledTimes(1);
  });

  test.each([
    'insiders',
    'public',
  ] as const)('全エディションで手動の起動前チェックとROM準備を表示しない', async (edition) => {
    const { screen } = await renderSettingsView(false, edition);

    await expect
      .element(screen.getByRole('button', { name: '起動前チェック' }))
      .not.toBeInTheDocument();
    await expect
      .element(screen.getByRole('button', { name: '共通 ROM を準備' }))
      .not.toBeInTheDocument();
  });

  test('スタートアップ起動をSwitchで切り替える', async () => {
    const { launcherActions, screen } = await renderSettingsView();

    await screen.getByText('Windowsログイン時に起動').click();

    expect(launcherActions.setStartupEnabled).toHaveBeenCalledWith(true);
  });

  test('新規部屋通知をSwitchで切り替える', async () => {
    const { screen, updateField } = await renderSettingsView();

    await screen.getByText('新規部屋通知').click();

    expect(updateField).toHaveBeenCalledWith(
      'newRoomNotificationsEnabled',
      false,
    );
  });

  test('詳細ログをSwitchで切り替える', async () => {
    const { screen, updateField } = await renderSettingsView();

    await screen.getByText('詳細ログ', { exact: true }).click();

    expect(updateField).toHaveBeenCalledWith('detailedLogsEnabled', true);
  });

  test('AI用プレイログをSwitchで切り替える', async () => {
    const { screen, updateField } = await renderSettingsView();

    await screen.getByText('AI用プレイログ').click();

    expect(updateField).toHaveBeenCalledWith('aiPlayLogEnabled', true);
  });

  test('パフォーマンスログをSwitchで切り替える', async () => {
    const { screen, updateField } = await renderSettingsView();

    await screen.getByText('パフォーマンスログ', { exact: true }).click();

    expect(updateField).toHaveBeenCalledWith('performanceLogsEnabled', true);
  });

  test('古い詳細ログの削除を確認して実行する', async () => {
    const { launcherActions, screen } = await renderSettingsView();

    await screen.getByRole('button', { name: '古い詳細ログを削除' }).click();
    await screen.getByRole('button', { name: '削除する' }).click();

    expect(launcherActions.cleanupDetailedLogs).toHaveBeenCalledTimes(1);
  });

  test('現在の構成パネルを表示しない', async () => {
    const { screen } = await renderSettingsView();

    await expect
      .element(screen.getByText('現在の構成', { exact: true }))
      .not.toBeInTheDocument();
  });
});
