import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { NuqsAdapter } from 'nuqs/adapters/react';
import { type ReactNode, useState } from 'react';
import { beforeEach, describe, expect, test, vi } from 'vitest';
import { render } from 'vitest-browser-react';
import { Tabs } from '../components/ui';
import { previewMatchHistory } from '../previewData';
import { HistoryView } from './HistoryView';

beforeEach(() => {
  window.history.replaceState(null, '', '?view=history');
});

function HistoryTestProviders({ children }: { children: ReactNode }) {
  const [queryClient] = useState(
    () =>
      new QueryClient({
        defaultOptions: { queries: { retry: false } },
      }),
  );
  return (
    <QueryClientProvider client={queryClient}>
      <NuqsAdapter>
        <Tabs.Root value="history">{children}</Tabs.Root>
      </NuqsAdapter>
    </QueryClientProvider>
  );
}

describe('履歴ビュー', () => {
  test('URLからすべてのフィルターを復元する', async () => {
    window.history.replaceState(
      null,
      '',
      '?view=history&period=recent30&opponent=preview-profile-rival&name=Rival&stage=2&outcome=win',
    );
    const screen = await render(
      <HistoryTestProviders>
        <HistoryView matches={previewMatchHistory()} />
      </HistoryTestProviders>,
    );

    await expect
      .element(screen.getByRole('combobox', { name: '期間' }))
      .toHaveTextContent('直近30戦');
    await expect
      .element(screen.getByRole('combobox', { name: '対戦相手' }))
      .toHaveTextContent('Rival');
    await expect
      .element(screen.getByRole('combobox', { name: 'ステージ' }))
      .toHaveTextContent('雪');
    await expect
      .element(screen.getByRole('combobox', { name: '履歴の結果' }))
      .toHaveTextContent('勝利');
  });

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
    const scrollTo = vi
      .spyOn(window, 'scrollTo')
      .mockImplementation(() => undefined);
    const screen = await render(
      <HistoryTestProviders>
        <HistoryView matches={previewMatchHistory()} />
      </HistoryTestProviders>,
    );

    await screen.getByText('3 - 1').click();
    await screen.getByRole('button', { name: 'Rivalとの戦績を見る' }).click();

    await vi.waitFor(() => {
      const search = new URLSearchParams(window.location.search);
      expect(search.get('opponent')).toBe('preview-profile-rival');
      expect(search.get('name')).toBe('Rival');
    });

    await expect
      .element(screen.getByRole('heading', { name: 'Rivalとの戦績' }))
      .toBeVisible();
    await expect.element(screen.getByText('1勝 0敗')).toBeVisible();
    expect(scrollTo).toHaveBeenCalledWith({ top: 0, behavior: 'smooth' });
    expect(
      document.querySelector(
        '[data-scope="collapsible"][data-part="root"][data-state="open"]',
      ),
    ).toBeNull();
    await screen.getByRole('button', { name: '全対戦履歴に戻る' }).click();
    await expect
      .element(screen.getByRole('heading', { name: 'Rivalとの戦績' }))
      .not.toBeInTheDocument();
    scrollTo.mockRestore();
  });

  test('対戦相手フィルターをルート履歴に記録して復元する', async () => {
    const screen = await render(
      <HistoryTestProviders>
        <HistoryView matches={previewMatchHistory()} />
      </HistoryTestProviders>,
    );

    await screen.getByRole('combobox', { name: '対戦相手' }).click();
    const rivalOption = [
      ...document.querySelectorAll<HTMLElement>('[role=option]'),
    ].find((option) => option.textContent?.includes('Rival'));
    expect(rivalOption).toBeDefined();
    rivalOption?.click();
    await vi.waitFor(() =>
      expect(new URLSearchParams(window.location.search).get('opponent')).toBe(
        'preview-profile-rival',
      ),
    );

    window.history.replaceState(null, '', '?view=history');
    window.dispatchEvent(new PopStateEvent('popstate'));
    await expect
      .element(screen.getByRole('combobox', { name: '対戦相手' }))
      .toHaveTextContent('すべて');
  });

  test('対戦詳細の表は自分と相手の名前をヘッダーに表示する', async () => {
    const screen = await render(
      <HistoryTestProviders>
        <HistoryView matches={previewMatchHistory()} />
      </HistoryTestProviders>,
    );

    await screen.getByText('3 - 1').click();

    const headers = [...document.querySelectorAll('th')].map((header) =>
      header.textContent?.trim(),
    );
    expect(headers.slice(3, 5)).toEqual(['Preview Player', 'Rival']);
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

  test('対戦の詳細DOMを初回展開まで生成しない', async () => {
    const screen = await render(
      <HistoryTestProviders>
        <HistoryView matches={previewMatchHistory()} />
      </HistoryTestProviders>,
    );

    const details = () =>
      document.querySelectorAll(
        '[data-scope="collapsible"][data-part="content"]',
      );

    expect(details()).toHaveLength(0);
    expect(document.querySelectorAll('th')).toHaveLength(0);

    const triggerContent = screen.getByText('3 - 1');
    await triggerContent.click();
    await vi.waitFor(() =>
      expect(document.querySelectorAll('th').length).toBeGreaterThan(0),
    );
    expect(details()).toHaveLength(1);

    const detailsElement = details().item(0) as HTMLElement;
    const detailsBody = detailsElement.querySelector(
      '[data-match-details-body]',
    ) as HTMLElement;
    expect(detailsElement.className).not.toContain('py_3');
    expect(detailsElement.className).not.toContain('bd-t-w_1px');
    expect(detailsBody.className).toContain('py_3');
    expect(detailsBody.className).toContain('bd-t-w_1px');

    await triggerContent.click();
    await vi.waitFor(() =>
      expect(details().item(0)?.getAttribute('data-state')).toBe('closed'),
    );
    expect(details()).toHaveLength(1);
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

  test('一覧の末尾までスクロールすると次の履歴を自動で読み込む', async () => {
    const [baseMatch] = previewMatchHistory();
    const matches = Array.from({ length: 51 }, (_, index) => ({
      ...baseMatch,
      id: `infinite-scroll-${String(index).padStart(2, '0')}`,
      playerNames: {
        ...baseMatch.playerNames,
        luigi: index === 50 ? '51件目の対戦相手' : `対戦相手 ${index + 1}`,
      },
      startedAt: new Date(
        Date.UTC(2026, 5, 30) - index * 86_400_000,
      ).toISOString(),
    }));
    await render(
      <HistoryTestProviders>
        <HistoryView matches={matches} />
      </HistoryTestProviders>,
    );

    const historyTriggers = () =>
      document.querySelectorAll(
        '[data-scope="collapsible"][data-part="trigger"]',
      );
    await vi.waitFor(() => expect(historyTriggers()).toHaveLength(50));

    const loadMoreTarget = document.querySelector<HTMLElement>(
      '[data-history-load-more]',
    );
    expect(loadMoreTarget).not.toBeNull();
    loadMoreTarget?.scrollIntoView();

    await vi.waitFor(() => expect(historyTriggers()).toHaveLength(51));
    expect(historyTriggers().item(50)).toHaveTextContent('51件目の対戦相手');
    expect(document.querySelector('[data-history-load-more]')).toBeNull();
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
