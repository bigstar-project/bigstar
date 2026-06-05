import { hc } from 'hono/client';
import type { AppType, RoomSummary } from './matchmakingApiContract';
import type { GameSettings } from './types';

export type { RoomSummary };

type CreateRoomInput = {
  signalUrl: string;
  hostName: string;
  settings: GameSettings;
};

type JoinRoomInput = {
  signalUrl: string;
  roomId: string;
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

export async function createRoom({
  hostName,
  settings,
  signalUrl,
}: CreateRoomInput) {
  const response = await clientFor(signalUrl).rooms.$post({
    json: {
      host_name: hostName,
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

export async function joinRoom({ roomId, signalUrl }: JoinRoomInput) {
  const response = await clientFor(signalUrl).rooms[':roomId'].join.$post({
    param: { roomId },
    json: {},
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
