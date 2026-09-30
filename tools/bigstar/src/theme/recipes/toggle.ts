import { defineRecipe } from '@pandacss/dev'

// Pressed state uses the palette's subtle role, so colorPalette tints the "on" state.
export const toggle = defineRecipe({
  className: 'kiso-toggle',
  jsx: ['Toggle'],
  base: {
    display: 'inline-flex',
    alignItems: 'center',
    justifyContent: 'center',
    flexShrink: '0',
    borderRadius: 'l2',
    color: 'fg.muted',
    fontWeight: 'medium',
    whiteSpace: 'nowrap',
    cursor: 'pointer',
    userSelect: 'none',
    outline: '0',
    transitionProperty: 'background-color, border-color, color',
    transitionDuration: 'fast',
    focusVisibleRing: 'outside',
    _hover: { bg: 'gray.plain.bg.hover', color: 'fg.default' },
    _pressed: {
      bg: 'colorPalette.subtle.bg',
      color: 'colorPalette.subtle.fg',
      // Hover outranks a bare state selector, so the pressed look repeats its own hover.
      _hover: { bg: 'colorPalette.subtle.bg.hover', color: 'colorPalette.subtle.fg' },
    },
    _disabled: { layerStyle: 'disabled' },
  },
  defaultVariants: { variant: 'plain', size: 'md' },
  variants: {
    variant: {
      plain: {},
      outline: { borderWidth: '1px', borderColor: 'gray.outline.border' },
    },
    size: {
      xs: { h: '8', minW: '8', px: '2', gap: '1', textStyle: 'sm', _icon: { boxSize: '4' } },
      sm: { h: '9', minW: '9', px: '2.5', gap: '2', textStyle: 'sm', _icon: { boxSize: '4' } },
      md: { h: '10', minW: '10', px: '3', gap: '2', textStyle: 'sm', _icon: { boxSize: '5' } },
      lg: { h: '11', minW: '11', px: '3.5', gap: '2', textStyle: 'md', _icon: { boxSize: '5' } },
      xl: { h: '12', minW: '12', px: '4', gap: '2.5', textStyle: 'md', _icon: { boxSize: '5.5' } },
      '2xl': { h: '16', minW: '16', px: '5', gap: '3', textStyle: 'lg', _icon: { boxSize: '6' } },
    },
  },
})

export const toggleGroup = defineRecipe({
  className: 'kiso-toggle-group',
  jsx: ['ToggleGroup'],
  base: {
    display: 'inline-flex',
    gap: '1',
    _vertical: { flexDirection: 'column' },
  },
  defaultVariants: { variant: 'plain' },
  variants: {
    variant: {
      plain: {},
      outline: { borderWidth: '1px', borderRadius: 'l3', p: '1' },
    },
  },
})
