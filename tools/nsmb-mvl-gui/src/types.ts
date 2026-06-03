export type Role = 'host' | 'client';
export type CourseMode = 'random' | 'select';
export type Lives = '3' | '5' | 'endless';
export type StatusKind = 'idle' | 'ok' | 'warn' | 'error';

export type Defaults = {
  signal_url: string;
  room_code: string;
  host_rom_path: string;
  client_rom_path: string;
  base_rom_path: string;
  port: number;
};

export type GameSettings = {
  course_mode: CourseMode;
  wins: number;
  big_stars: number;
  lives: Lives;
  match_seed: string;
};

export type LaunchRequest = {
  role: Role;
  signal_url: string;
  room_code: string;
  port: number;
  rom_path: string;
  settings: GameSettings;
};

export type GenerateRomRequest = {
  source_rom: string;
  host_rom: string;
  client_rom: string;
  stage: number;
  settings: GameSettings;
};

export type SaveRomPathsRequest = {
  host_rom_path: string;
  client_rom_path: string;
  base_rom_path: string;
};

export type LaunchResponse = {
  log_dir: string;
  melon_pid: number;
  bridge_pid: number;
};

export type GenerateRomResponse = {
  host_rom: string;
  client_rom: string;
  generated: boolean;
};

export type SessionStatus = {
  active: boolean;
  log_dir?: string;
  melon?: string;
  bridge?: string;
  webrtc?: BridgeDiagnostics;
  diagnostics_error?: string;
};

export type BridgeDiagnostics = {
  role?: string;
  phase?: string;
  signal_url?: string;
  session?: string;
  ice_servers?: string[];
  connection_state?: string;
  gathering_state?: string;
  ice_state?: string;
  selected_candidate_pair?: {
    route?: string;
    local_type?: string;
    remote_type?: string;
    local?: string;
    remote?: string;
    local_address?: string;
    remote_address?: string;
  };
  stats?: {
    app_to_webrtc_packets?: number;
    app_to_webrtc_bytes?: number;
    webrtc_to_app_packets?: number;
    webrtc_to_app_bytes?: number;
    dropped_no_local_target?: number;
  };
  last_error?: string;
};

export type PreflightResponse = {
  melonds_path: string;
  bridge_path: string;
  input_script: string;
  symbols_file: string;
  bridge_smoke: string;
};

export type FormState = {
  role: Role;
  signalUrl: string;
  roomCode: string;
  port: number;
  hostRomPath: string;
  clientRomPath: string;
  baseRomPath: string;
  courseMode: CourseMode;
  wins: number;
  bigStars: number;
  lives: Lives;
  matchSeed: string;
};
