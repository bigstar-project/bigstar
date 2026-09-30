import { useEffect, useState } from 'react';
import { css, cx } from 'styled-system/css';
import { card } from 'styled-system/recipes';
import { Button } from '@/components/ui/button';
import { Spinner } from '@/components/ui/spinner';
import { StatusDot } from './StatusDot';
import type { UpdateStatus, View } from './types';

const currentAppVersion = __BIGSTAR_GUI_VERSION__;

/** サイドバーに出す、進行中の対戦や部屋の状態 */
export type SidebarSession =
  | { kind: 'hosting'; sinceMs: number }
  | { kind: 'connecting' }
  | {
      kind: 'match';
      opponentName: string;
      opponentWins: number;
      selfWins: number;
    }
  | { kind: 'reconnecting'; deadlineMs: number | null }
  | { kind: 'cpu'; opponentName: string }
  | { kind: 'solo-test' };

const sessionView: Record<SidebarSession['kind'], View> = {
  hosting: 'battle',
  connecting: 'battle',
  match: 'battle',
  reconnecting: 'battle',
  cpu: 'cpu',
  'solo-test': 'solo-test',
};

const cardClass = cx(
  card({ variant: 'raised' }).root,
  css({ gap: '1.5', mb: '3', p: '3', textAlign: 'left' }),
);

const cardTitleClass = css({
  alignItems: 'center',
  display: 'flex',
  fontWeight: 'semibold',
  gap: '2',
  textStyle: 'sm',
});

const cardBodyClass = css({ color: 'fg.muted', textStyle: 'xs' });

export function SessionCard({
  onViewChange,
  session,
}: {
  onViewChange: (view: View) => void;
  session: SidebarSession;
}) {
  const ticking = session.kind === 'hosting' || session.kind === 'reconnecting';
  const now = useNow(ticking);
  const { detail, title, warn } = sessionText(session, now);
  return (
    <button
      className={cx(
        cardClass,
        css({
          cursor: 'pointer',
          gap: '1',
          transition: 'colors',
          _hover: { bg: 'gray.3' },
          _focusVisible: { focusVisibleRing: 'outside' },
        }),
      )}
      onClick={() => onViewChange(sessionView[session.kind])}
      type="button"
    >
      <span className={cardTitleClass}>
        <StatusDot tone={warn ? 'warning' : 'success'} />
        {title}
      </span>
      {detail ? (
        <span
          className={cx(
            cardBodyClass,
            // 点（2）と間（2）のぶん下げて、見出しの文字にそろえる
            css({ fontVariantNumeric: 'tabular-nums', pl: '4' }),
          )}
        >
          {detail}
        </span>
      ) : null}
    </button>
  );
}

function sessionText(
  session: SidebarSession,
  now: number,
): { detail?: string; title: string; warn?: boolean } {
  switch (session.kind) {
    case 'hosting':
      return {
        title: '部屋を公開中',
        detail: `相手待ち · ${formatElapsed(now - session.sinceMs)}`,
      };
    case 'connecting':
      return { title: '接続中', detail: '相手と接続しています' };
    case 'match':
      return {
        title: '対戦中',
        detail: `vs ${session.opponentName} · ${session.selfWins}–${session.opponentWins}`,
      };
    case 'reconnecting':
      return {
        title: '再接続中',
        detail:
          session.deadlineMs === null
            ? '相手の復帰を待っています'
            : `残り ${Math.max(0, Math.ceil((session.deadlineMs - now) / 1000))}秒`,
        warn: true,
      };
    case 'cpu':
      return { title: 'CPU対戦中', detail: `vs ${session.opponentName}` };
    case 'solo-test':
      return { title: 'ひとり検証中' };
  }
}

export function RomCard() {
  return (
    <output className={cardClass}>
      <span className={cardTitleClass}>
        <Spinner className={css({ color: 'fg.muted' })} size="xs" />
        ROMを生成中
      </span>
    </output>
  );
}

/** 更新が要るとき、取得中、失敗したときだけ出す。確認中と最新はフッターに出す */
export function UpdateCard({
  busy,
  onCheckForUpdate,
  updateStatus,
}: {
  busy: boolean;
  onCheckForUpdate: () => void;
  updateStatus: UpdateStatus;
}) {
  const version = updateStatus.version ? `v${updateStatus.version}` : null;
  switch (updateStatus.phase) {
    case 'available':
      return (
        <output className={cardClass}>
          <span className={cardTitleClass}>更新が必要です</span>
          <span className={cardBodyClass}>
            {version ? `${version} に` : ''}
            更新するまで部屋の作成・参加はできません
          </span>
          <Button
            className={css({ mt: '1.5' })}
            colorPalette="amber"
            disabled={busy}
            onClick={onCheckForUpdate}
            size="xs"
          >
            更新して再起動
          </Button>
        </output>
      );
    case 'downloading':
    case 'installed':
      return (
        <output className={cardClass}>
          <span className={cardTitleClass}>
            <Spinner className={css({ color: 'fg.muted' })} size="xs" />
            {version ? `${version} に更新中` : '更新中'}
          </span>
          <span className={cardBodyClass}>
            ダウンロードと適用が終わると自動で再起動します
          </span>
        </output>
      );
    case 'error':
      return (
        <output className={cardClass}>
          <span className={cx(cardTitleClass, css({ color: 'danger.11' }))}>
            更新できませんでした
          </span>
          <span className={cardBodyClass}>更新の確認か適用に失敗しました</span>
          <Button
            className={css({ mt: '1.5' })}
            colorPalette="gray"
            disabled={busy}
            onClick={onCheckForUpdate}
            size="xs"
            variant="outline"
          >
            もう一度試す
          </Button>
        </output>
      );
    default:
      return null;
  }
}

export function SidebarFooter({
  busy,
  onCheckForUpdate,
  playerName,
  updateStatus,
}: {
  busy: boolean;
  onCheckForUpdate: () => void;
  playerName: string;
  updateStatus: UpdateStatus;
}) {
  const { phase } = updateStatus;
  return (
    <div
      className={css({
        borderTopColor: 'gray.3',
        borderTopWidth: '1px',
        display: 'flex',
        flexDirection: 'column',
        gap: '1.5',
        pt: '3.5',
        px: '1.5',
      })}
    >
      <span
        className={css({
          color: playerName ? 'fg.default' : 'fg.subtle',
          fontWeight: 'semibold',
          overflow: 'hidden',
          textOverflow: 'ellipsis',
          textStyle: 'sm',
          whiteSpace: 'nowrap',
        })}
        title={playerName || undefined}
      >
        {playerName || 'プレイヤー名 未設定'}
      </span>
      <div
        className={css({
          alignItems: 'center',
          display: 'flex',
          fontSize: 'xs',
          h: '5.5',
          justifyContent: 'space-between',
        })}
      >
        <span
          className={css({
            alignItems: 'center',
            color: 'fg.subtle',
            display: 'flex',
            fontVariantNumeric: 'tabular-nums',
            gap: '1.5',
          })}
          title={`現在のバージョン v${currentAppVersion}`}
        >
          <span>v{currentAppVersion}</span>
          {phase === 'none' ? (
            <span className={css({ color: 'success.11' })}>最新</span>
          ) : null}
        </span>
        {phase === 'none' || phase === 'idle' ? (
          <button
            className={css({
              color: 'fg.muted',
              cursor: 'pointer',
              py: '0.5',
              textDecorationColor: 'gray.7',
              textDecorationLine: 'underline',
              textUnderlineOffset: '3px',
              _hover: { color: 'fg.default' },
              _disabled: { cursor: 'default', opacity: '0.5' },
              _focusVisible: { focusVisibleRing: 'outside' },
            })}
            disabled={busy}
            onClick={onCheckForUpdate}
            type="button"
          >
            更新を確認
          </button>
        ) : null}
        {phase === 'checking' ? (
          <output
            className={css({
              alignItems: 'center',
              color: 'fg.muted',
              display: 'flex',
              gap: '1.5',
            })}
          >
            <Spinner size="inherit" />
            確認中…
          </output>
        ) : null}
      </div>
    </div>
  );
}

/** 経過時間や残り秒数の表示のために、動いている間だけ現在時刻を更新する */
export function useNow(enabled: boolean, intervalMs = 1000) {
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    if (!enabled) return;
    setNow(Date.now());
    const timer = window.setInterval(() => setNow(Date.now()), intervalMs);
    return () => window.clearInterval(timer);
  }, [enabled, intervalMs]);
  return now;
}

export function formatElapsed(ms: number) {
  const seconds = Math.max(0, Math.floor(ms / 1000));
  const minutes = Math.floor(seconds / 60);
  if (minutes >= 60) {
    return `${Math.floor(minutes / 60)}:${String(minutes % 60).padStart(2, '0')}:${String(seconds % 60).padStart(2, '0')}`;
  }
  return `${minutes}:${String(seconds % 60).padStart(2, '0')}`;
}
