// Adapted from Park UI. See ../PARK-UI-LICENSE.
import { defineSemanticTokens } from '@pandacss/dev'

export const mauve = defineSemanticTokens.colors({
  '1': { value: { base: '#fdfcfd', _dark: '#121113' } },
  '2': { value: { base: '#faf9fb', _dark: '#1a191b' } },
  '3': { value: { base: '#f2eff3', _dark: '#232225' } },
  '4': { value: { base: '#eae7ec', _dark: '#2b292d' } },
  '5': { value: { base: '#e3dfe6', _dark: '#323035' } },
  '6': { value: { base: '#dbd8e0', _dark: '#3c393f' } },
  '7': { value: { base: '#d0cdd7', _dark: '#49474e' } },
  '8': { value: { base: '#bcbac7', _dark: '#625f69' } },
  '9': { value: { base: '#8e8c99', _dark: '#6f6d78' } },
  '10': { value: { base: '#84828e', _dark: '#7c7a85' } },
  '11': { value: { base: '#65636d', _dark: '#b5b2bc' } },
  '12': { value: { base: '#211f26', _dark: '#eeeef0' } },
  a1: { value: { base: '#55005503', _dark: '#00000000' } },
  a2: { value: { base: '#2b005506', _dark: '#f5f4f609' } },
  a3: { value: { base: '#30004010', _dark: '#ebeaf814' } },
  a4: { value: { base: '#20003618', _dark: '#eee5f81d' } },
  a5: { value: { base: '#20003820', _dark: '#efe6fe25' } },
  a6: { value: { base: '#14003527', _dark: '#f1e6fd30' } },
  a7: { value: { base: '#10003332', _dark: '#eee9ff40' } },
  a8: { value: { base: '#08003145', _dark: '#eee7ff5d' } },
  a9: { value: { base: '#05001d73', _dark: '#eae6fd6e' } },
  a10: { value: { base: '#0500197d', _dark: '#ece9fd7c' } },
  a11: { value: { base: '#0400119c', _dark: '#f5f1ffb7' } },
  a12: { value: { base: '#020008e0', _dark: '#fdfdffef' } },
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
