import { commands } from './bindings';
import type {
  Defaults,
  GenerateRomRequest,
  GenerateRomResponse,
  LaunchRequest,
  LaunchResponse,
  PreflightResponse,
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
  base_rom_path: 'C:\\Users\\Sugiyama\\melon-ds-mario\\roms\\nsmb-us.nds',
  port: 8165,
};

function isTauriRuntime() {
  return '__TAURI_INTERNALS__' in window;
}

export function getDefaults() {
  if (!isTauriRuntime()) {
    return Promise.resolve(previewDefaults);
  }
  return unwrapCommand(commands.getDefaults());
}

export function saveRomPaths(request: SaveRomPathsRequest) {
  if (!isTauriRuntime()) {
    return Promise.resolve(null);
  }
  return unwrapCommand(commands.saveRomPaths(request));
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
    return Promise.resolve<GenerateRomResponse>({
      host_rom: previewDefaults.host_rom_path,
      client_rom: previewDefaults.client_rom_path,
      generated: true,
    });
  }
  return unwrapCommand(commands.generateRoms(request));
}

export function ensureRoms(request: GenerateRomRequest) {
  if (!isTauriRuntime()) {
    return Promise.resolve<GenerateRomResponse>({
      host_rom: previewDefaults.host_rom_path,
      client_rom: previewDefaults.client_rom_path,
      generated: false,
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
    });
  }
  return unwrapCommand(commands.sessionStatus());
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
