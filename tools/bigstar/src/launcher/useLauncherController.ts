import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';
import { relaunch } from '@tauri-apps/plugin-process';
import { parseAsStringLiteral, useQueryState } from 'nuqs';
import { useCallback, useEffect, useRef, useState } from 'react';
import { recordAppError } from '../appDiagnostics';
import {
  areAiDevToolsEnabled,
  currentEdition,
  currentRuntimeCapabilities,
} from '../buildProfile';
import {
  currentSettings,
  defaultInputDelayFrames,
  defaultInputMaxFrameLead,
  generateSeed,
  initialForm,
  normalizedCourseStages,
  normalizedRngSeeds,
  processExited,
  selectedStageFrom,
  withRequiredPlan,
} from '../form';
import {
  closeRoom as closeMatchmakingRoom,
  createRoom as createMatchmakingRoom,
  getRoom as getMatchmakingRoom,
  joinRoom as joinMatchmakingRoom,
  listRooms,
  type RoomSummary,
} from '../matchmakingClient';
import { matchHistoryKeys } from '../queries/historyQueryKeys';
import {
  defaultsQueryOptions,
  launcherQueryKeys,
  sessionStatusQueryOptions,
  startupEnabledQueryOptions,
  updateQueryOptions,
} from '../queries/launcherQueries';
import { notifyNewRoomAvailable } from '../roomNotifications';
import {
  cleanupDetailedLogs as cleanupDetailedLogsCommand,
  createLogArchive as createLogArchiveCommand,
  ensureRoms,
  generateRoms,
  openLogDir as openLogDirCommand,
  openMelonds as openMelondsCommand,
  openMelondsInputConfig as openMelondsInputConfigCommand,
  runPreflightCheck,
  saveAiPlayLogEnabled,
  saveDetailedLogsEnabled,
  saveDiagnosticEventsEnabled,
  saveNewRoomNotificationsEnabled,
  savePerformanceLogsEnabled,
  savePlayerName as savePlayerNameCommand,
  saveRomPaths,
  selectRomFile,
  setStartupEnabled as setStartupEnabledCommand,
  startMatch as startMatchCommand,
  stopMatch as stopMatchCommand,
  uploadLogArchive as uploadLogArchiveCommand,
  upsertMatchHistory as upsertMatchHistoryCommand,
} from '../tauriClient';
import type {
  Defaults,
  FormState,
  GenerateRomRequest,
  GenerateRomResponse,
  LaunchRequest,
  MvlStageResult,
  RomIdentity,
  SessionStatus,
  StatusKind,
} from '../types';
import {
  type BattleMatchRecord,
  type BattleMatchStatus,
  type FeedbackInput,
  isUpdateRequired,
  type LauncherActions,
  type LauncherSummary,
  type SelectRomKey,
  type UpdateStatus,
  type View,
} from './types';
import {
  useHostedRoomSubscription,
  useLobbyRoomsSubscription,
} from './useMatchmakingSubscriptions';

const ACTIVITY_STATUS_VISIBLE_MS = 5000;

type ConnectionStatusState = {
  active: boolean;
  kind: StatusKind;
  text: string;
};

const TERMINAL_WEBRTC_STATES = new Set(['closed', 'disconnected', 'failed']);

export function connectionStatusFromSession(
  response: SessionStatus,
): ConnectionStatusState {
  const phase = response.webrtc?.phase?.toLowerCase() ?? null;
  const connectionState =
    response.webrtc?.connection_state?.toLowerCase() ?? null;

  if (response.game_state_mismatch) {
    return { active: response.active, kind: 'error', text: '同期エラー' };
  }
  if (
    phase === 'failed' ||
    (connectionState !== null && TERMINAL_WEBRTC_STATES.has(connectionState)) ||
    processExited(response.melon) ||
    processExited(response.bridge)
  ) {
    return { active: response.active, kind: 'error', text: '接続エラー' };
  }
  if (!response.active) {
    return { active: false, kind: 'idle', text: '未接続' };
  }
  if (response.diagnostics_error) {
    return {
      active: true,
      kind: 'warn',
      text: '接続状態を確認できません',
    };
  }
  if (
    phase === 'connected' &&
    (connectionState === null || connectionState === 'connected')
  ) {
    return { active: true, kind: 'ok', text: '接続済み' };
  }
  return { active: true, kind: 'idle', text: '接続中…' };
}

function isTauriRuntime() {
  return '__TAURI_INTERNALS__' in window;
}

function isWebSocketUrl(value: string) {
  return value.startsWith('ws://') || value.startsWith('wss://');
}

function feedbackUploadUrl(signalUrl: string) {
  const url = new URL(signalUrl);
  if (url.protocol === 'wss:') {
    url.protocol = 'https:';
  } else if (url.protocol === 'ws:') {
    url.protocol = 'http:';
  } else if (url.protocol !== 'https:' && url.protocol !== 'http:') {
    throw new Error(
      'ログアップロード先は ws:// または wss:// から導出してください',
    );
  }
  url.pathname = '/feedback';
  url.search = '';
  url.hash = '';
  return url.toString().replace(/\/$/, '');
}

function assertRomPairMatches(local: RomIdentity, remote: RomIdentity) {
  if (local.rom_pair_id !== remote.rom_pair_id) {
    throw new Error(
      `ROMまたはbridgeが相手と一致しません local=${local.rom_pair_id.slice(0, 12)} remote=${remote.rom_pair_id.slice(0, 12)}`,
    );
  }
}

type CompleteRomIdentity = RomIdentity & { bridge_sha256: string };

function requireCompleteRomIdentity(
  identity: RomIdentity,
): CompleteRomIdentity {
  if (!identity.bridge_sha256) {
    throw new Error('bridge hash を含む対戦 identity を生成できませんでした');
  }
  return identity as CompleteRomIdentity;
}

function matchIsComplete(results: MvlStageResult[]) {
  const latest = results.at(-1);
  if (!latest) {
    return false;
  }
  return (
    latest.mario_match_wins >= latest.target_wins ||
    latest.luigi_match_wins >= latest.target_wins
  );
}

function stageResultsEqual(left: MvlStageResult[], right: MvlStageResult[]) {
  return JSON.stringify(left) === JSON.stringify(right);
}

function formatBytes(bytes: number) {
  if (bytes < 1024) {
    return `${bytes}B`;
  }
  const units = ['KB', 'MB', 'GB'];
  let value = bytes / 1024;
  for (const unit of units) {
    if (value < 1024 || unit === 'GB') {
      return `${value.toFixed(value < 10 ? 1 : 0)}${unit}`;
    }
    value /= 1024;
  }
  return `${bytes}B`;
}

type PreparedRomCache = {
  sourceRom: string;
  hostRom: string;
  clientRom: string;
  identity: RomIdentity;
};

type HostedRoom = {
  roomId: string;
  form: FormState;
  playerIds: BattleMatchRecord['playerIds'];
  playerNames: BattleMatchRecord['playerNames'];
};

type PersistedSetting =
  | { kind: 'baseRomPath'; value: string }
  | { kind: 'diagnosticEvents'; value: boolean }
  | { kind: 'detailedLogs'; value: boolean }
  | { kind: 'performanceLogs'; value: boolean }
  | { kind: 'aiPlayLog'; value: boolean }
  | { kind: 'newRoomNotifications'; value: boolean };

function withPersistedSetting(
  defaults: Defaults | undefined,
  setting: PersistedSetting,
) {
  if (!defaults) return defaults;
  switch (setting.kind) {
    case 'baseRomPath':
      return { ...defaults, base_rom_path: setting.value };
    case 'diagnosticEvents':
      return { ...defaults, diagnostic_events_enabled: setting.value };
    case 'detailedLogs':
      return { ...defaults, detailed_logs_enabled: setting.value };
    case 'performanceLogs':
      return { ...defaults, performance_logs_enabled: setting.value };
    case 'aiPlayLog':
      return { ...defaults, ai_play_log_enabled: setting.value };
    case 'newRoomNotifications':
      return { ...defaults, new_room_notifications_enabled: setting.value };
  }
}

function playerNameOrFallback(value: string, fallback: string) {
  return value.trim() || fallback;
}

function defaultPlayerNames(form: FormState): BattleMatchRecord['playerNames'] {
  const localName = playerNameOrFallback(form.hostName, 'プレイヤー');
  if (form.role === 'host') {
    return {
      mario: localName,
      luigi: '相手',
    };
  }
  return {
    mario: '相手',
    luigi: localName,
  };
}

function defaultPlayerIds(
  form: FormState,
  localProfileId: string,
): BattleMatchRecord['playerIds'] {
  if (form.role === 'host') {
    return {
      mario: localProfileId,
      luigi: '',
    };
  }
  return {
    mario: '',
    luigi: localProfileId,
  };
}

export function useLauncherController() {
  const aiDevToolsEnabled = areAiDevToolsEnabled();
  const runtimeCapabilities = currentRuntimeCapabilities();
  const queryClient = useQueryClient();
  const defaultsQuery = useQuery(defaultsQueryOptions());
  const startupEnabledQuery = useQuery(startupEnabledQueryOptions());
  const sessionStatusQuery = useQuery(
    sessionStatusQueryOptions(defaultsQuery.isSuccess),
  );
  const updateQuery = useQuery(updateQueryOptions(isTauriRuntime()));
  const installUpdateMutation = useMutation({
    mutationFn: async (update: NonNullable<typeof updateQuery.data>) => {
      await update.downloadAndInstall();
      await relaunch();
    },
    onError: (error) => {
      void recordAppError('updater', 'update.install', error);
    },
  });
  const [activeView, setActiveView] = useQueryState(
    'view',
    parseAsStringLiteral(['battle', 'ai', 'history', 'settings'] as const)
      .withDefault('battle')
      .withOptions({ history: 'push' }),
  );
  const [form, setForm] = useState<FormState>(initialForm);
  const [connectionStatus, setConnectionStatus] =
    useState<ConnectionStatusState>({
      active: false,
      text: '初期化中',
      kind: 'idle' as StatusKind,
    });
  const [activityStatus, setActivityStatus] = useState<{
    text: string;
    kind: StatusKind;
  } | null>(null);
  const [lastLogDir, setLastLogDir] = useState('');

  const [rooms, setRooms] = useState<RoomSummary[]>([]);
  const [roomsLoading, setRoomsLoading] = useState(false);
  const [roomsError, setRoomsError] = useState<string | null>(null);
  const [onboardingRomsPrepared, setOnboardingRomsPrepared] = useState(false);
  const [romEnsureBusy, setRomEnsureBusy] = useState(false);
  const [romGenerationBusy, setRomGenerationBusy] = useState(false);
  const [onboardingInputConfigOpened, setOnboardingInputConfigOpened] =
    useState(false);
  const [onboardingPlayerNameConfigured, setOnboardingPlayerNameConfigured] =
    useState(false);
  const [matchmakingActionBusy, setMatchmakingActionBusy] = useState(false);
  const preparedRomCacheRef = useRef<PreparedRomCache | null>(null);
  const preparedRomPromiseRef = useRef<{
    sourceRom: string;
    promise: Promise<GenerateRomResponse>;
  } | null>(null);
  const matchmakingActionBusyRef = useRef(false);
  const hostedRoomLaunchBusyRef = useRef(false);
  const connectionActiveRef = useRef(false);
  const hostedRoomRef = useRef<HostedRoom | null>(null);
  const playerProfileIdRef = useRef('');
  const newRoomNotificationsEnabledRef = useRef(true);
  const ownRoomIdsRef = useRef<Set<string>>(new Set());
  const startupRomPreparationKeyRef = useRef<string | null>(null);
  const lobbyRoomSignalUrlRef = useRef<string | null>(null);
  const lobbySeenRoomIdsRef = useRef<Set<string> | null>(null);
  const [hostedRoom, setHostedRoom] = useState<HostedRoom | null>(null);
  const [currentMatch, setCurrentMatch] = useState<BattleMatchRecord | null>(
    null,
  );
  const currentMatchRef = useRef<BattleMatchRecord | null>(null);
  const [playerProfileId, setPlayerProfileId] = useState('');
  const defaultsInitializedRef = useRef(false);

  const defaultsLoaded =
    defaultsQuery.isSuccess && defaultsInitializedRef.current;
  const startupEnabled = startupEnabledQuery.data ?? false;
  const updateBusy = updateQuery.isFetching || installUpdateMutation.isPending;
  const updateStatus: UpdateStatus = installUpdateMutation.isPending
    ? { phase: 'downloading', version: updateQuery.data?.version }
    : installUpdateMutation.isError || updateQuery.isError
      ? { phase: 'error', version: updateQuery.data?.version }
      : updateQuery.isFetching
        ? { phase: 'checking' }
        : updateQuery.data
          ? { phase: 'available', version: updateQuery.data.version }
          : { phase: 'none' };

  const connectionActive = connectionStatus.active;
  const updateRequired = isUpdateRequired(updateStatus);
  const summary: LauncherSummary = {
    connectionActive,
    updateRequired,
    updateVersion: updateStatus.version,
  };

  const reportMutationError = useCallback((error: unknown) => {
    setActivityStatus({ text: String(error), kind: 'error' });
  }, []);

  const persistSettingMutation = useMutation({
    mutationFn: async (setting: PersistedSetting) => {
      switch (setting.kind) {
        case 'baseRomPath':
          await saveRomPaths({ base_rom_path: setting.value });
          break;
        case 'diagnosticEvents':
          await saveDiagnosticEventsEnabled({ enabled: setting.value });
          break;
        case 'detailedLogs':
          await saveDetailedLogsEnabled({ enabled: setting.value });
          break;
        case 'performanceLogs':
          await savePerformanceLogsEnabled({ enabled: setting.value });
          break;
        case 'aiPlayLog':
          await saveAiPlayLogEnabled({ enabled: setting.value });
          break;
        case 'newRoomNotifications':
          await saveNewRoomNotificationsEnabled({ enabled: setting.value });
          break;
      }
    },
    networkMode: 'always',
    onError: reportMutationError,
    onSuccess: (_result, setting) => {
      queryClient.setQueryData<Defaults>(
        launcherQueryKeys.defaults,
        (defaults) => withPersistedSetting(defaults, setting),
      );
    },
    scope: { id: 'launcher-settings' },
  });
  const upsertHistoryMutation = useMutation({
    mutationFn: upsertMatchHistoryCommand,
    networkMode: 'always',
    onError: (error) => {
      setActivityStatus({
        text: `対戦履歴の保存に失敗しました: ${String(error)}`,
        kind: 'warn',
      });
    },
    onSuccess: () =>
      queryClient.invalidateQueries({ queryKey: matchHistoryKeys.all }),
    scope: { id: 'match-history-write' },
  });
  const upsertHistory = upsertHistoryMutation.mutate;
  const startMatchMutation = useMutation({
    mutationFn: startMatchCommand,
    networkMode: 'always',
  });
  const launchMatch = startMatchMutation.mutateAsync;
  const stopMatchMutation = useMutation({
    mutationFn: stopMatchCommand,
    networkMode: 'always',
  });
  const startupMutation = useMutation({
    mutationFn: setStartupEnabledCommand,
    networkMode: 'always',
    onError: reportMutationError,
    onSuccess: (_result, enabled) => {
      queryClient.setQueryData(launcherQueryKeys.startupEnabled, enabled);
    },
  });
  const savePlayerNameMutation = useMutation({
    mutationFn: savePlayerNameCommand,
    networkMode: 'always',
    onError: reportMutationError,
    onSuccess: (_result, request) => {
      queryClient.setQueryData<Defaults>(
        launcherQueryKeys.defaults,
        (defaults) =>
          defaults
            ? { ...defaults, player_name: request.player_name }
            : defaults,
      );
    },
  });

  const applyLobbySnapshot = useCallback(
    (nextRooms: RoomSummary[], options: { notify: boolean }) => {
      setRooms(nextRooms);
      setRoomsError(null);

      if (lobbyRoomSignalUrlRef.current !== form.signalUrl) {
        lobbyRoomSignalUrlRef.current = form.signalUrl;
        lobbySeenRoomIdsRef.current = new Set(
          nextRooms.map((room) => room.room_id),
        );
        return;
      }

      const seen = lobbySeenRoomIdsRef.current ?? new Set<string>();
      const excludeOwnRooms = !runtimeCapabilities.notifyOwnRooms;
      for (const room of nextRooms) {
        const isOwnHostedRoom =
          excludeOwnRooms &&
          (room.room_id === hostedRoomRef.current?.roomId ||
            ownRoomIdsRef.current.has(room.room_id) ||
            (room.host_player_profile_id !== undefined &&
              room.host_player_profile_id === playerProfileIdRef.current));
        if (isOwnHostedRoom) {
          seen.add(room.room_id);
          continue;
        }
        if (!seen.has(room.room_id)) {
          seen.add(room.room_id);
          if (options.notify && newRoomNotificationsEnabledRef.current) {
            void notifyNewRoomAvailable(room).catch(() => {});
          }
        }
      }
      lobbySeenRoomIdsRef.current = seen;
    },
    [form.signalUrl, runtimeCapabilities.notifyOwnRooms],
  );

  useEffect(() => {
    connectionActiveRef.current = connectionActive;
  }, [connectionActive]);

  useEffect(() => {
    hostedRoomRef.current = hostedRoom;
  }, [hostedRoom]);

  useEffect(() => {
    newRoomNotificationsEnabledRef.current = form.newRoomNotificationsEnabled;
  }, [form.newRoomNotificationsEnabled]);

  const disableLobby = useCallback(() => {
    setRooms([]);
    setRoomsError(null);
    lobbyRoomSignalUrlRef.current = null;
    lobbySeenRoomIdsRef.current = null;
  }, []);
  const handleRoomsError = useCallback((error: unknown) => {
    setRoomsError(String(error));
  }, []);
  const handleLobbySnapshot = useCallback(
    (nextRooms: RoomSummary[]) => {
      applyLobbySnapshot(nextRooms, { notify: true });
    },
    [applyLobbySnapshot],
  );
  useLobbyRoomsSubscription({
    enabled:
      defaultsLoaded && isWebSocketUrl(form.signalUrl) && !connectionActive,
    onDisabled: disableLobby,
    onError: handleRoomsError,
    onLoadingChange: setRoomsLoading,
    onSnapshot: handleLobbySnapshot,
    signalUrl: form.signalUrl,
  });

  const archiveCurrentMatch = useCallback(
    (status: BattleMatchStatus = 'stopped') => {
      const current = currentMatchRef.current;
      if (!current) {
        return;
      }
      const archived: BattleMatchRecord = {
        ...current,
        status: matchIsComplete(current.stages) ? 'completed' : status,
      };
      currentMatchRef.current = archived;
      setCurrentMatch(archived);
      upsertHistory(archived);
    },
    [upsertHistory],
  );

  const applySessionResults = useCallback(
    (logDir: string, results: MvlStageResult[]) => {
      const current = currentMatchRef.current;
      if (!current || current.logDir !== logDir) {
        return;
      }
      if (stageResultsEqual(current.stages, results)) {
        return;
      }
      const next: BattleMatchRecord = {
        ...current,
        stages: results,
        status: matchIsComplete(results) ? 'completed' : current.status,
      };
      currentMatchRef.current = next;
      setCurrentMatch(next);
      if (next.status === 'completed') {
        upsertHistory(next);
      }
    },
    [upsertHistory],
  );

  useEffect(() => {
    if (!activityStatus) {
      return;
    }
    const timer = window.setTimeout(() => {
      setActivityStatus(null);
    }, ACTIVITY_STATUS_VISIBLE_MS);
    return () => window.clearTimeout(timer);
  }, [activityStatus]);

  const updateField = <K extends keyof FormState>(
    key: K,
    value: FormState[K],
  ) => {
    setForm((current) => {
      const next = { ...current, [key]: value };
      if (key === 'wins' || key === 'courseMode') {
        return {
          ...next,
          courseStages: normalizedCourseStages(next),
          rngSeeds: normalizedRngSeeds(next),
        };
      }
      if (key === 'matchSeed') {
        const rngSeeds = normalizedRngSeeds(next);
        return { ...next, rngSeeds };
      }
      return next;
    });
    if (!defaultsLoaded) {
      return;
    }
    switch (key) {
      case 'baseRomPath':
        persistSettingMutation.mutate({
          kind: 'baseRomPath',
          value: String(value),
        });
        break;
      case 'diagnosticEventsEnabled':
        persistSettingMutation.mutate({
          kind: 'diagnosticEvents',
          value: Boolean(value),
        });
        break;
      case 'detailedLogsEnabled':
        persistSettingMutation.mutate({
          kind: 'detailedLogs',
          value: Boolean(value),
        });
        break;
      case 'performanceLogsEnabled':
        persistSettingMutation.mutate({
          kind: 'performanceLogs',
          value: Boolean(value),
        });
        break;
      case 'aiPlayLogEnabled':
        persistSettingMutation.mutate({
          kind: 'aiPlayLog',
          value: Boolean(value),
        });
        break;
      case 'newRoomNotificationsEnabled':
        persistSettingMutation.mutate({
          kind: 'newRoomNotifications',
          value: Boolean(value),
        });
        break;
    }
  };

  const applySessionStatus = useCallback(
    (response: SessionStatus) => {
      if (response.log_dir) {
        setLastLogDir(response.log_dir);
        applySessionResults(response.log_dir, response.mvl_results);
      }
      setConnectionStatus(connectionStatusFromSession(response));
      if (!response.active) {
        if (
          response.log_dir &&
          currentMatchRef.current?.logDir === response.log_dir &&
          currentMatchRef.current.status === 'running'
        ) {
          archiveCurrentMatch('stopped');
        }
      }
    },
    [applySessionResults, archiveCurrentMatch],
  );

  useEffect(() => {
    const defaults = defaultsQuery.data;
    if (!defaults || defaultsInitializedRef.current) {
      return;
    }
    defaultsInitializedRef.current = true;
    const initialSeed = String(generateSeed());
    setForm({
      role: 'host',
      hostName: defaults.player_name,
      signalUrl: defaults.signal_url,
      roomCode: defaults.room_code,
      port: defaults.port,
      hostRomPath: defaults.host_rom_path,
      clientRomPath: defaults.client_rom_path,
      baseRomPath: defaults.base_rom_path,
      courseMode: 'random',
      courseStages: initialForm.courseStages,
      wins: initialForm.wins,
      bigStars: initialForm.bigStars,
      lives: initialForm.lives,
      matchSeed: initialSeed,
      rngSeeds: normalizedRngSeeds({
        ...initialForm,
        matchSeed: initialSeed,
      }),
      inputDelayFrames: initialForm.inputDelayFrames,
      inputMaxFrameLead: initialForm.inputMaxFrameLead,
      rollbackEnabled: initialForm.rollbackEnabled,
      diagnosticEventsEnabled: defaults.diagnostic_events_enabled ?? false,
      detailedLogsEnabled: defaults.detailed_logs_enabled ?? false,
      performanceLogsEnabled: defaults.performance_logs_enabled ?? false,
      aiPlayLogEnabled: defaults.ai_play_log_enabled ?? false,
      newRoomNotificationsEnabled:
        defaults.new_room_notifications_enabled ?? true,
    });
    setPlayerProfileId(defaults.player_profile_id);
    playerProfileIdRef.current = defaults.player_profile_id;
    setOnboardingRomsPrepared(defaults.roms_prepared_once);
    setOnboardingInputConfigOpened(defaults.input_config_opened_once);
    setOnboardingPlayerNameConfigured(defaults.player_name.trim().length > 0);
  }, [defaultsQuery.data]);

  useEffect(() => {
    if (sessionStatusQuery.data) {
      applySessionStatus(sessionStatusQuery.data);
    }
  }, [applySessionStatus, sessionStatusQuery.data]);

  useEffect(() => {
    if (defaultsQuery.error) {
      setActivityStatus({ text: String(defaultsQuery.error), kind: 'error' });
    } else if (startupEnabledQuery.error) {
      setActivityStatus({
        text: `スタートアップ設定の読み込みに失敗しました: ${String(startupEnabledQuery.error)}`,
        kind: 'warn',
      });
    }
    if (sessionStatusQuery.error) {
      setConnectionStatus((current) => ({
        ...current,
        text: '接続状態を確認できません',
        kind: 'warn',
      }));
    }
  }, [
    defaultsQuery.error,
    sessionStatusQuery.error,
    startupEnabledQuery.error,
  ]);

  const selectRomPath = async (key: SelectRomKey) => {
    try {
      const selected = await selectRomFile(form[key]);
      if (selected) {
        updateField(key, selected);
      }
    } catch (error) {
      setActivityStatus({ text: String(error), kind: 'error' });
    }
  };

  const preflightCheck = async () => {
    try {
      setActivityStatus({ text: '起動前チェック中', kind: 'idle' });
      const response = await runPreflightCheck();
      console.info('preflight', response);
      setActivityStatus({ text: '起動前チェック OK', kind: 'ok' });
    } catch (error) {
      setActivityStatus({ text: String(error), kind: 'error' });
    }
  };

  const prepareRomsFor = async (sourceForm: FormState) => {
    const request: GenerateRomRequest = {
      source_rom: sourceForm.baseRomPath,
    };

    try {
      setRomGenerationBusy(true);
      setActivityStatus({ text: '共通 ROM を準備中', kind: 'idle' });
      const response = await generateRoms(request);
      preparedRomCacheRef.current = {
        sourceRom: sourceForm.baseRomPath,
        hostRom: response.host_rom,
        clientRom: response.client_rom,
        identity: response.rom_identity,
      };
      setForm((current) => ({
        ...current,
        baseRomPath: sourceForm.baseRomPath,
        hostRomPath: response.host_rom,
        clientRomPath: response.client_rom,
      }));
      setOnboardingRomsPrepared(true);
      void queryClient.invalidateQueries({
        queryKey: launcherQueryKeys.defaults,
      });
      setActivityStatus({ text: '共通 ROM の準備が完了しました', kind: 'ok' });
    } catch (error) {
      setActivityStatus({ text: String(error), kind: 'error' });
    } finally {
      setRomGenerationBusy(false);
    }
  };

  const prepareRoms = async () => {
    await prepareRomsFor(form);
  };

  const selectBaseRomAndPrepare = async () => {
    try {
      const selected = await selectRomFile(form.baseRomPath);
      if (!selected) {
        return;
      }
      const nextForm = { ...form, baseRomPath: selected };
      setForm(nextForm);
      await persistSettingMutation.mutateAsync({
        kind: 'baseRomPath',
        value: selected,
      });
      await prepareRomsFor(nextForm);
    } catch (error) {
      setActivityStatus({ text: String(error), kind: 'error' });
    }
  };

  const ensurePreparedRoms = useCallback(
    async (nextForm: FormState) => {
      const inFlight = preparedRomPromiseRef.current;
      if (inFlight?.sourceRom === nextForm.baseRomPath) {
        return inFlight.promise;
      }
      const request: GenerateRomRequest = {
        source_rom: nextForm.baseRomPath,
      };
      const promise = (async () => {
        setRomEnsureBusy(true);
        const response = await ensureRoms(request);
        preparedRomCacheRef.current = {
          sourceRom: nextForm.baseRomPath,
          hostRom: response.host_rom,
          clientRom: response.client_rom,
          identity: response.rom_identity,
        };
        setForm((current) => ({
          ...current,
          hostRomPath: response.host_rom,
          clientRomPath: response.client_rom,
        }));
        setOnboardingRomsPrepared(true);
        void queryClient.invalidateQueries({
          queryKey: launcherQueryKeys.defaults,
        });
        return response;
      })();
      preparedRomPromiseRef.current = {
        sourceRom: nextForm.baseRomPath,
        promise,
      };
      try {
        return await promise;
      } finally {
        if (preparedRomPromiseRef.current?.promise === promise) {
          preparedRomPromiseRef.current = null;
        }
        setRomEnsureBusy(false);
      }
    },
    [queryClient],
  );

  const cachedPreparedRomsFor = useCallback(
    (sourceForm: FormState): GenerateRomResponse | null => {
      const cached = preparedRomCacheRef.current;
      if (!cached || cached.sourceRom !== sourceForm.baseRomPath) {
        return null;
      }
      return {
        host_rom: cached.hostRom,
        client_rom: cached.clientRom,
        generated: false,
        rom_identity: cached.identity,
      };
    },
    [],
  );

  useEffect(() => {
    if (!defaultsLoaded || connectionActive || !form.baseRomPath.trim()) {
      return;
    }
    const key = form.baseRomPath.trim();
    if (startupRomPreparationKeyRef.current === key) {
      return;
    }
    startupRomPreparationKeyRef.current = key;
    void ensurePreparedRoms(form).catch((error) => {
      startupRomPreparationKeyRef.current = null;
      setActivityStatus({
        text: `起動時のROM準備に失敗しました: ${String(error)}`,
        kind: 'warn',
      });
    });
  }, [defaultsLoaded, form, connectionActive, ensurePreparedRoms]);

  const startMatchFor = useCallback(
    async (
      sourceForm: FormState,
      playerNames: BattleMatchRecord['playerNames'] = defaultPlayerNames(
        sourceForm,
      ),
      playerIds: BattleMatchRecord['playerIds'] = defaultPlayerIds(
        sourceForm,
        playerProfileId,
      ),
    ) => {
      const nextForm = withRequiredPlan(sourceForm);
      if (
        JSON.stringify(currentSettings(nextForm)) !==
        JSON.stringify(currentSettings(form))
      ) {
        setForm(nextForm);
      }
      const stage = selectedStageFrom(
        nextForm.courseMode,
        nextForm.matchSeed,
        nextForm.courseStages,
      );
      if (stage === null) {
        setActivityStatus({
          text: 'Match seed は10進数、または 0x から始まる16進数で指定してください',
          kind: 'error',
        });
        return false;
      }

      const advancedDiagnostics =
        currentEdition() === 'insiders' ||
        runtimeCapabilities.configurableSignalServer;
      const performanceLogsEnabled =
        (currentEdition() === 'public' &&
          !runtimeCapabilities.configurableSignalServer) ||
        nextForm.performanceLogsEnabled;
      const request: LaunchRequest = {
        role: nextForm.role,
        signal_url: nextForm.signalUrl,
        room_code: nextForm.roomCode,
        port: nextForm.port,
        rom_path:
          nextForm.role === 'host'
            ? nextForm.hostRomPath
            : nextForm.clientRomPath,
        settings: currentSettings(nextForm),
        player_names: playerNames,
        diagnostic_events_enabled:
          advancedDiagnostics && nextForm.diagnosticEventsEnabled,
        detailed_logs_enabled:
          advancedDiagnostics && nextForm.detailedLogsEnabled,
        performance_logs_enabled: performanceLogsEnabled,
        ai_play_log_enabled: advancedDiagnostics && nextForm.aiPlayLogEnabled,
      };

      try {
        let roms = cachedPreparedRomsFor(nextForm);
        if (!roms) {
          setActivityStatus({ text: '共通 ROM を確認中', kind: 'idle' });
          roms = await ensurePreparedRoms(nextForm);
        }
        request.rom_path =
          nextForm.role === 'host' ? roms.host_rom : roms.client_rom;
        request.rom_identity = roms.rom_identity;
        setActivityStatus({ text: `起動中 stage=${stage}`, kind: 'idle' });
        const response = await launchMatch(request);
        archiveCurrentMatch('stopped');
        const record: BattleMatchRecord = {
          id: response.log_dir,
          logDir: response.log_dir,
          playerIds,
          playerNames,
          role: nextForm.role,
          roomCode: nextForm.roomCode,
          settings: request.settings,
          stages: [],
          startedAt: new Date().toISOString(),
          status: 'running',
        };
        currentMatchRef.current = record;
        setCurrentMatch(record);
        setLastLogDir(response.log_dir);
        setConnectionStatus({
          active: true,
          text: '接続中…',
          kind: 'idle',
        });
        setActivityStatus({
          text: '対戦を起動しました',
          kind: 'ok',
        });
        return true;
      } catch (error) {
        setActivityStatus({ text: String(error), kind: 'error' });
        return false;
      }
    },
    [
      archiveCurrentMatch,
      cachedPreparedRomsFor,
      ensurePreparedRoms,
      form,
      launchMatch,
      playerProfileId,
      runtimeCapabilities.configurableSignalServer,
    ],
  );

  const startMatch = async () => {
    await startMatchFor(
      form.courseMode === 'random'
        ? withRequiredPlan(form, { refreshRandom: true })
        : form,
    );
  };
  const startMatchForRef = useRef(startMatchFor);
  useEffect(() => {
    startMatchForRef.current = startMatchFor;
  }, [startMatchFor]);

  const createRoomMutation = useMutation({
    mutationFn: async ({
      romIdentity,
      sourceForm,
    }: {
      romIdentity: CompleteRomIdentity;
      sourceForm: FormState;
    }) => {
      const nextForm = withRequiredPlan(sourceForm);
      return createMatchmakingRoom({
        hostName: nextForm.hostName,
        hostProfileId: playerProfileId,
        romIdentity,
        settings: currentSettings(nextForm),
        signalUrl: nextForm.signalUrl,
      });
    },
  });

  const joinRoomMutation = useMutation({
    mutationFn: async ({
      playerName,
      romPairId,
      roomId,
    }: {
      playerName: string;
      romPairId: string;
      roomId: string;
    }) =>
      joinMatchmakingRoom({
        playerName,
        playerProfileId,
        romPairId,
        roomId,
        signalUrl: form.signalUrl,
      }),
  });

  const createRoom = async () => {
    if (matchmakingActionBusyRef.current) {
      return;
    }
    if (updateRequired) {
      setActivityStatus({
        text: updateStatus.version
          ? `GUI v${updateStatus.version} への更新が必要です`
          : 'GUI の更新が必要です',
        kind: 'warn',
      });
      return;
    }
    if (connectionActive) {
      setActivityStatus({
        text: '実行中の対戦を停止してから部屋を作成してください',
        kind: 'warn',
      });
      return;
    }
    if (hostedRoom) {
      setActivityStatus({
        text: '作成済みの部屋を閉じてから新しい部屋を作成してください',
        kind: 'warn',
      });
      return;
    }
    matchmakingActionBusyRef.current = true;
    setMatchmakingActionBusy(true);
    try {
      const playerName = form.hostName.trim();
      if (!playerName) {
        setActivityStatus({
          text: '設定画面でプレイヤーネームを保存してください',
          kind: 'warn',
        });
        return;
      }
      setActivityStatus({ text: '部屋用 ROM を確認中', kind: 'idle' });
      const plannedForm = withRequiredPlan(
        { ...form, hostName: playerName },
        { refreshRandom: true },
      );
      const stage = selectedStageFrom(
        plannedForm.courseMode,
        plannedForm.matchSeed,
        plannedForm.courseStages,
      );
      if (stage === null) {
        setActivityStatus({
          text: 'Match seed は10進数、または 0x から始まる16進数で指定してください',
          kind: 'error',
        });
        return;
      }
      let roms = cachedPreparedRomsFor(plannedForm);
      if (!roms) {
        roms = await ensurePreparedRoms(plannedForm);
      }
      setActivityStatus({ text: '部屋を作成中', kind: 'idle' });
      const response = await createRoomMutation.mutateAsync({
        romIdentity: requireCompleteRomIdentity(roms.rom_identity),
        sourceForm: plannedForm,
      });
      ownRoomIdsRef.current.add(response.room_id);
      const nextForm: FormState = {
        ...plannedForm,
        role: 'host',
        roomCode: response.room_id,
        signalUrl: response.signal_url,
      };
      setForm(nextForm);
      setHostedRoom({
        roomId: response.room_id,
        form: nextForm,
        playerIds: {
          mario: playerProfileId,
          luigi: '',
        },
        playerNames: {
          mario: playerName,
          luigi: '相手',
        },
      });
      setActivityStatus({
        text: `部屋を作成しました: ${response.room_id}。参加者を待っています`,
        kind: 'ok',
      });
    } catch (error) {
      setActivityStatus({ text: String(error), kind: 'error' });
    } finally {
      matchmakingActionBusyRef.current = false;
      setMatchmakingActionBusy(false);
    }
  };

  const joinRoom = async (roomId: string) => {
    if (matchmakingActionBusyRef.current) {
      return;
    }
    if (updateRequired) {
      setActivityStatus({
        text: updateStatus.version
          ? `GUI v${updateStatus.version} への更新が必要です`
          : 'GUI の更新が必要です',
        kind: 'warn',
      });
      return;
    }
    if (connectionActive) {
      setActivityStatus({
        text: '実行中の対戦を停止してから部屋に参加してください',
        kind: 'warn',
      });
      return;
    }
    if (hostedRoom) {
      setActivityStatus({
        text: '作成済みの部屋を閉じてから別の部屋に参加してください',
        kind: 'warn',
      });
      return;
    }
    matchmakingActionBusyRef.current = true;
    setMatchmakingActionBusy(true);
    try {
      const playerName = form.hostName.trim();
      if (!playerName) {
        setActivityStatus({
          text: '設定画面でプレイヤーネームを保存してください',
          kind: 'warn',
        });
        return;
      }
      setActivityStatus({ text: '部屋情報を確認中', kind: 'idle' });
      const room = await getMatchmakingRoom(form.signalUrl, roomId);
      const nextForm: FormState = {
        ...form,
        role: 'client',
        roomCode: room.room_id,
        courseMode: room.settings.course_mode,
        courseStages: room.settings.course_stages,
        wins: room.settings.wins,
        bigStars: room.settings.big_stars,
        lives: room.settings.lives,
        matchSeed: room.settings.match_seed,
        rngSeeds: room.settings.rng_seeds,
        inputDelayFrames:
          room.settings.input_delay_frames ?? defaultInputDelayFrames,
        inputMaxFrameLead:
          room.settings.input_max_frame_lead ?? defaultInputMaxFrameLead,
        rollbackEnabled: room.settings.rollback_enabled ?? false,
      };
      const stage = selectedStageFrom(
        nextForm.courseMode,
        nextForm.matchSeed,
        nextForm.courseStages,
      );
      if (stage === null) {
        setActivityStatus({
          text: 'Match seed は10進数、または 0x から始まる16進数で指定してください',
          kind: 'error',
        });
        return;
      }
      let roms = cachedPreparedRomsFor(nextForm);
      if (!roms) {
        setActivityStatus({ text: '参加用 ROM を確認中', kind: 'idle' });
        roms = await ensurePreparedRoms(nextForm);
      }
      assertRomPairMatches(roms.rom_identity, room.rom_identity);
      setActivityStatus({ text: '部屋に参加中', kind: 'idle' });
      const response = await joinRoomMutation.mutateAsync({
        playerName,
        romPairId: roms.rom_identity.rom_pair_id,
        roomId,
      });
      assertRomPairMatches(roms.rom_identity, response.rom_identity);
      nextForm.signalUrl = response.signal_url;
      setForm(nextForm);
      setActivityStatus({
        text: '部屋に参加しました。接続を確立してからmelonDSを起動します',
        kind: 'idle',
      });
      await startMatchFor(
        nextForm,
        {
          mario: playerNameOrFallback(room.host_name, '相手'),
          luigi: playerNameOrFallback(playerName, 'プレイヤー'),
        },
        {
          mario:
            room.host_player_profile_id ??
            response.host_player_profile_id ??
            '',
          luigi: playerProfileId,
        },
      );
    } catch (error) {
      setActivityStatus({ text: String(error), kind: 'error' });
    } finally {
      matchmakingActionBusyRef.current = false;
      setMatchmakingActionBusy(false);
    }
  };

  const launchHostedRoomFromEvent = useCallback(async (room: RoomSummary) => {
    const currentHostedRoom = hostedRoomRef.current;
    if (
      !currentHostedRoom ||
      currentHostedRoom.roomId !== room.room_id ||
      connectionActiveRef.current ||
      hostedRoomLaunchBusyRef.current
    ) {
      return;
    }

    try {
      hostedRoomLaunchBusyRef.current = true;
      matchmakingActionBusyRef.current = true;
      setMatchmakingActionBusy(true);
      setHostedRoom(null);
      setActivityStatus({
        text: '参加者を検出しました。接続を確立してからmelonDSを起動します',
        kind: 'idle',
      });
      await startMatchForRef.current(
        currentHostedRoom.form,
        {
          ...currentHostedRoom.playerNames,
          luigi: playerNameOrFallback(
            room.client_name ?? '',
            currentHostedRoom.playerNames.luigi,
          ),
        },
        {
          ...currentHostedRoom.playerIds,
          luigi: room.client_player_profile_id ?? '',
        },
      );
    } catch (error) {
      setHostedRoom(null);
      setActivityStatus({ text: String(error), kind: 'error' });
    } finally {
      hostedRoomLaunchBusyRef.current = false;
      matchmakingActionBusyRef.current = false;
      setMatchmakingActionBusy(false);
    }
  }, []);

  const handleHostedRoomJoined = useCallback(
    (room: RoomSummary) => {
      void launchHostedRoomFromEvent(room);
    },
    [launchHostedRoomFromEvent],
  );
  useHostedRoomSubscription({
    enabled: Boolean(hostedRoom) && !connectionActive,
    onError: handleRoomsError,
    onJoined: handleHostedRoomJoined,
    roomId: hostedRoom?.roomId ?? '',
    signalUrl: hostedRoom?.form.signalUrl ?? '',
  });

  const stopMatch = async () => {
    try {
      await stopMatchMutation.mutateAsync();
      archiveCurrentMatch('stopped');
      setActivityStatus({ text: '停止しました', kind: 'warn' });
    } catch (error) {
      setActivityStatus({ text: String(error), kind: 'error' });
    }
  };

  const cancelHostedRoom = async () => {
    if (!hostedRoom) {
      return;
    }
    try {
      setMatchmakingActionBusy(true);
      matchmakingActionBusyRef.current = true;
      await closeMatchmakingRoom(hostedRoom.form.signalUrl, hostedRoom.roomId);
      ownRoomIdsRef.current.delete(hostedRoom.roomId);
      setHostedRoom(null);
      setRooms((current) =>
        current.filter((room) => room.room_id !== hostedRoom.roomId),
      );
      setActivityStatus({ text: '部屋を閉じました', kind: 'warn' });
    } catch (error) {
      setActivityStatus({ text: String(error), kind: 'error' });
    } finally {
      matchmakingActionBusyRef.current = false;
      setMatchmakingActionBusy(false);
    }
  };

  const refreshRooms = async () => {
    if (!isWebSocketUrl(form.signalUrl)) {
      setActivityStatus({
        text: 'シグナリングURLは ws:// または wss:// で指定してください',
        kind: 'warn',
      });
      return;
    }
    try {
      setRoomsLoading(true);
      const response = await listRooms(form.signalUrl);
      applyLobbySnapshot(response.rooms, { notify: false });
    } catch (error) {
      setRoomsError(String(error));
      setActivityStatus({ text: String(error), kind: 'error' });
    } finally {
      setRoomsLoading(false);
    }
  };

  const openLogDir = async (logDir?: string) => {
    const target = logDir ?? lastLogDir;
    if (!target) {
      return;
    }
    try {
      await openLogDirCommand(target);
    } catch (error) {
      setActivityStatus({ text: String(error), kind: 'error' });
    }
  };

  const createLogArchive = async (logDir: string) => {
    try {
      const response = await createLogArchiveCommand(logDir);
      setActivityStatus({
        text: `安全な診断ZIPを作成しました: ${response.archive_path}`,
        kind: 'ok',
      });
    } catch (error) {
      setActivityStatus({ text: String(error), kind: 'error' });
    }
  };

  const uploadLogArchive = async (
    logDir: string,
    feedback: FeedbackInput,
  ): Promise<string | null> => {
    try {
      const response = await uploadLogArchiveCommand({
        log_dir: logDir,
        upload_url: feedbackUploadUrl(form.signalUrl),
        category: feedback.category,
        description: feedback.description,
        include_performance: feedback.includePerformance,
      });
      setActivityStatus({
        text: `フィードバックを送信しました: ${response.report_id}`,
        kind: 'ok',
      });
      return response.report_id;
    } catch (error) {
      setActivityStatus({ text: String(error), kind: 'error' });
      return null;
    }
  };

  const cleanupDetailedLogs = async () => {
    try {
      const response = await cleanupDetailedLogsCommand();
      setActivityStatus({
        text:
          response.deleted_files === 0 && response.deleted_dirs === 0
            ? '削除できる古い詳細ログはありません'
            : `古い詳細ログを削除しました: ${response.deleted_files}ファイル / ${formatBytes(response.freed_bytes)}`,
        kind: 'ok',
      });
    } catch (error) {
      setActivityStatus({ text: String(error), kind: 'error' });
    }
  };

  const openMelonds = async () => {
    try {
      const pid = await openMelondsCommand();
      setActivityStatus({
        text: `melonDS を起動しました pid:${pid}`,
        kind: 'ok',
      });
    } catch (error) {
      setActivityStatus({ text: String(error), kind: 'error' });
    }
  };

  const openMelondsInputConfig = async () => {
    try {
      const pid = await openMelondsInputConfigCommand();
      setActivityStatus({
        text: `melonDS の入力設定を開きました pid:${pid}`,
        kind: 'ok',
      });
      setOnboardingInputConfigOpened(true);
    } catch (error) {
      setActivityStatus({ text: String(error), kind: 'error' });
    }
  };

  const savePlayerName = async () => {
    const playerName = form.hostName.trim();
    if (!playerName) {
      setActivityStatus({
        text: 'プレイヤーネームを入力してください',
        kind: 'warn',
      });
      return;
    }
    if ([...playerName].length > 32) {
      setActivityStatus({
        text: 'プレイヤーネームは32文字以内で入力してください',
        kind: 'warn',
      });
      return;
    }

    try {
      await savePlayerNameMutation.mutateAsync({ player_name: playerName });
      setForm((current) => ({ ...current, hostName: playerName }));
      setOnboardingPlayerNameConfigured(true);
      setActivityStatus({
        text: 'プレイヤーネームを保存しました',
        kind: 'ok',
      });
    } catch {
      return;
    }
  };

  const setStartupEnabled = async (enabled: boolean) => {
    try {
      await startupMutation.mutateAsync(enabled);
      setActivityStatus({
        text: enabled
          ? 'Windowsログイン時の起動を登録しました'
          : 'Windowsログイン時の起動を解除しました',
        kind: 'ok',
      });
    } catch {
      return;
    }
  };

  const copyRoomCode = async () => {
    try {
      await navigator.clipboard.writeText(form.roomCode);
      setActivityStatus({ text: '部屋コードをコピーしました', kind: 'ok' });
    } catch {
      void recordAppError('gui', 'clipboard.copy_room_code', 'copy failed');
      setActivityStatus({
        text: '部屋コードのコピーに失敗しました',
        kind: 'warn',
      });
    }
  };

  const checkForUpdate = async () => {
    if (!isTauriRuntime()) {
      return;
    }
    if (updateQuery.data) {
      await installUpdateMutation.mutateAsync(updateQuery.data);
    } else {
      await updateQuery.refetch();
    }
  };

  const actions: LauncherActions = {
    cancelHostedRoom,
    checkForUpdate,
    cleanupDetailedLogs,
    copyRoomCode,
    createLogArchive,
    createRoom,
    joinRoom,
    openLogDir,
    openMelonds,
    openMelondsInputConfig,
    preflightCheck,
    prepareRoms,
    refreshRooms,
    savePlayerName,
    selectBaseRomAndPrepare,
    selectRomPath,
    setStartupEnabled,
    startMatch,
    stopMatch,
    uploadLogArchive,
  };

  const changeView = (view: View) => {
    if (view === 'ai' && !aiDevToolsEnabled) {
      return;
    }
    void setActiveView(view);
  };

  useEffect(() => {
    if (activeView === 'ai' && !aiDevToolsEnabled) {
      void setActiveView('battle');
    }
  }, [activeView, aiDevToolsEnabled, setActiveView]);

  return {
    actions,
    activeView,
    changeView,
    connectionActive,
    connectionStatus,
    currentMatch,
    activityStatus,
    form,
    lastLogDir,
    matchmakingRooms: {
      rooms,
      loading: roomsLoading,
      refreshDisabled:
        !defaultsLoaded ||
        !isWebSocketUrl(form.signalUrl) ||
        connectionActive ||
        matchmakingActionBusy ||
        createRoomMutation.isPending ||
        joinRoomMutation.isPending,
      busy:
        matchmakingActionBusy ||
        createRoomMutation.isPending ||
        joinRoomMutation.isPending,
      error: roomsError,
      hostedRoomId: hostedRoom?.roomId ?? null,
    },
    onboarding: {
      loaded: defaultsLoaded,
      romsPrepared: onboardingRomsPrepared,
      romGenerationBusy,
      inputConfigOpened: onboardingInputConfigOpened,
      playerNameConfigured: onboardingPlayerNameConfigured,
    },
    romStatus:
      romEnsureBusy || romGenerationBusy
        ? { text: 'ROM生成中', kind: 'idle' as StatusKind }
        : null,
    startup: {
      enabled: startupEnabled,
      loading: startupEnabledQuery.isPending || startupMutation.isPending,
    },
    summary,
    updateBusy,
    updateStatus,
    updateField,
  };
}
