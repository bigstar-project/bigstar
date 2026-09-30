import { defineSlotRecipe } from '@pandacss/dev'

// status picks the palette (as in Park UI); colorPalette can still override it.
export const alert = defineSlotRecipe({
  className: 'kiso-alert',
  jsx: ['Alert', /^Alert\./],
  slots: ['root', 'icon', 'content', 'title', 'description'],
  base: {
    root: {
      display: 'flex',
      alignItems: 'flex-start',
      position: 'relative',
      width: 'full',
      borderRadius: 'l3',
    },
    icon: {
      display: 'inline-flex',
      flexShrink: '0',
      alignItems: 'center',
      justifyContent: 'center',
    },
    content: { display: 'flex', flexDirection: 'column', flex: '1', gap: '1', minWidth: '0' },
    title: { fontWeight: 'semibold' },
    // App change: the title carries the status color; the explanation stays readable in fg.muted.
    description: { display: 'inline', color: 'fg.muted' },
  },
  defaultVariants: { status: 'info', variant: 'subtle', size: 'md' },
  variants: {
    status: {
      info: { root: { colorPalette: 'info' } },
      success: { root: { colorPalette: 'success' } },
      warning: { root: { colorPalette: 'warning' } },
      error: { root: { colorPalette: 'danger' } },
      neutral: { root: { colorPalette: 'gray' } },
    },
    variant: {
      solid: {
        root: { bg: 'colorPalette.solid.bg', color: 'colorPalette.solid.fg' },
        description: { color: 'inherit' },
      },
      surface: {
        root: {
          bg: 'colorPalette.surface.bg',
          borderWidth: '1px',
          borderColor: 'colorPalette.surface.border',
          color: 'colorPalette.surface.fg',
        },
      },
      subtle: { root: { bg: 'colorPalette.subtle.bg', color: 'colorPalette.subtle.fg' } },
      outline: {
        root: {
          borderWidth: '1px',
          borderColor: 'colorPalette.outline.border',
          color: 'colorPalette.outline.fg',
        },
      },
    },
    size: {
      md: { root: { gap: '3', p: '4', textStyle: 'sm' }, icon: { _icon: { boxSize: '5' } } },
      lg: { root: { gap: '4', p: '4', textStyle: 'md' }, icon: { _icon: { boxSize: '6' } } },
    },
  },
})
