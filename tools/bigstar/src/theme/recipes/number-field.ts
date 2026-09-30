import { defineSlotRecipe } from '@pandacss/dev'
import { fieldGroupVariants } from '../shared'

const stepper = {
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  flexShrink: '0',
  width: 'var(--number-field-height)',
  color: 'fg.muted',
  cursor: 'pointer',
  userSelect: 'none',
  transitionProperty: 'background-color, color',
  transitionDuration: 'fast',
  focusVisibleRing: 'inside',
  _hover: { bg: 'gray.surface.bg.hover', color: 'fg.default' },
  _active: { bg: 'gray.surface.bg.active' },
  // A stepper alone is disabled at min / max: mute it without opacity, so a disabled group
  // (which fades once, below) never fades its steppers twice.
  _disabled: { cursor: 'not-allowed', color: 'gray.a7' },
  _icon: { boxSize: '1em' },
} as const

export const numberField = defineSlotRecipe({
  className: 'kiso-number',
  jsx: ['NumberField', /^NumberField\./],
  slots: ['root', 'group', 'input', 'increment', 'decrement', 'scrubArea', 'scrubAreaCursor'],
  base: {
    root: { display: 'flex', flexDirection: 'column', gap: '1.5', width: 'full' },
    group: {
      display: 'flex',
      height: 'var(--number-field-height)',
      borderRadius: 'l2',
      overflow: 'hidden',
      color: 'fg.default',
      // The whole control fades here, once. Base UI marks the group disabled with the field.
      _disabled: { layerStyle: 'disabled' },
    },
    input: {
      flex: '1',
      minWidth: '0',
      bg: 'transparent',
      outline: '0',
      textAlign: 'center',
      fontVariantNumeric: 'tabular-nums',
    },
    increment: { ...stepper, borderInlineStartWidth: '1px' },
    decrement: { ...stepper, borderInlineEndWidth: '1px' },
    scrubArea: { cursor: 'ew-resize', textStyle: 'label' },
  },
  defaultVariants: { variant: 'outline', size: 'md' },
  variants: {
    variant: {
      outline: { group: fieldGroupVariants.outline },
      surface: { group: fieldGroupVariants.surface },
      subtle: { group: fieldGroupVariants.subtle },
    },
    size: {
      xs: { root: { '--number-field-height': 'sizes.8' }, group: { textStyle: 'sm' } },
      sm: { root: { '--number-field-height': 'sizes.9' }, group: { textStyle: 'sm' } },
      md: { root: { '--number-field-height': 'sizes.10' }, group: { textStyle: 'md' } },
      lg: { root: { '--number-field-height': 'sizes.11' }, group: { textStyle: 'md' } },
      xl: { root: { '--number-field-height': 'sizes.12' }, group: { textStyle: 'lg' } },
      '2xl': { root: { '--number-field-height': 'sizes.16' }, group: { textStyle: '3xl' } },
    },
  },
})
