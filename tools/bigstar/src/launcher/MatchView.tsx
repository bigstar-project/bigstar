import { Info } from '@phosphor-icons/react';
import { useEffect, useState } from 'react';
import { css, cx } from 'styled-system/css';
import * as Alert from '@/components/ui/alert';
import { Button } from '@/components/ui/button';
import * as Card from '@/components/ui/card';
import * as Progress from '@/components/ui/progress';
import { maxGamesForWins } from '../form';
import { localPlayerSide, opponentPlayerSide } from '../matchHistory';
import type { MvlStageResult } from '../types';
import { stageLabel } from './options';
import type { BattleMatchRecord, ConnectionStatusState } from './types';

// 相手の復帰を待つ時間。bigstar-net-bridge の再接続待ちと合わせる
const recoveryWindowSeconds = 60;

type PlayerSide = 'mario' | 'luigi';

type MatchPhase = 'live' | 'reconnecting' | 'timeout' | 'stopped' | 'finished';

type Tone = 'success' | 'warning' | 'danger' | 'gray';

/** 1 ゲーム分の列の状態。live / wait / cut は結果がまだ出ていないゲーム */
type GameState = 'decided' | 'live' | 'wait' | 'cut' | 'empty';

type GameSlot = {
  /** 1 から始まるゲーム番号。枠の並びは変わらないので key にも使う */
  number: number;
  result: MvlStageResult | null;
  stage: number | null;
  state: GameState;
  winner: PlayerSide | null;
};

export function MatchView({
  canStop,
  connection,
  match,
  onOpenHistory,
  onReturnToLobby,
  onStop,
}: {
  canStop: boolean;
  connection: ConnectionStatusState;
  match: BattleMatchRecord;
  onOpenHistory: () => void;
  onReturnToLobby: () => void;
  onStop: () => void;
}) {
  const phase = matchPhase(match, connection);
  const selfSide = localPlayerSide(match);
  const opponentSide = opponentPlayerSide(match);
  const latest = match.stages.at(-1);
  const selfWins = matchWinsFor(latest, selfSide);
  const opponentWins = matchWinsFor(latest, opponentSide);
  const outcome =
    phase === 'finished' && selfWins !== opponentWins
      ? selfWins > opponentWins
        ? 'win'
        : 'loss'
      : null;
  const slots = gameSlots(match, phase);
  const status = statusFor(phase, connection, match.stages.length + 1);
  const ended = match.status !== 'running';

  return (
    <div
      className={css({ display: 'flex', flexDirection: 'column', gap: '5' })}
    >
      <Card.Root className={css({ bg: 'gray.2' })}>
        <div
          className={css({
            alignItems: 'center',
            borderBottomWidth: '1px',
            display: 'flex',
            flexWrap: 'wrap',
            gap: '4',
            minH: '14',
            pl: '5',
            pr: '4',
            py: '2',
          })}
        >
          <output
            className={cx(
              toneText[status.tone],
              css({
                alignItems: 'center',
                display: 'flex',
                fontWeight: 'semibold',
                gap: '2.5',
                textStyle: 'sm',
              }),
            )}
          >
            <StatusDot pulse={status.pulse} tone={status.tone} />
            {status.text}
          </output>
          <RulesSummary match={match} />
          {!ended && canStop ? (
            <Button
              className={css({ ml: 'auto' })}
              colorPalette="danger"
              size="sm"
              variant="outline"
              onClick={onStop}
            >
              対戦を中止
            </Button>
          ) : null}
        </div>

        {phase === 'reconnecting' ? (
          <ReconnectAlert deadlineMs={connection.recoveryDeadlineMs ?? null} />
        ) : null}
        {phase === 'timeout' ? (
          <Alert.Root className={alertStripClass} role="alert" status="error">
            <Alert.Content>
              <Alert.Title>再接続がタイムアウトしました</Alert.Title>
              <Alert.Description className={css({ color: 'fg.muted' })}>
                60秒以内に通信が戻らなかったため、対戦を終了しました。この対戦は「中断」として履歴に残ります。
              </Alert.Description>
            </Alert.Content>
          </Alert.Root>
        ) : null}

        <ScoreHero
          dimmed={phase === 'reconnecting' || phase === 'timeout'}
          opponentName={match.playerNames[opponentSide]}
          opponentWins={opponentWins}
          outcome={outcome}
          selfName={match.playerNames[selfSide]}
          selfWins={selfWins}
        />

        <GameTable
          opponentName={match.playerNames[opponentSide]}
          opponentSide={opponentSide}
          selfSide={selfSide}
          slots={slots}
        />
      </Card.Root>

      {!ended ? (
        <p
          className={css({
            alignItems: 'center',
            color: 'fg.subtle',
            display: 'flex',
            gap: '2',
            textStyle: 'sm',
          })}
        >
          <Info className={css({ flexShrink: '0' })} size={16} />
          <span>
            <span className={css({ display: 'inline-block' })}>
              melonDS のウィンドウでプレイしてください。
            </span>
            <span className={css({ display: 'inline-block' })}>
              通信が一時的に切れても、60秒以内なら復帰できます。
            </span>
          </span>
        </p>
      ) : (
        <div
          className={css({ alignItems: 'center', display: 'flex', gap: '2.5' })}
        >
          <Button colorPalette="amber" onClick={onReturnToLobby}>
            ロビーに戻る
          </Button>
          <Button colorPalette="gray" variant="subtle" onClick={onOpenHistory}>
            履歴で詳しく見る
          </Button>
        </div>
      )}
    </div>
  );
}

function matchPhase(
  match: BattleMatchRecord,
  connection: ConnectionStatusState,
): MatchPhase {
  if (match.status === 'completed') return 'finished';
  if (connection.recoveryTimedOut) return 'timeout';
  if (match.status === 'stopped') return 'stopped';
  if (connection.recoveryDeadlineMs !== undefined) return 'reconnecting';
  return 'live';
}

function statusFor(
  phase: MatchPhase,
  connection: ConnectionStatusState,
  gameNumber: number,
): { pulse: boolean; text: string; tone: Tone } {
  switch (phase) {
    case 'finished':
      return { pulse: false, text: '対戦終了', tone: 'gray' };
    case 'stopped':
      return { pulse: false, text: '対戦を中断しました', tone: 'gray' };
    case 'timeout':
      return { pulse: false, text: '接続エラー', tone: 'danger' };
    case 'reconnecting':
      return { pulse: true, text: '再接続中…', tone: 'warning' };
    case 'live':
      // 接続が整うまでは、接続状態の文言をそのまま出す
      if (connection.kind === 'ok') {
        return {
          pulse: true,
          text: `第${gameNumber}ゲーム 進行中`,
          tone: 'success',
        };
      }
      return {
        pulse: connection.kind === 'idle',
        text: connection.text,
        tone:
          connection.kind === 'error'
            ? 'danger'
            : connection.kind === 'warn'
              ? 'warning'
              : 'gray',
      };
  }
}

function gameSlots(match: BattleMatchRecord, phase: MatchPhase): GameSlot[] {
  const results = [...match.stages].sort(
    (left, right) => left.game_index - right.game_index,
  );
  // 終わった対戦は実際に遊んだゲームだけ、進行中は最大ゲーム数まで枠を出す
  const count =
    phase === 'finished'
      ? results.length
      : Math.max(maxGamesForWins(match.settings.wins), results.length);
  // 途中で終わった対戦では、遊んでいたゲームを中断として残す
  const current: GameState | null =
    phase === 'live'
      ? 'live'
      : phase === 'reconnecting'
        ? 'wait'
        : phase === 'timeout' || phase === 'stopped'
          ? 'cut'
          : null;

  return Array.from({ length: count }, (_, index) => {
    const number = index + 1;
    const result = results[index] ?? null;
    if (result) {
      const winner = sideFromWinner(result.winner);
      return {
        number,
        result,
        stage: result.stage,
        state: winner ? 'decided' : 'cut',
        winner,
      };
    }
    if (index === results.length && current) {
      return {
        number,
        result: null,
        stage: match.settings.course_stages[index] ?? null,
        state: current,
        winner: null,
      };
    }
    return { number, result: null, stage: null, state: 'empty', winner: null };
  });
}

function matchWinsFor(result: MvlStageResult | undefined, side: PlayerSide) {
  return side === 'mario'
    ? (result?.mario_match_wins ?? 0)
    : (result?.luigi_match_wins ?? 0);
}

function sideFromWinner(winner: number | null): PlayerSide | null {
  if (winner === 0) return 'mario';
  if (winner === 1) return 'luigi';
  return null;
}

function displayLives(lives: number, dead: boolean) {
  return dead ? 0 : lives;
}

const toneText: Record<Tone, string> = {
  success: css({ color: 'success.11' }),
  warning: css({ color: 'warning.11' }),
  danger: css({ color: 'danger.11' }),
  gray: css({ color: 'fg.muted' }),
};

const toneDot: Record<Tone, string> = {
  success: css({ color: 'success.9' }),
  warning: css({ color: 'warning.9' }),
  danger: css({ color: 'danger.9' }),
  gray: css({ color: 'gray.9' }),
};

function StatusDot({
  pulse,
  size = 'md',
  tone,
}: {
  pulse: boolean;
  size?: 'sm' | 'md';
  tone: Tone;
}) {
  return (
    <span
      aria-hidden="true"
      className={cx(
        toneDot[tone],
        css({
          bg: 'current',
          borderRadius: 'full',
          boxSize: size === 'sm' ? '1.5' : '2',
          flexShrink: '0',
        }),
        pulse
          ? css({ animation: '[status-ring 1.8s ease-out infinite]' })
          : undefined,
      )}
    />
  );
}

function RulesSummary({ match }: { match: BattleMatchRecord }) {
  const { settings } = match;
  const rules = [
    settings.course_mode === 'random' ? 'ランダム' : 'コース指定',
    `${settings.wins}本先取`,
    `スター${settings.big_stars}`,
    settings.lives === 'endless' ? '残機無限' : `残機${settings.lives}`,
  ];
  return (
    <span className={css({ color: 'fg.muted', textStyle: 'sm' })}>
      {rules.map((rule, index) => (
        <span key={rule}>
          {index > 0 ? (
            <span
              aria-hidden="true"
              className={css({ color: 'gray.8', px: '1.5' })}
            >
              ·
            </span>
          ) : null}
          {rule}
        </span>
      ))}
    </span>
  );
}

const alertStripClass = css({
  borderBottomWidth: '1px',
  borderColor: 'colorPalette.a5',
  borderRadius: '[0]',
  px: '5',
});

function ReconnectAlert({ deadlineMs }: { deadlineMs: number | null }) {
  const now = useNow(deadlineMs !== null);
  const remaining =
    deadlineMs === null
      ? null
      : Math.max(0, Math.ceil((deadlineMs - now) / 1000));

  return (
    <Alert.Root
      className={cx(alertStripClass, css({ alignItems: 'center', gap: '6' }))}
      role="alert"
      status="warning"
    >
      <Alert.Content>
        <Alert.Title>相手との通信が途切れました</Alert.Title>
        <Alert.Description className={css({ color: 'fg.muted' })}>
          <span className={css({ display: 'inline-block' })}>
            60秒以内に戻れば、そのまま続けられます。
          </span>
          <span className={css({ display: 'inline-block' })}>
            melonDS は閉じずにお待ちください。
          </span>
        </Alert.Description>
      </Alert.Content>
      {remaining !== null ? (
        <div
          className={css({
            alignItems: 'flex-end',
            display: 'flex',
            flexDirection: 'column',
            flexShrink: '0',
            gap: '2',
            w: '36',
          })}
        >
          <span
            className={css({
              color: 'warning.11',
              fontVariantNumeric: 'tabular-nums',
              fontWeight: 'semibold',
              lineHeight: 'none',
              textStyle: '2xl',
            })}
          >
            {formatCountdown(remaining)}
          </span>
          <Progress.Root
            aria-label="再接続の残り時間"
            colorPalette="warning"
            max={recoveryWindowSeconds}
            shape="full"
            size="xs"
            value={Math.min(remaining, recoveryWindowSeconds)}
            variant="subtle"
          >
            <Progress.Track>
              <Progress.Indicator />
            </Progress.Track>
          </Progress.Root>
        </div>
      ) : null}
    </Alert.Root>
  );
}

/** 締め切りの表示用に、動いている間だけ現在時刻を更新する */
function useNow(enabled: boolean) {
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    if (!enabled) return;
    const timer = window.setInterval(() => setNow(Date.now()), 250);
    return () => window.clearInterval(timer);
  }, [enabled]);
  return now;
}

function formatCountdown(seconds: number) {
  return `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, '0')}`;
}

function ScoreHero({
  dimmed,
  opponentName,
  opponentWins,
  outcome,
  selfName,
  selfWins,
}: {
  dimmed: boolean;
  opponentName: string;
  opponentWins: number;
  outcome: 'win' | 'loss' | null;
  selfName: string;
  selfWins: number;
}) {
  const nameClass = css({
    fontWeight: 'bold',
    minW: '0',
    overflow: 'hidden',
    textOverflow: 'ellipsis',
    textStyle: 'lg',
    whiteSpace: 'nowrap',
  });
  const scoreClass = (lost: boolean) =>
    css({ color: lost ? 'fg.subtle' : 'fg.default' });

  return (
    <div
      className={cx(
        css({
          alignItems: 'center',
          columnGap: '2',
          display: 'grid',
          gridTemplateColumns: 'minmax(0, 1fr) token(sizes.56) minmax(0, 1fr)',
          pb: '7',
          pt: '8',
          px: '5',
          rowGap: '3',
          transitionDuration: 'normal',
          transitionProperty: '[opacity]',
        }),
        dimmed ? css({ opacity: '0.45' }) : undefined,
        outcome === 'win'
          ? css({
              backgroundImage:
                'radial-gradient(ellipse 60% 100% at 50% 0%, color-mix(in srgb, {colors.amber.9} 9%, transparent), transparent 70%)',
            })
          : undefined,
      )}
    >
      {outcome ? (
        <span
          className={css({
            gridColumn: '2',
            justifySelf: 'center',
            fontWeight: 'bold',
            letterSpacing: '[0.24em]',
            lineHeight: 'tight',
            pl: '[0.24em]',
            textStyle: '3xl',
          })}
        >
          <span
            className={
              outcome === 'win'
                ? css({
                    color: 'amber.9',
                    textShadow:
                      '[0 0 28px color-mix(in srgb, {colors.amber.9} 35%, transparent)]',
                  })
                : css({ color: 'fg.muted' })
            }
          >
            {outcome === 'win' ? '勝利' : '敗北'}
          </span>
        </span>
      ) : null}
      <span
        className={cx(nameClass, css({ gridColumn: '1', justifySelf: 'end' }))}
      >
        {selfName}
      </span>
      <div
        aria-label={`${selfWins} 対 ${opponentWins}`}
        className={css({
          alignItems: 'center',
          display: 'flex',
          fontSize: '6xl',
          fontVariantNumeric: 'tabular-nums',
          fontWeight: 'semibold',
          gap: '4.5',
          gridColumn: '2',
          justifyContent: 'center',
          letterSpacing: '[-0.01em]',
          lineHeight: 'none',
        })}
        role="img"
      >
        <span className={scoreClass(outcome === 'loss')}>{selfWins}</span>
        <span
          className={css({
            color: 'gray.8',
            fontSize: '3xl',
            fontWeight: 'normal',
          })}
        >
          –
        </span>
        <span className={scoreClass(outcome === 'win')}>{opponentWins}</span>
      </div>
      <span className={cx(nameClass, css({ gridColumn: '3' }))}>
        {opponentName}
      </span>
    </div>
  );
}

const gameStateLabel: Record<'live' | 'wait' | 'cut', string> = {
  live: 'プレイ中',
  wait: '通信待ち',
  cut: '中断',
};

const gameStateTone: Record<'live' | 'wait' | 'cut', Tone> = {
  live: 'success',
  wait: 'warning',
  cut: 'danger',
};

function GameTable({
  opponentName,
  opponentSide,
  selfSide,
  slots,
}: {
  opponentName: string;
  opponentSide: PlayerSide;
  selfSide: PlayerSide;
  slots: GameSlot[];
}) {
  // 名前の列はゲームの列の 0.7 倍の幅にする
  const nameColumnWidth = `${(0.7 / (0.7 + slots.length)) * 100}%`;

  return (
    <div
      className={css({ borderTopWidth: '1px', pb: '5', pt: '4.5', px: '5' })}
    >
      <table
        aria-label="ゲームごとの結果"
        className={css({
          borderCollapse: 'separate',
          // セルどうしのすき間は表の外周にも付くので、その分だけ外へ広げる
          borderSpacing: '[{spacing.2} {spacing.1.5}]',
          mx: '-2',
          my: '-1.5',
          tableLayout: 'fixed',
          w: '[calc(100% + {spacing.4})]',
        })}
      >
        <colgroup>
          <col style={{ width: nameColumnWidth }} />
          {slots.map((slot) => (
            <col key={slot.number} />
          ))}
        </colgroup>
        <thead>
          <tr className={css({ color: 'fg.muted', textStyle: 'xs' })}>
            <td />
            {slots.map((slot) => (
              <GameHeader key={slot.number} slot={slot} />
            ))}
          </tr>
        </thead>
        <tbody>
          <GameRow name="あなた" side={selfSide} slots={slots} />
          <GameRow name={opponentName} side={opponentSide} slots={slots} />
        </tbody>
      </table>
    </div>
  );
}

function GameHeader({ slot }: { slot: GameSlot }) {
  const running =
    slot.state === 'live' || slot.state === 'wait' || slot.state === 'cut'
      ? slot.state
      : null;
  const label = [
    `第${slot.number}ゲーム`,
    slot.stage !== null ? stageLabel(slot.stage) : null,
    running ? gameStateLabel[running] : null,
  ]
    .filter(Boolean)
    .join(' ');

  return (
    <th
      aria-label={label}
      className={css({
        fontWeight: 'normal',
        h: '6',
        p: '0',
        textAlign: 'start',
      })}
      scope="col"
    >
      <span
        className={css({
          alignItems: 'center',
          display: 'flex',
          gap: '1.5',
          pl: '2.5',
          whiteSpace: 'nowrap',
        })}
      >
        <span
          className={css({
            color: 'fg.subtle',
            fontVariantNumeric: 'tabular-nums',
          })}
        >
          {slot.number}
        </span>
        {slot.stage !== null ? <span>{stageLabel(slot.stage)}</span> : null}
        {running ? (
          <StatusDot
            pulse={running !== 'cut'}
            size="sm"
            tone={gameStateTone[running]}
          />
        ) : null}
      </span>
    </th>
  );
}

function GameRow({
  name,
  side,
  slots,
}: {
  name: string;
  side: PlayerSide;
  slots: GameSlot[];
}) {
  return (
    <tr>
      <th
        className={css({
          fontWeight: 'semibold',
          overflow: 'hidden',
          p: '0',
          textAlign: 'start',
          textOverflow: 'ellipsis',
          textStyle: 'sm',
          whiteSpace: 'nowrap',
        })}
        scope="row"
      >
        {name}
      </th>
      {slots.map((slot) => (
        <GameCell key={slot.number} side={side} slot={slot} />
      ))}
    </tr>
  );
}

const cellPaddingClass = css({ p: '0' });

const cellBaseClass = css({
  alignItems: 'center',
  borderRadius: 'l2',
  borderWidth: '1px',
  display: 'flex',
  gap: '2',
  h: '11',
  minW: '0',
  pl: '2.5',
  pr: '2',
});

const cellStateClass = {
  won: css({ bg: 'gray.3', borderColor: 'gray.6' }),
  lost: css({ borderColor: 'gray.4' }),
  live: css({ bg: 'success.a2', borderColor: 'success.a6' }),
  wait: css({ bg: 'warning.a2', borderColor: 'warning.a6' }),
  cut: css({
    bg: 'danger.a2',
    borderColor: 'danger.a6',
    borderStyle: 'dashed',
  }),
  // 破線は実線より薄く見えるので、罫線より 1 段濃くする
  empty: css({ borderColor: 'gray.5', borderStyle: 'dashed' }),
};

function GameCell({ side, slot }: { side: PlayerSide; slot: GameSlot }) {
  if (slot.state === 'empty') {
    return (
      <td aria-label="未実施" className={cellPaddingClass}>
        <div className={cx(cellBaseClass, cellStateClass.empty)} />
      </td>
    );
  }
  if (!slot.result) {
    const state = slot.state as 'live' | 'wait' | 'cut';
    return (
      <td className={cellPaddingClass}>
        <div
          className={cx(
            cellBaseClass,
            cellStateClass[state],
            toneText[gameStateTone[state]],
            css({ textStyle: 'xs' }),
          )}
        >
          {gameStateLabel[state]}
        </div>
      </td>
    );
  }

  const player = slot.result[side];
  const lives = displayLives(player.lives, player.dead);
  const won = slot.winner === side;
  const decided = slot.winner !== null;
  const tone = won ? 'won' : decided ? 'lost' : 'cut';

  return (
    <td
      aria-label={`スター ${player.stars}、残機 ${lives}${won ? '、勝ち' : ''}`}
      className={cellPaddingClass}
    >
      <div className={cx(cellBaseClass, cellStateClass[tone])}>
        <span
          className={cx(
            css({
              alignItems: 'center',
              display: 'flex',
              fontVariantNumeric: 'tabular-nums',
              gap: '1',
              textStyle: 'md',
            }),
            won
              ? css({ color: 'fg.default', fontWeight: 'semibold' })
              : css({ color: 'fg.subtle', fontWeight: 'medium' }),
          )}
        >
          <PixelIcon
            className={css({ color: won ? 'fg.muted' : 'gray.8' })}
            path={pixelStar}
          />
          {player.stars}
        </span>
        <span
          className={css({
            alignItems: 'center',
            color: won ? 'fg.muted' : 'fg.subtle',
            display: 'flex',
            fontVariantNumeric: 'tabular-nums',
            gap: '1',
            textStyle: 'xs',
          })}
        >
          <PixelIcon
            className={css({ color: won ? 'fg.muted' : 'gray.8' })}
            path={pixelHeart}
          />
          {lives}
        </span>
      </div>
    </td>
  );
}

// 7×7 のドット絵。拡大してもにじまないよう crispEdges で描く
const pixelStar =
  'M3 0h1v1H3zM2 1h3v1H2zM0 2h7v1H0zM1 3h5v1H1zM2 4h3v1H2zM1 5h2v1H1zM4 5h2v1H4zM1 6h1v1H1zM5 6h1v1H5z';
const pixelHeart =
  'M1 0h2v1H1zM4 0h2v1H4zM0 1h7v1H0zM0 2h7v1H0zM1 3h5v1H1zM2 4h3v1H2zM3 5h1v1H3z';

function PixelIcon({ className, path }: { className?: string; path: string }) {
  return (
    <svg
      aria-hidden="true"
      className={className}
      height="14"
      shapeRendering="crispEdges"
      viewBox="0 0 7 7"
      width="14"
    >
      <path d={path} fill="currentColor" />
    </svg>
  );
}
