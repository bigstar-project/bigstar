import { defineSlotRecipe } from '@pandacss/dev'

export const emptyState = defineSlotRecipe({
  className: 'kiso-empty-state',
  jsx: ['EmptyState', /^EmptyState\./],
  slots: ['root', 'icon', 'title', 'description', 'actions'],
  base: {
    root: {
      display: 'flex',
      flexDirection: 'column',
      alignItems: 'center',
      textAlign: 'center',
      width: 'full',
      borderRadius: 'l3',
    },
    icon: {
      display: 'grid',
      placeItems: 'center',
      borderRadius: 'l3',
      bg: 'gray.subtle.bg',
      color: 'fg.muted',
    },
    title: { fontWeight: 'semibold', color: 'fg.default' },
    description: { color: 'fg.muted', maxWidth: 'sm' },
    actions: { display: 'flex', flexWrap: 'wrap', justifyContent: 'center', gap: '2' },
  },
  defaultVariants: { variant: 'outline', size: 'md' },
  variants: {
    variant: {
      outline: {
        root: { borderWidth: '1px', borderStyle: 'dashed', borderColor: 'gray.outline.border' },
      },
      subtle: { root: { bg: 'gray.subtle.bg' } },
      plain: {},
    },
    size: {
      sm: {
        root: { gap: '2', px: '5', py: '6' },
        icon: { boxSize: '9', mb: '2', _icon: { boxSize: '4' } },
        title: { textStyle: 'sm' },
        description: { textStyle: 'xs' },
        actions: { mt: '3' },
      },
      md: {
        root: { gap: '2', px: '6', py: '10' },
        icon: { boxSize: '11', mb: '2', _icon: { boxSize: '5' } },
        title: { textStyle: 'md' },
        description: { textStyle: 'sm' },
        actions: { mt: '4' },
      },
      lg: {
        root: { gap: '3', px: '8', py: '14' },
        icon: { boxSize: '14', mb: '3', _icon: { boxSize: '6' } },
        title: { textStyle: 'lg' },
        description: { textStyle: 'md' },
        actions: { mt: '5' },
      },
    },
  },
})
