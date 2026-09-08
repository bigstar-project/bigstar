import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { afterEach, expect, test, vi } from 'vitest';
import { render } from 'vitest-browser-react';
import { commands, type SoloTestStatus } from '../bindings';
import { Tabs } from '../components/ui';
import { useSoloTest } from '../soloTest';
import { LauncherShell } from './LauncherShell';
import { SoloTestView } from './SoloTestView';

vi.mock('@tauri-apps/api/core', () => ({ isTauri: () => true }));
vi.mock('../bindings', () => ({
  commands: {
    getSoloTestStatus: vi.fn(),
    startSoloTest: vi.fn(),
    stopSoloTest: vi.fn(),
  },
}));
vi.mock('../tauriClient', () => ({ openLogDir: vi.fn() }));

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
    <Tabs.Root value="solo-test">
      <SoloTestView controller={controller} blocked={blocked} />
    </Tabs.Root>
  );
}

test('疑似WANの初期値で開始し、両側を停止できる', async () => {
  vi.mocked(commands.getSoloTestStatus).mockResolvedValue({
    status: 'ok',
    data: idle,
  });
  vi.mocked(commands.startSoloTest).mockImplementation(async (request) => ({
    status: 'ok',
    data: {
      ...idle,
      active: true,
      host_pid: 123,
      client_pid: 456,
      config: request,
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
  const start = screen.getByRole('button', { name: '2つのゲームを開始' });
  await expect.element(start).toBeEnabled();
  await start.click();
  expect(commands.startSoloTest).toHaveBeenCalledWith(
    expect.objectContaining({
      stage: 3,
      controlled_player: 'mario',
      rollback_enabled: true,
      host: { delay_frames: 2, jitter_frames: 1, drop_every: 0 },
      client: { delay_frames: 2, jitter_frames: 1, drop_every: 0 },
    }),
  );
  await expect.element(start).toBeDisabled();
  await screen.getByRole('button', { name: '両方を停止' }).click();
  expect(commands.stopSoloTest).toHaveBeenCalledOnce();
  await expect.element(start).toBeEnabled();
  client.clear();
});

test('通常対戦や部屋の待機中は起動できない', async () => {
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
    .element(screen.getByRole('button', { name: '2つのゲームを開始' }))
    .toBeDisabled();
  client.clear();
});

test.each([
  false,
  true,
])('検証タブは専用能力が有効な時だけ表示する: %s', async (enabled) => {
  const screen = await render(
    <LauncherShell
      activeView="battle"
      activityStatus={null}
      aiDevToolsEnabled={false}
      soloTestEnabled={enabled}
      connectionStatus={{ kind: 'idle', text: '未接続' }}
      onCheckForUpdate={vi.fn()}
      onViewChange={vi.fn()}
      romStatus={null}
      updateBusy={false}
      updateStatus={{ phase: 'idle' }}
    >
      <div />
    </LauncherShell>,
  );
  if (enabled)
    await expect
      .element(screen.getByRole('tab', { name: 'ひとり検証' }))
      .toBeVisible();
  else
    await expect
      .element(screen.getByRole('tab', { name: 'ひとり検証' }))
      .not.toBeInTheDocument();
});
