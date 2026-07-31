import { zValidator } from '@hono/zod-validator';
import { Hono } from 'hono';
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
const MAX_LOG_ARCHIVE_BYTES = 10 * 1024 * 1024;
const MAX_LOG_ARCHIVE_PART_BYTES = 5 * 1024 * 1024;
const FEEDBACK_RATE_LIMIT_WINDOW_MS = 60 * 60 * 1000;
const FEEDBACK_RATE_LIMIT_REQUESTS = 3;
const VALID_ROOM_ID = /^[A-Za-z0-9_-]{8,64}$/;
const VALID_SESSION_ID = /^[A-Za-z0-9_-]{1,64}$/;
const VALID_R2_KEY = /^[A-Za-z0-9/_ .-]{1,512}$/;
const DEFAULT_CORS_ORIGINS = [
  'http://127.0.0.1:1420',
  'http://localhost:1420',
  'http://127.0.0.1:1421',
  'http://localhost:1421',
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

const createLogArchiveUploadRequestSchema = z.object({
  file_name: z
    .string()
    .trim()
    .min(1)
    .max(160)
    .regex(/\.zip$/i),
  size: z.number().int().min(1).max(MAX_LOG_ARCHIVE_BYTES),
  category: z.enum([
    'gui',
    'connection',
    'performance',
    'desync',
    'crash',
    'update',
    'other',
  ]),
  description: z.string().trim().min(1).max(4000),
  app_version: z.string().trim().min(1).max(64),
  edition: z.enum(['public', 'insiders']),
  schema_version: z.literal(1),
});

const completeLogArchiveUploadRequestSchema = z.object({
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

const feedbackReportSchema = z.object({
  report_id: z.string().regex(VALID_SESSION_ID),
  archive_key: z.string().regex(VALID_R2_KEY),
  upload_id: z.string().min(1),
  upload_token_hash: z.string().length(64),
  status: z.enum(['uploading', 'complete']),
  created_at: z.string(),
  completed_at: z.string().optional(),
  category: createLogArchiveUploadRequestSchema.shape.category,
  description: createLogArchiveUploadRequestSchema.shape.description,
  app_version: createLogArchiveUploadRequestSchema.shape.app_version,
  edition: createLogArchiveUploadRequestSchema.shape.edition,
  schema_version: z.literal(1),
  declared_size: z.number().int().positive(),
  stored_size: z.number().int().nonnegative().optional(),
});

type FeedbackReport = z.infer<typeof feedbackReportSchema>;

const feedbackRequestsByClient = new Map<string, number[]>();

function sanitizeArchiveFileName(value: string): string {
  const base = value
    .replace(/[\\/:*?"<>|]+/g, '_')
    .replace(/\s+/g, ' ')
    .trim();
  return base.toLowerCase().endsWith('.zip') ? base : `${base}.zip`;
}

function feedbackPrefix(reportId: string): string {
  const now = new Date();
  const date = now.toISOString().slice(0, 10);
  return `feedback/${date}/${reportId}`;
}

function feedbackArchiveKey(reportId: string, fileName: string): string {
  return `${feedbackPrefix(reportId)}/${sanitizeArchiveFileName(fileName)}`;
}

function feedbackMetadataKey(reportId: string): string {
  return `feedback-reports/${reportId}.json`;
}

function clientRateLimitKey(request: Request): string {
  return (
    request.headers.get('CF-Connecting-IP') ??
    request.headers.get('x-forwarded-for')?.split(',')[0]?.trim() ??
    'unknown'
  );
}

function feedbackRateLimitExceeded(
  request: Request,
  now = Date.now(),
): boolean {
  const key = clientRateLimitKey(request);
  const recent = (feedbackRequestsByClient.get(key) ?? []).filter(
    (timestamp) => now - timestamp < FEEDBACK_RATE_LIMIT_WINDOW_MS,
  );
  if (recent.length >= FEEDBACK_RATE_LIMIT_REQUESTS) {
    feedbackRequestsByClient.set(key, recent);
    return true;
  }
  recent.push(now);
  feedbackRequestsByClient.set(key, recent);
  return false;
}

async function sha256Hex(value: string): Promise<string> {
  const digest = await crypto.subtle.digest(
    'SHA-256',
    new TextEncoder().encode(value),
  );
  return [...new Uint8Array(digest)]
    .map((byte) => byte.toString(16).padStart(2, '0'))
    .join('');
}

async function loadFeedbackReport(
  env: MatchmakingEnv,
  reportId: string,
): Promise<FeedbackReport | null> {
  const object = await env.LOG_ARCHIVES.get(feedbackMetadataKey(reportId));
  if (!object) return null;
  try {
    return feedbackReportSchema.parse(await object.json());
  } catch {
    return null;
  }
}

async function feedbackAuthorizationError(
  env: MatchmakingEnv,
  reportId: string,
  uploadId: string,
  token: string | undefined,
) {
  const report = await loadFeedbackReport(env, reportId);
  if (
    !report ||
    report.status !== 'uploading' ||
    report.upload_id !== uploadId ||
    !token ||
    (await sha256Hex(token)) !== report.upload_token_hash
  ) {
    return { error: 'invalid feedback upload authorization' } as const;
  }
  return report;
}

export const app = new Hono<{ Bindings: MatchmakingEnv }>();

app.use('*', async (c, next) => {
  const corsMiddleware = cors({
    origin: corsOrigins(c.env.CORS_ORIGINS),
    allowHeaders: ['Content-Type', 'x-bigstar-feedback-token'],
    allowMethods: ['GET', 'POST', 'PUT', 'DELETE', 'OPTIONS'],
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
    '/feedback/uploads',
    zValidator('json', createLogArchiveUploadRequestSchema),
    async (c) => {
      if (feedbackRateLimitExceeded(c.req.raw)) {
        return c.json(
          errorResponseSchema.parse({
            error: 'feedback upload rate limit exceeded',
          }),
          429,
        );
      }
      const body = c.req.valid('json');
      const reportId = randomUrlToken(18);
      const uploadToken = randomUrlToken(TOKEN_BYTES);
      const archiveKey = feedbackArchiveKey(reportId, body.file_name);
      const upload = await c.env.LOG_ARCHIVES.createMultipartUpload(
        archiveKey,
        {
          httpMetadata: {
            contentType: 'application/zip',
            contentDisposition: `attachment; filename="${sanitizeArchiveFileName(body.file_name)}"`,
          },
          customMetadata: {
            declaredSize: String(body.size),
            reportId,
          },
        },
      );
      const report: FeedbackReport = {
        report_id: reportId,
        archive_key: archiveKey,
        upload_id: upload.uploadId,
        upload_token_hash: await sha256Hex(uploadToken),
        status: 'uploading',
        created_at: new Date().toISOString(),
        category: body.category,
        description: body.description,
        app_version: body.app_version,
        edition: body.edition,
        schema_version: body.schema_version,
        declared_size: body.size,
      };
      await c.env.LOG_ARCHIVES.put(
        feedbackMetadataKey(reportId),
        JSON.stringify(report),
        { httpMetadata: { contentType: 'application/json' } },
      );
      return c.json(
        {
          report_id: reportId,
          upload_id: upload.uploadId,
          upload_token: uploadToken,
          max_size: MAX_LOG_ARCHIVE_BYTES,
          max_part_size: MAX_LOG_ARCHIVE_PART_BYTES,
        },
        201,
      );
    },
  )
  .put('/feedback/uploads/:reportId/:uploadId/parts/:partNumber', async (c) => {
    const reportId = c.req.param('reportId');
    const uploadId = c.req.param('uploadId');
    const partNumber = Number(c.req.param('partNumber'));
    if (!VALID_SESSION_ID.test(reportId) || !Number.isInteger(partNumber)) {
      const { body, status } = error('invalid upload part request', 400);
      return c.json(body, status);
    }
    const authorization = await feedbackAuthorizationError(
      c.env,
      reportId,
      uploadId,
      c.req.header('x-bigstar-feedback-token'),
    );
    if ('error' in authorization) {
      return c.json(errorResponseSchema.parse(authorization), 403);
    }
    if (partNumber < 1 || partNumber > 10_000) {
      const { body, status } = error('invalid part number', 400);
      return c.json(body, status);
    }
    const contentLength = Number(c.req.header('content-length') ?? '0');
    if (!Number.isFinite(contentLength) || contentLength <= 0) {
      const { body, status } = error('invalid upload part size', 400);
      return c.json(body, status);
    }
    const expectedPartCount = Math.ceil(
      authorization.declared_size / MAX_LOG_ARCHIVE_PART_BYTES,
    );
    const expectedPartSize =
      partNumber < expectedPartCount
        ? MAX_LOG_ARCHIVE_PART_BYTES
        : authorization.declared_size -
          MAX_LOG_ARCHIVE_PART_BYTES * (expectedPartCount - 1);
    if (partNumber > expectedPartCount || contentLength !== expectedPartSize) {
      const { body, status } = error(
        'upload part does not match declared archive size',
        400,
      );
      return c.json(body, status);
    }
    if (!c.req.raw.body) {
      const { body, status } = error('missing upload part body', 400);
      return c.json(body, status);
    }
    const partBody = await c.req.arrayBuffer();
    if (partBody.byteLength !== expectedPartSize) {
      const { body, status } = error(
        'upload part body does not match declared archive size',
        400,
      );
      return c.json(body, status);
    }
    const upload = c.env.LOG_ARCHIVES.resumeMultipartUpload(
      authorization.archive_key,
      uploadId,
    );
    const part = await upload.uploadPart(partNumber, partBody);
    return c.json(part, 200);
  })
  .post(
    '/feedback/uploads/:reportId/:uploadId/complete',
    zValidator('json', completeLogArchiveUploadRequestSchema),
    async (c) => {
      const reportId = c.req.param('reportId');
      const uploadId = c.req.param('uploadId');
      if (!VALID_SESSION_ID.test(reportId)) {
        const { body, status } = error('invalid feedback report', 400);
        return c.json(body, status);
      }
      const authorization = await feedbackAuthorizationError(
        c.env,
        reportId,
        uploadId,
        c.req.header('x-bigstar-feedback-token'),
      );
      if ('error' in authorization) {
        return c.json(errorResponseSchema.parse(authorization), 403);
      }
      const body = c.req.valid('json');
      const expectedPartCount = Math.ceil(
        authorization.declared_size / MAX_LOG_ARCHIVE_PART_BYTES,
      );
      const partNumbers = body.parts
        .map((part) => part.partNumber)
        .sort((left, right) => left - right);
      if (
        partNumbers.length !== expectedPartCount ||
        partNumbers.some((partNumber, index) => partNumber !== index + 1)
      ) {
        const { body: errorBody, status } = error(
          'upload parts do not match declared archive size',
          400,
        );
        return c.json(errorBody, status);
      }
      const upload = c.env.LOG_ARCHIVES.resumeMultipartUpload(
        authorization.archive_key,
        uploadId,
      );
      const orderedParts = [...body.parts].sort(
        (left, right) => left.partNumber - right.partNumber,
      );
      const object = await upload.complete(orderedParts);
      if (object.size !== authorization.declared_size) {
        await c.env.LOG_ARCHIVES.delete(authorization.archive_key);
        const { body: errorBody, status } = error(
          'uploaded archive size mismatch',
          400,
        );
        return c.json(errorBody, status);
      }
      const completeReport: FeedbackReport = {
        ...authorization,
        status: 'complete',
        completed_at: new Date().toISOString(),
        stored_size: object.size,
        upload_token_hash: '0'.repeat(64),
      };
      await c.env.LOG_ARCHIVES.put(
        feedbackMetadataKey(reportId),
        JSON.stringify(completeReport),
        { httpMetadata: { contentType: 'application/json' } },
      );
      if (c.env.FEEDBACK_WEBHOOK_URL?.trim()) {
        c.executionCtx.waitUntil(
          fetch(c.env.FEEDBACK_WEBHOOK_URL, {
            method: 'POST',
            headers: { 'content-type': 'application/json' },
            body: JSON.stringify({
              content: `Bigstar feedback ${reportId}: ${authorization.category} / ${authorization.edition} ${authorization.app_version}`,
            }),
          }).then(() => undefined),
        );
      }
      return c.json(
        {
          report_id: reportId,
          etag: object.etag,
          size: object.size,
        },
        200,
      );
    },
  )
  .delete('/feedback/uploads/:reportId/:uploadId', async (c) => {
    const reportId = c.req.param('reportId');
    const uploadId = c.req.param('uploadId');
    const authorization = await feedbackAuthorizationError(
      c.env,
      reportId,
      uploadId,
      c.req.header('x-bigstar-feedback-token'),
    );
    if ('error' in authorization) {
      return c.json(errorResponseSchema.parse(authorization), 403);
    }
    await c.env.LOG_ARCHIVES.resumeMultipartUpload(
      authorization.archive_key,
      uploadId,
    ).abort();
    await c.env.LOG_ARCHIVES.delete(feedbackMetadataKey(reportId));
    return c.body(null, 204);
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
