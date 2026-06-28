import type { RoomSummary } from '../matchmakingClient';
import type {
  BridgeDiagnostics,
  FormState,
  GameStateMismatch,
  MatchHistoryRecord,
} from '../types';

export type View = 'battle' | 'history' | 'settings';

export type UpdateStatus = {
  phase:
    | 'idle'
    | 'checking'
    | 'available'
    | 'downloading'
    | 'installed'
    | 'none'
    | 'error';
  version?: string;
};

export function isUpdateRequired(updateStatus: UpdateStatus) {
  return (
    updateStatus.phase === 'available' ||
    updateStatus.phase === 'downloading' ||
    updateStatus.phase === 'installed'
  );
}

export type UpdateFormField = <K extends keyof FormState>(
  key: K,
  value: FormState[K],
) => void;

export type SelectRomKey = 'baseRomPath';

export type LauncherSummary = {
  connectionActive: boolean;
  courseNote: string;
  currentRomPath: string;
  romPreparation: string;
  romsConfigured: boolean;
  selectedStageLabel: string;
  updateRequired: boolean;
  updateVersion?: string;
};

export type LauncherActions = {
  checkForUpdate: () => Promise<void>;
  cleanupDetailedLogs: () => Promise<void>;
  copyRoomCode: () => Promise<void>;
  createLogArchive: (logDir: string) => Promise<void>;
  createRoom: () => Promise<void>;
  deleteMatchHistory: (matchId: string) => Promise<void>;
  cancelHostedRoom: () => Promise<void>;
  joinRoom: (roomId: string) => Promise<void>;
  openLogDir: (logDir?: string) => Promise<void>;
  openMelonds: () => Promise<void>;
  openMelondsInputConfig: () => Promise<void>;
  pollStatus: () => Promise<void>;
  preflightCheck: () => Promise<void>;
  prepareRoms: () => Promise<void>;
  refreshRooms: () => Promise<void>;
  savePlayerName: () => Promise<void>;
  selectBaseRomAndPrepare: () => Promise<void>;
  selectRomPath: (key: SelectRomKey) => Promise<void>;
  setStartupEnabled: (enabled: boolean) => Promise<void>;
  startMatch: () => Promise<void>;
  stopMatch: () => Promise<void>;
  uploadLogArchive: (logDir: string) => Promise<void>;
};

export type OnboardingState = {
  loaded: boolean;
  romsPrepared: boolean;
  romGenerationBusy: boolean;
  inputConfigOpened: boolean;
  playerNameConfigured: boolean;
};

export type DiagnosticsState = {
  bridgeDiagnostics: BridgeDiagnostics | null;
  gameStateMismatch: GameStateMismatch | null;
};

export type MatchmakingRoomsState = {
  rooms: RoomSummary[];
  loading: boolean;
  refreshDisabled: boolean;
  busy: boolean;
  error: string | null;
  hostedRoomId: string | null;
};

export type StartupState = {
  enabled: boolean;
  loading: boolean;
};

export type BattleMatchStatus = MatchHistoryRecord['status'];

export type BattleMatchRecord = MatchHistoryRecord;
