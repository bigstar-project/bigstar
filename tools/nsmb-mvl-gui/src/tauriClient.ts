import { commands } from './bindings';
import type {
  Defaults,
  GenerateRomRequest,
  GenerateRomResponse,
  LaunchRequest,
  LaunchResponse,
  MatchHistoryRecord,
  PreflightResponse,
  SaveDiagnosticEventsRequest,
  SavePlayerNameRequest,
  SaveRomPathsRequest,
  SessionStatus,
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

const previewDefaults: Defaults = {
  signal_url: 'wss://nsmb-mvl-signaling-prod.uniunntaro.workers.dev/session',
  room_code: 'test-room',
  host_rom_path:
    'C:\\Users\\Sugiyama\\AppData\\Roaming\\dev.melonds.nsmb-mvl\\roms\\nsmb-mvl-host.nds',
  client_rom_path:
    'C:\\Users\\Sugiyama\\AppData\\Roaming\\dev.melonds.nsmb-mvl\\roms\\nsmb-mvl-client.nds',
  base_rom_path: '',
  player_name: '',
  roms_prepared_once: false,
  input_config_opened_once: false,
  diagnostic_events_enabled: false,
  port: 8165,
};

const readyPreviewDefaults: Defaults = {
  ...previewDefaults,
  base_rom_path: 'C:\\Users\\Sugiyama\\roms\\New Super Mario Bros.nds',
  player_name: 'Preview Player',
  roms_prepared_once: true,
  input_config_opened_once: true,
};

const previewRomIdentity = {
  client_rom_sha256:
    '2222222222222222222222222222222222222222222222222222222222222222',
  generator_id:
    '3333333333333333333333333333333333333333333333333333333333333333',
  host_rom_sha256:
    '1111111111111111111111111111111111111111111111111111111111111111',
  rom_pair_id:
    '4444444444444444444444444444444444444444444444444444444444444444',
};

const previewMatchHistoryKey = 'nsmb-mvl-preview-match-history';

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

export function savePlayerName(request: SavePlayerNameRequest) {
  if (!isTauriRuntime()) {
    return Promise.resolve(null);
  }
  return unwrapCommand(commands.savePlayerName(request));
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
      return Promise.resolve<MatchHistoryRecord[]>(raw ? JSON.parse(raw) : []);
    } catch {
      return Promise.resolve<MatchHistoryRecord[]>([]);
    }
  }
  return unwrapCommand(commands.loadMatchHistory());
}

export function saveMatchHistory(matches: MatchHistoryRecord[]) {
  if (!isTauriRuntime()) {
    window.localStorage.setItem(
      previewMatchHistoryKey,
      JSON.stringify(matches.slice(0, 100)),
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
