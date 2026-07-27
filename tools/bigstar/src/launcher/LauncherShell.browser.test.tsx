import { useState } from 'react';
import { describe, expect, test, vi } from 'vitest';
import { render } from 'vitest-browser-react';
import { LauncherShell } from './LauncherShell';
import type { View } from './types';

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
  test('Ctrl+1/2/3で対応するタブへ移動する', async () => {
    const screen = await render(<ShortcutTestShell />);

    pressShortcut('2');
    await expect
      .element(screen.getByTestId('active-view'))
      .toHaveTextContent('history');

    pressShortcut('3');
    await expect
      .element(screen.getByTestId('active-view'))
      .toHaveTextContent('settings');

    pressShortcut('1');
    await expect
      .element(screen.getByTestId('active-view'))
      .toHaveTextContent('battle');
  });

  test('Ctrl+Tabで順送りしCtrl+Shift+Tabで逆送りする', async () => {
    const screen = await render(<ShortcutTestShell />);

    pressShortcut('Tab');
    await expect
      .element(screen.getByTestId('active-view'))
      .toHaveTextContent('history');

    pressShortcut('Tab');
    await expect
      .element(screen.getByTestId('active-view'))
      .toHaveTextContent('settings');

    pressShortcut('Tab', true);
    await expect
      .element(screen.getByTestId('active-view'))
      .toHaveTextContent('history');
  });
});
