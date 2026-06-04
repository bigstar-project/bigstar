import { Tabs } from '@base-ui/react/tabs';
import { Flag, FlagCheckered, Gear, Wrench } from '@phosphor-icons/react';
import type { ReactNode } from 'react';
import { StatusPill } from '../components/StatusPill';
import type { StatusKind } from '../types';
import type { UpdateStatus, View } from './types';

function updateButtonLabel(updateStatus: UpdateStatus) {
  if (updateStatus.phase === 'checking') {
    return '確認中';
  }
  if (updateStatus.phase === 'available') {
    return '更新あり';
  }
  if (updateStatus.phase === 'downloading') {
    return '取得中';
  }
  if (updateStatus.phase === 'installed') {
    return '再起動中';
  }
  if (updateStatus.phase === 'error') {
    return '更新失敗';
  }
  return '更新確認';
}

function updateButtonClass(updateStatus: UpdateStatus) {
  if (updateStatus.phase === 'available') {
    return 'border-yellow-300 bg-yellow-400 text-slate-950 shadow-[0_0_28px_rgba(250,204,21,0.32)] hover:bg-yellow-300';
  }
  if (updateStatus.phase === 'error') {
    return 'border-red-400/80 bg-red-500/18 text-red-100 hover:bg-red-500/25';
  }
  if (
    updateStatus.phase === 'checking' ||
    updateStatus.phase === 'downloading'
  ) {
    return 'border-blue-300/70 bg-blue-500/18 text-blue-100';
  }
  return 'border-slate-600 bg-slate-950/35 text-slate-200 hover:border-slate-500 hover:bg-slate-800/65';
}

export function LauncherShell({
  activeView,
  children,
  onCheckForUpdate,
  onViewChange,
  status,
  updateBusy,
  updateStatus,
}: {
  activeView: View;
  children: ReactNode;
  onCheckForUpdate: () => void;
  onViewChange: (view: View) => void;
  status: { text: string; kind: StatusKind };
  updateBusy: boolean;
  updateStatus: UpdateStatus;
}) {
  return (
    <Tabs.Root
      className="launcher-background min-h-screen text-slate-100"
      orientation="vertical"
      value={activeView}
      onValueChange={(value) => onViewChange(value as View)}
    >
      <main className="grid min-h-screen grid-cols-[236px_minmax(0,1fr)] max-[1280px]:grid-cols-[92px_minmax(0,1fr)]">
        <aside className="sticky top-0 grid h-screen border-r border-blue-300/15 bg-[#06101d]/88 px-4 py-6 shadow-[inset_-1px_0_0_rgba(96,165,250,0.08)] backdrop-blur-sm max-[1280px]:px-3">
          <div className="grid content-between">
            <div className="grid gap-8">
              <div className="grid gap-1 px-2 max-[1280px]:justify-items-center">
                <div className="text-3xl font-black leading-none text-white max-[1280px]:text-xl">
                  NSMB
                </div>
                <div className="text-3xl font-black leading-none max-[1280px]:text-xl">
                  <span className="text-red-400">M</span>
                  <span className="text-sky-300">v</span>
                  <span className="text-emerald-300">L</span>
                </div>
                <div className="text-xs font-bold text-sky-300/80 max-[1280px]:hidden">
                  Mario vs Luigi Online
                </div>
              </div>

              <Tabs.List className="grid gap-3">
                <Tabs.Tab
                  className="group flex min-h-14 items-center gap-3 rounded-lg border border-transparent px-3 text-left text-slate-300 outline-none transition hover:border-blue-300/30 hover:bg-blue-400/10 data-[active]:border-blue-400 data-[active]:bg-blue-500/20 data-[active]:text-white data-[active]:shadow-[0_0_22px_rgba(59,130,246,0.2)] max-[1280px]:justify-center"
                  value="battle"
                >
                  <FlagCheckered
                    className="shrink-0 text-white group-data-[active]:text-yellow-300"
                    size={28}
                    weight="fill"
                  />
                  <span className="text-base font-black max-[1280px]:hidden">
                    対戦
                  </span>
                </Tabs.Tab>
                <Tabs.Tab
                  className="group flex min-h-14 items-center gap-3 rounded-lg border border-transparent px-3 text-left text-slate-300 outline-none transition hover:border-blue-300/30 hover:bg-blue-400/10 data-[active]:border-blue-400 data-[active]:bg-blue-500/20 data-[active]:text-white data-[active]:shadow-[0_0_22px_rgba(59,130,246,0.2)] max-[1280px]:justify-center"
                  value="settings"
                >
                  <Gear
                    className="shrink-0 text-slate-400 group-data-[active]:text-yellow-300"
                    size={28}
                    weight="fill"
                  />
                  <span className="text-base font-black max-[1280px]:hidden">
                    設定
                  </span>
                </Tabs.Tab>
              </Tabs.List>
            </div>
            <button
              type="button"
              className={`inline-flex min-h-11 items-center justify-center gap-2 rounded-md border px-3 font-black transition focus:outline-none focus-visible:ring-4 focus-visible:ring-blue-300/20 disabled:cursor-not-allowed disabled:opacity-60 max-[1280px]:min-w-14 ${updateButtonClass(updateStatus)}`}
              disabled={updateBusy}
              title={
                updateStatus.version ? `v${updateStatus.version}` : '更新を確認'
              }
              onClick={onCheckForUpdate}
            >
              <Wrench className="shrink-0" size={20} weight="bold" />
              <span className="max-[1280px]:hidden">
                {updateButtonLabel(updateStatus)}
              </span>
            </button>
          </div>
        </aside>

        <div className="min-w-0 bg-[linear-gradient(180deg,rgba(7,17,31,0.72)_0%,rgba(10,21,38,0.58)_58%,rgba(6,11,20,0.72)_100%)]">
          <div className="mx-auto grid w-[min(1260px,calc(100vw-284px))] gap-6 px-7 py-7 max-[1280px]:w-[calc(100vw-92px)] max-[1280px]:px-5 max-[720px]:px-4">
            <header className="flex items-start justify-between gap-5 max-[720px]:grid">
              <div className="grid gap-1">
                <div className="flex items-center gap-3">
                  {activeView === 'battle' ? (
                    <Flag className="text-red-400" size={36} weight="fill" />
                  ) : (
                    <Gear className="text-slate-300" size={36} weight="fill" />
                  )}
                  <h1 className="text-3xl font-black text-white">
                    {activeView === 'battle' ? '対戦' : '設定'}
                  </h1>
                </div>
                <p className="text-sm font-semibold text-slate-400">
                  {activeView === 'battle'
                    ? 'オンラインでライバルと対戦しよう！'
                    : 'オンライン対戦の環境を整えましょう'}
                </p>
              </div>
              <div className="flex items-center gap-3 max-[720px]:flex-wrap">
                <StatusPill kind={status.kind}>{status.text}</StatusPill>
              </div>
            </header>

            {children}
          </div>
        </div>
      </main>
    </Tabs.Root>
  );
}
