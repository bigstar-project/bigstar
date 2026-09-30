import { defineSlotRecipe } from '@pandacss/dev'
import { arrow } from '../shared'

// Base UI sets data-type from toast.add({ type }); success / error / warning / info pick the
// palette of the indicator icon. The card itself stays neutral.
// Stacking follows Base UI's hero demo: toasts sit behind the frontmost one, smaller and peeking
// out above it, and fan out when the viewport is hovered or focused (data-expanded).
export const toast = defineSlotRecipe({
  className: 'kiso-toast',
  jsx: ['Toast', /^Toast\./, 'Toaster'],
  slots: [
    'viewport',
    'positioner',
    'root',
    'arrow',
    'indicator',
    'content',
    'title',
    'description',
    'close',
    'action',
  ],
  base: {
    viewport: {
      position: 'fixed',
      bottom: '4',
      insetInlineEnd: '4',
      zIndex: 'toast',
      width: 'sm',
      maxWidth: 'calc(100vw - token(spacing.8))',
      outline: '0',
    },
    // Anchored toasts: Positioner places one toast next to an element instead of the stack.
    positioner: { zIndex: 'toast' },
    root: {
      '--gap': 'spacing.3',
      '--peek': 'spacing.3',
      '--scale': 'calc(max(0, 1 - (var(--toast-index) * 0.1)))',
      '--shrink': 'calc(1 - var(--scale))',
      '--height': 'var(--toast-frontmost-height, var(--toast-height))',
      '--offset-y':
        'calc(var(--toast-offset-y) * -1 + (var(--toast-index) * var(--gap) * -1) + var(--toast-swipe-movement-y))',
      position: 'absolute',
      insetInlineEnd: '0',
      bottom: '0',
      width: 'full',
      height: 'var(--height)',
      zIndex: 'calc(1000 - var(--toast-index))',
      display: 'flex',
      alignItems: 'flex-start',
      gap: '3',
      p: '4',
      bg: 'gray.surface.bg',
      color: 'fg.default',
      borderWidth: '1px',
      borderColor: 'border',
      borderRadius: 'l3',
      boxShadow: 'lg',
      textStyle: 'sm',
      userSelect: 'none',
      focusVisibleRing: 'outside',
      transformOrigin: 'bottom center',
      // Collapsed: each toast behind is shorter (the frontmost height), smaller and peeks above.
      transform:
        'translateX(var(--toast-swipe-movement-x)) translateY(calc(var(--toast-swipe-movement-y) - (var(--toast-index) * var(--peek)) - (var(--shrink) * var(--height)))) scale(var(--scale))',
      transition: 'transform 0.5s cubic-bezier(0.22, 1, 0.36, 1), opacity 0.5s, height 0.15s',
      // Keeps the hover alive in the gap between expanded toasts.
      _after: {
        content: '""',
        position: 'absolute',
        top: '100%',
        insetInline: '0',
        height: 'calc(var(--gap) + 1px)',
      },
      // Only the frontmost toast shows its content until the stack expands.
      '& > *': { transition: 'opacity 0.25s cubic-bezier(0.22, 1, 0.36, 1)' },
      '&:has(> [data-behind]:not([data-expanded])) > *': { opacity: 0 },
      // Base UI's viewport expansion, not Kiso's _expanded (data-panel-open).
      '&[data-expanded]': {
        transform: 'translateX(var(--toast-swipe-movement-x)) translateY(var(--offset-y))',
        height: 'var(--toast-height)',
      },
      _startingStyle: { transform: 'translateY(150%)' },
      _endingStyle: {
        opacity: 0,
        transform: 'translateY(150%)',
        '&[data-swipe-direction=up]': {
          transform: 'translateY(calc(var(--toast-swipe-movement-y) - 150%))',
        },
        '&[data-swipe-direction=down]': {
          transform: 'translateY(calc(var(--toast-swipe-movement-y) + 150%))',
        },
        '&[data-swipe-direction=left]': {
          transform:
            'translateX(calc(var(--toast-swipe-movement-x) - 150%)) translateY(var(--offset-y))',
        },
        '&[data-swipe-direction=right]': {
          transform:
            'translateX(calc(var(--toast-swipe-movement-x) + 150%)) translateY(var(--offset-y))',
        },
      },
      '&[data-limited]': { opacity: 0 },
      '&[data-type=success]': { colorPalette: 'success' },
      '&[data-type=error]': { colorPalette: 'danger' },
      '&[data-type=warning]': { colorPalette: 'warning' },
      '&[data-type=info]': { colorPalette: 'info' },
      '&[data-type=loading]': { colorPalette: 'gray' },
      // Inside a Positioner the toast is a small callout next to its anchor, not a stack card.
      '.kiso-toast__positioner > &': {
        position: 'relative',
        insetInlineEnd: 'auto',
        bottom: 'auto',
        width: 'max-content',
        maxWidth: 'min(token(sizes.xs), var(--available-width))',
        height: 'auto',
        zIndex: 'auto',
        py: '2.5',
        px: '3',
        transform: 'none',
        transformOrigin: 'var(--transform-origin)',
        transitionProperty: 'opacity, scale',
        transitionDuration: 'fast',
        _after: { display: 'none' },
        _startingStyle: { transform: 'none', opacity: 0, scale: '0.96' },
        _endingStyle: { transform: 'none', opacity: 0, scale: '0.96' },
      },
    },
    arrow: { ...arrow, borderColor: 'border' },
    // Optional: without it the toast is a plain neutral card.
    indicator: {
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      flexShrink: '0',
      h: '5',
      color: 'colorPalette.plain.fg',
      _icon: { boxSize: '5' },
    },
    content: { display: 'flex', flexDirection: 'column', gap: '1', flex: '1', minWidth: '0' },
    title: { fontWeight: 'semibold' },
    description: { color: 'fg.muted' },
    close: {
      display: 'grid',
      placeItems: 'center',
      flexShrink: '0',
      boxSize: '6',
      mt: '-0.5',
      mr: '-1',
      color: 'fg.muted',
      borderRadius: 'l2',
      cursor: 'pointer',
      focusVisibleRing: 'outside',
      _hover: { bg: 'gray.plain.bg.hover', color: 'fg.default' },
      _icon: { boxSize: '4' },
    },
    action: {
      alignSelf: 'center',
      flexShrink: '0',
      h: '8',
      px: '3',
      borderRadius: 'l2',
      fontWeight: 'semibold',
      textStyle: 'sm',
      bg: 'gray.subtle.bg',
      color: 'gray.subtle.fg',
      cursor: 'pointer',
      focusVisibleRing: 'outside',
      _hover: { bg: 'gray.subtle.bg.hover' },
    },
  },
})
