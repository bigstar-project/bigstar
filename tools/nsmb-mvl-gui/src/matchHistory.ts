import type {
  MatchHistoryDashboard,
  MatchHistoryFilter,
  MatchHistoryOpponent,
  MatchHistoryPage,
  MatchHistoryPageRequest,
  MatchHistoryRecord,
} from './types';

export function localPlayerSide(match: MatchHistoryRecord) {
  return match.role === 'host' ? ('mario' as const) : ('luigi' as const);
}

export function opponentPlayerSide(match: MatchHistoryRecord) {
  return match.role === 'host' ? ('luigi' as const) : ('mario' as const);
}

export function opponentPlayerId(match: MatchHistoryRecord) {
  return match.playerIds[opponentPlayerSide(match)];
}

export function opponentPlayerName(match: MatchHistoryRecord) {
  return match.playerNames[opponentPlayerSide(match)];
}

function matchOutcome(match: MatchHistoryRecord) {
  if (match.status !== 'completed') {
    return match.status === 'stopped' ? ('stopped' as const) : null;
  }
  const local = localPlayerSide(match) === 'mario' ? 0 : 1;
  let localWins = 0;
  let opponentWins = 0;
  for (const stage of match.stages) {
    if (!stage.resolved || stage.winner === null) continue;
    if (stage.winner === local) localWins += 1;
    else opponentWins += 1;
  }
  if (localWins === opponentWins) return null;
  return localWins > opponentWins ? ('win' as const) : ('loss' as const);
}

export function hasPlayedResult(match: MatchHistoryRecord) {
  return match.stages.some(
    (stage) => stage.resolved && (stage.winner === 0 || stage.winner === 1),
  );
}

function scopedMatches(
  matches: MatchHistoryRecord[],
  filter: MatchHistoryFilter,
) {
  let scoped = [...matches]
    .filter(hasPlayedResult)
    .filter(
      (match) =>
        !filter.sinceStartedAt || match.startedAt >= filter.sinceStartedAt,
    )
    .filter(
      (match) =>
        !filter.opponentPlayerId ||
        opponentPlayerId(match) === filter.opponentPlayerId,
    )
    .sort(
      (left, right) =>
        right.startedAt.localeCompare(left.startedAt) ||
        right.id.localeCompare(left.id),
    );
  if (filter.recentMatches) {
    scoped = scoped.slice(0, filter.recentMatches);
  }
  return scoped;
}

export function queryPreviewMatchHistory(
  matches: MatchHistoryRecord[],
  request: MatchHistoryPageRequest,
): MatchHistoryPage {
  let filtered = scopedMatches(matches, request.filter)
    .filter((match) => {
      if (!request.filter.outcome) return true;
      if (request.filter.outcome === 'completed') {
        return match.status === 'completed';
      }
      return matchOutcome(match) === request.filter.outcome;
    })
    .filter(
      (match) =>
        request.filter.stage === null ||
        match.stages.some(
          (stage) => stage.resolved && stage.stage === request.filter.stage,
        ),
    );
  const total = filtered.length;
  if (request.cursor) {
    const cursor = request.cursor;
    filtered = filtered.filter(
      (match) =>
        match.startedAt < cursor.startedAt ||
        (match.startedAt === cursor.startedAt && match.id < cursor.id),
    );
  }
  const limit = Math.max(1, Math.min(request.limit, 100));
  const pageMatches = filtered.slice(0, limit);
  const last = pageMatches.at(-1);
  return {
    matches: pageMatches,
    nextCursor:
      filtered.length > limit && last
        ? { id: last.id, startedAt: last.startedAt }
        : null,
    total,
  };
}

export function previewHistoryDashboard(
  matches: MatchHistoryRecord[],
  filter: MatchHistoryFilter,
): MatchHistoryDashboard {
  const scoped = scopedMatches(matches, { ...filter, outcome: null });
  const completed = scoped.filter((match) => {
    const outcome = matchOutcome(match);
    return outcome === 'win' || outcome === 'loss';
  });
  const wins = completed.filter(
    (match) => matchOutcome(match) === 'win',
  ).length;
  const losses = completed.length - wins;
  const stopped = scoped.filter((match) => match.status === 'stopped').length;
  const localWins: boolean[] = [];
  const stageTotals = new Map<number, { wins: number; losses: number }>();
  for (const match of scoped) {
    const local = localPlayerSide(match) === 'mario' ? 0 : 1;
    for (const result of match.stages) {
      if (
        !result.resolved ||
        result.winner === null ||
        (filter.stage !== null && result.stage !== filter.stage)
      ) {
        continue;
      }
      const won = result.winner === local;
      localWins.push(won);
      if (result.stage !== null) {
        const totals = stageTotals.get(result.stage) ?? { wins: 0, losses: 0 };
        if (won) totals.wins += 1;
        else totals.losses += 1;
        stageTotals.set(result.stage, totals);
      }
    }
  }

  let streak = 0;
  let streakKind: 'win' | 'loss' | null = null;
  for (const match of completed) {
    const kind = matchOutcome(match);
    if (kind !== 'win' && kind !== 'loss') continue;
    if (!streakKind) streakKind = kind;
    if (kind !== streakKind) break;
    streak += 1;
  }

  const chronologicalWithLookback = [...completed].reverse().slice(-69);
  const visibleStart = Math.max(0, chronologicalWithLookback.length - 60);
  const trend = chronologicalWithLookback
    .slice(visibleStart)
    .map((match, visibleIndex) => {
      const index = visibleStart + visibleIndex;
      const window = chronologicalWithLookback.slice(
        Math.max(0, index - 9),
        index + 1,
      );
      const windowWins = window.filter(
        (candidate) => matchOutcome(candidate) === 'win',
      ).length;
      return {
        matchId: match.id,
        startedAt: match.startedAt,
        opponentName: opponentPlayerName(match),
        won: matchOutcome(match) === 'win',
        rollingWinRate: windowWins / window.length,
      };
    });
  return {
    summary: {
      wins,
      losses,
      stopped,
      gameWins: localWins.filter(Boolean).length,
      gameLosses: localWins.filter((won) => !won).length,
      streak,
      streakKind,
    },
    trend,
    stages: [...stageTotals.entries()]
      .sort(([left], [right]) => left - right)
      .map(([stage, totals]) => ({ stage, ...totals })),
  };
}

export function previewHistoryOpponents(
  matches: MatchHistoryRecord[],
): MatchHistoryOpponent[] {
  const opponents = new Map<string, MatchHistoryOpponent>();
  for (const match of [...matches].sort((left, right) =>
    right.startedAt.localeCompare(left.startedAt),
  )) {
    const playerId = opponentPlayerId(match);
    if (!playerId || !hasPlayedResult(match)) continue;
    const existing = opponents.get(playerId);
    if (existing) {
      existing.matches += 1;
      continue;
    }
    opponents.set(playerId, {
      playerId,
      latestName: opponentPlayerName(match),
      matches: 1,
      lastPlayedAt: match.startedAt,
    });
  }
  return [...opponents.values()].sort(
    (left, right) =>
      right.matches - left.matches ||
      right.lastPlayedAt.localeCompare(left.lastPlayedAt) ||
      left.playerId.localeCompare(right.playerId),
  );
}
