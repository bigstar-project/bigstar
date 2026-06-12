import type { CourseMode, Lives, Role } from './bindings';

export type {
  AiArtifact,
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
  ReadAiTextFileRequest,
  ReadAiTextFileResponse,
  Role,
  RunAiToolRequest,
  RunAiToolResponse,
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
};
