import type { RoomSummary } from '../matchmakingClient';
import type { BridgeDiagnostics, FormState } from '../types';

export type View = 'battle' | 'ai' | 'settings';

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
};

export type LauncherActions = {
  checkForUpdate: () => Promise<void>;
  copyRoomCode: () => Promise<void>;
  createRoom: () => Promise<void>;
  joinRoom: (roomId: string) => Promise<void>;
  openLogDir: () => Promise<void>;
  openMelonds: () => Promise<void>;
  openMelondsInputConfig: () => Promise<void>;
  pollStatus: () => Promise<void>;
  preflightCheck: () => Promise<void>;
  prepareRoms: () => Promise<void>;
  refreshRooms: () => Promise<void>;
  selectBaseRomAndPrepare: () => Promise<void>;
  selectRomPath: (key: SelectRomKey) => Promise<void>;
  startMatch: () => Promise<void>;
  stopMatch: () => Promise<void>;
};

export type OnboardingState = {
  loaded: boolean;
  romsPrepared: boolean;
  romGenerationBusy: boolean;
  inputConfigOpened: boolean;
};

export type DiagnosticsState = {
  bridgeDiagnostics: BridgeDiagnostics | null;
};

export type MatchmakingRoomsState = {
  rooms: RoomSummary[];
  loading: boolean;
  refreshDisabled: boolean;
  busy: boolean;
  error: string | null;
};
