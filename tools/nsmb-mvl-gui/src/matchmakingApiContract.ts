import type { Hono } from 'hono';
import type { GameSettings } from './types';

type JsonEndpoint<Input, Output, Status extends number = 200> = {
  input: Input;
  output: Output;
  outputFormat: 'json';
  status: Status;
};

type Empty = Record<never, never>;

export type RoomSummary = {
  room_id: string;
  host_name: string;
  settings: GameSettings;
  status: 'open' | 'closed';
  can_join: boolean;
  created_at: number;
  expires_at: number;
};

type CreateRoomRequest = {
  host_name: string;
  settings: GameSettings;
};

type CreateRoomResponse = {
  room_id: string;
  host_token: string;
  signal_url: string;
  settings: GameSettings;
};

type JoinRoomResponse = {
  room_id: string;
  join_token: string;
  signal_url: string;
  settings: GameSettings;
};

type AppSchema = {
  '/health': {
    $get: JsonEndpoint<Empty, { ok: true }>;
  };
  '/rooms': {
    $get: JsonEndpoint<Empty, { rooms: RoomSummary[] }>;
    $post: JsonEndpoint<{ json: CreateRoomRequest }, CreateRoomResponse, 201>;
  };
  '/rooms/:roomId': {
    $get: JsonEndpoint<{ param: { roomId: string } }, RoomSummary>;
  };
  '/rooms/:roomId/join': {
    $post: JsonEndpoint<
      { param: { roomId: string }; json: Empty },
      JoinRoomResponse
    >;
  };
  '/rooms/:roomId/close': {
    $post: JsonEndpoint<{ param: { roomId: string } }, { ok: true }>;
  };
};

export type AppType = Hono<Empty, AppSchema, '/'>;
