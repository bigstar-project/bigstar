import { SELF } from 'cloudflare:test';
import { describe, expect, test } from 'vitest';
import type {
  HostRoomEventMessage,
  LobbyRoomsMessage,
  WsServerMessage,
} from '../src/schemas';

type ServerTestMessage =
  | HostRoomEventMessage
  | LobbyRoomsMessage
  | WsServerMessage;

const gameSettings = {
  big_stars: 5,
  course_mode: 'random',
  course_stages: [0, 1, 2],
  input_delay_frames: 4,
  input_max_frame_lead: 4,
  lives: 'endless',
  match_seed: '123',
  rng_seeds: ['123', '124', '125'],
  rollback_enabled: false,
  wins: 2,
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
  bridge_sha256:
    '5555555555555555555555555555555555555555555555555555555555555555',
};
const hostProfileId = '11111111-1111-4111-8111-111111111111';
const clientProfileId = '22222222-2222-4222-8222-222222222222';

async function json<T>(response: Response): Promise<T> {
  return response.json() as Promise<T>;
}

async function createRoom() {
  const response = await SELF.fetch('https://match.test/rooms', {
    body: JSON.stringify({
      host_name: 'Host Player',
      host_player_profile_id: hostProfileId,
      rom_identity: romIdentity,
      settings: gameSettings,
    }),
    headers: { 'content-type': 'application/json' },
    method: 'POST',
  });
  expect(response.status).toBe(201);
  return json<{
    host_player_profile_id?: string;
    host_token: string;
    room_id: string;
    signal_url: string;
  }>(response);
}

async function reserveJoin(roomId: string) {
  const response = await SELF.fetch(`https://match.test/rooms/${roomId}/join`, {
    body: JSON.stringify({
      player_name: 'Client Player',
      player_profile_id: clientProfileId,
      rom_pair_id: romIdentity.rom_pair_id,
    }),
    headers: { 'content-type': 'application/json' },
    method: 'POST',
  });
  expect(response.status).toBe(200);
  return json<{
    client_name?: string;
    client_player_profile_id?: string;
    host_player_profile_id?: string;
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

  const messages: ServerTestMessage[] = [];
  const waiters: Array<(message: ServerTestMessage) => void> = [];
  ws?.addEventListener('message', (event: MessageEvent) => {
    const message = JSON.parse(String(event.data)) as ServerTestMessage;
    messages.push(message);
    waiters.shift()?.(message);
  });

  return {
    close: () => ws?.close(),
    messages,
    nextMessage: () =>
      new Promise<ServerTestMessage>((resolve, reject) => {
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
  type: ServerTestMessage['type'],
) {
  for (let index = 0; index < 8; index += 1) {
    const message = await socket.nextMessage();
    if (message.type === type) {
      return message;
    }
  }
  throw new Error(`message ${type} was not received`);
}

async function waitForRoomsSnapshot(
  socket: Awaited<ReturnType<typeof connectWebSocket>>,
  predicate: (roomIds: string[]) => boolean,
) {
  for (let index = 0; index < 8; index += 1) {
    const message = await waitForMessage(socket, 'rooms_snapshot');
    if (message.type !== 'rooms_snapshot') {
      continue;
    }
    const roomIds = message.rooms.map((room) => room.room_id);
    if (predicate(roomIds)) {
      return message;
    }
  }
  throw new Error('matching rooms_snapshot was not received');
}

function hostEventsUrl(signalUrl: string, roomId: string, token: string) {
  const url = new URL(signalUrl);
  url.protocol = url.protocol === 'wss:' ? 'https:' : 'http:';
  url.pathname = `/rooms/${roomId}/events`;
  url.search = '';
  url.searchParams.set('token', token);
  return url.toString();
}

describe('マッチメイキング HTTP API', () => {
  test('部屋の作成、一覧取得、参加予約、クローズができる', async () => {
    const health = await SELF.fetch('https://match.test/health');
    expect(health.status).toBe(200);
    await expect(json(health)).resolves.toEqual({ ok: true });

    const created = await createRoom();
    expect(created.room_id).toMatch(/^[A-Za-z0-9_-]{8,64}$/);
    expect(created.host_token).toHaveLength(32);
    expect(created).toMatchObject({
      host_player_profile_id: hostProfileId,
    });
    expect(created.signal_url).toBe('wss://match.test/session');

    const listed = await SELF.fetch('https://match.test/rooms');
    expect(listed.status).toBe(200);
    expect(await json(listed)).toMatchObject({
      rooms: [
        {
          can_join: true,
          host_name: 'Host Player',
          host_player_profile_id: hostProfileId,
          rom_identity: romIdentity,
          room_id: created.room_id,
          settings: gameSettings,
          status: 'open',
        },
      ],
    });

    const joined = await reserveJoin(created.room_id);
    expect(joined.client_name).toBe('Client Player');
    expect(joined.client_player_profile_id).toBe(clientProfileId);
    expect(joined.host_player_profile_id).toBe(hostProfileId);
    expect(joined.join_token).toHaveLength(32);
    expect(joined.room_id).toBe(created.room_id);

    const afterJoin = await SELF.fetch(
      `https://match.test/rooms/${created.room_id}`,
    );
    expect(afterJoin.status).toBe(200);
    expect(await json(afterJoin)).toMatchObject({
      client_name: 'Client Player',
      client_player_profile_id: clientProfileId,
      host_player_profile_id: hostProfileId,
      room_id: created.room_id,
      status: 'joining',
    });

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

    const duplicateRandomStages = await SELF.fetch('https://match.test/rooms', {
      body: JSON.stringify({
        host_name: 'Host Player',
        rom_identity: romIdentity,
        settings: {
          ...gameSettings,
          course_mode: 'random',
          course_stages: [0, 1, 0],
        },
      }),
      headers: { 'content-type': 'application/json' },
      method: 'POST',
    });
    expect(duplicateRandomStages.status).toBe(400);
  });

  test('ROM ID が一致しない参加予約を拒否する', async () => {
    const created = await createRoom();
    const response = await SELF.fetch(
      `https://match.test/rooms/${created.room_id}/join`,
      {
        body: JSON.stringify({
          rom_pair_id:
            '5555555555555555555555555555555555555555555555555555555555555555',
        }),
        headers: { 'content-type': 'application/json' },
        method: 'POST',
      },
    );

    expect(response.status).toBe(409);
    expect(await json(response)).toEqual({ error: 'match identity mismatch' });
  });
});

describe('フィードバック upload API', () => {
  const feedback = {
    app_version: '0.10.1',
    category: 'connection',
    description: '接続中にタイムアウトしました',
    edition: 'public',
    file_name: 'feedback.zip',
    schema_version: 1,
    size: 3,
  };

  test('必須情報と10MB上限を検証して一時upload tokenを発行する', async () => {
    const missingDescription = await SELF.fetch(
      'https://match.test/feedback/uploads',
      {
        body: JSON.stringify({
          ...feedback,
          description: '',
        }),
        headers: { 'content-type': 'application/json' },
        method: 'POST',
      },
    );
    expect(missingDescription.status).toBe(400);

    const tooLarge = await SELF.fetch('https://match.test/feedback/uploads', {
      body: JSON.stringify({
        ...feedback,
        size: 10 * 1024 * 1024 + 1,
      }),
      headers: { 'content-type': 'application/json' },
      method: 'POST',
    });
    expect(tooLarge.status).toBe(400);

    const created = await SELF.fetch('https://match.test/feedback/uploads', {
      body: JSON.stringify(feedback),
      headers: { 'content-type': 'application/json' },
      method: 'POST',
    });
    expect(created.status).toBe(201);
    const createdBody = (await json(created)) as {
      report_id: string;
      upload_id: string;
      upload_token: string;
    };
    expect(createdBody).toMatchObject({
      max_part_size: 5 * 1024 * 1024,
      max_size: 10 * 1024 * 1024,
      report_id: expect.any(String),
      upload_id: expect.any(String),
      upload_token: expect.any(String),
    });

    const partUrl = `https://match.test/feedback/uploads/${createdBody.report_id}/${createdBody.upload_id}/parts/1`;
    const unauthorizedPart = await SELF.fetch(partUrl, {
      body: new Uint8Array([1, 2, 3]),
      headers: { 'x-bigstar-feedback-token': 'wrong-token' },
      method: 'PUT',
    });
    expect(unauthorizedPart.status).toBe(403);

    const uploadedPart = await SELF.fetch(partUrl, {
      body: new Uint8Array([1, 2, 3]),
      headers: {
        'content-length': '3',
        'x-bigstar-feedback-token': createdBody.upload_token,
      },
      method: 'PUT',
    });
    expect(uploadedPart.status).toBe(200);
    const part = await json(uploadedPart);

    const completed = await SELF.fetch(
      `https://match.test/feedback/uploads/${createdBody.report_id}/${createdBody.upload_id}/complete`,
      {
        body: JSON.stringify({ parts: [part] }),
        headers: {
          'content-type': 'application/json',
          'x-bigstar-feedback-token': createdBody.upload_token,
        },
        method: 'POST',
      },
    );
    expect(completed.status).toBe(200);
    expect(await json(completed)).toMatchObject({
      report_id: createdBody.report_id,
      size: 3,
    });
  });
});

describe('Lobby WebSocket API', () => {
  test('接続時snapshotと部屋更新snapshotをpushする', async () => {
    const created = await createRoom();
    const lobby = await connectWebSocket('https://match.test/rooms/subscribe');

    await waitForRoomsSnapshot(lobby, (roomIds) =>
      roomIds.includes(created.room_id),
    );

    const second = await createRoom();
    await waitForRoomsSnapshot(lobby, (roomIds) =>
      [created.room_id, second.room_id].every((roomId) =>
        roomIds.includes(roomId),
      ),
    );

    await reserveJoin(created.room_id);
    await waitForRoomsSnapshot(
      lobby,
      (roomIds) => !roomIds.includes(created.room_id),
    );

    const close = await SELF.fetch(
      `https://match.test/rooms/${second.room_id}/close`,
      { method: 'POST' },
    );
    expect(close.status).toBe(200);
    await waitForRoomsSnapshot(
      lobby,
      (roomIds) => !roomIds.includes(second.room_id),
    );

    lobby.close();
  });
});

describe('部屋主イベント WebSocket API', () => {
  test('参加予約が成功した瞬間に部屋主へjoinedイベントをpushする', async () => {
    const created = await createRoom();
    const events = await connectWebSocket(
      hostEventsUrl(created.signal_url, created.room_id, created.host_token),
    );

    await reserveJoin(created.room_id);
    const joined = await waitForMessage(events, 'joined');

    expect(joined).toMatchObject({
      room: {
        client_name: 'Client Player',
        client_player_profile_id: clientProfileId,
        room_id: created.room_id,
        status: 'joining',
      },
      type: 'joined',
    });

    events.close();
  });

  test('参加予約後に接続してもjoinedイベントを即時snapshotとして受け取る', async () => {
    const created = await createRoom();
    await reserveJoin(created.room_id);

    const events = await connectWebSocket(
      hostEventsUrl(created.signal_url, created.room_id, created.host_token),
    );
    const joined = await waitForMessage(events, 'joined');

    expect(joined).toMatchObject({
      room: {
        client_name: 'Client Player',
        room_id: created.room_id,
        status: 'joining',
      },
      type: 'joined',
    });

    events.close();
  });

  test('不正なhost tokenを拒否する', async () => {
    const created = await createRoom();
    const response = await SELF.fetch(
      hostEventsUrl(created.signal_url, created.room_id, 'wrong-token'),
      {
        headers: { Upgrade: 'websocket' },
      },
    );

    expect(response.status).toBe(403);
    expect(await json(response)).toEqual({ error: 'invalid room token' });
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
