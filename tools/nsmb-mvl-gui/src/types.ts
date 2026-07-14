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
  MatchHistoryCursor,
  MatchHistoryDashboard,
  MatchHistoryFilter,
  MatchHistoryOpponent,
  MatchHistoryOutcome,
  MatchHistoryPage,
  MatchHistoryPageRequest,
  MatchHistoryRecord,
  MatchHistoryStageStatistics,
  MatchHistoryStatus,
  MatchHistoryStreakKind,
  MatchHistorySummary,
  MatchHistoryTrendPoint,
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
  SaveAiPlayLogRequest,
  SaveDetailedLogsRequest,
  SaveDiagnosticEventsRequest,
  SaveNewRoomNotificationsRequest,
  SavePerformanceLogsRequest,
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
  aiPlayLogEnabled: boolean;
  performanceLogsEnabled: boolean;
  newRoomNotificationsEnabled: boolean;
};
