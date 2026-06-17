import {
  Broadcast,
  CheckCircle,
  FlagCheckered,
  GameController,
  HardDrives,
  Play,
  ShieldCheck,
  WarningCircle,
  WifiHigh,
} from '@phosphor-icons/react';
import { css } from 'styled-system/css';
import { token } from 'styled-system/tokens';
import {
  FilePathField,
  NumberField,
  SelectField,
  TextField,
} from '../components/Fields';
import { SummaryItem } from '../components/SummaryItem';
import { Button, Tabs } from '../components/ui';
import type { FormState } from '../types';
import { InfoPanel, SettingsPanel } from './LauncherCards';
import { shortPath } from './path';
import type {
  LauncherActions,
  LauncherSummary,
  UpdateFormField,
} from './types';

const diagnosticEventOptions = [
  { value: 'off', label: 'Off' },
  { value: 'on', label: 'On' },
];

export function SettingsView({
  actions,
  form,
  summary,
  updateField,
}: {
  actions: Pick<
    LauncherActions,
    | 'openMelonds'
    | 'openMelondsInputConfig'
    | 'pollStatus'
    | 'preflightCheck'
    | 'prepareRoms'
    | 'selectRomPath'
  >;
  form: FormState;
  summary: LauncherSummary;
  updateField: UpdateFormField;
}) {
  return (
    <Tabs.Content value="settings">
      <div
        className={css({
          display: 'grid',
          gap: '5',
          gridTemplateColumns: `minmax(0, 1fr) ${token('sizes.settingsAside')}`,
          '@media (max-width: 1180px)': {
            gridTemplateColumns: '1fr',
          },
        })}
      >
        <section
          className={css({
            alignContent: 'start',
            display: 'grid',
            gap: '4',
          })}
        >
          <SettingsPanel
            icon={<Broadcast size={24} weight="bold" />}
            title="接続設定"
          >
            <div
              className={css({
                display: 'grid',
                gap: '3',
                gridTemplateColumns: 'minmax(0, 1fr) auto',
                '@media (max-width: 760px)': {
                  gridTemplateColumns: '1fr',
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

          <SettingsPanel
            icon={<HardDrives size={24} weight="fill" />}
            title="ROM 設定"
          >
            <FilePathField
              label="ベース ROM"
              value={form.baseRomPath}
              onBrowse={() => void actions.selectRomPath('baseRomPath')}
            />
          </SettingsPanel>

          <SettingsPanel
            icon={<GameController size={24} weight="fill" />}
            title="melonDS 設定"
          >
            <div
              className={css({
                display: 'grid',
                gap: '4',
                gridTemplateColumns: 'repeat(2, minmax(0, 1fr))',
                '@media (max-width: 760px)': {
                  gridTemplateColumns: '1fr',
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
          </SettingsPanel>

          <div
            className={css({
              display: 'grid',
              gap: '4',
              gridTemplateColumns: 'repeat(2, minmax(0, 1fr))',
              '@media (max-width: 760px)': {
                gridTemplateColumns: '1fr',
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
            gap: '4',
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
                gap: '4',
                p: '4',
              })}
            >
              <CheckCircle
                className={css({
                  color: 'green.plain.fg',
                  flexShrink: '0',
                })}
                size={46}
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
