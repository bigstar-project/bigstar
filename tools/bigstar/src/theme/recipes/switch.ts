import { defineSlotRecipe } from '@pandacss/dev'
import { choiceLabel, choiceLabelSizes } from '../shared'

// Wrap Root and its text in Label.

export const switchRecipe = defineSlotRecipe({
  className: 'kiso-switch',
  jsx: ['Switch', /^Switch\./],
  slots: ['root', 'thumb', 'label'],
  base: {
    root: {
      '--switch-diff': 'calc(var(--switch-width) - var(--switch-height))',
      display: 'inline-flex',
      alignItems: 'center',
      flexShrink: '0',
      position: 'relative',
      verticalAlign: 'middle',
      width: 'var(--switch-width)',
      height: 'var(--switch-height)',
      borderRadius: 'full',
      cursor: 'pointer',
      transitionProperty: 'background-color, box-shadow',
      transitionDuration: 'fast',
      focusVisibleRing: 'outside',
      _disabled: { layerStyle: 'disabled' },
      _invalid: { outline: '2px solid', outlineColor: 'error', outlineOffset: '2px' },
    },
    thumb: {
      display: 'block',
      flexShrink: '0',
      width: 'var(--switch-height)',
      height: 'var(--switch-height)',
      borderRadius: 'full',
      transitionProperty: 'translate, background-color',
      transitionDuration: 'fast',
      _checked: { translate: 'var(--switch-diff) 0' },
      '&:dir(rtl)[data-checked]': { translate: 'calc(var(--switch-diff) * -1) 0' },
    },
    label: choiceLabel,
  },
  defaultVariants: { variant: 'solid', size: 'md' },
  variants: {
    variant: {
      solid: {
        root: {
          // An inset ring keeps the off state visible on any surface.
          bg: 'gray.a5',
          boxShadow: 'inset 0 0 0 1px {colors.gray.a6}',
          _checked: { bg: 'colorPalette.solid.bg', boxShadow: 'none' },
        },
        thumb: {
          bg: 'white',
          scale: '0.8',
          boxShadow: 'xs',
          _checked: { bg: 'colorPalette.solid.fg' },
        },
      },
    },
    size: {
      xs: {
        root: { '--switch-width': 'sizes.8', '--switch-height': 'sizes.4' },
        label: choiceLabelSizes.xs,
      },
      sm: {
        root: { '--switch-width': 'sizes.9', '--switch-height': 'sizes.4.5' },
        label: choiceLabelSizes.sm,
      },
      md: {
        root: { '--switch-width': 'sizes.10', '--switch-height': 'sizes.5' },
        label: choiceLabelSizes.md,
      },
      lg: {
        root: { '--switch-width': 'sizes.11', '--switch-height': 'sizes.5.5' },
        label: choiceLabelSizes.lg,
      },
      xl: {
        root: { '--switch-width': 'sizes.12', '--switch-height': 'sizes.6' },
        label: choiceLabelSizes.xl,
      },
      '2xl': {
        root: { '--switch-width': 'sizes.16', '--switch-height': 'sizes.8' },
        label: choiceLabelSizes['2xl'],
      },
    },
  },
})
