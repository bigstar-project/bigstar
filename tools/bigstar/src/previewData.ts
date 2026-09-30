import type {
  Defaults,
  MatchHistoryRecord,
  MvlStageResult,
  SessionStatus,
} from './types';

export const previewDefaults: Defaults = {
  signal_url: 'wss://bigstar-signaling-prod.uniunntaro.workers.dev/session',
  room_code: 'test-room',
  host_rom_path:
    'C:\\Users\\Sugiyama\\AppData\\Roaming\\Bigstar Insiders\\roms\\bigstar-host.nds',
  client_rom_path:
    'C:\\Users\\Sugiyama\\AppData\\Roaming\\Bigstar Insiders\\roms\\bigstar-client.nds',
  base_rom_path: '',
  player_name: '',
  player_profile_id: 'preview-profile-player',
  roms_prepared_once: false,
  input_config_opened_once: false,
  diagnostic_events_enabled: false,
  detailed_logs_enabled: false,
  ai_play_log_enabled: false,
  performance_logs_enabled: false,
  new_room_notifications_enabled: true,
  port: 8165,
};

export const readyPreviewDefaults: Defaults = {
  ...previewDefaults,
  base_rom_path: 'C:\\Users\\Sugiyama\\roms\\New Super Mario Bros.nds',
  player_name: 'Preview Player',
  roms_prepared_once: true,
  input_config_opened_once: true,
};

export const previewRomIdentity = {
  client_rom_sha256:
    '2222222222222222222222222222222222222222222222222222222222222222',
  generator_id:
    '3333333333333333333333333333333333333333333333333333333333333333',
  host_rom_sha256:
    '1111111111111111111111111111111111111111111111111111111111111111',
  rom_pair_id:
    '4444444444444444444444444444444444444444444444444444444444444444',
  bridge_sha256:
    '5555555555555555555555555555555555555555555555555555555555555555',
  save_sha256:
    '6666666666666666666666666666666666666666666666666666666666666666',
};

export const previewMatchHistoryKey = 'bigstar-preview-match-history-v4';

const previewMatchSettings = {
  big_stars: 10,
  course_mode: 'select' as const,
  course_stages: [0, 2, 4, 1, 3],
  input_delay_frames: 4,
  input_max_frame_lead: 4,
  lives: '3' as const,
  match_seed: '592814',
  rng_seeds: ['592814', '592815', '592816', '592817', '592818'],
  rollback_enabled: false,
  wins: 3,
};

function previewStageResult({
  frame,
  gameIndex,
  luigiLives,
  luigiMatchWins,
  luigiStars,
  marioLives,
  marioMatchWins,
  marioStars,
  stage,
  targetWins = previewMatchSettings.wins,
  winner,
}: {
  frame: number;
  gameIndex: number;
  luigiLives: number;
  luigiMatchWins: number;
  luigiStars: number;
  marioLives: number;
  marioMatchWins: number;
  marioStars: number;
  stage: number;
  targetWins?: number;
  winner: 0 | 1;
}): MvlStageResult {
  return {
    game_index: gameIndex,
    stage,
    frame,
    winner,
    mario: {
      stars: marioStars,
      displayed_stars: marioStars,
      collected_stars: marioStars,
      lives: marioLives,
      deaths: 3 - marioLives,
      dead: marioLives <= 0,
    },
    luigi: {
      stars: luigiStars,
      displayed_stars: luigiStars,
      collected_stars: luigiStars,
      lives: luigiLives,
      deaths: 3 - luigiLives,
      dead: luigiLives <= 0,
    },
    mario_match_wins: marioMatchWins,
    luigi_match_wins: luigiMatchWins,
    target_wins: targetWins,
    resolved: true,
    line: `preview game=${gameIndex} stage=${stage} winner=${winner}`,
  };
}

export function previewMatchHistory(): MatchHistoryRecord[] {
  return [
    {
      id: 'preview-history-1',
      logDir:
        'C:\\Users\\Sugiyama\\AppData\\Roaming\\Bigstar Insiders\\logs\\preview-2026-06-21-1940',
      playerNames: {
        mario: 'Preview Player',
        luigi: 'Rival',
      },
      playerIds: {
        mario: 'preview-profile-player',
        luigi: 'preview-profile-rival',
      },
      role: 'host',
      roomCode: 'preview-room',
      settings: previewMatchSettings,
      stages: [
        previewStageResult({
          frame: 9420,
          gameIndex: 0,
          luigiLives: 2,
          luigiMatchWins: 0,
          luigiStars: 7,
          marioLives: 3,
          marioMatchWins: 1,
          marioStars: 10,
          stage: 0,
          winner: 0,
        }),
        previewStageResult({
          frame: 8876,
          gameIndex: 1,
          luigiLives: 2,
          luigiMatchWins: 1,
          luigiStars: 10,
          marioLives: 1,
          marioMatchWins: 1,
          marioStars: 8,
          stage: 2,
          winner: 1,
        }),
        previewStageResult({
          frame: 10_238,
          gameIndex: 2,
          luigiLives: 0,
          luigiMatchWins: 1,
          luigiStars: 6,
          marioLives: 2,
          marioMatchWins: 2,
          marioStars: 10,
          stage: 4,
          winner: 0,
        }),
        previewStageResult({
          frame: 9812,
          gameIndex: 3,
          luigiLives: 1,
          luigiMatchWins: 1,
          luigiStars: 9,
          marioLives: 1,
          marioMatchWins: 3,
          marioStars: 10,
          stage: 1,
          winner: 0,
        }),
      ],
      startedAt: '2026-06-21T10:40:00.000Z',
      status: 'completed',
    },
    {
      id: 'preview-history-2',
      logDir:
        'C:\\Users\\Sugiyama\\AppData\\Roaming\\Bigstar Insiders\\logs\\preview-2026-06-20-2318',
      playerNames: {
        mario: 'Alice',
        luigi: 'Preview Player',
      },
      playerIds: {
        mario: 'preview-profile-alice',
        luigi: 'preview-profile-player',
      },
      role: 'client',
      roomCode: 'join-demo',
      settings: {
        ...previewMatchSettings,
        big_stars: 5,
        course_mode: 'random',
        course_stages: [3, 1, 0],
        lives: '5',
        match_seed: '914203',
        rng_seeds: ['914203', '914204', '914205'],
        wins: 2,
      },
      stages: [
        previewStageResult({
          frame: 7210,
          gameIndex: 0,
          luigiLives: 5,
          luigiMatchWins: 1,
          luigiStars: 5,
          marioLives: 3,
          marioMatchWins: 0,
          marioStars: 3,
          stage: 3,
          winner: 1,
        }),
        previewStageResult({
          frame: 6544,
          gameIndex: 1,
          luigiLives: 2,
          luigiMatchWins: 1,
          luigiStars: 4,
          marioLives: 4,
          marioMatchWins: 1,
          marioStars: 5,
          stage: 1,
          winner: 0,
        }),
      ],
      startedAt: '2026-06-20T14:18:00.000Z',
      status: 'stopped',
    },
    {
      id: 'preview-history-3',
      logDir:
        'C:\\Users\\Sugiyama\\AppData\\Roaming\\Bigstar Insiders\\logs\\preview-2026-06-21-1203',
      playerNames: {
        mario: 'Preview Player',
        luigi: 'KoopaKid',
      },
      playerIds: {
        mario: 'preview-profile-player',
        luigi: 'preview-profile-koopa-kid',
      },
      role: 'host',
      roomCode: 'quick-match',
      settings: {
        ...previewMatchSettings,
        course_mode: 'random',
        course_stages: [4, 0, 2],
        match_seed: '120315',
        rng_seeds: ['120315', '120316', '120317'],
        wins: 2,
      },
      stages: [
        previewStageResult({
          frame: 8012,
          gameIndex: 0,
          luigiLives: 2,
          luigiMatchWins: 1,
          luigiStars: 10,
          marioLives: 1,
          marioMatchWins: 0,
          marioStars: 8,
          stage: 4,
          targetWins: 2,
          winner: 1,
        }),
        previewStageResult({
          frame: 9124,
          gameIndex: 1,
          luigiLives: 1,
          luigiMatchWins: 2,
          luigiStars: 10,
          marioLives: 0,
          marioMatchWins: 0,
          marioStars: 6,
          stage: 0,
          targetWins: 2,
          winner: 1,
        }),
      ],
      startedAt: '2026-06-21T03:03:00.000Z',
      status: 'completed',
    },
    {
      id: 'preview-history-4',
      logDir:
        'C:\\Users\\Sugiyama\\AppData\\Roaming\\Bigstar Insiders\\logs\\preview-2026-06-18-2005',
      playerNames: {
        mario: 'TestUser',
        luigi: 'Preview Player',
      },
      playerIds: {
        mario: 'preview-profile-test-user',
        luigi: 'preview-profile-player',
      },
      role: 'client',
      roomCode: 'night-battle',
      settings: {
        ...previewMatchSettings,
        course_stages: [2, 4, 1],
        match_seed: '420050',
        rng_seeds: ['420050', '420051', '420052'],
        wins: 2,
      },
      stages: [
        previewStageResult({
          frame: 7033,
          gameIndex: 0,
          luigiLives: 3,
          luigiMatchWins: 1,
          luigiStars: 10,
          marioLives: 1,
          marioMatchWins: 0,
          marioStars: 7,
          stage: 2,
          targetWins: 2,
          winner: 1,
        }),
        previewStageResult({
          frame: 6908,
          gameIndex: 1,
          luigiLives: 2,
          luigiMatchWins: 2,
          luigiStars: 10,
          marioLives: 0,
          marioMatchWins: 0,
          marioStars: 5,
          stage: 4,
          targetWins: 2,
          winner: 1,
        }),
      ],
      startedAt: '2026-06-18T11:05:00.000Z',
      status: 'completed',
    },
  ];
}

// 対戦画面をブラウザーで確かめるための状態。?previewMatch=live などで選ぶ
export const previewMatchScenarios = [
  'live',
  'reconnecting',
  'timeout',
  'stopped',
  'finished',
  'lost',
] as const;

export type PreviewMatchScenario = (typeof previewMatchScenarios)[number];

const previewCurrentMatchLogDir =
  'C:UsersSugiyamaAppDataRoamingBigstar Insiderslogspreview-current-match';

function previewCurrentStages(scenario: PreviewMatchScenario) {
  const first = previewStageResult({
    frame: 9420,
    gameIndex: 0,
    luigiLives: 0,
    luigiMatchWins: 0,
    luigiStars: 6,
    marioLives: 2,
    marioMatchWins: 1,
    marioStars: 10,
    stage: 0,
    winner: 0,
  });
  if (scenario !== 'finished' && scenario !== 'lost') {
    return [first];
  }
  const won = scenario === 'finished';
  return [
    first,
    previewStageResult({
      frame: 8876,
      gameIndex: 1,
      luigiLives: 1,
      luigiMatchWins: 1,
      luigiStars: 10,
      marioLives: 0,
      marioMatchWins: 1,
      marioStars: 7,
      stage: 1,
      winner: 1,
    }),
    previewStageResult({
      frame: 10_238,
      gameIndex: 2,
      luigiLives: won ? 1 : 2,
      luigiMatchWins: won ? 1 : 2,
      luigiStars: won ? 4 : 10,
      marioLives: won ? 3 : 0,
      marioMatchWins: won ? 2 : 1,
      marioStars: won ? 10 : 6,
      stage: 2,
      winner: won ? 0 : 1,
    }),
    previewStageResult({
      frame: 9812,
      gameIndex: 3,
      luigiLives: 1,
      luigiMatchWins: won ? 1 : 3,
      luigiStars: won ? 8 : 10,
      marioLives: won ? 1 : 0,
      marioMatchWins: won ? 3 : 1,
      marioStars: won ? 10 : 9,
      stage: 4,
      winner: won ? 0 : 1,
    }),
  ];
}

export function previewCurrentMatch(
  scenario: PreviewMatchScenario,
): MatchHistoryRecord {
  return {
    id: 'preview-current-match',
    logDir: previewCurrentMatchLogDir,
    playerIds: {
      mario: 'preview-profile-player',
      luigi: 'preview-profile-rival',
    },
    playerNames: {
      mario: 'Preview Player',
      luigi: 'Rival',
    },
    role: 'host',
    roomCode: 'preview-room',
    settings: {
      ...previewMatchSettings,
      course_mode: 'random',
      course_stages: [0, 1, 2, 4, 3],
    },
    stages: previewCurrentStages(scenario),
    startedAt: '2026-06-21T10:40:00.000Z',
    status:
      scenario === 'finished' || scenario === 'lost'
        ? 'completed'
        : scenario === 'timeout' || scenario === 'stopped'
          ? 'stopped'
          : 'running',
  };
}

let previewRecoveryDeadline: number | null = null;

export function previewSessionStatus(
  scenario: PreviewMatchScenario,
  stopped: boolean,
): SessionStatus {
  const running =
    !stopped && (scenario === 'live' || scenario === 'reconnecting');
  const webrtc = (phase: string, connectionState: string) => ({
    role: 'host',
    phase,
    signal_url: null,
    session: null,
    ice_servers: null,
    connection_state: connectionState,
    gathering_state: null,
    ice_state: null,
    selected_candidate_pair: null,
    stats: null,
    last_error: scenario === 'timeout' ? 'deadline-exceeded' : null,
    recovery_deadline_unix_ms:
      phase === 'recovering' ? previewRecoveryDeadline : null,
  });
  if (scenario === 'reconnecting' && previewRecoveryDeadline === null) {
    // 残り 42 秒から数え始める
    previewRecoveryDeadline = Date.now() + 42_000;
  }
  return {
    active: running,
    log_dir: previewCurrentMatchLogDir,
    melon: running ? 'running' : null,
    bridge: running ? 'running' : null,
    webrtc: running
      ? scenario === 'reconnecting'
        ? webrtc('recovering', 'disconnected')
        : webrtc('connected', 'connected')
      : scenario === 'timeout'
        ? webrtc('failed', 'failed')
        : null,
    diagnostics_error: null,
    game_state_mismatch: null,
    mvl_results: previewCurrentStages(scenario),
  };
}
