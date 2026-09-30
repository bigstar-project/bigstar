import { defineGlobalStyles } from '@pandacss/dev'

// Merge into panda.config.ts: globalCss. Panda's preflight reads the --global-color-* variables.
export const globalCss = defineGlobalStyles({
  '*': {
    '--global-color-border': 'colors.border',
    '--global-color-placeholder': 'colors.fg.subtle',
    '--global-color-selection': 'colors.colorPalette.subtle.bg',
    '--global-color-focus-ring': 'colors.colorPalette.solid.bg',
  },
  html: {
    // Every component inherits the accent; set colorPalette on any ancestor to recolor a subtree.
    colorPalette: 'accent',
    colorScheme: 'light',
    fontFamily: 'sans',
    '&.dark': { colorScheme: 'dark' },
  },
  body: { bg: 'canvas', color: 'fg.default' },
  '@media (prefers-reduced-motion: reduce)': {
    '*, *::before, *::after': {
      animationDuration: '0.01ms!',
      transitionDuration: '0.01ms!',
      scrollBehavior: 'auto!',
    },
  },
})
