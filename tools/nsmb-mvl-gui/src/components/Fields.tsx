import { Portal } from '@ark-ui/react';
import type { ReactNode } from 'react';
import { css, cx } from 'styled-system/css';
import { Button, Field, Input, Select } from './ui';

export function RoleButton({
  active,
  icon,
  onClick,
  subtitle,
  title,
  tone,
}: {
  active: boolean;
  icon: ReactNode;
  onClick: () => void;
  subtitle: string;
  title: string;
  tone: 'green' | 'red';
}) {
  return (
    <button
      type="button"
      aria-pressed={active}
      className={cx(
        css({
          alignItems: 'center',
          borderRadius: 'l2',
          borderWidth: '1px',
          display: 'flex',
          focusVisibleRing: 'outside',
          gap: '2.5',
          minH: '14',
          p: '3',
          textAlign: 'left',
          transition: 'common',
          cursor: 'pointer',
        }),
        css(
          tone === 'red'
            ? active
              ? {
                  bg: 'red.subtle.bg',
                  borderColor: 'red.outline.border',
                  color: 'fg.default',
                }
              : {
                  bg: 'gray.surface.bg',
                  borderColor: 'gray.surface.border',
                  color: 'fg.muted',
                  _hover: {
                    bg: 'red.subtle.bg',
                    borderColor: 'red.outline.border',
                    color: 'fg.default',
                  },
                }
            : active
              ? {
                  bg: 'green.subtle.bg',
                  borderColor: 'green.outline.border',
                  color: 'fg.default',
                }
              : {
                  bg: 'gray.surface.bg',
                  borderColor: 'gray.surface.border',
                  color: 'fg.muted',
                  _hover: {
                    bg: 'green.subtle.bg',
                    borderColor: 'green.outline.border',
                    color: 'fg.default',
                  },
                },
        ),
      )}
      onClick={onClick}
    >
      <span
        className={css({
          color: active
            ? tone === 'red'
              ? 'red.plain.fg'
              : 'green.plain.fg'
            : 'fg.muted',
        })}
      >
        {icon}
      </span>
      <span
        className={css({
          display: 'grid',
          gap: '1',
          minW: '0',
        })}
      >
        <span
          className={css({
            fontWeight: 'black',
            lineHeight: 'tight',
            textStyle: 'md',
          })}
        >
          {title}
        </span>
        <span
          className={css({
            color: 'fg.muted',
            fontWeight: 'semibold',
            lineHeight: 'tight',
            textStyle: 'xs',
          })}
        >
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
    <Field.Root
      className={css({
        display: 'grid',
        gap: '1',
        minW: '0',
      })}
    >
      <Field.Label
        className={css({
          color: 'fg.muted',
          fontWeight: 'black',
          textStyle: 'xs',
        })}
      >
        {label}
      </Field.Label>
      <Input
        variant="outline"
        className={css({
          color: 'fg.default',
          fontWeight: 'semibold',
        })}
        value={value}
        maxLength={maxLength}
        placeholder={placeholder}
        autoComplete="off"
        onChange={(event) => onChange(event.target.value)}
      />
    </Field.Root>
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
    <Field.Root
      className={css({
        display: 'grid',
        gap: '1',
        minW: '0',
      })}
    >
      <Field.Label
        className={css({
          color: 'fg.muted',
          fontWeight: 'black',
          textStyle: 'xs',
        })}
      >
        {label}
      </Field.Label>
      <div
        className={css({
          display: 'grid',
          gap: '1.5',
          gridTemplateColumns: 'minmax(0, 1fr) auto',
        })}
      >
        <Input
          variant="outline"
          className={css({
            color: 'fg.default',
            fontWeight: 'semibold',
          })}
          value={value}
          placeholder="未選択"
          readOnly
          title={value}
        />
        <Button variant="outline" onClick={onBrowse}>
          参照
        </Button>
      </div>
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
    <Field.Root
      className={css({
        display: 'grid',
        gap: '1',
        minW: '0',
      })}
    >
      <Field.Label
        className={css({
          color: 'fg.muted',
          fontWeight: 'black',
          textStyle: 'xs',
        })}
      >
        {label}
      </Field.Label>
      <Input
        variant="outline"
        className={css({
          color: 'fg.default',
          fontWeight: 'semibold',
        })}
        type="number"
        min={min}
        max={max}
        value={value}
        onChange={(event) => onChange(Number(event.target.value))}
      />
    </Field.Root>
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
  const collection = Select.createListCollection({ items: options });
  const menu = (
    <Select.Positioner>
      <Select.Content>
        {options.map((option) => (
          <Select.Item key={option.value} item={option}>
            <Select.ItemText>{option.label}</Select.ItemText>
            <Select.ItemIndicator />
          </Select.Item>
        ))}
      </Select.Content>
    </Select.Positioner>
  );

  return (
    <div
      className={css({
        display: 'grid',
        gap: '1',
        minW: '0',
      })}
    >
      <Select.Root
        collection={collection}
        // size="lg"
        value={[value]}
        variant="outline"
        onValueChange={(details) => {
          const nextValue = details.value[0];
          if (nextValue) {
            onChange(nextValue);
          }
        }}
      >
        <Select.Label
          className={css({
            color: 'fg.muted',
            fontWeight: 'black',
            textStyle: 'xs',
          })}
        >
          {label}
        </Select.Label>
        <Select.Control>
          <Select.Trigger>
            <span
              className={css({
                alignItems: 'center',
                display: 'flex',
                gap: '2',
                minW: '0',
              })}
            >
              {icon ? (
                <span
                  className={css({
                    color: 'blue.plain.fg',
                    flexShrink: '0',
                  })}
                >
                  {icon}
                </span>
              ) : null}
              <Select.ValueText />
            </span>
            <Select.IndicatorGroup>
              <Select.Indicator />
            </Select.IndicatorGroup>
          </Select.Trigger>
        </Select.Control>
        <Portal>{menu}</Portal>
        <Select.HiddenSelect />
      </Select.Root>
    </div>
  );
}
