import alchemy from 'alchemy';
import { DurableObjectNamespace, Worker } from 'alchemy/cloudflare';
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

export const signaling = await Worker('signaling', {
  adopt: true,
  entrypoint: './src/index.ts',
  bindings: {
    SIGNALING_ROOM: signalingRoom,
    DEFAULT_ICE_SERVERS:
      process.env.DEFAULT_ICE_SERVERS ?? 'stun:stun.l.google.com:19302',
  },
  bundle: {
    minify: true,
    keepNames: false,
  },
  url: true,
});

console.log({ url: signaling.url });

await app.finalize();
