import type { MatchHistoryFilter } from '../types';

export const matchHistoryKeys = {
  all: ['matchHistory'] as const,
  dashboard: (filter: MatchHistoryFilter, sourceKey: unknown) =>
    [...matchHistoryKeys.all, 'dashboard', sourceKey, filter] as const,
  list: (filter: MatchHistoryFilter, sourceKey: unknown) =>
    [...matchHistoryKeys.all, 'list', sourceKey, filter] as const,
  opponents: (sourceKey: unknown) =>
    [...matchHistoryKeys.all, 'opponents', sourceKey] as const,
};
