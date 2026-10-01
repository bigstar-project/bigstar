import { Info } from '@phosphor-icons/react';
import { useEffect, useState } from 'react';
import { css, cx } from 'styled-system/css';
import * as Alert from '@/components/ui/alert';
import { Button } from '@/components/ui/button';
import * as Card from '@/components/ui/card';
import * as Progress from '@/components/ui/progress';
import { localPlayerSide, opponentPlayerSide } from '../matchHistory';
import {
  GameTable,
  gameSlots,
  type MatchPhase,
  matchWinsFor,
  toneText,
} from './GameTable';
import { StatusDot, type StatusTone } from './StatusDot';
import type { BattleMatchRecord, ConnectionStatusState } from './types';

// 相手の復帰を待つ時間。bigstar-net-bridge の再接続待ちと合わせる
const recoveryWindowSeconds = 60;

type Tone = StatusTone;

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
      <Card.Root variant="raised">
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
              <Alert.Description>
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

        <div
          className={css({
            borderTopWidth: '1px',
            pb: '5',
            pt: '4.5',
            px: '5',
          })}
        >
          <GameTable
            opponentName={match.playerNames[opponentSide]}
            opponentSide={opponentSide}
            selfSide={selfSide}
            slots={slots}
          />
        </div>
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
  borderRadius: 'none',
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
        <Alert.Description>
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
          transition: 'opacity',
          transitionDuration: 'normal',
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
            letterSpacing: 'widest',
            lineHeight: 'tight',
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
