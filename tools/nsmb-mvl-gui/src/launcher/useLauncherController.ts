import { relaunch } from '@tauri-apps/plugin-process';
import { check } from '@tauri-apps/plugin-updater';
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import {
  currentSettings,
  generateSeed,
  initialForm,
  processExited,
  selectedStageFrom,
  withRequiredSeed,
} from '../form';
import {
  ensureRoms,
  generateRoms,
  getDefaults,
  getSessionStatus,
  openLogDir as openLogDirCommand,
  openMelonds as openMelondsCommand,
  openMelondsInputConfig as openMelondsInputConfigCommand,
  runPreflightCheck,
  saveRomPaths,
  selectRomFile,
  startMatch as startMatchCommand,
  stopMatch as stopMatchCommand,
} from '../tauriClient';
import type {
  BridgeDiagnostics,
  FormState,
  GenerateRomRequest,
  LaunchRequest,
  SaveRomPathsRequest,
  StatusKind,
} from '../types';
import type {
  LauncherActions,
  LauncherSummary,
  SelectRomKey,
  UpdateStatus,
  View,
} from './types';

const UPDATE_CHECK_INTERVAL_MS = 5 * 60 * 1000;
const ACTIVITY_STATUS_VISIBLE_MS = 5000;

function isTauriRuntime() {
  return '__TAURI_INTERNALS__' in window;
}

export function useLauncherController() {
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
  const [defaultsLoaded, setDefaultsLoaded] = useState(false);
  const [romPreparation, setRomPreparation] = useState('未確認');
  const [updateBusy, setUpdateBusy] = useState(false);
  const [updateStatus, setUpdateStatus] = useState<UpdateStatus>({
    phase: 'idle',
  });
  const availableUpdateRef = useRef<Awaited<ReturnType<typeof check>>>(null);
  const updateBusyRef = useRef(false);
  const updatePhaseRef = useRef<UpdateStatus['phase']>('idle');

  const currentRomPath =
    form.role === 'host' ? form.hostRomPath : form.clientRomPath;
  const selectedStage = useMemo(
    () => selectedStageFrom(form.courseMode, form.matchSeed),
    [form.courseMode, form.matchSeed],
  );
  const selectedStageLabel =
    selectedStage === null
      ? form.courseMode === 'random'
        ? 'seed未設定'
        : '0'
      : String(selectedStage);
  const courseNote =
    form.courseMode === 'select'
      ? 'Choose Each Time は direct route では未対応のため、現在は固定 stage 0 で起動します。'
      : 'Match seed から stage 0-4 を決め、起動時に生成済み共通 ROM へ渡します。';
  const connectionActive =
    connectionStatus.text.startsWith('実行中') ||
    connectionStatus.text.startsWith('起動済み');
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
  };

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
    setForm((current) => ({ ...current, [key]: value }));
  };

  const pollStatus = useCallback(async () => {
    try {
      const response = await getSessionStatus();
      if (response.log_dir) {
        setLastLogDir(response.log_dir);
      }
      setBridgeDiagnostics(response.webrtc ?? null);
      if (processExited(response.melon) || processExited(response.bridge)) {
        setConnectionStatus({
          text: `プロセス終了 melonDS:${response.melon ?? '-'} bridge:${response.bridge ?? '-'}`,
          kind: 'error',
        });
        return;
      }
      if (!response.active) {
        setConnectionStatus({ text: '未接続', kind: 'idle' });
        return;
      }
      if (response.diagnostics_error) {
        setConnectionStatus({ text: response.diagnostics_error, kind: 'warn' });
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
        setForm({
          role: 'host',
          signalUrl: defaults.signal_url,
          roomCode: defaults.room_code,
          port: defaults.port,
          hostRomPath: defaults.host_rom_path,
          clientRomPath: defaults.client_rom_path,
          baseRomPath: defaults.base_rom_path,
          courseMode: 'random',
          wins: 2,
          bigStars: 5,
          lives: 'endless',
          matchSeed: String(generateSeed()),
        });
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
      host_rom_path: form.hostRomPath,
      client_rom_path: form.clientRomPath,
      base_rom_path: form.baseRomPath,
    };
    const timer = window.setTimeout(() => {
      void saveRomPaths(request).catch((error) => {
        setActivityStatus({ text: String(error), kind: 'warn' });
      });
    }, 250);
    return () => window.clearTimeout(timer);
  }, [defaultsLoaded, form.hostRomPath, form.clientRomPath, form.baseRomPath]);

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

  const prepareRoms = async () => {
    const nextForm = withRequiredSeed(form);
    if (nextForm.matchSeed !== form.matchSeed) {
      setForm(nextForm);
    }
    const stage = selectedStageFrom(nextForm.courseMode, nextForm.matchSeed);
    if (stage === null) {
      setActivityStatus({
        text: 'Match seed は10進数、または 0x から始まる16進数で指定してください',
        kind: 'error',
      });
      return;
    }

    const request: GenerateRomRequest = {
      source_rom: nextForm.baseRomPath,
      host_rom: nextForm.hostRomPath,
      client_rom: nextForm.clientRomPath,
      stage,
      settings: currentSettings(nextForm),
    };

    try {
      setActivityStatus({ text: '共通 ROM を準備中', kind: 'idle' });
      const response = await generateRoms(request);
      setForm((current) => ({
        ...current,
        hostRomPath: response.host_rom,
        clientRomPath: response.client_rom,
      }));
      setRomPreparation('準備済み');
      setActivityStatus({ text: '共通 ROM の準備が完了しました', kind: 'ok' });
    } catch (error) {
      setActivityStatus({ text: String(error), kind: 'error' });
    }
  };

  const ensurePreparedRoms = async (nextForm: FormState, stage: number) => {
    const request: GenerateRomRequest = {
      source_rom: nextForm.baseRomPath,
      host_rom: nextForm.hostRomPath,
      client_rom: nextForm.clientRomPath,
      stage,
      settings: currentSettings(nextForm),
    };
    const response = await ensureRoms(request);
    setForm((current) => ({
      ...current,
      hostRomPath: response.host_rom,
      clientRomPath: response.client_rom,
    }));
    setRomPreparation(response.generated ? '初回準備済み' : '再利用');
    return response;
  };

  const startMatch = async () => {
    const nextForm = withRequiredSeed(form);
    if (nextForm.matchSeed !== form.matchSeed) {
      setForm(nextForm);
    }
    const stage = selectedStageFrom(nextForm.courseMode, nextForm.matchSeed);
    if (stage === null) {
      setActivityStatus({
        text: 'Match seed は10進数、または 0x から始まる16進数で指定してください',
        kind: 'error',
      });
      return;
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
    };

    try {
      setActivityStatus({ text: '共通 ROM を確認中', kind: 'idle' });
      const roms = await ensurePreparedRoms(nextForm, stage);
      request.rom_path =
        nextForm.role === 'host' ? roms.host_rom : roms.client_rom;
      setActivityStatus({ text: `起動中 stage=${stage}`, kind: 'idle' });
      const response = await startMatchCommand(request);
      setLastLogDir(response.log_dir);
      setActivityStatus({
        text: `起動済み melonDS:${response.melon_pid} bridge:${response.bridge_pid}`,
        kind: 'ok',
      });
    } catch (error) {
      setActivityStatus({ text: String(error), kind: 'error' });
    }
  };

  const stopMatch = async () => {
    try {
      await stopMatchCommand();
      setActivityStatus({ text: '停止しました', kind: 'warn' });
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
    checkForUpdate,
    copyRoomCode,
    openLogDir,
    openMelonds,
    openMelondsInputConfig,
    pollStatus,
    preflightCheck,
    prepareRoms,
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
    summary,
    updateBusy,
    updateStatus,
    updateField,
  };
}
