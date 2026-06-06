import { DurableObject } from 'cloudflare:workers';
import { z } from 'zod';
import { app } from './app';
import {
  type CreateRoomParams,
  type LobbyObjectApi,
  type MatchmakingEnv,
  publicRoom,
  type ReserveJoinParams,
  type RoomObjectApi,
  type RoomRecord,
  roomRecordSchema,
} from './do-api';
import {
  type Role,
  type RoomSummary,
  roleSchema,
  roomSummarySchema,
  type WsClientMessage,
  wsClientMessageSchema,
} from './schemas';

type Attachment = {
  role: Role;
  session: string;
  token: string | null;
  joinedAt: number;
  legacy: boolean;
};

type RelaySignalMessage = Exclude<WsClientMessage, { type: 'ping' }> & {
  from: Role;
};

type QueuedSignalMessage = {
  message: RelaySignalMessage;
  targetRole: Role;
};

const DEFAULT_STUN_SERVER = 'stun:stun.l.google.com:19302';
const ROOM_KEY = 'room';
const LOBBY_ROOMS_KEY = 'rooms';
const QUEUED_SIGNALS_KEY = 'queued-signals';

const attachmentSchema = z.object({
  role: roleSchema,
  session: z.string(),
  token: z.string().nullable(),
  joinedAt: z.number(),
  legacy: z.boolean(),
});

function json(data: unknown, init?: ResponseInit): Response {
  return new Response(JSON.stringify(data), {
    ...init,
    headers: {
      'content-type': 'application/json; charset=utf-8',
      ...init?.headers,
    },
  });
}

function parseIceServers(value: string | undefined): string[] {
  const servers = value?.trim() ? value : DEFAULT_STUN_SERVER;
  return servers
    .split(',')
    .map((server) => server.trim())
    .filter(Boolean);
}

function peerRole(role: Role): Role {
  return role === 'offer' ? 'answer' : 'offer';
}

function send(ws: WebSocket, data: unknown): void {
  ws.send(JSON.stringify(data));
}

function getAttachment(ws: WebSocket): Attachment | null {
  const parsed = attachmentSchema.safeParse(ws.deserializeAttachment());
  return parsed.success ? parsed.data : null;
}

function peerCount(sockets: WebSocket[]): number {
  return sockets.filter((socket) => getAttachment(socket) !== null).length;
}

export default {
  fetch(request: Request, env: MatchmakingEnv, ctx: ExecutionContext) {
    return app.fetch(request, env, ctx);
  },
} satisfies ExportedHandler<MatchmakingEnv>;

export class SignalingRoom
  extends DurableObject<MatchmakingEnv>
  implements RoomObjectApi
{
  async createRoom(params: CreateRoomParams): Promise<RoomRecord> {
    const existing = await this.ctx.storage.get<RoomRecord>(ROOM_KEY);
    if (existing && existing.status !== 'closed') {
      throw new Error('room already exists');
    }
    const room = roomRecordSchema.parse({
      room_id: params.room_id,
      host_name: params.host_name,
      host_token: params.host_token,
      join_token: null,
      status: 'open',
      settings: params.settings,
      created_at: params.now,
      updated_at: params.now,
      expires_at: params.expires_at,
      can_join: true,
      peer_count: 0,
    });
    await this.ctx.storage.put(ROOM_KEY, room);
    return room;
  }

  async reserveJoin(params: ReserveJoinParams): Promise<RoomRecord> {
    const room = await this.requireRoom(params.now);
    if (room.status !== 'open') {
      throw new Error('room is not joinable');
    }
    const next = roomRecordSchema.parse({
      ...room,
      status: 'joining',
      join_token: params.join_token,
      updated_at: params.now,
      can_join: false,
    });
    await this.ctx.storage.put(ROOM_KEY, next);
    return next;
  }

  async getRoom(now: number): Promise<RoomRecord | null> {
    const room = await this.ctx.storage.get<RoomRecord>(ROOM_KEY);
    if (!room || room.status === 'closed' || room.expires_at <= now) {
      return null;
    }
    return room;
  }

  async closeRoom(now: number): Promise<RoomRecord | null> {
    const room = await this.ctx.storage.get<RoomRecord>(ROOM_KEY);
    if (!room) {
      return null;
    }
    const next = roomRecordSchema.parse({
      ...room,
      status: 'closed',
      updated_at: now,
      can_join: false,
    });
    await this.ctx.storage.put(ROOM_KEY, next);
    return next;
  }

  async fetch(request: Request): Promise<Response> {
    const url = new URL(request.url);
    if (request.headers.get('Upgrade') !== 'websocket') {
      return json({ error: 'expected websocket upgrade' }, { status: 426 });
    }

    const session =
      url.searchParams.get('room') ?? url.searchParams.get('session');
    const role = roleSchema.safeParse(url.searchParams.get('role'));
    const token = url.searchParams.get('token');
    if (!session || !role.success) {
      return json({ error: 'invalid session or role' }, { status: 400 });
    }

    const room = await this.ctx.storage.get<RoomRecord>(ROOM_KEY);
    const legacy = room === undefined;
    if (!legacy) {
      const expectedToken =
        role.data === 'offer' ? room.host_token : room.join_token;
      if (!expectedToken || token !== expectedToken) {
        return json({ error: 'invalid room token' }, { status: 403 });
      }
      if (room.status === 'closed' || room.expires_at <= Date.now()) {
        return json({ error: 'room is closed' }, { status: 410 });
      }
    }

    const existing = this.getSocketByRole(role.data);
    if (existing !== null) {
      return json(
        { error: `role ${role.data} is already connected` },
        { status: 409 },
      );
    }

    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair) as [WebSocket, WebSocket];
    this.ctx.acceptWebSocket(server);
    server.serializeAttachment({
      role: role.data,
      session,
      token,
      joinedAt: Date.now(),
      legacy,
    } satisfies Attachment);

    const sockets = this.ctx.getWebSockets();
    const count = peerCount(sockets);
    console.log('signaling join', {
      session,
      role: role.data,
      peerCount: count,
    });
    send(server, {
      type: 'hello',
      role: role.data,
      session,
      peerCount: count,
      iceServers: parseIceServers(this.env.DEFAULT_ICE_SERVERS),
      settings: room?.settings,
    });
    this.broadcast(
      {
        type: 'peer-joined',
        role: role.data,
        peerCount: count,
      },
      server,
    );

    if (this.getSocketByRole('offer') && this.getSocketByRole('answer')) {
      await this.markConnected();
      this.broadcast({ type: 'ready-for-offer', peerCount: 2 });
    }
    await this.flushQueuedSignals(role.data);

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
    let rawMessage: unknown;
    try {
      rawMessage = JSON.parse(message);
    } catch {
      send(ws, { type: 'error', error: 'invalid json' });
      return;
    }
    const parsed = wsClientMessageSchema.safeParse(rawMessage);
    if (!parsed.success) {
      send(ws, { type: 'error', error: 'unsupported message' });
      return;
    }
    if (parsed.data.type === 'ping') {
      send(ws, { type: 'pong' });
      return;
    }

    const targetRole = peerRole(attachment.role);
    const relay: RelaySignalMessage = {
      ...parsed.data,
      from: attachment.role,
    };
    const target = this.getSocketByRole(targetRole);
    if (target === null) {
      await this.queueSignal(targetRole, relay);
      console.log('signaling queued relay', {
        session: attachment.session,
        from: attachment.role,
        to: targetRole,
        type: parsed.data.type,
      });
      return;
    }
    console.log('signaling relay', {
      session: attachment.session,
      from: attachment.role,
      to: targetRole,
      type: parsed.data.type,
    });
    send(target, relay);
  }

  async webSocketClose(ws: WebSocket) {
    const attachment = getAttachment(ws);
    if (attachment === null) {
      return;
    }
    const count = peerCount(this.ctx.getWebSockets());
    console.log('signaling close', {
      session: attachment.session,
      role: attachment.role,
      peerCount: count,
    });
    this.broadcast(
      {
        type: 'peer-left',
        role: attachment.role,
        peerCount: count,
      },
      ws,
    );
    if (!attachment.legacy) {
      await this.closeRoom(Date.now());
      await this.env.LOBBY.get(this.env.LOBBY.idFromName('global')).removeRoom(
        attachment.session,
      );
    }
  }

  private async requireRoom(now: number): Promise<RoomRecord> {
    const room = await this.getRoom(now);
    if (room === null) {
      throw new Error('room not found');
    }
    return room;
  }

  private async markConnected(): Promise<void> {
    const room = await this.ctx.storage.get<RoomRecord>(ROOM_KEY);
    if (!room || room.status === 'connected') {
      return;
    }
    const next = roomRecordSchema.parse({
      ...room,
      status: 'connected',
      updated_at: Date.now(),
      can_join: false,
    });
    await this.ctx.storage.put(ROOM_KEY, next);
    await this.env.LOBBY.get(this.env.LOBBY.idFromName('global')).upsertRoom(
      publicRoom(next, 2),
    );
  }

  private getSocketByRole(role: Role): WebSocket | null {
    for (const socket of this.ctx.getWebSockets()) {
      const attachment = getAttachment(socket);
      if (attachment?.role === role) {
        return socket;
      }
    }
    return null;
  }

  private async queueSignal(
    targetRole: Role,
    message: RelaySignalMessage,
  ): Promise<void> {
    const queued =
      (await this.ctx.storage.get<QueuedSignalMessage[]>(QUEUED_SIGNALS_KEY)) ??
      [];
    queued.push({ message, targetRole });
    await this.ctx.storage.put(QUEUED_SIGNALS_KEY, queued);
  }

  private async flushQueuedSignals(role: Role): Promise<void> {
    const queued =
      (await this.ctx.storage.get<QueuedSignalMessage[]>(QUEUED_SIGNALS_KEY)) ??
      [];
    if (queued.length === 0) {
      return;
    }
    const target = this.getSocketByRole(role);
    if (target === null) {
      return;
    }
    const remaining: QueuedSignalMessage[] = [];
    for (const item of queued) {
      if (item.targetRole === role) {
        send(target, item.message);
      } else {
        remaining.push(item);
      }
    }
    await this.ctx.storage.put(QUEUED_SIGNALS_KEY, remaining);
  }

  private broadcast(data: unknown, except?: WebSocket): void {
    for (const socket of this.ctx.getWebSockets()) {
      if (socket !== except) {
        send(socket, data);
      }
    }
  }
}

export class LobbyObject
  extends DurableObject<MatchmakingEnv>
  implements LobbyObjectApi
{
  async listRooms(now: number): Promise<RoomSummary[]> {
    const rooms = await this.readRooms();
    const fresh = Object.fromEntries(
      Object.entries(rooms).filter(
        ([, room]) =>
          room.status !== 'closed' && room.expires_at > now && room.can_join,
      ),
    );
    if (Object.keys(fresh).length !== Object.keys(rooms).length) {
      await this.ctx.storage.put(LOBBY_ROOMS_KEY, fresh);
    }
    return Object.values(fresh).sort((a, b) => b.created_at - a.created_at);
  }

  async upsertRoom(room: RoomSummary): Promise<void> {
    const rooms = await this.readRooms();
    rooms[room.room_id] = roomSummarySchema.parse(room);
    await this.ctx.storage.put(LOBBY_ROOMS_KEY, rooms);
  }

  async removeRoom(roomId: string): Promise<void> {
    const rooms = await this.readRooms();
    delete rooms[roomId];
    await this.ctx.storage.put(LOBBY_ROOMS_KEY, rooms);
  }

  private async readRooms(): Promise<Record<string, RoomSummary>> {
    return (
      (await this.ctx.storage.get<Record<string, RoomSummary>>(
        LOBBY_ROOMS_KEY,
      )) ?? {}
    );
  }
}
