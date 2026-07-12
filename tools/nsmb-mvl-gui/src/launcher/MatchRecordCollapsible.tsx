import { Portal } from '@ark-ui/react';
import {
  CaretDown,
  ChartLineUp,
  CircleNotch,
  CloudArrowUp,
  FileZip,
  FolderOpen,
  MinusCircle,
  Star,
  Trash,
  Trophy,
  XCircle,
} from '@phosphor-icons/react';
import type { ReactNode } from 'react';
import { useState } from 'react';
import { css, cx } from 'styled-system/css';
import lifeMushroom from '../assets/life-mushroom.png';
import playerLBadge from '../assets/player-l.png';
import playerMBadge from '../assets/player-m.png';
import {
  Badge,
  Button,
  CloseButton,
  Collapsible,
  Dialog,
} from '../components/ui';
import type { MvlStageResult, Role } from '../types';
import { stageLabel } from './options';
import type { BattleMatchRecord } from './types';

type PlayerSide = 'mario' | 'luigi';
type MatchOutcome = 'win' | 'loss' | 'stopped' | 'running' | 'complete';

export function MatchRecordCollapsible({
  defaultOpen,
  match,
  onDelete,
  onCreateLogArchive,
  onOpenLogDir,
  onSelectOpponent,
  onUploadLogArchive,
  showStageDots = true,
  showStartedAt = true,
}: {
  defaultOpen: boolean;
  match: BattleMatchRecord;
  onCreateLogArchive?: () => Promise<void> | void;
  onDelete?: () => Promise<void> | void;
  onOpenLogDir?: () => Promise<void> | void;
  onSelectOpponent?: (playerId: string, playerName: string) => void;
  onUploadLogArchive?: () => Promise<void> | void;
  showStageDots?: boolean;
  showStartedAt?: boolean;
}) {
  const [open, setOpen] = useState(defaultOpen);
  const latestStage = match.stages.at(-1);
  const selfSide = sideFromRole(match.role);
  const opponentSide = oppositeSide(selfSide);
  const selfWins = matchWinsFor(latestStage, selfSide);
  const opponentWins = matchWinsFor(latestStage, opponentSide);
  const winner = matchWinner(match);
  const outcome = matchOutcome(match, selfSide);
  const playedStages = match.stages
    .map((result, index) => ({ index, result }))
    .filter(({ result }) => sideFromWinner(result.winner));
  const triggerGridClassName = summaryTriggerGridClass({
    showStageDots,
    showStartedAt,
  });

  return (
    <Collapsible.Root
      className={css({
        borderBottomColor: 'gray.surface.border',
        borderBottomWidth: '1px',
        display: 'grid',
        _last: { borderBottomWidth: '0' },
        '&[data-state=open]': {
          bg: 'black.a3',
        },
      })}
      open={open}
      onOpenChange={(details) => setOpen(details.open)}
    >
      <Collapsible.Trigger
        className={cx(
          css({
            alignItems: 'center',
            cursor: 'pointer',
            display: 'grid',
            gap: '3',
            listStyle: 'none',
            minH: '14',
            px: '3',
            py: '2',
            transition: 'common',
            _hover: { bg: 'white.a1' },
          }),
          triggerGridClassName,
        )}
        type="button"
      >
        <OutcomeBadge outcome={outcome} />
        {showStartedAt ? (
          <div
            className={css({
              color: 'fg.muted',
              display: 'grid',
              fontWeight: 'bold',
              gap: '0.5',
              lineHeight: 'tight',
              textStyle: 'xs',
            })}
          >
            <span>{formatTime(match.startedAt)}</span>
            <span>{formatShortDate(match.startedAt)}</span>
          </div>
        ) : null}
        <div
          className={css({
            alignItems: 'center',
            display: 'grid',
            gap: '3',
            gridTemplateColumns: {
              base: 'minmax(4.75rem, 8rem) 3.25rem minmax(4.75rem, 8rem)',
              lg: 'minmax(6.5rem, 10rem) 3.25rem minmax(6.5rem, 10rem)',
            },
            justifyContent: 'center',
            maxW: 'full',
            minW: '0',
          })}
        >
          <PlayerSummary
            side={selfSide}
            name={match.playerNames[selfSide]}
            isWinner={winner === selfSide}
            position="left"
          />
          <div
            className={css({
              color: 'fg.default',
              display: 'grid',
              fontWeight: 'black',
              justifyItems: 'center',
              lineHeight: 'none',
              textStyle: 'xl',
              whiteSpace: 'nowrap',
            })}
          >
            {selfWins} - {opponentWins}
          </div>
          <PlayerSummary
            side={opponentSide}
            name={match.playerNames[opponentSide]}
            isWinner={winner === opponentSide}
            position="right"
          />
        </div>
        {showStageDots ? <StageDots match={match} selfSide={selfSide} /> : null}
        <Collapsible.Indicator
          className={css({
            color: 'fg.muted',
            justifySelf: 'end',
            transition: 'transform',
            '&[data-state=open]': { transform: 'rotate(180deg)' },
          })}
        >
          <CaretDown size={18} weight="bold" />
        </Collapsible.Indicator>
      </Collapsible.Trigger>
      <Collapsible.Content
        className={css({
          borderTopColor: 'gray.surface.border',
          borderTopWidth: '1px',
          display: 'grid',
          gap: '2',
          px: '4',
          py: '3',
        })}
      >
        {onSelectOpponent ? (
          <div className={css({ display: 'flex', justifyContent: 'flex-end' })}>
            <Button
              onClick={() => {
                const opponentSide = selfSide === 'mario' ? 'luigi' : 'mario';
                setOpen(false);
                onSelectOpponent(
                  match.playerIds[opponentSide],
                  match.playerNames[opponentSide],
                );
              }}
              size="xs"
              variant="plain"
            >
              <ChartLineUp size={16} weight="bold" />
              {match.playerNames[selfSide === 'mario' ? 'luigi' : 'mario']}
              との戦績を見る
            </Button>
          </div>
        ) : null}
        <div
          className={css({
            borderColor: 'gray.surface.border',
            borderRadius: 'l2',
            borderWidth: '1px',
            overflowX: 'auto',
            overflowY: 'hidden',
          })}
        >
          <table
            className={css({
              borderCollapse: 'collapse',
              minW: '[28rem]',
              tableLayout: 'fixed',
              textStyle: 'xs',
              w: 'full',
            })}
          >
            <colgroup>
              <col className={css({ w: '[3.25rem]' })} />
              <col className={css({ w: '[4.25rem]' })} />
              <col className={css({ w: '[7rem]' })} />
              <col className={css({ w: '[3.25rem]' })} />
              <col className={css({ w: '[3.25rem]' })} />
              <col className={css({ w: '[3.25rem]' })} />
              <col className={css({ w: '[3.25rem]' })} />
            </colgroup>
            <thead>
              <tr
                className={css({
                  bg: 'white.a1',
                  color: 'fg.muted',
                  fontWeight: 'black',
                  textAlign: 'left',
                })}
              >
                <StageHeader rowSpan={2}>ゲーム</StageHeader>
                <StageHeader rowSpan={2}>ステージ</StageHeader>
                <StageHeader rowSpan={2}>勝者</StageHeader>
                <StageHeader align="center" colSpan={2}>
                  {match.playerNames[selfSide]}
                </StageHeader>
                <StageHeader align="center" colSpan={2}>
                  {match.playerNames[opponentSide]}
                </StageHeader>
              </tr>
              <tr
                className={css({
                  bg: 'white.a1',
                  color: 'fg.muted',
                  fontWeight: 'black',
                  textAlign: 'left',
                })}
              >
                <StageHeader align="center" compact>
                  スター
                </StageHeader>
                <StageHeader align="center" compact>
                  ライフ
                </StageHeader>
                <StageHeader align="center" compact>
                  スター
                </StageHeader>
                <StageHeader align="center" compact>
                  ライフ
                </StageHeader>
              </tr>
            </thead>
            <tbody>
              {playedStages.map(({ index, result }) => (
                <StageResultTableRow
                  index={index}
                  key={`${match.id}-${result.game_index}`}
                  match={match}
                  result={result}
                  selfSide={selfSide}
                />
              ))}
            </tbody>
          </table>
        </div>
        {match.logDir || onDelete ? (
          <div
            className={css({
              alignItems: { base: 'stretch', md: 'center' },
              display: 'grid',
              gap: '2',
              gridTemplateColumns: { base: '1fr', md: 'minmax(0, 1fr) auto' },
            })}
          >
            {match.logDir ? (
              <code
                className={css({
                  color: 'fg.subtle',
                  fontFamily: 'mono',
                  fontWeight: 'semibold',
                  minW: '0',
                  overflowWrap: 'anywhere',
                  textStyle: 'xs',
                })}
              >
                {match.logDir}
              </code>
            ) : (
              <span />
            )}
            <div
              className={css({
                display: 'flex',
                flexWrap: 'wrap',
                gap: '2',
                justifyContent: { base: 'flex-start', md: 'flex-end' },
              })}
            >
              {match.logDir && onOpenLogDir ? (
                <LogActionButton
                  icon={<FolderOpen size={16} weight="bold" />}
                  label="ログを開く"
                  onClick={onOpenLogDir}
                />
              ) : null}
              {match.logDir && onCreateLogArchive ? (
                <LogActionButton
                  icon={<FileZip size={16} weight="bold" />}
                  label="zipを作成"
                  onClick={onCreateLogArchive}
                />
              ) : null}
              {match.logDir && onUploadLogArchive ? (
                <LogActionButton
                  icon={<CloudArrowUp size={16} weight="bold" />}
                  label="ログを送信"
                  onClick={onUploadLogArchive}
                />
              ) : null}
              {onDelete ? <DeleteMatchButton onDelete={onDelete} /> : null}
            </div>
          </div>
        ) : null}
      </Collapsible.Content>
    </Collapsible.Root>
  );
}

function LogActionButton({
  icon,
  label,
  onClick,
}: {
  icon: ReactNode;
  label: string;
  onClick: () => Promise<void> | void;
}) {
  const [busy, setBusy] = useState(false);

  return (
    <Button
      disabled={busy}
      loading={busy}
      onClick={async () => {
        setBusy(true);
        try {
          await onClick();
        } finally {
          setBusy(false);
        }
      }}
      size="xs"
      variant="outline"
    >
      {icon}
      {label}
    </Button>
  );
}

function DeleteMatchButton({
  onDelete,
}: {
  onDelete: () => Promise<void> | void;
}) {
  const [confirming, setConfirming] = useState(false);
  const [deleting, setDeleting] = useState(false);

  return (
    <Dialog.Root
      open={confirming}
      onOpenChange={(details) => setConfirming(details.open)}
    >
      <Dialog.Trigger asChild>
        <Button
          aria-label="対戦履歴を削除"
          className={css({
            color: 'red.plain.fg',
          })}
          size="xs"
          variant="outline"
        >
          <Trash size={16} weight="bold" />
          削除
        </Button>
      </Dialog.Trigger>
      <Portal>
        <Dialog.Backdrop />
        <Dialog.Positioner>
          <Dialog.Content
            className={css({
              maxW: 'md',
              w: 'full',
            })}
          >
            <Dialog.CloseTrigger>
              <CloseButton />
            </Dialog.CloseTrigger>
            <Dialog.Header>
              <Dialog.Title>対戦履歴を削除しますか？</Dialog.Title>
              <Dialog.Description>
                この対戦履歴はローカルの保存データから削除されます。
              </Dialog.Description>
            </Dialog.Header>
            <Dialog.Footer>
              <Dialog.ActionTrigger asChild>
                <Button disabled={deleting} variant="outline">
                  キャンセル
                </Button>
              </Dialog.ActionTrigger>
              <Button
                colorPalette="red"
                loading={deleting}
                onClick={async () => {
                  setDeleting(true);
                  try {
                    await onDelete();
                    setConfirming(false);
                  } finally {
                    setDeleting(false);
                  }
                }}
                variant="solid"
              >
                <Trash size={16} weight="bold" />
                削除する
              </Button>
            </Dialog.Footer>
          </Dialog.Content>
        </Dialog.Positioner>
      </Portal>
    </Dialog.Root>
  );
}

function summaryTriggerGridClass({
  showStageDots,
  showStartedAt,
}: {
  showStageDots: boolean;
  showStartedAt: boolean;
}) {
  if (showStartedAt && showStageDots) {
    return css({
      gridTemplateColumns: {
        base: '5.25rem 3.25rem minmax(0, 1fr) 6.25rem 1rem',
        lg: '5.25rem 3.25rem minmax(0, 1fr) 7.5rem 1rem',
      },
    });
  }
  if (showStartedAt) {
    return css({
      gridTemplateColumns: '5.25rem 3.25rem minmax(0, 1fr) 1rem',
    });
  }
  if (showStageDots) {
    return css({
      gridTemplateColumns: {
        base: '5.25rem minmax(0, 1fr) 6.25rem 1rem',
        lg: '5.25rem minmax(0, 1fr) 7.5rem 1rem',
      },
    });
  }
  return css({
    gridTemplateColumns: '5.25rem minmax(0, 1fr) 1rem',
  });
}

function OutcomeBadge({ outcome }: { outcome: MatchOutcome }) {
  const config: Record<
    MatchOutcome,
    {
      colorPalette: 'gray' | 'green' | 'red' | 'yellow' | 'blue';
      icon: ReactNode;
      label: string;
    }
  > = {
    win: {
      colorPalette: 'yellow',
      icon: <Trophy size={15} weight="fill" />,
      label: '勝利',
    },
    loss: {
      colorPalette: 'red',
      icon: <XCircle size={15} weight="fill" />,
      label: '敗北',
    },
    stopped: {
      colorPalette: 'gray',
      icon: <MinusCircle size={15} weight="fill" />,
      label: '中断',
    },
    running: {
      colorPalette: 'blue',
      icon: <CircleNotch size={15} weight="bold" />,
      label: '対戦中',
    },
    complete: {
      colorPalette: 'gray',
      icon: <Trophy size={15} weight="fill" />,
      label: '完了',
    },
  };
  const selected = config[outcome];

  return (
    <Badge
      colorPalette={selected.colorPalette}
      size="lg"
      variant={outcome === 'win' ? 'surface' : 'subtle'}
      css={{
        ...(outcome === 'win'
          ? {
              bg: 'yellow.subtle.bg',
              borderColor: 'yellow.outline.border',
              borderWidth: '1px',
              color: 'yellow.plain.fg',
            }
          : {}),
        ...(outcome === 'stopped'
          ? {
              bg: 'gray.surface.bg',
              borderColor: 'gray.surface.border',
              borderWidth: '1px',
              color: 'fg.muted',
            }
          : {}),
        fontWeight: 'black',
        justifyContent: 'center',
        minW: '0',
        w: 'full',
      }}
    >
      {selected.icon}
      {selected.label}
    </Badge>
  );
}

function PlayerSummary({
  isWinner,
  name,
  position,
  side,
}: {
  isWinner: boolean;
  name: string;
  position: 'left' | 'right';
  side: PlayerSide;
}) {
  return (
    <div
      className={css({
        alignItems: 'center',
        display: 'flex',
        gap: '2',
        justifyContent: position === 'left' ? 'flex-end' : 'flex-start',
        justifySelf: position === 'left' ? 'end' : 'start',
        maxW: 'full',
        minW: '0',
        overflow: 'hidden',
        whiteSpace: 'nowrap',
        w: 'full',
      })}
      data-player-position={position}
    >
      <img
        src={side === 'mario' ? playerMBadge : playerLBadge}
        alt=""
        className={css({
          flexShrink: '0',
          objectFit: 'contain',
        })}
        style={{ height: 28, width: 28 }}
      />
      <div
        className={css({
          display: 'grid',
          gap: '0.5',
          maxW: 'full',
          minW: '0',
        })}
      >
        <div
          className={css({
            alignItems: 'center',
            color: isWinner ? 'yellow.plain.fg' : 'fg.default',
            display: 'flex',
            fontWeight: 'black',
            gap: '1.5',
            minW: '0',
            textStyle: 'sm',
          })}
        >
          <span
            className={css({
              overflow: 'hidden',
              textOverflow: 'ellipsis',
              whiteSpace: 'nowrap',
            })}
          >
            {name}
          </span>
        </div>
      </div>
    </div>
  );
}

function StageDots({
  match,
  selfSide,
}: {
  match: BattleMatchRecord;
  selfSide: PlayerSide;
}) {
  const plannedStages = match.settings.course_stages.slice(
    0,
    Math.max(match.settings.wins * 2 - 1, match.stages.length),
  );

  return (
    <div
      className={css({
        alignItems: 'center',
        display: 'grid',
        justifyItems: 'end',
        minW: '0',
      })}
      data-testid="stage-dots"
    >
      <div
        className={css({
          display: 'grid',
          gap: '1',
          justifyContent: 'end',
        })}
        style={{
          gridTemplateColumns: `repeat(${plannedStages.length}, 16px)`,
        }}
      >
        {plannedStages.map((_, index) => {
          const result = match.stages[index];
          const winner = sideFromWinner(result?.winner);
          return (
            <div
              className={css({
                alignItems: 'center',
                display: 'grid',
                gap: '0.5',
                justifyItems: 'center',
                minW: '0',
              })}
              key={`${match.id}-dot-${index}`}
            >
              <span
                className={css({
                  color: 'fg.muted',
                  fontSize: '[10px]',
                  fontWeight: 'bold',
                  lineHeight: 'none',
                })}
              >
                {index + 1}
              </span>
              <span
                className={css({
                  alignItems: 'center',
                  bg: !winner
                    ? 'gray.surface.bg'
                    : winner === 'mario'
                      ? 'red.subtle.bg'
                      : 'green.subtle.bg',
                  borderColor:
                    winner === selfSide
                      ? 'yellow.outline.border'
                      : 'gray.surface.border',
                  borderRadius: 'full',
                  borderWidth: '1px',
                  color: !winner
                    ? 'fg.subtle'
                    : winner === 'mario'
                      ? 'red.subtle.fg'
                      : 'green.subtle.fg',
                  display: 'inline-flex',
                  flexShrink: '0',
                  fontSize: '[10px]',
                  fontWeight: 'black',
                  justifyContent: 'center',
                  lineHeight: 'none',
                })}
                style={{ height: 16, width: 16 }}
              >
                {winner ? sideInitial(winner) : '-'}
              </span>
            </div>
          );
        })}
      </div>
    </div>
  );
}

function StageHeader({
  align = 'left',
  children,
  colSpan,
  compact = false,
  rowSpan,
}: {
  align?: 'center' | 'left';
  children: ReactNode;
  colSpan?: number;
  compact?: boolean;
  rowSpan?: number;
}) {
  return (
    <th
      colSpan={colSpan}
      rowSpan={rowSpan}
      className={css({
        borderBottomColor: 'gray.surface.border',
        borderBottomWidth: '1px',
        px: compact ? '1' : '1.5',
        py: compact ? '1' : '1.5',
        textAlign: align,
        whiteSpace: 'nowrap',
      })}
    >
      {children}
    </th>
  );
}

function StageCell({
  align = 'left',
  children,
}: {
  align?: 'center' | 'left';
  children: ReactNode;
}) {
  return (
    <td
      className={css({
        borderBottomColor: 'gray.surface.border',
        borderBottomWidth: '1px',
        color: 'fg.default',
        fontWeight: 'bold',
        minW: '0',
        overflow: 'hidden',
        px: '1.5',
        py: '1.5',
        textAlign: align,
        textOverflow: 'ellipsis',
        verticalAlign: 'middle',
        whiteSpace: 'nowrap',
      })}
    >
      {children}
    </td>
  );
}

function StageResultTableRow({
  index,
  match,
  result,
  selfSide,
}: {
  index: number;
  match: BattleMatchRecord;
  result: MvlStageResult;
  selfSide: PlayerSide;
}) {
  const winner = sideFromWinner(result.winner);
  if (!winner) {
    return null;
  }
  const winnerName = match.playerNames[winner];
  const opponentSide = oppositeSide(selfSide);
  const selfResult = result[selfSide];
  const opponentResult = result[opponentSide];

  return (
    <tr className={css({ _hover: { bg: 'white.a1' } })}>
      <StageCell>{index + 1}</StageCell>
      <StageCell>
        {stageLabel(result.stage ?? match.settings.course_stages[index] ?? 0)}
      </StageCell>
      <StageCell>
        <span
          className={css({
            alignItems: 'center',
            color: winner === 'mario' ? 'red.plain.fg' : 'green.plain.fg',
            display: 'inline-flex',
            gap: '1',
            maxW: 'full',
            minW: '0',
          })}
        >
          <img
            src={winner === 'mario' ? playerMBadge : playerLBadge}
            alt=""
            className={css({
              flexShrink: '0',
              objectFit: 'contain',
            })}
            style={{ height: 16, width: 16 }}
          />
          <span
            className={css({ overflow: 'hidden', textOverflow: 'ellipsis' })}
          >
            {winnerName}
          </span>
        </span>
      </StageCell>
      <StageCell align="center">
        <StageMetric type="star" value={selfResult.stars} />
      </StageCell>
      <StageCell align="center">
        <StageMetric
          type="life"
          value={displayLives(selfResult.lives, selfResult.dead)}
        />
      </StageCell>
      <StageCell align="center">
        <StageMetric type="star" value={opponentResult.stars} />
      </StageCell>
      <StageCell align="center">
        <StageMetric
          type="life"
          value={displayLives(opponentResult.lives, opponentResult.dead)}
        />
      </StageCell>
    </tr>
  );
}

function StageMetric({
  type,
  value,
}: {
  type: 'life' | 'star';
  value?: number;
}) {
  if (value === undefined) {
    return <span className={css({ color: 'fg.subtle' })}>-</span>;
  }

  return (
    <span
      className={css({
        alignItems: 'center',
        display: 'inline-flex',
        gap: '1.5',
        justifyContent: 'center',
        lineHeight: 'none',
        minW: '0',
      })}
    >
      <span
        className={css({
          color: 'fg.default',
          fontVariantNumeric: 'tabular-nums',
          fontWeight: 'bold',
          minW: '[1.2em]',
          textAlign: 'right',
        })}
      >
        {value}
      </span>
      {type === 'star' ? (
        <Star
          className={css({
            color: 'yellow.plain.fg',
            flexShrink: '0',
          })}
          size={14}
          weight="fill"
        />
      ) : (
        <img
          src={lifeMushroom}
          alt=""
          className={css({
            flexShrink: '0',
            objectFit: 'contain',
          })}
          style={{ height: 14, width: 14 }}
        />
      )}
    </span>
  );
}

function matchOutcome(
  match: BattleMatchRecord,
  selfSide: PlayerSide,
): MatchOutcome {
  const winner = matchWinner(match);
  if (winner) {
    return winner === selfSide ? 'win' : 'loss';
  }
  if (match.status === 'stopped') {
    return 'stopped';
  }
  if (match.status === 'running') {
    return 'running';
  }
  return 'complete';
}

function matchWinner(match: BattleMatchRecord): PlayerSide | null {
  const latest = match.stages.at(-1);
  if (!latest) {
    return null;
  }
  if (latest.mario_match_wins >= latest.target_wins) {
    return 'mario';
  }
  if (latest.luigi_match_wins >= latest.target_wins) {
    return 'luigi';
  }
  return null;
}

function sideFromRole(role: Role): PlayerSide {
  return role === 'host' ? 'mario' : 'luigi';
}

function oppositeSide(side: PlayerSide): PlayerSide {
  return side === 'mario' ? 'luigi' : 'mario';
}

function matchWinsFor(result: MvlStageResult | undefined, side: PlayerSide) {
  return side === 'mario'
    ? (result?.mario_match_wins ?? 0)
    : (result?.luigi_match_wins ?? 0);
}

function sideFromWinner(winner?: number | null): PlayerSide | null {
  if (winner === 0) {
    return 'mario';
  }
  if (winner === 1) {
    return 'luigi';
  }
  return null;
}

function sideInitial(side: PlayerSide) {
  return side === 'mario' ? 'M' : 'L';
}

function displayLives(lives?: number, dead?: boolean) {
  if (lives === undefined) {
    return undefined;
  }
  return dead ? 0 : lives;
}

function formatShortDate(value: string) {
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) {
    return value;
  }
  return date.toLocaleDateString('ja-JP', {
    day: '2-digit',
    month: '2-digit',
  });
}

function formatTime(value: string) {
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) {
    return '--:--';
  }
  return date.toLocaleTimeString('ja-JP', {
    hour: '2-digit',
    minute: '2-digit',
  });
}
