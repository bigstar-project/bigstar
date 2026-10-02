import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { afterEach, expect, test, vi } from 'vitest';
import { render } from 'vitest-browser-react';
import * as Tabs from '@/components/ui/tabs';
import { type CpuMatchRules, commands, type SoloTestStatus } from '../bindings';
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

const defaultRules: CpuMatchRules = {
  wins: 3,
  big_stars: 10,
  lives: 'endless',
};
test.each([
  ['クリボー', 'beginner', defaultRules],
  ['ノコノコ', 'combat_v2', defaultRules],
  ['カロン', 'development', defaultRules],
  ['カロン', 'development', { wins: 2, big_stars: 3, lives: '5' }],
] as const)('%sを選択して対戦を開始・停止する', async (label, profile, rules) => {
  vi.mocked(commands.getSoloTestStatus).mockResolvedValue({
    status: 'ok',
    data: idle,
  });
  vi.mocked(commands.startCpuMatch).mockImplementation(
    async (opponent, cpuRules) => ({
      status: 'ok',
      data: {
        ...idle,
        active: true,
        config: {
          cpu_opponent: opponent,
          cpu_rules: cpuRules,
          stage: 0,
          controlled_player: 'mario',
          rollback_enabled: false,
          input_delay_frames: 2,
          match_seed: '45',
          host: { delay_frames: 0, jitter_frames: 0, drop_every: 0 },
          client: { delay_frames: 0, jitter_frames: 0, drop_every: 0 },
        },
      },
    }),
  );
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
  // カード全体が選択の対象なので、名前を押して選ぶ
  await screen.getByText(label, { exact: true }).click();
  const radio = screen.getByRole('radio', { name: label });
  await expect.element(radio).toBeChecked();
  const choice = (field: string, value: string) =>
    screen
      .getByRole('group', { name: field })
      .getByRole('button', { name: value, exact: true });
  for (const [field, expected] of [
    ['先取数', '3'],
    ['ビッグスター', '10'],
    ['残機', '無限'],
  ] as const) {
    await expect
      .element(choice(field, expected))
      .toHaveAttribute('aria-pressed', 'true');
  }
  if (rules !== defaultRules) {
    for (const [field, value] of [
      ['先取数', String(rules.wins)],
      ['ビッグスター', String(rules.big_stars)],
      ['残機', rules.lives === 'endless' ? '無限' : rules.lives],
    ] as const) {
      await choice(field, value).click();
    }
  }
  const start = screen.getByRole('button', { name: '対戦を始める' });
  await expect.element(start).toBeEnabled();
  await start.click();
  expect(commands.startCpuMatch).toHaveBeenCalledWith(profile, rules);
  // 対戦中は開始ボタンを隠し、選択肢は変えられない
  await expect.element(start).not.toBeInTheDocument();
  await expect
    .element(screen.getByText(`${label}と対戦中`))
    .toBeInTheDocument();
  await expect.element(choice('先取数', '1')).toBeDisabled();
  await expect.element(radio).toBeDisabled();
  await screen.getByRole('button', { name: '対戦を終了' }).click();
  expect(commands.stopSoloTest).toHaveBeenCalledOnce();
  await expect
    .element(screen.getByRole('button', { name: '対戦を始める' }))
    .toBeEnabled();
  client.clear();
});

test('強さを 10 段階の数字で示す', async () => {
  vi.mocked(commands.getSoloTestStatus).mockResolvedValue({
    status: 'ok',
    data: idle,
  });
  const client = new QueryClient();
  const screen = await render(
    <QueryClientProvider client={client}>
      <TestView />
    </QueryClientProvider>,
  );
  await expect.element(screen.getByText('強さは10段階')).toBeInTheDocument();
  for (const [label, strength] of [
    ['クリボー', 1],
    ['ノコノコ', 2],
    ['カロン', 3],
  ] as const) {
    await expect
      .element(screen.getByRole('radio', { name: label }))
      .toHaveAccessibleName(`${label} 強さ ${strength}`);
  }
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
