import {
  ArrowClockwise,
  Copy,
  DownloadSimple,
  Info,
  Plus,
  Stop,
  Warning,
} from '@phosphor-icons/react';
import { type ReactNode, useState } from 'react';
import { css, cx } from 'styled-system/css';
import { button } from 'styled-system/recipes';
import { Button } from '@/components/ui/button';
import * as Dialog from '@/components/ui/dialog';
import * as Tabs from '@/components/ui/tabs';
import type { FormState } from '../types';
import { CreateRoomDialog } from './CreateRoomDialog';
import { MatchView } from './MatchView';
import { PageHeader } from './PageHeader';
import { PixelStar, PixelVersus } from './PixelIcons';
import { formatRoomAge, roomRuleParts } from './roomRules';
import { formatElapsed, useNow } from './SidebarStatus';
import type {
  BattleMatchRecord,
  ConnectionStatusState,
  LauncherActions,
  LauncherSummary,
  MatchmakingRoomsState,
  UpdateFormField,
} from './types';

type Room = MatchmakingRoomsState['rooms'][number];

export function BattleView({
  actions,
  connectionStatus,
  form,
  matchmakingRooms,
  currentMatch,
  onOpenHistory,
  onReturnToLobby,
  showMatch,
  summary,
  updateBusy = false,
  updateField,
}: {
  actions: Pick<
    LauncherActions,
    | 'checkForUpdate'
    | 'copyRoomCode'
    | 'cancelHostedRoom'
    | 'createRoom'
    | 'joinRoom'
    | 'refreshRooms'
    | 'stopMatch'
  >;
  connectionStatus: ConnectionStatusState;
  form: FormState;
  matchmakingRooms: MatchmakingRoomsState;
  currentMatch: BattleMatchRecord | null;
  onOpenHistory: () => void;
  onReturnToLobby: () => void;
  /** 対戦中と、終わった対戦を閉じるまでは、ロビーの代わりに対戦画面を出す */
  showMatch: boolean;
  summary: LauncherSummary;
  updateBusy?: boolean;
  updateField: UpdateFormField;
}) {
  const [createOpen, setCreateOpen] = useState(false);

  if (showMatch && currentMatch) {
    return (
      <Tabs.Panel className={panelClass} keepMounted value="battle">
        <PageHeader title="対戦" />
        <MatchView
          canStop={summary.connectionActive}
          connection={connectionStatus}
          match={currentMatch}
          onOpenHistory={onOpenHistory}
          onReturnToLobby={onReturnToLobby}
          onStop={() => void actions.stopMatch()}
        />
      </Tabs.Panel>
    );
  }

  const { hostedRoom } = matchmakingRooms;
  const matchmakingDisabled =
    summary.connectionActive ||
    summary.updateRequired ||
    Boolean(matchmakingRooms.hostedRoomId);
  const otherRooms = matchmakingRooms.rooms.filter(
    (room) => room.room_id !== matchmakingRooms.hostedRoomId,
  );

  let headerAction: ReactNode = null;
  if (summary.connectionActive) {
    headerAction = (
      <Button
        colorPalette="gray"
        onClick={() => void actions.stopMatch()}
        variant="subtle"
      >
        <Stop weight="fill" />
        停止
      </Button>
    );
  } else if (!hostedRoom) {
    headerAction = (
      <Dialog.Trigger
        className={cx(
          button({ size: 'md' }),
          css({ colorPalette: 'amber', fontWeight: 'bold', pl: '4', pr: '5' }),
        )}
        disabled={matchmakingDisabled || matchmakingRooms.busy}
      >
        <Plus weight="bold" />
        部屋を作る
      </Dialog.Trigger>
    );
  }

  return (
    <Tabs.Panel className={panelClass} keepMounted value="battle">
      <Dialog.Root open={createOpen} onOpenChange={setCreateOpen}>
        <PageHeader actions={headerAction} title="対戦" />
        <CreateRoomDialog
          busy={matchmakingRooms.busy}
          disabled={matchmakingDisabled}
          form={form}
          onClose={() => setCreateOpen(false)}
          onCreate={actions.createRoom}
          updateField={updateField}
        />
      </Dialog.Root>

      {summary.updateRequired ? (
        <UpdateRequiredNotice
          busy={updateBusy}
          onUpdate={() => void actions.checkForUpdate()}
          version={summary.updateVersion}
        />
      ) : null}

      {hostedRoom ? (
        <>
          <HostingCard
            busy={matchmakingRooms.busy}
            hostedRoom={hostedRoom}
            onCancel={() => void actions.cancelHostedRoom()}
            onCopy={() => void actions.copyRoomCode()}
            playerName={form.hostName.trim()}
          />
          {otherRooms.length > 0 ? (
            <p
              className={css({
                alignItems: 'center',
                color: 'fg.subtle',
                display: 'flex',
                fontSize: '[13px]',
                gap: '2',
                mt: '-1',
              })}
            >
              <Info size={14} weight="bold" />
              ほかに {otherRooms.length}{' '}
              部屋が募集中です。部屋を閉じると参加できます。
            </p>
          ) : null}
        </>
      ) : (
        <RoomsSection
          busy={matchmakingRooms.busy}
          canCreate={!matchmakingDisabled && !matchmakingRooms.busy}
          disabled={matchmakingDisabled}
          error={matchmakingRooms.error}
          loading={matchmakingRooms.loading}
          onCreate={() => setCreateOpen(true)}
          onJoin={(roomId) => void actions.joinRoom(roomId)}
          onRefresh={() => void actions.refreshRooms()}
          refreshDisabled={matchmakingRooms.refreshDisabled}
          rooms={otherRooms}
        />
      )}
    </Tabs.Panel>
  );
}

const panelClass = css({
  display: 'flex',
  flexDirection: 'column',
  gap: '6',
  outline: 'none',
});

const cardClass = css({
  bg: 'gray.2',
  borderColor: 'gray.4',
  borderRadius: 'l3',
  borderWidth: '1px',
});

function RoomsSection({
  busy,
  canCreate,
  disabled,
  error,
  loading,
  onCreate,
  onJoin,
  onRefresh,
  refreshDisabled,
  rooms,
}: {
  busy: boolean;
  canCreate: boolean;
  disabled: boolean;
  error: string | null;
  loading: boolean;
  onCreate: () => void;
  onJoin: (roomId: string) => void;
  onRefresh: () => void;
  refreshDisabled: boolean;
  rooms: Room[];
}) {
  const [joiningRoomId, setJoiningRoomId] = useState<string | null>(null);
  const now = useNow(rooms.length > 0, 30_000);

  return (
    <section
      aria-labelledby="public-rooms-title"
      className={css({ display: 'flex', flexDirection: 'column', mt: '3' })}
    >
      <div
        className={css({
          alignItems: 'center',
          display: 'flex',
          justifyContent: 'space-between',
          pb: '3.5',
        })}
      >
        <div
          className={css({ alignItems: 'center', display: 'flex', gap: '2.5' })}
        >
          <h2
            className={css({ fontSize: 'md', fontWeight: 'bold' })}
            id="public-rooms-title"
          >
            公開ルーム
          </h2>
          {error ? null : (
            <span
              className={css({
                bg: 'gray.4',
                borderRadius: 'full',
                fontSize: '[12.5px]',
                fontVariantNumeric: 'tabular-nums',
                fontWeight: 'semibold',
                lineHeight: '[22px]',
                minW: '[22px]',
                px: '[7px]',
                textAlign: 'center',
              })}
            >
              <span aria-hidden="true">{rooms.length}</span>
              <span className={css({ srOnly: true })}>
                募集中 {rooms.length} 件
              </span>
            </span>
          )}
        </div>
        <div
          className={css({ alignItems: 'center', display: 'flex', gap: '3.5' })}
        >
          <span
            className={cx(
              css({
                alignItems: 'center',
                display: 'flex',
                fontSize: '[12.5px]',
                gap: '[7px]',
              }),
              error ? css({ color: 'danger.11' }) : css({ color: 'fg.subtle' }),
            )}
          >
            <span
              aria-hidden="true"
              className={cx(
                css({ borderRadius: 'full', boxSize: '1.5' }),
                error ? css({ bg: 'danger.9' }) : css({ bg: 'success.9' }),
              )}
            />
            {error ? '接続できません' : '自動で反映'}
          </span>
          <Button
            aria-label="一覧を再読み込み"
            className={css({ color: 'fg.muted' })}
            colorPalette="gray"
            disabled={refreshDisabled}
            loading={loading}
            onClick={onRefresh}
            size="xs"
            variant="outline"
          >
            <ArrowClockwise weight="bold" />
          </Button>
        </div>
      </div>

      {error ? (
        <div
          className={css({
            alignItems: 'center',
            bg: 'danger.2',
            borderColor: 'danger.6',
            borderRadius: 'l3',
            borderWidth: '1px',
            display: 'flex',
            gap: '3.5',
            px: '5',
            py: '[18px]',
          })}
          role="alert"
        >
          <Warning
            className={css({ color: 'danger.11', flexShrink: '0' })}
            size={18}
            weight="bold"
          />
          <div
            className={css({
              display: 'flex',
              flexDirection: 'column',
              flexGrow: '1',
              gap: '1',
            })}
          >
            <span className={css({ fontSize: '[15px]', fontWeight: 'bold' })}>
              公開ルームを取得できませんでした
            </span>
            <span className={css({ color: 'fg.muted', fontSize: '[13px]' })}>
              インターネット接続を確認して、再読み込みしてください。
            </span>
          </div>
          <Button
            colorPalette="gray"
            disabled={refreshDisabled}
            loading={loading}
            onClick={onRefresh}
            size="sm"
            variant="subtle"
          >
            再読み込み
          </Button>
        </div>
      ) : rooms.length === 0 ? (
        <div
          className={css({
            alignItems: 'center',
            borderColor: 'gray.4',
            borderRadius: 'l3',
            borderStyle: 'dashed',
            borderWidth: '1px',
            display: 'flex',
            flexDirection: 'column',
            gap: '2.5',
            pb: '[60px]',
            pt: '14',
            px: '6',
            textAlign: 'center',
          })}
        >
          <PixelStar
            className={css({ color: 'gray.4' })}
            eyeColor="var(--colors-canvas)"
            size={48}
          />
          <span
            className={css({
              fontSize: '[17px]',
              fontWeight: 'bold',
              mt: '2.5',
            })}
          >
            いま募集中の部屋はありません
          </span>
          <span
            className={css({
              color: 'fg.muted',
              display: 'flex',
              flexDirection: 'column',
              fontSize: '[13.5px]',
              lineHeight: '[1.75]',
            })}
          >
            <span>部屋を作ると、ここに表示されて相手を待てます。</span>
            <span>
              設定で「新しい部屋の通知」をオンにすると、部屋ができたときにお知らせします。
            </span>
          </span>
          {canCreate ? (
            <Button
              className={css({ mt: '3.5' })}
              colorPalette="gray"
              onClick={onCreate}
              size="sm"
              variant="subtle"
            >
              部屋を作って待つ
            </Button>
          ) : null}
        </div>
      ) : (
        <ul
          className={css({
            display: 'flex',
            flexDirection: 'column',
            gap: '2',
          })}
        >
          {rooms.map((room) => (
            <RoomRow
              key={room.room_id}
              disabled={disabled || busy || !room.can_join}
              joining={busy && joiningRoomId === room.room_id}
              now={now}
              room={room}
              onJoin={() => {
                setJoiningRoomId(room.room_id);
                onJoin(room.room_id);
              }}
            />
          ))}
        </ul>
      )}
    </section>
  );
}

function RoomRow({
  disabled,
  joining,
  now,
  onJoin,
  room,
}: {
  disabled: boolean;
  joining: boolean;
  now: number;
  onJoin: () => void;
  room: Room;
}) {
  const [course, ...rules] = roomRuleParts({
    bigStars: room.settings.big_stars,
    courseMode: room.settings.course_mode,
    lives: room.settings.lives,
    stageCount: room.settings.course_stages.length,
    wins: room.settings.wins,
  });
  return (
    <li
      className={cx(
        cardClass,
        css({
          alignItems: 'center',
          display: 'flex',
          gap: '5',
          minH: '[72px]',
          pl: '5',
          pr: '4',
          py: '3.5',
        }),
      )}
    >
      <div
        className={css({
          display: 'flex',
          flexDirection: 'column',
          flexGrow: '1',
          gap: '[5px]',
          minW: '0',
        })}
      >
        <span
          className={css({
            fontSize: '[15px]',
            fontWeight: 'semibold',
            overflow: 'hidden',
            textOverflow: 'ellipsis',
            whiteSpace: 'nowrap',
          })}
        >
          {room.host_name}
        </span>
        <RuleList
          className={css({ fontSize: '[13px]' })}
          parts={[course, ...rules]}
        />
      </div>
      <span
        className={css({
          color: 'fg.subtle',
          flexShrink: '0',
          fontSize: '[12.5px]',
          whiteSpace: 'nowrap',
        })}
      >
        {formatRoomAge(room.created_at, now)}
      </span>
      <Button
        aria-label={`${room.host_name} の部屋に参加`}
        className={css({ flexShrink: '0', w: '[84px]' })}
        colorPalette="gray"
        disabled={disabled}
        loading={joining}
        onClick={onJoin}
        size="sm"
        variant="subtle"
      >
        参加
      </Button>
    </li>
  );
}

/** 「ランダム · 3本先取 · スター10 · 残機3」。最初の項目（コース）を少し明るくする */
function RuleList({
  className,
  parts,
}: {
  className?: string;
  parts: string[];
}) {
  return (
    <span
      className={cx(
        css({
          alignItems: 'center',
          color: 'fg.muted',
          display: 'flex',
          flexWrap: 'wrap',
          gap: '2',
        }),
        className,
      )}
    >
      {parts.map((part, index) => (
        <span
          key={part}
          className={css({ alignItems: 'center', display: 'flex', gap: '2' })}
        >
          {index > 0 ? (
            <span aria-hidden="true" className={css({ color: 'gray.7' })}>
              ·
            </span>
          ) : null}
          <span className={index === 0 ? css({ color: 'gray.12' }) : undefined}>
            {part}
          </span>
        </span>
      ))}
    </span>
  );
}

function HostingCard({
  busy,
  hostedRoom,
  onCancel,
  onCopy,
  playerName,
}: {
  busy: boolean;
  hostedRoom: NonNullable<MatchmakingRoomsState['hostedRoom']>;
  onCancel: () => void;
  onCopy: () => void;
  playerName: string;
}) {
  const now = useNow(true);
  const { form } = hostedRoom;
  const rules = roomRuleParts({
    bigStars: form.bigStars,
    courseMode: form.courseMode,
    lives: form.lives,
    stageCount: form.courseStages.length,
    wins: form.wins,
  });
  return (
    <section
      aria-label="あなたの部屋"
      className={cx(
        cardClass,
        css({
          borderRadius: '[14px]',
          display: 'flex',
          flexDirection: 'column',
        }),
      )}
    >
      <div
        className={css({
          alignItems: 'center',
          display: 'flex',
          justifyContent: 'space-between',
          pt: '[18px]',
          px: '6',
        })}
      >
        <span
          className={css({
            alignItems: 'center',
            display: 'flex',
            fontSize: 'sm',
            fontWeight: 'semibold',
            gap: '2.5',
          })}
        >
          <span
            aria-hidden="true"
            className={css({
              animation: '[status-ring 1.8s ease-out infinite]',
              bg: 'current',
              borderRadius: 'full',
              boxSize: '2',
              color: 'success.9',
            })}
          />
          部屋を公開中
        </span>
        <span className={css({ color: 'fg.muted', fontSize: '[13px]' })}>
          経過{' '}
          <span
            className={css({
              color: 'fg.default',
              fontVariantNumeric: 'tabular-nums',
              fontWeight: 'semibold',
            })}
          >
            {formatElapsed(now - hostedRoom.createdAtMs)}
          </span>
        </span>
      </div>

      <div
        className={css({
          alignItems: 'center',
          display: 'grid',
          gridTemplateColumns: '[minmax(0, 1fr) 72px minmax(0, 1fr)]',
          pb: '2',
          pt: '[22px]',
          px: '6',
        })}
      >
        <div
          className={css({
            bg: 'gray.3',
            borderColor: 'gray.5',
            borderRadius: 'l3',
            borderWidth: '1px',
            display: 'flex',
            flexDirection: 'column',
            gap: '1.5',
            h: '[104px]',
            justifyContent: 'center',
            px: '[22px]',
          })}
        >
          <span className={css({ color: 'fg.muted', fontSize: '[12.5px]' })}>
            あなた
          </span>
          <span
            className={css({
              fontSize: '[19px]',
              fontWeight: 'bold',
              overflow: 'hidden',
              textOverflow: 'ellipsis',
              whiteSpace: 'nowrap',
            })}
          >
            {playerName || 'プレイヤー'}
          </span>
        </div>
        <span
          className={css({
            color: 'gray.6',
            display: 'flex',
            justifyContent: 'center',
          })}
        >
          <PixelVersus size={36} />
        </span>
        <div
          className={css({
            animation: '[slot-sweep 2.4s ease-in-out infinite]',
            borderColor: 'gray.5',
            borderRadius: 'l3',
            borderStyle: 'dashed',
            borderWidth: '[1.5px]',
            display: 'flex',
            flexDirection: 'column',
            gap: '2',
            h: '[104px]',
            justifyContent: 'center',
            px: '[22px]',
          })}
        >
          <span
            className={css({
              alignItems: 'center',
              color: 'gray.12',
              display: 'flex',
              fontSize: 'md',
              fontWeight: 'semibold',
              gap: '2.5',
            })}
          >
            相手を待っています
            <WaitingDots />
          </span>
          <span className={css({ color: 'fg.subtle', fontSize: '[12.5px]' })}>
            参加されると melonDS が自動で起動します
          </span>
        </div>
      </div>

      <div
        className={css({
          alignItems: 'center',
          borderTopColor: 'gray.3',
          borderTopWidth: '1px',
          display: 'flex',
          gap: '7',
          mt: '[18px]',
          px: '6',
          py: '4',
        })}
      >
        <HostingFact label="ルール">
          <RuleList
            className={css({ color: 'fg.default', fontSize: '[13.5px]' })}
            parts={rules}
          />
        </HostingFact>
        <span
          aria-hidden="true"
          className={css({ bg: 'gray.3', h: '[34px]', w: '[1px]' })}
        />
        <HostingFact label="部屋コード">
          <span
            className={css({
              alignItems: 'center',
              display: 'flex',
              gap: '2',
              minW: '0',
            })}
          >
            <code
              className={css({
                fontFamily: 'mono',
                fontSize: 'sm',
                fontWeight: 'semibold',
                letterSpacing: '[0.04em]',
                wordBreak: 'break-all',
              })}
            >
              {hostedRoom.roomId}
            </code>
            <Button
              aria-label="部屋コードをコピー"
              className={css({ boxSize: '[26px]', minW: '[26px]', px: '0' })}
              colorPalette="gray"
              disabled={busy}
              onClick={onCopy}
              size="2xs"
              variant="subtle"
            >
              <Copy weight="bold" />
            </Button>
          </span>
        </HostingFact>
        <Button
          className={css({ flexShrink: '0', ml: 'auto' })}
          colorPalette="danger"
          loading={busy}
          onClick={onCancel}
          size="sm"
          variant="outline"
        >
          部屋を閉じる
        </Button>
      </div>
    </section>
  );
}

function HostingFact({
  children,
  label,
}: {
  children: ReactNode;
  label: string;
}) {
  return (
    <div
      className={css({
        display: 'flex',
        flexDirection: 'column',
        gap: '1',
        minW: '0',
      })}
    >
      <span className={css({ color: 'fg.subtle', fontSize: 'xs' })}>
        {label}
      </span>
      {children}
    </div>
  );
}

function WaitingDots() {
  return (
    <span aria-hidden="true" className={css({ display: 'flex', gap: '1' })}>
      {[0, 0.2, 0.4].map((delay) => (
        <span
          key={delay}
          className={css({
            animation: '[waiting-dot 1.4s ease-in-out infinite]',
            bg: 'fg.muted',
            borderRadius: 'full',
            boxSize: '1.5',
          })}
          style={{ animationDelay: `${delay}s` }}
        />
      ))}
    </span>
  );
}

function UpdateRequiredNotice({
  busy,
  onUpdate,
  version,
}: {
  busy: boolean;
  onUpdate: () => void;
  version?: string;
}) {
  return (
    <div
      className={cx(
        cardClass,
        css({
          alignItems: 'center',
          borderColor: 'gray.5',
          display: 'flex',
          gap: '4',
          px: '5',
          py: '[18px]',
        }),
      )}
      role="alert"
    >
      <span
        className={css({
          alignItems: 'center',
          bg: 'amber.3',
          borderRadius: 'l2',
          boxSize: '10',
          color: 'amber.9',
          display: 'flex',
          flexShrink: '0',
          justifyContent: 'center',
        })}
      >
        <DownloadSimple size={18} weight="bold" />
      </span>
      <div
        className={css({
          display: 'flex',
          flexDirection: 'column',
          flexGrow: '1',
          gap: '1',
        })}
      >
        <span className={css({ fontSize: '[15px]', fontWeight: 'bold' })}>
          {version ? `v${version} への更新が必要です` : '更新が必要です'}
        </span>
        <span className={css({ color: 'fg.muted', fontSize: '[13px]' })}>
          更新するまで部屋の作成・参加はできません。更新後は自動で再起動します。
        </span>
      </div>
      <Button
        className={css({ flexShrink: '0' })}
        colorPalette="amber"
        loading={busy}
        onClick={onUpdate}
        size="sm"
      >
        更新して再起動
      </Button>
    </div>
  );
}
