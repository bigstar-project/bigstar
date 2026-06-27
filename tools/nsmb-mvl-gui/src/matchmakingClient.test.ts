import { afterEach, describe, expect, test, vi } from 'vitest';
import {
  createRoom,
  hostRoomEventsUrl,
  joinRoom,
  listRooms,
  lobbyRoomsSubscribeUrl,
} from './matchmakingClient';
import type { GameSettings } from './types';

const settings: GameSettings = {
  big_stars: 5,
  course_mode: 'random',
  course_stages: [0, 1, 2, 3, 4],
  input_delay_frames: 4,
  input_max_frame_lead: 4,
  lives: 'endless',
  match_seed: '1',
  rng_seeds: ['1', '2', '3', '4', '5'],
  rollback_enabled: false,
  wins: 3,
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

function jsonResponse(body: unknown, init?: ResponseInit) {
  return new Response(JSON.stringify(body), {
    headers: { 'content-type': 'application/json' },
    ...init,
  });
}

afterEach(() => {
  vi.unstubAllGlobals();
});

describe('マッチメイキングクライアント', () => {
  test('Lobby WebSocket 購読URLをシグナリングURLから組み立てる', () => {
    expect(
      lobbyRoomsSubscribeUrl('wss://match.example/session?token=secret'),
    ).toBe('wss://match.example/rooms/subscribe');
    expect(lobbyRoomsSubscribeUrl('http://127.0.0.1:8787/session')).toBe(
      'ws://127.0.0.1:8787/rooms/subscribe',
    );
  });

  test('部屋主イベントURLはhost tokenだけを引き継ぐ', () => {
    expect(
      hostRoomEventsUrl(
        'wss://match.example/session?room=old&role=offer&token=host-secret',
        'room-1',
      ),
    ).toBe('wss://match.example/rooms/room-1/events?token=host-secret');
  });

  test('シグナリング用ウェブソケット URL から HTTP API の部屋一覧を取得する', async () => {
    const fetch = vi.fn(async () =>
      jsonResponse({
        rooms: [{ room_id: 'room-1', host_name: 'Alice' }],
      }),
    );
    vi.stubGlobal('fetch', fetch);

    await expect(listRooms('wss://match.example/session')).resolves.toEqual({
      rooms: [{ room_id: 'room-1', host_name: 'Alice' }],
    });

    expect(fetch).toHaveBeenCalledWith(
      'https://match.example/rooms',
      expect.objectContaining({ method: 'GET' }),
    );
  });

  test('部屋を作成して返されたシグナリング URL にホストトークンを付与する', async () => {
    const fetch = vi.fn(async () =>
      jsonResponse({
        host_token: 'host-secret',
        room_id: 'room-1',
        signal_url: 'wss://match.example/session?room=room-1',
      }),
    );
    vi.stubGlobal('fetch', fetch);

    await expect(
      createRoom({
        hostName: 'Alice',
        hostProfileId,
        romIdentity,
        settings,
        signalUrl: 'wss://match.example/session',
      }),
    ).resolves.toMatchObject({
      room_id: 'room-1',
      signal_url: 'wss://match.example/session?room=room-1&token=host-secret',
    });

    expect(fetch).toHaveBeenCalledWith(
      'https://match.example/rooms',
      expect.objectContaining({
        body: JSON.stringify({
          host_name: 'Alice',
          host_player_profile_id: hostProfileId,
          rom_identity: romIdentity,
          settings,
        }),
        method: 'POST',
      }),
    );
  });

  test('部屋に参加して返されたシグナリング URL に参加トークンを付与する', async () => {
    const fetch = vi.fn(async () =>
      jsonResponse({
        join_token: 'join-secret',
        room_id: 'room-1',
        signal_url: 'wss://match.example/session',
      }),
    );
    vi.stubGlobal('fetch', fetch);

    await expect(
      joinRoom({
        playerName: 'Bob',
        playerProfileId: clientProfileId,
        romPairId: romIdentity.rom_pair_id,
        roomId: 'room-1',
        signalUrl: 'ws://127.0.0.1:8787/session?old=true',
      }),
    ).resolves.toMatchObject({
      room_id: 'room-1',
      signal_url: 'wss://match.example/session?token=join-secret',
    });

    expect(fetch).toHaveBeenCalledWith(
      'http://127.0.0.1:8787/rooms/room-1/join',
      expect.objectContaining({
        body: JSON.stringify({
          player_name: 'Bob',
          player_profile_id: clientProfileId,
          rom_pair_id: romIdentity.rom_pair_id,
        }),
        method: 'POST',
      }),
    );
  });

  test('サーバーが拒否したとき API のエラーメッセージを投げる', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn(async () =>
        jsonResponse({ error: 'room is not joinable' }, { status: 409 }),
      ),
    );

    await expect(
      joinRoom({
        playerName: 'Bob',
        playerProfileId: clientProfileId,
        romPairId: romIdentity.rom_pair_id,
        roomId: 'room-1',
        signalUrl: 'wss://match.example/session',
      }),
    ).rejects.toThrow('room is not joinable');
  });
});
