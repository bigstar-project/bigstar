// Adapted from Park UI. See ../PARK-UI-LICENSE.
import { defineSemanticTokens } from '@pandacss/dev'

export const yellow = defineSemanticTokens.colors({
  '1': { value: { base: '#fdfdf9', _dark: '#14120b' } },
  '2': { value: { base: '#fefce9', _dark: '#1b180f' } },
  '3': { value: { base: '#fffab8', _dark: '#2d2305' } },
  '4': { value: { base: '#fff394', _dark: '#362b00' } },
  '5': { value: { base: '#ffe770', _dark: '#433500' } },
  '6': { value: { base: '#f3d768', _dark: '#524202' } },
  '7': { value: { base: '#e4c767', _dark: '#665417' } },
  '8': { value: { base: '#d5ae39', _dark: '#836a21' } },
  '9': { value: { base: '#ffe629', _dark: '#ffe629' } },
  '10': { value: { base: '#ffdc00', _dark: '#ffff57' } },
  '11': { value: { base: '#9e6c00', _dark: '#f5e147' } },
  '12': { value: { base: '#473b1f', _dark: '#f6eeb4' } },
  a1: { value: { base: '#aaaa0006', _dark: '#d1510004' } },
  a2: { value: { base: '#f4dd0016', _dark: '#f9b4000b' } },
  a3: { value: { base: '#ffee0047', _dark: '#ffaa001e' } },
  a4: { value: { base: '#ffe3016b', _dark: '#fdb70028' } },
  a5: { value: { base: '#ffd5008f', _dark: '#febb0036' } },
  a6: { value: { base: '#ebbc0097', _dark: '#fec40046' } },
  a7: { value: { base: '#d2a10098', _dark: '#fdcb225c' } },
  a8: { value: { base: '#c99700c6', _dark: '#fdca327b' } },
  a9: { value: { base: '#ffe100d6', _dark: '#ffe629' } },
  a10: { value: { base: '#ffdc00', _dark: '#ffff57' } },
  a11: { value: { base: '#9e6c00', _dark: '#fee949f5' } },
  a12: { value: { base: '#2e2000e0', _dark: '#fef6baf6' } },
  solid: {
    bg: {
      DEFAULT: { value: { base: '{colors.yellow.9}', _dark: '{colors.yellow.9}' } },
      hover: { value: { base: '{colors.yellow.10}', _dark: '{colors.yellow.10}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.gray.12}', _dark: '{colors.gray.1}' } } },
  },
  subtle: {
    bg: {
      DEFAULT: { value: { base: '{colors.yellow.a3}', _dark: '{colors.yellow.a3}' } },
      hover: { value: { base: '{colors.yellow.a4}', _dark: '{colors.yellow.a4}' } },
      active: { value: { base: '{colors.yellow.a5}', _dark: '{colors.yellow.a5}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.yellow.a11}', _dark: '{colors.yellow.a11}' } } },
  },
  surface: {
    bg: {
      DEFAULT: { value: { base: '{colors.yellow.a2}', _dark: '{colors.yellow.a2}' } },
      active: { value: { base: '{colors.yellow.a3}', _dark: '{colors.yellow.a3}' } },
    },
    border: {
      DEFAULT: { value: { base: '{colors.yellow.a6}', _dark: '{colors.yellow.a6}' } },
      hover: { value: { base: '{colors.yellow.a7}', _dark: '{colors.yellow.a7}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.yellow.a11}', _dark: '{colors.yellow.a11}' } } },
  },
  outline: {
    bg: {
      hover: { value: { base: '{colors.yellow.a2}', _dark: '{colors.yellow.a2}' } },
      active: { value: { base: '{colors.yellow.a3}', _dark: '{colors.yellow.a3}' } },
    },
    border: { DEFAULT: { value: { base: '{colors.yellow.a7}', _dark: '{colors.yellow.a7}' } } },
    fg: { DEFAULT: { value: { base: '{colors.yellow.a11}', _dark: '{colors.yellow.a11}' } } },
  },
  plain: {
    bg: {
      hover: { value: { base: '{colors.yellow.a3}', _dark: '{colors.yellow.a3}' } },
      active: { value: { base: '{colors.yellow.a4}', _dark: '{colors.yellow.a4}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.yellow.a11}', _dark: '{colors.yellow.a11}' } } },
  },
})
