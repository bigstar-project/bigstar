import type { ReactNode } from 'react';

export function ActionButton({
  children,
  className = '',
  disabled = false,
  icon,
  kind,
  onClick,
  type,
}: {
  children: ReactNode;
  kind: 'primary' | 'outline' | 'ghost' | 'danger';
  type: 'button' | 'submit';
  className?: string;
  icon?: ReactNode;
  onClick?: () => void;
  disabled?: boolean;
}) {
  const styles = {
    primary:
      'border-yellow-300 bg-yellow-400 text-slate-950 hover:bg-yellow-300 shadow-[0_0_22px_rgba(250,204,21,0.22)]',
    outline:
      'border-blue-400/70 bg-blue-500/10 text-blue-100 hover:bg-blue-500/18 hover:border-blue-300',
    ghost:
      'border-slate-600 bg-slate-950/35 text-slate-200 hover:border-slate-500 hover:bg-slate-800/65',
    danger: 'border-red-400/70 bg-red-500/10 text-red-100 hover:bg-red-500/18',
  };
  return (
    <button
      type={type}
      className={`inline-flex min-h-11 min-w-24 items-center justify-center gap-2 rounded-md border px-4 font-black transition focus:outline-none focus-visible:ring-4 focus-visible:ring-blue-300/20 disabled:cursor-not-allowed disabled:opacity-50 ${styles[kind]} ${className}`}
      disabled={disabled}
      onClick={onClick}
    >
      {icon}
      {children}
    </button>
  );
}
