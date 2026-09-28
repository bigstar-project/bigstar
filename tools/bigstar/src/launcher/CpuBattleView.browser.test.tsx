import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { afterEach, expect, test, vi } from 'vitest';
import { render } from 'vitest-browser-react';
import { commands, type SoloTestStatus } from '../bindings';
import { Tabs } from '../components/ui';
import { useSoloTest } from '../soloTest';
import { CpuBattleView } from './CpuBattleView';

vi.mock('@tauri-apps/api/core', () => ({ isTauri: () => true }));
vi.mock('../bindings', () => ({
  commands: {
    getSoloTestStatus: vi.fn(),
    startSoloTest: vi.fn(),
    startCpuMatch: vi.fn(),
    stopSoloTest: vi.fn(),
  },
}));

const idle: SoloTestStatus = {
  active: false,
  preparing: false,
  log_dir: null,
  host_pid: null,
  client_pid: null,
  error: null,
  config: null,
};
afterEach(() => vi.clearAllMocks());

function TestView({ blocked = false }: { blocked?: boolean }) {
  const controller = useSoloTest(true);
  return (
    <Tabs.Root value="cpu">
      <CpuBattleView controller={controller} blocked={blocked} />
    </Tabs.Root>
  );
}

test.each([
  ['クリボー', 'beginner'],
  ['ノコノコ', 'combat_v2'],
  ['カロン', 'development'],
] as const)('%sを選択して対戦を開始・停止する', async (label, profile) => {
  vi.mocked(commands.getSoloTestStatus).mockResolvedValue({
    status: 'ok',
    data: idle,
  });
  vi.mocked(commands.startCpuMatch).mockImplementation(async (opponent) => ({
    status: 'ok',
    data: {
      ...idle,
      active: true,
      config: {
        cpu_opponent: opponent,
        stage: 0,
        controlled_player: 'mario',
        rollback_enabled: false,
        input_delay_frames: 2,
        match_seed: '45',
        host: { delay_frames: 0, jitter_frames: 0, drop_every: 0 },
        client: { delay_frames: 0, jitter_frames: 0, drop_every: 0 },
      },
    },
  }));
  vi.mocked(commands.stopSoloTest).mockResolvedValue({
    status: 'ok',
    data: idle,
  });
  const client = new QueryClient({
    defaultOptions: { queries: { retry: false, refetchInterval: false } },
  });
  const screen = await render(
    <QueryClientProvider client={client}>
      <TestView />
    </QueryClientProvider>,
  );
  await screen.getByRole('combobox', { name: '対戦相手' }).click();
  await screen.getByRole('option', { name: label, exact: true }).click();
  const start = screen.getByRole('button', { name: '対戦を始める' });
  await expect.element(start).toBeEnabled();
  await start.click();
  expect(commands.startCpuMatch).toHaveBeenCalledWith(profile);
  await expect.element(start).toBeDisabled();
  await screen.getByRole('button', { name: '対戦を終了' }).click();
  expect(commands.stopSoloTest).toHaveBeenCalledOnce();
  await expect.element(start).toBeEnabled();
  client.clear();
});

test('通常対戦などがある間は開始しない', async () => {
  vi.mocked(commands.getSoloTestStatus).mockResolvedValue({
    status: 'ok',
    data: idle,
  });
  const client = new QueryClient();
  const screen = await render(
    <QueryClientProvider client={client}>
      <TestView blocked />
    </QueryClientProvider>,
  );
  await expect
    .element(screen.getByRole('button', { name: '対戦を始める' }))
    .toBeDisabled();
  expect(commands.startCpuMatch).not.toHaveBeenCalled();
  client.clear();
});
