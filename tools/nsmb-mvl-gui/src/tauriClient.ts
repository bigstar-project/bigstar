import { commands } from './bindings';
import {
  previewHistoryDashboard,
  previewHistoryOpponents,
  queryPreviewMatchHistory,
} from './matchHistory';
import {
  previewDefaults,
  previewMatchHistory,
  previewMatchHistoryKey,
  previewRomIdentity,
  readyPreviewDefaults,
} from './previewData';
import type {
  CleanupDetailedLogsResponse,
  GenerateRomRequest,
  GenerateRomResponse,
  LaunchRequest,
  LaunchResponse,
  LogArchiveResponse,
  MatchHistoryFilter,
  MatchHistoryPageRequest,
  MatchHistoryRecord,
  PreflightResponse,
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
} from './types';

async function unwrapCommand<T>(
  result: Promise<
    { status: 'ok'; data: T } | { status: 'error'; error: string }
  >,
) {
  const response = await result;
  if (response.status === 'error') {
    throw response.error;
  }
  return response.data;
}

function isTauriRuntime() {
  return '__TAURI_INTERNALS__' in window;
}

function previewScenario() {
  const search =
    typeof window.location?.search === 'string' ? window.location.search : '';
  const value = new URLSearchParams(search).get('preview');
  return value === 'ready' || value === 'main' ? 'ready' : 'onboarding';
}

function previewDefaultsForCurrentUrl() {
  return previewScenario() === 'ready' ? readyPreviewDefaults : previewDefaults;
}

export function getDefaults() {
  if (!isTauriRuntime()) {
    return Promise.resolve(previewDefaultsForCurrentUrl());
  }
  return unwrapCommand(commands.getDefaults());
}

export function saveRomPaths(request: SaveRomPathsRequest) {
  if (!isTauriRuntime()) {
    return Promise.resolve(null);
  }
  return unwrapCommand(commands.saveRomPaths(request));
}

export function saveDiagnosticEventsEnabled(
  request: SaveDiagnosticEventsRequest,
) {
  if (!isTauriRuntime()) {
    return Promise.resolve(null);
  }
  return unwrapCommand(commands.saveDiagnosticEventsEnabled(request));
}

export function saveDetailedLogsEnabled(request: SaveDetailedLogsRequest) {
  if (!isTauriRuntime()) {
    return Promise.resolve(null);
  }
  return unwrapCommand(commands.saveDetailedLogsEnabled(request));
}

export function savePerformanceLogsEnabled(
  request: SavePerformanceLogsRequest,
) {
  if (!isTauriRuntime()) {
    return Promise.resolve(null);
  }
  return unwrapCommand(commands.savePerformanceLogsEnabled(request));
}

export function saveNewRoomNotificationsEnabled(
  request: SaveNewRoomNotificationsRequest,
) {
  if (!isTauriRuntime()) {
    return Promise.resolve(null);
  }
  return unwrapCommand(commands.saveNewRoomNotificationsEnabled(request));
}

export function savePlayerName(request: SavePlayerNameRequest) {
  if (!isTauriRuntime()) {
    return Promise.resolve(null);
  }
  return unwrapCommand(commands.savePlayerName(request));
}

export function showNewRoomNotification(
  request: ShowNewRoomNotificationRequest,
) {
  if (!isTauriRuntime()) {
    return Promise.resolve(false);
  }
  return unwrapCommand(commands.showNewRoomNotification(request));
}

export function getStartupEnabled() {
  if (!isTauriRuntime()) {
    return Promise.resolve(false);
  }
  return unwrapCommand(commands.getStartupEnabled());
}

export function setStartupEnabled(enabled: boolean) {
  if (!isTauriRuntime()) {
    return Promise.resolve(null);
  }
  return unwrapCommand(commands.setStartupEnabled(enabled));
}

export function selectRomFile(currentPath: string) {
  if (!isTauriRuntime()) {
    return Promise.resolve(currentPath || null);
  }
  return unwrapCommand(commands.selectRomFile(currentPath));
}

export function runPreflightCheck() {
  if (!isTauriRuntime()) {
    return Promise.resolve<PreflightResponse>({
      melonds_path: 'preview',
      bridge_path: 'preview',
      input_script: 'preview',
      symbols_file: 'preview',
      bridge_smoke: 'ok',
    });
  }
  return unwrapCommand(commands.preflightCheck());
}

export function generateRoms(request: GenerateRomRequest) {
  if (!isTauriRuntime()) {
    const defaults = previewDefaultsForCurrentUrl();
    return Promise.resolve<GenerateRomResponse>({
      host_rom: defaults.host_rom_path,
      client_rom: defaults.client_rom_path,
      generated: true,
      rom_identity: previewRomIdentity,
    });
  }
  return unwrapCommand(commands.generateRoms(request));
}

export function ensureRoms(request: GenerateRomRequest) {
  if (!isTauriRuntime()) {
    const defaults = previewDefaultsForCurrentUrl();
    return Promise.resolve<GenerateRomResponse>({
      host_rom: defaults.host_rom_path,
      client_rom: defaults.client_rom_path,
      generated: false,
      rom_identity: previewRomIdentity,
    });
  }
  return unwrapCommand(commands.ensureRoms(request));
}

export function startMatch(request: LaunchRequest) {
  if (!isTauriRuntime()) {
    return Promise.resolve<LaunchResponse>({
      log_dir: 'preview',
      melon_pid: request.role === 'host' ? 1001 : 1002,
      bridge_pid: 2001,
    });
  }
  return unwrapCommand(commands.startMatch(request));
}

export function stopMatch() {
  if (!isTauriRuntime()) {
    return Promise.resolve();
  }
  return unwrapCommand(commands.stopMatch());
}

export function getSessionStatus() {
  if (!isTauriRuntime()) {
    return Promise.resolve<SessionStatus>({
      active: false,
      log_dir: null,
      melon: null,
      bridge: null,
      webrtc: null,
      diagnostics_error: null,
      game_state_mismatch: null,
      mvl_results: [],
    });
  }
  return unwrapCommand(commands.sessionStatus());
}

function storedPreviewMatchHistory() {
  try {
    const raw = window.localStorage.getItem(previewMatchHistoryKey);
    if (raw) return JSON.parse(raw) as MatchHistoryRecord[];
  } catch {
    // 壊れたプレビューデータはサンプル履歴へフォールバックする。
  }
  return previewScenario() === 'ready' ? previewMatchHistory() : [];
}

function storePreviewMatchHistory(matches: MatchHistoryRecord[]) {
  window.localStorage.setItem(previewMatchHistoryKey, JSON.stringify(matches));
}

export function upsertMatchHistory(record: MatchHistoryRecord) {
  if (!isTauriRuntime()) {
    const stored = storedPreviewMatchHistory();
    storePreviewMatchHistory([
      record,
      ...stored.filter((match) => match.id !== record.id),
    ]);
    return Promise.resolve(null);
  }
  return unwrapCommand(commands.upsertMatchHistory(record));
}

export function deleteMatchHistory(matchId: string) {
  if (!isTauriRuntime()) {
    storePreviewMatchHistory(
      storedPreviewMatchHistory().filter((match) => match.id !== matchId),
    );
    return Promise.resolve(null);
  }
  return unwrapCommand(commands.deleteMatchHistory(matchId));
}

export function queryMatchHistory(request: MatchHistoryPageRequest) {
  if (!isTauriRuntime()) {
    return Promise.resolve(
      queryPreviewMatchHistory(storedPreviewMatchHistory(), request),
    );
  }
  return unwrapCommand(commands.queryMatchHistory(request));
}

export function loadMatchHistoryDashboard(filter: MatchHistoryFilter) {
  if (!isTauriRuntime()) {
    return Promise.resolve(
      previewHistoryDashboard(storedPreviewMatchHistory(), filter),
    );
  }
  return unwrapCommand(commands.loadMatchHistoryDashboard(filter));
}

export function loadMatchHistoryOpponents() {
  if (!isTauriRuntime()) {
    return Promise.resolve(
      previewHistoryOpponents(storedPreviewMatchHistory()),
    );
  }
  return unwrapCommand(commands.loadMatchHistoryOpponents());
}

export function openLogDir(path: string) {
  if (!isTauriRuntime()) {
    return Promise.resolve(null);
  }
  return unwrapCommand(commands.openLogDir(path));
}

export function createLogArchive(logDir: string) {
  if (!isTauriRuntime()) {
    return Promise.resolve<LogArchiveResponse>({
      archive_path: `${logDir}\\nsmb-mvl-logs-preview.zip`,
      size: 1024,
    });
  }
  return unwrapCommand(commands.createLogArchive(logDir));
}

export function cleanupDetailedLogs() {
  if (!isTauriRuntime()) {
    return Promise.resolve<CleanupDetailedLogsResponse>({
      scanned_log_dirs: 4,
      skipped_active_log_dirs: 0,
      deleted_files: 12,
      deleted_dirs: 4,
      freed_bytes: 64 * 1024 * 1024,
    });
  }
  return unwrapCommand(commands.cleanupDetailedLogs());
}

export function uploadLogArchive(request: UploadLogArchiveRequest) {
  if (!isTauriRuntime()) {
    return Promise.resolve<UploadLogArchiveResponse>({
      archive_path: `${request.log_dir}\\nsmb-mvl-logs-preview.zip`,
      key: 'log-archives/preview/nsmb-mvl-logs-preview.zip',
      size: 1024,
    });
  }
  return unwrapCommand(commands.uploadLogArchive(request));
}

export function openMelonds() {
  if (!isTauriRuntime()) {
    return Promise.resolve(3001);
  }
  return unwrapCommand(commands.openMelonds());
}

export function openMelondsInputConfig() {
  if (!isTauriRuntime()) {
    return Promise.resolve(3002);
  }
  return unwrapCommand(commands.openMelondsInputConfig());
}
