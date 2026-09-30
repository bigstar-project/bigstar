// Adapted from Park UI. See ./PARK-UI-LICENSE.
import { defineSemanticTokens } from '@pandacss/dev'

export const shadows = defineSemanticTokens.shadows({
  xs: {
    value: {
      base: '0px 1px 2px {colors.gray.a6}, 0px 0px 1px {colors.gray.a7}',
      _dark: '0px 1px 1px {colors.black.a8}, 0px 0px 1px inset {colors.gray.a8}',
    },
  },
  sm: {
    value: {
      base: '0px 2px 4px {colors.gray.a4}, 0px 0px 1px {colors.gray.a4}',
      _dark: '0px 2px 4px {colors.black.a8}, 0px 0px 1px inset {colors.gray.a8}',
    },
  },
  md: {
    value: {
      base: '0px 4px 8px {colors.gray.a4}, 0px 0px 1px {colors.gray.a4}',
      _dark: '0px 4px 8px {colors.black.a8}, 0px 0px 1px inset {colors.gray.a8}',
    },
  },
  lg: {
    value: {
      base: '0px 8px 16px {colors.gray.a4}, 0px 0px 1px {colors.gray.a4}',
      _dark: '0px 8px 16px {colors.black.a8}, 0px 0px 1px inset {colors.gray.a8}',
    },
  },
  xl: {
    value: {
      base: '0px 16px 24px {colors.gray.a4}, 0px 0px 1px {colors.gray.a4}',
      _dark: '0px 16px 24px {colors.black.a8}, 0px 0px 1px inset {colors.gray.a8}',
    },
  },
  '2xl': {
    value: {
      base: '0px 24px 40px {colors.gray.a4}, 0px 0px 1px {colors.gray.a4}',
      _dark: '0px 24px 40px {colors.black.a8}, 0px 0px 1px inset {colors.gray.a8}',
    },
  },
  inset: {
    value: {
      base: 'inset 8px 0 12px -8px {colors.gray.a4}',
      _dark: 'inset 8px 0 12px -8px {colors.black.a6}',
    },
  },
})
