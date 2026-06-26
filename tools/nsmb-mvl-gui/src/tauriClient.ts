import { commands } from './bindings';
import { maxMatchHistoryRecords } from './matchHistory';
import {
  previewDefaults,
  previewMatchHistory,
  previewMatchHistoryKey,
  previewRomIdentity,
  readyPreviewDefaults,
} from './previewData';
import type {
  GenerateRomRequest,
  GenerateRomResponse,
  LaunchRequest,
  LaunchResponse,
  MatchHistoryRecord,
  PreflightResponse,
  SaveDiagnosticEventsRequest,
  SaveNewRoomNotificationsRequest,
  SavePlayerNameRequest,
  SaveRomPathsRequest,
  SessionStatus,
  ShowNewRoomNotificationRequest,
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

export function loadMatchHistory() {
  if (!isTauriRuntime()) {
    try {
      const raw = window.localStorage.getItem(previewMatchHistoryKey);
      const stored = raw ? (JSON.parse(raw) as MatchHistoryRecord[]) : [];
      if (stored.length > 0 || previewScenario() !== 'ready') {
        return Promise.resolve<MatchHistoryRecord[]>(stored);
      }
      return Promise.resolve<MatchHistoryRecord[]>(previewMatchHistory());
    } catch {
      return Promise.resolve<MatchHistoryRecord[]>(
        previewScenario() === 'ready' ? previewMatchHistory() : [],
      );
    }
  }
  return unwrapCommand(commands.loadMatchHistory());
}

export function saveMatchHistory(matches: MatchHistoryRecord[]) {
  if (!isTauriRuntime()) {
    window.localStorage.setItem(
      previewMatchHistoryKey,
      JSON.stringify(matches.slice(0, maxMatchHistoryRecords)),
    );
    return Promise.resolve(null);
  }
  return unwrapCommand(commands.saveMatchHistory(matches));
}

export function openLogDir(path: string) {
  if (!isTauriRuntime()) {
    return Promise.resolve(null);
  }
  return unwrapCommand(commands.openLogDir(path));
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
