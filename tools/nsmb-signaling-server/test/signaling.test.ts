import { SELF } from 'cloudflare:test';
import { describe, expect, test } from 'vitest';
import type { WsServerMessage } from '../src/schemas';

const gameSettings = {
  big_stars: 5,
  course_mode: 'random',
  lives: 'endless',
  match_seed: '123',
  wins: 2,
};

async function json<T>(response: Response): Promise<T> {
  return response.json() as Promise<T>;
}

async function createRoom() {
  const response = await SELF.fetch('https://match.test/rooms', {
    body: JSON.stringify({
      host_name: 'Host Player',
      settings: gameSettings,
    }),
    headers: { 'content-type': 'application/json' },
    method: 'POST',
  });
  expect(response.status).toBe(201);
  return json<{
    host_token: string;
    room_id: string;
    signal_url: string;
  }>(response);
}

async function reserveJoin(roomId: string) {
  const response = await SELF.fetch(`https://match.test/rooms/${roomId}/join`, {
    body: '{}',
    headers: { 'content-type': 'application/json' },
    method: 'POST',
  });
  expect(response.status).toBe(200);
  return json<{
    join_token: string;
    room_id: string;
    signal_url: string;
  }>(response);
}

function sessionUrl(
  signalUrl: string,
  roomId: string,
  role: 'answer' | 'offer',
  token: string,
) {
  const url = new URL(signalUrl);
  url.protocol = url.protocol === 'wss:' ? 'https:' : 'http:';
  url.searchParams.set('room', roomId);
  url.searchParams.set('role', role);
  url.searchParams.set('token', token);
  return url.toString();
}

async function connectWebSocket(url: string) {
  const response = await SELF.fetch(url, {
    headers: { Upgrade: 'websocket' },
  });
  expect(response.status).toBe(101);
  const ws = response.webSocket;
  expect(ws).toBeDefined();
  ws?.accept();

  const messages: WsServerMessage[] = [];
  const waiters: Array<(message: WsServerMessage) => void> = [];
  ws?.addEventListener('message', (event: MessageEvent) => {
    const message = JSON.parse(String(event.data)) as WsServerMessage;
    messages.push(message);
    waiters.shift()?.(message);
  });

  return {
    close: () => ws?.close(),
    messages,
    nextMessage: () =>
      new Promise<WsServerMessage>((resolve, reject) => {
        const next = messages.shift();
        if (next) {
          resolve(next);
          return;
        }
        const timer = setTimeout(
          () => reject(new Error('timed out waiting for websocket message')),
          2000,
        );
        waiters.push((message) => {
          clearTimeout(timer);
          resolve(message);
        });
      }),
    send: (message: unknown) => ws?.send(JSON.stringify(message)),
  };
}

async function waitForMessage(
  socket: Awaited<ReturnType<typeof connectWebSocket>>,
  type: WsServerMessage['type'],
) {
  for (let index = 0; index < 8; index += 1) {
    const message = await socket.nextMessage();
    if (message.type === type) {
      return message;
    }
  }
  throw new Error(`message ${type} was not received`);
}

describe('マッチメイキング HTTP API', () => {
  test('部屋の作成、一覧取得、参加予約、クローズができる', async () => {
    const health = await SELF.fetch('https://match.test/health');
    expect(health.status).toBe(200);
    await expect(json(health)).resolves.toEqual({ ok: true });

    const created = await createRoom();
    expect(created.room_id).toMatch(/^[A-Za-z0-9_-]{8,64}$/);
    expect(created.host_token).toHaveLength(32);
    expect(created.signal_url).toBe('wss://match.test/session');

    const listed = await SELF.fetch('https://match.test/rooms');
    expect(listed.status).toBe(200);
    expect(await json(listed)).toMatchObject({
      rooms: [
        {
          can_join: true,
          host_name: 'Host Player',
          room_id: created.room_id,
          settings: gameSettings,
          status: 'open',
        },
      ],
    });

    const joined = await reserveJoin(created.room_id);
    expect(joined.join_token).toHaveLength(32);
    expect(joined.room_id).toBe(created.room_id);

    const close = await SELF.fetch(
      `https://match.test/rooms/${created.room_id}/close`,
      { method: 'POST' },
    );
    expect(close.status).toBe(200);

    const afterClose = await SELF.fetch(
      `https://match.test/rooms/${created.room_id}`,
    );
    expect(afterClose.status).toBe(404);
  });

  test('不正な部屋 ID と作成ペイロードを拒否する', async () => {
    const invalidRoom = await SELF.fetch('https://match.test/rooms/bad room');
    expect(invalidRoom.status).toBe(400);

    const invalidCreate = await SELF.fetch('https://match.test/rooms', {
      body: JSON.stringify({
        host_name: '',
        settings: { ...gameSettings, wins: 99 },
      }),
      headers: { 'content-type': 'application/json' },
      method: 'POST',
    });
    expect(invalidCreate.status).toBe(400);
  });
});

describe('シグナリング WebSocket Durable Object', () => {
  test('有効な部屋トークンを必須にする', async () => {
    const created = await createRoom();
    const response = await SELF.fetch(
      sessionUrl(created.signal_url, created.room_id, 'offer', 'wrong-token'),
      {
        headers: { Upgrade: 'websocket' },
      },
    );
    expect(response.status).toBe(403);
    expect(await json(response)).toEqual({ error: 'invalid room token' });
  });

  test('両ピア接続後にメッセージを中継する', async () => {
    const created = await createRoom();
    const joined = await reserveJoin(created.room_id);
    const offer = await connectWebSocket(
      sessionUrl(
        created.signal_url,
        created.room_id,
        'offer',
        created.host_token,
      ),
    );
    const answer = await connectWebSocket(
      sessionUrl(
        joined.signal_url,
        joined.room_id,
        'answer',
        joined.join_token,
      ),
    );

    expect(await waitForMessage(offer, 'hello')).toMatchObject({
      iceServers: ['stun:stun.l.google.com:19302'],
      role: 'offer',
      settings: gameSettings,
      type: 'hello',
    });
    expect(await waitForMessage(answer, 'hello')).toMatchObject({
      role: 'answer',
      settings: gameSettings,
      type: 'hello',
    });
    expect(await waitForMessage(offer, 'ready-for-offer')).toMatchObject({
      peerCount: 2,
      type: 'ready-for-offer',
    });

    offer.send({ sdp: 'offer-sdp', sdpType: 'offer', type: 'sdp' });
    expect(await waitForMessage(answer, 'sdp')).toMatchObject({
      from: 'offer',
      sdp: 'offer-sdp',
      sdpType: 'offer',
      type: 'sdp',
    });

    answer.send({ type: 'ping' });
    expect(await waitForMessage(answer, 'pong')).toEqual({ type: 'pong' });

    offer.close();
    answer.close();
  });

  test('回答側ピアの参加まで早期オファーメッセージをキューする', async () => {
    const created = await createRoom();
    const joined = await reserveJoin(created.room_id);
    const offer = await connectWebSocket(
      sessionUrl(
        created.signal_url,
        created.room_id,
        'offer',
        created.host_token,
      ),
    );

    await waitForMessage(offer, 'hello');
    offer.send({ sdp: 'early-offer-sdp', sdpType: 'offer', type: 'sdp' });

    const answer = await connectWebSocket(
      sessionUrl(
        joined.signal_url,
        joined.room_id,
        'answer',
        joined.join_token,
      ),
    );

    await waitForMessage(answer, 'hello');
    expect(await waitForMessage(answer, 'sdp')).toMatchObject({
      from: 'offer',
      sdp: 'early-offer-sdp',
      type: 'sdp',
    });

    offer.close();
    answer.close();
  });
});
