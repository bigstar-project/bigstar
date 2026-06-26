import { describe, expect, test, vi } from 'vitest';
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

  test('履歴を展開して確認したときだけ削除対象 ID を渡す', async () => {
    const [playedMatch] = previewMatchHistory();
    const onDeleteMatch = vi.fn();

    const screen = await render(
      <Tabs.Root value="history">
        <HistoryView matches={[playedMatch]} onDeleteMatch={onDeleteMatch} />
      </Tabs.Root>,
    );

    await expect
      .element(screen.getByRole('button', { name: '対戦履歴を削除' }))
      .not.toBeInTheDocument();

    await screen.getByText('3 - 1').click();

    await screen.getByRole('button', { name: '対戦履歴を削除' }).click();

    expect(onDeleteMatch).not.toHaveBeenCalled();
    await expect
      .element(screen.getByText('対戦履歴を削除しますか？'))
      .toBeVisible();

    await screen.getByRole('button', { name: '削除する' }).click();

    expect(onDeleteMatch).toHaveBeenCalledWith(playedMatch.id);
  });
});
