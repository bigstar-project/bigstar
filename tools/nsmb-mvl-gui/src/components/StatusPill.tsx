import type { StatusKind } from '../types';

export function StatusPill({
  children,
  kind,
}: {
  children: string;
  kind: StatusKind;
}) {
  const colors: Record<StatusKind, string> = {
    idle: 'border-slate-600 bg-slate-950/55 text-slate-300',
    ok: 'border-emerald-400/45 bg-emerald-500/12 text-emerald-200',
    warn: 'border-amber-300/45 bg-amber-500/12 text-amber-100',
    error:
      'border-rose-400/70 bg-rose-500/14 text-rose-100 shadow-[0_0_26px_rgba(244,63,94,0.22)]',
  };
  const label: Record<StatusKind, string> = {
    idle: '待機',
    ok: '正常',
    warn: '注意',
    error: 'エラー',
  };
  return (
    <div
      className={`grid min-h-12 max-w-[48ch] gap-0.5 overflow-wrap-anywhere rounded-lg border px-3 py-2 shadow-sm ${colors[kind]}`}
    >
      <span className="text-[11px] font-black uppercase tracking-normal opacity-75">
        {label[kind]}
      </span>
      <span className="text-sm font-bold leading-snug">{children}</span>
    </div>
  );
}
