import { afterEach, describe, expect, test, vi } from 'vitest';
import { render } from 'vitest-browser-react';
import { Tabs } from '../components/ui';
import { initialForm } from '../form';
import { SettingsView } from './SettingsView';
import type { LauncherActions, LauncherSummary } from './types';

const summary: LauncherSummary = {
  connectionActive: false,
  courseNote: '起動時にコース列と各試合の seed を確定します。',
  currentRomPath: 'C:\\roms\\host.nds',
  romPreparation: '再利用',
  romsConfigured: true,
  selectedStageLabel: '土管',
  updateRequired: false,
};

afterEach(() => {
  vi.unstubAllGlobals();
});

async function renderSettingsView(configurableSignalServer = true) {
  vi.stubGlobal('__NSMB_MVL_RUNTIME_CAPABILITIES__', {
    aiDevTools: true,
    automaticUnresolvedSessionReport: true,
    configurableSignalServer,
    manualLogUpload: true,
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
    uploadLogArchive: vi.fn(async () => {}),
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
        summary={summary}
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

  test('distributionではシグナリングサーバー設定を表示しない', async () => {
    const { screen } = await renderSettingsView(false);

    expect(document.body.textContent).not.toContain('シグナリングサーバー');
    expect(document.body.textContent).not.toContain('接続確認');
    expect(document.body.textContent).not.toContain(
      'ws://127.0.0.1:8787/session',
    );
    await expect.element(screen.getByLabelText('UDP ポート')).toBeVisible();
  });

  test('ロム、起動前チェック、melonDS 関連処理を実行する', async () => {
    const { launcherActions, screen } = await renderSettingsView();

    await screen.getByRole('button', { name: '参照' }).click();
    await screen.getByRole('button', { name: 'melonDS を開く' }).click();
    await screen.getByRole('button', { name: '入力設定を開く' }).click();
    await screen.getByRole('button', { name: '起動前チェック' }).click();
    await screen.getByRole('button', { name: '共通 ROM を準備' }).click();

    expect(launcherActions.selectRomPath).toHaveBeenCalledWith('baseRomPath');
    expect(launcherActions.openMelonds).toHaveBeenCalledTimes(1);
    expect(launcherActions.openMelondsInputConfig).toHaveBeenCalledTimes(1);
    expect(launcherActions.preflightCheck).toHaveBeenCalledTimes(1);
    expect(launcherActions.prepareRoms).toHaveBeenCalledTimes(1);
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

  test('現在の設定状態を要約して表示する', async () => {
    const { screen } = await renderSettingsView();

    await expect.element(screen.getByText('対戦準備 OK！')).toBeVisible();
    await expect.element(screen.getByText('host.nds')).toBeVisible();
    await expect
      .element(screen.getByText('ws://127.0.0.1:8787/session'))
      .toBeVisible();
    await expect.element(screen.getByText('8165')).toBeVisible();
  });
});
