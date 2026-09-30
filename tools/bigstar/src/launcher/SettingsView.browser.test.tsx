import { useState } from 'react';
import { afterEach, describe, expect, test, vi } from 'vitest';
import { type Locator, userEvent } from 'vitest/browser';
import { render } from 'vitest-browser-react';
import * as Tabs from '@/components/ui/tabs';
import { initialForm } from '../form';
import type { FormState } from '../types';
import { SettingsView } from './SettingsView';
import type { LauncherActions, UpdateFormField } from './types';

afterEach(() => {
  vi.unstubAllGlobals();
});

// 入力欄を離れたときの保存を確かめるため、アプリと同じようにフォームの状態を持つ
function SettingsHarness({
  actions,
  onUpdateField,
}: {
  actions: LauncherActions;
  onUpdateField: UpdateFormField;
}) {
  const [form, setForm] = useState<FormState>({
    ...initialForm,
    baseRomPath: 'C:\\roms\\base.nds',
    hostName: 'Player',
    hostRomPath: 'C:\\roms\\host.nds',
    roomCode: 'test-room',
    signalUrl: 'ws://127.0.0.1:8787/session',
  });
  const updateField: UpdateFormField = (key, value) => {
    onUpdateField(key, value);
    setForm((current) => ({ ...current, [key]: value }));
  };

  return (
    <Tabs.Root value="settings">
      <SettingsView
        actions={actions}
        form={form}
        startup={{ enabled: false, loading: false }}
        updateField={updateField}
      />
    </Tabs.Root>
  );
}

// ブラウザテストでは CSS を読み込まないので、Base UI がダイアログの背後に敷く
// 固定配置の要素が、配置されていないダイアログの上に重なる。ダイアログ内は DOM から直接押す
function clickInDialog(locator: Locator) {
  (locator.element() as HTMLElement).click();
}

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
    <SettingsHarness actions={launcherActions} onUpdateField={updateField} />,
  );

  return { launcherActions, screen, updateField };
}

describe('設定ビュー', () => {
  test('設定セクションを指定された順番で表示する', async () => {
    await renderSettingsView();

    const headings = Array.from(document.querySelectorAll('h2')).map(
      (heading) => heading.textContent?.trim(),
    );

    expect(headings).toEqual([
      'プロフィール',
      'melonDS と ROM',
      '常駐と通知',
      '接続',
      '診断',
    ]);
  });

  test('プレイヤーネームは入力を確定したときに保存する', async () => {
    const { launcherActions, screen, updateField } = await renderSettingsView();
    const input = screen.getByLabelText('プレイヤーネーム');

    await input.fill('Alice');
    expect(launcherActions.savePlayerName).not.toHaveBeenCalled();
    await userEvent.keyboard('{Enter}');

    expect(updateField).toHaveBeenCalledWith('hostName', 'Alice');
    expect(launcherActions.savePlayerName).toHaveBeenCalledTimes(1);
    await expect.element(screen.getByText('5 / 32')).toBeVisible();
  });

  test('プレイヤーネームを変えずに離れたときは保存しない', async () => {
    const { launcherActions, screen } = await renderSettingsView();

    await screen.getByLabelText('プレイヤーネーム').click();
    await screen.getByRole('button', { name: 'melonDS を開く' }).click();

    expect(launcherActions.savePlayerName).not.toHaveBeenCalled();
  });

  test('localではシグナリングサーバーとUDPポートを更新する', async () => {
    const { screen, updateField } = await renderSettingsView();

    await screen
      .getByLabelText('シグナリングサーバー')
      .fill('wss://match.test/session');
    await screen.getByLabelText('UDPポート').fill('9000');
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
    await expect.element(screen.getByLabelText('UDPポート')).toBeVisible();
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
    expect(document.body.textContent).not.toContain('古い詳細ログ');
    expect(document.body.textContent).not.toContain('Insiders');
  });

  test('Public distributionでは詳細診断設定を表示しない', async () => {
    await renderSettingsView(false, 'public');

    expect(document.body.textContent).not.toContain('診断');
    expect(document.body.textContent).not.toContain(
      '入力・通信・画面状態のログを増やします',
    );
    expect(document.body.textContent).not.toContain('AI用プレイログ');
  });

  test('ロムとmelonDS関連処理を実行する', async () => {
    const { launcherActions, screen } = await renderSettingsView();

    await expect.element(screen.getByText('C:\\roms\\base.nds')).toBeVisible();
    await screen.getByRole('button', { name: '変更' }).click();
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

  test('スタートアップ起動を行のクリックで切り替える', async () => {
    const { launcherActions, screen } = await renderSettingsView();

    await expect
      .element(screen.getByRole('switch', { name: 'Windows ログイン時に起動' }))
      .not.toBeChecked();
    await screen.getByText('Windows ログイン時に起動').click();

    expect(launcherActions.setStartupEnabled).toHaveBeenCalledWith(true);
  });

  test('新しい部屋の通知をSwitchで切り替える', async () => {
    const { screen, updateField } = await renderSettingsView();

    await screen.getByText('新しい部屋の通知').click();

    expect(updateField).toHaveBeenCalledWith(
      'newRoomNotificationsEnabled',
      false,
    );
    await expect
      .element(screen.getByRole('switch', { name: '新しい部屋の通知' }))
      .not.toBeChecked();
  });

  test('診断イベントログをSwitchで切り替える', async () => {
    const { screen, updateField } = await renderSettingsView();

    await screen.getByText('診断イベントログ').click();

    expect(updateField).toHaveBeenCalledWith('diagnosticEventsEnabled', true);
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

    await screen.getByRole('button', { name: '削除…' }).click();
    await expect
      .element(screen.getByRole('alertdialog'))
      .toHaveTextContent('古い詳細ログを削除しますか？');
    clickInDialog(screen.getByRole('button', { name: '削除する' }));

    await vi.waitFor(() =>
      expect(launcherActions.cleanupDetailedLogs).toHaveBeenCalledTimes(1),
    );
    await expect
      .element(screen.getByRole('alertdialog'))
      .not.toBeInTheDocument();
  });

  test('古い詳細ログの削除はキャンセルできる', async () => {
    const { launcherActions, screen } = await renderSettingsView();

    await screen.getByRole('button', { name: '削除…' }).click();
    await expect.element(screen.getByRole('alertdialog')).toBeVisible();
    clickInDialog(screen.getByRole('button', { name: 'キャンセル' }));

    expect(launcherActions.cleanupDetailedLogs).not.toHaveBeenCalled();
    await expect
      .element(screen.getByRole('alertdialog'))
      .not.toBeInTheDocument();
  });

  test('現在の構成パネルを表示しない', async () => {
    const { screen } = await renderSettingsView();

    await expect
      .element(screen.getByText('現在の構成', { exact: true }))
      .not.toBeInTheDocument();
  });
});
