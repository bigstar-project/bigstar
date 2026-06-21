import { Clock, Heart, Question, Star, Trophy } from '@phosphor-icons/react';
import type { ReactNode } from 'react';
import { css } from 'styled-system/css';
import playerLBadge from '../assets/player-l.png';
import playerMBadge from '../assets/player-m.png';
import { Badge, Card } from '../components/ui';
import type { MvlStageResult } from '../types';
import type { BattleMatchRecord } from './types';

export function MatchResultCard({
  match,
  title = '対戦状況',
}: {
  match: BattleMatchRecord;
  title?: string;
}) {
  const latestStage = match.stages.at(-1);
  const marioWins = latestStage?.mario_match_wins ?? 0;
  const luigiWins = latestStage?.luigi_match_wins ?? 0;
  const finalWinner = matchWinner(match);
  const plannedStages = match.settings.course_stages.slice(
    0,
    Math.max(match.settings.wins * 2 - 1, match.stages.length),
  );

  return (
    <Card.Root
      variant="outline"
      css={{
        bg: 'app.card',
        backdropFilter: 'auto',
        backdropBlur: 'md',
        backdropSaturate: '180%',
        display: 'grid',
        gap: '3',
        p: '3.5',
      }}
    >
      <div
        className={css({
          alignItems: { base: 'stretch', md: 'start' },
          display: 'flex',
          flexDirection: { base: 'column', md: 'row' },
          gap: '2',
          justifyContent: 'space-between',
        })}
      >
        <div className={css({ display: 'grid', gap: '1' })}>
          <div
            className={css({
              alignItems: 'center',
              color: 'fg.default',
              display: 'flex',
              fontWeight: 'black',
              gap: '1.5',
              textStyle: 'md',
            })}
          >
            <Trophy
              className={css({ color: 'yellow.plain.fg' })}
              size={20}
              weight="fill"
            />
            {title}
          </div>
          <div
            className={css({
              color: 'fg.muted',
              fontWeight: 'bold',
              overflowWrap: 'anywhere',
              textStyle: 'sm',
            })}
          >
            {formatStartedAt(match.startedAt)} / room {match.roomCode}
          </div>
        </div>
        <div
          className={css({
            alignItems: 'center',
            display: 'flex',
            flexWrap: 'wrap',
            gap: '2',
            justifyContent: { base: 'flex-start', md: 'flex-end' },
          })}
        >
          <Badge colorPalette={statusPalette(match)} variant="subtle">
            {statusLabel(match)}
          </Badge>
          <Badge colorPalette="gray" variant="outline">
            {match.settings.wins}本先取
          </Badge>
        </div>
      </div>

      <div
        className={css({
          alignItems: 'center',
          bg: 'gray.surface.bg',
          borderColor: 'gray.surface.border',
          borderRadius: 'l2',
          borderWidth: '1px',
          display: 'grid',
          gap: '2',
          gridTemplateColumns: {
            base: '1fr',
            sm: 'minmax(0, 1fr) auto minmax(0, 1fr)',
          },
          p: '3',
        })}
      >
        <PlayerScore
          imageSrc={playerMBadge}
          name={match.playerNames.mario}
          score={marioWins}
          winner={finalWinner === 0}
        />
        <div
          className={css({
            color: 'fg.default',
            fontWeight: 'black',
            justifySelf: 'center',
            textStyle: '2xl',
          })}
        >
          {marioWins} - {luigiWins}
        </div>
        <PlayerScore
          align="end"
          imageSrc={playerLBadge}
          name={match.playerNames.luigi}
          score={luigiWins}
          winner={finalWinner === 1}
        />
      </div>

      <div className={css({ display: 'grid', gap: '2' })}>
        {plannedStages.map((stage, index) => (
          <StageResultRow
            key={`${match.id}-${index}`}
            result={match.stages[index] ?? null}
            match={match}
            stage={stage}
            index={index}
          />
        ))}
      </div>

      {match.logDir ? (
        <code
          className={css({
            color: 'fg.subtle',
            fontFamily: 'mono',
            fontWeight: 'semibold',
            overflowWrap: 'anywhere',
            textStyle: 'xs',
          })}
        >
          {match.logDir}
        </code>
      ) : null}
    </Card.Root>
  );
}

export function EmptyMatchResultCard({
  message,
  title,
}: {
  message: string;
  title: string;
}) {
  return (
    <Card.Root
      variant="outline"
      css={{
        bg: 'app.card',
        backdropFilter: 'auto',
        backdropBlur: 'md',
        display: 'grid',
        gap: '2',
        p: '3.5',
      }}
    >
      <div
        className={css({
          alignItems: 'center',
          color: 'fg.default',
          display: 'flex',
          fontWeight: 'black',
          gap: '1.5',
          textStyle: 'md',
        })}
      >
        <Clock className={css({ color: 'blue.plain.fg' })} size={20} />
        {title}
      </div>
      <div
        className={css({
          color: 'fg.muted',
          fontWeight: 'bold',
          textStyle: 'sm',
        })}
      >
        {message}
      </div>
    </Card.Root>
  );
}

function PlayerScore({
  align = 'start',
  imageSrc,
  name,
  score,
  winner,
}: {
  align?: 'start' | 'end';
  imageSrc: string;
  name: string;
  score: number;
  winner: boolean;
}) {
  return (
    <div
      className={css({
        alignItems: 'center',
        display: 'flex',
        flexDirection: align === 'end' ? 'row-reverse' : 'row',
        gap: '2',
        justifyContent: align === 'end' ? 'flex-end' : 'flex-start',
        minW: '0',
      })}
    >
      <img
        src={imageSrc}
        alt=""
        className={css({ h: '9', objectFit: 'contain', w: '9' })}
      />
      <div
        className={css({
          display: 'grid',
          gap: '0.5',
          justifyItems: align === 'end' ? 'end' : 'start',
          minW: '0',
        })}
      >
        <div
          className={css({
            color: winner ? 'yellow.plain.fg' : 'fg.default',
            fontWeight: 'black',
            textStyle: 'md',
          })}
        >
          {name}
        </div>
        <div
          className={css({
            color: 'fg.muted',
            fontWeight: 'bold',
            textStyle: 'sm',
          })}
        >
          {score}勝
        </div>
      </div>
    </div>
  );
}

function StageResultRow({
  index,
  match,
  result,
  stage,
}: {
  index: number;
  match: BattleMatchRecord;
  result: MvlStageResult | null;
  stage: number;
}) {
  const winnerLabel =
    result?.winner === 0
      ? `${match.playerNames.mario} 勝利`
      : result?.winner === 1
        ? `${match.playerNames.luigi} 勝利`
        : '未確定';
  return (
    <div
      className={css({
        alignItems: 'center',
        bg: 'gray.surface.bg',
        borderColor: result ? 'gray.surface.border' : 'gray.5',
        borderRadius: 'l2',
        borderWidth: '1px',
        display: 'grid',
        gap: '2',
        gridTemplateColumns: {
          base: '1fr',
          lg: '7rem minmax(0, 1fr) minmax(0, 1fr)',
        },
        p: '2.5',
      })}
    >
      <div className={css({ display: 'grid', gap: '1' })}>
        <div
          className={css({
            color: 'fg.default',
            fontWeight: 'black',
            textStyle: 'sm',
          })}
        >
          Game {index + 1}
        </div>
        <div
          className={css({
            color: 'fg.muted',
            fontWeight: 'bold',
            textStyle: 'xs',
          })}
        >
          Stage {result?.stage ?? stage}
        </div>
      </div>
      <div
        className={css({
          alignItems: 'center',
          color: result?.winner === 0 ? 'red.plain.fg' : 'fg.default',
          display: 'flex',
          flexWrap: 'wrap',
          fontWeight: 'black',
          gap: '2',
          textStyle: 'sm',
        })}
      >
        {result ? (
          result.resolved ? (
            <Trophy size={18} weight="fill" />
          ) : (
            <Question size={18} weight="fill" />
          )
        ) : (
          <Clock size={18} />
        )}
        {result ? winnerLabel : '未プレイ'}
      </div>
      <div
        className={css({
          color: 'fg.muted',
          display: 'flex',
          flexWrap: 'wrap',
          fontWeight: 'bold',
          gap: '2',
          textStyle: 'sm',
        })}
      >
        <Metric icon={<Star size={16} weight="fill" />}>
          {formatPair(result?.mario.stars, result?.luigi.stars)}
        </Metric>
        <Metric icon={<Heart size={16} weight="fill" />}>
          {formatPair(
            displayLives(result?.mario.lives, result?.mario.dead),
            displayLives(result?.luigi.lives, result?.luigi.dead),
          )}
        </Metric>
      </div>
    </div>
  );
}

function Metric({ children, icon }: { children: string; icon: ReactNode }) {
  return (
    <span
      className={css({
        alignItems: 'center',
        display: 'inline-flex',
        gap: '1',
      })}
    >
      {icon}
      {children}
    </span>
  );
}

function matchWinner(match: BattleMatchRecord) {
  const latest = match.stages.at(-1);
  if (!latest) {
    return null;
  }
  if (latest.mario_match_wins >= latest.target_wins) {
    return 0;
  }
  if (latest.luigi_match_wins >= latest.target_wins) {
    return 1;
  }
  return null;
}

function statusLabel(match: BattleMatchRecord) {
  const winner = matchWinner(match);
  if (winner === 0) {
    return `${match.playerNames.mario} 勝利`;
  }
  if (winner === 1) {
    return `${match.playerNames.luigi} 勝利`;
  }
  if (match.status === 'running') {
    return '対戦中';
  }
  if (match.status === 'stopped') {
    return '中断';
  }
  return '完了';
}

function statusPalette(
  match: BattleMatchRecord,
): 'gray' | 'green' | 'red' | 'yellow' {
  if (matchWinner(match) !== null) {
    return 'yellow';
  }
  if (match.status === 'running') {
    return 'green';
  }
  if (match.status === 'stopped') {
    return 'red';
  }
  return 'gray';
}

function formatPair(left?: number, right?: number) {
  if (left === undefined || right === undefined) {
    return '- / -';
  }
  return `${left} / ${right}`;
}

function displayLives(lives?: number, dead?: boolean) {
  if (lives === undefined) {
    return undefined;
  }
  return dead ? 0 : lives;
}

function formatStartedAt(value: string) {
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) {
    return value;
  }
  return date.toLocaleString('ja-JP', {
    day: '2-digit',
    hour: '2-digit',
    minute: '2-digit',
    month: '2-digit',
  });
}
