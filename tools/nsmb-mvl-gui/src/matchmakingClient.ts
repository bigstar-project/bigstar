import { hc } from 'hono/client';
import type { AppType } from '../../nsmb-signaling-server/src/app';
import type {
  RomIdentity,
  RoomSummary,
} from '../../nsmb-signaling-server/src/schemas';
import type { GameSettings } from './types';

export type { RoomSummary };

type CreateRoomInput = {
  signalUrl: string;
  hostName: string;
  settings: GameSettings;
  romIdentity: RomIdentity;
};

type JoinRoomInput = {
  signalUrl: string;
  roomId: string;
  playerName: string;
  romPairId: string;
};

function apiBaseFromSignalUrl(signalUrl: string): string {
  const url = new URL(signalUrl);
  url.protocol = url.protocol === 'wss:' ? 'https:' : 'http:';
  url.pathname = '';
  url.search = '';
  url.hash = '';
  return url.toString();
}

function signalUrlWithToken(signalUrl: string, token: string): string {
  const url = new URL(signalUrl);
  url.searchParams.set('token', token);
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
  romIdentity,
  settings,
  signalUrl,
}: CreateRoomInput) {
  const response = await clientFor(signalUrl).rooms.$post({
    json: {
      host_name: hostName,
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
  romPairId,
  roomId,
  signalUrl,
}: JoinRoomInput) {
  const response = await clientFor(signalUrl).rooms[':roomId'].join.$post({
    param: { roomId },
    json: { player_name: playerName, rom_pair_id: romPairId },
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
