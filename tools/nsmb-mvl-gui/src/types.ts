import type { CourseMode, Lives, Role } from './bindings';

export type {
  BridgeDiagnostics,
  CourseMode,
  Defaults,
  GameSettings,
  GenerateRomRequest,
  GenerateRomResponse,
  LaunchRequest,
  LaunchResponse,
  Lives,
  PreflightResponse,
  Role,
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
  wins: number;
  bigStars: number;
  lives: Lives;
  matchSeed: string;
};
