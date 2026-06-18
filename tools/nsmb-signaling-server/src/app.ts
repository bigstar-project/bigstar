import { zValidator } from '@hono/zod-validator';
import { Hono } from 'hono';
import { cors } from 'hono/cors';
import { type MatchmakingEnv, publicRoom } from './do-api';
import {
  createRoomRequestSchema,
  errorResponseSchema,
  joinRoomRequestSchema,
  listRoomsResponseSchema,
} from './schemas';

const ROOM_ID_BYTES = 9;
const TOKEN_BYTES = 24;
const ROOM_TTL_MS = 10 * 60 * 1000;
const VALID_ROOM_ID = /^[A-Za-z0-9_-]{8,64}$/;
const VALID_SESSION_ID = /^[A-Za-z0-9_-]{1,64}$/;
const DEFAULT_CORS_ORIGINS = [
  'http://127.0.0.1:1420',
  'http://localhost:1420',
  'http://tauri.localhost',
  'https://tauri.localhost',
  'tauri://localhost',
];

function corsOrigins(value: string | undefined): string[] {
  if (!value?.trim()) {
    return DEFAULT_CORS_ORIGINS;
  }
  return value
    .split(',')
    .map((origin) => origin.trim())
    .filter(Boolean);
}

function randomUrlToken(bytes: number): string {
  const values = new Uint8Array(bytes);
  crypto.getRandomValues(values);
  return btoa(String.fromCharCode(...values))
    .replaceAll('+', '-')
    .replaceAll('/', '_')
    .replaceAll('=', '');
}

function newRoomId(): string {
  return randomUrlToken(ROOM_ID_BYTES);
}

function signalUrl(request: Request): string {
  const url = new URL(request.url);
  url.pathname = '/session';
  url.search = '';
  url.protocol = url.protocol === 'https:' ? 'wss:' : 'ws:';
  return url.toString();
}

function sessionId(request: Request): string | null {
  const url = new URL(request.url);
  return url.searchParams.get('room') ?? url.searchParams.get('session');
}

function error(error: string, status: 400 | 404 | 409 | 500) {
  return { body: errorResponseSchema.parse({ error }), status };
}

export const app = new Hono<{ Bindings: MatchmakingEnv }>();

app.use('*', async (c, next) => {
  const corsMiddleware = cors({
    origin: corsOrigins(c.env.CORS_ORIGINS),
    allowHeaders: ['Content-Type'],
    allowMethods: ['GET', 'POST', 'OPTIONS'],
    maxAge: 600,
  });
  return corsMiddleware(c, next);
});

app.get('/session', async (c) => {
  const session = sessionId(c.req.raw);
  if (!session || !VALID_SESSION_ID.test(session)) {
    const { body, status } = error('invalid session', 400);
    return c.json(body, status);
  }
  const room = c.env.SIGNALING_ROOM.get(
    c.env.SIGNALING_ROOM.idFromName(session),
  );
  return room.fetch(c.req.raw);
});

const route = app
  .get('/health', (c) => c.json({ ok: true }, 200))
  .get('/rooms', async (c) => {
    const lobby = c.env.LOBBY.get(c.env.LOBBY.idFromName('global'));
    const rooms = await lobby.listRooms(Date.now());
    return c.json(listRoomsResponseSchema.parse({ rooms }), 200);
  })
  .post('/rooms', zValidator('json', createRoomRequestSchema), async (c) => {
    const body = c.req.valid('json');
    const roomId = newRoomId();
    const hostToken = randomUrlToken(TOKEN_BYTES);
    const now = Date.now();
    const room = c.env.SIGNALING_ROOM.get(
      c.env.SIGNALING_ROOM.idFromName(roomId),
    );
    const record = await room.createRoom({
      room_id: roomId,
      host_name: body.host_name,
      host_token: hostToken,
      settings: body.settings,
      rom_identity: body.rom_identity,
      now,
      expires_at: now + ROOM_TTL_MS,
    });
    const lobby = c.env.LOBBY.get(c.env.LOBBY.idFromName('global'));
    await lobby.upsertRoom(publicRoom(record));
    return c.json(
      {
        room_id: roomId,
        host_token: hostToken,
        signal_url: signalUrl(c.req.raw),
        settings: record.settings,
        rom_identity: record.rom_identity,
      },
      201,
    );
  })
  .get('/rooms/:roomId', async (c) => {
    const roomId = c.req.param('roomId');
    if (!VALID_ROOM_ID.test(roomId)) {
      const { body, status } = error('invalid room id', 400);
      return c.json(body, status);
    }
    const room = c.env.SIGNALING_ROOM.get(
      c.env.SIGNALING_ROOM.idFromName(roomId),
    );
    const record = await room.getRoom(Date.now());
    if (record === null) {
      const { body, status } = error('room not found', 404);
      return c.json(body, status);
    }
    return c.json(publicRoom(record), 200);
  })
  .post(
    '/rooms/:roomId/join',
    zValidator('json', joinRoomRequestSchema),
    async (c) => {
      const body = c.req.valid('json');
      const roomId = c.req.param('roomId');
      if (!VALID_ROOM_ID.test(roomId)) {
        const { body, status } = error('invalid room id', 400);
        return c.json(body, status);
      }
      const joinToken = randomUrlToken(TOKEN_BYTES);
      const room = c.env.SIGNALING_ROOM.get(
        c.env.SIGNALING_ROOM.idFromName(roomId),
      );
      const now = Date.now();
      const current = await room.getRoom(now);
      if (current === null) {
        const { body, status } = error('room not found', 404);
        return c.json(body, status);
      }
      if (current.rom_identity.rom_pair_id !== body.rom_pair_id) {
        const { body: errorBody, status } = error('rom identity mismatch', 409);
        return c.json(errorBody, status);
      }
      const record = await room.reserveJoin({
        join_token: joinToken,
        rom_pair_id: body.rom_pair_id,
        now,
      });
      const lobby = c.env.LOBBY.get(c.env.LOBBY.idFromName('global'));
      await lobby.upsertRoom(publicRoom(record));
      return c.json(
        {
          room_id: roomId,
          join_token: joinToken,
          signal_url: signalUrl(c.req.raw),
          settings: record.settings,
          rom_identity: record.rom_identity,
        },
        200,
      );
    },
  )
  .post('/rooms/:roomId/close', async (c) => {
    const roomId = c.req.param('roomId');
    if (!VALID_ROOM_ID.test(roomId)) {
      const { body, status } = error('invalid room id', 400);
      return c.json(body, status);
    }
    const room = c.env.SIGNALING_ROOM.get(
      c.env.SIGNALING_ROOM.idFromName(roomId),
    );
    await room.closeRoom(Date.now());
    const lobby = c.env.LOBBY.get(c.env.LOBBY.idFromName('global'));
    await lobby.removeRoom(roomId);
    return c.json({ ok: true }, 200);
  });

app.onError((err, c) => {
  const message = err instanceof Error ? err.message : String(err);
  if (message === 'room not found') {
    const { body, status } = error(message, 404);
    return c.json(body, status);
  }
  if (
    message === 'room is not joinable' ||
    message === 'room already exists' ||
    message === 'rom identity mismatch'
  ) {
    const { body, status } = error(message, 409);
    return c.json(body, status);
  }
  const { body, status } = error(message, 500);
  return c.json(body, status);
});

export type AppType = typeof route;
