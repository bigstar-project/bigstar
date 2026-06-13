import { commands } from './bindings';
import type {
  AiArtifact,
  AiReplayFrameRef,
  Defaults,
  GenerateRomRequest,
  GenerateRomResponse,
  LaunchRequest,
  LaunchResponse,
  OpenAiReplayLogRequest,
  OpenAiReplayLogResponse,
  PreflightResponse,
  ReadAiReplayFrameRequest,
  ReadAiReplayFrameResponse,
  ReadAiTextFileRequest,
  ReadAiTextFileResponse,
  RunAiToolRequest,
  RunAiToolResponse,
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
  roms_prepared_once: false,
  input_config_opened_once: false,
  port: 8165,
};

const previewPlaylogLine = JSON.stringify({
  schema: 'nsmb_mvl_ai_play_log_v1',
  frame: 900,
  hash: '0x12345678',
  inputs: {
    player0: { held: 0x810 },
    player1: { held: 0x20 },
    appliedPlayer1: { held: 0x20, valid: true },
  },
  players: [
    {
      found: 1,
      pos: { x: 409600, y: 819200, z: 0 },
      powerup: 0,
      dead: 0,
      battleStars: 0,
      coins: 0,
      visualState: {
        powerup: { name: 'small' },
        inventoryPowerup: { name: 'none' },
      },
      tileProbe: {
        found: 1,
        summary: { effectiveHoleAhead: 0, wallAhead: 0 },
        samples: [],
      },
    },
    {
      found: 1,
      pos: { x: 450560, y: 819200, z: 0 },
      powerup: 2,
      dead: 0,
      battleStars: 1,
      coins: 7,
      visualState: {
        powerup: { name: 'fire' },
        inventoryPowerup: { name: 'none' },
      },
      tileProbe: {
        found: 1,
        summary: { effectiveHoleAhead: 1, wallAhead: 0 },
        samples: [
          {
            found: 1,
            name: 'aheadFeet',
            worldX: 516096,
            worldY: 819200,
            tileId: 25,
            behavior: '0x00080002',
            solidish: 1,
            tile: { breakableBlock: 1 },
            block: {
              any: 1,
              breakable: 1,
              visibleStorageBreakableCandidate: 1,
            },
          },
        ],
        grid: {
          encoding: 'sparse_non_empty',
          width: 33,
          height: 17,
          loggedCells: 3,
          totalCells: 561,
          cells: [
            {
              row: 10,
              col: 17,
              relTileX: 1,
              relTileY: 0,
              pixelX: 126,
              pixelY: 200,
              tileId: 71,
              behavior: '0x00050007',
              solidish: 1,
              tile: { questionBlock: 1 },
              block: { any: 1, itemBox: 1, question: 1, storageContents: 7 },
            },
            {
              row: 11,
              col: 16,
              relTileX: 0,
              relTileY: 1,
              pixelX: 110,
              pixelY: 216,
              tileId: 25,
              behavior: '0x00080002',
              solidish: 1,
              tile: { breakableBlock: 1 },
              block: {
                any: 1,
                breakable: 1,
                visibleStorageBreakableCandidate: 1,
              },
            },
            {
              row: 9,
              col: 18,
              relTileX: 2,
              relTileY: -1,
              pixelX: 142,
              pixelY: 184,
              tileId: 0,
              behavior: '0x00000000',
              solidish: 0,
              tile: { coin: 1 },
              block: { any: 0 },
            },
          ],
        },
      },
    },
  ],
  objectSummary: { active: 3 },
  visualSummary: {
    categoryCounts: { coin: 1, player: 2, big_star_actor: 1 },
    visibleCamera0: 3,
    visibleCamera1: 3,
  },
  specialObjects: {
    fireballs: {
      active: 1,
      activeSlots: 1,
      slots: [
        {
          index: 0,
          kindName: 'player1',
          ownerCandidate: 1,
          ownerConfidence: 100,
          ownerVerified: 1,
          pos: { x: 462848, y: 811008, z: 0 },
          relative: { p1dx: 12288, p1dy: -8192 },
        },
      ],
    },
  },
  objects: [
    {
      category: 'big_star_actor',
      objectId: '0x00A1',
      settings: '0x00000000',
      pos: { x: 475136, y: 790528, z: 0 },
    },
    {
      category: 'coin',
      objectId: '0x0042',
      settings: '0x00000000',
      pos: { x: 458752, y: 806912, z: 0 },
    },
  ],
});

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

export function listAiArtifacts() {
  if (!isTauriRuntime()) {
    return Promise.resolve<AiArtifact[]>([
      {
        path: 'preview/logs/client/ai-playlog.jsonl',
        kind: 'playlog',
        bytes: previewPlaylogLine.length,
        modified_unix_secs: Date.now() / 1000,
      },
      {
        path: 'preview/logs/client/recording.json',
        kind: 'recording',
        bytes: 512,
        modified_unix_secs: Date.now() / 1000 - 60,
      },
    ]);
  }
  return unwrapCommand(commands.listAiArtifacts());
}

export function readAiTextFile(request: ReadAiTextFileRequest) {
  if (!isTauriRuntime()) {
    return Promise.resolve<ReadAiTextFileResponse>({
      original_bytes: previewPlaylogLine.length * 2,
      path: request.path,
      sampled: false,
      sampled_lines: 0,
      text: `${previewPlaylogLine}\n${previewPlaylogLine.replace('900', '930')}\n`,
    });
  }
  return unwrapCommand(commands.readAiTextFile(request));
}

export function openAiReplayLog(request: OpenAiReplayLogRequest) {
  if (!isTauriRuntime()) {
    return Promise.resolve<OpenAiReplayLogResponse>({
      compressed: false,
      data_bytes: previewPlaylogLine.length * 2,
      data_path: request.path,
      frames: [
        { byte_offset: 0, frame: 900, index: 0 },
        { byte_offset: previewPlaylogLine.length + 1, frame: 930, index: 1 },
      ] satisfies AiReplayFrameRef[],
      original_bytes: previewPlaylogLine.length * 2,
      source_path: request.path,
    });
  }
  return unwrapCommand(commands.openAiReplayLog(request));
}

export function readAiReplayFrame(request: ReadAiReplayFrameRequest) {
  if (!isTauriRuntime()) {
    const byteOffset = request.byte_offset ?? 0;
    return Promise.resolve<ReadAiReplayFrameResponse>({
      frame_json:
        byteOffset > 0
          ? `${previewPlaylogLine.replace('900', '930')}\n`
          : `${previewPlaylogLine}\n`,
      previous_frame_json:
        request.previous_byte_offset === null ||
        request.previous_byte_offset === undefined
          ? null
          : `${previewPlaylogLine}\n`,
    });
  }
  return unwrapCommand(commands.readAiReplayFrame(request));
}

export function selectAiLogFile(currentPath: string) {
  if (!isTauriRuntime()) {
    return Promise.resolve<string | null>(null);
  }
  return unwrapCommand(commands.selectAiLogFile(currentPath));
}

export function runAiTool(request: RunAiToolRequest) {
  if (!isTauriRuntime()) {
    return Promise.resolve<RunAiToolResponse>({
      cwd: 'preview',
      command_line: `preview ${request.task}`,
      exit_code: 0,
      stdout:
        request.task === 'render_svg'
          ? `rendered ${request.output_path ?? 'preview.svg'}`
          : `preview ${request.task} completed`,
      stderr: '',
      output_path: request.output_path,
    });
  }
  return unwrapCommand(commands.runAiTool(request));
}
