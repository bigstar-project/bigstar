import { afterEach, describe, expect, test, vi } from 'vitest';
import { createRoom, joinRoom, listRooms } from './matchmakingClient';
import type { GameSettings } from './types';

const settings: GameSettings = {
  big_stars: 5,
  course_mode: 'random',
  lives: 'endless',
  match_seed: 'seed-1',
  wins: 3,
};

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
        body: JSON.stringify({}),
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
        roomId: 'room-1',
        signalUrl: 'wss://match.example/session',
      }),
    ).rejects.toThrow('room is not joinable');
  });
});
