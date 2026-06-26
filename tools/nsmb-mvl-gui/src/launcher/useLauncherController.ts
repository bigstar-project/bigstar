import { useMutation } from '@tanstack/react-query';
import { relaunch } from '@tauri-apps/plugin-process';
import { check } from '@tauri-apps/plugin-updater';
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
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
import { maxMatchHistoryRecords } from '../matchHistory';
import {
  closeRoom as closeMatchmakingRoom,
  createRoom as createMatchmakingRoom,
  getRoom as getMatchmakingRoom,
  joinRoom as joinMatchmakingRoom,
  listRooms,
  type RoomSummary,
  subscribeHostRoomEvents,
  subscribeLobbyRooms,
} from '../matchmakingClient';
import { notifyNewRoomAvailable } from '../roomNotifications';
import {
  ensureRoms,
  generateRoms,
  getDefaults,
  getSessionStatus,
  getStartupEnabled,
  loadMatchHistory,
  openLogDir as openLogDirCommand,
  openMelonds as openMelondsCommand,
  openMelondsInputConfig as openMelondsInputConfigCommand,
  runPreflightCheck,
  saveDiagnosticEventsEnabled,
  saveMatchHistory,
  saveNewRoomNotificationsEnabled,
  savePlayerName as savePlayerNameCommand,
  saveRomPaths,
  selectRomFile,
  setStartupEnabled as setStartupEnabledCommand,
  startMatch as startMatchCommand,
  stopMatch as stopMatchCommand,
} from '../tauriClient';
import type {
  BridgeDiagnostics,
  FormState,
  GameStateMismatch,
  GenerateRomRequest,
  GenerateRomResponse,
  LaunchRequest,
  MvlStageResult,
  RomIdentity,
  SaveRomPathsRequest,
  StatusKind,
} from '../types';
import { stageLabel } from './options';
import {
  type BattleMatchRecord,
  type BattleMatchStatus,
  isUpdateRequired,
  type LauncherActions,
  type LauncherSummary,
  type SelectRomKey,
  type UpdateStatus,
  type View,
} from './types';

const UPDATE_CHECK_INTERVAL_MS = 5 * 60 * 1000;
const ACTIVITY_STATUS_VISIBLE_MS = 5000;
const MATCHMAKING_WS_RECONNECT_DELAY_MS = 5000;
const MATCH_HISTORY_SAVE_DELAY_MS = 300;

function isTauriRuntime() {
  return '__TAURI_INTERNALS__' in window;
}

function isWebSocketUrl(value: string) {
  return value.startsWith('ws://') || value.startsWith('wss://');
}

function assertRomPairMatches(local: RomIdentity, remote: RomIdentity) {
  if (local.rom_pair_id !== remote.rom_pair_id) {
    throw new Error(
      `ROMが相手と一致しません local=${local.rom_pair_id.slice(0, 12)} remote=${remote.rom_pair_id.slice(0, 12)}`,
    );
  }
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
  const [activeView, setActiveView] = useState<View>(() =>
    window.location.hash === '#settings'
      ? 'settings'
      : window.location.hash === '#history'
        ? 'history'
        : 'battle',
  );
  const [form, setForm] = useState<FormState>(initialForm);
  const [connectionStatus, setConnectionStatus] = useState({
    text: '初期化中',
    kind: 'idle' as StatusKind,
  });
  const [activityStatus, setActivityStatus] = useState<{
    text: string;
    kind: StatusKind;
  } | null>(null);
  const [lastLogDir, setLastLogDir] = useState('');
  const [bridgeDiagnostics, setBridgeDiagnostics] =
    useState<BridgeDiagnostics | null>(null);
  const [gameStateMismatch, setGameStateMismatch] =
    useState<GameStateMismatch | null>(null);
  const [defaultsLoaded, setDefaultsLoaded] = useState(false);
  const [startupEnabled, setStartupEnabledState] = useState(false);
  const [startupLoading, setStartupLoading] = useState(false);
  const [rooms, setRooms] = useState<RoomSummary[]>([]);
  const [roomsLoading, setRoomsLoading] = useState(false);
  const [roomsError, setRoomsError] = useState<string | null>(null);
  const [romPreparation, setRomPreparation] = useState('未確認');
  const [onboardingRomsPrepared, setOnboardingRomsPrepared] = useState(false);
  const [romEnsureBusy, setRomEnsureBusy] = useState(false);
  const [romGenerationBusy, setRomGenerationBusy] = useState(false);
  const [onboardingInputConfigOpened, setOnboardingInputConfigOpened] =
    useState(false);
  const [onboardingPlayerNameConfigured, setOnboardingPlayerNameConfigured] =
    useState(false);
  const [matchmakingActionBusy, setMatchmakingActionBusy] = useState(false);
  const [updateBusy, setUpdateBusy] = useState(false);
  const [updateStatus, setUpdateStatus] = useState<UpdateStatus>({
    phase: 'idle',
  });
  const availableUpdateRef = useRef<Awaited<ReturnType<typeof check>>>(null);
  const updateBusyRef = useRef(false);
  const updatePhaseRef = useRef<UpdateStatus['phase']>('idle');
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
  const [matchHistory, setMatchHistory] = useState<BattleMatchRecord[]>([]);
  const matchHistoryRef = useRef<BattleMatchRecord[]>([]);
  const [matchHistoryLoaded, setMatchHistoryLoaded] = useState(false);
  const [playerProfileId, setPlayerProfileId] = useState('');

  const currentRomPath =
    form.role === 'host' ? form.hostRomPath : form.clientRomPath;
  const selectedStage = useMemo(
    () => selectedStageFrom(form.courseMode, form.matchSeed, form.courseStages),
    [form.courseMode, form.courseStages, form.matchSeed],
  );
  const selectedStageLabel =
    selectedStage === null
      ? form.courseMode === 'random'
        ? '未確定'
        : stageLabel(0)
      : stageLabel(selectedStage);
  const courseNote =
    form.courseMode === 'select'
      ? `ゲーム ${form.courseStages.map((stage) => stageLabel(stage)).join(' / ')}`
      : '起動時にコース列と各試合の seed を確定します。';
  const connectionActive =
    connectionStatus.text.startsWith('実行中') ||
    connectionStatus.text.startsWith('起動済み');
  const updateRequired = isUpdateRequired(updateStatus);
  const romsConfigured = Boolean(
    form.hostRomPath && form.clientRomPath && form.baseRomPath,
  );
  const summary: LauncherSummary = {
    connectionActive,
    courseNote,
    currentRomPath,
    romPreparation,
    romsConfigured,
    selectedStageLabel,
    updateRequired,
    updateVersion: updateStatus.version,
  };

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
      for (const room of nextRooms) {
        const isOwnHostedRoom =
          room.room_id === hostedRoomRef.current?.roomId ||
          ownRoomIdsRef.current.has(room.room_id) ||
          (room.host_player_profile_id !== undefined &&
            room.host_player_profile_id === playerProfileIdRef.current);
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
    [form.signalUrl],
  );

  useEffect(() => {
    connectionActiveRef.current = connectionActive;
  }, [connectionActive]);

  useEffect(() => {
    hostedRoomRef.current = hostedRoom;
  }, [hostedRoom]);

  useEffect(() => {
    playerProfileIdRef.current = playerProfileId;
  }, [playerProfileId]);

  useEffect(() => {
    newRoomNotificationsEnabledRef.current = form.newRoomNotificationsEnabled;
  }, [form.newRoomNotificationsEnabled]);

  useEffect(() => {
    if (
      !defaultsLoaded ||
      !isWebSocketUrl(form.signalUrl) ||
      connectionActive
    ) {
      setRoomsLoading(false);
      setRooms([]);
      setRoomsError(null);
      lobbyRoomSignalUrlRef.current = null;
      lobbySeenRoomIdsRef.current = null;
      return;
    }

    let disposed = false;
    let unsubscribe: (() => void) | null = null;
    let reconnectTimer: number | null = null;

    const connect = () => {
      if (disposed) {
        return;
      }
      setRoomsLoading(true);
      unsubscribe = subscribeLobbyRooms(form.signalUrl, {
        onClose: () => {
          unsubscribe = null;
          if (!disposed) {
            reconnectTimer = window.setTimeout(
              connect,
              MATCHMAKING_WS_RECONNECT_DELAY_MS,
            );
          }
        },
        onError: (error) => {
          if (!disposed) {
            setRoomsError(String(error));
          }
        },
        onOpen: () => {
          if (!disposed) {
            setRoomsLoading(false);
          }
        },
        onSnapshot: (nextRooms) => {
          if (!disposed) {
            setRoomsLoading(false);
            applyLobbySnapshot(nextRooms, { notify: true });
          }
        },
      });
    };

    connect();
    return () => {
      disposed = true;
      if (reconnectTimer !== null) {
        window.clearTimeout(reconnectTimer);
      }
      unsubscribe?.();
    };
  }, [applyLobbySnapshot, connectionActive, defaultsLoaded, form.signalUrl]);

  useEffect(() => {
    updateBusyRef.current = updateBusy;
  }, [updateBusy]);

  useEffect(() => {
    updatePhaseRef.current = updateStatus.phase;
  }, [updateStatus.phase]);

  useEffect(() => {
    currentMatchRef.current = currentMatch;
  }, [currentMatch]);

  useEffect(() => {
    matchHistoryRef.current = matchHistory;
  }, [matchHistory]);

  useEffect(() => {
    if (!matchHistoryLoaded) {
      return;
    }
    const timer = window.setTimeout(() => {
      void saveMatchHistory(matchHistory).catch((error) => {
        setActivityStatus({
          text: `対戦履歴の保存に失敗しました: ${String(error)}`,
          kind: 'warn',
        });
      });
    }, MATCH_HISTORY_SAVE_DELAY_MS);
    return () => window.clearTimeout(timer);
  }, [matchHistoryLoaded, matchHistory]);

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
      matchHistoryRef.current = [
        archived,
        ...matchHistoryRef.current.filter((match) => match.id !== archived.id),
      ].slice(0, maxMatchHistoryRecords);
      setMatchHistory((history) =>
        [
          archived,
          ...history.filter((match) => match.id !== archived.id),
        ].slice(0, maxMatchHistoryRecords),
      );
    },
    [],
  );

  const deleteMatchHistory = useCallback(async (matchId: string) => {
    const nextHistory = matchHistoryRef.current.filter(
      (match) => match.id !== matchId,
    );
    if (nextHistory.length === matchHistoryRef.current.length) {
      return;
    }
    matchHistoryRef.current = nextHistory;
    setMatchHistory(nextHistory);
    try {
      await saveMatchHistory(nextHistory);
      setActivityStatus({
        text: '対戦履歴を削除しました',
        kind: 'warn',
      });
    } catch (error) {
      setActivityStatus({
        text: `対戦履歴の保存に失敗しました: ${String(error)}`,
        kind: 'error',
      });
    }
  }, []);

  const applySessionResults = useCallback(
    (logDir: string, results: MvlStageResult[]) => {
      const current = currentMatchRef.current;
      if (!current || current.logDir !== logDir) {
        return;
      }
      const next: BattleMatchRecord = {
        ...current,
        stages: results,
        status: matchIsComplete(results) ? 'completed' : current.status,
      };
      currentMatchRef.current = next;
      setCurrentMatch(next);
    },
    [],
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
  };

  const pollStatus = useCallback(async () => {
    try {
      const response = await getSessionStatus();
      if (response.log_dir) {
        setLastLogDir(response.log_dir);
        applySessionResults(response.log_dir, response.mvl_results);
      }
      setBridgeDiagnostics(response.webrtc ?? null);
      setGameStateMismatch(response.game_state_mismatch ?? null);
      if (processExited(response.melon) || processExited(response.bridge)) {
        setConnectionStatus({
          text: `プロセス終了 melonDS:${response.melon ?? '-'} bridge:${response.bridge ?? '-'}`,
          kind: 'error',
        });
        return;
      }
      if (!response.active) {
        setGameStateMismatch(null);
        setConnectionStatus({ text: '未接続', kind: 'idle' });
        if (
          response.log_dir &&
          currentMatchRef.current?.logDir === response.log_dir &&
          currentMatchRef.current.status === 'running'
        ) {
          archiveCurrentMatch('stopped');
        }
        return;
      }
      if (response.diagnostics_error) {
        setConnectionStatus({ text: response.diagnostics_error, kind: 'warn' });
        return;
      }
      if (response.game_state_mismatch) {
        setConnectionStatus({
          text: `実行中: ゲーム状態ミスマッチ frame=${response.game_state_mismatch.frame ?? '-'}`,
          kind: 'warn',
        });
        return;
      }
      setConnectionStatus({
        text: `実行中 melonDS:${response.melon ?? '-'} bridge:${response.bridge ?? '-'}`,
        kind: 'ok',
      });
    } catch {
      setConnectionStatus({ text: '状態取得に失敗しました', kind: 'warn' });
    }
  }, [applySessionResults, archiveCurrentMatch]);

  useEffect(() => {
    let disposed = false;

    async function init() {
      try {
        const defaults = await getDefaults();
        if (disposed) return;
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
          newRoomNotificationsEnabled:
            defaults.new_room_notifications_enabled ?? true,
        });
        setPlayerProfileId(defaults.player_profile_id);
        setOnboardingRomsPrepared(defaults.roms_prepared_once);
        setOnboardingInputConfigOpened(defaults.input_config_opened_once);
        setOnboardingPlayerNameConfigured(
          defaults.player_name.trim().length > 0,
        );
        try {
          setStartupEnabledState(await getStartupEnabled());
        } catch (error) {
          if (!disposed) {
            setActivityStatus({
              text: `スタートアップ設定の読み込みに失敗しました: ${String(error)}`,
              kind: 'warn',
            });
          }
        }
        try {
          const persistedHistory = await loadMatchHistory();
          if (!disposed) {
            const loadedHistory = persistedHistory.slice(
              0,
              maxMatchHistoryRecords,
            );
            matchHistoryRef.current = loadedHistory;
            setMatchHistory(loadedHistory);
          }
        } catch (error) {
          if (!disposed) {
            setActivityStatus({
              text: `対戦履歴の読み込みに失敗しました: ${String(error)}`,
              kind: 'warn',
            });
          }
        } finally {
          if (!disposed) {
            setMatchHistoryLoaded(true);
          }
        }
        setDefaultsLoaded(true);
        await pollStatus();
      } catch (error) {
        if (!disposed) {
          setActivityStatus({ text: String(error), kind: 'error' });
        }
      }
    }

    void init();
    const timer = window.setInterval(pollStatus, 2000);
    return () => {
      disposed = true;
      window.clearInterval(timer);
    };
  }, [pollStatus]);

  useEffect(() => {
    if (!defaultsLoaded) {
      return;
    }
    const request: SaveRomPathsRequest = {
      base_rom_path: form.baseRomPath,
    };
    const timer = window.setTimeout(() => {
      void saveRomPaths(request).catch((error) => {
        setActivityStatus({ text: String(error), kind: 'warn' });
      });
    }, 250);
    return () => window.clearTimeout(timer);
  }, [defaultsLoaded, form.baseRomPath]);

  useEffect(() => {
    if (!defaultsLoaded) {
      return;
    }
    const timer = window.setTimeout(() => {
      void saveDiagnosticEventsEnabled({
        enabled: form.diagnosticEventsEnabled,
      }).catch((error) => {
        setActivityStatus({ text: String(error), kind: 'warn' });
      });
    }, 250);
    return () => window.clearTimeout(timer);
  }, [defaultsLoaded, form.diagnosticEventsEnabled]);

  useEffect(() => {
    if (!defaultsLoaded) {
      return;
    }
    const timer = window.setTimeout(() => {
      void saveNewRoomNotificationsEnabled({
        enabled: form.newRoomNotificationsEnabled,
      }).catch((error) => {
        setActivityStatus({ text: String(error), kind: 'warn' });
      });
    }, 250);
    return () => window.clearTimeout(timer);
  }, [defaultsLoaded, form.newRoomNotificationsEnabled]);

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
      setRomPreparation('準備済み');
      setOnboardingRomsPrepared(true);
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
      await saveRomPaths({ base_rom_path: selected });
      await prepareRomsFor(nextForm);
    } catch (error) {
      setActivityStatus({ text: String(error), kind: 'error' });
    }
  };

  const ensurePreparedRoms = useCallback(async (nextForm: FormState) => {
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
      setRomPreparation(response.generated ? '初回準備済み' : '再利用');
      setOnboardingRomsPrepared(true);
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
  }, []);

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
    setRomPreparation('起動時準備中');
    void ensurePreparedRoms(form)
      .then((response) => {
        setRomPreparation(
          response.generated ? '起動時に準備済み' : '起動時に再利用',
        );
      })
      .catch((error) => {
        startupRomPreparationKeyRef.current = null;
        setRomPreparation('準備失敗');
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
        diagnostic_events_enabled: nextForm.diagnosticEventsEnabled,
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
        const response = await startMatchCommand(request);
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
        setGameStateMismatch(null);
        setActivityStatus({
          text: `起動済み melonDS:${response.melon_pid} bridge:${response.bridge_pid}`,
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
      playerProfileId,
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
      romIdentity: RomIdentity;
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
        romIdentity: roms.rom_identity,
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

  useEffect(() => {
    if (!hostedRoom || connectionActive) {
      return;
    }

    let disposed = false;
    let unsubscribe: (() => void) | null = null;
    let reconnectTimer: number | null = null;

    const connect = () => {
      if (disposed) {
        return;
      }
      unsubscribe = subscribeHostRoomEvents(
        hostedRoom.form.signalUrl,
        hostedRoom.roomId,
        {
          onClose: () => {
            unsubscribe = null;
            if (!disposed && !hostedRoomLaunchBusyRef.current) {
              reconnectTimer = window.setTimeout(
                connect,
                MATCHMAKING_WS_RECONNECT_DELAY_MS,
              );
            }
          },
          onError: (error) => {
            if (!disposed) {
              setRoomsError(String(error));
            }
          },
          onJoined: (room) => {
            if (!disposed) {
              void launchHostedRoomFromEvent(room);
            }
          },
        },
      );
    };

    connect();
    return () => {
      disposed = true;
      if (reconnectTimer !== null) {
        window.clearTimeout(reconnectTimer);
      }
      unsubscribe?.();
    };
  }, [connectionActive, hostedRoom, launchHostedRoomFromEvent]);

  const stopMatch = async () => {
    try {
      await stopMatchCommand();
      setGameStateMismatch(null);
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

  const openLogDir = async () => {
    if (!lastLogDir) {
      return;
    }
    try {
      await openLogDirCommand(lastLogDir);
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
      await savePlayerNameCommand({ player_name: playerName });
      setForm((current) => ({ ...current, hostName: playerName }));
      setOnboardingPlayerNameConfigured(true);
      setActivityStatus({
        text: 'プレイヤーネームを保存しました',
        kind: 'ok',
      });
    } catch (error) {
      setActivityStatus({ text: String(error), kind: 'error' });
    }
  };

  const setStartupEnabled = async (enabled: boolean) => {
    const previous = startupEnabled;
    setStartupLoading(true);
    setStartupEnabledState(enabled);
    try {
      await setStartupEnabledCommand(enabled);
      setActivityStatus({
        text: enabled
          ? 'Windowsログイン時の起動を登録しました'
          : 'Windowsログイン時の起動を解除しました',
        kind: 'ok',
      });
    } catch (error) {
      setStartupEnabledState(previous);
      setActivityStatus({ text: String(error), kind: 'error' });
    } finally {
      setStartupLoading(false);
    }
  };

  const copyRoomCode = async () => {
    try {
      await navigator.clipboard.writeText(form.roomCode);
      setActivityStatus({ text: '部屋コードをコピーしました', kind: 'ok' });
    } catch {
      setActivityStatus({
        text: '部屋コードのコピーに失敗しました',
        kind: 'warn',
      });
    }
  };

  const checkForUpdate = useCallback(async () => {
    if (!isTauriRuntime()) {
      setUpdateStatus({ phase: 'none' });
      return;
    }

    if (updateStatus.phase === 'available' && availableUpdateRef.current) {
      try {
        setUpdateBusy(true);
        setUpdateStatus({
          phase: 'downloading',
          version: availableUpdateRef.current.version,
        });
        await availableUpdateRef.current.downloadAndInstall((event) => {
          if (event.event === 'Finished') {
            setUpdateStatus({
              phase: 'installed',
              version: availableUpdateRef.current?.version,
            });
          }
        });
        await relaunch();
      } catch {
        setUpdateStatus({
          phase: 'error',
          version: availableUpdateRef.current?.version,
        });
      } finally {
        setUpdateBusy(false);
      }
      return;
    }

    try {
      setUpdateBusy(true);
      setUpdateStatus({ phase: 'checking' });
      const update = await check();
      if (!update) {
        availableUpdateRef.current = null;
        setUpdateStatus({ phase: 'none' });
        return;
      }
      availableUpdateRef.current = update;
      setUpdateStatus({ phase: 'available', version: update.version });
    } catch {
      setUpdateStatus({ phase: 'error' });
    } finally {
      setUpdateBusy(false);
    }
  }, [updateStatus.phase]);

  const checkForUpdateInBackground = useCallback(async () => {
    if (
      !isTauriRuntime() ||
      updateBusyRef.current ||
      updatePhaseRef.current === 'available' ||
      updatePhaseRef.current === 'checking' ||
      updatePhaseRef.current === 'downloading'
    ) {
      return;
    }
    try {
      setUpdateStatus({ phase: 'checking' });
      const update = await check();
      if (!update) {
        availableUpdateRef.current = null;
        setUpdateStatus({ phase: 'none' });
        return;
      }
      availableUpdateRef.current = update;
      setUpdateStatus({ phase: 'available', version: update.version });
    } catch {
      setUpdateStatus({ phase: 'error' });
    }
  }, []);

  useEffect(() => {
    void checkForUpdateInBackground();
    const timer = window.setInterval(
      () => void checkForUpdateInBackground(),
      UPDATE_CHECK_INTERVAL_MS,
    );
    return () => window.clearInterval(timer);
  }, [checkForUpdateInBackground]);

  const actions: LauncherActions = {
    cancelHostedRoom,
    checkForUpdate,
    copyRoomCode,
    createRoom,
    deleteMatchHistory,
    joinRoom,
    openLogDir,
    openMelonds,
    openMelondsInputConfig,
    pollStatus,
    preflightCheck,
    prepareRoms,
    refreshRooms,
    savePlayerName,
    selectBaseRomAndPrepare,
    selectRomPath,
    setStartupEnabled,
    startMatch,
    stopMatch,
  };

  const changeView = (view: View) => {
    setActiveView(view);
    window.history.replaceState(null, '', `#${view}`);
  };

  return {
    actions,
    activeView,
    bridgeDiagnostics,
    changeView,
    connectionActive,
    connectionStatus,
    currentMatch,
    activityStatus,
    form,
    lastLogDir,
    gameStateMismatch,
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
    matchHistory,
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
      loading: startupLoading,
    },
    summary,
    updateBusy,
    updateStatus,
    updateField,
  };
}
