import { defineRecipe } from '@pandacss/dev'

export const button = defineRecipe({
  className: 'kiso-button',
  jsx: ['Button', 'Pagination'],
  base: {
    alignItems: 'center',
    appearance: 'none',
    borderRadius: 'l2',
    cursor: 'pointer',
    display: 'inline-flex',
    flexShrink: '0',
    fontWeight: 'semibold',
    isolation: 'isolate',
    justifyContent: 'center',
    outline: '0',
    position: 'relative',
    transitionDuration: 'fast',
    transitionProperty: 'background-color, border-color, color, box-shadow',
    userSelect: 'none',
    verticalAlign: 'middle',
    whiteSpace: 'nowrap',
    focusVisibleRing: 'outside',
    _icon: { flexShrink: '0' },
    _disabled: { layerStyle: 'disabled' },
    // Loading without loadingText: the label keeps the width, the spinner sits on top.
    '& [data-slot=loader]': {
      position: 'absolute',
      inset: '0',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
    },
    '& [data-slot=label]': {
      display: 'inline-flex',
      alignItems: 'center',
      gap: 'inherit',
      opacity: '0',
    },
  },
  defaultVariants: { variant: 'solid', size: 'md' },
  variants: {
    // Hover outranks a bare state selector, so each pressed look repeats itself under _hover.
    variant: {
      solid: {
        bg: 'colorPalette.solid.bg',
        color: 'colorPalette.solid.fg',
        _hover: { bg: 'colorPalette.solid.bg.hover' },
      },
      surface: {
        bg: 'colorPalette.surface.bg',
        borderWidth: '1px',
        borderColor: 'colorPalette.surface.border',
        color: 'colorPalette.surface.fg',
        _hover: { borderColor: 'colorPalette.surface.border.hover' },
        _active: { bg: 'colorPalette.surface.bg.active' },
        _pressed: { bg: 'colorPalette.surface.bg.active' },
      },
      subtle: {
        bg: 'colorPalette.subtle.bg',
        color: 'colorPalette.subtle.fg',
        _hover: { bg: 'colorPalette.subtle.bg.hover' },
        _active: { bg: 'colorPalette.subtle.bg.active' },
        _pressed: {
          bg: 'colorPalette.subtle.bg.active',
          _hover: { bg: 'colorPalette.subtle.bg.active' },
        },
      },
      outline: {
        borderWidth: '1px',
        borderColor: 'colorPalette.outline.border',
        color: 'colorPalette.outline.fg',
        _hover: { bg: 'colorPalette.outline.bg.hover' },
        _active: { bg: 'colorPalette.outline.bg.active' },
        _pressed: {
          bg: 'colorPalette.outline.bg.active',
          _hover: { bg: 'colorPalette.outline.bg.active' },
        },
      },
      plain: {
        color: 'colorPalette.plain.fg',
        _hover: { bg: 'colorPalette.plain.bg.hover' },
        _active: { bg: 'colorPalette.plain.bg.active' },
        _pressed: {
          bg: 'colorPalette.plain.bg.active',
          _hover: { bg: 'colorPalette.plain.bg.active' },
        },
      },
    },
    /** Icon-only: equal width and height. */
    square: { true: { aspectRatio: '1', px: '0' } },
    size: {
      '2xs': { h: '6', minW: '6', textStyle: 'xs', px: '2', gap: '1', _icon: { boxSize: '3.5' } },
      xs: { h: '8', minW: '8', textStyle: 'sm', px: '2.5', gap: '1', _icon: { boxSize: '4' } },
      sm: { h: '9', minW: '9', textStyle: 'sm', px: '3', gap: '2', _icon: { boxSize: '4' } },
      md: { h: '10', minW: '10', textStyle: 'sm', px: '3.5', gap: '2', _icon: { boxSize: '5' } },
      lg: { h: '11', minW: '11', textStyle: 'md', px: '4', gap: '2', _icon: { boxSize: '5' } },
      xl: {
        h: '12',
        minW: '12',
        textStyle: 'md',
        px: '4.5',
        gap: '2.5',
        _icon: { boxSize: '5.5' },
      },
      '2xl': { h: '16', minW: '16', textStyle: 'lg', px: '6', gap: '3', _icon: { boxSize: '6' } },
    },
  },
})
