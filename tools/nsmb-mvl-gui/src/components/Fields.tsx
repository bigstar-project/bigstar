import type { ReactNode } from 'react';

export function RoleButton({
  active,
  children,
  onClick,
}: {
  active: boolean;
  children: string;
  onClick: () => void;
}) {
  return (
    <button
      type="button"
      aria-pressed={active}
      className={`min-h-9 rounded-md px-3 font-semibold transition ${
        active
          ? 'bg-white text-slate-950 shadow-sm'
          : 'text-slate-600 hover:bg-slate-50'
      }`}
      onClick={onClick}
    >
      {children}
    </button>
  );
}

export function TextField({
  label,
  value,
  maxLength,
  placeholder,
  onChange,
}: {
  label: string;
  value: string;
  maxLength?: number;
  placeholder?: string;
  onChange: (value: string) => void;
}) {
  return (
    <label className="grid gap-1.5 text-sm font-bold text-slate-700">
      {label}
      <input
        className="min-h-10 rounded-md border border-slate-300 bg-white px-3 py-2 font-normal text-slate-950 outline-none focus:border-blue-600 focus:ring-2 focus:ring-blue-100"
        value={value}
        maxLength={maxLength}
        placeholder={placeholder}
        autoComplete="off"
        onChange={(event) => onChange(event.target.value)}
      />
    </label>
  );
}

export function FilePathField({
  label,
  value,
  onBrowse,
}: {
  label: string;
  value: string;
  onBrowse: () => void;
}) {
  return (
    <label className="grid gap-1.5 text-sm font-bold text-slate-700">
      {label}
      <div className="grid grid-cols-[minmax(0,1fr)_auto] gap-2">
        <input
          className="min-h-10 rounded-md border border-slate-300 bg-slate-50 px-3 py-2 font-normal text-slate-950 outline-none focus:border-blue-600 focus:ring-2 focus:ring-blue-100"
          value={value}
          placeholder="未選択"
          readOnly
          title={value}
        />
        <ActionButton kind="secondary" type="button" onClick={onBrowse}>
          参照
        </ActionButton>
      </div>
    </label>
  );
}

export function NumberField({
  label,
  value,
  min,
  max,
  onChange,
}: {
  label: string;
  value: number;
  min: number;
  max: number;
  onChange: (value: number) => void;
}) {
  return (
    <label className="grid gap-1.5 text-sm font-bold text-slate-700">
      {label}
      <input
        className="min-h-10 rounded-md border border-slate-300 bg-white px-3 py-2 font-normal text-slate-950 outline-none focus:border-blue-600 focus:ring-2 focus:ring-blue-100"
        type="number"
        min={min}
        max={max}
        value={value}
        onChange={(event) => onChange(Number(event.target.value))}
      />
    </label>
  );
}

export function SelectField({
  label,
  value,
  children,
  onChange,
}: {
  label: string;
  value: string;
  children: ReactNode;
  onChange: (value: string) => void;
}) {
  return (
    <label className="grid gap-1.5 text-sm font-bold text-slate-700">
      {label}
      <select
        className="min-h-10 rounded-md border border-slate-300 bg-white px-3 py-2 font-normal text-slate-950 outline-none focus:border-blue-600 focus:ring-2 focus:ring-blue-100"
        value={value}
        onChange={(event) => onChange(event.target.value)}
      >
        {children}
      </select>
    </label>
  );
}

export function ActionButton({
  children,
  kind,
  type,
  onClick,
  disabled = false,
}: {
  children: string;
  kind: 'primary' | 'secondary';
  type: 'button' | 'submit';
  onClick?: () => void;
  disabled?: boolean;
}) {
  const styles =
    kind === 'primary'
      ? 'border-blue-700 bg-blue-600 text-white hover:bg-blue-700'
      : 'border-slate-300 bg-white text-slate-800 hover:bg-slate-50';
  return (
    <button
      type={type}
      className={`min-h-10 min-w-24 rounded-md border px-4 font-bold transition disabled:cursor-not-allowed disabled:opacity-50 ${styles}`}
      disabled={disabled}
      onClick={onClick}
    >
      {children}
    </button>
  );
}
