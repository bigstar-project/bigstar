import { defineSlotRecipe } from '@pandacss/dev';

export const card = defineSlotRecipe({
  className: 'card',
  slots: ['root', 'header', 'body', 'footer', 'title', 'description'],
  base: {
    root: {
      borderRadius: 'l3',
      display: 'flex',
      flexDirection: 'column',
      overflow: 'hidden',
      position: 'relative',
    },
    header: {
      display: 'flex',
      flexDirection: 'column',
      gap: '1',
      p: '6',
    },
    body: {
      display: 'flex',
      flex: '1',
      flexDirection: 'column',
      pb: '6',
      px: '6',
    },
    footer: {
      display: 'flex',
      justifyContent: 'flex-end',
      gap: '3',
      pb: '6',
      pt: '2',
      px: '6',
    },
    title: {
      textStyle: 'lg',
      fontWeight: 'semibold',
    },
    description: {
      color: 'fg.muted',
      textStyle: 'sm',
    },
  },
  defaultVariants: {
    variant: 'outline',
  },
  variants: {
    variant: {
      elevated: {
        root: {
          bg: 'gray.surface.bg',
          boxShadow: 'lg',
        },
      },
      outline: {
        root: {
          bg: 'gray.surface.bg',
          borderColor: 'gray.surface.border',
          borderWidth: '1px',
        },
      },
      glass: {
        root: {
          bg: 'app.card',
          backdropFilter: 'auto',
          backdropBlur: 'md',
          backdropSaturate: '180%',
          borderColor: 'gray.surface.border',
          borderWidth: '1px',
          boxShadow: 'panel',
        },
      },
      subtle: {
        root: {
          bg: 'gray.subtle.bg',
        },
      },
    },
  },
});
