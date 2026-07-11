import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { type ReactNode, useState } from 'react';
import { describe, expect, test, vi } from 'vitest';
import { render } from 'vitest-browser-react';
import { Tabs } from '../components/ui';
import { previewMatchHistory } from '../previewData';
import { HistoryView } from './HistoryView';

function HistoryTestProviders({ children }: { children: ReactNode }) {
  const [queryClient] = useState(
    () =>
      new QueryClient({
        defaultOptions: { queries: { retry: false } },
      }),
  );
  return (
    <QueryClientProvider client={queryClient}>
      <Tabs.Root value="history">{children}</Tabs.Root>
    </QueryClientProvider>
  );
}

describe('履歴ビュー', () => {
  test('ダッシュボードとフィルターを表示する', async () => {
    const screen = await render(
      <HistoryTestProviders>
        <HistoryView matches={previewMatchHistory()} />
      </HistoryTestProviders>,
    );

    await expect.element(screen.getByText('対戦勝率')).toBeVisible();
    await expect.element(screen.getByText('ゲーム勝率')).toBeVisible();
    await expect
      .element(screen.getByRole('img', { name: '勝率推移グラフ' }))
      .toBeVisible();
    await expect
      .element(screen.getByRole('img', { name: '勝率推移グラフ' }))
      .toHaveAttribute('viewBox', '0 0 280 100');
    await expect.element(screen.getByText('ステージ別勝率')).toBeVisible();
    await expect
      .element(screen.getByRole('combobox', { name: '期間' }))
      .toBeVisible();
    await expect
      .element(screen.getByRole('combobox', { name: '期間' }))
      .toHaveTextContent('全期間');
    await expect
      .element(screen.getByRole('combobox', { name: '対戦相手' }))
      .toBeVisible();
    await expect
      .element(screen.getByRole('combobox', { name: 'ステージ' }))
      .toBeVisible();
    await expect
      .element(screen.getByRole('combobox', { name: '履歴の結果' }))
      .toBeVisible();
    await expect
      .element(screen.getByRole('combobox', { name: '履歴の結果' }))
      .toHaveTextContent('完了した対戦');
    await expect.element(screen.getByText('3件')).toBeVisible();

    await screen.getByRole('combobox', { name: '履歴の結果' }).click();
    await screen.getByRole('option', { name: 'すべて' }).click();
    await expect.element(screen.getByText('4件')).toBeVisible();
  });

  test('対戦相手の名前から個別戦績へ移動する', async () => {
    const screen = await render(
      <HistoryTestProviders>
        <HistoryView matches={previewMatchHistory()} />
      </HistoryTestProviders>,
    );

    await screen.getByText('3 - 1').click();
    await screen.getByRole('button', { name: 'Rivalとの戦績を見る' }).click();

    await expect
      .element(screen.getByRole('heading', { name: 'Rivalとの戦績' }))
      .toBeVisible();
    await expect.element(screen.getByText('1勝 0敗')).toBeVisible();
    await screen.getByRole('button', { name: '全対戦履歴に戻る' }).click();
    await expect
      .element(screen.getByRole('heading', { name: 'Rivalとの戦績' }))
      .not.toBeInTheDocument();
  });

  test('相手の最後の履歴を削除したら全履歴へ戻る', async () => {
    function DeletableHistory() {
      const [matches, setMatches] = useState(previewMatchHistory());
      return (
        <HistoryTestProviders>
          <HistoryView
            matches={matches}
            onDeleteMatch={async (matchId) => {
              setMatches((current) =>
                current.filter((match) => match.id !== matchId),
              );
            }}
          />
        </HistoryTestProviders>
      );
    }

    const screen = await render(<DeletableHistory />);
    await screen.getByText('3 - 1').click();
    await screen.getByRole('button', { name: 'Rivalとの戦績を見る' }).click();
    await screen.getByText('3 - 1').click();
    await screen.getByRole('button', { name: '対戦履歴を削除' }).click();
    await screen.getByRole('button', { name: '削除する' }).click();

    await expect
      .element(screen.getByRole('heading', { name: 'Rivalとの戦績' }))
      .not.toBeInTheDocument();
    await expect
      .element(screen.getByRole('combobox', { name: '対戦相手' }))
      .toHaveTextContent('すべて');
  });

  test('最新の対戦をデフォルトでは展開しない', async () => {
    const screen = await render(
      <HistoryTestProviders>
        <HistoryView matches={previewMatchHistory()} />
      </HistoryTestProviders>,
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
      <HistoryTestProviders>
        <HistoryView matches={[unplayedMatch, playedMatch]} />
      </HistoryTestProviders>,
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
      <HistoryTestProviders>
        <HistoryView matches={[playedMatch]} onDeleteMatch={onDeleteMatch} />
      </HistoryTestProviders>,
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

  test('履歴を展開するとログ操作ボタンからログフォルダを渡す', async () => {
    const [playedMatch] = previewMatchHistory();
    const onOpenLogDir = vi.fn();
    const onCreateLogArchive = vi.fn();
    const onUploadLogArchive = vi.fn();

    const screen = await render(
      <HistoryTestProviders>
        <HistoryView
          matches={[playedMatch]}
          onCreateLogArchive={onCreateLogArchive}
          onOpenLogDir={onOpenLogDir}
          onUploadLogArchive={onUploadLogArchive}
        />
      </HistoryTestProviders>,
    );

    await screen.getByText('3 - 1').click();
    await screen.getByRole('button', { name: 'ログを開く' }).click();
    await screen.getByRole('button', { name: 'zipを作成' }).click();
    await screen.getByRole('button', { name: 'ログを送信' }).click();

    expect(onOpenLogDir).toHaveBeenCalledWith(playedMatch.logDir);
    expect(onCreateLogArchive).toHaveBeenCalledWith(playedMatch.logDir);
    expect(onUploadLogArchive).toHaveBeenCalledWith(playedMatch.logDir);
  });
});
