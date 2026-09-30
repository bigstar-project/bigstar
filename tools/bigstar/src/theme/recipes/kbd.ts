import { defineRecipe } from '@pandacss/dev'

export const kbd = defineRecipe({
  className: 'kiso-kbd',
  jsx: ['Kbd'],
  base: {
    display: 'inline-flex',
    alignItems: 'center',
    justifyContent: 'center',
    flexShrink: '0',
    fontFamily: 'mono',
    fontWeight: 'medium',
    whiteSpace: 'nowrap',
    userSelect: 'none',
    borderRadius: 'l2',
    // Keys read as neutral; colorPalette on the element still tints them.
    colorPalette: 'gray',
  },
  defaultVariants: { variant: 'surface', size: 'md' },
  variants: {
    variant: {
      solid: { bg: 'colorPalette.solid.bg', color: 'colorPalette.solid.fg' },
      surface: {
        bg: 'colorPalette.surface.bg',
        borderWidth: '1px',
        borderColor: 'colorPalette.surface.border',
        color: 'colorPalette.surface.fg',
      },
      outline: {
        borderWidth: '1px',
        borderColor: 'colorPalette.outline.border',
        color: 'colorPalette.outline.fg',
      },
      subtle: { bg: 'colorPalette.subtle.bg', color: 'colorPalette.subtle.fg' },
      plain: { color: 'colorPalette.plain.fg' },
    },
    size: {
      sm: { textStyle: 'xs', h: '4.5', minW: '4.5', px: '1' },
      md: { textStyle: 'sm', h: '5', minW: '5', px: '1' },
      lg: { textStyle: 'sm', h: '5.5', minW: '5.5', px: '1' },
      xl: { textStyle: 'md', h: '6', minW: '6', px: '1.5' },
    },
  },
})
