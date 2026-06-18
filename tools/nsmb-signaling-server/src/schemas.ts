import { z } from 'zod';

export const roleSchema = z.enum(['offer', 'answer']);

export const courseModeSchema = z.enum(['random', 'select']);
export const livesSchema = z.enum(['3', '5', 'endless']);
const sha256Schema = z.string().regex(/^[0-9a-f]{64}$/i);

export const gameSettingsSchema = z
  .object({
    course_mode: courseModeSchema,
    course_stages: z.array(z.number().int().min(0).max(4)).min(1).max(5),
    wins: z.number().int().min(1).max(3),
    big_stars: z
      .number()
      .int()
      .refine((value) => value === 3 || value === 5 || value === 10),
    lives: livesSchema,
    match_seed: z.string().regex(/^(0x[0-9a-f]+|[0-9]+)$/i),
    rng_seeds: z
      .array(z.string().regex(/^(0x[0-9a-f]+|[0-9]+)$/i))
      .min(1)
      .max(5),
    input_delay_frames: z.number().int().min(0).max(16).default(4),
    input_max_frame_lead: z.number().int().min(0).max(16).default(4),
    rollback_enabled: z.boolean().default(false),
  })
  .superRefine((settings, ctx) => {
    const maxGames = settings.wins * 2 - 1;
    if (settings.course_stages.length !== maxGames) {
      ctx.addIssue({
        code: z.ZodIssueCode.custom,
        message: `course_stages must contain ${maxGames} entries`,
        path: ['course_stages'],
      });
    }
    if (
      settings.course_mode === 'random' &&
      new Set(settings.course_stages).size !== settings.course_stages.length
    ) {
      ctx.addIssue({
        code: z.ZodIssueCode.custom,
        message: 'random course_stages must not contain duplicates',
        path: ['course_stages'],
      });
    }
    if (settings.rng_seeds.length !== maxGames) {
      ctx.addIssue({
        code: z.ZodIssueCode.custom,
        message: `rng_seeds must contain ${maxGames} entries`,
        path: ['rng_seeds'],
      });
    }
    if (settings.rng_seeds[0] !== settings.match_seed) {
      ctx.addIssue({
        code: z.ZodIssueCode.custom,
        message: 'match_seed must match rng_seeds[0]',
        path: ['match_seed'],
      });
    }
  });

export const roomStatusSchema = z.enum([
  'open',
  'joining',
  'connected',
  'closed',
]);

export const romIdentitySchema = z.object({
  rom_pair_id: sha256Schema,
  generator_id: sha256Schema,
  host_rom_sha256: sha256Schema,
  client_rom_sha256: sha256Schema,
});

export const roomSummarySchema = z.object({
  room_id: z.string(),
  host_name: z.string(),
  status: roomStatusSchema,
  settings: gameSettingsSchema,
  rom_identity: romIdentitySchema,
  created_at: z.number().int(),
  updated_at: z.number().int(),
  expires_at: z.number().int(),
  can_join: z.boolean(),
  peer_count: z.number().int().min(0).max(2),
});

export const createRoomRequestSchema = z.object({
  host_name: z.string().trim().min(1).max(32),
  settings: gameSettingsSchema,
  rom_identity: romIdentitySchema,
});

export const createRoomResponseSchema = z.object({
  room_id: z.string(),
  host_token: z.string(),
  signal_url: z.string(),
  settings: gameSettingsSchema,
  rom_identity: romIdentitySchema,
});

export const joinRoomRequestSchema = z.object({
  player_name: z.string().trim().min(1).max(32).optional(),
  rom_pair_id: sha256Schema,
});

export const joinRoomResponseSchema = z.object({
  room_id: z.string(),
  join_token: z.string(),
  signal_url: z.string(),
  settings: gameSettingsSchema,
  rom_identity: romIdentitySchema,
});

export const listRoomsResponseSchema = z.object({
  rooms: z.array(roomSummarySchema),
});

export const errorResponseSchema = z.object({
  error: z.string(),
});

export const wsClientMessageSchema = z.discriminatedUnion('type', [
  z.object({
    type: z.literal('sdp'),
    sdpType: z.enum(['offer', 'answer']),
    sdp: z.string(),
  }),
  z.object({
    type: z.literal('candidate'),
    candidate: z.unknown(),
  }),
  z.object({
    type: z.literal('ping'),
  }),
]);

export const wsServerMessageSchema = z.discriminatedUnion('type', [
  z.object({
    type: z.literal('hello'),
    role: roleSchema,
    session: z.string(),
    peerCount: z.number().int(),
    iceServers: z.array(z.string()),
    settings: gameSettingsSchema.optional(),
  }),
  z.object({
    type: z.literal('ready-for-offer'),
    peerCount: z.number().int(),
  }),
  z.object({
    type: z.literal('peer-joined'),
    role: roleSchema,
    peerCount: z.number().int(),
  }),
  z.object({
    type: z.literal('peer-left'),
    role: roleSchema,
    peerCount: z.number().int(),
  }),
  z.object({
    type: z.literal('pong'),
  }),
  z.object({
    type: z.literal('error'),
    error: z.string(),
  }),
  z.object({
    type: z.literal('sdp'),
    from: roleSchema,
    sdpType: z.enum(['offer', 'answer']),
    sdp: z.string(),
  }),
  z.object({
    type: z.literal('candidate'),
    from: roleSchema,
    candidate: z.unknown(),
  }),
]);

export type Role = z.infer<typeof roleSchema>;
export type GameSettings = z.infer<typeof gameSettingsSchema>;
export type RomIdentity = z.infer<typeof romIdentitySchema>;
export type RoomStatus = z.infer<typeof roomStatusSchema>;
export type RoomSummary = z.infer<typeof roomSummarySchema>;
export type CreateRoomRequest = z.infer<typeof createRoomRequestSchema>;
export type CreateRoomResponse = z.infer<typeof createRoomResponseSchema>;
export type JoinRoomResponse = z.infer<typeof joinRoomResponseSchema>;
export type WsClientMessage = z.infer<typeof wsClientMessageSchema>;
export type WsServerMessage = z.infer<typeof wsServerMessageSchema>;
