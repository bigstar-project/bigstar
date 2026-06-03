import { Tabs } from '@base-ui/react/tabs';
import {
  Broadcast,
  CheckCircle,
  FlagCheckered,
  HardDrives,
  ShieldCheck,
  WifiHigh,
} from '@phosphor-icons/react';
import { ActionButton } from '../components/Button';
import { FilePathField, NumberField, TextField } from '../components/Fields';
import { SummaryItem } from '../components/SummaryItem';
import type { FormState } from '../types';
import { InfoPanel, SettingsPanel } from './LauncherCards';
import { shortPath } from './path';
import type {
  LauncherActions,
  LauncherSummary,
  UpdateFormField,
} from './types';

export function SettingsView({
  actions,
  form,
  summary,
  updateField,
}: {
  actions: Pick<
    LauncherActions,
    'pollStatus' | 'preflightCheck' | 'prepareRoms' | 'selectRomPath'
  >;
  form: FormState;
  summary: LauncherSummary;
  updateField: UpdateFormField;
}) {
  return (
    <Tabs.Panel value="settings">
      <div className="grid grid-cols-[minmax(0,1fr)_360px] gap-5 max-[1180px]:grid-cols-1">
        <section className="grid content-start gap-4">
          <div className="grid rounded-lg border border-slate-700/90 bg-slate-950/55">
            <div className="grid gap-4 p-5">
              <SettingsPanel
                icon={<Broadcast size={24} weight="bold" />}
                title="接続設定"
              >
                <div className="grid grid-cols-[minmax(0,1fr)_auto] gap-3 max-[760px]:grid-cols-1">
                  <TextField
                    label="シグナリングサーバー"
                    value={form.signalUrl}
                    onChange={(value) => updateField('signalUrl', value)}
                  />
                  <ActionButton
                    className="self-end"
                    kind="outline"
                    type="button"
                    icon={<WifiHigh size={18} weight="bold" />}
                    onClick={() => void actions.pollStatus()}
                  >
                    接続確認
                  </ActionButton>
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
                  label="ホスト用 ROM"
                  value={form.hostRomPath}
                  onBrowse={() => void actions.selectRomPath('hostRomPath')}
                />
                <FilePathField
                  label="参加用 ROM"
                  value={form.clientRomPath}
                  onBrowse={() => void actions.selectRomPath('clientRomPath')}
                />
                <FilePathField
                  label="ベース ROM"
                  value={form.baseRomPath}
                  onBrowse={() => void actions.selectRomPath('baseRomPath')}
                />
              </SettingsPanel>

              <div className="grid grid-cols-[minmax(0,1fr)_minmax(0,1fr)] gap-4 max-[760px]:grid-cols-1">
                <ActionButton
                  kind="ghost"
                  type="button"
                  icon={<ShieldCheck size={20} weight="bold" />}
                  onClick={() => void actions.preflightCheck()}
                >
                  起動前チェック
                </ActionButton>
                <ActionButton
                  kind="primary"
                  type="button"
                  icon={<HardDrives size={20} weight="fill" />}
                  onClick={() => void actions.prepareRoms()}
                >
                  共通 ROM を準備
                </ActionButton>
              </div>
            </div>
          </div>
        </section>

        <aside className="grid content-start gap-4">
          <InfoPanel
            icon={<FlagCheckered size={24} weight="fill" />}
            title="現在の構成"
            badge={summary.romsConfigured ? '準備 OK' : '未完了'}
            badgeTone={summary.romsConfigured ? 'green' : 'slate'}
          >
            <div className="flex items-center gap-4 rounded-lg border border-emerald-400/20 bg-emerald-500/10 p-4">
              <CheckCircle
                className="shrink-0 text-emerald-300"
                size={46}
                weight="bold"
              />
              <div>
                <div className="text-base font-black text-emerald-200">
                  {summary.romsConfigured
                    ? '対戦準備 OK！'
                    : '設定を確認してください'}
                </div>
                <div className="text-sm font-semibold text-slate-400">
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
    </Tabs.Panel>
  );
}
