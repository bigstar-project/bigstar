import { Portal } from '@ark-ui/react';
import {
  ArrowsClockwise,
  Crown,
  Flag,
  Heart,
  RadioButton,
  Rewind,
  Star,
  Stop,
  Trophy,
  Users,
  WarningCircle,
} from '@phosphor-icons/react';
import { useState } from 'react';
import { css, cx } from 'styled-system/css';
import { surface } from 'styled-system/recipes';
import { NumberField, SelectField } from '../components/Fields';
import { Button, CloseButton, Dialog, Tabs } from '../components/ui';
import {
  clampStage,
  defaultInputDelayFrames,
  defaultInputMaxFrameLead,
  maxGamesForWins,
  rollbackInputDelayFrames,
  rollbackInputMaxFrameLead,
} from '../form';
import type { CourseMode, FormState, Lives } from '../types';
import { LauncherCard } from './LauncherCards';
import { MatchResultCard } from './MatchResultCard';
import {
  bigStarsOptions,
  courseOptions,
  livesOptions,
  rollbackOptions,
  stageOptions,
  winsOptions,
} from './options';
import type {
  BattleMatchRecord,
  LauncherActions,
  LauncherSummary,
  MatchmakingRoomsState,
  UpdateFormField,
} from './types';

export function BattleView({
  actions,
  form,
  matchmakingRooms,
  currentMatch,
  summary,
  updateField,
}: {
  actions: Pick<
    LauncherActions,
    | 'copyRoomCode'
    | 'cancelHostedRoom'
    | 'createRoom'
    | 'joinRoom'
    | 'refreshRooms'
    | 'stopMatch'
  >;
  form: FormState;
  matchmakingRooms: MatchmakingRoomsState;
  currentMatch: BattleMatchRecord | null;
  summary: LauncherSummary;
  updateField: UpdateFormField;
}) {
  const matchmakingDisabled =
    summary.connectionActive ||
    summary.updateRequired ||
    Boolean(matchmakingRooms.hostedRoomId);

  return (
    <Tabs.Content value="battle">
      <form
        className={css({
          maxW: {
            base: 'xl',
            xl: 'mainPanel',
          },
          mx: 'auto',
          w: 'full',
        })}
        onSubmit={(event) => {
          event.preventDefault();
        }}
      >
        <section className={css({ display: 'grid', gap: '3' })}>
          {currentMatch ? (
            <MatchResultCard match={currentMatch} title="現在の対戦状況" />
          ) : null}

          <LauncherCard
            title="公開ルーム"
            icon={<Users size={24} weight="fill" />}
            badge={matchmakingRooms.loading ? '更新中' : undefined}
          >
            <div className={css({ display: 'grid', gap: '2.5' })}>
              <div
                className={css({
                  alignItems: { base: 'stretch', md: 'center' },
                  display: 'flex',
                  flexDirection: { base: 'column', md: 'row' },
                  gap: '2',
                  justifyContent: 'space-between',
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
                    loading={matchmakingRooms.loading}
                    disabled={matchmakingRooms.refreshDisabled}
                    onClick={() => void actions.refreshRooms()}
                  >
                    <ArrowsClockwise size={16} weight="bold" />
                    更新
                  </Button>
                </div>
                <div
                  className={css({
                    display: 'flex',
                    flexDirection: { base: 'column', md: 'row' },
                    gap: '2',
                  })}
                >
                  {summary.connectionActive ? (
                    <Button
                      variant="outline"
                      onClick={() => void actions.stopMatch()}
                    >
                      <Stop size={18} weight="fill" />
                      停止
                    </Button>
                  ) : null}
                  <CreateRoomDialog
                    busy={matchmakingRooms.busy}
                    disabled={matchmakingDisabled}
                    form={form}
                    onCreate={actions.createRoom}
                    updateField={updateField}
                  />
                </div>
              </div>
              {matchmakingRooms.error ? (
                <div
                  className={css({
                    color: 'red.subtle.fg',
                    fontWeight: 'bold',
                    textStyle: 'sm',
                  })}
                >
                  公開ルームを取得できませんでした。更新をお試しください。
                </div>
              ) : null}
              {summary.updateRequired ? (
                <UpdateRequiredNotice version={summary.updateVersion} />
              ) : null}
              {matchmakingRooms.hostedRoomId ? (
                <HostedRoomNotice
                  busy={matchmakingRooms.busy}
                  roomId={matchmakingRooms.hostedRoomId}
                  onCancel={() => void actions.cancelHostedRoom()}
                  onCopy={() => void actions.copyRoomCode()}
                />
              ) : null}
              {!matchmakingRooms.error ? (
                <RoomList
                  busy={matchmakingRooms.busy}
                  disabled={matchmakingDisabled}
                  rooms={matchmakingRooms.rooms}
                  onJoin={(roomId) => void actions.joinRoom(roomId)}
                />
              ) : null}
            </div>
          </LauncherCard>
        </section>
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
              maxW: 'xl',
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
              <div className={css({ display: 'grid', gap: '3' })}>
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
                disabled={disabled}
                onClick={async () => {
                  await onCreate();
                  setOpen(false);
                }}
              >
                <Crown size={18} weight="fill" />
                作成して待機
              </Button>
            </Dialog.Footer>
          </Dialog.Content>
        </Dialog.Positioner>
      </Portal>
    </Dialog.Root>
  );
}

function HostedRoomNotice({
  busy,
  onCancel,
  onCopy,
  roomId,
}: {
  busy: boolean;
  onCancel: () => void;
  onCopy: () => void;
  roomId: string;
}) {
  return (
    <div
      className={css({
        bg: 'yellow.subtle.bg',
        borderColor: 'yellow.outline.border',
        borderRadius: 'l2',
        borderWidth: '1px',
        display: 'grid',
        gap: '2',
        gridTemplateColumns: {
          base: '1fr',
          md: 'minmax(0, 1fr) auto auto',
        },
        p: '2.5',
        alignItems: { base: 'stretch', md: 'center' },
      })}
    >
      <div className={css({ display: 'grid', gap: '1', minW: '0' })}>
        <div
          className={css({
            color: 'yellow.subtle.fg',
            fontWeight: 'black',
            textStyle: 'sm',
          })}
        >
          参加者を待っています
        </div>
        <code
          className={css({
            color: 'yellow.subtle.fg',
            fontFamily: 'mono',
            fontWeight: 'bold',
            overflowWrap: 'anywhere',
            textStyle: 'sm',
          })}
        >
          {roomId}
        </code>
      </div>
      <Button variant="outline" disabled={busy} onClick={onCopy}>
        部屋コードをコピー
      </Button>
      <Button variant="outline" loading={busy} onClick={onCancel}>
        部屋を閉じる
      </Button>
    </div>
  );
}

function MatchSettingsFields({
  form,
  updateField,
}: {
  form: FormState;
  updateField: UpdateFormField;
}) {
  const updateRollback = (value: string) => {
    const enabled = value === 'on';
    updateField('rollbackEnabled', enabled);
    updateField(
      'inputDelayFrames',
      enabled ? rollbackInputDelayFrames : defaultInputDelayFrames,
    );
    updateField(
      'inputMaxFrameLead',
      enabled ? rollbackInputMaxFrameLead : defaultInputMaxFrameLead,
    );
  };

  return (
    <div className={css({ display: 'grid', gap: '2.5' })}>
      <div
        className={css({
          display: 'grid',
          gap: '2',
          gridTemplateColumns: {
            base: '1fr',
            md: 'repeat(2, minmax(0, 1fr))',
            xl: 'repeat(4, minmax(0, 1fr))',
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
      {form.courseMode === 'select' ? (
        <CourseSequenceFields form={form} updateField={updateField} />
      ) : null}
      <div
        className={css({
          display: 'grid',
          gap: '2',
          gridTemplateColumns: {
            base: '1fr',
            sm: 'repeat(2, minmax(0, 1fr))',
            lg: 'repeat(3, minmax(0, 1fr))',
          },
        })}
      >
        <SelectField
          icon={<Rewind size={18} weight="fill" />}
          label="ロールバック"
          options={rollbackOptions}
          value={form.rollbackEnabled ? 'on' : 'off'}
          onChange={updateRollback}
        />
        <NumberField
          label="InputDelayFrames"
          min={0}
          max={16}
          value={form.inputDelayFrames}
          onChange={(value) =>
            updateField('inputDelayFrames', clampNetplaySetting(value))
          }
        />
        <NumberField
          label="InputMaxFrameLead"
          min={0}
          max={16}
          value={form.inputMaxFrameLead}
          onChange={(value) =>
            updateField('inputMaxFrameLead', clampNetplaySetting(value))
          }
        />
      </div>
    </div>
  );
}

function CourseSequenceFields({
  form,
  updateField,
}: {
  form: FormState;
  updateField: UpdateFormField;
}) {
  const games = maxGamesForWins(form.wins);
  const stages = Array.from({ length: games }, (_, index) =>
    clampStage(form.courseStages[index] ?? 0),
  );
  return (
    <div
      className={css({
        display: 'grid',
        gap: '2',
        gridTemplateColumns: {
          base: '1fr',
          lg: 'repeat(3, minmax(0, 1fr))',
        },
      })}
    >
      {stages.map((stage, index) => (
        <SelectField
          key={`game-${index + 1}`}
          icon={<Flag size={18} weight="fill" />}
          label={`ゲーム ${index + 1}`}
          options={stageOptions}
          value={String(stage)}
          onChange={(value) => {
            const next = [...stages];
            next[index] = clampStage(Number(value));
            updateField('courseStages', next);
          }}
        />
      ))}
    </div>
  );
}

function clampNetplaySetting(value: number) {
  if (!Number.isFinite(value)) {
    return 0;
  }
  return Math.min(16, Math.max(0, Math.trunc(value)));
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
        className={cx(
          surface({ variant: 'inset' }),
          css({
            color: 'fg.muted',
            fontWeight: 'semibold',
            p: '3',
            textStyle: 'sm',
          }),
        )}
      >
        募集中の部屋はありません
      </div>
    );
  }

  return (
    <div
      className={cx(
        surface({ variant: 'inset' }),
        css({ display: 'grid', overflow: 'hidden' }),
      )}
    >
      {rooms.map((room) => (
        <div
          key={room.room_id}
          className={css({
            borderBottomColor: 'gray.surface.border',
            borderBottomWidth: '1px',
            display: 'grid',
            gap: '2',
            gridTemplateColumns: {
              base: '1fr',
              md: 'minmax(0, 1fr) auto',
            },
            p: '2.5',
            _last: {
              borderBottomWidth: '0',
            },
            alignItems: { base: 'stretch', md: 'center' },
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

function UpdateRequiredNotice({ version }: { version?: string }) {
  return (
    <div
      className={css({
        alignItems: 'flex-start',
        bg: 'yellow.subtle.bg',
        borderColor: 'yellow.outline.border',
        borderRadius: 'l2',
        borderWidth: '1px',
        color: 'yellow.subtle.fg',
        display: 'flex',
        gap: '2',
        p: '2.5',
      })}
    >
      <WarningCircle
        className={css({ flexShrink: '0', mt: '0.5' })}
        size={20}
        weight="fill"
      />
      <div className={css({ display: 'grid', gap: '1' })}>
        <div className={css({ fontWeight: 'black', textStyle: 'sm' })}>
          GUI の更新が必要です
        </div>
        <div
          className={css({
            fontWeight: 'bold',
            overflowWrap: 'anywhere',
            textStyle: 'sm',
          })}
        >
          {version
            ? `v${version} に更新するまで、部屋の作成・参加はできません。画面左下の更新ボタンから更新してください。`
            : '更新を適用するまで、部屋の作成・参加はできません。画面左下の更新ボタンから更新してください。'}
        </div>
      </div>
    </div>
  );
}

function formatRoomSettings(room: MatchmakingRoomsState['rooms'][number]) {
  const rollback = room.settings.rollback_enabled ? 'RB=on' : 'RB=off';
  const stages = room.settings.course_stages.join('/');
  return `Course=${room.settings.course_mode}[${stages}] Wins=${room.settings.wins} Star=${room.settings.big_stars} Lives=${room.settings.lives} Delay=${room.settings.input_delay_frames} Lead=${room.settings.input_max_frame_lead} ${rollback}`;
}
