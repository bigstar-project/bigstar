import { useState } from 'react';
import { afterEach, describe, expect, test, vi } from 'vitest';
import { render } from 'vitest-browser-react';
import { LauncherShell } from './LauncherShell';
import type { View } from './types';

afterEach(() => {
  vi.unstubAllGlobals();
});

function ShortcutTestShell() {
  const [view, setView] = useState<View>('battle');
  return (
    <>
      <output data-testid="active-view">{view}</output>
      <LauncherShell
        activeView={view}
        activityStatus={null}
        connectionStatus={{ kind: 'idle', text: '未接続' }}
        onCheckForUpdate={vi.fn()}
        onViewChange={setView}
        romStatus={null}
        updateBusy={false}
        updateStatus={{ phase: 'idle' }}
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
      .element(screen.getByTestId('brand-star-row'))
      .toHaveTextContent('STARInsiders');
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
