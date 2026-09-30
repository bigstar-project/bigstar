import { defineSlotRecipe } from '@pandacss/dev'

// Shared by Progress and Meter.
export const progress = defineSlotRecipe({
  className: 'kiso-progress',
  jsx: ['Progress', /^Progress\./, 'Meter', /^Meter\./],
  slots: ['root', 'label', 'value', 'track', 'indicator'],
  base: {
    root: {
      display: 'grid',
      gridTemplateColumns: '1fr auto',
      alignItems: 'center',
      columnGap: '2',
      rowGap: '1.5',
      width: 'full',
      textStyle: 'sm',
    },
    label: { fontWeight: 'medium' },
    value: { textStyle: 'xs', fontWeight: 'medium', fontVariantNumeric: 'tabular-nums' },
    track: { gridColumn: '1 / -1', position: 'relative', overflow: 'hidden' },
    indicator: {
      height: 'full',
      borderRadius: 'inherit',
      transitionProperty: 'width',
      transitionDuration: 'slow',
      _indeterminate: {
        position: 'absolute',
        width: '40%',
        animation: 'kiso-indeterminate 1.2s ease-in-out infinite',
      },
    },
  },
  defaultVariants: { variant: 'solid', size: 'md', shape: 'rounded' },
  variants: {
    variant: {
      solid: { track: { bg: 'gray.subtle.bg' }, indicator: { bg: 'colorPalette.solid.bg' } },
      subtle: {
        track: { bg: 'colorPalette.subtle.bg.active' },
        indicator: { bg: 'colorPalette.solid.bg' },
      },
    },
    shape: {
      square: {},
      rounded: { track: { borderRadius: 'l1' } },
      full: { track: { borderRadius: 'full' } },
    },
    striped: {
      true: {
        indicator: {
          '--stripe-size': 'sizes.4',
          '--stripe-color': { base: 'rgba(255, 255, 255, 0.3)', _dark: 'rgba(0, 0, 0, 0.3)' },
          backgroundImage:
            'linear-gradient(45deg, var(--stripe-color) 25%, transparent 25%, transparent 50%, var(--stripe-color) 50%, var(--stripe-color) 75%, transparent 75%, transparent)',
          backgroundSize: 'var(--stripe-size) var(--stripe-size)',
        },
      },
    },
    animated: { true: { indicator: { animation: 'kiso-stripes 1s linear infinite' } } },
    size: {
      xs: { track: { h: '1.5' } },
      sm: { track: { h: '2' } },
      md: { track: { h: '2.5' } },
      lg: { track: { h: '3' } },
      xl: { track: { h: '3.5' } },
    },
  },
})
