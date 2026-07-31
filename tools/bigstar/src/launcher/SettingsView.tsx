import { Portal } from '@ark-ui/react';
import {
  BellRinging,
  Broadcast,
  GameController,
  HardDrives,
  Play,
  Trash,
  UserCircle,
  WarningCircle,
} from '@phosphor-icons/react';
import { useState } from 'react';
import { css, cx } from 'styled-system/css';
import { surface } from 'styled-system/recipes';
import { currentEdition, currentRuntimeCapabilities } from '../buildProfile';
import {
  FilePathField,
  NumberField,
  SelectField,
  TextField,
} from '../components/Fields';
import { Button, CloseButton, Dialog, Switch, Tabs } from '../components/ui';
import type { FormState } from '../types';
import { SettingsPanel } from './LauncherCards';
import type { LauncherActions, StartupState, UpdateFormField } from './types';

const diagnosticEventOptions = [
  { value: 'off', label: 'Off' },
  { value: 'on', label: 'On' },
];

const settingsSwitchClassName = cx(
  surface({ variant: 'inset' }),
  css({
    alignItems: 'center',
    colorPalette: 'blue',
    display: 'flex',
    gap: '3',
    justifyContent: 'space-between',
    minH: '16',
    p: '3',
    w: 'full',
  }),
);

export function SettingsView({
  actions,
  form,
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
  startup: StartupState;
  updateField: UpdateFormField;
}) {
  const [cleanupConfirmOpen, setCleanupConfirmOpen] = useState(false);
  const [cleanupBusy, setCleanupBusy] = useState(false);
  const { configurableSignalServer } = currentRuntimeCapabilities();
  const insidersEdition = currentEdition() === 'insiders';
  const advancedDiagnostics = insidersEdition || configurableSignalServer;

  return (
    <Tabs.Content value="settings">
      <div
        className={css({
          maxW: { base: 'xl', xl: 'mainPanel' },
          mx: 'auto',
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
              className={settingsSwitchClassName}
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
              className={settingsSwitchClassName}
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
            {advancedDiagnostics ? (
              <SelectField
                icon={<WarningCircle size={18} weight="fill" />}
                label="診断イベントログ"
                options={diagnosticEventOptions}
                value={form.diagnosticEventsEnabled ? 'on' : 'off'}
                onChange={(value) =>
                  updateField('diagnosticEventsEnabled', value === 'on')
                }
              />
            ) : null}
            {insidersEdition ? (
              <Switch.Root
                checked={form.performanceLogsEnabled}
                onCheckedChange={(details) =>
                  updateField('performanceLogsEnabled', details.checked)
                }
                className={settingsSwitchClassName}
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
                    パフォーマンスログ
                  </Switch.Label>
                  <div
                    className={css({
                      color: 'fg.muted',
                      textStyle: 'xs',
                    })}
                  >
                    FPS低下時の処理時間、音声待ち、CPU時間を低負荷で記録します
                  </div>
                </div>
                <Switch.Control />
              </Switch.Root>
            ) : null}
            {advancedDiagnostics ? (
              <Switch.Root
                checked={form.detailedLogsEnabled}
                onCheckedChange={(details) =>
                  updateField('detailedLogsEnabled', details.checked)
                }
                className={settingsSwitchClassName}
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
            ) : null}
            {advancedDiagnostics ? (
              <Switch.Root
                checked={form.aiPlayLogEnabled}
                onCheckedChange={(details) =>
                  updateField('aiPlayLogEnabled', details.checked)
                }
                className={settingsSwitchClassName}
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
                    AI用プレイログ
                  </Switch.Label>
                  <div
                    className={css({
                      color: 'fg.muted',
                      textStyle: 'xs',
                    })}
                  >
                    stage 0の対戦中だけcompact observation v3ログを保存します
                  </div>
                </div>
                <Switch.Control />
              </Switch.Root>
            ) : null}
            {insidersEdition ? (
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
                        <Dialog.Title>
                          古い詳細ログを削除しますか？
                        </Dialog.Title>
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
            ) : null}
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
            {configurableSignalServer ? (
              <TextField
                label="シグナリングサーバー"
                value={form.signalUrl}
                onChange={(value) => updateField('signalUrl', value)}
              />
            ) : null}
            <NumberField
              label="UDP ポート"
              value={form.port}
              min={1}
              max={65535}
              onChange={(value) => updateField('port', value)}
            />
          </SettingsPanel>
        </section>
      </div>
    </Tabs.Content>
  );
}
