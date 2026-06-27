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
  hostRoomEventMessageSchema,
  lobbyRoomsMessageSchema,
  type Role,
  type RoomSummary,
  roleSchema,
  roomSummarySchema,
  type WsClientMessage,
  wsClientMessageSchema,
} from './schemas';

type Attachment = {
  type: 'signal';
  role: Role;
  session: string;
  token: string | null;
  joinedAt: number;
  legacy: boolean;
};

type HostEventsAttachment = {
  type: 'host-events';
  roomId: string;
  token: string;
  joinedAt: number;
};

type LobbyAttachment = {
  type: 'lobby';
  joinedAt: number;
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
  type: z.literal('signal'),
  role: roleSchema,
  session: z.string(),
  token: z.string().nullable(),
  joinedAt: z.number(),
  legacy: z.boolean(),
});

const hostEventsAttachmentSchema = z.object({
  type: z.literal('host-events'),
  roomId: z.string(),
  token: z.string(),
  joinedAt: z.number(),
});

const lobbyAttachmentSchema = z.object({
  type: z.literal('lobby'),
  joinedAt: z.number(),
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

function getHostEventsAttachment(ws: WebSocket): HostEventsAttachment | null {
  const parsed = hostEventsAttachmentSchema.safeParse(
    ws.deserializeAttachment(),
  );
  return parsed.success ? parsed.data : null;
}

function getLobbyAttachment(ws: WebSocket): LobbyAttachment | null {
  const parsed = lobbyAttachmentSchema.safeParse(ws.deserializeAttachment());
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
      ...(params.host_player_profile_id
        ? { host_player_profile_id: params.host_player_profile_id }
        : {}),
      host_token: params.host_token,
      join_token: null,
      status: 'open',
      settings: params.settings,
      rom_identity: params.rom_identity,
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
      ...(params.client_name ? { client_name: params.client_name } : {}),
      ...(params.client_player_profile_id
        ? { client_player_profile_id: params.client_player_profile_id }
        : {}),
      updated_at: params.now,
      can_join: false,
    });
    await this.ctx.storage.put(ROOM_KEY, next);
    this.notifyHostJoined(next);
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
    if (url.pathname.endsWith('/events')) {
      return this.fetchHostEvents(url);
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
      type: 'signal',
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
      if (getHostEventsAttachment(ws) !== null) {
        if (message === '{"type":"ping"}') {
          send(ws, { type: 'pong' });
        }
        return;
      }
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

  private async fetchHostEvents(url: URL): Promise<Response> {
    const token = url.searchParams.get('token');
    const room = await this.ctx.storage.get<RoomRecord>(ROOM_KEY);
    if (!room || room.status === 'closed' || room.expires_at <= Date.now()) {
      return json({ error: 'room is closed' }, { status: 410 });
    }
    if (!token || token !== room.host_token) {
      return json({ error: 'invalid room token' }, { status: 403 });
    }

    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair) as [WebSocket, WebSocket];
    this.ctx.acceptWebSocket(server);
    server.serializeAttachment({
      type: 'host-events',
      roomId: room.room_id,
      token,
      joinedAt: Date.now(),
    } satisfies HostEventsAttachment);

    if (room.status !== 'open') {
      send(
        server,
        hostRoomEventMessageSchema.parse({
          type: 'joined',
          room: publicRoom(room, peerCount(this.ctx.getWebSockets())),
        }),
      );
    }

    return new Response(null, { status: 101, webSocket: client });
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

  private notifyHostJoined(room: RoomRecord): void {
    const event = hostRoomEventMessageSchema.parse({
      type: 'joined',
      room: publicRoom(room, peerCount(this.ctx.getWebSockets())),
    });
    for (const socket of this.ctx.getWebSockets()) {
      const attachment = getHostEventsAttachment(socket);
      if (attachment?.roomId === room.room_id) {
        send(socket, event);
      }
    }
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
    const { changed, rooms } = await this.freshRooms(now);
    if (changed) {
      this.broadcastSnapshot(rooms);
    }
    return rooms;
  }

  async upsertRoom(room: RoomSummary): Promise<void> {
    const rooms = await this.readRooms();
    rooms[room.room_id] = roomSummarySchema.parse(room);
    await this.ctx.storage.put(LOBBY_ROOMS_KEY, rooms);
    this.broadcastSnapshot(this.sortedJoinableRooms(rooms, Date.now()));
  }

  async removeRoom(roomId: string): Promise<void> {
    const rooms = await this.readRooms();
    delete rooms[roomId];
    await this.ctx.storage.put(LOBBY_ROOMS_KEY, rooms);
    this.broadcastSnapshot(this.sortedJoinableRooms(rooms, Date.now()));
  }

  async fetch(request: Request): Promise<Response> {
    if (request.headers.get('Upgrade') !== 'websocket') {
      return json({ error: 'expected websocket upgrade' }, { status: 426 });
    }

    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair) as [WebSocket, WebSocket];
    this.ctx.acceptWebSocket(server);
    server.serializeAttachment({
      type: 'lobby',
      joinedAt: Date.now(),
    } satisfies LobbyAttachment);
    const { changed, rooms } = await this.freshRooms(Date.now());
    if (changed) {
      this.broadcastSnapshot(rooms);
    }
    send(
      server,
      lobbyRoomsMessageSchema.parse({
        type: 'rooms_snapshot',
        rooms,
      }),
    );
    return new Response(null, { status: 101, webSocket: client });
  }

  webSocketMessage(ws: WebSocket, message: string | ArrayBuffer): void {
    if (getLobbyAttachment(ws) === null) {
      ws.close(1011, 'missing websocket attachment');
      return;
    }
    if (message === '{"type":"ping"}') {
      send(ws, { type: 'pong' });
    }
  }

  webSocketClose(_ws: WebSocket): void {
    // Attachments are maintained by the runtime; no storage cleanup is needed.
  }

  private async readRooms(): Promise<Record<string, RoomSummary>> {
    return (
      (await this.ctx.storage.get<Record<string, RoomSummary>>(
        LOBBY_ROOMS_KEY,
      )) ?? {}
    );
  }

  private async freshRooms(
    now: number,
  ): Promise<{ changed: boolean; rooms: RoomSummary[] }> {
    const rooms = await this.readRooms();
    const fresh = Object.fromEntries(
      Object.entries(rooms).filter(
        ([, room]) =>
          room.status !== 'closed' && room.expires_at > now && room.can_join,
      ),
    );
    const changed = Object.keys(fresh).length !== Object.keys(rooms).length;
    if (changed) {
      await this.ctx.storage.put(LOBBY_ROOMS_KEY, fresh);
    }
    return {
      changed,
      rooms: this.sortedJoinableRooms(fresh, now),
    };
  }

  private sortedJoinableRooms(
    rooms: Record<string, RoomSummary>,
    now: number,
  ): RoomSummary[] {
    return Object.values(rooms)
      .filter(
        (room) =>
          room.status !== 'closed' && room.expires_at > now && room.can_join,
      )
      .sort((a, b) => b.created_at - a.created_at);
  }

  private broadcastSnapshot(rooms: RoomSummary[]): void {
    const message = lobbyRoomsMessageSchema.parse({
      type: 'rooms_snapshot',
      rooms,
    });
    for (const socket of this.ctx.getWebSockets()) {
      if (getLobbyAttachment(socket) !== null) {
        send(socket, message);
      }
    }
  }
}
