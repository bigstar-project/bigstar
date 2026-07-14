import { describe, expect, test } from 'vitest';
import {
  previewHistoryDashboard,
  previewHistoryOpponents,
  queryPreviewMatchHistory,
} from './matchHistory';
import { previewMatchHistory } from './previewData';
import type { MatchHistoryFilter } from './types';

const allFilter: MatchHistoryFilter = {
  recentMatches: null,
  sinceStartedAt: null,
  opponentPlayerId: null,
  stage: null,
  outcome: null,
};

describe('対戦履歴集計', () => {
  test('対戦単位とゲーム単位を分けて集計する', () => {
    const dashboard = previewHistoryDashboard(previewMatchHistory(), allFilter);

    expect(dashboard.summary).toMatchObject({
      wins: 2,
      losses: 1,
      stopped: 1,
      gameWins: 6,
      gameLosses: 4,
    });
    expect(dashboard.trend).toHaveLength(3);
    expect(dashboard.stages).toEqual(
      expect.arrayContaining([
        expect.objectContaining({ stage: 0 }),
        expect.objectContaining({ stage: 4 }),
      ]),
    );
  });

  test('相手・ステージ・結果を組み合わせて履歴を絞り込む', () => {
    const page = queryPreviewMatchHistory(previewMatchHistory(), {
      filter: {
        ...allFilter,
        opponentPlayerId: 'preview-profile-rival',
        stage: 0,
        outcome: 'win',
      },
      cursor: null,
      limit: 50,
    });

    expect(page.total).toBe(1);
    expect(page.matches[0]?.id).toBe('preview-history-1');
  });

  test('完了した対戦では中断した対戦を除外する', () => {
    const page = queryPreviewMatchHistory(previewMatchHistory(), {
      filter: { ...allFilter, outcome: 'completed' },
      cursor: null,
      limit: 50,
    });

    expect(page.total).toBe(3);
    expect(page.matches.every((match) => match.status === 'completed')).toBe(
      true,
    );
  });

  test('決着したステージがない対戦は履歴と対戦相手候補から除外する', () => {
    const [played] = previewMatchHistory();
    const unplayed = {
      ...played,
      id: 'unplayed-match',
      playerIds: { ...played.playerIds, luigi: 'ghost-rival' },
      playerNames: { ...played.playerNames, luigi: 'Ghost Rival' },
      stages: [],
      status: 'stopped' as const,
    };

    const page = queryPreviewMatchHistory([unplayed, played], {
      filter: allFilter,
      cursor: null,
      limit: 50,
    });
    const opponents = previewHistoryOpponents([unplayed, played]);

    expect(page.total).toBe(1);
    expect(page.matches[0]?.id).toBe(played.id);
    expect(opponents).toHaveLength(1);
    expect(opponents[0]?.playerId).not.toBe('ghost-rival');
  });

  test('名前ではなくプレイヤーIDで対戦相手をまとめる', () => {
    const [first] = previewMatchHistory();
    const renamed = {
      ...first,
      id: 'renamed-rival',
      playerNames: { ...first.playerNames, luigi: 'New Rival Name' },
      startedAt: '2026-06-22T10:40:00.000Z',
    };

    const opponents = previewHistoryOpponents([first, renamed]);

    expect(opponents).toEqual([
      expect.objectContaining({
        playerId: 'preview-profile-rival',
        latestName: 'New Rival Name',
        matches: 2,
      }),
    ]);
  });

  test('対戦相手を対戦回数が多い順に並べる', () => {
    const [first] = previewMatchHistory();
    const frequentOpponent = {
      ...first,
      id: 'frequent-opponent-1',
      playerIds: { ...first.playerIds, luigi: 'frequent-rival' },
      playerNames: { ...first.playerNames, luigi: 'Frequent Rival' },
      startedAt: '2026-06-20T10:40:00.000Z',
    };
    const frequentOpponentAgain = {
      ...frequentOpponent,
      id: 'frequent-opponent-2',
      startedAt: '2026-06-21T10:40:00.000Z',
    };

    const opponents = previewHistoryOpponents([
      first,
      frequentOpponent,
      frequentOpponentAgain,
    ]);

    expect(
      opponents.map(({ playerId, matches }) => ({ playerId, matches })),
    ).toEqual([
      { playerId: 'frequent-rival', matches: 2 },
      { playerId: 'preview-profile-rival', matches: 1 },
    ]);
  });

  test('1000件を超えても切り捨てず、一覧だけをページ分取得する', () => {
    const [template] = previewMatchHistory();
    const matches = Array.from({ length: 1001 }, (_, index) => ({
      ...template,
      id: `match-${String(index).padStart(4, '0')}`,
      startedAt: new Date(Date.UTC(2026, 0, 1, 0, index)).toISOString(),
    }));

    const page = queryPreviewMatchHistory(matches, {
      filter: allFilter,
      cursor: null,
      limit: 50,
    });

    expect(page.total).toBe(1001);
    expect(page.matches).toHaveLength(50);
    expect(page.nextCursor).not.toBeNull();
  });

  test('勝率推移の先頭にも表示範囲より前の9戦を含める', () => {
    const [template] = previewMatchHistory();
    const matches = Array.from({ length: 70 }, (_, index) => ({
      ...template,
      id: `trend-${String(index).padStart(2, '0')}`,
      startedAt: new Date(Date.UTC(2026, 0, 1, 0, index)).toISOString(),
      stages:
        index === 10
          ? template.stages.map((stage, gameIndex) => ({
              ...stage,
              winner: 1 as const,
              mario_match_wins: 0,
              luigi_match_wins: gameIndex + 1,
            }))
          : template.stages,
    }));

    const dashboard = previewHistoryDashboard(matches, allFilter);

    expect(dashboard.trend).toHaveLength(60);
    expect(dashboard.trend[0]).toMatchObject({
      matchId: 'trend-10',
      rollingWinRate: 0.9,
    });
  });
});
