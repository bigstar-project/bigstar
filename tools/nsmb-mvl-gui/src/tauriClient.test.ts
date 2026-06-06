import { afterEach, describe, expect, test, vi } from 'vitest';
import type { GenerateRomRequest, LaunchRequest } from './types';

const invokeMock = vi.hoisted(() => vi.fn());

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

afterEach(() => {
  invokeMock.mockReset();
  vi.unstubAllGlobals();
});

describe('タウリクライアント', () => {
  test('タウリ外ではコマンドを呼ばずプレビュー用デフォルト値を返す', async () => {
    setWindow({});
    const client = await importClient();

    await expect(client.getDefaults()).resolves.toMatchObject({
      room_code: 'test-room',
      signal_url:
        'wss://nsmb-mvl-signaling-prod.uniunntaro.workers.dev/session',
    });

    expect(invokeMock).not.toHaveBeenCalled();
  });

  test('タウリ内部オブジェクトがあるとき生成済みコマンドを呼ぶ', async () => {
    setWindow({ __TAURI_INTERNALS__: {} });
    invokeMock.mockResolvedValueOnce({
      base_rom_path: 'C:\\roms\\base.nds',
      client_rom_path: 'C:\\roms\\client.nds',
      host_rom_path: 'C:\\roms\\host.nds',
      input_config_opened_once: true,
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
      })
      .mockResolvedValueOnce({
        bridge_pid: 20,
        log_dir: 'C:\\logs\\run1',
        melon_pid: 10,
      });
    const client = await importClient();
    const romRequest: GenerateRomRequest = {
      settings: {
        big_stars: 5,
        course_mode: 'random',
        lives: 'endless',
        match_seed: 'seed-1',
        wins: 3,
      },
      source_rom: 'C:\\roms\\base.nds',
      stage: 2,
    };
    const launchRequest: LaunchRequest = {
      port: 8165,
      role: 'host',
      rom_path: 'C:\\roms\\host.nds',
      room_code: 'room-1',
      settings: romRequest.settings,
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
