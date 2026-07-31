import alchemy from 'alchemy';
import { DurableObjectNamespace, R2Bucket, Worker } from 'alchemy/cloudflare';
import { CloudflareStateStore } from 'alchemy/state';

const shouldUseStateStore =
  process.env.NODE_ENV === 'production' ||
  process.env.CI === 'true' ||
  process.env.GITHUB_ACTIONS === 'true' ||
  Boolean(process.env.ALCHEMY_STATE_TOKEN);

const edition = process.env.BIGSTAR_EDITION ?? 'insiders';
if (edition !== 'insiders' && edition !== 'public') {
  throw new Error(`Unsupported BIGSTAR_EDITION: ${edition}`);
}

const appName =
  edition === 'insiders'
    ? 'bigstar-signaling-insiders'
    : 'bigstar-signaling-public';
const app = await alchemy(appName, {
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
        condition: { type: 'Age', maxAge: 14 * 24 * 60 * 60 },
      },
    },
  ],
});

const defaultCorsOrigins = [
  'http://127.0.0.1:1420',
  'http://localhost:1420',
  'http://127.0.0.1:1421',
  'http://localhost:1421',
  'http://tauri.localhost',
  'https://tauri.localhost',
  'tauri://localhost',
].join(',');
const feedbackWebhookUrl = process.env.FEEDBACK_WEBHOOK_URL;

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
    ...(feedbackWebhookUrl
      ? {
          FEEDBACK_WEBHOOK_URL: alchemy.secret(
            feedbackWebhookUrl,
            `${edition}-feedback-webhook-url`,
          ),
        }
      : {}),
    APP_EDITION: edition,
  },
  bundle: {
    minify: true,
    keepNames: false,
  },
  url: true,
});

console.log({ url: signaling.url });

await app.finalize();
