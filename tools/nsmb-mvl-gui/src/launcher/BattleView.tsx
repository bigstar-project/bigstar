import { Collapsible } from '@base-ui/react/collapsible';
import { Tabs } from '@base-ui/react/tabs';
import {
  Broadcast,
  CaretDown,
  Copy,
  Crown,
  Flag,
  HardDrives,
  Info,
  Play,
  RadioButton,
  ShieldCheck,
  Star,
  Stop,
  Trophy,
  Users,
  WifiHigh,
} from '@phosphor-icons/react';
import lifeMushroom from '../assets/life-mushroom.png';
import playerLBadge from '../assets/player-l.png';
import playerMBadge from '../assets/player-m.png';
import {
  ActionButton,
  RoleButton,
  SelectField,
  TextField,
} from '../components/Fields';
import { SummaryItem } from '../components/SummaryItem';
import { WebRtcDiagnosticsPanel } from '../components/WebRtcDiagnosticsPanel';
import type { CourseMode, FormState, Lives } from '../types';
import { InfoPanel, RomReadyRow, SmallInfoCard } from './LauncherCards';
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
  UpdateFormField,
} from './types';

export function BattleView({
  actions,
  diagnostics,
  form,
  lastLogDir,
  summary,
  updateField,
}: {
  actions: Pick<
    LauncherActions,
    | 'copyRoomCode'
    | 'openLogDir'
    | 'preflightCheck'
    | 'prepareRoms'
    | 'startMatch'
    | 'stopMatch'
  >;
  diagnostics: DiagnosticsState;
  form: FormState;
  lastLogDir: string;
  summary: LauncherSummary;
  updateField: UpdateFormField;
}) {
  return (
    <Tabs.Panel value="battle">
      <form
        className="grid grid-cols-[minmax(0,1fr)_390px] gap-5 max-[1180px]:grid-cols-1"
        onSubmit={(event) => {
          event.preventDefault();
          void actions.startMatch();
        }}
      >
        <section className="grid gap-4">
          <div className="rounded-lg border border-slate-700/90 bg-slate-950/55 p-6 shadow-[0_24px_70px_rgba(0,0,0,0.28)]">
            <div className="grid grid-cols-[minmax(220px,0.85fr)_minmax(0,1.15fr)] gap-6 max-[860px]:grid-cols-1">
              <div className="grid gap-3 border-r border-slate-700/80 pr-6 max-[860px]:border-r-0 max-[860px]:border-b max-[860px]:pb-5 max-[860px]:pr-0">
                <div className="flex items-center gap-2 text-sm font-black text-slate-300">
                  部屋コード
                  <Info className="text-slate-500" size={18} />
                </div>
                <input
                  className="w-full border-none bg-transparent text-4xl font-black text-white outline-none placeholder:text-slate-700 max-[860px]:text-4xl"
                  value={form.roomCode}
                  maxLength={64}
                  placeholder="test-room"
                  autoComplete="off"
                  onChange={(event) =>
                    updateField('roomCode', event.target.value)
                  }
                />
                <button
                  type="button"
                  className="inline-flex min-h-10 w-fit min-w-32 items-center justify-center gap-2 rounded-md border border-blue-400/70 bg-blue-500/10 px-5 font-black text-blue-100 transition hover:border-blue-300 hover:bg-blue-500/18 focus:outline-none focus:ring-4 focus:ring-blue-300/20"
                  onClick={() => void actions.copyRoomCode()}
                >
                  <Copy size={18} weight="bold" />
                  コピー
                </button>
              </div>

              <div className="grid gap-3">
                <div className="text-sm font-black text-slate-300">
                  モードを選択
                </div>
                <div className="grid grid-cols-2 gap-3 max-[640px]:grid-cols-1">
                  <RoleButton
                    active={form.role === 'host'}
                    icon={<Crown size={26} weight="fill" />}
                    title="ホスト"
                    subtitle="部屋を作成して待つ"
                    onClick={() => updateField('role', 'host')}
                  />
                  <RoleButton
                    active={form.role === 'client'}
                    icon={<Users size={26} weight="fill" />}
                    title="参加"
                    subtitle="部屋に参加する"
                    onClick={() => updateField('role', 'client')}
                  />
                </div>
                <div className="flex flex-wrap items-center justify-between gap-3 rounded-lg bg-slate-900/80 px-3 py-2">
                  <div className="flex items-center gap-2 text-sm font-black text-slate-300">
                    <span
                      className={`size-3 rounded-full ${
                        summary.connectionActive
                          ? 'bg-emerald-400'
                          : 'bg-slate-500'
                      }`}
                    />
                    状態:
                    <span
                      className={
                        summary.connectionActive
                          ? 'text-emerald-300'
                          : 'text-slate-300'
                      }
                    >
                      {summary.connectionActive ? '接続中' : '待機中'}
                    </span>
                  </div>
                  <div className="flex items-center gap-2 text-sm font-black text-slate-400">
                    <WifiHigh size={20} weight="bold" />
                    {summary.connectionActive ? 'オンライン' : '未接続'}
                  </div>
                </div>
              </div>
            </div>
          </div>

          <div className="rounded-lg border border-slate-700/90 bg-slate-950/45 p-5">
            <div className="mb-4 flex items-center gap-3">
              <h2 className="flex items-center gap-2 text-lg font-black text-white">
                <Star className="text-yellow-300" size={24} weight="fill" />
                ゲーム設定
              </h2>
            </div>

            <div className="grid gap-3">
              <div className="grid grid-cols-4 gap-3 max-[1120px]:grid-cols-2 max-[720px]:grid-cols-1">
                <SelectField
                  icon={<RadioButton size={18} />}
                  label="コース"
                  options={courseOptions}
                  value={form.courseMode}
                  onChange={(value) =>
                    updateField('courseMode', value as CourseMode)
                  }
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
                  icon={
                    <img
                      src={lifeMushroom}
                      alt=""
                      className="size-5 object-contain"
                    />
                  }
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
          </div>

          <div className="grid justify-items-center gap-3">
            <button
              type="submit"
              className="group relative min-h-20 w-[min(680px,100%)] overflow-hidden rounded-lg border-4 border-yellow-300 bg-red-600 px-8 text-3xl font-black text-white shadow-[0_0_0_1px_rgba(255,255,255,0.1),0_0_38px_rgba(239,68,68,0.48)] transition hover:bg-red-500 focus:outline-none focus:ring-4 focus:ring-yellow-300/35"
            >
              <span className="flex items-center justify-center gap-4">
                対戦を開始
                <Play
                  className="transition group-hover:translate-x-1"
                  size={34}
                  weight="fill"
                />
              </span>
            </button>
            <div className="flex flex-wrap justify-center gap-2">
              <ActionButton
                kind="ghost"
                type="button"
                icon={<ShieldCheck size={18} weight="bold" />}
                onClick={() => void actions.preflightCheck()}
              >
                起動前チェック
              </ActionButton>
              <ActionButton
                kind="ghost"
                type="button"
                icon={<HardDrives size={18} weight="bold" />}
                onClick={() => void actions.prepareRoms()}
              >
                共通ROM再準備
              </ActionButton>
              <ActionButton
                kind="danger"
                type="button"
                icon={<Stop size={18} weight="fill" />}
                onClick={() => void actions.stopMatch()}
              >
                停止
              </ActionButton>
            </div>
            <p className="text-sm font-semibold text-slate-500">
              ホストはすべての準備ができたら開始してください
            </p>
          </div>

          <BattleLogPanel
            lastLogDir={lastLogDir}
            onOpenLogDir={() => void actions.openLogDir()}
          />
        </section>

        <aside className="grid content-start gap-4">
          <InfoPanel
            icon={<Broadcast size={22} weight="bold" />}
            title="接続状況"
            badge={summary.connectionActive ? '良好' : '待機'}
            badgeTone={summary.connectionActive ? 'green' : 'slate'}
          >
            <SummaryItem
              label="接続品質"
              value={summary.connectionActive ? '接続中' : '未接続'}
            />
            <WebRtcDiagnosticsPanel
              diagnostics={diagnostics.bridgeDiagnostics}
              compact
            />
          </InfoPanel>

          <InfoPanel
            icon={<HardDrives size={22} weight="fill" />}
            title="ROM 準備状況"
            badge={summary.romsConfigured ? '設定済み' : '未設定'}
            badgeTone={summary.romsConfigured ? 'green' : 'slate'}
          >
            <RomReadyRow label="ホスト用 ROM" value={form.hostRomPath} />
            <RomReadyRow label="参加用 ROM" value={form.clientRomPath} />
            <RomReadyRow label="共通 ROM" value={form.baseRomPath} />
            <div className="rounded-lg border border-blue-400/30 bg-blue-500/10 px-3 py-3 text-sm font-bold text-blue-200">
              {summary.romsConfigured
                ? 'ROM パスが設定されています'
                : '設定画面で ROM パスを指定してください'}
            </div>
          </InfoPanel>

          <div className="grid grid-cols-2 gap-3">
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
    </Tabs.Panel>
  );
}

function BattleLogPanel({
  lastLogDir,
  onOpenLogDir,
}: {
  lastLogDir: string;
  onOpenLogDir: () => void;
}) {
  return (
    <Collapsible.Root className="rounded-lg border border-slate-700/90 bg-slate-950/45">
      <Collapsible.Trigger className="flex min-h-14 w-full items-center justify-between px-5 text-left text-slate-300 outline-none transition hover:text-white">
        <span className="flex items-center gap-3 text-base font-black">
          <Broadcast size={22} />
          通信ログ
          <span className="text-sm font-semibold text-slate-500">
            ログはここに表示されます（クリックで展開）
          </span>
        </span>
        <CaretDown size={24} />
      </Collapsible.Trigger>
      <Collapsible.Panel className="grid gap-3 border-t border-slate-700/80 p-5">
        <div className="flex items-center justify-between gap-3">
          <span className="text-sm font-bold text-slate-400">
            Log directory
          </span>
          <ActionButton
            kind="outline"
            type="button"
            disabled={!lastLogDir}
            onClick={onOpenLogDir}
          >
            ログを開く
          </ActionButton>
        </div>
        <code className="overflow-wrap-anywhere rounded-lg border border-slate-700 bg-slate-950 px-3 py-2 font-mono text-xs font-semibold text-slate-300">
          {lastLogDir || 'not started'}
        </code>
      </Collapsible.Panel>
    </Collapsible.Root>
  );
}
