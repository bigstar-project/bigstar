import type { StatusKind } from '../types';

export function StatusPill({
  children,
  kind,
}: {
  children: string;
  kind: StatusKind;
}) {
  const colors: Record<StatusKind, string> = {
    idle: 'border-slate-300 bg-white text-slate-600',
    ok: 'border-emerald-300 bg-emerald-50 text-emerald-900',
    warn: 'border-amber-300 bg-amber-50 text-amber-900',
    error:
      'border-rose-500 bg-rose-50 text-rose-950 shadow-[0_0_0_1px_rgba(244,63,94,0.35)]',
  };
  const label: Record<StatusKind, string> = {
    idle: '待機',
    ok: '正常',
    warn: '注意',
    error: 'エラー',
  };
  return (
    <div
      className={`grid min-h-12 max-w-[58ch] gap-0.5 overflow-wrap-anywhere rounded-lg border px-3 py-2 ${colors[kind]}`}
    >
      <span className="text-[11px] font-black uppercase tracking-normal">
        {label[kind]}
      </span>
      <span className="text-sm font-bold leading-snug">{children}</span>
    </div>
  );
}
