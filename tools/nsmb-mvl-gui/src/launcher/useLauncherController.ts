import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';
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
import {
  closeRoom as closeMatchmakingRoom,
  createRoom as createMatchmakingRoom,
  getRoom as getMatchmakingRoom,
  joinRoom as joinMatchmakingRoom,
  listRooms,
} from '../matchmakingClient';
import {
  ensureRoms,
  generateRoms,
  getDefaults,
  getSessionStatus,
  openLogDir as openLogDirCommand,
  openMelonds as openMelondsCommand,
  openMelondsInputConfig as openMelondsInputConfigCommand,
  runPreflightCheck,
  saveDiagnosticEventsEnabled,
  saveRomPaths,
  selectRomFile,
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
  RomIdentity,
  SaveRomPathsRequest,
  StatusKind,
} from '../types';
import {
  isUpdateRequired,
  type LauncherActions,
  type LauncherSummary,
  type SelectRomKey,
  type UpdateStatus,
  type View,
} from './types';

const UPDATE_CHECK_INTERVAL_MS = 5 * 60 * 1000;
const ACTIVITY_STATUS_VISIBLE_MS = 5000;
const ROOMS_REFETCH_INTERVAL_MS = 15 * 1000;

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

type PreparedRomCache = {
  sourceRom: string;
  hostRom: string;
  clientRom: string;
  identity: RomIdentity;
};

type HostedRoom = {
  roomId: string;
  form: FormState;
};

export function useLauncherController() {
  const queryClient = useQueryClient();
  const [activeView, setActiveView] = useState<View>(() =>
    window.location.hash === '#settings' ? 'settings' : 'battle',
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
  const [romPreparation, setRomPreparation] = useState('未確認');
  const [onboardingRomsPrepared, setOnboardingRomsPrepared] = useState(false);
  const [romEnsureBusy, setRomEnsureBusy] = useState(false);
  const [romGenerationBusy, setRomGenerationBusy] = useState(false);
  const [onboardingInputConfigOpened, setOnboardingInputConfigOpened] =
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
  const startupRomPreparationKeyRef = useRef<string | null>(null);
  const [hostedRoom, setHostedRoom] = useState<HostedRoom | null>(null);

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
        : '0'
      : String(selectedStage);
  const courseNote =
    form.courseMode === 'select'
      ? `Game ${form.courseStages.map((stage) => stage).join(' / ')}`
      : '起動時にコース列と各試合の seed を確定します。';
  const connectionActive =
    connectionStatus.text.startsWith('実行中') ||
    connectionStatus.text.startsWith('起動済み');
  const updateRequired = isUpdateRequired(updateStatus);
  const romsConfigured = Boolean(
    form.hostRomPath && form.clientRomPath && form.baseRomPath,
  );
  const roomsQueryEnabled =
    defaultsLoaded &&
    activeView === 'battle' &&
    isWebSocketUrl(form.signalUrl) &&
    !connectionActive &&
    !hostedRoom;

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
  const roomsQuery = useQuery({
    enabled: roomsQueryEnabled,
    queryFn: async () => {
      const response = await listRooms(form.signalUrl);
      return response.rooms;
    },
    queryKey: ['matchmakingRooms', form.signalUrl],
    refetchInterval: roomsQueryEnabled ? ROOMS_REFETCH_INTERVAL_MS : false,
  });

  useEffect(() => {
    updateBusyRef.current = updateBusy;
  }, [updateBusy]);

  useEffect(() => {
    updatePhaseRef.current = updateStatus.phase;
  }, [updateStatus.phase]);

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
  }, []);

  useEffect(() => {
    let disposed = false;

    async function init() {
      try {
        const defaults = await getDefaults();
        if (disposed) return;
        const initialSeed = String(generateSeed());
        setForm({
          role: 'host',
          hostName: 'Player',
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
        });
        setOnboardingRomsPrepared(defaults.roms_prepared_once);
        setOnboardingInputConfigOpened(defaults.input_config_opened_once);
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
    async (sourceForm: FormState) => {
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
    [cachedPreparedRomsFor, ensurePreparedRoms, form],
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
        romIdentity,
        settings: currentSettings(nextForm),
        signalUrl: nextForm.signalUrl,
      });
    },
  });

  const joinRoomMutation = useMutation({
    mutationFn: async ({
      romPairId,
      roomId,
    }: {
      romPairId: string;
      roomId: string;
    }) => joinMatchmakingRoom({ romPairId, roomId, signalUrl: form.signalUrl }),
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
      setActivityStatus({ text: '部屋用 ROM を確認中', kind: 'idle' });
      const plannedForm = withRequiredPlan(form, { refreshRandom: true });
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
      const nextForm: FormState = {
        ...plannedForm,
        role: 'host',
        roomCode: response.room_id,
        signalUrl: response.signal_url,
      };
      setForm(nextForm);
      setHostedRoom({ roomId: response.room_id, form: nextForm });
      await queryClient.invalidateQueries({ queryKey: ['matchmakingRooms'] });
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
        romPairId: roms.rom_identity.rom_pair_id,
        roomId,
      });
      assertRomPairMatches(roms.rom_identity, response.rom_identity);
      nextForm.signalUrl = response.signal_url;
      setForm(nextForm);
      await queryClient.invalidateQueries({ queryKey: ['matchmakingRooms'] });
      setActivityStatus({
        text: '部屋に参加しました。接続を確立してからmelonDSを起動します',
        kind: 'idle',
      });
      await startMatchFor(nextForm);
    } catch (error) {
      setActivityStatus({ text: String(error), kind: 'error' });
    } finally {
      matchmakingActionBusyRef.current = false;
      setMatchmakingActionBusy(false);
    }
  };

  useEffect(() => {
    if (!hostedRoom || connectionActive || hostedRoomLaunchBusyRef.current) {
      return;
    }

    let disposed = false;
    const pollHostedRoom = async () => {
      if (hostedRoomLaunchBusyRef.current) {
        return;
      }
      try {
        const room = await getMatchmakingRoom(
          hostedRoom.form.signalUrl,
          hostedRoom.roomId,
        );
        if (disposed) {
          return;
        }
        if (room.status === 'open') {
          return;
        }
        hostedRoomLaunchBusyRef.current = true;
        matchmakingActionBusyRef.current = true;
        setMatchmakingActionBusy(true);
        setHostedRoom(null);
        setActivityStatus({
          text: '参加者を検出しました。接続を確立してからmelonDSを起動します',
          kind: 'idle',
        });
        await startMatchForRef.current(hostedRoom.form);
      } catch (error) {
        if (!disposed) {
          setHostedRoom(null);
          setActivityStatus({ text: String(error), kind: 'error' });
        }
      } finally {
        hostedRoomLaunchBusyRef.current = false;
        matchmakingActionBusyRef.current = false;
        setMatchmakingActionBusy(false);
      }
    };

    void pollHostedRoom();
    const timer = window.setInterval(() => void pollHostedRoom(), 2000);
    return () => {
      disposed = true;
      window.clearInterval(timer);
    };
  }, [hostedRoom, connectionActive]);

  const stopMatch = async () => {
    try {
      await stopMatchCommand();
      setGameStateMismatch(null);
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
      setHostedRoom(null);
      await queryClient.invalidateQueries({ queryKey: ['matchmakingRooms'] });
      setActivityStatus({ text: '部屋を閉じました', kind: 'warn' });
    } catch (error) {
      setActivityStatus({ text: String(error), kind: 'error' });
    } finally {
      matchmakingActionBusyRef.current = false;
      setMatchmakingActionBusy(false);
    }
  };

  const refreshRooms = async () => {
    try {
      await roomsQuery.refetch();
    } catch (error) {
      setActivityStatus({ text: String(error), kind: 'error' });
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
    joinRoom,
    openLogDir,
    openMelonds,
    openMelondsInputConfig,
    pollStatus,
    preflightCheck,
    prepareRoms,
    refreshRooms,
    selectBaseRomAndPrepare,
    selectRomPath,
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
    activityStatus,
    form,
    lastLogDir,
    gameStateMismatch,
    matchmakingRooms: {
      rooms: roomsQuery.data ?? [],
      loading: roomsQuery.isFetching,
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
      error: roomsQuery.error ? String(roomsQuery.error) : null,
      hostedRoomId: hostedRoom?.roomId ?? null,
    },
    onboarding: {
      loaded: defaultsLoaded,
      romsPrepared: onboardingRomsPrepared,
      romGenerationBusy,
      inputConfigOpened: onboardingInputConfigOpened,
    },
    romStatus:
      romEnsureBusy || romGenerationBusy
        ? { text: 'ROM生成中', kind: 'idle' as StatusKind }
        : null,
    summary,
    updateBusy,
    updateStatus,
    updateField,
  };
}
