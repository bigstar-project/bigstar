import { Portal } from '@ark-ui/react';
import {
  ArrowsClockwise,
  Broadcast,
  Crown,
  Flag,
  GearSix,
  Heart,
  Play,
  RadioButton,
  Star,
  Stop,
  Trophy,
  Users,
} from '@phosphor-icons/react';
import { useState } from 'react';
import { css, cx } from 'styled-system/css';
import { token } from 'styled-system/tokens';
import playerLBadge from '../assets/player-l.png';
import playerMBadge from '../assets/player-m.png';
import { RoleButton, SelectField, TextField } from '../components/Fields';
import { SummaryItem } from '../components/SummaryItem';
import { Button, CloseButton, Dialog, Tabs } from '../components/ui';
import { WebRtcDiagnosticsPanel } from '../components/WebRtcDiagnosticsPanel';
import type { CourseMode, FormState, Lives } from '../types';
import { InfoPanel, LauncherCard, SmallInfoCard } from './LauncherCards';
import {
  bigStarsOptions,
  courseOptions,
  livesOptions,
  winsOptions,
} from './options';
import type {
  DiagnosticsState,
  LauncherActions,
  LauncherSummary,
  MatchmakingRoomsState,
  UpdateFormField,
} from './types';

export function BattleView({
  actions,
  diagnostics,
  form,
  lastLogDir,
  matchmakingRooms,
  summary,
  updateField,
}: {
  actions: Pick<
    LauncherActions,
    | 'copyRoomCode'
    | 'createRoom'
    | 'joinRoom'
    | 'openLogDir'
    | 'refreshRooms'
    | 'startMatch'
    | 'stopMatch'
  >;
  diagnostics: DiagnosticsState;
  form: FormState;
  lastLogDir: string;
  matchmakingRooms: MatchmakingRoomsState;
  summary: LauncherSummary;
  updateField: UpdateFormField;
}) {
  return (
    <Tabs.Content value="battle">
      <form
        className={css({
          display: 'grid',
          gap: '5',
          gridTemplateColumns: `minmax(0, 1fr) ${token('sizes.diagnostics')}`,
          '@media (max-width: 1380px)': {
            gridTemplateColumns: '1fr',
          },
        })}
        onSubmit={(event) => {
          event.preventDefault();
        }}
      >
        <section
          className={css({
            display: 'grid',
            gap: '4',
          })}
        >
          <LauncherCard
            title="公開ルーム"
            icon={<Users size={24} weight="fill" />}
            badge={matchmakingRooms.loading ? '更新中' : undefined}
          >
            <div className={css({ display: 'grid', gap: '3' })}>
              <div
                className={css({
                  alignItems: 'center',
                  display: 'flex',
                  gap: '3',
                  justifyContent: 'space-between',
                  '@media (max-width: 760px)': {
                    alignItems: 'stretch',
                    flexDirection: 'column',
                  },
                })}
              >
                <div
                  className={css({
                    alignItems: 'center',
                    color: 'fg.muted',
                    display: 'flex',
                    gap: '2',
                    fontWeight: 'bold',
                    textStyle: 'sm',
                  })}
                >
                  <span>{matchmakingRooms.rooms.length} 件</span>
                  <Button
                    variant="outline"
                    size="sm"
                    loading={matchmakingRooms.loading}
                    disabled={matchmakingRooms.refreshDisabled}
                    onClick={() => void actions.refreshRooms()}
                  >
                    <ArrowsClockwise size={16} weight="bold" />
                    更新
                  </Button>
                </div>
                <CreateRoomDialog
                  busy={matchmakingRooms.busy}
                  disabled={summary.connectionActive}
                  form={form}
                  onCreate={actions.createRoom}
                  updateField={updateField}
                />
                {summary.connectionActive ? (
                  <Button
                    variant="outline"
                    onClick={() => void actions.stopMatch()}
                  >
                    <Stop size={18} weight="fill" />
                    停止
                  </Button>
                ) : null}
              </div>
              {matchmakingRooms.error ? (
                <div
                  className={css({
                    color: 'red.subtle.fg',
                    fontWeight: 'bold',
                    textStyle: 'sm',
                  })}
                >
                  {matchmakingRooms.error}
                </div>
              ) : null}
              <RoomList
                busy={matchmakingRooms.busy}
                disabled={summary.connectionActive}
                rooms={matchmakingRooms.rooms}
                onJoin={(roomId) => void actions.joinRoom(roomId)}
              />
            </div>
          </LauncherCard>

          <ManualConnectionPanel
            actions={actions}
            form={form}
            summary={summary}
            updateField={updateField}
          />

          <BattleLogPanel
            lastLogDir={lastLogDir}
            onOpenLogDir={() => void actions.openLogDir()}
          />
        </section>

        <aside
          className={css({
            alignContent: 'start',
            display: 'grid',
            gap: '4',
          })}
        >
          <InfoPanel
            icon={<Broadcast size={22} weight="bold" />}
            title="接続状況"
            badge={summary.connectionActive ? '良好' : '待機'}
            badgeTone={summary.connectionActive ? 'green' : 'slate'}
          >
            <SummaryItem
              label="接続状態"
              value={summary.connectionActive ? '接続中' : '未接続'}
            />
            <WebRtcDiagnosticsPanel
              diagnostics={diagnostics.bridgeDiagnostics}
              compact
            />
          </InfoPanel>

          <div
            className={css({
              display: 'grid',
              gap: '3',
              gridTemplateColumns: 'repeat(2, minmax(0, 1fr))',
              '@media (max-width: 560px)': {
                gridTemplateColumns: '1fr',
              },
            })}
          >
            <SmallInfoCard
              imageSrc={form.role === 'host' ? playerMBadge : playerLBadge}
              label="操作キャラ"
              value={form.role === 'host' ? 'Mario' : 'Luigi'}
            />
            <SmallInfoCard
              icon={<Flag size={30} weight="fill" />}
              label="起動ステージ"
              value={summary.selectedStageLabel}
              caption="0-4 決定"
            />
          </div>
        </aside>
      </form>
    </Tabs.Content>
  );
}

function CreateRoomDialog({
  busy,
  disabled,
  form,
  onCreate,
  updateField,
}: {
  busy: boolean;
  disabled: boolean;
  form: FormState;
  onCreate: () => Promise<void>;
  updateField: UpdateFormField;
}) {
  const [open, setOpen] = useState(false);

  return (
    <Dialog.Root open={open} onOpenChange={(details) => setOpen(details.open)}>
      <Dialog.Trigger asChild>
        <Button
          disabled={disabled}
          loading={busy}
          variant="solid"
          colorPalette="yellow"
          size="xl"
        >
          <Crown size={18} weight="fill" />
          部屋を作る
        </Button>
      </Dialog.Trigger>
      <Portal>
        <Dialog.Backdrop />
        <Dialog.Positioner>
          <Dialog.Content
            className={css({
              maxW: '3xl',
              w: 'full',
            })}
          >
            <Dialog.CloseTrigger>
              <CloseButton />
            </Dialog.CloseTrigger>
            <Dialog.Header>
              <Dialog.Title>部屋を作る</Dialog.Title>
            </Dialog.Header>
            <Dialog.Body>
              <div className={css({ display: 'grid', gap: '4' })}>
                <TextField
                  label="ホスト名"
                  value={form.hostName}
                  maxLength={32}
                  placeholder="Player"
                  onChange={(value) => updateField('hostName', value)}
                />
                <MatchSettingsFields form={form} updateField={updateField} />
              </div>
            </Dialog.Body>
            <Dialog.Footer>
              <Dialog.ActionTrigger asChild>
                <Button variant="outline">キャンセル</Button>
              </Dialog.ActionTrigger>
              <Button
                loading={busy}
                variant="solid"
                colorPalette="yellow"
                onClick={async () => {
                  await onCreate();
                  setOpen(false);
                }}
              >
                <Crown size={18} weight="fill" />
                作成して起動
              </Button>
            </Dialog.Footer>
          </Dialog.Content>
        </Dialog.Positioner>
      </Portal>
    </Dialog.Root>
  );
}

function MatchSettingsFields({
  form,
  updateField,
}: {
  form: FormState;
  updateField: UpdateFormField;
}) {
  return (
    <div className={css({ display: 'grid', gap: '3' })}>
      <div
        className={css({
          display: 'grid',
          gap: '3',
          gridTemplateColumns: 'repeat(4, minmax(0, 1fr))',
          '@media (max-width: 1260px)': {
            gridTemplateColumns: 'repeat(2, minmax(0, 1fr))',
          },
          '@media (max-width: 720px)': {
            gridTemplateColumns: '1fr',
          },
        })}
      >
        <SelectField
          icon={<RadioButton size={18} />}
          label="コース"
          options={courseOptions}
          value={form.courseMode}
          onChange={(value) => updateField('courseMode', value as CourseMode)}
        />
        <SelectField
          icon={<Trophy size={18} weight="fill" />}
          label="勝利数"
          options={winsOptions}
          value={String(form.wins)}
          onChange={(value) => updateField('wins', Number(value))}
        />
        <SelectField
          icon={<Star size={18} weight="fill" />}
          label="ビッグスター"
          options={bigStarsOptions}
          value={String(form.bigStars)}
          onChange={(value) => updateField('bigStars', Number(value))}
        />
        <SelectField
          icon={<Heart size={18} weight="fill" />}
          label="残機"
          options={livesOptions}
          value={form.lives}
          onChange={(value) => updateField('lives', value as Lives)}
        />
      </div>
      <TextField
        label="Match seed"
        value={form.matchSeed}
        placeholder="ランダム時は空でも自動生成"
        onChange={(value) => updateField('matchSeed', value)}
      />
    </div>
  );
}

function ManualConnectionPanel({
  actions,
  form,
  summary,
  updateField,
}: {
  actions: Pick<LauncherActions, 'startMatch' | 'stopMatch'>;
  form: FormState;
  summary: LauncherSummary;
  updateField: UpdateFormField;
}) {
  return (
    <LauncherCard title="手動接続" icon={<GearSix size={24} weight="fill" />}>
      <details
        className={css({
          borderColor: 'gray.surface.border',
          borderRadius: 'l2',
          borderWidth: '1px',
          overflow: 'hidden',
        })}
      >
        <summary
          className={css({
            bg: 'gray.surface.bg',
            color: 'fg.default',
            cursor: 'pointer',
            fontWeight: 'black',
            listStyle: 'none',
            px: '4',
            py: '3',
            textStyle: 'sm',
            focusVisibleRing: 'inside',
          })}
        >
          部屋コードとロールを編集
        </summary>
        <div
          className={css({
            display: 'grid',
            gap: '5',
            p: '4',
          })}
        >
          <div
            className={css({
              display: 'grid',
              gap: '4',
              gridTemplateColumns: 'minmax(220px, 0.85fr) minmax(0, 1.15fr)',
              '@media (max-width: 760px)': {
                gridTemplateColumns: '1fr',
              },
            })}
          >
            <TextField
              label="部屋コード"
              value={form.roomCode}
              maxLength={64}
              placeholder="test-room"
              onChange={(value) => updateField('roomCode', value)}
            />
            <div
              className={css({
                alignContent: 'end',
                display: 'grid',
                gap: '3',
                gridTemplateColumns: 'repeat(2, minmax(0, 1fr))',
                '@media (max-width: 620px)': {
                  gridTemplateColumns: '1fr',
                },
              })}
            >
              <RoleButton
                active={form.role === 'host'}
                icon={<Crown size={26} weight="fill" />}
                title="ホスト"
                subtitle="offer側"
                tone="red"
                onClick={() => updateField('role', 'host')}
              />
              <RoleButton
                active={form.role === 'client'}
                icon={<Users size={26} weight="fill" />}
                title="参加"
                subtitle="answer側"
                tone="green"
                onClick={() => updateField('role', 'client')}
              />
            </div>
          </div>
          <MatchSettingsFields form={form} updateField={updateField} />
          <StartStopButton
            active={summary.connectionActive}
            onStart={() => void actions.startMatch()}
            onStop={() => void actions.stopMatch()}
          />
        </div>
      </details>
    </LauncherCard>
  );
}

function StartStopButton({
  active,
  onStart,
  onStop,
}: {
  active: boolean;
  onStart: () => void;
  onStop: () => void;
}) {
  return (
    <button
      type="button"
      className={cx(
        css({
          borderRadius: 'l2',
          borderWidth: '4px',
          color: 'fg.default',
          cursor: 'pointer',
          focusVisibleRing: 'outside',
          fontWeight: 'black',
          minH: '16',
          overflow: 'hidden',
          px: '6',
          py: '4',
          position: 'relative',
          textStyle: '2xl',
          transition: 'common',
          w: 'full',
        }),
        active
          ? css({
              bg: 'gray.3',
              borderColor: 'gray.3',
              _hover: {
                bg: 'red.subtle.bg',
              },
            })
          : css({
              bg: 'red.700',
              borderColor: 'yellow.500',
              _hover: {
                bg: 'red.600',
              },
            }),
      )}
      onClick={active ? onStop : onStart}
    >
      <span
        className={css({
          alignItems: 'center',
          display: 'flex',
          gap: '4',
          justifyContent: 'center',
        })}
      >
        {active ? '停止' : '対戦を開始'}
        {active ? (
          <Stop size={34} weight="fill" />
        ) : (
          <Play size={34} weight="fill" />
        )}
      </span>
    </button>
  );
}

function RoomList({
  busy,
  disabled,
  onJoin,
  rooms,
}: {
  busy: boolean;
  disabled: boolean;
  rooms: MatchmakingRoomsState['rooms'];
  onJoin: (roomId: string) => void;
}) {
  if (rooms.length === 0) {
    return (
      <div
        className={css({
          bg: 'gray.surface.bg',
          borderColor: 'gray.surface.border',
          borderRadius: 'l2',
          borderWidth: '1px',
          color: 'fg.muted',
          fontWeight: 'semibold',
          p: '4',
          textStyle: 'sm',
        })}
      >
        募集中の部屋はありません
      </div>
    );
  }

  return (
    <div
      className={css({
        borderColor: 'gray.surface.border',
        borderRadius: 'l2',
        borderWidth: '1px',
        display: 'grid',
        overflow: 'hidden',
      })}
    >
      {rooms.map((room) => (
        <div
          key={room.room_id}
          className={css({
            alignItems: 'center',
            bg: 'gray.surface.bg',
            borderBottomColor: 'gray.surface.border',
            borderBottomWidth: '1px',
            display: 'grid',
            gap: '3',
            gridTemplateColumns: 'minmax(0, 1fr) auto',
            p: '3',
            _last: {
              borderBottomWidth: '0',
            },
            '@media (max-width: 760px)': {
              alignItems: 'stretch',
              gridTemplateColumns: '1fr',
            },
          })}
        >
          <div className={css({ display: 'grid', gap: '1', minW: '0' })}>
            <div
              className={css({
                color: 'fg.default',
                fontWeight: 'black',
                overflow: 'hidden',
                textOverflow: 'ellipsis',
                textStyle: 'md',
                whiteSpace: 'nowrap',
              })}
            >
              {room.host_name}
            </div>
            <div
              className={css({
                color: 'fg.muted',
                fontWeight: 'semibold',
                overflowWrap: 'anywhere',
                textStyle: 'sm',
              })}
            >
              {room.room_id} / {formatRoomSettings(room)}
            </div>
          </div>
          <Button
            variant="outline"
            loading={busy}
            disabled={disabled || !room.can_join}
            onClick={() => onJoin(room.room_id)}
          >
            <Users size={18} weight="fill" />
            参加
          </Button>
        </div>
      ))}
    </div>
  );
}

function formatRoomSettings(room: MatchmakingRoomsState['rooms'][number]) {
  return `Course=${room.settings.course_mode} Wins=${room.settings.wins} Star=${room.settings.big_stars} Lives=${room.settings.lives}`;
}

function BattleLogPanel({
  lastLogDir,
  onOpenLogDir,
}: {
  lastLogDir: string;
  onOpenLogDir: () => void;
}) {
  return (
    <LauncherCard title="通信ログ" icon={<Broadcast size={22} />}>
      <div className={css({ display: 'grid', gap: '3' })}>
        <div
          className={css({
            alignItems: 'center',
            display: 'flex',
            gap: '3',
            justifyContent: 'space-between',
          })}
        >
          <span
            className={css({
              color: 'fg.muted',
              fontWeight: 'bold',
              textStyle: 'sm',
            })}
          >
            Log directory
          </span>
          <Button
            variant="outline"
            disabled={!lastLogDir}
            onClick={onOpenLogDir}
          >
            ログを開く
          </Button>
        </div>
        <code
          className={css({
            bg: 'gray.surface.bg',
            borderColor: 'gray.surface.border',
            borderRadius: 'l2',
            borderWidth: '1px',
            color: 'fg.muted',
            fontFamily: 'mono',
            fontWeight: 'semibold',
            overflowWrap: 'anywhere',
            px: '3',
            py: '2',
            textStyle: 'xs',
          })}
        >
          {lastLogDir || 'not started'}
        </code>
      </div>
    </LauncherCard>
  );
}
