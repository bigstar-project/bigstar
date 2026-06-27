import { readFileSync } from 'node:fs';
import { expect, type Page, test } from '@playwright/test';

const packageJson = JSON.parse(
  readFileSync(new URL('../package.json', import.meta.url), 'utf8'),
) as { version: string };

const settings = {
  course_mode: 'random',
  course_stages: [0, 1, 2, 3, 4],
  wins: 3,
  big_stars: 10,
  lives: '3',
  match_seed: '123',
  rng_seeds: ['123', '124', '125', '126', '127'],
  input_delay_frames: 3,
  input_max_frame_lead: 4,
  rollback_enabled: false,
};

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
const hostProfileId = '11111111-1111-4111-8111-111111111111';
const clientProfileId = '22222222-2222-4222-8222-222222222222';

async function installGuiDriver(
  page: Page,
  options: {
    inputConfigOpened?: boolean;
    playerName?: string;
    romsPrepared?: boolean;
  } = {},
) {
  await page.addInitScript(
    ({
      hostProfileId,
      inputConfigOpened,
      playerName,
      romIdentity,
      romsPrepared,
    }) => {
      const state = {
        active: false,
        inputConfigOpened,
        lastLogDir: null as string | null,
        playerName,
        playerProfileId: hostProfileId,
        romsPrepared,
      };
      const calls: { args: unknown[]; name: string }[] = [];
      window.addEventListener('error', (event) => {
        calls.push({ args: [event.message], name: 'window_error' });
      });
      window.addEventListener('unhandledrejection', (event) => {
        calls.push({
          args: [String(event.reason)],
          name: 'unhandledrejection',
        });
      });

      Object.assign(window, {
        __NSMB_MVL_E2E__: { calls, state },
        __TAURI_INTERNALS__: {
          invoke: (command: string, args: Record<string, unknown> = {}) => {
            if (command.startsWith('plugin:updater|')) {
              return null;
            }
            if (command === 'ensure_roms') {
              calls.push({ args: [args.request], name: command });
              return {
                client_rom: 'C:\\roms\\client.nds',
                generated: false,
                host_rom: 'C:\\roms\\host.nds',
                rom_identity: romIdentity,
              };
            }
            if (command === 'generate_roms') {
              calls.push({ args: [args.request], name: command });
              state.romsPrepared = true;
              return {
                client_rom: 'C:\\roms\\client.nds',
                generated: true,
                host_rom: 'C:\\roms\\host.nds',
                rom_identity: romIdentity,
              };
            }
            if (command === 'get_defaults') {
              return {
                base_rom_path: state.romsPrepared ? 'C:\\roms\\base.nds' : '',
                client_rom_path: 'C:\\roms\\client.nds',
                host_rom_path: 'C:\\roms\\host.nds',
                input_config_opened_once: state.inputConfigOpened,
                diagnostic_events_enabled: false,
                new_room_notifications_enabled: true,
                player_name: state.playerName,
                player_profile_id: state.playerProfileId,
                port: 8165,
                roms_prepared_once: state.romsPrepared,
                room_code: 'test-room',
                signal_url: 'ws://127.0.0.1:8787/session',
              };
            }
            if (command === 'session_status') {
              return {
                active: state.active,
                bridge: state.active ? 'running' : null,
                diagnostics_error: null,
                log_dir: state.lastLogDir,
                melon: state.active ? 'running' : null,
                webrtc: null,
              };
            }
            if (command === 'open_log_dir') {
              calls.push({ args: [args.path], name: command });
              return null;
            }
            if (command === 'open_melonds_input_config') {
              calls.push({ args: [], name: command });
              state.inputConfigOpened = true;
              return 3002;
            }
            if (command === 'preflight_check') {
              calls.push({ args: [], name: command });
              return {
                bridge_path: 'bridge',
                bridge_smoke: 'ok',
                input_script: 'input',
                melonds_path: 'melonDS',
                symbols_file: 'symbols',
              };
            }
            if (command === 'save_rom_paths') {
              calls.push({ args: [args.request], name: command });
              return null;
            }
            if (command === 'save_diagnostic_events_enabled') {
              calls.push({ args: [args.request], name: command });
              return null;
            }
            if (command === 'save_new_room_notifications_enabled') {
              calls.push({ args: [args.request], name: command });
              return null;
            }
            if (command === 'save_player_name') {
              calls.push({ args: [args.request], name: command });
              state.playerName = (
                args.request as { player_name: string }
              ).player_name;
              return null;
            }
            if (command === 'select_rom_file') {
              return 'C:\\roms\\base.nds';
            }
            if (command === 'start_match') {
              calls.push({ args: [args.request], name: command });
              state.active = true;
              state.lastLogDir = 'C:\\logs\\run1';
              return {
                bridge_pid: 200,
                log_dir: state.lastLogDir,
                melon_pid: 100,
              };
            }
            if (command === 'stop_match') {
              calls.push({ args: [], name: command });
              state.active = false;
              return null;
            }
            throw new Error(`unexpected Tauri command: ${command}`);
          },
        },
      });
    },
    {
      hostProfileId,
      inputConfigOpened: options.inputConfigOpened ?? true,
      playerName: options.playerName ?? 'Player',
      romIdentity,
      romsPrepared: options.romsPrepared ?? true,
    },
  );
}

async function installRoomsApi(page: Page) {
  const publicRoom = {
    can_join: true,
    created_at: 1,
    expires_at: Date.now() + 600_000,
    host_name: 'Host Player',
    host_player_profile_id: hostProfileId,
    peer_count: 1,
    room_id: 'room12345',
    settings,
    rom_identity: romIdentity,
    status: 'open',
    updated_at: 1,
  };
  await page.addInitScript((publicRoom) => {
    class RoomsWebSocket extends EventTarget {
      readonly url: string;
      readyState: number = WebSocket.CONNECTING;

      constructor(url: string | URL) {
        super();
        this.url = String(url);
        window.setTimeout(() => {
          this.readyState = WebSocket.OPEN;
          this.dispatchEvent(new Event('open'));
          if (new URL(this.url).pathname === '/rooms/subscribe') {
            this.dispatchEvent(
              new MessageEvent('message', {
                data: JSON.stringify({
                  rooms: [publicRoom],
                  type: 'rooms_snapshot',
                }),
              }),
            );
          }
        }, 0);
      }

      close() {
        this.readyState = WebSocket.CLOSED;
        this.dispatchEvent(new CloseEvent('close'));
      }

      send() {}
    }

    Object.assign(RoomsWebSocket, {
      CLOSED: WebSocket.CLOSED,
      CLOSING: WebSocket.CLOSING,
      CONNECTING: WebSocket.CONNECTING,
      OPEN: WebSocket.OPEN,
    });
    window.WebSocket = RoomsWebSocket as unknown as typeof WebSocket;
  }, publicRoom);

  await page.route('**/rooms', async (route) => {
    if (route.request().method() !== 'GET') {
      await route.fallback();
      return;
    }
    await route.fulfill({
      contentType: 'application/json',
      json: {
        rooms: [publicRoom],
      },
    });
  });
  await page.route('**/rooms/room12345', async (route) => {
    if (route.request().method() !== 'GET') {
      await route.fallback();
      return;
    }
    await route.fulfill({
      contentType: 'application/json',
      json: {
        can_join: true,
        created_at: 1,
        expires_at: Date.now() + 600_000,
        host_name: 'Host Player',
        host_player_profile_id: hostProfileId,
        peer_count: 1,
        room_id: 'room12345',
        settings,
        rom_identity: romIdentity,
        status: 'open',
        updated_at: 1,
      },
    });
  });
  await page.route('**/rooms/room12345/join', async (route) => {
    await route.fulfill({
      contentType: 'application/json',
      json: {
        join_token: 'join-token',
        host_player_profile_id: hostProfileId,
        client_player_profile_id: clientProfileId,
        room_id: 'room12345',
        rom_identity: romIdentity,
        settings,
        signal_url: 'ws://127.0.0.1:8787/session',
      },
    });
  });
}

async function e2eCalls(page: Page) {
  return page.evaluate(
    () =>
      (
        window as typeof window & {
          __NSMB_MVL_E2E__: { calls: { args: unknown[]; name: string }[] };
        }
      ).__NSMB_MVL_E2E__.calls,
  );
}

async function waitForGuiReady(page: Page) {
  await expect(page.getByText('未接続').first()).toBeVisible();
}

async function callNames(page: Page) {
  return (await e2eCalls(page)).map((call) => call.name);
}

function lastCall(calls: { args: unknown[]; name: string }[], name: string) {
  return [...calls].reverse().find((call) => call.name === name);
}

test('初回セットアップでロム生成と入力設定を完了できる', async ({ page }) => {
  await installGuiDriver(page, {
    inputConfigOpened: false,
    playerName: '',
    romsPrepared: false,
  });
  await installRoomsApi(page);

  await page.goto('/');
  await waitForGuiReady(page);

  await expect(
    page.getByRole('heading', { name: '初回セットアップ' }),
  ).toBeVisible();
  await expect(
    page.getByText(`v${packageJson.version}`, { exact: true }),
  ).toBeVisible();
  const onboardingDialog = page.getByRole('dialog');
  await onboardingDialog.getByLabel('プレイヤーネーム').fill('Alice');
  await onboardingDialog.getByRole('button', { name: '保存' }).click();
  await expect.poll(() => callNames(page)).toContain('save_player_name');
  await page.getByRole('button', { name: 'ROMを選んで生成' }).click();
  await expect.poll(() => callNames(page)).toContain('generate_roms');
  const inputConfigButton = page.getByRole('button', {
    name: '入力設定を開く',
  });
  await expect(inputConfigButton).toBeEnabled();
  await inputConfigButton.click();

  await expect(
    page.getByRole('heading', { name: '初回セットアップ' }),
  ).toBeHidden();
  await expect(page.getByRole('heading', { name: '対戦' })).toBeVisible();

  const calls = await e2eCalls(page);
  expect(calls.map((call) => call.name)).toContain('generate_roms');
  expect(calls.map((call) => call.name)).toContain('open_melonds_input_config');
});

test('手動接続でクライアント起動ペイロードを作れる', async ({ page }) => {
  await installGuiDriver(page);
  await installRoomsApi(page);

  await page.goto('/');
  await waitForGuiReady(page);
  await page.getByText('部屋コードとロールを編集').click();
  await page.getByLabel('部屋コード').fill('manual-room');
  await page
    .locator('button[aria-pressed="false"]')
    .filter({ hasText: 'answer側' })
    .click();
  await page.getByRole('button', { name: '対戦を開始' }).click();

  await expect.poll(() => callNames(page)).toContain('start_match');

  const calls = await e2eCalls(page);
  const start = lastCall(calls, 'start_match');
  expect(start?.args[0]).toMatchObject({
    port: 8165,
    role: 'client',
    room_code: 'manual-room',
    rom_path: 'C:\\roms\\client.nds',
    diagnostic_events_enabled: false,
    settings: {
      big_stars: 10,
      course_mode: 'random',
      input_delay_frames: 3,
      input_max_frame_lead: 4,
      lives: '3',
      rollback_enabled: false,
      wins: 3,
    },
    signal_url: 'ws://127.0.0.1:8787/session',
  });
});

test('公開ルーム参加でサーバー側の対戦設定を引き継いで起動する', async ({
  page,
}) => {
  await installGuiDriver(page);
  await installRoomsApi(page);

  await page.goto('/');
  await waitForGuiReady(page);
  await expect(page.getByText('Host Player')).toBeVisible();
  await page.getByRole('button', { name: '参加' }).first().click();

  await expect.poll(() => callNames(page)).toContain('start_match');

  const calls = await e2eCalls(page);
  const start = lastCall(calls, 'start_match');
  expect(start?.args[0]).toMatchObject({
    role: 'client',
    room_code: 'room12345',
    rom_path: 'C:\\roms\\client.nds',
    diagnostic_events_enabled: false,
    settings,
    signal_url: 'ws://127.0.0.1:8787/session?token=join-token',
  });
});
