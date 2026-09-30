import { defineSlotRecipe } from '@pandacss/dev'

export const card = defineSlotRecipe({
  className: 'kiso-card',
  jsx: ['Card', /^Card\./],
  slots: ['root', 'header', 'title', 'description', 'body', 'footer'],
  base: {
    root: {
      display: 'flex',
      flexDirection: 'column',
      position: 'relative',
      overflow: 'hidden',
      borderRadius: 'l3',
      color: 'fg.default',
    },
    header: { display: 'flex', flexDirection: 'column', gap: '1', p: 'var(--card-padding)' },
    body: {
      display: 'flex',
      flexDirection: 'column',
      flex: '1',
      px: 'var(--card-padding)',
      pb: 'var(--card-padding)',
      // Without a header, the body opens the card.
      '&:first-child': { pt: 'var(--card-padding)' },
    },
    footer: {
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'flex-end',
      gap: '3',
      px: 'var(--card-padding)',
      pb: 'var(--card-padding)',
      pt: '2',
    },
    title: { fontWeight: 'semibold' },
    description: { color: 'fg.muted', textStyle: 'sm' },
  },
  defaultVariants: { variant: 'outline', size: 'md' },
  variants: {
    variant: {
      elevated: { root: { bg: 'gray.surface.bg', boxShadow: 'lg' } },
      outline: { root: { bg: 'gray.surface.bg', borderWidth: '1px' } },
      subtle: { root: { bg: 'gray.subtle.bg' } },
    },
    // Park UI has one spacing (md); sm and lg are Kiso additions.
    size: {
      sm: { root: { '--card-padding': 'spacing.4' }, title: { textStyle: 'md' } },
      md: { root: { '--card-padding': 'spacing.6' }, title: { textStyle: 'lg' } },
      lg: { root: { '--card-padding': 'spacing.8' }, title: { textStyle: 'xl' } },
    },
  },
})
