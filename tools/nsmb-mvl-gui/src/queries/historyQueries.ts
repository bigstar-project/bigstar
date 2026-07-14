import { infiniteQueryOptions, queryOptions } from '@tanstack/react-query';
import {
  previewHistoryDashboard,
  previewHistoryOpponents,
  queryPreviewMatchHistory,
} from '../matchHistory';
import {
  loadMatchHistoryDashboard,
  loadMatchHistoryOpponents,
  queryMatchHistory,
} from '../tauriClient';
import type {
  MatchHistoryCursor,
  MatchHistoryFilter,
  MatchHistoryRecord,
} from '../types';
import { matchHistoryKeys } from './historyQueryKeys';

export { matchHistoryKeys } from './historyQueryKeys';

const localQueryDefaults = {
  networkMode: 'always' as const,
  retry: false,
  staleTime: Infinity,
};

function historySourceKey(matches?: MatchHistoryRecord[]) {
  return matches ? ['preview', matches] : 'sqlite';
}

export function matchHistoryOpponentsOptions(matches?: MatchHistoryRecord[]) {
  return queryOptions({
    queryKey: matchHistoryKeys.opponents(historySourceKey(matches)),
    queryFn: () =>
      matches ? previewHistoryOpponents(matches) : loadMatchHistoryOpponents(),
    ...localQueryDefaults,
  });
}

export function matchHistoryDashboardOptions(
  filter: MatchHistoryFilter,
  matches?: MatchHistoryRecord[],
) {
  return queryOptions({
    queryKey: matchHistoryKeys.dashboard(filter, historySourceKey(matches)),
    queryFn: () =>
      matches
        ? previewHistoryDashboard(matches, filter)
        : loadMatchHistoryDashboard(filter),
    ...localQueryDefaults,
  });
}

export function matchHistoryListOptions(
  filter: MatchHistoryFilter,
  pageSize: number,
  matches?: MatchHistoryRecord[],
) {
  return infiniteQueryOptions({
    queryKey: matchHistoryKeys.list(filter, historySourceKey(matches)),
    queryFn: ({ pageParam }) =>
      matches
        ? queryPreviewMatchHistory(matches, {
            filter,
            cursor: pageParam,
            limit: pageSize,
          })
        : queryMatchHistory({
            filter,
            cursor: pageParam,
            limit: pageSize,
          }),
    initialPageParam: null as MatchHistoryCursor | null,
    getNextPageParam: (page) => page.nextCursor ?? undefined,
    ...localQueryDefaults,
  });
}
