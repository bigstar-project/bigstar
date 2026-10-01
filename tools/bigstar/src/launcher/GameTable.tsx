import { css, cx } from 'styled-system/css';
import { maxGamesForWins } from '../form';
import type { MvlStageResult } from '../types';
import { stageLabel } from './options';
import { StatusDot, type StatusTone } from './StatusDot';
import type { BattleMatchRecord } from './types';

export type PlayerSide = 'mario' | 'luigi';

export type MatchPhase =
  | 'live'
  | 'reconnecting'
  | 'timeout'
  | 'stopped'
  | 'finished';

type Tone = StatusTone;

/** 1 ゲーム分の列の状態。live / wait / cut は結果がまだ出ていないゲーム */
type GameState = 'decided' | 'live' | 'wait' | 'cut' | 'empty';

export type GameSlot = {
  /** 1 から始まるゲーム番号。枠の並びは変わらないので key にも使う */
  number: number;
  result: MvlStageResult | null;
  stage: number | null;
  state: GameState;
  winner: PlayerSide | null;
};

export function gameSlots(
  match: BattleMatchRecord,
  phase: MatchPhase,
): GameSlot[] {
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
        // 結果にコースが入っていないときは、部屋で決めたコースを出す
        stage: result.stage ?? match.settings.course_stages[index] ?? null,
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

export function matchWinsFor(
  result: MvlStageResult | undefined,
  side: PlayerSide,
) {
  return side === 'mario'
    ? (result?.mario_match_wins ?? 0)
    : (result?.luigi_match_wins ?? 0);
}

export function sideFromWinner(winner: number | null): PlayerSide | null {
  if (winner === 0) return 'mario';
  if (winner === 1) return 'luigi';
  return null;
}

function displayLives(lives: number, dead: boolean) {
  return dead ? 0 : lives;
}

export const toneText: Record<Tone, string> = {
  success: css({ color: 'success.11' }),
  warning: css({ color: 'warning.11' }),
  danger: css({ color: 'danger.11' }),
  gray: css({ color: 'fg.muted' }),
};

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

/** 自分と相手の 2 行で、ゲームごとのスターと残機を並べる表。対戦画面と履歴で使う */
export function GameTable({
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
