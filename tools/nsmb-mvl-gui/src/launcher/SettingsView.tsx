import { Portal } from '@ark-ui/react';
import {
  BellRinging,
  Broadcast,
  CheckCircle,
  FlagCheckered,
  GameController,
  HardDrives,
  Play,
  ShieldCheck,
  Trash,
  UserCircle,
  WarningCircle,
  WifiHigh,
} from '@phosphor-icons/react';
import { useState } from 'react';
import { css } from 'styled-system/css';
import { token } from 'styled-system/tokens';
import {
  FilePathField,
  NumberField,
  SelectField,
  TextField,
} from '../components/Fields';
import { SummaryItem } from '../components/SummaryItem';
import { Button, CloseButton, Dialog, Switch, Tabs } from '../components/ui';
import type { FormState } from '../types';
import { InfoPanel, SettingsPanel } from './LauncherCards';
import { shortPath } from './path';
import type {
  LauncherActions,
  LauncherSummary,
  StartupState,
  UpdateFormField,
} from './types';

const diagnosticEventOptions = [
  { value: 'off', label: 'Off' },
  { value: 'on', label: 'On' },
];

export function SettingsView({
  actions,
  form,
  startup,
  summary,
  updateField,
}: {
  actions: Pick<
    LauncherActions,
    | 'openMelonds'
    | 'openMelondsInputConfig'
    | 'cleanupDetailedLogs'
    | 'pollStatus'
    | 'preflightCheck'
    | 'prepareRoms'
    | 'savePlayerName'
    | 'selectRomPath'
    | 'setStartupEnabled'
  >;
  form: FormState;
  startup: StartupState;
  summary: LauncherSummary;
  updateField: UpdateFormField;
}) {
  const [cleanupConfirmOpen, setCleanupConfirmOpen] = useState(false);
  const [cleanupBusy, setCleanupBusy] = useState(false);

  return (
    <Tabs.Content value="settings">
      <div
        className={css({
          display: 'grid',
          gap: '4',
          gridTemplateColumns: {
            base: '1fr',
            xl: `minmax(0, 1fr) ${token('sizes.settingsAside')}`,
          },
          maxW: { base: 'xl', xl: 'contentMax' },
          mx: { base: 'auto', xl: '0' },
          w: 'full',
        })}
      >
        <section
          className={css({
            alignContent: 'start',
            display: 'grid',
            gap: '3',
          })}
        >
          <SettingsPanel
            icon={<UserCircle size={24} weight="fill" />}
            title="プロフィール"
          >
            <div
              className={css({
                display: 'grid',
                gap: '2',
                gridTemplateColumns: {
                  base: '1fr',
                  md: 'minmax(0, 1fr) auto',
                },
              })}
            >
              <TextField
                label="プレイヤーネーム"
                value={form.hostName}
                maxLength={32}
                placeholder="Player"
                onChange={(value) => updateField('hostName', value)}
              />
              <Button
                className={css({ alignSelf: 'end' })}
                variant="outline"
                onClick={() => void actions.savePlayerName()}
              >
                保存
              </Button>
            </div>
          </SettingsPanel>

          <SettingsPanel
            icon={<BellRinging size={24} weight="fill" />}
            title="常駐・通知"
          >
            <Switch.Root
              checked={startup.enabled}
              disabled={startup.loading}
              onCheckedChange={(details) =>
                void actions.setStartupEnabled(details.checked)
              }
              className={css({
                alignItems: 'center',
                bg: 'app.panel',
                borderColor: 'border',
                borderRadius: 'l2',
                borderWidth: '1px',
                colorPalette: 'blue',
                display: 'flex',
                gap: '3',
                justifyContent: 'space-between',
                minH: '16',
                p: '3',
                w: 'full',
              })}
            >
              <Switch.HiddenInput />
              <div
                className={css({
                  display: 'grid',
                  gap: '1',
                  minW: '0',
                })}
              >
                <Switch.Label className={css({ fontSize: 'sm' })}>
                  Windowsログイン時に起動
                </Switch.Label>
                <div
                  className={css({
                    color: 'fg.muted',
                    textStyle: 'xs',
                  })}
                >
                  タスクトレイに最小化された状態で起動します
                </div>
              </div>
              <Switch.Control />
            </Switch.Root>
            <Switch.Root
              checked={form.newRoomNotificationsEnabled}
              onCheckedChange={(details) =>
                updateField('newRoomNotificationsEnabled', details.checked)
              }
              className={css({
                alignItems: 'center',
                bg: 'app.panel',
                borderColor: 'border',
                borderRadius: 'l2',
                borderWidth: '1px',
                colorPalette: 'blue',
                display: 'flex',
                gap: '3',
                justifyContent: 'space-between',
                minH: '16',
                p: '3',
                w: 'full',
              })}
            >
              <Switch.HiddenInput />
              <div
                className={css({
                  display: 'grid',
                  gap: '1',
                  minW: '0',
                })}
              >
                <Switch.Label className={css({ fontSize: 'sm' })}>
                  新規部屋通知
                </Switch.Label>
                <div
                  className={css({
                    color: 'fg.muted',
                    textStyle: 'xs',
                  })}
                >
                  新しい部屋が作られたときに通知を受け取ることができます
                </div>
              </div>
              <Switch.Control />
            </Switch.Root>
          </SettingsPanel>

          <SettingsPanel
            icon={<GameController size={24} weight="fill" />}
            title="melonDS設定"
          >
            <div
              className={css({
                display: 'grid',
                gap: '2',
                gridTemplateColumns: {
                  base: '1fr',
                  md: 'repeat(2, minmax(0, 1fr))',
                },
              })}
            >
              <Button
                variant="outline"
                onClick={() => void actions.openMelonds()}
              >
                <Play size={20} weight="fill" />
                melonDS を開く
              </Button>
              <Button
                variant="outline"
                onClick={() => void actions.openMelondsInputConfig()}
              >
                <GameController size={20} weight="fill" />
                入力設定を開く
              </Button>
            </div>
            <SelectField
              icon={<WarningCircle size={18} weight="fill" />}
              label="診断イベントログ"
              options={diagnosticEventOptions}
              value={form.diagnosticEventsEnabled ? 'on' : 'off'}
              onChange={(value) =>
                updateField('diagnosticEventsEnabled', value === 'on')
              }
            />
            <Switch.Root
              checked={form.detailedLogsEnabled}
              onCheckedChange={(details) =>
                updateField('detailedLogsEnabled', details.checked)
              }
              className={css({
                alignItems: 'center',
                bg: 'app.panel',
                borderColor: 'border',
                borderRadius: 'l2',
                borderWidth: '1px',
                colorPalette: 'blue',
                display: 'flex',
                gap: '3',
                justifyContent: 'space-between',
                minH: '16',
                p: '3',
                w: 'full',
              })}
            >
              <Switch.HiddenInput />
              <div
                className={css({
                  display: 'grid',
                  gap: '1',
                  minW: '0',
                })}
              >
                <Switch.Label className={css({ fontSize: 'sm' })}>
                  詳細ログ
                </Switch.Label>
                <div
                  className={css({
                    color: 'fg.muted',
                    textStyle: 'xs',
                  })}
                >
                  入力、通信、画面状態のログを増やして原因調査しやすくします
                </div>
              </div>
              <Switch.Control />
            </Switch.Root>
            <Dialog.Root
              open={cleanupConfirmOpen}
              onOpenChange={(details) => setCleanupConfirmOpen(details.open)}
            >
              <Dialog.Trigger asChild>
                <Button variant="outline">
                  <Trash size={18} weight="bold" />
                  古い詳細ログを削除
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
                      <Dialog.Title>古い詳細ログを削除しますか？</Dialog.Title>
                      <Dialog.Description>
                        対戦履歴、作成済みzip、現在実行中の対戦ログは残ります。
                      </Dialog.Description>
                    </Dialog.Header>
                    <Dialog.Footer>
                      <Dialog.ActionTrigger asChild>
                        <Button disabled={cleanupBusy} variant="outline">
                          キャンセル
                        </Button>
                      </Dialog.ActionTrigger>
                      <Button
                        colorPalette="red"
                        loading={cleanupBusy}
                        onClick={async () => {
                          setCleanupBusy(true);
                          try {
                            await actions.cleanupDetailedLogs();
                            setCleanupConfirmOpen(false);
                          } finally {
                            setCleanupBusy(false);
                          }
                        }}
                        variant="solid"
                      >
                        <Trash size={18} weight="bold" />
                        削除する
                      </Button>
                    </Dialog.Footer>
                  </Dialog.Content>
                </Dialog.Positioner>
              </Portal>
            </Dialog.Root>
          </SettingsPanel>

          <SettingsPanel
            icon={<HardDrives size={24} weight="fill" />}
            title="ROM設定"
          >
            <FilePathField
              label="ベース ROM"
              value={form.baseRomPath}
              onBrowse={() => void actions.selectRomPath('baseRomPath')}
            />
          </SettingsPanel>

          <SettingsPanel
            icon={<Broadcast size={24} weight="bold" />}
            title="接続設定"
          >
            <div
              className={css({
                display: 'grid',
                gap: '2',
                gridTemplateColumns: {
                  base: '1fr',
                  md: 'minmax(0, 1fr) auto',
                },
              })}
            >
              <TextField
                label="シグナリングサーバー"
                value={form.signalUrl}
                onChange={(value) => updateField('signalUrl', value)}
              />
              <Button
                className={css({ alignSelf: 'end' })}
                variant="outline"
                onClick={() => void actions.pollStatus()}
              >
                <WifiHigh size={18} weight="bold" />
                接続確認
              </Button>
            </div>
            <NumberField
              label="UDP ポート"
              value={form.port}
              min={1}
              max={65535}
              onChange={(value) => updateField('port', value)}
            />
          </SettingsPanel>

          <div
            className={css({
              display: 'grid',
              gap: '3',
              gridTemplateColumns: {
                base: '1fr',
                md: 'repeat(2, minmax(0, 1fr))',
              },
            })}
          >
            <Button
              variant="outline"
              onClick={() => void actions.preflightCheck()}
            >
              <ShieldCheck size={20} weight="bold" />
              起動前チェック
            </Button>
            <Button
              variant="outline"
              onClick={() => void actions.prepareRoms()}
            >
              <HardDrives size={20} weight="fill" />
              共通 ROM を準備
            </Button>
          </div>
        </section>

        <aside
          className={css({
            alignContent: 'start',
            display: 'grid',
            gap: '3',
          })}
        >
          <InfoPanel
            icon={<FlagCheckered size={24} weight="fill" />}
            title="現在の構成"
            badge={summary.romsConfigured ? '準備 OK' : '未完了'}
            badgeTone={summary.romsConfigured ? 'green' : 'slate'}
          >
            <div
              className={css({
                alignItems: 'center',
                bg: 'green.subtle.bg',
                borderColor: 'green.outline.border',
                borderRadius: 'l2',
                borderWidth: '1px',
                display: 'flex',
                gap: '2.5',
                p: '3',
              })}
            >
              <CheckCircle
                className={css({
                  color: 'green.plain.fg',
                  flexShrink: '0',
                })}
                size={32}
                weight="bold"
              />
              <div>
                <div
                  className={css({
                    color: 'green.subtle.fg',
                    fontWeight: 'black',
                    textStyle: 'md',
                  })}
                >
                  {summary.romsConfigured
                    ? '対戦準備 OK！'
                    : '設定を確認してください'}
                </div>
                <div
                  className={css({
                    color: 'fg.muted',
                    fontWeight: 'semibold',
                    textStyle: 'sm',
                  })}
                >
                  {summary.romsConfigured
                    ? 'ROM パスが設定されています'
                    : '未設定の ROM パスがあります'}
                </div>
              </div>
            </div>
            <SummaryItem
              label="接続"
              value={summary.connectionActive ? '接続中' : '未接続'}
            />
            <SummaryItem
              label="使用 ROM"
              value={shortPath(summary.currentRomPath)}
            />
            <SummaryItem label="共通 ROM" value={summary.romPreparation} />
            <SummaryItem label="シグナリング" value={form.signalUrl || '-'} />
            <SummaryItem label="UDP ポート" value={String(form.port)} />
          </InfoPanel>
        </aside>
      </div>
    </Tabs.Content>
  );
}
