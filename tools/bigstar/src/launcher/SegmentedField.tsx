import { useId } from 'react';
import { css } from 'styled-system/css';
import { Toggle, ToggleGroup } from '@/components/ui/toggle';
import type { Lives } from '../bindings';
import { bigStarsOptions, livesOptions, winsOptions } from './options';

export const fieldLabelClass = css({ fontWeight: 'semibold', textStyle: 'sm' });

/** 選択肢が少ない設定は、押したものが反転するボタンの並びで選ぶ */
export function SegmentedField({
  disabled = false,
  label,
  onChange,
  options,
  value,
}: {
  disabled?: boolean;
  label: string;
  onChange: (value: string) => void;
  options: { value: string; label: string }[];
  value: string;
}) {
  const labelId = useId();
  return (
    <div
      className={css({ display: 'flex', flexDirection: 'column', gap: '2' })}
    >
      <span className={fieldLabelClass} id={labelId}>
        {label}
      </span>
      <ToggleGroup
        aria-labelledby={labelId}
        colorPalette="gray"
        disabled={disabled}
        fitted
        value={[value]}
        variant="enclosed"
        onValueChange={(next) => {
          // 押し直しで選択が外れないように、空の選択は無視する
          const [picked] = next;
          if (picked !== undefined) onChange(picked);
        }}
      >
        {options.map((option) => (
          <Toggle
            key={option.value}
            pressedVariant="solid"
            size="xs"
            value={option.value}
          >
            {option.label}
          </Toggle>
        ))}
      </ToggleGroup>
    </div>
  );
}

export type MatchRules = { bigStars: number; lives: Lives; wins: number };

/** 先取数・ビッグスター・残機の 3 つを横に並べる。部屋作成と CPU 対戦で同じ並びにする */
export function MatchRuleFields({
  disabled = false,
  onChange,
  value,
}: {
  disabled?: boolean;
  onChange: (patch: Partial<MatchRules>) => void;
  value: MatchRules;
}) {
  return (
    <div
      className={css({
        display: 'grid',
        gap: '3.5',
        gridTemplateColumns: 'repeat(3, minmax(0, 1fr))',
      })}
    >
      <SegmentedField
        disabled={disabled}
        label="先取数"
        options={winsOptions}
        value={String(value.wins)}
        onChange={(next) => onChange({ wins: Number(next) })}
      />
      <SegmentedField
        disabled={disabled}
        label="ビッグスター"
        options={bigStarsOptions}
        value={String(value.bigStars)}
        onChange={(next) => onChange({ bigStars: Number(next) })}
      />
      <SegmentedField
        disabled={disabled}
        label="残機"
        options={livesOptions}
        value={value.lives}
        onChange={(next) => onChange({ lives: next as Lives })}
      />
    </div>
  );
}
