import { Select } from '@base-ui/react/select';
import { CaretDown, Check } from '@phosphor-icons/react';
import type { ReactNode } from 'react';
import { ActionButton } from './Button';

export function RoleButton({
  active,
  icon,
  onClick,
  subtitle,
  title,
}: {
  active: boolean;
  icon: ReactNode;
  onClick: () => void;
  subtitle: string;
  title: string;
}) {
  return (
    <button
      type="button"
      aria-pressed={active}
      className={`flex min-h-20 items-center gap-3 rounded-lg border p-4 text-left transition focus:outline-none focus-visible:ring-4 focus-visible:ring-blue-300/25 ${
        active
          ? 'border-red-400 bg-red-500/18 text-white shadow-[0_0_28px_rgba(239,68,68,0.22)]'
          : 'border-slate-600 bg-slate-950/35 text-slate-300 hover:border-blue-400/70 hover:bg-blue-500/10'
      }`}
      onClick={onClick}
    >
      <span className={active ? 'text-yellow-300' : 'text-blue-300'}>
        {icon}
      </span>
      <span className="grid min-w-0 gap-1">
        <span className="text-xl font-black leading-tight">{title}</span>
        <span className="text-sm font-semibold leading-tight text-slate-400">
          {subtitle}
        </span>
      </span>
    </button>
  );
}

export function TextField({
  label,
  maxLength,
  onChange,
  placeholder,
  value,
}: {
  label: string;
  value: string;
  maxLength?: number;
  placeholder?: string;
  onChange: (value: string) => void;
}) {
  return (
    <label className="grid gap-1.5 text-sm font-black text-slate-300">
      {label}
      <input
        className="min-h-11 rounded-md border border-slate-600 bg-slate-950/60 px-3 py-2 font-semibold text-slate-100 outline-none transition placeholder:text-slate-600 hover:border-slate-500 focus:border-blue-400 focus:ring-4 focus:ring-blue-400/15"
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
  onBrowse,
  value,
}: {
  label: string;
  value: string;
  onBrowse: () => void;
}) {
  return (
    <label className="grid gap-1.5 text-sm font-black text-slate-300">
      {label}
      <div className="grid grid-cols-[minmax(0,1fr)_auto] gap-2">
        <input
          className="min-h-11 rounded-md border border-slate-600 bg-slate-950/60 px-3 py-2 font-semibold text-slate-100 outline-none transition placeholder:text-slate-600 focus:border-blue-400 focus:ring-4 focus:ring-blue-400/15"
          value={value}
          placeholder="未選択"
          readOnly
          title={value}
        />
        <ActionButton kind="outline" type="button" onClick={onBrowse}>
          参照
        </ActionButton>
      </div>
    </label>
  );
}

export function NumberField({
  label,
  max,
  min,
  onChange,
  value,
}: {
  label: string;
  value: number;
  min: number;
  max: number;
  onChange: (value: number) => void;
}) {
  return (
    <label className="grid gap-1.5 text-sm font-black text-slate-300">
      {label}
      <input
        className="min-h-11 rounded-md border border-slate-600 bg-slate-950/60 px-3 py-2 font-semibold text-slate-100 outline-none transition focus:border-blue-400 focus:ring-4 focus:ring-blue-400/15"
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
  icon,
  label,
  onChange,
  options,
  value,
}: {
  icon?: ReactNode;
  label: string;
  value: string;
  options: Array<{ value: string; label: string }>;
  onChange: (value: string) => void;
}) {
  return (
    <div className="grid gap-1.5 text-sm font-black text-slate-300">
      <span>{label}</span>
      <Select.Root
        items={options}
        value={value}
        onValueChange={(nextValue) => {
          if (nextValue !== null) {
            onChange(String(nextValue));
          }
        }}
      >
        <Select.Trigger className="flex min-h-11 w-full items-center justify-between gap-3 rounded-md border border-slate-600 bg-slate-950/60 px-3 py-2 text-left font-semibold text-slate-100 outline-none transition hover:border-slate-500 focus-visible:border-blue-400 focus-visible:ring-4 focus-visible:ring-blue-400/15">
          <span className="flex min-w-0 items-center gap-2">
            {icon ? <span className="text-blue-300">{icon}</span> : null}
            <Select.Value />
          </span>
          <Select.Icon>
            <CaretDown size={18} weight="bold" />
          </Select.Icon>
        </Select.Trigger>
        <Select.Portal>
          <Select.Positioner alignItemWithTrigger={false} sideOffset={8}>
            <Select.Popup className="z-50 min-w-[var(--anchor-width)] rounded-lg border border-slate-600 bg-slate-950 p-1 text-slate-100 shadow-2xl shadow-black/45">
              {options.map((option) => (
                <Select.Item
                  key={option.value}
                  className="grid min-h-10 cursor-default grid-cols-[1fr_auto] items-center gap-3 rounded-md px-3 text-sm font-bold outline-none data-[highlighted]:bg-blue-500/25 data-[selected]:text-blue-200"
                  value={option.value}
                >
                  <Select.ItemText>{option.label}</Select.ItemText>
                  <Select.ItemIndicator>
                    <Check size={16} weight="bold" />
                  </Select.ItemIndicator>
                </Select.Item>
              ))}
            </Select.Popup>
          </Select.Positioner>
        </Select.Portal>
      </Select.Root>
    </div>
  );
}
