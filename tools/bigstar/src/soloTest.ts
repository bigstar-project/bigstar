import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';
import { isTauri } from '@tauri-apps/api/core';
import {
  commands,
  type SoloTestRequest,
  type SoloTestStatus,
} from './bindings';

const key = ['solo-test-status'] as const;
const idle: SoloTestStatus = {
  active: false,
  preparing: false,
  log_dir: null,
  host_pid: null,
  client_pid: null,
  error: null,
  config: null,
};

async function unwrap<T>(
  promise: Promise<
    { status: 'ok'; data: T } | { status: 'error'; error: string }
  >,
): Promise<T> {
  const result = await promise;
  if (result.status === 'error') throw new Error(result.error);
  return result.data;
}

export function useSoloTest(enabled: boolean) {
  const client = useQueryClient();
  const query = useQuery({
    queryKey: key,
    queryFn: () =>
      isTauri() ? unwrap(commands.getSoloTestStatus()) : Promise.resolve(idle),
    enabled: enabled === true,
    refetchInterval: 1000,
    retry: false,
  });
  const start = useMutation({
    mutationFn: (request: SoloTestRequest) =>
      unwrap(commands.startSoloTest(request)),
    onSuccess: (data) => client.setQueryData(key, data),
  });
  const stop = useMutation({
    mutationFn: () => unwrap(commands.stopSoloTest()),
    onSuccess: (data) => client.setQueryData(key, data),
  });
  return {
    status: query.data ?? idle,
    busy: start.isPending || stop.isPending,
    start,
    stop,
    error: start.error ?? stop.error ?? query.error,
    available: enabled && isTauri() && query.isSuccess,
  };
}

export type SoloTestController = ReturnType<typeof useSoloTest>;

export const soloPresets = {
  wan: {
    label: '疑似WAN（標準）',
    host: { delay_frames: 2, jitter_frames: 1, drop_every: 0 },
    client: { delay_frames: 2, jitter_frames: 1, drop_every: 0 },
  },
  local: {
    label: '遅延なし（比較用）',
    host: { delay_frames: 0, jitter_frames: 0, drop_every: 0 },
    client: { delay_frames: 0, jitter_frames: 0, drop_every: 0 },
  },
  jitter: {
    label: '揺らぎが大きい回線',
    host: { delay_frames: 4, jitter_frames: 3, drop_every: 0 },
    client: { delay_frames: 4, jitter_frames: 3, drop_every: 0 },
  },
  asymmetric: {
    label: '非対称の回線',
    host: { delay_frames: 5, jitter_frames: 2, drop_every: 0 },
    client: { delay_frames: 1, jitter_frames: 0, drop_every: 0 },
  },
  loss: {
    label: 'パケット間引きあり',
    host: { delay_frames: 2, jitter_frames: 1, drop_every: 17 },
    client: { delay_frames: 2, jitter_frames: 1, drop_every: 17 },
  },
} satisfies Record<
  string,
  {
    label: string;
    host: SoloTestRequest['host'];
    client: SoloTestRequest['client'];
  }
>;
