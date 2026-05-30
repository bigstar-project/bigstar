import { DurableObject } from 'cloudflare:workers';

type Role = 'offer' | 'answer';

type Attachment = {
  role: Role;
  session: string;
  joinedAt: number;
};

type Env = {
  SIGNALING_ROOM: DurableObjectNamespace;
  DEFAULT_ICE_SERVERS?: string;
};

type SignalMessage =
  | {
      type: 'sdp';
      sdpType: 'offer' | 'answer';
      sdp: string;
    }
  | {
      type: 'candidate';
      candidate: unknown;
    }
  | {
      type: 'ping';
    };

const VALID_SESSION = /^[A-Za-z0-9_-]{1,64}$/;
const VALID_ROLES = new Set<Role>(['offer', 'answer']);

function json(data: unknown, init?: ResponseInit): Response {
  return new Response(JSON.stringify(data), {
    ...init,
    headers: {
      'content-type': 'application/json; charset=utf-8',
      ...init?.headers,
    },
  });
}

function parseRole(value: string | null): Role | null {
  if (value === null) {
    return null;
  }
  return VALID_ROLES.has(value as Role) ? (value as Role) : null;
}

function parseIceServers(value: string | undefined): string[] {
  return (value ?? '')
    .split(',')
    .map((server) => server.trim())
    .filter(Boolean);
}

function peerRole(role: Role): Role {
  return role === 'offer' ? 'answer' : 'offer';
}

function getAttachment(ws: WebSocket): Attachment | null {
  const attachment = ws.deserializeAttachment();
  if (
    typeof attachment !== 'object' ||
    attachment === null ||
    !('role' in attachment) ||
    !('session' in attachment)
  ) {
    return null;
  }
  return attachment as Attachment;
}

function send(ws: WebSocket, data: unknown): void {
  ws.send(JSON.stringify(data));
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);

    if (url.pathname === '/health') {
      return json({ ok: true });
    }

    if (request.headers.get('Upgrade') !== 'websocket') {
      return json(
        {
          error: 'expected websocket upgrade',
          example: '/session?session=ROOM_ID&role=offer',
        },
        { status: 426 },
      );
    }

    const session = url.searchParams.get('session');
    const role = parseRole(url.searchParams.get('role'));
    if (session === null || !VALID_SESSION.test(session)) {
      return json(
        { error: 'session must match ^[A-Za-z0-9_-]{1,64}$' },
        { status: 400 },
      );
    }
    if (role === null) {
      return json({ error: 'role must be offer or answer' }, { status: 400 });
    }

    const id = env.SIGNALING_ROOM.idFromName(session);
    const room = env.SIGNALING_ROOM.get(id);
    return room.fetch(request);
  },
};

export class SignalingRoom extends DurableObject<Env> {
  async fetch(request: Request): Promise<Response> {
    const url = new URL(request.url);
    const session = url.searchParams.get('session');
    const role = parseRole(url.searchParams.get('role'));

    if (request.headers.get('Upgrade') !== 'websocket') {
      return json({ error: 'expected websocket upgrade' }, { status: 426 });
    }
    if (session === null || !VALID_SESSION.test(session) || role === null) {
      return json({ error: 'invalid session or role' }, { status: 400 });
    }

    const existing = this.getSocketByRole(role);
    if (existing !== null) {
      return json(
        { error: `role ${role} is already connected` },
        { status: 409 },
      );
    }

    const webSocketPair = new WebSocketPair();
    const [client, server] = Object.values(webSocketPair) as [
      WebSocket,
      WebSocket,
    ];

    this.ctx.acceptWebSocket(server);
    server.serializeAttachment({
      role,
      session,
      joinedAt: Date.now(),
    } satisfies Attachment);

    send(server, {
      type: 'hello',
      role,
      session,
      peerCount: this.getSockets().length,
      iceServers: parseIceServers(this.env.DEFAULT_ICE_SERVERS),
    });
    this.broadcast(
      {
        type: 'peer-joined',
        role,
        peerCount: this.getSockets().length,
      },
      server,
    );

    return new Response(null, { status: 101, webSocket: client });
  }

  async webSocketMessage(ws: WebSocket, message: string | ArrayBuffer) {
    const attachment = getAttachment(ws);
    if (attachment === null) {
      ws.close(1011, 'missing websocket attachment');
      return;
    }

    if (typeof message !== 'string') {
      send(ws, { type: 'error', error: 'binary messages are not supported' });
      return;
    }

    const parsed = this.parseMessage(ws, message);
    if (parsed === null) {
      return;
    }

    if (parsed.type === 'ping') {
      send(ws, { type: 'pong' });
      return;
    }

    const target = this.getSocketByRole(peerRole(attachment.role));
    if (target === null) {
      send(ws, { type: 'error', error: 'peer is not connected' });
      return;
    }

    send(target, {
      ...parsed,
      from: attachment.role,
    });
  }

  async webSocketClose(ws: WebSocket) {
    const attachment = getAttachment(ws);
    if (attachment === null) {
      return;
    }
    this.broadcast(
      {
        type: 'peer-left',
        role: attachment.role,
        peerCount: this.getSockets().length,
      },
      ws,
    );
  }

  private parseMessage(ws: WebSocket, message: string): SignalMessage | null {
    let parsed: unknown;
    try {
      parsed = JSON.parse(message);
    } catch {
      send(ws, { type: 'error', error: 'invalid json' });
      return null;
    }

    if (typeof parsed !== 'object' || parsed === null || !('type' in parsed)) {
      send(ws, { type: 'error', error: 'message type is required' });
      return null;
    }

    const value = parsed as Partial<SignalMessage>;
    if (value.type === 'ping') {
      return { type: 'ping' };
    }
    if (
      value.type === 'sdp' &&
      (value.sdpType === 'offer' || value.sdpType === 'answer') &&
      typeof value.sdp === 'string'
    ) {
      return {
        type: 'sdp',
        sdpType: value.sdpType,
        sdp: value.sdp,
      };
    }
    if (value.type === 'candidate' && 'candidate' in value) {
      return {
        type: 'candidate',
        candidate: value.candidate,
      };
    }

    send(ws, { type: 'error', error: 'unsupported message' });
    return null;
  }

  private getSockets(): WebSocket[] {
    return this.ctx.getWebSockets();
  }

  private getSocketByRole(role: Role): WebSocket | null {
    for (const socket of this.getSockets()) {
      const attachment = getAttachment(socket);
      if (attachment?.role === role) {
        return socket;
      }
    }
    return null;
  }

  private broadcast(data: unknown, except?: WebSocket): void {
    for (const socket of this.getSockets()) {
      if (socket !== except) {
        send(socket, data);
      }
    }
  }
}
