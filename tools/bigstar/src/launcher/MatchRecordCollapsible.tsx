import { useState } from 'react';
import { css, cx } from 'styled-system/css';
import * as AlertDialog from '@/components/ui/alert-dialog';
import { Button } from '@/components/ui/button';
import * as Collapsible from '@/components/ui/collapsible';
import { localPlayerSide, opponentPlayerSide } from '../matchHistory';
import { FeedbackDialog } from './FeedbackDialog';
import {
  type GameSlot,
  GameTable,
  gameSlots,
  matchWinsFor,
  type PlayerSide,
} from './GameTable';
import { stageLabel } from './options';
import { roomRuleParts } from './roomRules';
import type { BattleMatchRecord, FeedbackInput } from './types';

type MatchOutcome = 'win' | 'loss' | 'stopped' | 'running' | 'complete';

const outcomeLabel: Record<MatchOutcome, string> = {
  win: '勝利',
  loss: '敗北',
  stopped: '中断',
  running: '対戦中',
  complete: '完了',
};

const outcomeClass: Record<MatchOutcome, string> = {
  win: css({ color: 'amber.9', fontWeight: 'bold' }),
  loss: css({ color: 'fg.subtle', fontWeight: 'semibold' }),
  stopped: css({ color: 'danger.11', fontWeight: 'semibold' }),
  running: css({ color: 'success.11', fontWeight: 'semibold' }),
  complete: css({ color: 'fg.muted', fontWeight: 'semibold' }),
};

/** 対戦ログの 1 行。押すと、ゲームごとの結果とその対戦への操作を開く */
export function MatchRecordCollapsible({
  match,
  onDelete,
  onOpenLogDir,
  onSelectOpponent,
  onUploadLogArchive,
}: {
  match: BattleMatchRecord;
  onDelete?: () => Promise<void> | void;
  onOpenLogDir?: () => Promise<void> | void;
  onSelectOpponent?: (playerId: string, playerName: string) => void;
  onUploadLogArchive?: (feedback: FeedbackInput) => Promise<string | null>;
}) {
  const [open, setOpen] = useState(false);
  const selfSide = localPlayerSide(match);
  const opponentSide = opponentPlayerSide(match);
  const opponentName = match.playerNames[opponentSide];
  const latest = match.stages.at(-1);
  const selfWins = matchWinsFor(latest, selfSide);
  const opponentWins = matchWinsFor(latest, opponentSide);
  const outcome = matchOutcome(match, selfSide);
  const slots = gameSlots(match, 'finished');
  const time = formatTime(match.startedAt);
  const score = `${selfWins}–${opponentWins}`;
  // 部屋の一覧と同じ並びにする。コースはゲームごとに出すので、先頭のコースの項目は外す
  const rules = roomRuleParts({
    bigStars: match.settings.big_stars,
    courseMode: match.settings.course_mode,
    lives: match.settings.lives,
    stageCount: match.settings.course_stages.length,
    wins: match.settings.wins,
  }).slice(1);

  return (
    <Collapsible.Root
      className={rowClass}
      data-history-match=""
      open={open}
      onOpenChange={setOpen}
    >
      <Collapsible.Trigger className={triggerClass}>
        <span
          className={cx(
            css({ textStyle: 'sm', whiteSpace: 'nowrap' }),
            outcomeClass[outcome],
          )}
        >
          {outcomeLabel[outcome]}
        </span>
        <span
          className={css({
            display: 'flex',
            flexDirection: 'column',
            gap: '1',
            minW: '0',
          })}
        >
          <span
            className={css({
              fontWeight: 'semibold',
              textStyle: 'md',
              truncate: true,
            })}
          >
            <span
              className={css({
                color: 'fg.subtle',
                fontWeight: 'normal',
                pr: '1.5',
              })}
            >
              vs
            </span>
            {opponentName}
          </span>
          <span
            className={css({
              color: 'fg.subtle',
              fontVariantNumeric: 'tabular-nums',
              textStyle: 'xs',
              truncate: true,
            })}
          >
            {[time, ...rules].join(' · ')}
          </span>
        </span>
        <StageChips selfSide={selfSide} slots={slots} />
        <span
          className={css({
            color: outcome === 'win' ? 'fg.default' : 'fg.subtle',
            fontVariantNumeric: 'tabular-nums',
            fontWeight: 'semibold',
            textAlign: 'end',
            textStyle: 'lg',
          })}
        >
          {score}
        </span>
        <Collapsible.Indicator />
      </Collapsible.Trigger>
      <Collapsible.Panel>
        <div className={detailsClass} data-match-details-body="">
          {/* ゲームが少ないときに枠が横に伸びすぎないよう、1 ゲームの幅に上限を付ける */}
          <div style={{ maxWidth: `${(slots.length + 0.7) * 7}rem` }}>
            <GameTable
              opponentName={opponentName}
              opponentSide={opponentSide}
              selfSide={selfSide}
              slots={slots}
            />
          </div>
          <div
            className={css({
              alignItems: 'center',
              display: 'flex',
              flexWrap: 'wrap',
              gap: '1',
            })}
          >
            {onSelectOpponent ? (
              <Button
                colorPalette="gray"
                size="xs"
                variant="subtle"
                onClick={() => {
                  setOpen(false);
                  onSelectOpponent(match.playerIds[opponentSide], opponentName);
                }}
              >
                {opponentName}との戦績を見る
              </Button>
            ) : null}
            {match.logDir && onOpenLogDir ? (
              <OpenLogButton onClick={onOpenLogDir} />
            ) : null}
            {match.logDir && onUploadLogArchive ? (
              <FeedbackDialog
                matchDate={formatDateTime(match.startedAt)}
                matchSummary={`${opponentName} 戦 ${score}`}
                onSubmit={onUploadLogArchive}
              />
            ) : null}
            {onDelete ? <DeleteMatchButton onDelete={onDelete} /> : null}
          </div>
        </div>
      </Collapsible.Panel>
    </Collapsible.Root>
  );
}

const rowClass = css({
  // 行が多くても、画面の外の行は描かずに済ませる
  containIntrinsicSize: '[auto 4rem]',
  contentVisibility: 'auto',
  '&:not(:first-child)': { borderTopWidth: '1px' },
  '&:has(> [data-panel-open])': { bg: 'gray.a2' },
});

const triggerClass = css({
  borderRadius: 'none',
  columnGap: '4',
  display: 'grid',
  focusVisibleRing: 'inside',
  gridTemplateColumns: '2.5rem minmax(0, 1fr) auto 3rem auto',
  h: '16',
  pl: '5',
  pr: '4.5',
  py: '0',
  textAlign: 'start',
  _hover: { bg: 'gray.a2' },
});

const detailsClass = css({
  display: 'flex',
  flexDirection: 'column',
  gap: '4',
  pb: '5',
  pr: '5',
  // 広いときは、結果の文字の下から始まるように左をそろえる
  pl: {
    base: '5',
    lg: '[calc({spacing.5} + {spacing.10} + {spacing.4})]',
  },
});

/** 遊んだコースを順に並べる。勝ったコースを明るく、中断したコースを赤い取り消し線で示す */
function StageChips({
  selfSide,
  slots,
}: {
  selfSide: PlayerSide;
  slots: GameSlot[];
}) {
  return (
    <span className={css({ alignItems: 'center', display: 'flex', gap: '3' })}>
      {slots.map((slot) => {
        const result =
          slot.winner === null
            ? 'cut'
            : slot.winner === selfSide
              ? 'won'
              : 'lost';
        return (
          <span
            key={slot.number}
            className={cx(
              css({ textStyle: 'sm', whiteSpace: 'nowrap' }),
              stageChipClass[result],
            )}
          >
            {slot.stage !== null
              ? stageLabel(slot.stage)
              : `第${slot.number}ゲーム`}
            <span className={css({ srOnly: true })}>
              （{stageResultLabel[result]}）
            </span>
          </span>
        );
      })}
    </span>
  );
}

const stageChipClass = {
  won: css({ color: 'fg.default', fontWeight: 'semibold' }),
  lost: css({ color: 'fg.subtle' }),
  cut: css({
    color: 'danger.11',
    textDecorationColor: 'danger.a7',
    textDecorationLine: 'line-through',
  }),
};

const stageResultLabel = { won: '勝ち', lost: '負け', cut: '中断' };

function OpenLogButton({ onClick }: { onClick: () => Promise<void> | void }) {
  const [busy, setBusy] = useState(false);

  return (
    <Button
      colorPalette="gray"
      loading={busy}
      size="xs"
      variant="plain"
      onClick={async () => {
        setBusy(true);
        try {
          await onClick();
        } finally {
          setBusy(false);
        }
      }}
    >
      ログを開く
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
    <AlertDialog.Root open={confirming} size="sm" onOpenChange={setConfirming}>
      <AlertDialog.Trigger
        aria-label="対戦履歴を削除"
        className={css({ ml: 'auto' })}
        render={<Button colorPalette="danger" size="xs" variant="outline" />}
      >
        削除…
      </AlertDialog.Trigger>
      <AlertDialog.Portal>
        <AlertDialog.Backdrop />
        <AlertDialog.Popup>
          <AlertDialog.Header>
            <AlertDialog.Title>対戦履歴を削除しますか？</AlertDialog.Title>
            <AlertDialog.Description>
              この対戦履歴はローカルの保存データから削除されます。
            </AlertDialog.Description>
          </AlertDialog.Header>
          <AlertDialog.Footer>
            <AlertDialog.Close
              disabled={deleting}
              render={
                <Button colorPalette="gray" variant="subtle">
                  キャンセル
                </Button>
              }
            />
            <Button
              colorPalette="danger"
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
            >
              削除する
            </Button>
          </AlertDialog.Footer>
        </AlertDialog.Popup>
      </AlertDialog.Portal>
    </AlertDialog.Root>
  );
}

function matchOutcome(
  match: BattleMatchRecord,
  selfSide: PlayerSide,
): MatchOutcome {
  const latest = match.stages.at(-1);
  const winner: PlayerSide | null = !latest
    ? null
    : latest.mario_match_wins >= latest.target_wins
      ? 'mario'
      : latest.luigi_match_wins >= latest.target_wins
        ? 'luigi'
        : null;
  if (winner) return winner === selfSide ? 'win' : 'loss';
  if (match.status === 'stopped') return 'stopped';
  if (match.status === 'running') return 'running';
  return 'complete';
}

function formatTime(value: string) {
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return '--:--';
  return date.toLocaleTimeString('ja-JP', {
    hour: '2-digit',
    minute: '2-digit',
  });
}

/** 「6/21 19:40」 */
function formatDateTime(value: string) {
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return value;
  return `${date.getMonth() + 1}/${date.getDate()} ${formatTime(value)}`;
}
