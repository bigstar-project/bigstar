import alchemy from 'alchemy';
import { DurableObjectNamespace, R2Bucket, Worker } from 'alchemy/cloudflare';
import { CloudflareStateStore } from 'alchemy/state';

const shouldUseStateStore =
  process.env.NODE_ENV === 'production' ||
  process.env.CI === 'true' ||
  process.env.GITHUB_ACTIONS === 'true' ||
  Boolean(process.env.ALCHEMY_STATE_TOKEN);

const app = await alchemy('nsmb-mvl-signaling', {
  password: process.env.ALCHEMY_PASSWORD,
  stateStore: shouldUseStateStore
    ? (scope) => new CloudflareStateStore(scope)
    : undefined,
});

const signalingRoom = DurableObjectNamespace('signaling-room', {
  className: 'SignalingRoom',
  sqlite: true,
});

const lobby = DurableObjectNamespace('lobby', {
  className: 'LobbyObject',
  sqlite: true,
});

const logArchives = await R2Bucket('log-archives', {
  lifecycle: [
    {
      id: 'delete-after-1-day',
      conditions: { prefix: '' },
      enabled: true,
      deleteObjectsTransition: {
        condition: { type: 'Age', maxAge: 24 * 60 * 60 },
      },
    },
  ],
});

const defaultCorsOrigins = [
  'http://127.0.0.1:1420',
  'http://localhost:1420',
  'http://tauri.localhost',
  'https://tauri.localhost',
  'tauri://localhost',
].join(',');

export const signaling = await Worker('signaling', {
  adopt: true,
  entrypoint: './src/index.ts',
  bindings: {
    SIGNALING_ROOM: signalingRoom,
    LOBBY: lobby,
    LOG_ARCHIVES: logArchives,
    DEFAULT_ICE_SERVERS:
      process.env.DEFAULT_ICE_SERVERS ?? 'stun:stun.l.google.com:19302',
    CORS_ORIGINS: process.env.CORS_ORIGINS ?? defaultCorsOrigins,
    LOG_UPLOAD_TOKEN: process.env.LOG_UPLOAD_TOKEN ?? '',
  },
  bundle: {
    minify: true,
    keepNames: false,
  },
  url: true,
});

console.log({ url: signaling.url });

await app.finalize();
