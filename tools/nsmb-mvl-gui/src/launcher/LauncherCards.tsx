import { CheckCircle } from '@phosphor-icons/react';
import type { ReactNode } from 'react';
import { readyLabel, shortPath } from './path';

export function InfoPanel({
  badge,
  badgeTone,
  children,
  icon,
  title,
}: {
  badge?: string;
  badgeTone?: 'green' | 'slate';
  children: ReactNode;
  icon: ReactNode;
  title: string;
}) {
  return (
    <section className="grid gap-4 rounded-lg border border-slate-700/90 bg-slate-950/55 p-5 shadow-[0_24px_70px_rgba(0,0,0,0.25)]">
      <div className="flex items-center justify-between gap-3">
        <h2 className="flex items-center gap-2 text-lg font-black text-white">
          <span className="text-blue-300">{icon}</span>
          {title}
        </h2>
        {badge ? (
          <span
            className={`rounded-full border px-3 py-1 text-xs font-black ${
              badgeTone === 'green'
                ? 'border-emerald-400/30 bg-emerald-400/12 text-emerald-300'
                : 'border-slate-500/40 bg-slate-700/35 text-slate-300'
            }`}
          >
            {badge}
          </span>
        ) : null}
      </div>
      <div className="grid gap-3">{children}</div>
    </section>
  );
}

export function MetricCard({
  icon,
  label,
  tone,
  value,
}: {
  icon: ReactNode;
  label: string;
  tone: 'blue' | 'green' | 'red' | 'yellow';
  value: string;
}) {
  const colors = {
    blue: 'text-blue-300 bg-blue-500/12 border-blue-300/20',
    green: 'text-emerald-300 bg-emerald-500/12 border-emerald-300/20',
    red: 'text-red-300 bg-red-500/12 border-red-300/20',
    yellow: 'text-yellow-300 bg-yellow-500/12 border-yellow-300/20',
  };
  return (
    <div
      className={`flex min-h-20 items-center gap-3 rounded-lg border px-4 ${colors[tone]}`}
    >
      <span className="shrink-0">{icon}</span>
      <div className="min-w-0">
        <div className="text-xs font-bold text-slate-400">{label}</div>
        <div className="text-base font-black leading-tight text-white">
          {value}
        </div>
      </div>
    </div>
  );
}

export function RomReadyRow({
  label,
  value,
}: {
  label: string;
  value: string;
}) {
  const ready = Boolean(value);
  return (
    <div className="grid grid-cols-[auto_minmax(0,1fr)_auto] items-center gap-3">
      <CheckCircle
        className={ready ? 'text-emerald-400' : 'text-slate-600'}
        size={22}
        weight="fill"
      />
      <div className="min-w-0">
        <div className="text-sm font-black text-slate-300">{label}</div>
        <div className="truncate text-xs font-semibold text-slate-500">
          {shortPath(value)}
        </div>
      </div>
      <span
        className={`text-xs font-black ${
          ready ? 'text-emerald-300' : 'text-slate-500'
        }`}
      >
        {readyLabel(value)}
      </span>
    </div>
  );
}

export function SmallInfoCard({
  caption,
  icon,
  imageSrc,
  label,
  value,
}: {
  caption?: string;
  icon?: ReactNode;
  imageSrc?: string;
  label: string;
  value: string;
}) {
  return (
    <div className="grid min-h-28 gap-2 rounded-lg border border-slate-700/90 bg-slate-950/55 p-4">
      <div className="text-sm font-black text-slate-400">{label}</div>
      <div className="flex items-center gap-3 text-xl font-black text-white">
        {imageSrc ? (
          <img src={imageSrc} alt="" className="size-12 object-contain" />
        ) : (
          <span className="text-red-300">{icon}</span>
        )}
        {value}
      </div>
      {caption ? (
        <div className="text-sm font-semibold text-slate-500">{caption}</div>
      ) : null}
    </div>
  );
}

export function SettingsSectionLabel({
  active = false,
  icon,
  label,
}: {
  active?: boolean;
  icon: ReactNode;
  label: string;
}) {
  return (
    <div
      className={`flex min-h-16 items-center justify-center gap-2 border-r border-slate-700/80 text-base font-black last:border-r-0 ${
        active
          ? 'border-t-2 border-t-red-500 bg-red-500/10 text-white'
          : 'text-slate-400'
      }`}
    >
      <span className={active ? 'text-red-300' : 'text-slate-500'}>{icon}</span>
      {label}
    </div>
  );
}

export function SettingsPanel({
  children,
  icon,
  title,
}: {
  children: ReactNode;
  icon: ReactNode;
  title: string;
}) {
  return (
    <section className="grid gap-4 rounded-lg border border-slate-700/90 bg-slate-900/45 p-4">
      <h2 className="flex items-center gap-2 text-lg font-black text-white">
        <span className="text-blue-300">{icon}</span>
        {title}
      </h2>
      {children}
    </section>
  );
}

export function RuleChip({
  active,
  label,
}: {
  active: boolean;
  label: string;
}) {
  return (
    <div
      className={`min-h-10 rounded-lg border px-3 py-2 text-center text-xs font-black ${
        active
          ? 'border-yellow-300/50 bg-yellow-400/12 text-yellow-200'
          : 'border-slate-700 bg-slate-950/50 text-slate-500'
      }`}
    >
      {label}
    </div>
  );
}
