// Adapted from Park UI. See ../PARK-UI-LICENSE.
import { defineSemanticTokens } from '@pandacss/dev'

export const neutral = defineSemanticTokens.colors({
  '1': { value: { base: '#fcfcfc', _dark: '#111111' } },
  '2': { value: { base: '#f9f9f9', _dark: '#191919' } },
  '3': { value: { base: '#f0f0f0', _dark: '#222222' } },
  '4': { value: { base: '#e8e8e8', _dark: '#2a2a2a' } },
  '5': { value: { base: '#e0e0e0', _dark: '#313131' } },
  '6': { value: { base: '#d9d9d9', _dark: '#3a3a3a' } },
  '7': { value: { base: '#cecece', _dark: '#484848' } },
  '8': { value: { base: '#bbbbbb', _dark: '#606060' } },
  '9': { value: { base: '#8d8d8d', _dark: '#6e6e6e' } },
  '10': { value: { base: '#838383', _dark: '#7b7b7b' } },
  '11': { value: { base: '#646464', _dark: '#b4b4b4' } },
  '12': { value: { base: '#202020', _dark: '#eeeeee' } },
  a1: { value: { base: '#00000003', _dark: '#00000000' } },
  a2: { value: { base: '#00000006', _dark: '#ffffff09' } },
  a3: { value: { base: '#0000000f', _dark: '#ffffff12' } },
  a4: { value: { base: '#00000017', _dark: '#ffffff1b' } },
  a5: { value: { base: '#0000001f', _dark: '#ffffff22' } },
  a6: { value: { base: '#00000026', _dark: '#ffffff2c' } },
  a7: { value: { base: '#00000031', _dark: '#ffffff3b' } },
  a8: { value: { base: '#00000044', _dark: '#ffffff55' } },
  a9: { value: { base: '#00000072', _dark: '#ffffff64' } },
  a10: { value: { base: '#0000007c', _dark: '#ffffff72' } },
  a11: { value: { base: '#0000009b', _dark: '#ffffffaf' } },
  a12: { value: { base: '#000000df', _dark: '#ffffffed' } },
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
