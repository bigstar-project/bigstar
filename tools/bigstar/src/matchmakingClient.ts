import { hc } from 'hono/client';
import type { AppType } from '../../bigstar-signaling-server/src/app';
import type {
  HostRoomEventMessage,
  LobbyRoomsMessage,
  RomIdentity,
  RoomSummary,
} from '../../bigstar-signaling-server/src/schemas';
import type { GameSettings } from './types';

export type { RoomSummary };

type CreateRoomInput = {
  signalUrl: string;
  hostName: string;
  hostProfileId: string;
  settings: GameSettings;
  romIdentity: RomIdentity;
};

type JoinRoomInput = {
  signalUrl: string;
  roomId: string;
  playerName: string;
  playerProfileId: string;
  romPairId: string;
};

type LobbyRoomsHandlers = {
  onSnapshot: (rooms: RoomSummary[]) => void;
  onError?: (error: Error) => void;
  onClose?: () => void;
  onOpen?: () => void;
};

type HostRoomEventsHandlers = {
  onJoined: (room: RoomSummary) => void;
  onError?: (error: Error) => void;
  onClose?: () => void;
  onOpen?: () => void;
};

function apiBaseFromSignalUrl(signalUrl: string): string {
  const url = new URL(signalUrl);
  url.protocol = url.protocol === 'wss:' ? 'https:' : 'http:';
  url.pathname = '';
  url.search = '';
  url.hash = '';
  return url.toString();
}

function webSocketUrlFromSignalUrl(signalUrl: string): URL {
  const url = new URL(signalUrl);
  url.protocol =
    url.protocol === 'https:'
      ? 'wss:'
      : url.protocol === 'http:'
        ? 'ws:'
        : url.protocol;
  return url;
}

function signalUrlWithToken(signalUrl: string, token: string): string {
  const url = new URL(signalUrl);
  url.searchParams.set('token', token);
  return url.toString();
}

export function lobbyRoomsSubscribeUrl(signalUrl: string): string {
  const url = webSocketUrlFromSignalUrl(signalUrl);
  url.pathname = '/rooms/subscribe';
  url.search = '';
  url.hash = '';
  return url.toString();
}

export function hostRoomEventsUrl(signalUrl: string, roomId: string): string {
  const source = new URL(signalUrl);
  const token = source.searchParams.get('token');
  const url = webSocketUrlFromSignalUrl(signalUrl);
  url.pathname = `/rooms/${encodeURIComponent(roomId)}/events`;
  url.search = '';
  url.hash = '';
  if (token) {
    url.searchParams.set('token', token);
  }
  return url.toString();
}

function clientFor(signalUrl: string) {
  return hc<AppType>(apiBaseFromSignalUrl(signalUrl));
}

async function readError(response: Response): Promise<string> {
  try {
    const data = (await response.json()) as { error?: unknown };
    if (typeof data.error === 'string') {
      return data.error;
    }
  } catch {
    // Fall through to status text.
  }
  return response.statusText || `HTTP ${response.status}`;
}

export async function listRooms(signalUrl: string) {
  const response = await clientFor(signalUrl).rooms.$get();
  if (!response.ok) {
    throw new Error(await readError(response));
  }
  return response.json();
}

export function subscribeLobbyRooms(
  signalUrl: string,
  handlers: LobbyRoomsHandlers,
): () => void {
  const socket = new WebSocket(lobbyRoomsSubscribeUrl(signalUrl));
  socket.addEventListener('open', () => handlers.onOpen?.());
  socket.addEventListener('message', (event) => {
    try {
      const message = JSON.parse(String(event.data)) as LobbyRoomsMessage;
      if (message.type === 'rooms_snapshot' && Array.isArray(message.rooms)) {
        handlers.onSnapshot(message.rooms);
      }
    } catch (error) {
      handlers.onError?.(
        error instanceof Error ? error : new Error(String(error)),
      );
    }
  });
  socket.addEventListener('error', () => {
    handlers.onError?.(new Error('lobby websocket error'));
  });
  socket.addEventListener('close', () => handlers.onClose?.());
  return () => socket.close();
}

export function subscribeHostRoomEvents(
  signalUrl: string,
  roomId: string,
  handlers: HostRoomEventsHandlers,
): () => void {
  const socket = new WebSocket(hostRoomEventsUrl(signalUrl, roomId));
  socket.addEventListener('open', () => handlers.onOpen?.());
  socket.addEventListener('message', (event) => {
    try {
      const message = JSON.parse(String(event.data)) as HostRoomEventMessage;
      if (message.type === 'joined') {
        handlers.onJoined(message.room);
      }
    } catch (error) {
      handlers.onError?.(
        error instanceof Error ? error : new Error(String(error)),
      );
    }
  });
  socket.addEventListener('error', () => {
    handlers.onError?.(new Error('host room events websocket error'));
  });
  socket.addEventListener('close', () => handlers.onClose?.());
  return () => socket.close();
}

export async function getRoom(signalUrl: string, roomId: string) {
  const response = await clientFor(signalUrl).rooms[':roomId'].$get({
    param: { roomId },
  });
  if (!response.ok) {
    throw new Error(await readError(response));
  }
  return response.json();
}

export async function createRoom({
  hostName,
  hostProfileId,
  romIdentity,
  settings,
  signalUrl,
}: CreateRoomInput) {
  const response = await clientFor(signalUrl).rooms.$post({
    json: {
      host_name: hostName,
      host_player_profile_id: hostProfileId,
      rom_identity: romIdentity,
      settings,
    },
  });
  if (!response.ok) {
    throw new Error(await readError(response));
  }
  const data = await response.json();
  return {
    ...data,
    signal_url: signalUrlWithToken(data.signal_url, data.host_token),
  };
}

export async function joinRoom({
  playerName,
  playerProfileId,
  romPairId,
  roomId,
  signalUrl,
}: JoinRoomInput) {
  const response = await clientFor(signalUrl).rooms[':roomId'].join.$post({
    param: { roomId },
    json: {
      player_name: playerName,
      player_profile_id: playerProfileId,
      rom_pair_id: romPairId,
    },
  });
  if (!response.ok) {
    throw new Error(await readError(response));
  }
  const data = await response.json();
  return {
    ...data,
    signal_url: signalUrlWithToken(data.signal_url, data.join_token),
  };
}

export async function closeRoom(signalUrl: string, roomId: string) {
  const response = await clientFor(signalUrl).rooms[':roomId'].close.$post({
    param: { roomId },
  });
  if (!response.ok) {
    throw new Error(await readError(response));
  }
  return response.json();
}
