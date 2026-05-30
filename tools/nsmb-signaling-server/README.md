# NSMB MvL Signaling Server

Cloudflare Workers + Durable Objects signaling server for `nsmb-net-bridge`.

This service only exchanges WebRTC signaling messages. Game packets still flow
peer-to-peer through the WebRTC DataChannel.

## Commands

```powershell
corepack pnpm install
corepack pnpm typecheck
corepack pnpm format-and-lint:fix
corepack pnpm dev
```

Deployment is intentionally left to the repository owner:

```powershell
corepack pnpm deploy -- --stage prod
```

## WebSocket Endpoint

```text
wss://<worker-host>/session?session=<room_id>&role=offer
wss://<worker-host>/session?session=<room_id>&role=answer
```

`session` must match `^[A-Za-z0-9_-]{1,64}$`.

The Durable Object allows at most one `offer` client and one `answer` client per
session. It relays JSON messages with `type: "sdp"` and `type: "candidate"` to
the opposite role.

## Environment

- `ALCHEMY_PASSWORD`: used by Alchemy state handling.
- `ALCHEMY_STATE_TOKEN`: optional; enables Cloudflare-backed state store.
- `DEFAULT_ICE_SERVERS`: comma-separated STUN/TURN URI list returned to clients.
  Defaults to `stun:stun.l.google.com:19302`.
