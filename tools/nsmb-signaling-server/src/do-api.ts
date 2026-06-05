import { z } from 'zod';
import {
  type gameSettingsSchema,
  type RoomSummary,
  roomSummarySchema,
} from './schemas';

export const roomRecordSchema = roomSummarySchema.extend({
  host_token: z.string(),
  join_token: z.string().nullable(),
});

export type RoomRecord = z.infer<typeof roomRecordSchema>;

export type CreateRoomParams = {
  room_id: string;
  host_name: string;
  host_token: string;
  settings: z.infer<typeof gameSettingsSchema>;
  now: number;
  expires_at: number;
};

export type ReserveJoinParams = {
  join_token: string;
  now: number;
};

export type RoomObjectApi = {
  createRoom(params: CreateRoomParams): Promise<RoomRecord>;
  reserveJoin(params: ReserveJoinParams): Promise<RoomRecord>;
  getRoom(now: number): Promise<RoomRecord | null>;
  closeRoom(now: number): Promise<RoomRecord | null>;
};

export type LobbyObjectApi = {
  listRooms(now: number): Promise<RoomSummary[]>;
  upsertRoom(room: RoomSummary): Promise<void>;
  removeRoom(roomId: string): Promise<void>;
};

type TypedDurableObjectNamespace<T> = {
  idFromName(name: string): unknown;
  get(id: unknown): T;
};

export type MatchmakingEnv = {
  SIGNALING_ROOM: TypedDurableObjectNamespace<RoomObjectApi>;
  LOBBY: TypedDurableObjectNamespace<LobbyObjectApi>;
  DEFAULT_ICE_SERVERS?: string;
  CORS_ORIGINS?: string;
};

export function publicRoom(record: RoomRecord, peerCount = 0): RoomSummary {
  return {
    room_id: record.room_id,
    host_name: record.host_name,
    status: record.status,
    settings: record.settings,
    created_at: record.created_at,
    updated_at: record.updated_at,
    expires_at: record.expires_at,
    can_join: record.status === 'open',
    peer_count: peerCount,
  };
}
