import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { NuqsAdapter } from 'nuqs/adapters/react';
import type { ReactNode } from 'react';
import { afterEach, beforeEach, describe, expect, test, vi } from 'vitest';
import { render } from 'vitest-browser-react';
import {
  getRoom,
  listRooms,
  subscribeHostRoomEvents,
  subscribeLobbyRooms,
} from '../matchmakingClient';
import { notifyNewRoomAvailable } from '../roomNotifications';
import { getDefaults } from '../tauriClient';
import type { LaunchRequest, LaunchResponse } from '../types';
import { useLauncherController } from './useLauncherController';

const mocks = vi.hoisted(() => {
  const hostProfileId = '11111111-1111-4111-8111-111111111111';
  const clientProfileId = '22222222-2222-4222-8222-222222222222';
  const romIdentity = {
    client_rom_sha256:
      '2222222222222222222222222222222222222222222222222222222222222222',
    generator_id:
      '3333333333333333333333333333333333333333333333333333333333333333',
    host_rom_sha256:
      '1111111111111111111111111111111111111111111111111111111111111111',
    rom_pair_id:
      '4444444444444444444444444444444444444444444444444444444444444444',
    bridge_sha256:
      '5555555555555555555555555555555555555555555555555555555555555555',
  };
  return {
    romIdentity,
    roomDetail: {
      can_join: false,
      client_name: 'Client Player',
      client_player_profile_id: clientProfileId,
      created_at: 1,
      expires_at: Date.now() + 600_000,
      host_name: 'Host Player',
      host_player_profile_id: hostProfileId,
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
      status: 'joining',
      updated_at: 2,
    },
    hostRoomEventHandlers: [] as Array<{
      onJoined: (room: unknown) => void;
    }>,
    lobbyHandlers: [] as Array<{
      onOpen?: () => void;
      onSnapshot: (rooms: unknown[]) => void;
    }>,
    startMatchMock: vi.fn((_request: LaunchRequest) =>
      Promise.resolve<LaunchResponse>({
        bridge_pid: 2001,
        log_dir: 'C:\\logs\\run1',
        melon_pid: 1001,
      }),
    ),
    upsertMatchHistoryMock: vi.fn(async () => null),
    hostProfileId,
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
  subscribeHostRoomEvents: vi.fn((_signalUrl, _roomId, handlers) => {
    mocks.hostRoomEventHandlers.push(handlers);
    return vi.fn();
  }),
  subscribeLobbyRooms: vi.fn((_signalUrl, handlers) => {
    mocks.lobbyHandlers.push(handlers);
    handlers.onOpen?.();
    return vi.fn();
  }),
}));

vi.mock('../roomNotifications', () => ({
  notifyNewRoomAvailable: vi.fn(async () => true),
}));

vi.mock('../tauriClient', () => ({
  createLogArchive: vi.fn(async () => ({
    archive_path: 'C:\\logs\\run1\\nsmb-mvl-logs.zip',
    size: 1024,
  })),
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
    detailed_logs_enabled: false,
    performance_logs_enabled: false,
    host_rom_path: 'C:\\roms\\host.nds',
    input_config_opened_once: true,
    log_archive_upload_token: '',
    new_room_notifications_enabled: true,
    player_name: 'Host Player',
    player_profile_id: mocks.hostProfileId,
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
    mvl_results: [],
    webrtc: null,
  })),
  getStartupEnabled: vi.fn(async () => false),
  deleteMatchHistory: vi.fn(async () => null),
  cleanupDetailedLogs: vi.fn(async () => ({
    deleted_dirs: 1,
    deleted_files: 3,
    freed_bytes: 1024,
    scanned_log_dirs: 2,
    skipped_active_log_dirs: 0,
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
  saveDetailedLogsEnabled: vi.fn(async () => null),
  savePerformanceLogsEnabled: vi.fn(async () => null),
  saveNewRoomNotificationsEnabled: vi.fn(async () => null),
  savePlayerName: vi.fn(async () => null),
  saveRomPaths: vi.fn(async () => null),
  selectRomFile: vi.fn(async (currentPath: string) => currentPath),
  setStartupEnabled: vi.fn(async () => null),
  startMatch: mocks.startMatchMock,
  stopMatch: vi.fn(async () => {}),
  upsertMatchHistory: mocks.upsertMatchHistoryMock,
  uploadLogArchive: vi.fn(async () => ({
    archive_path: 'C:\\logs\\run1\\nsmb-mvl-logs.zip',
    key: 'log-archives/test/nsmb-mvl-logs.zip',
    size: 1024,
  })),
}));

beforeEach(() => {
  vi.clearAllMocks();
  mocks.hostRoomEventHandlers = [];
  mocks.lobbyHandlers = [];
  window.history.replaceState(null, '', window.location.pathname);
});

afterEach(() => {
  vi.unstubAllGlobals();
});

function roomSummary(
  roomId: string,
  hostProfileId = '33333333-3333-4333-8333-333333333333',
) {
  return {
    can_join: true,
    created_at: 1,
    expires_at: Date.now() + 600_000,
    host_name: 'Host Player',
    host_player_profile_id: hostProfileId,
    peer_count: 1,
    room_id: roomId,
    rom_identity: mocks.romIdentity,
    settings: {
      ...mocks.roomDetail.settings,
      big_stars: 10 as const,
      course_mode: 'random' as const,
      lives: '3' as const,
    },
    status: 'open' as const,
    updated_at: 1,
  };
}

function TestProviders({ children }: { children: ReactNode }) {
  const queryClient = new QueryClient({
    defaultOptions: {
      queries: { retry: false },
    },
  });
  return (
    <QueryClientProvider client={queryClient}>
      <NuqsAdapter>{children}</NuqsAdapter>
    </QueryClientProvider>
  );
}

function LauncherHarness() {
  const launcher = useLauncherController();
  return (
    <div>
      <button type="button" onClick={() => void launcher.actions.createRoom()}>
        create
      </button>
      <button type="button" onClick={() => void launcher.actions.stopMatch()}>
        stop
      </button>
      <output aria-label="busy">
        {launcher.matchmakingRooms.busy ? 'busy' : 'idle'}
      </output>
      <output aria-label="status">{launcher.activityStatus?.text ?? ''}</output>
      <output aria-label="opponent">
        {launcher.currentMatch?.playerNames.luigi ?? ''}
      </output>
      <output aria-label="room-count">
        {launcher.matchmakingRooms.rooms.length}
      </output>
      <output aria-label="active-view">{launcher.activeView}</output>
      <button type="button" onClick={() => launcher.changeView('history')}>
        history
      </button>
      <button type="button" onClick={() => launcher.changeView('settings')}>
        settings
      </button>
      <button
        type="button"
        onClick={() => void launcher.actions.refreshRooms()}
      >
        refresh
      </button>
    </div>
  );
}

describe('useLauncherController', () => {
  test('画面遷移を履歴に追加してpopstateで復元する', async () => {
    const pushState = vi.spyOn(window.history, 'pushState');
    const screen = await render(
      <TestProviders>
        <LauncherHarness />
      </TestProviders>,
    );

    await screen.getByRole('button', { name: 'settings' }).click();
    await vi.waitFor(() =>
      expect(new URLSearchParams(window.location.search).get('view')).toBe(
        'settings',
      ),
    );
    expect(pushState).toHaveBeenCalled();
    await expect
      .element(screen.getByLabelText('active-view'))
      .toHaveTextContent('settings');

    window.history.replaceState(null, '', '?view=battle');
    window.dispatchEvent(new PopStateEvent('popstate'));
    await expect
      .element(screen.getByLabelText('active-view'))
      .toHaveTextContent('battle');
    pushState.mockRestore();
  });

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
      expect(subscribeHostRoomEvents).toHaveBeenCalledTimes(1),
    );
    mocks.hostRoomEventHandlers.at(-1)?.onJoined(mocks.roomDetail);
    await vi.waitFor(() =>
      expect(mocks.startMatchMock).toHaveBeenCalledTimes(1),
    );
    expect(getRoom).not.toHaveBeenCalled();

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
    await expect
      .element(screen.getByLabelText('opponent'))
      .toHaveTextContent('Client Player');
  });

  test('stopped match remains visible until the next match starts', async () => {
    const screen = await render(
      <TestProviders>
        <LauncherHarness />
      </TestProviders>,
    );

    await screen.getByRole('button', { name: 'create' }).click();
    await vi.waitFor(() =>
      expect(subscribeHostRoomEvents).toHaveBeenCalledTimes(1),
    );
    mocks.hostRoomEventHandlers.at(-1)?.onJoined(mocks.roomDetail);
    await vi.waitFor(() =>
      expect(mocks.startMatchMock).toHaveBeenCalledTimes(1),
    );
    await expect
      .element(screen.getByLabelText('opponent'))
      .toHaveTextContent('Client Player');

    await screen.getByRole('button', { name: 'stop' }).click();

    await expect
      .element(screen.getByLabelText('opponent'))
      .toHaveTextContent('Client Player');
  });

  test('lobby websocket snapshot updates rooms and notifies only rooms added after the initial snapshot', async () => {
    window.history.replaceState(null, '', '?view=settings');
    const screen = await render(
      <TestProviders>
        <LauncherHarness />
      </TestProviders>,
    );

    await vi.waitFor(() => expect(subscribeLobbyRooms).toHaveBeenCalled());
    mocks.lobbyHandlers.at(-1)?.onSnapshot([roomSummary('old-room')]);

    await expect
      .element(screen.getByLabelText('room-count'))
      .toHaveTextContent('1');
    expect(notifyNewRoomAvailable).not.toHaveBeenCalled();

    mocks.lobbyHandlers
      .at(-1)
      ?.onSnapshot([roomSummary('old-room'), roomSummary('new-room')]);

    await vi.waitFor(() =>
      expect(notifyNewRoomAvailable).toHaveBeenCalledWith(
        expect.objectContaining({
          host_name: 'Host Player',
          room_id: 'new-room',
        }),
      ),
    );
    await expect
      .element(screen.getByLabelText('room-count'))
      .toHaveTextContent('2');
    expect(listRooms).not.toHaveBeenCalled();
  });

  test('lobby websocket snapshot does not notify rooms hosted by the local player', async () => {
    vi.stubGlobal('__NSMB_MVL_BUILD_PROFILE__', 'distribution');

    await render(
      <TestProviders>
        <LauncherHarness />
      </TestProviders>,
    );

    await vi.waitFor(() => expect(subscribeLobbyRooms).toHaveBeenCalled());
    mocks.lobbyHandlers.at(-1)?.onSnapshot([]);
    mocks.lobbyHandlers
      .at(-1)
      ?.onSnapshot([
        roomSummary('old-room'),
        roomSummary('own-room', mocks.hostProfileId),
      ]);

    await vi.waitFor(() =>
      expect(notifyNewRoomAvailable).toHaveBeenCalledWith(
        expect.objectContaining({ room_id: 'old-room' }),
      ),
    );
    expect(notifyNewRoomAvailable).not.toHaveBeenCalledWith(
      expect.objectContaining({ room_id: 'own-room' }),
    );
    expect(notifyNewRoomAvailable).toHaveBeenCalledTimes(1);
  });

  test('local build can notify rooms hosted by the local player for two-window debugging', async () => {
    vi.stubGlobal('__NSMB_MVL_BUILD_PROFILE__', 'local');

    await render(
      <TestProviders>
        <LauncherHarness />
      </TestProviders>,
    );

    await vi.waitFor(() => expect(subscribeLobbyRooms).toHaveBeenCalled());
    mocks.lobbyHandlers.at(-1)?.onSnapshot([]);
    mocks.lobbyHandlers
      .at(-1)
      ?.onSnapshot([roomSummary('own-room', mocks.hostProfileId)]);

    await vi.waitFor(() =>
      expect(notifyNewRoomAvailable).toHaveBeenCalledWith(
        expect.objectContaining({ room_id: 'own-room' }),
      ),
    );
  });

  test('lobby websocket snapshot does not notify new rooms when the setting is disabled', async () => {
    vi.mocked(getDefaults).mockResolvedValueOnce({
      base_rom_path: 'C:\\roms\\base.nds',
      client_rom_path: 'C:\\roms\\client.nds',
      diagnostic_events_enabled: false,
      detailed_logs_enabled: false,
      performance_logs_enabled: false,
      host_rom_path: 'C:\\roms\\host.nds',
      input_config_opened_once: true,
      log_archive_upload_token: '',
      new_room_notifications_enabled: false,
      player_name: 'Host Player',
      player_profile_id: mocks.hostProfileId,
      port: 8165,
      roms_prepared_once: true,
      room_code: 'test-room',
      signal_url: 'ws://127.0.0.1:8787/session',
    });

    const screen = await render(
      <TestProviders>
        <LauncherHarness />
      </TestProviders>,
    );

    await vi.waitFor(() => expect(subscribeLobbyRooms).toHaveBeenCalled());
    mocks.lobbyHandlers.at(-1)?.onSnapshot([roomSummary('old-room')]);
    mocks.lobbyHandlers
      .at(-1)
      ?.onSnapshot([roomSummary('old-room'), roomSummary('new-room')]);

    await expect
      .element(screen.getByLabelText('room-count'))
      .toHaveTextContent('2');
    expect(notifyNewRoomAvailable).not.toHaveBeenCalled();
  });

  test('manual room refresh uses GET rooms as a snapshot fallback without notification', async () => {
    const listRoomsMock = vi.mocked(listRooms);
    listRoomsMock.mockResolvedValueOnce({
      rooms: [roomSummary('manual-room')],
    });

    const screen = await render(
      <TestProviders>
        <LauncherHarness />
      </TestProviders>,
    );

    await screen.getByRole('button', { name: 'refresh' }).click();

    await vi.waitFor(() =>
      expect(listRoomsMock).toHaveBeenCalledWith('ws://127.0.0.1:8787/session'),
    );
    await expect
      .element(screen.getByLabelText('room-count'))
      .toHaveTextContent('1');
    expect(notifyNewRoomAvailable).not.toHaveBeenCalled();
  });
});
