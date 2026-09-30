// Adapted from Park UI. See ../PARK-UI-LICENSE.
import { defineSemanticTokens } from '@pandacss/dev'

export const orange = defineSemanticTokens.colors({
  '1': { value: { base: '#fefcfb', _dark: '#17120e' } },
  '2': { value: { base: '#fff7ed', _dark: '#1e160f' } },
  '3': { value: { base: '#ffefd6', _dark: '#331e0b' } },
  '4': { value: { base: '#ffdfb5', _dark: '#462100' } },
  '5': { value: { base: '#ffd19a', _dark: '#562800' } },
  '6': { value: { base: '#ffc182', _dark: '#66350c' } },
  '7': { value: { base: '#f5ae73', _dark: '#7e451d' } },
  '8': { value: { base: '#ec9455', _dark: '#a35829' } },
  '9': { value: { base: '#f76b15', _dark: '#f76b15' } },
  '10': { value: { base: '#ef5f00', _dark: '#ff801f' } },
  '11': { value: { base: '#cc4e00', _dark: '#ffa057' } },
  '12': { value: { base: '#582d1d', _dark: '#ffe0c2' } },
  a1: { value: { base: '#c0400004', _dark: '#ec360007' } },
  a2: { value: { base: '#ff8e0012', _dark: '#fe6d000e' } },
  a3: { value: { base: '#ff9c0029', _dark: '#fb6a0025' } },
  a4: { value: { base: '#ff91014a', _dark: '#ff590039' } },
  a5: { value: { base: '#ff8b0065', _dark: '#ff61004a' } },
  a6: { value: { base: '#ff81007d', _dark: '#fd75045c' } },
  a7: { value: { base: '#ed6c008c', _dark: '#ff832c75' } },
  a8: { value: { base: '#e35f00aa', _dark: '#fe84389d' } },
  a9: { value: { base: '#f65e00ea', _dark: '#fe6d15f7' } },
  a10: { value: { base: '#ef5f00', _dark: '#ff801f' } },
  a11: { value: { base: '#cc4e00', _dark: '#ffa057' } },
  a12: { value: { base: '#431200e2', _dark: '#ffe0c2' } },
  solid: {
    bg: {
      DEFAULT: { value: { base: '{colors.orange.9}', _dark: '{colors.orange.9}' } },
      hover: { value: { base: '{colors.orange.10}', _dark: '{colors.orange.10}' } },
    },
    fg: { DEFAULT: { value: { base: 'white', _dark: 'white' } } },
  },
  subtle: {
    bg: {
      DEFAULT: { value: { base: '{colors.orange.a3}', _dark: '{colors.orange.a3}' } },
      hover: { value: { base: '{colors.orange.a4}', _dark: '{colors.orange.a4}' } },
      active: { value: { base: '{colors.orange.a5}', _dark: '{colors.orange.a5}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.orange.a11}', _dark: '{colors.orange.a11}' } } },
  },
  surface: {
    bg: {
      DEFAULT: { value: { base: '{colors.orange.a2}', _dark: '{colors.orange.a2}' } },
      active: { value: { base: '{colors.orange.a3}', _dark: '{colors.orange.a3}' } },
    },
    border: {
      DEFAULT: { value: { base: '{colors.orange.a6}', _dark: '{colors.orange.a6}' } },
      hover: { value: { base: '{colors.orange.a7}', _dark: '{colors.orange.a7}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.orange.a11}', _dark: '{colors.orange.a11}' } } },
  },
  outline: {
    bg: {
      hover: { value: { base: '{colors.orange.a2}', _dark: '{colors.orange.a2}' } },
      active: { value: { base: '{colors.orange.a3}', _dark: '{colors.orange.a3}' } },
    },
    border: { DEFAULT: { value: { base: '{colors.orange.a7}', _dark: '{colors.orange.a7}' } } },
    fg: { DEFAULT: { value: { base: '{colors.orange.a11}', _dark: '{colors.orange.a11}' } } },
  },
  plain: {
    bg: {
      hover: { value: { base: '{colors.orange.a3}', _dark: '{colors.orange.a3}' } },
      active: { value: { base: '{colors.orange.a4}', _dark: '{colors.orange.a4}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.orange.a11}', _dark: '{colors.orange.a11}' } } },
  },
})
