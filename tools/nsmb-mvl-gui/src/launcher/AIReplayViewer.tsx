import {
  ArrowClockwise,
  Crosshair,
  Database,
  FileText,
  FilmStrip,
  GameController,
  Play,
  TerminalWindow,
  UploadSimple,
} from '@phosphor-icons/react';
import { useCallback, useEffect, useMemo, useState } from 'react';
import { css } from 'styled-system/css';
import { Button, Input, Tabs } from '../components/ui';
import { listAiArtifacts, readAiTextFile, runAiTool } from '../tauriClient';
import type { AiArtifact, RunAiToolRequest, RunAiToolResponse } from '../types';
import { LauncherCard, SmallInfoCard } from './LauncherCards';

type Vec3 = { x?: number | string; y?: number | string; z?: number | string };
type PlayerState = {
  found?: boolean | number;
  pos?: Vec3;
  vel?: Vec3;
  powerup?: number | string;
  dead?: number | string;
  battleStars?: number | string;
  coins?: number | string;
  contact?: Record<string, unknown>;
  tileProbe?: {
    found?: boolean | number;
    summary?: Record<string, unknown>;
    samples?: Array<{
      name?: string;
      pixelX?: number | string;
      pixelY?: number | string;
      tileId?: number | string;
      behavior?: number | string;
      solidish?: boolean | number;
      block?: Record<string, unknown>;
    }>;
  };
};
type ReplayObject = {
  category?: string;
  objectId?: string;
  settings?: string;
  pos?: Vec3;
  relative?: Record<string, Vec3>;
};
type ReplayFrame = {
  frame?: number | string;
  hash?: string;
  inputs?: Record<
    string,
    { held?: number | string; heldHex?: string; valid?: boolean }
  >;
  players?: PlayerState[];
  targets?: Record<string, unknown>;
  objectSummary?: Record<string, unknown>;
  specialObjects?: {
    fireballs?: {
      active?: number | string;
      activeSlots?: number | string;
      handler?: string;
      words?: Array<number | string>;
      slots?: Array<{
        index?: number | string;
        kindName?: string;
        ownerCandidate?: number | string;
        ownerConfidence?: number | string;
        ownerVerified?: number | string;
        pos?: Vec3;
      }>;
    };
    projectiles?: {
      handler?: string;
      words?: Array<number | string>;
    };
  };
  visualSummary?: {
    categoryCounts?: Record<string, number | string>;
    visibleCamera0?: number | string;
    visibleCamera1?: number | string;
  };
  objects?: ReplayObject[];
};
type EventSample = {
  frame?: number | string;
  player?: number | string;
  sample?: string;
  sampleIndex?: number | string;
  tileId?: number | string;
  behavior?: number | string;
  itemBox?: boolean | number;
  storageContents?: number | string;
  before?: number | string;
  after?: number | string;
  active?: number | string;
  handler?: string;
  words?: Array<number | string>;
  categories?: string[];
};
type EventSamples = Record<string, EventSample[]>;
type ParsedReplay = {
  frames: ReplayFrame[];
  eventSamples: EventSamples;
  manifestLabel?: string;
};
type AiTaskId =
  | 'inspect_playlog'
  | 'render_svg'
  | 'human_recording'
  | 'recording_postcommands'
  | 'build_dataset'
  | 'train_imitation'
  | 'export_runtime_model'
  | 'closed_loop_eval'
  | 'recording_replay';
type WorkbenchForm = {
  inputPath: string;
  outputPath: string;
  sessionPath: string;
  datasetPath: string;
  modelPath: string;
  runtimeModelPath: string;
  logDir: string;
  scenario: string;
  policy: 'imitation' | 'rule' | 'neutral';
  seed: string;
  labelSource: 'player' | 'applied' | 'console' | 'auto';
  player: 0 | 1;
  frame: number;
  frames: number;
  epochs: number;
  threshold: number;
  maxObjects: number;
  dryRun: boolean;
  splitByRecording: boolean;
  allowJit: boolean;
  dualWindow: boolean;
  noPacketCapture: boolean;
  scanFrames: boolean;
};

const initialWorkbenchForm: WorkbenchForm = {
  inputPath: '',
  outputPath: '',
  sessionPath: '',
  datasetPath: '',
  modelPath: '',
  runtimeModelPath: '',
  logDir: '',
  scenario: 'free-play',
  policy: 'imitation',
  seed: '',
  labelSource: 'player',
  player: 1,
  frame: 0,
  frames: 2600,
  epochs: 500,
  threshold: 0.5,
  maxObjects: 96,
  dryRun: false,
  splitByRecording: true,
  allowJit: true,
  dualWindow: false,
  noPacketCapture: true,
  scanFrames: true,
};

const aiTasks: Array<{
  id: AiTaskId;
  label: string;
  inputLabel: string;
  outputLabel?: string;
}> = [
  {
    id: 'inspect_playlog',
    label: 'ログ要約',
    inputLabel: 'ai-playlog.jsonl',
  },
  {
    id: 'render_svg',
    label: 'SVG生成',
    inputLabel: 'ai-playlog.jsonl',
    outputLabel: 'frame.svg',
  },
  {
    id: 'human_recording',
    label: '手動ログ収集',
    inputLabel: '不要',
  },
  {
    id: 'recording_postcommands',
    label: '録画後処理',
    inputLabel: 'recording-session.json',
  },
  {
    id: 'build_dataset',
    label: 'dataset作成',
    inputLabel: 'recording/index/playlog',
    outputLabel: 'ai-dataset-player1.csv',
  },
  {
    id: 'train_imitation',
    label: '模倣学習',
    inputLabel: 'dataset.csv',
    outputLabel: 'model.npz',
  },
  {
    id: 'export_runtime_model',
    label: 'runtime model出力',
    inputLabel: 'model.npz',
    outputLabel: 'runtime-model.json',
  },
  {
    id: 'closed_loop_eval',
    label: '閉ループ評価',
    inputLabel: 'model.json 任意',
  },
  {
    id: 'recording_replay',
    label: 'replay検証',
    inputLabel: 'recording.json',
  },
];

const buttonBits: Array<[string, number]> = [
  ['A', 0],
  ['B', 1],
  ['SELECT', 2],
  ['START', 3],
  ['RIGHT', 4],
  ['LEFT', 5],
  ['UP', 6],
  ['DOWN', 7],
  ['R', 8],
  ['L', 9],
  ['X', 10],
  ['Y', 11],
];

function numeric(value: unknown, fallback = 0) {
  if (typeof value === 'number') return value;
  if (typeof value === 'string') {
    return Number.parseInt(value, value.startsWith('0x') ? 16 : 10);
  }
  if (typeof value === 'boolean') return value ? 1 : 0;
  return fallback;
}

function parseReplayText(text: string): ParsedReplay {
  const trimmed = text.trim();
  if (!trimmed) return { eventSamples: {}, frames: [] };
  if (trimmed.startsWith('{')) {
    try {
      const parsed = JSON.parse(trimmed) as {
        frame?: number | string;
        frames?: ReplayFrame[];
        kind?: string;
        labelSource?: string;
        quality?: { status?: string };
        schema?: string;
        summary?: { eventSamples?: EventSamples };
      };
      if (Array.isArray(parsed.frames)) {
        return { eventSamples: {}, frames: parsed.frames };
      }
      if (parsed.summary?.eventSamples) {
        const labelParts = [
          parsed.kind,
          parsed.labelSource,
          parsed.quality?.status,
        ].filter(Boolean);
        return {
          eventSamples: parsed.summary.eventSamples,
          frames: [],
          manifestLabel: labelParts.join(' / '),
        };
      }
      if (
        parsed.frame !== undefined ||
        parsed.schema === 'nsmb_mvl_ai_play_log_v1'
      ) {
        return { eventSamples: {}, frames: [parsed as ReplayFrame] };
      }
    } catch {
      // Multi-line JSONL also starts with "{". Fall through to line-by-line parsing.
    }
  }
  return {
    eventSamples: {},
    frames: trimmed
      .split(/\r?\n/)
      .filter((line) => line.trim().length > 0)
      .map((line) => JSON.parse(line) as ReplayFrame),
  };
}

function buttonsText(input?: {
  held?: number | string;
  heldHex?: string;
  valid?: boolean;
}) {
  if (!input || input.valid === false) return '-';
  const held = numeric(input.held ?? input.heldHex);
  const names = buttonBits
    .filter(([, bit]) => (held & (1 << bit)) !== 0)
    .map(([name]) => name);
  return names.length ? names.join('+') : '-';
}

function hexText(value: unknown) {
  const n = numeric(value, Number.NaN);
  if (Number.isNaN(n)) return '-';
  return `0x${n.toString(16).toUpperCase()}`;
}

function basename(path: string) {
  return path.split(/[\\/]/).pop() || path;
}

function formatBytes(value: number | null | undefined) {
  const bytes = value ?? 0;
  if (bytes >= 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
  if (bytes >= 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${Math.round(bytes)} B`;
}

function artifactTone(kind: string) {
  if (kind === 'playlog' || kind === 'recording') return 'green';
  if (kind === 'svg' || kind === 'closed_loop_eval') return 'slate';
  return 'slate';
}

function numberOrNull(value: number) {
  return Number.isFinite(value) ? value : null;
}

function buildAiToolRequest(
  task: AiTaskId,
  form: WorkbenchForm,
): RunAiToolRequest {
  const inputPath =
    task === 'train_imitation' && !form.inputPath.trim()
      ? form.datasetPath
      : task === 'export_runtime_model' && !form.inputPath.trim()
        ? form.modelPath
        : form.inputPath;
  return {
    task,
    input_path: inputPath.trim() || null,
    output_path: form.outputPath.trim() || null,
    session_path: form.sessionPath.trim() || null,
    dataset_path: form.datasetPath.trim() || null,
    model_path: form.modelPath.trim() || null,
    runtime_model_path: form.runtimeModelPath.trim() || null,
    log_dir: form.logDir.trim() || null,
    scenario: form.scenario.trim() || null,
    policy: form.policy,
    seed: form.seed.trim() || null,
    label_source: form.labelSource,
    player: form.player,
    frame: numberOrNull(form.frame),
    frames: numberOrNull(form.frames),
    epochs: numberOrNull(form.epochs),
    threshold: numberOrNull(form.threshold),
    max_objects: numberOrNull(form.maxObjects),
    dry_run: form.dryRun,
    split_by_recording: form.splitByRecording,
    allow_jit: form.allowJit,
    dual_window: form.dualWindow,
    no_packet_capture: form.noPacketCapture,
    scan_frames: form.scanFrames,
  };
}

function pos(entity?: { pos?: Vec3 }) {
  return {
    x: numeric(entity?.pos?.x),
    y: numeric(entity?.pos?.y),
    z: numeric(entity?.pos?.z),
  };
}

function svgPoint(dx: number, dy: number) {
  return {
    x: 320 + dx / 4096 / 2,
    y: 180 + dy / 4096 / 2,
  };
}

function frameEvents(frame: ReplayFrame, previous?: ReplayFrame) {
  const events: string[] = [];
  for (const index of [0, 1]) {
    const player = frame.players?.[index];
    const before = previous?.players?.[index];
    if (!player) continue;
    if (numeric(player.dead) && !numeric(before?.dead)) {
      events.push(`P${index} death`);
    }
    if (numeric(player.powerup) !== numeric(before?.powerup)) {
      events.push(
        `P${index} power ${numeric(before?.powerup)}>${numeric(player.powerup)}`,
      );
    }
    if (numeric(player.battleStars) > numeric(before?.battleStars)) {
      events.push(`P${index} star +`);
    }
    const summary = player.tileProbe?.summary ?? {};
    if (numeric(summary.effectiveHoleAhead))
      events.push(`P${index} hole ahead`);
    if (numeric(summary.wallAhead)) events.push(`P${index} wall ahead`);
    if (numeric(summary.holeSuppressedByContact)) {
      events.push(`P${index} ground suppress`);
    }
    const blockHit = (player.tileProbe?.samples ?? []).some((sample) => {
      const block = sample.block ?? {};
      return numeric(block.any) || numeric(block.itemBox);
    });
    if (blockHit) events.push(`P${index} block`);
  }
  for (const category of new Set(
    (frame.objects ?? []).map((object) => object.category),
  )) {
    if (
      category === 'world_item' ||
      category === 'neutral_item' ||
      category === 'dropped_star_item' ||
      category === 'item' ||
      category === 'item_spawn_effect' ||
      category === 'projectile' ||
      category === 'player_fireball'
    ) {
      events.push(String(category));
    }
  }
  const fireballsActive = numeric(frame.specialObjects?.fireballs?.active);
  if (fireballsActive > 0) events.push(`fireball x${fireballsActive}`);
  return events;
}

function eventTitle(name: string, sample: EventSample) {
  const player =
    sample.player === undefined || sample.player === null
      ? ''
      : `P${numeric(sample.player)} `;
  if (name === 'playerDeath') return `${player}death`;
  if (name === 'starPickup')
    return `${player}star ${sample.before}>${sample.after}`;
  if (name === 'coinChange')
    return `${player}coin ${sample.before}>${sample.after}`;
  if (name === 'powerupChange')
    return `${player}power ${sample.before}>${sample.after}`;
  if (name === 'blockCandidateVisible') {
    const tile =
      sample.tileId === undefined ? '' : ` tile ${hexText(sample.tileId)}`;
    const storage =
      sample.storageContents === undefined
        ? ''
        : ` storage ${numeric(sample.storageContents)}`;
    return `${player}block ${sample.sample ?? '-'}${tile}${storage}`;
  }
  if (name === 'fireballActive') {
    return `fireball x${numeric(sample.active)}`;
  }
  if (sample.categories?.length)
    return `${name}: ${sample.categories.join(', ')}`;
  return name;
}

function ReplayScene({
  frame,
  playerIndex,
}: {
  frame: ReplayFrame;
  playerIndex: 0 | 1;
}) {
  const self = frame.players?.[playerIndex];
  const opponent = frame.players?.[playerIndex ^ 1];
  const selfPos = pos(self);
  const opponentPos = pos(opponent);
  const opponentPoint = svgPoint(
    opponentPos.x - selfPos.x,
    opponentPos.y - selfPos.y,
  );
  const objects = (frame.objects ?? []).slice(0, 64);
  const fireballSlots = frame.specialObjects?.fireballs?.slots ?? [];

  return (
    <svg
      viewBox="0 0 640 360"
      className={css({
        borderColor: 'gray.surface.border',
        borderRadius: 'l2',
        borderWidth: '1px',
        h: 'auto',
        w: 'full',
      })}
      style={{
        backgroundColor: 'rgba(5, 12, 20, 0.72)',
        maxHeight: 560,
        minHeight: 360,
      }}
      role="img"
      aria-label="AI replay scene"
      data-testid="ai-replay-scene"
    >
      <title>frame {frame.frame}</title>
      <line x1="0" x2="640" y1="180" y2="180" stroke="rgba(255,255,255,0.16)" />
      <line x1="320" x2="320" y1="0" y2="360" stroke="rgba(255,255,255,0.16)" />
      {objects.map((object, index) => {
        const objectPos = pos(object);
        const point = svgPoint(
          objectPos.x - selfPos.x,
          objectPos.y - selfPos.y,
        );
        const category = object.category ?? 'object';
        const color = category.includes('star')
          ? '#facc15'
          : category.includes('item') || category === 'coin'
            ? '#34d399'
            : category.includes('hazard') || category.includes('enemy')
              ? '#f87171'
              : category.includes('platform')
                ? '#60a5fa'
                : '#94a3b8';
        return (
          <g key={`${object.objectId ?? 'obj'}-${index}`}>
            <circle
              cx={point.x}
              cy={point.y}
              r="7"
              fill={color}
              opacity="0.9"
            />
            <title>{`${category} ${object.objectId ?? ''} ${object.settings ?? ''}`}</title>
          </g>
        );
      })}
      {fireballSlots.map((slot, index) => {
        const fireballPos = pos(slot);
        const point = svgPoint(
          fireballPos.x - selfPos.x,
          fireballPos.y - selfPos.y,
        );
        const owner = numeric(slot.ownerCandidate, -1);
        const color =
          owner === 0 ? '#f87171' : owner === 1 ? '#60a5fa' : '#fb923c';
        return (
          <g key={`fireball-${slot.index ?? index}`}>
            <circle
              cx={point.x}
              cy={point.y}
              r="8"
              fill={color}
              stroke="#fed7aa"
              strokeWidth="2"
            />
            <text
              x={point.x + 10}
              y={point.y + 4}
              fill="#fed7aa"
              fontSize="11"
              fontWeight="700"
            >
              F{owner >= 0 ? owner : '?'}
            </text>
            <title>{`${slot.kindName ?? 'fireball'} owner=${owner} confidence=${numeric(slot.ownerConfidence)} verified=${numeric(slot.ownerVerified)}`}</title>
          </g>
        );
      })}
      <circle cx={opponentPoint.x} cy={opponentPoint.y} r="12" fill="#60a5fa" />
      <text
        x={opponentPoint.x + 14}
        y={opponentPoint.y + 4}
        fill="#dbeafe"
        fontSize="12"
        fontWeight="700"
      >
        P{playerIndex ^ 1}
      </text>
      <circle cx="320" cy="180" r="14" fill="#f87171" />
      <text x="338" y="184" fill="#fee2e2" fontSize="12" fontWeight="700">
        P{playerIndex}
      </text>
    </svg>
  );
}

export function AIReplayViewer() {
  const [artifacts, setArtifacts] = useState<AiArtifact[]>([]);
  const [artifactsError, setArtifactsError] = useState<string | null>(null);
  const [artifactsLoading, setArtifactsLoading] = useState(false);
  const [frames, setFrames] = useState<ReplayFrame[]>([]);
  const [eventSamples, setEventSamples] = useState<EventSamples>({});
  const [manifestLabel, setManifestLabel] = useState('');
  const [frameIndex, setFrameIndex] = useState(0);
  const [playerIndex, setPlayerIndex] = useState<0 | 1>(1);
  const [error, setError] = useState<string | null>(null);
  const [pathInput, setPathInput] = useState('');
  const [svgText, setSvgText] = useState('');
  const [task, setTask] = useState<AiTaskId>('inspect_playlog');
  const [workbenchForm, setWorkbenchForm] =
    useState<WorkbenchForm>(initialWorkbenchForm);
  const [commandRunning, setCommandRunning] = useState(false);
  const [commandError, setCommandError] = useState<string | null>(null);
  const [commandResult, setCommandResult] = useState<RunAiToolResponse | null>(
    null,
  );
  const frame = frames[Math.min(frameIndex, Math.max(0, frames.length - 1))];
  const previous = frames[Math.max(0, frameIndex - 1)];
  const events = useMemo(
    () => (frame ? frameEvents(frame, previous) : []),
    [frame, previous],
  );
  const eventSampleRows = useMemo(
    () =>
      Object.entries(eventSamples)
        .flatMap(([name, samples]) =>
          samples.map((sample, index) => ({
            key: `${name}-${index}-${sample.frame ?? 'na'}-${sample.player ?? 'na'}-${sample.sample ?? ''}`,
            name,
            sample,
          })),
        )
        .sort((a, b) => numeric(a.sample.frame) - numeric(b.sample.frame))
        .slice(0, 80),
    [eventSamples],
  );
  const categoryCounts = frame?.visualSummary?.categoryCounts ?? {};
  const fireballsActive = numeric(frame?.specialObjects?.fireballs?.active);
  const fireballSlotCount = numeric(
    frame?.specialObjects?.fireballs?.activeSlots,
  );
  const selectedTask = aiTasks.find((candidate) => candidate.id === task);

  const refreshArtifacts = useCallback(async () => {
    setArtifactsLoading(true);
    setArtifactsError(null);
    try {
      setArtifacts(await listAiArtifacts());
    } catch (err) {
      setArtifactsError(err instanceof Error ? err.message : String(err));
    } finally {
      setArtifactsLoading(false);
    }
  }, []);

  useEffect(() => {
    void refreshArtifacts();
  }, [refreshArtifacts]);

  function applyParsedReplay(parsed: ParsedReplay) {
    const eventCount = Object.values(parsed.eventSamples).reduce(
      (total, samples) => total + samples.length,
      0,
    );
    setFrames(parsed.frames);
    setEventSamples(parsed.eventSamples);
    setManifestLabel(parsed.manifestLabel ?? '');
    setFrameIndex(0);
    setSvgText('');
    setError(
      parsed.frames.length || eventCount
        ? null
        : 'フレームまたはイベントが見つかりません',
    );
  }

  async function loadText(text: string, label = '') {
    try {
      const parsed = parseReplayText(text);
      applyParsedReplay(parsed);
      if (label) {
        setPathInput(label);
        setWorkbenchForm((current) => ({ ...current, inputPath: label }));
      }
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
      setFrames([]);
      setEventSamples({});
      setManifestLabel('');
      setSvgText('');
    }
  }

  async function loadFile(file: File) {
    try {
      const text = await file.text();
      await loadText(text, file.name);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
      setFrames([]);
      setEventSamples({});
      setManifestLabel('');
      setSvgText('');
    }
  }

  async function loadPath(path: string) {
    const trimmed = path.trim();
    if (!trimmed) return;
    try {
      const response = await readAiTextFile({ path: trimmed });
      if (trimmed.toLowerCase().endsWith('.svg')) {
        setSvgText(response.text);
        setError(null);
        setPathInput(response.path);
        setWorkbenchForm((current) => ({
          ...current,
          inputPath: response.path,
        }));
      } else {
        await loadText(response.text, response.path);
      }
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  }

  function selectArtifact(artifact: AiArtifact) {
    setPathInput(artifact.path);
    setWorkbenchForm((current) => {
      if (artifact.kind === 'session') {
        return {
          ...current,
          sessionPath: artifact.path,
          inputPath: artifact.path,
        };
      }
      if (artifact.kind === 'dataset') {
        return {
          ...current,
          datasetPath: artifact.path,
          inputPath: artifact.path,
        };
      }
      if (artifact.kind === 'model') {
        return {
          ...current,
          modelPath: artifact.path,
          inputPath: artifact.path,
        };
      }
      if (artifact.kind === 'runtime_model') {
        return {
          ...current,
          runtimeModelPath: artifact.path,
          modelPath: artifact.path,
        };
      }
      return { ...current, inputPath: artifact.path };
    });
  }

  async function executeTask() {
    setCommandRunning(true);
    setCommandError(null);
    setCommandResult(null);
    try {
      const result = await runAiTool(buildAiToolRequest(task, workbenchForm));
      setCommandResult(result);
      if (result.exit_code !== 0) {
        setCommandError(`exit ${result.exit_code ?? 'unknown'}`);
      }
      if (task === 'render_svg' && result.output_path) {
        const response = await readAiTextFile({ path: result.output_path });
        setSvgText(response.text);
        setPathInput(response.path);
      }
      await refreshArtifacts();
    } catch (err) {
      setCommandError(err instanceof Error ? err.message : String(err));
    } finally {
      setCommandRunning(false);
    }
  }

  return (
    <Tabs.Content value="ai">
      <div className={css({ display: 'grid', gap: '5' })}>
        <div
          className={css({
            display: 'grid',
            gap: '4',
            gridTemplateColumns: 'minmax(0, 1fr) 420px',
            '@media (max-width: 1120px)': { gridTemplateColumns: '1fr' },
          })}
        >
          <LauncherCard
            title="AI Workbench"
            icon={<TerminalWindow size={24} weight="fill" />}
            badge={
              commandRunning ? 'running' : commandResult ? 'done' : undefined
            }
            badgeTone={commandError ? 'slate' : 'green'}
          >
            <div className={css({ display: 'grid', gap: '3' })}>
              <div
                className={css({
                  display: 'grid',
                  gap: '2',
                  gridTemplateColumns: 'minmax(0, 1fr) auto auto',
                  '@media (max-width: 840px)': { gridTemplateColumns: '1fr' },
                })}
              >
                <Input
                  aria-label="ログまたは成果物パス"
                  value={pathInput}
                  placeholder="logs\...\ai-playlog.jsonl / recording.json / frame.svg"
                  onChange={(event) => {
                    setPathInput(event.currentTarget.value);
                    setWorkbenchForm((current) => ({
                      ...current,
                      inputPath: event.currentTarget.value,
                    }));
                  }}
                />
                <Button type="button" onClick={() => void loadPath(pathInput)}>
                  <FileText size={18} weight="bold" />
                  開く
                </Button>
                <Button
                  type="button"
                  variant="outline"
                  loading={artifactsLoading}
                  onClick={() => void refreshArtifacts()}
                >
                  <ArrowClockwise size={18} weight="bold" />
                  更新
                </Button>
              </div>
              <div
                className={css({
                  display: 'grid',
                  gap: '3',
                  gridTemplateColumns: 'repeat(4, minmax(0, 1fr))',
                  '@media (max-width: 960px)': {
                    gridTemplateColumns: 'repeat(2, minmax(0, 1fr))',
                  },
                  '@media (max-width: 560px)': { gridTemplateColumns: '1fr' },
                })}
              >
                <label
                  className={css({ display: 'grid', gap: '1' })}
                  htmlFor="ai-workbench-frame"
                >
                  <span
                    className={css({
                      color: 'fg.muted',
                      fontWeight: 'bold',
                      textStyle: 'xs',
                    })}
                  >
                    タスク
                  </span>
                  <select
                    value={task}
                    className={css({
                      bg: 'gray.surface.bg',
                      borderColor: 'gray.surface.border',
                      borderRadius: 'l1',
                      borderWidth: '1px',
                      color: 'fg.default',
                      minH: '10',
                      px: '3',
                    })}
                    onChange={(event) =>
                      setTask(event.currentTarget.value as AiTaskId)
                    }
                  >
                    {aiTasks.map((candidate) => (
                      <option key={candidate.id} value={candidate.id}>
                        {candidate.label}
                      </option>
                    ))}
                  </select>
                </label>
                <label
                  className={css({ display: 'grid', gap: '1' })}
                  htmlFor="ai-workbench-player"
                >
                  <span
                    className={css({
                      color: 'fg.muted',
                      fontWeight: 'bold',
                      textStyle: 'xs',
                    })}
                  >
                    player
                  </span>
                  <select
                    id="ai-workbench-player"
                    value={workbenchForm.player}
                    className={css({
                      bg: 'gray.surface.bg',
                      borderColor: 'gray.surface.border',
                      borderRadius: 'l1',
                      borderWidth: '1px',
                      color: 'fg.default',
                      minH: '10',
                      px: '3',
                    })}
                    onChange={(event) => {
                      const value =
                        Number(event.currentTarget.value) === 0 ? 0 : 1;
                      setWorkbenchForm((current) => ({
                        ...current,
                        player: value,
                      }));
                      setPlayerIndex(value);
                    }}
                  >
                    <option value={1}>Luigi / P1</option>
                    <option value={0}>Mario / P0</option>
                  </select>
                </label>
                <label
                  className={css({ display: 'grid', gap: '1' })}
                  htmlFor="ai-workbench-frame"
                >
                  <span
                    className={css({
                      color: 'fg.muted',
                      fontWeight: 'bold',
                      textStyle: 'xs',
                    })}
                  >
                    frame
                  </span>
                  <Input
                    id="ai-workbench-frame"
                    type="number"
                    value={workbenchForm.frame}
                    onChange={(event) =>
                      setWorkbenchForm((current) => ({
                        ...current,
                        frame: Number(event.currentTarget.value),
                      }))
                    }
                  />
                </label>
                <label
                  className={css({ display: 'grid', gap: '1' })}
                  htmlFor="ai-workbench-frames"
                >
                  <span
                    className={css({
                      color: 'fg.muted',
                      fontWeight: 'bold',
                      textStyle: 'xs',
                    })}
                  >
                    frames
                  </span>
                  <Input
                    id="ai-workbench-frames"
                    type="number"
                    value={workbenchForm.frames}
                    onChange={(event) =>
                      setWorkbenchForm((current) => ({
                        ...current,
                        frames: Number(event.currentTarget.value),
                      }))
                    }
                  />
                </label>
              </div>
              <div
                className={css({
                  display: 'grid',
                  gap: '3',
                  gridTemplateColumns: 'repeat(2, minmax(0, 1fr))',
                  '@media (max-width: 840px)': { gridTemplateColumns: '1fr' },
                })}
              >
                <Input
                  aria-label="出力パス"
                  value={workbenchForm.outputPath}
                  placeholder={selectedTask?.outputLabel ?? 'output path 任意'}
                  onChange={(event) =>
                    setWorkbenchForm((current) => ({
                      ...current,
                      outputPath: event.currentTarget.value,
                    }))
                  }
                />
                <Input
                  aria-label="model path"
                  value={workbenchForm.modelPath}
                  placeholder="model.npz / runtime-model.json"
                  onChange={(event) =>
                    setWorkbenchForm((current) => ({
                      ...current,
                      modelPath: event.currentTarget.value,
                    }))
                  }
                />
                <Input
                  aria-label="dataset path"
                  value={workbenchForm.datasetPath}
                  placeholder="ai-dataset-player1.csv"
                  onChange={(event) =>
                    setWorkbenchForm((current) => ({
                      ...current,
                      datasetPath: event.currentTarget.value,
                    }))
                  }
                />
                <Input
                  aria-label="session path"
                  value={workbenchForm.sessionPath}
                  placeholder="recording-session.json"
                  onChange={(event) =>
                    setWorkbenchForm((current) => ({
                      ...current,
                      sessionPath: event.currentTarget.value,
                    }))
                  }
                />
                <Input
                  aria-label="scenario"
                  value={workbenchForm.scenario}
                  placeholder="star-chase / item-box / fire / free-play"
                  onChange={(event) =>
                    setWorkbenchForm((current) => ({
                      ...current,
                      scenario: event.currentTarget.value,
                    }))
                  }
                />
                <Input
                  aria-label="seed"
                  value={workbenchForm.seed}
                  placeholder="0x12345678"
                  onChange={(event) =>
                    setWorkbenchForm((current) => ({
                      ...current,
                      seed: event.currentTarget.value,
                    }))
                  }
                />
              </div>
              <div
                className={css({
                  alignItems: 'center',
                  display: 'flex',
                  flexWrap: 'wrap',
                  gap: '3',
                })}
              >
                {[
                  ['allowJit', 'JIT'],
                  ['dryRun', 'dry-run'],
                  ['splitByRecording', 'recording split'],
                  ['noPacketCapture', 'no packet capture'],
                  ['dualWindow', 'dual window'],
                  ['scanFrames', 'scan frames'],
                ].map(([key, label]) => (
                  <label
                    key={key}
                    className={css({
                      alignItems: 'center',
                      color: 'fg.muted',
                      display: 'inline-flex',
                      fontWeight: 'bold',
                      gap: '2',
                      textStyle: 'sm',
                    })}
                  >
                    <input
                      type="checkbox"
                      checked={Boolean(
                        workbenchForm[key as keyof WorkbenchForm],
                      )}
                      onChange={(event) =>
                        setWorkbenchForm((current) => ({
                          ...current,
                          [key]: event.currentTarget.checked,
                        }))
                      }
                    />
                    {label}
                  </label>
                ))}
              </div>
              <div
                className={css({
                  alignItems: 'center',
                  display: 'flex',
                  flexWrap: 'wrap',
                  gap: '3',
                })}
              >
                <Button
                  type="button"
                  loading={commandRunning}
                  onClick={() => void executeTask()}
                >
                  <Play size={18} weight="bold" />
                  実行
                </Button>
                <span
                  className={css({
                    color: 'fg.muted',
                    fontWeight: 'bold',
                    textStyle: 'sm',
                  })}
                >
                  {selectedTask?.inputLabel}
                </span>
                {commandError ? (
                  <span
                    className={css({
                      color: 'red.subtle.fg',
                      fontWeight: 'bold',
                      textStyle: 'sm',
                    })}
                  >
                    {commandError}
                  </span>
                ) : null}
              </div>
              {commandResult ? (
                <div className={css({ display: 'grid', gap: '2' })}>
                  <div
                    className={css({
                      color: 'fg.muted',
                      fontFamily: 'mono',
                      overflowWrap: 'anywhere',
                      textStyle: 'xs',
                    })}
                  >
                    {commandResult.command_line}
                  </div>
                  <pre
                    className={css({
                      bg: 'gray.12',
                      borderColor: 'gray.surface.border',
                      borderRadius: 'l1',
                      borderWidth: '1px',
                      color: 'gray.1',
                      fontFamily: 'mono',
                      maxH: '64',
                      overflow: 'auto',
                      p: '3',
                      textStyle: 'xs',
                      whiteSpace: 'pre-wrap',
                    })}
                  >
                    {commandResult.stdout || commandResult.stderr || '-'}
                    {commandResult.stderr
                      ? `\n\nstderr:\n${commandResult.stderr}`
                      : ''}
                  </pre>
                </div>
              ) : null}
            </div>
          </LauncherCard>

          <LauncherCard
            title="AI成果物"
            icon={<Database size={24} weight="fill" />}
            badge={`${artifacts.length}`}
          >
            {artifactsError ? (
              <div
                className={css({ color: 'red.subtle.fg', fontWeight: 'bold' })}
              >
                {artifactsError}
              </div>
            ) : null}
            <div
              className={css({ display: 'grid', gap: '2', overflow: 'auto' })}
              style={{ maxHeight: 520 }}
            >
              {artifacts.map((artifact) => (
                <button
                  key={`${artifact.kind}-${artifact.path}`}
                  type="button"
                  className={css({
                    bg: 'gray.subtle.bg',
                    borderColor: 'gray.surface.border',
                    borderRadius: 'l1',
                    borderWidth: '1px',
                    color: 'fg.default',
                    display: 'grid',
                    gap: '1',
                    justifyItems: 'start',
                    px: '3',
                    py: '2',
                    textAlign: 'left',
                    _hover: { borderColor: 'blue.outline.border' },
                  })}
                  onClick={() => {
                    selectArtifact(artifact);
                    if (
                      artifact.kind === 'playlog' ||
                      artifact.kind === 'recording' ||
                      artifact.kind === 'svg'
                    ) {
                      void loadPath(artifact.path);
                    }
                  }}
                >
                  <span
                    className={css({
                      color:
                        artifactTone(artifact.kind) === 'green'
                          ? 'green.subtle.fg'
                          : 'fg.muted',
                      fontWeight: 'black',
                      textStyle: 'xs',
                    })}
                  >
                    {artifact.kind} / {formatBytes(artifact.bytes)}
                  </span>
                  <span
                    className={css({
                      fontWeight: 'bold',
                      maxW: 'full',
                      overflowWrap: 'anywhere',
                      textStyle: 'sm',
                    })}
                  >
                    {basename(artifact.path)}
                  </span>
                </button>
              ))}
            </div>
          </LauncherCard>
        </div>

        <LauncherCard
          title="AIログビューア"
          icon={<FilmStrip size={24} weight="fill" />}
          badge={
            frames.length
              ? `${frames.length} frames`
              : eventSampleRows.length
                ? `${eventSampleRows.length} events`
                : undefined
          }
        >
          <div
            className={css({
              alignItems: 'center',
              display: 'flex',
              flexWrap: 'wrap',
              gap: '3',
              justifyContent: 'space-between',
            })}
          >
            <label
              className={css({
                alignItems: 'center',
                cursor: 'pointer',
                display: 'inline-flex',
                gap: '2',
              })}
            >
              <input
                type="file"
                accept=".jsonl,.json"
                aria-label="AIログファイル"
                className={css({ display: 'none' })}
                onChange={(event) => {
                  const file = event.currentTarget.files?.[0];
                  if (file) void loadFile(file);
                }}
              />
              <Button type="button" asChild>
                <span>
                  <UploadSimple size={18} weight="bold" />
                  ログを開く
                </span>
              </Button>
            </label>
            <div className={css({ display: 'flex', gap: '2' })}>
              <Button
                variant={playerIndex === 0 ? 'solid' : 'outline'}
                onClick={() => setPlayerIndex(0)}
              >
                P0
              </Button>
              <Button
                variant={playerIndex === 1 ? 'solid' : 'outline'}
                onClick={() => setPlayerIndex(1)}
              >
                P1
              </Button>
            </div>
          </div>
          {error ? (
            <div
              className={css({ color: 'red.subtle.fg', fontWeight: 'bold' })}
            >
              {error}
            </div>
          ) : null}
          {frame ? (
            <div className={css({ display: 'grid', gap: '3' })}>
              <input
                type="range"
                min="0"
                max={Math.max(0, frames.length - 1)}
                value={frameIndex}
                onChange={(event) =>
                  setFrameIndex(Number(event.currentTarget.value))
                }
              />
              <div
                className={css({
                  color: 'fg.muted',
                  fontWeight: 'bold',
                  textStyle: 'sm',
                })}
              >
                frame {frame.frame} / index {frameIndex} / hash{' '}
                {frame.hash ?? '-'}
              </div>
            </div>
          ) : null}
          {manifestLabel ? (
            <div
              className={css({
                color: 'fg.muted',
                fontWeight: 'bold',
                textStyle: 'sm',
              })}
            >
              manifest {manifestLabel}
            </div>
          ) : null}
        </LauncherCard>

        {svgText ? (
          <LauncherCard
            title="生成SVG"
            icon={<Crosshair size={24} weight="fill" />}
          >
            <iframe
              className={css({
                bg: 'gray.12',
                borderColor: 'gray.surface.border',
                borderRadius: 'l2',
                borderWidth: '1px',
                h: 'xl',
                w: 'full',
              })}
              data-testid="ai-rendered-svg"
              sandbox=""
              srcDoc={svgText}
              title="生成SVG"
            />
          </LauncherCard>
        ) : null}

        {eventSampleRows.length ? (
          <LauncherCard
            title="記録イベント"
            icon={<GameController size={22} weight="fill" />}
            badge={`${eventSampleRows.length}`}
          >
            <div
              className={css({
                display: 'grid',
                gap: '2',
                overflow: 'auto',
              })}
              style={{ maxHeight: 260 }}
            >
              {eventSampleRows.map(({ key, name, sample }) => (
                <button
                  key={key}
                  type="button"
                  className={css({
                    alignItems: 'center',
                    bg: 'gray.subtle.bg',
                    borderColor: 'gray.surface.border',
                    borderRadius: 'l1',
                    borderWidth: '1px',
                    color: 'fg.default',
                    display: 'grid',
                    gap: '1',
                    justifyItems: 'start',
                    px: '3',
                    py: '2',
                    textAlign: 'left',
                  })}
                  onClick={() => {
                    const target = frames.findIndex(
                      (candidate) =>
                        numeric(candidate.frame) === numeric(sample.frame),
                    );
                    if (target >= 0) setFrameIndex(target);
                  }}
                >
                  <span
                    className={css({
                      color: 'fg.muted',
                      fontWeight: 'bold',
                      textStyle: 'xs',
                    })}
                  >
                    frame {sample.frame ?? '-'} / {name}
                  </span>
                  <span
                    className={css({ fontWeight: 'bold', textStyle: 'sm' })}
                  >
                    {eventTitle(name, sample)}
                  </span>
                </button>
              ))}
            </div>
          </LauncherCard>
        ) : null}

        {frame ? (
          <div
            className={css({
              display: 'grid',
              gap: '4',
              gridTemplateColumns: 'minmax(0, 1fr) 360px',
              '@media (max-width: 1120px)': { gridTemplateColumns: '1fr' },
            })}
          >
            <LauncherCard
              title="相対配置"
              icon={<Crosshair size={24} weight="fill" />}
            >
              <ReplayScene frame={frame} playerIndex={playerIndex} />
            </LauncherCard>
            <div
              className={css({
                alignContent: 'start',
                display: 'grid',
                gap: '3',
              })}
            >
              <SmallInfoCard
                label="入力 P0"
                value={buttonsText(frame.inputs?.player0)}
              />
              <SmallInfoCard
                label="入力 P1"
                value={buttonsText(frame.inputs?.player1)}
              />
              <SmallInfoCard
                label="可視 object"
                value={`${numeric(frame.objectSummary?.active)} active`}
                caption={`cam0 ${numeric(frame.visualSummary?.visibleCamera0)} / cam1 ${numeric(frame.visualSummary?.visibleCamera1)}`}
              />
              <SmallInfoCard
                label="fireball"
                value={`${fireballsActive} active`}
                caption={`slots ${fireballSlotCount} / ${frame.specialObjects?.fireballs?.handler ?? '-'}`}
              />
              <LauncherCard
                title="イベント"
                icon={<GameController size={22} weight="fill" />}
              >
                <div
                  className={css({
                    color: 'fg.default',
                    display: 'flex',
                    flexWrap: 'wrap',
                    gap: '2',
                    fontWeight: 'bold',
                    textStyle: 'sm',
                  })}
                >
                  {events.length ? (
                    events.map((event) => (
                      <span
                        key={event}
                        className={css({
                          bg: 'blue.subtle.bg',
                          borderRadius: 'l1',
                          px: '2',
                          py: '1',
                        })}
                      >
                        {event}
                      </span>
                    ))
                  ) : (
                    <span>-</span>
                  )}
                </div>
              </LauncherCard>
              <LauncherCard title="カテゴリ数">
                <div
                  className={css({
                    display: 'grid',
                    gap: '1',
                    overflow: 'auto',
                  })}
                  style={{ maxHeight: 260 }}
                >
                  {Object.entries(categoryCounts).map(([name, count]) => (
                    <div
                      key={name}
                      className={css({
                        color: 'fg.muted',
                        display: 'flex',
                        justifyContent: 'space-between',
                        textStyle: 'sm',
                      })}
                    >
                      <span>{name}</span>
                      <strong>{numeric(count)}</strong>
                    </div>
                  ))}
                </div>
              </LauncherCard>
            </div>
          </div>
        ) : null}
      </div>
    </Tabs.Content>
  );
}
