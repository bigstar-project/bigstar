import { invoke } from '@tauri-apps/api/core';
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
  return invoke<Defaults>('get_defaults');
}

export function saveRomPaths(request: SaveRomPathsRequest) {
  if (!isTauriRuntime()) {
    return Promise.resolve(request);
  }
  return invoke('save_rom_paths', { request });
}

export function selectRomFile(currentPath: string) {
  if (!isTauriRuntime()) {
    return Promise.resolve(currentPath || null);
  }
  return invoke<string | null>('select_rom_file', { currentPath });
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
  return invoke<PreflightResponse>('preflight_check');
}

export function generateRoms(request: GenerateRomRequest) {
  if (!isTauriRuntime()) {
    return Promise.resolve<GenerateRomResponse>({
      host_rom: request.host_rom,
      client_rom: request.client_rom,
      generated: true,
    });
  }
  return invoke<GenerateRomResponse>('generate_roms', { request });
}

export function ensureRoms(request: GenerateRomRequest) {
  if (!isTauriRuntime()) {
    return Promise.resolve<GenerateRomResponse>({
      host_rom: request.host_rom,
      client_rom: request.client_rom,
      generated: false,
    });
  }
  return invoke<GenerateRomResponse>('ensure_roms', { request });
}

export function startMatch(request: LaunchRequest) {
  if (!isTauriRuntime()) {
    return Promise.resolve<LaunchResponse>({
      log_dir: 'preview',
      melon_pid: request.role === 'host' ? 1001 : 1002,
      bridge_pid: 2001,
    });
  }
  return invoke<LaunchResponse>('start_match', { request });
}

export function stopMatch() {
  if (!isTauriRuntime()) {
    return Promise.resolve();
  }
  return invoke('stop_match');
}

export function getSessionStatus() {
  if (!isTauriRuntime()) {
    return Promise.resolve<SessionStatus>({
      active: false,
      log_dir: '',
    });
  }
  return invoke<SessionStatus>('session_status');
}

export function openLogDir(path: string) {
  if (!isTauriRuntime()) {
    return Promise.resolve(path);
  }
  return invoke('open_log_dir', { path });
}
