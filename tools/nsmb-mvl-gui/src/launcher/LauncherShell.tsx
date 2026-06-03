import { Tabs } from '@base-ui/react/tabs';
import {
  Flag,
  FlagCheckered,
  GameController,
  Gear,
  Wrench,
} from '@phosphor-icons/react';
import type { ReactNode } from 'react';
import { ActionButton } from '../components/Fields';
import { StatusPill } from '../components/StatusPill';
import type { StatusKind } from '../types';
import type { View } from './types';

export function LauncherShell({
  activeView,
  children,
  connectionActive,
  onCheckForUpdate,
  onViewChange,
  status,
  updateBusy,
}: {
  activeView: View;
  children: ReactNode;
  connectionActive: boolean;
  onCheckForUpdate: () => void;
  onViewChange: (view: View) => void;
  status: { text: string; kind: StatusKind };
  updateBusy: boolean;
}) {
  return (
    <Tabs.Root
      className="launcher-background min-h-screen text-slate-100"
      orientation="vertical"
      value={activeView}
      onValueChange={(value) => onViewChange(value as View)}
    >
      <main className="grid min-h-screen grid-cols-[236px_minmax(0,1fr)] max-[980px]:grid-cols-[92px_minmax(0,1fr)]">
        <aside className="relative grid border-r border-blue-300/15 bg-[#06101d]/88 px-4 py-6 shadow-[inset_-1px_0_0_rgba(96,165,250,0.08)] backdrop-blur-sm max-[980px]:px-3">
          <div className="grid content-between">
            <div className="grid gap-8">
              <div className="grid gap-1 px-2 max-[980px]:justify-items-center">
                <div className="text-3xl font-black leading-none text-white max-[980px]:text-xl">
                  NSMB
                </div>
                <div className="text-3xl font-black leading-none max-[980px]:text-xl">
                  <span className="text-red-400">M</span>
                  <span className="text-sky-300">v</span>
                  <span className="text-emerald-300">L</span>
                </div>
                <div className="text-xs font-bold text-sky-300/80 max-[980px]:hidden">
                  Mario vs Luigi Online
                </div>
              </div>

              <Tabs.List className="grid gap-3">
                <Tabs.Tab
                  className="group flex min-h-14 items-center gap-3 rounded-lg border border-transparent px-3 text-left text-slate-300 outline-none transition hover:border-blue-300/30 hover:bg-blue-400/10 data-[active]:border-blue-400 data-[active]:bg-blue-500/20 data-[active]:text-white data-[active]:shadow-[0_0_22px_rgba(59,130,246,0.2)] max-[980px]:justify-center"
                  value="battle"
                >
                  <FlagCheckered
                    className="shrink-0 text-white group-data-[active]:text-yellow-300"
                    size={28}
                    weight="fill"
                  />
                  <span className="text-base font-black max-[980px]:hidden">
                    対戦
                  </span>
                </Tabs.Tab>
                <Tabs.Tab
                  className="group flex min-h-14 items-center gap-3 rounded-lg border border-transparent px-3 text-left text-slate-300 outline-none transition hover:border-blue-300/30 hover:bg-blue-400/10 data-[active]:border-red-400 data-[active]:bg-red-500/20 data-[active]:text-white data-[active]:shadow-[0_0_22px_rgba(239,68,68,0.18)] max-[980px]:justify-center"
                  value="settings"
                >
                  <Gear
                    className="shrink-0 text-slate-400 group-data-[active]:text-red-300"
                    size={28}
                    weight="fill"
                  />
                  <span className="text-base font-black max-[980px]:hidden">
                    設定
                  </span>
                </Tabs.Tab>
              </Tabs.List>
            </div>

            <div className="grid gap-4 px-2 max-[980px]:justify-items-center">
              <div className="rounded-lg border border-slate-700/80 bg-slate-950/40 p-4 text-center max-[980px]:hidden">
                <GameController
                  className="mx-auto mb-3 text-slate-400"
                  size={32}
                  weight="fill"
                />
                <p className="text-sm font-bold text-slate-300">
                  準備を整えて
                  <br />
                  対戦に挑もう！
                </p>
              </div>
              <div className="flex items-center gap-2 text-xs font-bold text-slate-500">
                <span
                  className={`size-2 rounded-full ${
                    connectionActive ? 'bg-emerald-400' : 'bg-slate-500'
                  }`}
                />
                <span className="max-[980px]:hidden">v0.1.1</span>
              </div>
            </div>
          </div>
        </aside>

        <div className="min-w-0 bg-[linear-gradient(180deg,rgba(7,17,31,0.72)_0%,rgba(10,21,38,0.58)_58%,rgba(6,11,20,0.72)_100%)]">
          <div className="mx-auto grid w-[min(1260px,calc(100vw-284px))] gap-6 px-7 py-7 max-[980px]:w-[calc(100vw-92px)] max-[980px]:px-5">
            <header className="flex items-start justify-between gap-5">
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
              <div className="flex items-center gap-3">
                <ActionButton
                  kind="ghost"
                  type="button"
                  disabled={updateBusy}
                  icon={<Wrench size={18} weight="bold" />}
                  onClick={onCheckForUpdate}
                >
                  更新確認
                </ActionButton>
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
