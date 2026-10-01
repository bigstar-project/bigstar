import { type ReactNode, useId, useState } from 'react';
import { css, cx } from 'styled-system/css';
import * as AlertDialog from '@/components/ui/alert-dialog';
import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import * as Card from '@/components/ui/card';
import * as Field from '@/components/ui/field';
import { Input } from '@/components/ui/input';
import * as NumberField from '@/components/ui/number-field';
import * as Switch from '@/components/ui/switch';
import * as Tabs from '@/components/ui/tabs';
import { currentEdition, currentRuntimeCapabilities } from '../buildProfile';
import type { FormState } from '../types';
import type { LauncherActions, StartupState, UpdateFormField } from './types';

const playerNameMaxLength = 32;

// 左に説明、右に操作を置く 1 行。行どうしは Card の中で罫線で区切る
const rowClass = css({
  alignItems: 'center',
  columnGap: '6',
  display: 'grid',
  gridTemplateColumns: 'minmax(0, 1fr) auto',
  px: '5',
  py: '3.5',
  w: 'full',
});

const rowTextClass = css({
  display: 'flex',
  flexDirection: 'column',
  gap: '1',
  minW: '0',
});

const rowLabelClass = css({
  color: 'fg.default',
  fontWeight: 'semibold',
  textStyle: 'sm',
});

const rowDescriptionClass = css({
  color: 'fg.muted',
  textStyle: 'sm',
});

export function SettingsView({
  actions,
  form,
  romGenerationBusy = false,
  startup,
  updateField,
}: {
  actions: Pick<
    LauncherActions,
    | 'openMelonds'
    | 'openMelondsInputConfig'
    | 'cleanupDetailedLogs'
    | 'savePlayerName'
    | 'selectRomPath'
    | 'setStartupEnabled'
  >;
  form: FormState;
  romGenerationBusy?: boolean;
  startup: StartupState;
  updateField: UpdateFormField;
}) {
  const { configurableSignalServer } = currentRuntimeCapabilities();
  const insidersEdition = currentEdition() === 'insiders';
  const advancedDiagnostics = insidersEdition || configurableSignalServer;

  return (
    <Tabs.Panel keepMounted value="settings">
      <div
        className={css({ display: 'flex', flexDirection: 'column', gap: '8' })}
      >
        <SettingsSection title="プロフィール">
          <PlayerNameRow
            value={form.hostName}
            onChange={(value) => updateField('hostName', value)}
            onCommit={() => void actions.savePlayerName()}
          />
        </SettingsSection>

        <SettingsSection title="melonDS と ROM">
          <div className={rowClass}>
            <div className={rowTextClass}>
              <span className={rowLabelClass}>ベースROM</span>
              <span
                className={css({
                  color: 'fg.muted',
                  fontFamily: 'mono',
                  overflow: 'hidden',
                  textOverflow: 'ellipsis',
                  textStyle: 'xs',
                  whiteSpace: 'nowrap',
                })}
                title={form.baseRomPath || undefined}
              >
                {form.baseRomPath || '未選択'}
              </span>
              {romGenerationBusy ? (
                <span className={rowDescriptionClass}>
                  オンライン対戦用ROMを生成しています…
                </span>
              ) : null}
            </div>
            <Button
              colorPalette="gray"
              disabled={romGenerationBusy}
              size="sm"
              variant="subtle"
              onClick={() => void actions.selectRomPath('baseRomPath')}
            >
              {form.baseRomPath ? '変更' : '選択'}
            </Button>
          </div>
          <ActionRow
            label="ボタン割り当て"
            description="melonDS の入力設定で変更します"
            action="入力設定を開く"
            onClick={() => void actions.openMelondsInputConfig()}
          />
          <ActionRow
            label="melonDS"
            description="対戦せずに melonDS だけを起動します"
            action="melonDS を開く"
            onClick={() => void actions.openMelonds()}
          />
        </SettingsSection>

        <SettingsSection title="常駐と通知">
          <SwitchRow
            label="Windows ログイン時に起動"
            description="タスクトレイに最小化した状態で起動します"
            checked={startup.enabled}
            disabled={startup.loading}
            onCheckedChange={(checked) =>
              void actions.setStartupEnabled(checked)
            }
          />
          <SwitchRow
            label="新しい部屋の通知"
            description="だれかが部屋を作ると、デスクトップに通知します"
            checked={form.newRoomNotificationsEnabled}
            onCheckedChange={(checked) =>
              updateField('newRoomNotificationsEnabled', checked)
            }
          />
        </SettingsSection>

        <SettingsSection title="接続">
          {configurableSignalServer ? (
            <Field.Root className={rowClass}>
              <div className={rowTextClass}>
                <Field.Label className={rowLabelClass}>
                  シグナリングサーバー
                </Field.Label>
                <Field.Description>
                  ws:// または wss:// で始まるURL
                </Field.Description>
              </div>
              <Input
                className={css({
                  fontFamily: 'mono',
                  textStyle: 'xs',
                  // 最小幅のウィンドウでも左の説明が折り返さない幅にする
                  w: { base: '80', lg: '96' },
                })}
                size="sm"
                spellCheck={false}
                value={form.signalUrl}
                onChange={(event) =>
                  updateField('signalUrl', event.target.value)
                }
              />
            </Field.Root>
          ) : null}
          <Field.Root className={rowClass}>
            <div className={rowTextClass}>
              <Field.Label className={rowLabelClass}>UDPポート</Field.Label>
              <Field.Description>
                通常は変更不要です（1〜65535）
              </Field.Description>
            </div>
            <NumberField.Root
              className={css({ w: '24' })}
              format={{ useGrouping: false }}
              max={65535}
              min={1}
              size="sm"
              value={form.port}
              onValueChange={(value) => {
                if (value !== null) updateField('port', value);
              }}
            >
              <NumberField.Group>
                <NumberField.Input
                  className={css({
                    fontWeight: 'semibold',
                    px: '3',
                    textAlign: 'end',
                  })}
                />
              </NumberField.Group>
            </NumberField.Root>
          </Field.Root>
        </SettingsSection>

        {advancedDiagnostics ? (
          <SettingsSection
            title="診断"
            badge={
              insidersEdition ? (
                <Badge colorPalette="gray" variant="subtle">
                  Insiders
                </Badge>
              ) : null
            }
            description="不具合の調査用です。ふだんはオフのままで問題ありません。"
          >
            <SwitchRow
              label="診断イベントログ"
              description="接続や同期の診断イベントを記録します"
              checked={form.diagnosticEventsEnabled}
              onCheckedChange={(checked) =>
                updateField('diagnosticEventsEnabled', checked)
              }
            />
            <SwitchRow
              label="詳細ログ"
              description="入力・通信・画面状態のログを増やします"
              checked={form.detailedLogsEnabled}
              onCheckedChange={(checked) =>
                updateField('detailedLogsEnabled', checked)
              }
            />
            {insidersEdition ? (
              <SwitchRow
                label="パフォーマンスログ"
                description="FPS低下時の処理時間・音声待ち・CPU時間を記録します"
                checked={form.performanceLogsEnabled}
                onCheckedChange={(checked) =>
                  updateField('performanceLogsEnabled', checked)
                }
              />
            ) : null}
            <SwitchRow
              label="AI用プレイログ"
              description="草原（stage 0）の対戦中だけ AI 学習用の観測ログを保存します"
              checked={form.aiPlayLogEnabled}
              onCheckedChange={(checked) =>
                updateField('aiPlayLogEnabled', checked)
              }
            />
            {insidersEdition ? (
              <CleanupLogsRow onCleanup={actions.cleanupDetailedLogs} />
            ) : null}
          </SettingsSection>
        ) : null}
      </div>
    </Tabs.Panel>
  );
}

function SettingsSection({
  badge,
  children,
  description,
  title,
}: {
  badge?: ReactNode;
  children: ReactNode;
  description?: string;
  title: string;
}) {
  return (
    <section
      className={css({ display: 'flex', flexDirection: 'column', gap: '2.5' })}
    >
      <div
        className={css({ display: 'flex', flexDirection: 'column', gap: '1' })}
      >
        <div
          className={css({ alignItems: 'center', display: 'flex', gap: '2.5' })}
        >
          <h2 className={css({ fontWeight: 'bold', textStyle: 'md' })}>
            {title}
          </h2>
          {badge}
        </div>
        {description ? (
          <p className={css({ color: 'fg.subtle', textStyle: 'sm' })}>
            {description}
          </p>
        ) : null}
      </div>
      <Card.Root
        className={css({ '& > * + *': { borderTopWidth: '1px' } })}
        variant="raised"
      >
        {children}
      </Card.Root>
    </section>
  );
}

function ActionRow({
  action,
  description,
  label,
  onClick,
}: {
  action: string;
  description: string;
  label: string;
  onClick: () => void;
}) {
  return (
    <div className={rowClass}>
      <div className={rowTextClass}>
        <span className={rowLabelClass}>{label}</span>
        <span className={rowDescriptionClass}>{description}</span>
      </div>
      <Button colorPalette="gray" size="sm" variant="subtle" onClick={onClick}>
        {action}
      </Button>
    </div>
  );
}

function SwitchRow({
  checked,
  description,
  disabled,
  label,
  onCheckedChange,
}: {
  checked: boolean;
  description: string;
  disabled?: boolean;
  label: string;
  onCheckedChange: (checked: boolean) => void;
}) {
  const labelId = useId();
  const descriptionId = useId();

  // 行のどこを押しても切り替わるように、行全体を Switch.Label にする
  return (
    <Switch.Label className={cx(rowClass, css({ gap: '0' }))}>
      <span className={rowTextClass}>
        <span id={labelId} className={rowLabelClass}>
          {label}
        </span>
        <span id={descriptionId} className={rowDescriptionClass}>
          {description}
        </span>
      </span>
      <Switch.Root
        aria-describedby={descriptionId}
        aria-labelledby={labelId}
        checked={checked}
        colorPalette="gray"
        disabled={disabled}
        onCheckedChange={(next) => onCheckedChange(next)}
      >
        <Switch.Thumb />
      </Switch.Root>
    </Switch.Label>
  );
}

function PlayerNameRow({
  onChange,
  onCommit,
  value,
}: {
  onChange: (value: string) => void;
  onCommit: () => void;
  value: string;
}) {
  // 保存ボタンは置かず、入力欄を離れたときに変わっていれば保存する
  const [valueOnFocus, setValueOnFocus] = useState(value);

  return (
    <Field.Root className={rowClass}>
      <div className={rowTextClass}>
        <Field.Label className={rowLabelClass}>プレイヤーネーム</Field.Label>
        <Field.Description>
          公開ルームの一覧で相手に表示されます
        </Field.Description>
      </div>
      <div className={css({ alignItems: 'center', display: 'flex', gap: '3' })}>
        <span
          aria-hidden="true"
          className={css({
            color: 'fg.subtle',
            fontVariantNumeric: 'tabular-nums',
            textStyle: 'xs',
          })}
        >
          {value.length} / {playerNameMaxLength}
        </span>
        <Input
          className={css({ w: '64' })}
          maxLength={playerNameMaxLength}
          placeholder="Player"
          size="sm"
          value={value}
          onBlur={() => {
            if (value !== valueOnFocus) onCommit();
          }}
          onChange={(event) => onChange(event.target.value)}
          onFocus={() => setValueOnFocus(value)}
          onKeyDown={(event) => {
            if (event.key === 'Enter') event.currentTarget.blur();
          }}
        />
      </div>
    </Field.Root>
  );
}

function CleanupLogsRow({ onCleanup }: { onCleanup: () => Promise<void> }) {
  const [open, setOpen] = useState(false);
  const [busy, setBusy] = useState(false);

  return (
    <div className={rowClass}>
      <div className={rowTextClass}>
        <span className={rowLabelClass}>古い詳細ログ</span>
        <span className={rowDescriptionClass}>
          対戦履歴・作成済みzip・実行中の対戦ログは残ります
        </span>
      </div>
      <AlertDialog.Root open={open} onOpenChange={(next) => setOpen(next)}>
        <AlertDialog.Trigger
          render={<Button colorPalette="danger" size="sm" variant="outline" />}
        >
          削除
        </AlertDialog.Trigger>
        <AlertDialog.Portal>
          <AlertDialog.Backdrop />
          <AlertDialog.Popup>
            <AlertDialog.Header>
              <AlertDialog.Title>
                古い詳細ログを削除しますか？
              </AlertDialog.Title>
              <AlertDialog.Description>
                対戦履歴、作成済みzip、実行中の対戦ログは残ります。
              </AlertDialog.Description>
            </AlertDialog.Header>
            <AlertDialog.Footer>
              <AlertDialog.Close
                disabled={busy}
                render={<Button colorPalette="gray" variant="outline" />}
              >
                キャンセル
              </AlertDialog.Close>
              <Button
                colorPalette="danger"
                loading={busy}
                onClick={async () => {
                  setBusy(true);
                  try {
                    await onCleanup();
                    setOpen(false);
                  } finally {
                    setBusy(false);
                  }
                }}
              >
                削除する
              </Button>
            </AlertDialog.Footer>
          </AlertDialog.Popup>
        </AlertDialog.Portal>
      </AlertDialog.Root>
    </div>
  );
}
