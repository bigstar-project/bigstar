// Adapted from Park UI. See ../PARK-UI-LICENSE.
import { defineSemanticTokens } from '@pandacss/dev'

export const sage = defineSemanticTokens.colors({
  '1': { value: { base: '#fbfdfc', _dark: '#101211' } },
  '2': { value: { base: '#f7f9f8', _dark: '#171918' } },
  '3': { value: { base: '#eef1f0', _dark: '#202221' } },
  '4': { value: { base: '#e6e9e8', _dark: '#272a29' } },
  '5': { value: { base: '#dfe2e0', _dark: '#2e3130' } },
  '6': { value: { base: '#d7dad9', _dark: '#373b39' } },
  '7': { value: { base: '#cbcfcd', _dark: '#444947' } },
  '8': { value: { base: '#b8bcba', _dark: '#5b625f' } },
  '9': { value: { base: '#868e8b', _dark: '#63706b' } },
  '10': { value: { base: '#7c8481', _dark: '#717d79' } },
  '11': { value: { base: '#5f6563', _dark: '#adb5b2' } },
  '12': { value: { base: '#1a211e', _dark: '#eceeed' } },
  a1: { value: { base: '#00804004', _dark: '#00000000' } },
  a2: { value: { base: '#00402008', _dark: '#f0f2f108' } },
  a3: { value: { base: '#002d1e11', _dark: '#f3f5f412' } },
  a4: { value: { base: '#001f1519', _dark: '#f2fefd1a' } },
  a5: { value: { base: '#00180820', _dark: '#f1fbfa22' } },
  a6: { value: { base: '#00140d28', _dark: '#edfbf42d' } },
  a7: { value: { base: '#00140a34', _dark: '#edfcf73c' } },
  a8: { value: { base: '#000f0847', _dark: '#ebfdf657' } },
  a9: { value: { base: '#00110b79', _dark: '#dffdf266' } },
  a10: { value: { base: '#00100a83', _dark: '#e5fdf674' } },
  a11: { value: { base: '#000a07a0', _dark: '#f4fefbb0' } },
  a12: { value: { base: '#000805e5', _dark: '#fdfffeed' } },
  solid: {
    bg: {
      DEFAULT: { value: { base: '{colors.black}', _dark: '{colors.white}' } },
      hover: { value: { base: '{colors.gray.12}', _dark: '{colors.gray.12}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.white}', _dark: '{colors.black}' } } },
  },
  subtle: {
    bg: {
      DEFAULT: { value: { base: '{colors.gray.a3}', _dark: '{colors.gray.a3}' } },
      hover: { value: { base: '{colors.gray.a4}', _dark: '{colors.gray.a4}' } },
      active: { value: { base: '{colors.gray.a5}', _dark: '{colors.gray.a5}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.gray.12}', _dark: '{colors.gray.12}' } } },
  },
  surface: {
    bg: {
      DEFAULT: { value: { base: '{colors.white}', _dark: '{colors.gray.1}' } },
      hover: { value: { base: '{colors.gray.2}', _dark: '{colors.gray.2}' } },
      active: { value: { base: '{colors.gray.3}', _dark: '{colors.gray.3}' } },
    },
    border: {
      DEFAULT: { value: { base: '{colors.gray.6}', _dark: '{colors.gray.6}' } },
      hover: { value: { base: '{colors.gray.7}', _dark: '{colors.gray.7}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.gray.12}', _dark: '{colors.gray.12}' } } },
  },
  outline: {
    bg: {
      hover: { value: { base: '{colors.gray.a2}', _dark: '{colors.gray.a2}' } },
      active: { value: { base: '{colors.gray.a3}', _dark: '{colors.gray.a3}' } },
    },
    border: { DEFAULT: { value: { base: '{colors.gray.6}', _dark: '{colors.gray.6}' } } },
    fg: { DEFAULT: { value: { base: '{colors.gray.12}', _dark: '{colors.gray.12}' } } },
  },
  plain: {
    bg: {
      hover: { value: { base: '{colors.gray.a3}', _dark: '{colors.gray.a3}' } },
      active: { value: { base: '{colors.gray.a4}', _dark: '{colors.gray.a4}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.gray.12}', _dark: '{colors.gray.12}' } } },
  },
})
