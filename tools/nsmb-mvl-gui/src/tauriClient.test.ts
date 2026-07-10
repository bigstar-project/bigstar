import { afterEach, describe, expect, test, vi } from 'vitest';
import type { GenerateRomRequest, LaunchRequest } from './types';

const invokeMock = vi.hoisted(() => vi.fn());
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

vi.mock('@tauri-apps/api/core', () => ({
  invoke: invokeMock,
}));

async function importClient() {
  vi.resetModules();
  return import('./tauriClient');
}

function setWindow(value: unknown) {
  vi.stubGlobal('window', value);
}

function setPreviewWindow(search = '') {
  setWindow({ location: { search } });
}

function setPreviewWindowWithStorage(
  search: string,
  storedValue: string | null,
) {
  setWindow({
    location: { search },
    localStorage: {
      getItem: vi.fn(() => storedValue),
      setItem: vi.fn(),
    },
  });
}

afterEach(() => {
  invokeMock.mockReset();
  vi.unstubAllGlobals();
});

describe('タウリクライアント', () => {
  test('タウリ外ではコマンドを呼ばずプレビュー用デフォルト値を返す', async () => {
    setPreviewWindow();
    const client = await importClient();

    await expect(client.getDefaults()).resolves.toMatchObject({
      input_config_opened_once: false,
      player_name: '',
      player_profile_id: 'preview-profile-player',
      roms_prepared_once: false,
      room_code: 'test-room',
      signal_url:
        'wss://nsmb-mvl-signaling-prod.uniunntaro.workers.dev/session',
    });

    expect(invokeMock).not.toHaveBeenCalled();
  });

  test('readyプレビューではオンボーディング完了済みのデフォルト値を返す', async () => {
    setPreviewWindow('?preview=ready');
    const client = await importClient();

    await expect(client.getDefaults()).resolves.toMatchObject({
      base_rom_path: 'C:\\Users\\Sugiyama\\roms\\New Super Mario Bros.nds',
      input_config_opened_once: true,
      player_name: 'Preview Player',
      player_profile_id: 'preview-profile-player',
      roms_prepared_once: true,
    });

    expect(invokeMock).not.toHaveBeenCalled();
  });

  test('プレビューではスタートアップ設定をデフォルトOFFのローカルfallbackで扱う', async () => {
    setPreviewWindow();
    const client = await importClient();

    await expect(client.getStartupEnabled()).resolves.toBe(false);
    await expect(client.setStartupEnabled(true)).resolves.toBeNull();

    expect(invokeMock).not.toHaveBeenCalled();
  });

  test('プレビューでは新規部屋通知設定の保存でコマンドを呼ばない', async () => {
    setPreviewWindow();
    const client = await importClient();

    await expect(
      client.saveNewRoomNotificationsEnabled({ enabled: false }),
    ).resolves.toBeNull();

    expect(invokeMock).not.toHaveBeenCalled();
  });

  test('readyプレビューでは保存済み履歴が空なら仮の対戦履歴を返す', async () => {
    setPreviewWindowWithStorage('?preview=ready', null);
    const client = await importClient();

    const history = await client.loadMatchHistory();

    expect(history).toEqual(
      expect.arrayContaining([
        expect.objectContaining({
          id: 'preview-history-1',
          playerNames: { mario: 'Preview Player', luigi: 'Rival' },
          status: 'completed',
        }),
        expect.objectContaining({
          id: 'preview-history-2',
          role: 'client',
          status: 'stopped',
        }),
        expect.objectContaining({
          id: 'preview-history-3',
          role: 'host',
          status: 'completed',
        }),
        expect.objectContaining({
          id: 'preview-history-4',
          role: 'client',
          status: 'completed',
        }),
      ]),
    );

    expect(invokeMock).not.toHaveBeenCalled();
  });

  test('readyプレビューでも保存済み履歴がある場合はlocalStorageを優先する', async () => {
    setPreviewWindowWithStorage(
      '?preview=ready',
      JSON.stringify([
        {
          id: 'stored-match',
          logDir: '',
          playerNames: { mario: 'Stored Mario', luigi: 'Stored Luigi' },
          role: 'host',
          roomCode: 'stored-room',
          settings: {
            big_stars: 10,
            course_mode: 'random',
            course_stages: [0],
            input_delay_frames: 4,
            input_max_frame_lead: 4,
            lives: '3',
            match_seed: '1',
            rng_seeds: ['1'],
            rollback_enabled: false,
            wins: 1,
          },
          stages: [],
          startedAt: '2026-06-21T00:00:00.000Z',
          status: 'running',
        },
      ]),
    );
    const client = await importClient();

    await expect(client.loadMatchHistory()).resolves.toMatchObject([
      {
        id: 'stored-match',
        playerNames: { mario: 'Stored Mario', luigi: 'Stored Luigi' },
      },
    ]);

    expect(invokeMock).not.toHaveBeenCalled();
  });

  test('タウリ内部オブジェクトがあるとき生成済みコマンドを呼ぶ', async () => {
    setWindow({ __TAURI_INTERNALS__: {} });
    invokeMock.mockResolvedValueOnce({
      base_rom_path: 'C:\\roms\\base.nds',
      client_rom_path: 'C:\\roms\\client.nds',
      host_rom_path: 'C:\\roms\\host.nds',
      input_config_opened_once: true,
      diagnostic_events_enabled: false,
      detailed_logs_enabled: false,
      performance_logs_enabled: false,
      new_room_notifications_enabled: true,
      player_name: 'Alice',
      player_profile_id: '33333333-3333-4333-8333-333333333333',
      port: 8165,
      roms_prepared_once: true,
      room_code: 'room-1',
      signal_url: 'wss://match.example/session',
    });
    const client = await importClient();

    await expect(client.getDefaults()).resolves.toMatchObject({
      base_rom_path: 'C:\\roms\\base.nds',
      room_code: 'room-1',
    });

    expect(invokeMock).toHaveBeenCalledWith('get_defaults');
  });

  test('コマンドペイロードを生成済みバインディングに渡す', async () => {
    setWindow({ __TAURI_INTERNALS__: {} });
    invokeMock
      .mockResolvedValueOnce({
        client_rom: 'C:\\roms\\client.nds',
        generated: true,
        host_rom: 'C:\\roms\\host.nds',
        rom_identity: romIdentity,
      })
      .mockResolvedValueOnce({
        bridge_pid: 20,
        log_dir: 'C:\\logs\\run1',
        melon_pid: 10,
      });
    const client = await importClient();
    const romRequest: GenerateRomRequest = {
      source_rom: 'C:\\roms\\base.nds',
    };
    const launchRequest: LaunchRequest = {
      port: 8165,
      role: 'host',
      rom_path: 'C:\\roms\\host.nds',
      room_code: 'room-1',
      settings: {
        big_stars: 5,
        course_mode: 'random',
        course_stages: [2, 3, 4, 0, 1],
        input_delay_frames: 4,
        input_max_frame_lead: 4,
        lives: 'endless',
        match_seed: '1',
        rng_seeds: ['1', '2', '3', '4', '5'],
        rollback_enabled: false,
        wins: 3,
      },
      diagnostic_events_enabled: true,
      detailed_logs_enabled: true,
      performance_logs_enabled: true,
      signal_url: 'wss://match.example/session',
    };

    await expect(client.generateRoms(romRequest)).resolves.toMatchObject({
      generated: true,
    });
    await expect(client.startMatch(launchRequest)).resolves.toMatchObject({
      log_dir: 'C:\\logs\\run1',
    });

    expect(invokeMock).toHaveBeenNthCalledWith(1, 'generate_roms', {
      request: romRequest,
    });
    expect(invokeMock).toHaveBeenNthCalledWith(2, 'start_match', {
      request: launchRequest,
    });
  });

  test('通常のエラー以外のコマンド拒否をコマンドエラーとして展開する', async () => {
    setWindow({ __TAURI_INTERNALS__: {} });
    invokeMock.mockRejectedValueOnce('preflight failed');
    const client = await importClient();

    await expect(client.runPreflightCheck()).rejects.toBe('preflight failed');
  });
});
