import {
  Crosshair,
  FilmStrip,
  GameController,
  UploadSimple,
} from '@phosphor-icons/react';
import { useMemo, useState } from 'react';
import { css } from 'styled-system/css';
import { Button, Tabs } from '../components/ui';
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
  categories?: string[];
};
type EventSamples = Record<string, EventSample[]>;
type ParsedReplay = {
  frames: ReplayFrame[];
  eventSamples: EventSamples;
  manifestLabel?: string;
};

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
    const parsed = JSON.parse(trimmed) as {
      frames?: ReplayFrame[];
      kind?: string;
      labelSource?: string;
      quality?: { status?: string };
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
  const [frames, setFrames] = useState<ReplayFrame[]>([]);
  const [eventSamples, setEventSamples] = useState<EventSamples>({});
  const [manifestLabel, setManifestLabel] = useState('');
  const [frameIndex, setFrameIndex] = useState(0);
  const [playerIndex, setPlayerIndex] = useState<0 | 1>(1);
  const [error, setError] = useState<string | null>(null);
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

  async function loadFile(file: File) {
    try {
      const text = await file.text();
      const parsed = parseReplayText(text);
      const eventCount = Object.values(parsed.eventSamples).reduce(
        (total, samples) => total + samples.length,
        0,
      );
      setFrames(parsed.frames);
      setEventSamples(parsed.eventSamples);
      setManifestLabel(parsed.manifestLabel ?? '');
      setFrameIndex(0);
      setError(
        parsed.frames.length || eventCount
          ? null
          : 'フレームまたはイベントが見つかりません',
      );
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
      setFrames([]);
      setEventSamples({});
      setManifestLabel('');
    }
  }

  return (
    <Tabs.Content value="ai">
      <div className={css({ display: 'grid', gap: '5' })}>
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
