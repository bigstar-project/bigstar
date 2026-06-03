import { relaunch } from '@tauri-apps/plugin-process';
import { check } from '@tauri-apps/plugin-updater';
import { useCallback, useEffect, useMemo, useState } from 'react';
import {
  ActionButton,
  FilePathField,
  NumberField,
  RoleButton,
  SelectField,
  TextField,
} from './components/Fields';
import { StatusPill } from './components/StatusPill';
import { SummaryItem } from './components/SummaryItem';
import { WebRtcDiagnosticsPanel } from './components/WebRtcDiagnosticsPanel';
import {
  currentSettings,
  generateSeed,
  initialForm,
  processExited,
  selectedStageFrom,
  withRequiredSeed,
} from './form';
import {
  ensureRoms,
  generateRoms,
  getDefaults,
  getSessionStatus,
  openLogDir as openLogDirCommand,
  runPreflightCheck,
  saveRomPaths,
  selectRomFile,
  startMatch as startMatchCommand,
  stopMatch as stopMatchCommand,
} from './tauriClient';
import type {
  BridgeDiagnostics,
  CourseMode,
  FormState,
  GenerateRomRequest,
  LaunchRequest,
  Lives,
  SaveRomPathsRequest,
  StatusKind,
} from './types';

export function App() {
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

  const selectRomPath = async (
    key: 'hostRomPath' | 'clientRomPath' | 'baseRomPath',
  ) => {
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

  return (
    <main className="mx-auto grid w-[min(1160px,calc(100vw-48px))] gap-6 py-8">
      <header className="flex items-end justify-between gap-6">
        <div>
          <p className="mb-1 text-sm font-bold text-slate-500">
            NSMB Mario vs Luigi
          </p>
          <h1 className="text-3xl font-bold text-slate-950">対戦ランチャー</h1>
        </div>
        <div className="flex items-end gap-3">
          <ActionButton
            kind="secondary"
            type="button"
            disabled={updateBusy}
            onClick={() => void checkForUpdate()}
          >
            更新確認
          </ActionButton>
          <StatusPill kind={status.kind}>{status.text}</StatusPill>
        </div>
      </header>

      <div className="grid grid-cols-[minmax(0,1.4fr)_minmax(320px,0.9fr)] gap-5 max-[860px]:grid-cols-1">
        <form
          className="grid self-start content-start gap-4 rounded-lg border border-slate-200 bg-white p-5 shadow-sm"
          onSubmit={(event) => {
            event.preventDefault();
            void startMatch();
          }}
        >
          <h2 className="text-lg font-bold text-slate-950">接続</h2>
          <div
            className="grid grid-cols-2 gap-1 rounded-lg border border-slate-300 bg-slate-100 p-1"
            role="radiogroup"
          >
            <RoleButton
              active={form.role === 'host'}
              onClick={() => updateField('role', 'host')}
            >
              ホスト
            </RoleButton>
            <RoleButton
              active={form.role === 'client'}
              onClick={() => updateField('role', 'client')}
            >
              参加
            </RoleButton>
          </div>

          <TextField
            label="部屋コード"
            value={form.roomCode}
            maxLength={64}
            onChange={(value) => updateField('roomCode', value)}
          />
          <TextField
            label="シグナリングサーバー URL"
            value={form.signalUrl}
            onChange={(value) => updateField('signalUrl', value)}
          />
          <NumberField
            label="UDP ポート"
            value={form.port}
            min={1}
            max={65535}
            onChange={(value) => updateField('port', value)}
          />

          <h2 className="pt-2 text-lg font-bold text-slate-950">ROM</h2>
          <FilePathField
            label="ホスト用 ROM"
            value={form.hostRomPath}
            onBrowse={() => void selectRomPath('hostRomPath')}
          />
          <FilePathField
            label="参加用 ROM"
            value={form.clientRomPath}
            onBrowse={() => void selectRomPath('clientRomPath')}
          />
          <FilePathField
            label="ベース ROM"
            value={form.baseRomPath}
            onBrowse={() => void selectRomPath('baseRomPath')}
          />

          <div className="mt-1 flex flex-wrap justify-end gap-2">
            <ActionButton
              kind="secondary"
              type="button"
              onClick={() => void preflightCheck()}
            >
              起動前チェック
            </ActionButton>
            <ActionButton
              kind="secondary"
              type="button"
              onClick={() => void prepareRoms()}
            >
              共通ROM再準備
            </ActionButton>
            <ActionButton kind="primary" type="submit">
              開始
            </ActionButton>
            <ActionButton
              kind="secondary"
              type="button"
              onClick={() => void stopMatch()}
            >
              停止
            </ActionButton>
          </div>

          <div className="grid gap-1 pt-1 text-xs font-bold text-slate-500">
            <div className="flex items-center justify-between gap-2">
              <span>Log directory</span>
              <ActionButton
                kind="secondary"
                type="button"
                disabled={!lastLogDir}
                onClick={() => void openLogDir()}
              >
                ログを開く
              </ActionButton>
            </div>
            <code className="overflow-wrap-anywhere rounded-md border border-slate-200 bg-slate-50 px-3 py-2 font-mono text-xs font-semibold text-slate-800">
              {lastLogDir || 'not started'}
            </code>
          </div>
        </form>

        <section className="grid content-start gap-4 rounded-lg border border-slate-200 bg-white p-5 shadow-sm">
          <h2 className="text-lg font-bold text-slate-950">ゲーム設定</h2>
          <SelectField
            label="コース"
            value={form.courseMode}
            onChange={(value) => updateField('courseMode', value as CourseMode)}
          >
            <option value="random">ランダム</option>
            <option value="select">毎回選ぶ</option>
          </SelectField>
          <SelectField
            label="勝利数"
            value={String(form.wins)}
            onChange={(value) => updateField('wins', Number(value))}
          >
            {[1, 2, 3].map((value) => (
              <option key={value} value={value}>
                {value}
              </option>
            ))}
          </SelectField>
          <SelectField
            label="ビッグスター"
            value={String(form.bigStars)}
            onChange={(value) => updateField('bigStars', Number(value))}
          >
            {[3, 5, 10].map((value) => (
              <option key={value} value={value}>
                {value}
              </option>
            ))}
          </SelectField>
          <SelectField
            label="残機"
            value={form.lives}
            onChange={(value) => updateField('lives', value as Lives)}
          >
            <option value="3">3</option>
            <option value="5">5</option>
            <option value="endless">無限</option>
          </SelectField>
          <TextField
            label="Match seed"
            value={form.matchSeed}
            placeholder="ランダム時は空でも自動生成"
            onChange={(value) => updateField('matchSeed', value)}
          />

          <div className="mt-1 grid gap-3 border-t border-slate-200 pt-4">
            <SummaryItem
              label="操作キャラ"
              value={form.role === 'host' ? 'Mario' : 'Luigi'}
            />
            <SummaryItem label="使用 ROM" value={currentRomPath || '未設定'} />
            <SummaryItem label="起動 stage" value={selectedStageLabel} />
            <SummaryItem label="共通 ROM" value={romPreparation} />
            <SummaryItem label="コース処理" value={courseNote} />
          </div>
          <WebRtcDiagnosticsPanel diagnostics={bridgeDiagnostics} />
        </section>
      </div>
    </main>
  );
}
