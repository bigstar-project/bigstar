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

export function getDefaults() {
  return invoke<Defaults>('get_defaults');
}

export function saveRomPaths(request: SaveRomPathsRequest) {
  return invoke('save_rom_paths', { request });
}

export function selectRomFile(currentPath: string) {
  return invoke<string | null>('select_rom_file', { currentPath });
}

export function runPreflightCheck() {
  return invoke<PreflightResponse>('preflight_check');
}

export function generateRoms(request: GenerateRomRequest) {
  return invoke<GenerateRomResponse>('generate_roms', { request });
}

export function ensureRoms(request: GenerateRomRequest) {
  return invoke<GenerateRomResponse>('ensure_roms', { request });
}

export function startMatch(request: LaunchRequest) {
  return invoke<LaunchResponse>('start_match', { request });
}

export function stopMatch() {
  return invoke('stop_match');
}

export function getSessionStatus() {
  return invoke<SessionStatus>('session_status');
}

export function openLogDir(path: string) {
  return invoke('open_log_dir', { path });
}
