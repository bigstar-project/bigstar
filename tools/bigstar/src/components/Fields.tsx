import { css } from 'styled-system/css';
import * as Field from '@/components/ui/field';
import { Input } from '@/components/ui/input';
import * as NumberFieldParts from '@/components/ui/number-field';
import * as Select from '@/components/ui/select';

// ラベルを上、入力欄を下に置く。ひとり検証の設定欄で使う
const fieldClass = css({
  display: 'flex',
  flexDirection: 'column',
  gap: '1.5',
  minW: '0',
});

const labelClass = css({ color: 'fg.muted', textStyle: 'xs' });

export function TextField({
  label,
  onChange,
  value,
}: {
  label: string;
  value: string;
  onChange: (value: string) => void;
}) {
  return (
    <Field.Root className={fieldClass}>
      <Field.Label className={labelClass}>{label}</Field.Label>
      <Input
        autoComplete="off"
        size="sm"
        value={value}
        onChange={(event) => onChange(event.target.value)}
      />
    </Field.Root>
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
    <Field.Root className={fieldClass}>
      <Field.Label className={labelClass}>{label}</Field.Label>
      <NumberFieldParts.Root
        format={{ useGrouping: false }}
        max={max}
        min={min}
        size="sm"
        value={value}
        onValueChange={(next) => {
          if (next !== null) onChange(next);
        }}
      >
        <NumberFieldParts.Group>
          <NumberFieldParts.Input
            className={css({ px: '3', textAlign: 'start' })}
          />
        </NumberFieldParts.Group>
      </NumberFieldParts.Root>
    </Field.Root>
  );
}

export function SelectField({
  disabled,
  label,
  onChange,
  options,
  value,
}: {
  disabled?: boolean;
  label: string;
  value: string;
  options: Array<{ value: string; label: string }>;
  onChange: (value: string) => void;
}) {
  return (
    // Select.Root は要素を出さないので、ラベルと選択欄を縦に並べる箱を置く
    <div className={fieldClass}>
      <Select.Root
        disabled={disabled}
        items={options}
        size="sm"
        value={value}
        onValueChange={(next) => {
          if (next !== null) onChange(next);
        }}
      >
        <Select.Label className={labelClass}>{label}</Select.Label>
        <Select.Trigger className={css({ minW: '0', w: 'full' })}>
          <Select.Value />
          <Select.Icon />
        </Select.Trigger>
        <Select.Portal>
          <Select.Positioner alignItemWithTrigger={false} sideOffset={4}>
            <Select.Popup>
              <Select.List>
                {options.map((option) => (
                  <Select.Item key={option.value} value={option.value}>
                    <Select.ItemText>{option.label}</Select.ItemText>
                    <Select.ItemIndicator />
                  </Select.Item>
                ))}
              </Select.List>
            </Select.Popup>
          </Select.Positioner>
        </Select.Portal>
      </Select.Root>
    </div>
  );
}
