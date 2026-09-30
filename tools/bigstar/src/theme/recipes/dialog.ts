import { defineSlotRecipe } from '@pandacss/dev'
import { closeTrigger } from '../shared'

// Shared by Dialog and AlertDialog. Header, Body and Footer are optional layout parts.
export const dialog = defineSlotRecipe({
  className: 'kiso-dialog',
  jsx: ['Dialog', /^Dialog\./, 'AlertDialog', /^AlertDialog\./],
  slots: [
    'backdrop',
    'popup',
    'header',
    'body',
    'footer',
    'title',
    'description',
    'close',
    'closeTrigger',
  ],
  base: {
    backdrop: {
      position: 'fixed',
      inset: '0',
      bg: 'black.a7',
      zIndex: 'overlay',
      transitionProperty: 'opacity',
      transitionDuration: 'normal',
      _startingStyle: { opacity: 0 },
      _endingStyle: { opacity: 0 },
      // A nested dialog dims its parent instead of stacking a second backdrop under it.
      '&[data-nested]': { bg: 'transparent' },
    },
    popup: {
      position: 'fixed',
      left: '50%',
      zIndex: 'modal',
      display: 'flex',
      flexDirection: 'column',
      gap: { base: '4', md: '6' },
      width: 'calc(100vw - token(spacing.8))',
      maxHeight: 'calc(100dvh - token(spacing.12))',
      overflowY: 'auto',
      p: { base: '4', md: '6' },
      // App change: gray.surface.bg is the canvas in dark mode, so the popup is raised and outlined.
      bg: { base: 'gray.surface.bg', _dark: 'gray.2' },
      color: 'fg.default',
      borderWidth: '1px',
      borderRadius: 'l3',
      boxShadow: 'lg',
      outline: '0',
      textStyle: 'sm',
      transitionProperty: 'opacity, scale',
      transitionDuration: 'normal',
      _startingStyle: { opacity: 0, scale: '0.96' },
      _endingStyle: { opacity: 0, scale: '0.96' },
      // While a child dialog is open, the parent steps back: slightly smaller and dimmed.
      '&[data-nested-dialog-open]': {
        scale: 'calc(1 - 0.04 * var(--nested-dialogs))',
        overflow: 'hidden',
        _after: {
          content: '""',
          position: 'absolute',
          inset: '0',
          borderRadius: 'inherit',
          bg: 'black.a4',
          pointerEvents: 'none',
        },
      },
    },
    header: { display: 'flex', flexDirection: 'column', gap: '1' },
    body: { display: 'flex', flexDirection: 'column', gap: '4', flex: '1' },
    footer: { display: 'flex', alignItems: 'center', justifyContent: 'flex-end', gap: '3' },
    title: { fontWeight: 'semibold', textStyle: 'lg', color: 'fg.default' },
    description: { color: 'fg.muted', textStyle: 'sm' },
    close: {},
    closeTrigger,
  },
  defaultVariants: { size: 'md', placement: 'center' },
  variants: {
    size: {
      xs: { popup: { maxWidth: 'xs' } },
      sm: { popup: { maxWidth: 'sm' } },
      md: { popup: { maxWidth: 'md' } },
      lg: { popup: { maxWidth: 'lg' } },
      xl: { popup: { maxWidth: 'xl' } },
      full: {
        popup: {
          width: '100vw',
          maxWidth: '100vw',
          height: '100dvh',
          maxHeight: '100dvh',
          borderRadius: '0',
        },
      },
    },
    placement: {
      center: { popup: { top: '50%', translate: '-50% -50%' } },
      top: { popup: { top: '16', translate: '-50% 0' } },
    },
  },
  compoundVariants: [{ size: 'full', css: { popup: { top: '0!', translate: '-50% 0!' } } }],
})
