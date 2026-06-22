import { describe, expect, test } from 'vitest';
import { render } from 'vitest-browser-react';
import { Tabs } from '../components/ui';
import { previewMatchHistory } from '../previewData';
import { HistoryView } from './HistoryView';

describe('履歴ビュー', () => {
  test('最新の対戦をデフォルトでは展開しない', async () => {
    const screen = await render(
      <Tabs.Root value="history">
        <HistoryView matches={previewMatchHistory()} />
      </Tabs.Root>,
    );

    await expect.element(screen.getByText('3 - 1')).toBeVisible();
    expect(
      document.querySelector(
        '[data-scope="collapsible"][data-part="root"][data-state="open"]',
      ),
    ).toBeNull();
  });

  test('未プレイだけの対戦は履歴に表示しない', async () => {
    const [playedMatch] = previewMatchHistory();
    const unplayedMatch = {
      ...playedMatch,
      id: 'unplayed-match',
      playerNames: {
        mario: 'Unplayed Mario',
        luigi: 'Unplayed Luigi',
      },
      stages: [],
      status: 'stopped' as const,
    };

    const screen = await render(
      <Tabs.Root value="history">
        <HistoryView matches={[unplayedMatch, playedMatch]} />
      </Tabs.Root>,
    );

    await expect.element(screen.getByText('3 - 1')).toBeVisible();
    await expect
      .element(screen.getByText('Unplayed Mario'))
      .not.toBeInTheDocument();
    await expect.element(screen.getByText('0 - 0')).not.toBeInTheDocument();
  });
});
