import { relaunch } from '@tauri-apps/plugin-process';
import { check } from '@tauri-apps/plugin-updater';
import { useCallback, useEffect, useMemo, useState } from 'react';
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
  View,
} from './types';

export function useLauncherController() {
  const [activeView, setActiveView] = useState<View>(() =>
    window.location.hash === '#settings' ? 'settings' : 'battle',
  );
  const [form, setForm] = useState<FormState>(initialForm);
  const [status, setStatus] = useState({
    text: '初期化中',
    kind: 'idle' as StatusKind,
  });
  const [lastLogDir, setLastLogDir] = useState('');
  const [bridgeDiagnostics, setBridgeDiagnostics] =
    useState<BridgeDiagnostics | null>(null);
  const [defaultsLoaded, setDefaultsLoaded] = useState(false);
  const [romPreparation, setRomPreparation] = useState('未確認');
  const [updateBusy, setUpdateBusy] = useState(false);

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
    status.text.startsWith('実行中') || status.text.startsWith('起動済み');
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
        setStatus({
          text: `プロセス終了 melonDS:${response.melon ?? '-'} bridge:${response.bridge ?? '-'}`,
          kind: 'error',
        });
        return;
      }
      if (!response.active) {
        setStatus({ text: '未接続', kind: 'idle' });
        return;
      }
      if (response.diagnostics_error) {
        setStatus({ text: response.diagnostics_error, kind: 'warn' });
        return;
      }
      setStatus({
        text: `実行中 melonDS:${response.melon ?? '-'} bridge:${response.bridge ?? '-'}`,
        kind: 'ok',
      });
    } catch {
      setStatus({ text: '状態取得に失敗しました', kind: 'warn' });
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
          setStatus({ text: String(error), kind: 'error' });
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
        setStatus({ text: String(error), kind: 'warn' });
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
      setStatus({ text: String(error), kind: 'error' });
    }
  };

  const preflightCheck = async () => {
    try {
      setStatus({ text: '起動前チェック中', kind: 'idle' });
      const response = await runPreflightCheck();
      console.info('preflight', response);
      setStatus({ text: '起動前チェック OK', kind: 'ok' });
    } catch (error) {
      setStatus({ text: String(error), kind: 'error' });
    }
  };

  const prepareRoms = async () => {
    const nextForm = withRequiredSeed(form);
    if (nextForm.matchSeed !== form.matchSeed) {
      setForm(nextForm);
    }
    const stage = selectedStageFrom(nextForm.courseMode, nextForm.matchSeed);
    if (stage === null) {
      setStatus({
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
      setStatus({ text: '共通 ROM を準備中', kind: 'idle' });
      const response = await generateRoms(request);
      setForm((current) => ({
        ...current,
        hostRomPath: response.host_rom,
        clientRomPath: response.client_rom,
      }));
      setRomPreparation('準備済み');
      setStatus({ text: '共通 ROM の準備が完了しました', kind: 'ok' });
    } catch (error) {
      setStatus({ text: String(error), kind: 'error' });
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
      setStatus({
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
      setStatus({ text: '共通 ROM を確認中', kind: 'idle' });
      const roms = await ensurePreparedRoms(nextForm, stage);
      request.rom_path =
        nextForm.role === 'host' ? roms.host_rom : roms.client_rom;
      setStatus({ text: `起動中 stage=${stage}`, kind: 'idle' });
      const response = await startMatchCommand(request);
      setLastLogDir(response.log_dir);
      setStatus({
        text: `起動済み melonDS:${response.melon_pid} bridge:${response.bridge_pid}`,
        kind: 'ok',
      });
    } catch (error) {
      setStatus({ text: String(error), kind: 'error' });
    }
  };

  const stopMatch = async () => {
    try {
      await stopMatchCommand();
      setStatus({ text: '停止しました', kind: 'warn' });
    } catch (error) {
      setStatus({ text: String(error), kind: 'error' });
    }
  };

  const openLogDir = async () => {
    if (!lastLogDir) {
      return;
    }
    try {
      await openLogDirCommand(lastLogDir);
    } catch (error) {
      setStatus({ text: String(error), kind: 'error' });
    }
  };

  const openMelonds = async () => {
    try {
      const pid = await openMelondsCommand();
      setStatus({ text: `melonDS を起動しました pid:${pid}`, kind: 'ok' });
    } catch (error) {
      setStatus({ text: String(error), kind: 'error' });
    }
  };

  const openMelondsInputConfig = async () => {
    try {
      const pid = await openMelondsInputConfigCommand();
      setStatus({
        text: `melonDS の入力設定を開きました pid:${pid}`,
        kind: 'ok',
      });
    } catch (error) {
      setStatus({ text: String(error), kind: 'error' });
    }
  };

  const copyRoomCode = async () => {
    try {
      await navigator.clipboard.writeText(form.roomCode);
      setStatus({ text: '部屋コードをコピーしました', kind: 'ok' });
    } catch {
      setStatus({ text: '部屋コードのコピーに失敗しました', kind: 'warn' });
    }
  };

  const checkForUpdate = async () => {
    try {
      setUpdateBusy(true);
      setStatus({ text: '更新を確認中', kind: 'idle' });
      const update = await check();
      if (!update) {
        setStatus({ text: '利用可能な更新はありません', kind: 'ok' });
        return;
      }
      setStatus({
        text: `v${update.version} をインストール中`,
        kind: 'idle',
      });
      await update.downloadAndInstall((event) => {
        if (event.event === 'Started') {
          setStatus({
            text: `更新をダウンロード中 (${event.data.contentLength ?? '不明'} bytes)`,
            kind: 'idle',
          });
        }
        if (event.event === 'Progress') {
          setStatus({
            text: `更新をダウンロード中 (+${event.data.chunkLength} bytes)`,
            kind: 'idle',
          });
        }
        if (event.event === 'Finished') {
          setStatus({ text: '更新をインストールしました', kind: 'ok' });
        }
      });
      await relaunch();
    } catch (error) {
      setStatus({ text: String(error), kind: 'error' });
    } finally {
      setUpdateBusy(false);
    }
  };

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
    form,
    lastLogDir,
    status,
    summary,
    updateBusy,
    updateField,
  };
}
