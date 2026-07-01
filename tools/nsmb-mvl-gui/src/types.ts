import type { CourseMode, Lives, Role } from './bindings';

export type {
  AiArtifact,
  AiReplayFrameRef,
  BridgeDiagnostics,
  CleanupDetailedLogsResponse,
  CourseMode,
  Defaults,
  GameSettings,
  GameStateMismatch,
  GenerateRomRequest,
  GenerateRomResponse,
  LaunchRequest,
  LaunchResponse,
  Lives,
  LogArchiveResponse,
  MatchHistoryRecord,
  MatchHistoryStatus,
  MatchPlayerNames,
  MvlPlayerResult,
  MvlStageResult,
  OpenAiReplayLogRequest,
  OpenAiReplayLogResponse,
  PreflightResponse,
  ReadAiReplayFrameRequest,
  ReadAiReplayFrameResponse,
  ReadAiTextFileRequest,
  ReadAiTextFileResponse,
  Role,
  RomIdentity,
  RunAiToolRequest,
  RunAiToolResponse,
  SaveDetailedLogsRequest,
  SaveDiagnosticEventsRequest,
  SaveNewRoomNotificationsRequest,
  SavePlayerNameRequest,
  SaveRomPathsRequest,
  SessionStatus,
  ShowNewRoomNotificationRequest,
  UploadLogArchiveRequest,
  UploadLogArchiveResponse,
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
  detailedLogsEnabled: boolean;
  newRoomNotificationsEnabled: boolean;
};
