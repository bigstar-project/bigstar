import { CaretRight } from '@phosphor-icons/react';
import { type ReactNode, useId } from 'react';
import { css, cx } from 'styled-system/css';
import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import * as Collapsible from '@/components/ui/collapsible';
import * as Dialog from '@/components/ui/dialog';
import * as NumberField from '@/components/ui/number-field';
import * as Select from '@/components/ui/select';
import * as Switch from '@/components/ui/switch';
import { Toggle, ToggleGroup } from '@/components/ui/toggle';
import {
  clampStage,
  defaultInputDelayFrames,
  defaultInputMaxFrameLead,
  maxGamesForWins,
  rollbackInputDelayFrames,
  rollbackInputMaxFrameLead,
  rollbackPredictionHorizonFrames,
} from '../form';
import type { CourseMode, FormState, Lives } from '../types';
import {
  bigStarsOptions,
  courseOptions,
  livesOptions,
  stageOptions,
  winsOptions,
} from './options';
import type { UpdateFormField } from './types';

/** 部屋を作るダイアログの中身。Dialog.Root と開くボタンは呼び出し側に置く */
export function CreateRoomDialog({
  busy,
  disabled,
  form,
  onCreate,
  onClose,
  updateField,
}: {
  busy: boolean;
  disabled: boolean;
  form: FormState;
  onCreate: () => Promise<void>;
  onClose: () => void;
  updateField: UpdateFormField;
}) {
  return (
    <Dialog.Portal>
      <Dialog.Backdrop className={css({ bg: '[rgba(6, 7, 9, 0.74)]' })} />
      <Dialog.Popup
        className={css({
          bg: 'gray.2',
          borderColor: 'gray.4',
          borderRadius: '[14px]',
          borderWidth: '1px',
          boxShadow: '[0 28px 72px rgba(0, 0, 0, 0.55)]',
          gap: '[22px]',
          maxW: '[540px]',
          pb: '[22px]',
          pt: '[26px]',
          px: '7',
        })}
      >
        <Dialog.Header className={css({ gap: '1.5', pr: '10' })}>
          <Dialog.Title className={css({ fontSize: 'xl', fontWeight: 'bold' })}>
            部屋を作る
          </Dialog.Title>
          <Dialog.Description className={css({ fontSize: '[13px]' })}>
            公開ルームに表示され、最初に参加した人と対戦が始まります。
          </Dialog.Description>
        </Dialog.Header>
        <Dialog.CloseTrigger aria-label="閉じる" />

        <MatchSettingsFields form={form} updateField={updateField} />

        <Dialog.Footer className={css({ gap: '2.5', mt: '-1' })}>
          <Dialog.Close
            render={
              <Button colorPalette="gray" variant="subtle">
                キャンセル
              </Button>
            }
          />
          <Button
            colorPalette="amber"
            disabled={disabled}
            loading={busy}
            onClick={async () => {
              await onCreate();
              onClose();
            }}
          >
            作成して待機
          </Button>
        </Dialog.Footer>
      </Dialog.Popup>
    </Dialog.Portal>
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
    <div
      className={css({
        display: 'flex',
        flexDirection: 'column',
        gap: '[22px]',
      })}
    >
      <div
        className={css({ display: 'flex', flexDirection: 'column', gap: '2' })}
      >
        <SegmentedField
          label="コース"
          options={courseOptions}
          value={form.courseMode}
          onChange={(value) => updateField('courseMode', value as CourseMode)}
        />
        {form.courseMode === 'select' ? (
          <CourseSequenceFields form={form} updateField={updateField} />
        ) : null}
      </div>

      <div
        className={css({
          display: 'flex',
          flexDirection: 'column',
          gap: '2.5',
        })}
      >
        <div
          className={css({
            display: 'grid',
            gap: '3.5',
            gridTemplateColumns: 'repeat(3, minmax(0, 1fr))',
          })}
        >
          <SegmentedField
            label="先取数"
            options={winsOptions}
            value={String(form.wins)}
            onChange={(value) => updateField('wins', Number(value))}
          />
          <SegmentedField
            label="ビッグスター"
            options={bigStarsOptions}
            value={String(form.bigStars)}
            onChange={(value) => updateField('bigStars', Number(value))}
          />
          <SegmentedField
            label="残機"
            options={livesOptions}
            value={form.lives}
            onChange={(value) => updateField('lives', value as Lives)}
          />
        </div>
        <p
          className={css({
            color: 'fg.subtle',
            fontSize: '[12.5px]',
            lineHeight: '[1.6]',
          })}
        >
          1ゲームは、ビッグスターを先に集めるか相手の残機を0にすると勝ちです。
        </p>
      </div>

      <NetplaySettings form={form} updateField={updateField} />
    </div>
  );
}

const fieldLabelClass = css({ fontSize: '[13px]', fontWeight: 'semibold' });

/** 選択肢が少ない設定は、押したものが反転するボタンの並びで選ぶ */
function SegmentedField({
  label,
  onChange,
  options,
  value,
}: {
  label: string;
  onChange: (value: string) => void;
  options: { value: string; label: string }[];
  value: string;
}) {
  const labelId = useId();
  return (
    <div
      className={css({ display: 'flex', flexDirection: 'column', gap: '2' })}
    >
      <span className={fieldLabelClass} id={labelId}>
        {label}
      </span>
      <ToggleGroup
        aria-labelledby={labelId}
        className={css({
          bg: 'app.sidebar',
          borderColor: 'gray.4',
          borderRadius: '[9px]',
          borderWidth: '1px',
          display: 'grid',
          gap: '0.5',
          p: '[3px]',
        })}
        style={{
          gridTemplateColumns: `repeat(${options.length}, minmax(0, 1fr))`,
        }}
        value={[value]}
        onValueChange={(next) => {
          // 押し直しで選択が外れないように、空の選択は無視する
          const [picked] = next;
          if (picked !== undefined) onChange(picked);
        }}
      >
        {options.map((option) => (
          <Toggle
            key={option.value}
            className={css({
              borderRadius: 'l1',
              fontSize: '[13px]',
              fontWeight: 'medium',
              h: '[34px]',
              minW: '0',
              px: '2',
              _pressed: {
                bg: 'fg.default',
                color: 'gray.1',
                fontWeight: 'semibold',
                _hover: { bg: 'fg.default', color: 'gray.1' },
              },
            })}
            value={option.value}
          >
            {option.label}
          </Toggle>
        ))}
      </ToggleGroup>
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
        bg: 'app.sidebar',
        borderColor: 'gray.4',
        borderRadius: '[9px]',
        borderWidth: '1px',
        display: 'grid',
        gap: '2',
        gridTemplateColumns: 'repeat(5, minmax(0, 1fr))',
        p: '2',
      })}
    >
      {stages.map((stage, index) => (
        // Select.Root は要素を出さないので、ラベルと選択欄を縦に並べる箱を置く
        <div
          key={`game-${index + 1}`}
          className={css({
            display: 'flex',
            flexDirection: 'column',
            gap: '1.5',
            minW: '0',
          })}
        >
          <Select.Root
            items={stageOptions}
            size="sm"
            value={String(stage)}
            onValueChange={(value) => {
              if (value === null) return;
              const next = [...stages];
              next[index] = clampStage(Number(value));
              updateField('courseStages', next);
            }}
          >
            <Select.Label
              className={css({ color: 'fg.subtle', fontSize: 'xs' })}
            >
              ゲーム {index + 1}
            </Select.Label>
            <Select.Trigger className={css({ minW: '0', w: 'full' })}>
              <Select.Value />
              <Select.Icon />
            </Select.Trigger>
            <Select.Portal>
              <Select.Positioner alignItemWithTrigger={false} sideOffset={4}>
                <Select.Popup>
                  <Select.List>
                    {stageOptions.map((option) => (
                      <Select.Item key={option.value} value={option.value}>
                        <Select.ItemText>{option.label}</Select.ItemText>
                        <Select.ItemIndicator />
                      </Select.Item>
                    ))}
                  </Select.List>
                </Select.Popup>
              </Select.Positioner>
            </Select.Portal>
          </Select.Root>
        </div>
      ))}
    </div>
  );
}

function NetplaySettings({
  form,
  updateField,
}: {
  form: FormState;
  updateField: UpdateFormField;
}) {
  const rollback = form.rollbackEnabled;
  const summary = rollback
    ? `遅延 ${form.inputDelayFrames}F · 予測 ${rollbackPredictionHorizonFrames}F · RB 有効`
    : `遅延 ${form.inputDelayFrames}F · 先行 ${form.inputMaxFrameLead}F · RB 無効`;

  const updateRollback = (enabled: boolean) => {
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
    <Collapsible.Root
      className={css({ borderTopColor: 'gray.4', borderTopWidth: '1px' })}
    >
      <Collapsible.Trigger
        className={css({
          alignItems: 'center',
          display: 'flex',
          gap: '2.5',
          h: '12',
          w: 'full',
          '&[data-panel-open] [data-chevron]': { transform: 'rotate(90deg)' },
        })}
      >
        <CaretRight
          className={css({
            color: 'fg.muted',
            transition: 'transform',
            transitionDuration: 'fast',
          })}
          data-chevron
          size={14}
          weight="bold"
        />
        <span className={fieldLabelClass}>通信の詳細設定</span>
        <span
          className={css({
            color: 'fg.subtle',
            fontSize: '[12.5px]',
            fontVariantNumeric: 'tabular-nums',
            ml: 'auto',
          })}
        >
          {summary}
        </span>
      </Collapsible.Trigger>
      <Collapsible.Panel>
        <div
          className={css({
            display: 'flex',
            flexDirection: 'column',
            gap: '0.5',
            pb: '1.5',
            pl: '6',
            pt: '0.5',
          })}
        >
          <FrameRow
            description="大きいほど安定、小さいほど操作が機敏"
            label="入力遅延"
            value={form.inputDelayFrames}
            onChange={(value) => updateField('inputDelayFrames', value)}
          />
          {rollback ? (
            <SettingRow
              description="ロールバック有効時は固定です"
              label="予測フレーム"
              muted
            >
              <span
                className={css({
                  color: 'fg.muted',
                  fontSize: '[13px]',
                  fontVariantNumeric: 'tabular-nums',
                  fontWeight: 'semibold',
                  textAlign: 'center',
                  w: '[104px]',
                })}
              >
                {rollbackPredictionHorizonFrames} F
              </span>
            </SettingRow>
          ) : (
            <FrameRow
              description="相手の入力を待たずに進めるフレーム数"
              label="先行フレーム上限"
              value={form.inputMaxFrameLead}
              onChange={(value) => updateField('inputMaxFrameLead', value)}
            />
          )}
          <RollbackRow checked={rollback} onCheckedChange={updateRollback} />
        </div>
      </Collapsible.Panel>
    </Collapsible.Root>
  );
}

const rowClass = css({
  alignItems: 'center',
  display: 'flex',
  gap: '4',
  justifyContent: 'space-between',
  minH: '[52px]',
});

const rowTextClass = css({
  display: 'flex',
  flexDirection: 'column',
  gap: '0.5',
  minW: '0',
});

const rowLabelClass = css({ fontSize: '[13.5px]', fontWeight: 'semibold' });

const rowDescriptionClass = css({ color: 'fg.subtle', fontSize: '[12.5px]' });

function SettingRow({
  children,
  description,
  label,
  labelId,
  muted,
}: {
  children: ReactNode;
  description: string;
  label: ReactNode;
  labelId?: string;
  muted?: boolean;
}) {
  return (
    <div className={rowClass}>
      <div className={rowTextClass}>
        <span
          className={cx(
            rowLabelClass,
            muted ? css({ color: 'fg.muted' }) : undefined,
          )}
          id={labelId}
        >
          {label}
        </span>
        <span className={rowDescriptionClass}>{description}</span>
      </div>
      {children}
    </div>
  );
}

function FrameRow({
  description,
  label,
  onChange,
  value,
}: {
  description: string;
  label: string;
  onChange: (value: number) => void;
  value: number;
}) {
  const labelId = useId();
  return (
    <SettingRow description={description} label={label} labelId={labelId}>
      <NumberField.Root
        className={css({ flexShrink: '0', w: '[104px]' })}
        max={16}
        min={0}
        size="sm"
        value={value}
        onValueChange={(next) => {
          if (next !== null) onChange(clampNetplaySetting(next));
        }}
      >
        <NumberField.Group>
          <NumberField.Decrement aria-label={`${label}を減らす`} />
          <NumberField.Input
            aria-labelledby={labelId}
            className={css({
              fontVariantNumeric: 'tabular-nums',
              fontWeight: 'semibold',
              textAlign: 'center',
            })}
          />
          <NumberField.Increment aria-label={`${label}を増やす`} />
        </NumberField.Group>
      </NumberField.Root>
    </SettingRow>
  );
}

function RollbackRow({
  checked,
  onCheckedChange,
}: {
  checked: boolean;
  onCheckedChange: (checked: boolean) => void;
}) {
  const labelId = useId();
  const descriptionId = useId();
  // 行のどこを押しても切り替わるように、行全体を Switch.Label にする
  return (
    <Switch.Label className={rowClass}>
      <span className={rowTextClass}>
        <span
          className={cx(
            rowLabelClass,
            css({ alignItems: 'center', display: 'flex', gap: '2' }),
          )}
        >
          <span id={labelId}>ロールバック</span>
          <Badge colorPalette="gray" size="sm" variant="subtle">
            試験機能
          </Badge>
        </span>
        <span className={rowDescriptionClass} id={descriptionId}>
          遅延を隠す代わりに画面が巻き戻ることがあります
        </span>
      </span>
      <Switch.Root
        aria-describedby={descriptionId}
        aria-labelledby={labelId}
        checked={checked}
        colorPalette="gray"
        onCheckedChange={(next) => onCheckedChange(next)}
      >
        <Switch.Thumb />
      </Switch.Root>
    </Switch.Label>
  );
}

function clampNetplaySetting(value: number) {
  if (!Number.isFinite(value)) {
    return 0;
  }
  return Math.min(16, Math.max(0, Math.trunc(value)));
}
