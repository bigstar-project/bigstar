import { type ComponentProps, useState } from 'react';
import { afterEach, describe, expect, test, vi } from 'vitest';
import { render } from 'vitest-browser-react';
import { LauncherShell } from './LauncherShell';
import type { View } from './types';

type ShellProps = Partial<ComponentProps<typeof LauncherShell>>;

afterEach(() => {
  vi.unstubAllGlobals();
});

function ShortcutTestShell({
  initialView = 'battle',
  ...props
}: ShellProps & { initialView?: View }) {
  const [view, setView] = useState<View>(initialView);
  return (
    <>
      <output data-testid="active-view">{view}</output>
      <LauncherShell
        activeView={view}
        activityStatus={null}
        onCheckForUpdate={vi.fn()}
        onViewChange={setView}
        romStatus={null}
        updateBusy={false}
        updateStatus={{ phase: 'idle' }}
        {...props}
      >
        <div />
      </LauncherShell>
    </>
  );
}

function pressShortcut(key: string, shiftKey = false) {
  const init: KeyboardEventInit = {
    bubbles: true,
    code: key === 'Tab' ? 'Tab' : `Digit${key}`,
    ctrlKey: true,
    key,
    shiftKey,
  };
  document.dispatchEvent(new KeyboardEvent('keydown', init));
  document.dispatchEvent(new KeyboardEvent('keyup', init));
}

describe('ランチャーのタブショートカット', () => {
  test('Ctrl+1/2/3/4で対応するタブへ移動する', async () => {
    const screen = await render(<ShortcutTestShell />);
    for (const [key, view, label] of [
      ['2', 'cpu', 'CPU対戦'],
      ['3', 'history', '対戦履歴'],
      ['4', 'settings', '設定'],
      ['1', 'battle', '対戦'],
    ]) {
      pressShortcut(key);
      await expect
        .element(screen.getByTestId('active-view'))
        .toHaveTextContent(view);
      await expect
        .element(screen.getByRole('tab', { name: label, exact: true }))
        .toHaveTextContent(`Ctrl+${key}`);
    }
  });

  test('Ctrl+TabでCPU対戦を含めて順送りしCtrl+Shift+Tabで逆送りする', async () => {
    const screen = await render(<ShortcutTestShell />);
    for (const view of ['cpu', 'history', 'settings', 'battle']) {
      pressShortcut('Tab');
      await expect
        .element(screen.getByTestId('active-view'))
        .toHaveTextContent(view);
    }
    for (const view of ['settings', 'history', 'cpu', 'battle']) {
      pressShortcut('Tab', true);
      await expect
        .element(screen.getByTestId('active-view'))
        .toHaveTextContent(view);
    }
  });
});

describe('ランチャーのエディション表示', () => {
  test('Insiders版ではブランド名の横にバッジを表示する', async () => {
    const screen = await render(<ShortcutTestShell />);

    await expect
      .element(screen.getByTestId('edition-badge'))
      .toHaveTextContent('Insiders');
    await expect
      .element(screen.getByTestId('brand'))
      .toHaveTextContent('BIGSTARInsiders');
    await expect.element(screen.getByText('ONLINE')).not.toBeInTheDocument();
  });

  test('Public版ではエディションバッジを表示しない', async () => {
    vi.stubGlobal('__BIGSTAR_EDITION_CONFIG__', {
      badge: 'Public',
      displayName: 'Bigstar',
      edition: 'public',
    });

    const screen = await render(<ShortcutTestShell />);

    await expect
      .element(screen.getByTestId('edition-badge'))
      .not.toBeInTheDocument();
  });
});

describe('ランチャーのサイドバー', () => {
  test('募集中の部屋があれば対戦タブに部屋の数を出す', async () => {
    const screen = await render(<ShortcutTestShell roomCount={3} />);

    await expect
      .element(screen.getByRole('tab', { name: '対戦', exact: true }))
      .toHaveTextContent('対戦3');
  });

  test('部屋を公開中のカードから対戦画面へ戻れる', async () => {
    const screen = await render(
      <ShortcutTestShell
        initialView="settings"
        session={{ kind: 'hosting', sinceMs: Date.now() - 65_000 }}
      />,
    );

    const card = screen.getByRole('button', { name: /部屋を公開中/ });
    await expect.element(card).toHaveTextContent('相手待ち · 1:05');
    await card.click();
    await expect
      .element(screen.getByTestId('active-view'))
      .toHaveTextContent('battle');
  });

  test('更新が必要なときはサイドバーから更新できる', async () => {
    const onCheckForUpdate = vi.fn();
    const screen = await render(
      <ShortcutTestShell
        onCheckForUpdate={onCheckForUpdate}
        updateStatus={{ phase: 'available', version: '0.13.0' }}
      />,
    );

    await expect
      .element(
        screen.getByText('v0.13.0 に更新するまで部屋の作成・参加はできません'),
      )
      .toBeVisible();
    await screen.getByRole('button', { name: '更新して再起動' }).click();
    expect(onCheckForUpdate).toHaveBeenCalledTimes(1);
  });

  test('最新のときはバージョンの横に最新と出し、手動で確認できる', async () => {
    const onCheckForUpdate = vi.fn();
    const screen = await render(
      <ShortcutTestShell
        onCheckForUpdate={onCheckForUpdate}
        playerName="Host Player"
        updateStatus={{ phase: 'none' }}
      />,
    );

    await expect.element(screen.getByText('Host Player')).toBeVisible();
    await expect.element(screen.getByText('最新')).toBeVisible();
    await screen.getByRole('button', { name: '更新を確認' }).click();
    expect(onCheckForUpdate).toHaveBeenCalledTimes(1);
  });

  test('操作の結果は右下の通知に出す', async () => {
    const screen = await render(
      <ShortcutTestShell
        activityStatus={{ kind: 'ok', text: '部屋を作成しました' }}
      />,
    );

    await expect
      .element(
        screen
          .getByRole('region', { name: '通知' })
          .getByText('部屋を作成しました'),
      )
      .toBeVisible();
  });
});
