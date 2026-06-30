import { zValidator } from '@hono/zod-validator';
import { type Context, Hono } from 'hono';
import { cors } from 'hono/cors';
import { z } from 'zod';
import { type MatchmakingEnv, publicRoom } from './do-api';
import {
  createRoomRequestSchema,
  errorResponseSchema,
  joinRoomRequestSchema,
  listRoomsResponseSchema,
} from './schemas';

const ROOM_ID_BYTES = 9;
const TOKEN_BYTES = 24;
const ROOM_TTL_MS = 3 * 60 * 60 * 1000;
const MAX_LOG_ARCHIVE_BYTES = 1024 * 1024 * 1024;
const MAX_LOG_ARCHIVE_PART_BYTES = 64 * 1024 * 1024;
const VALID_ROOM_ID = /^[A-Za-z0-9_-]{8,64}$/;
const VALID_SESSION_ID = /^[A-Za-z0-9_-]{1,64}$/;
const VALID_R2_KEY = /^[A-Za-z0-9/_ .-]{1,512}$/;
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

function authError(c: Context<{ Bindings: MatchmakingEnv }>) {
  const token = c.env.LOG_UPLOAD_TOKEN?.trim();
  if (!token) {
    return null;
  }
  if (c.req.header('x-nsmb-mvl-log-upload-token') === token) {
    return null;
  }
  return c.json(
    errorResponseSchema.parse({ error: 'invalid upload token' }),
    403,
  );
}

const createLogArchiveUploadRequestSchema = z.object({
  file_name: z
    .string()
    .trim()
    .min(1)
    .max(160)
    .regex(/\.zip$/i),
  size: z.number().int().min(1).max(MAX_LOG_ARCHIVE_BYTES),
});

const completeLogArchiveUploadRequestSchema = z.object({
  key: z.string().regex(VALID_R2_KEY),
  parts: z
    .array(
      z.object({
        etag: z.string().min(1),
        partNumber: z.number().int().min(1).max(10_000),
      }),
    )
    .min(1)
    .max(10_000),
});

function sanitizeArchiveFileName(value: string): string {
  const base = value
    .replace(/[\\/:*?"<>|]+/g, '_')
    .replace(/\s+/g, ' ')
    .trim();
  return base.toLowerCase().endsWith('.zip') ? base : `${base}.zip`;
}

function newLogArchiveKey(fileName: string): string {
  const now = new Date();
  const date = now.toISOString().slice(0, 10);
  return `log-archives/${date}/${randomUrlToken(18)}-${sanitizeArchiveFileName(fileName)}`;
}

export const app = new Hono<{ Bindings: MatchmakingEnv }>();

app.use('*', async (c, next) => {
  const corsMiddleware = cors({
    origin: corsOrigins(c.env.CORS_ORIGINS),
    allowHeaders: ['Content-Type', 'x-nsmb-mvl-log-upload-token'],
    allowMethods: ['GET', 'POST', 'PUT', 'OPTIONS'],
    maxAge: 600,
  });
  return corsMiddleware(c, next);
});

function expectedWebSocket(request: Request) {
  return request.headers.get('Upgrade') === 'websocket';
}

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
  .get('/rooms/subscribe', async (c) => {
    if (!expectedWebSocket(c.req.raw)) {
      const { body, status } = error('expected websocket upgrade', 400);
      return c.json(body, status);
    }
    const lobby = c.env.LOBBY.get(c.env.LOBBY.idFromName('global'));
    return lobby.fetch(c.req.raw);
  })
  .get('/rooms', async (c) => {
    const lobby = c.env.LOBBY.get(c.env.LOBBY.idFromName('global'));
    const rooms = await lobby.listRooms(Date.now());
    return c.json(listRoomsResponseSchema.parse({ rooms }), 200);
  })
  .post(
    '/log-archives/uploads',
    zValidator('json', createLogArchiveUploadRequestSchema),
    async (c) => {
      const unauthorized = authError(c);
      if (unauthorized) {
        return unauthorized;
      }
      const body = c.req.valid('json');
      const key = newLogArchiveKey(body.file_name);
      const upload = await c.env.LOG_ARCHIVES.createMultipartUpload(key, {
        httpMetadata: {
          contentType: 'application/zip',
          contentDisposition: `attachment; filename="${sanitizeArchiveFileName(body.file_name)}"`,
        },
        customMetadata: {
          declaredSize: String(body.size),
        },
      });
      return c.json(
        {
          key,
          upload_id: upload.uploadId,
          max_size: MAX_LOG_ARCHIVE_BYTES,
          max_part_size: MAX_LOG_ARCHIVE_PART_BYTES,
        },
        201,
      );
    },
  )
  .put('/log-archives/uploads/:uploadId/parts/:partNumber', async (c) => {
    const unauthorized = authError(c);
    if (unauthorized) {
      return unauthorized;
    }
    const uploadId = c.req.param('uploadId');
    const partNumber = Number(c.req.param('partNumber'));
    const key = c.req.query('key');
    if (!key || !VALID_R2_KEY.test(key) || !Number.isInteger(partNumber)) {
      const { body, status } = error('invalid upload part request', 400);
      return c.json(body, status);
    }
    if (partNumber < 1 || partNumber > 10_000) {
      const { body, status } = error('invalid part number', 400);
      return c.json(body, status);
    }
    const contentLength = Number(c.req.header('content-length') ?? '0');
    if (
      !Number.isFinite(contentLength) ||
      contentLength <= 0 ||
      contentLength > MAX_LOG_ARCHIVE_PART_BYTES
    ) {
      const { body, status } = error('invalid upload part size', 400);
      return c.json(body, status);
    }
    if (!c.req.raw.body) {
      const { body, status } = error('missing upload part body', 400);
      return c.json(body, status);
    }
    const upload = c.env.LOG_ARCHIVES.resumeMultipartUpload(key, uploadId);
    const part = await upload.uploadPart(partNumber, await c.req.arrayBuffer());
    return c.json(part, 200);
  })
  .post(
    '/log-archives/uploads/:uploadId/complete',
    zValidator('json', completeLogArchiveUploadRequestSchema),
    async (c) => {
      const unauthorized = authError(c);
      if (unauthorized) {
        return unauthorized;
      }
      const uploadId = c.req.param('uploadId');
      const body = c.req.valid('json');
      const upload = c.env.LOG_ARCHIVES.resumeMultipartUpload(
        body.key,
        uploadId,
      );
      const object = await upload.complete(body.parts);
      return c.json(
        {
          key: object.key,
          etag: object.etag,
          size: object.size,
        },
        200,
      );
    },
  )
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
      host_player_profile_id: body.host_player_profile_id,
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
        ...(record.host_player_profile_id
          ? { host_player_profile_id: record.host_player_profile_id }
          : {}),
        signal_url: signalUrl(c.req.raw),
        settings: record.settings,
        rom_identity: record.rom_identity,
      },
      201,
    );
  })
  .get('/rooms/:roomId/events', async (c) => {
    const roomId = c.req.param('roomId');
    if (!VALID_ROOM_ID.test(roomId)) {
      const { body, status } = error('invalid room id', 400);
      return c.json(body, status);
    }
    if (!expectedWebSocket(c.req.raw)) {
      const { body, status } = error('expected websocket upgrade', 400);
      return c.json(body, status);
    }
    const room = c.env.SIGNALING_ROOM.get(
      c.env.SIGNALING_ROOM.idFromName(roomId),
    );
    return room.fetch(c.req.raw);
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
        const { body: errorBody, status } = error(
          'match identity mismatch',
          409,
        );
        return c.json(errorBody, status);
      }
      const record = await room.reserveJoin({
        join_token: joinToken,
        client_name: body.player_name,
        client_player_profile_id: body.player_profile_id,
        rom_pair_id: body.rom_pair_id,
        now,
      });
      const lobby = c.env.LOBBY.get(c.env.LOBBY.idFromName('global'));
      await lobby.upsertRoom(publicRoom(record));
      return c.json(
        {
          room_id: roomId,
          join_token: joinToken,
          ...(record.host_player_profile_id
            ? { host_player_profile_id: record.host_player_profile_id }
            : {}),
          ...(record.client_name ? { client_name: record.client_name } : {}),
          ...(record.client_player_profile_id
            ? { client_player_profile_id: record.client_player_profile_id }
            : {}),
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
    message === 'match identity mismatch'
  ) {
    const { body, status } = error(message, 409);
    return c.json(body, status);
  }
  const { body, status } = error(message, 500);
  return c.json(body, status);
});

export type AppType = typeof route;
