import { queryOptions } from '@tanstack/react-query';
import { check } from '@tauri-apps/plugin-updater';
import { recordAppError } from '../appDiagnostics';
import {
  getDefaults,
  getSessionStatus,
  getStartupEnabled,
} from '../tauriClient';

const localQueryDefaults = {
  networkMode: 'always' as const,
  retry: false,
};

export const launcherQueryKeys = {
  defaults: ['launcher', 'defaults'] as const,
  session: ['launcher', 'session'] as const,
  startupEnabled: ['launcher', 'startupEnabled'] as const,
  update: ['launcher', 'update'] as const,
};

export function defaultsQueryOptions() {
  return queryOptions({
    queryKey: launcherQueryKeys.defaults,
    queryFn: getDefaults,
    staleTime: Infinity,
    ...localQueryDefaults,
  });
}

export function updateQueryOptions(enabled: boolean) {
  return queryOptions({
    enabled,
    queryKey: launcherQueryKeys.update,
    queryFn: async () => {
      try {
        return await check();
      } catch (error) {
        void recordAppError('updater', 'update.check', error);
        throw error;
      }
    },
    refetchInterval: (query) => (query.state.data ? false : 5 * 60 * 1000),
    staleTime: 5 * 60 * 1000,
    retry: false,
  });
}

export function startupEnabledQueryOptions() {
  return queryOptions({
    queryKey: launcherQueryKeys.startupEnabled,
    queryFn: getStartupEnabled,
    staleTime: Infinity,
    ...localQueryDefaults,
  });
}

export function sessionStatusQueryOptions(enabled: boolean) {
  return queryOptions({
    enabled,
    queryKey: launcherQueryKeys.session,
    queryFn: getSessionStatus,
    refetchInterval: 2000,
    staleTime: 1000,
    ...localQueryDefaults,
  });
}
