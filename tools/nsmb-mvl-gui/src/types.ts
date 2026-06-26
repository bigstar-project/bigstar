import type { CourseMode, Lives, Role } from './bindings';

export type {
  BridgeDiagnostics,
  CourseMode,
  Defaults,
  GameSettings,
  GameStateMismatch,
  GenerateRomRequest,
  GenerateRomResponse,
  LaunchRequest,
  LaunchResponse,
  Lives,
  MatchHistoryRecord,
  MatchHistoryStatus,
  MatchPlayerNames,
  MvlPlayerResult,
  MvlStageResult,
  PreflightResponse,
  Role,
  RomIdentity,
  SaveDiagnosticEventsRequest,
  SaveNewRoomNotificationsRequest,
  SavePlayerNameRequest,
  SaveRomPathsRequest,
  SessionStatus,
} from './bindings';

export type StatusKind = 'idle' | 'ok' | 'warn' | 'error';

export type FormState = {
  role: Role;
  hostName: string;
  signalUrl: string;
  roomCode: string;
  port: number;
  hostRomPath: string;
  clientRomPath: string;
  baseRomPath: string;
  courseMode: CourseMode;
  courseStages: number[];
  wins: number;
  bigStars: number;
  lives: Lives;
  matchSeed: string;
  rngSeeds: string[];
  inputDelayFrames: number;
  inputMaxFrameLead: number;
  rollbackEnabled: boolean;
  diagnosticEventsEnabled: boolean;
  newRoomNotificationsEnabled: boolean;
};
