import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import type { ReactNode } from 'react';
import { describe, expect, test, vi } from 'vitest';
import { render } from 'vitest-browser-react';
import type { LaunchRequest, LaunchResponse } from '../types';
import { useLauncherController } from './useLauncherController';

const mocks = vi.hoisted(() => {
  const romIdentity = {
    client_rom_sha256:
      '2222222222222222222222222222222222222222222222222222222222222222',
    generator_id:
      '3333333333333333333333333333333333333333333333333333333333333333',
    host_rom_sha256:
      '1111111111111111111111111111111111111111111111111111111111111111',
    rom_pair_id:
      '4444444444444444444444444444444444444444444444444444444444444444',
  };
  return {
    romIdentity,
    roomDetail: {
      can_join: false,
      created_at: 1,
      expires_at: Date.now() + 600_000,
      host_name: 'Host Player',
      peer_count: 2,
      room_id: 'host-room-1',
      rom_identity: romIdentity,
      settings: {
        big_stars: 10,
        course_mode: 'random',
        course_stages: [0, 1, 2, 3, 4],
        input_delay_frames: 4,
        input_max_frame_lead: 4,
        lives: '3',
        match_seed: '123',
        rng_seeds: ['123', '124', '125', '126', '127'],
        rollback_enabled: false,
        wins: 3,
      },
      signal_url: 'ws://127.0.0.1:8787/session?token=host-secret',
      status: 'matched',
      updated_at: 2,
    },
    startMatchMock: vi.fn((_request: LaunchRequest) =>
      Promise.resolve<LaunchResponse>({
        bridge_pid: 2001,
        log_dir: 'C:\\logs\\run1',
        melon_pid: 1001,
      }),
    ),
  };
});

vi.mock('@tauri-apps/plugin-process', () => ({
  relaunch: vi.fn(async () => {}),
}));

vi.mock('@tauri-apps/plugin-updater', () => ({
  check: vi.fn(async () => null),
}));

vi.mock('../matchmakingClient', () => ({
  closeRoom: vi.fn(async () => {}),
  createRoom: vi.fn(async () => ({
    room_id: 'host-room-1',
    signal_url: 'ws://127.0.0.1:8787/session?token=host-secret',
  })),
  getRoom: vi.fn(async () => mocks.roomDetail),
  joinRoom: vi.fn(async () => ({
    rom_identity: mocks.romIdentity,
    signal_url: 'ws://127.0.0.1:8787/session?token=join-secret',
  })),
  listRooms: vi.fn(async () => ({ rooms: [] })),
}));

vi.mock('../tauriClient', () => ({
  ensureRoms: vi.fn(async () => ({
    client_rom: 'C:\\roms\\client.nds',
    generated: false,
    host_rom: 'C:\\roms\\host.nds',
    rom_identity: mocks.romIdentity,
  })),
  generateRoms: vi.fn(async () => ({
    client_rom: 'C:\\roms\\client.nds',
    generated: true,
    host_rom: 'C:\\roms\\host.nds',
    rom_identity: mocks.romIdentity,
  })),
  getDefaults: vi.fn(async () => ({
    base_rom_path: 'C:\\roms\\base.nds',
    client_rom_path: 'C:\\roms\\client.nds',
    diagnostic_events_enabled: false,
    host_rom_path: 'C:\\roms\\host.nds',
    input_config_opened_once: true,
    player_name: 'Host Player',
    port: 8165,
    roms_prepared_once: true,
    room_code: 'test-room',
    signal_url: 'ws://127.0.0.1:8787/session',
  })),
  getSessionStatus: vi.fn(async () => ({
    active: false,
    bridge: null,
    diagnostics_error: null,
    game_state_mismatch: null,
    log_dir: null,
    melon: null,
    webrtc: null,
  })),
  openLogDir: vi.fn(async () => {}),
  openMelonds: vi.fn(async () => {}),
  openMelondsInputConfig: vi.fn(async () => {}),
  runPreflightCheck: vi.fn(async () => ({
    bridge_path: 'bridge',
    bridge_smoke: 'ok',
    input_script: 'inputs',
    melonds_path: 'melonDS',
    symbols_file: 'symbols',
  })),
  saveDiagnosticEventsEnabled: vi.fn(async () => null),
  savePlayerName: vi.fn(async () => null),
  saveRomPaths: vi.fn(async () => null),
  selectRomFile: vi.fn(async (currentPath: string) => currentPath),
  startMatch: mocks.startMatchMock,
  stopMatch: vi.fn(async () => {}),
}));

function TestProviders({ children }: { children: ReactNode }) {
  const queryClient = new QueryClient({
    defaultOptions: {
      queries: { retry: false },
    },
  });
  return (
    <QueryClientProvider client={queryClient}>{children}</QueryClientProvider>
  );
}

function LauncherHarness() {
  const launcher = useLauncherController();
  return (
    <div>
      <button type="button" onClick={() => void launcher.actions.createRoom()}>
        create
      </button>
      <output aria-label="busy">
        {launcher.matchmakingRooms.busy ? 'busy' : 'idle'}
      </output>
      <output aria-label="status">{launcher.activityStatus?.text ?? ''}</output>
    </div>
  );
}

describe('useLauncherController', () => {
  test('hosted room auto launch clears matchmaking busy after startMatch resolves', async () => {
    let resolveStartMatch: (response: LaunchResponse) => void = () => {};
    mocks.startMatchMock.mockImplementationOnce(
      () =>
        new Promise<LaunchResponse>((resolve) => {
          resolveStartMatch = resolve;
        }),
    );

    const screen = await render(
      <TestProviders>
        <LauncherHarness />
      </TestProviders>,
    );

    await screen.getByRole('button', { name: 'create' }).click();
    await vi.waitFor(() =>
      expect(mocks.startMatchMock).toHaveBeenCalledTimes(1),
    );

    resolveStartMatch({
      bridge_pid: 2001,
      log_dir: 'C:\\logs\\run1',
      melon_pid: 1001,
    });

    await vi.waitFor(async () => {
      await expect
        .element(screen.getByLabelText('busy'))
        .toHaveTextContent('idle');
    });
    await expect
      .element(screen.getByLabelText('status'))
      .toHaveTextContent('起動済み');
  });
});
