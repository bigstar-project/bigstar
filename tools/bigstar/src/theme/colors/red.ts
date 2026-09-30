// Adapted from Park UI. See ../PARK-UI-LICENSE.
import { defineSemanticTokens } from '@pandacss/dev'

export const red = defineSemanticTokens.colors({
  '1': { value: { base: '#fffcfc', _dark: '#191111' } },
  '2': { value: { base: '#fff7f7', _dark: '#201314' } },
  '3': { value: { base: '#feebec', _dark: '#3b1219' } },
  '4': { value: { base: '#ffdbdc', _dark: '#500f1c' } },
  '5': { value: { base: '#ffcdce', _dark: '#611623' } },
  '6': { value: { base: '#fdbdbe', _dark: '#72232d' } },
  '7': { value: { base: '#f4a9aa', _dark: '#8c333a' } },
  '8': { value: { base: '#eb8e90', _dark: '#b54548' } },
  '9': { value: { base: '#e5484d', _dark: '#e5484d' } },
  '10': { value: { base: '#dc3e42', _dark: '#ec5d5e' } },
  '11': { value: { base: '#ce2c31', _dark: '#ff9592' } },
  '12': { value: { base: '#641723', _dark: '#ffd1d9' } },
  a1: { value: { base: '#ff000003', _dark: '#f4121209' } },
  a2: { value: { base: '#ff000008', _dark: '#f22f3e11' } },
  a3: { value: { base: '#f3000d14', _dark: '#ff173f2d' } },
  a4: { value: { base: '#ff000824', _dark: '#fe0a3b44' } },
  a5: { value: { base: '#ff000632', _dark: '#ff204756' } },
  a6: { value: { base: '#f8000442', _dark: '#ff3e5668' } },
  a7: { value: { base: '#df000356', _dark: '#ff536184' } },
  a8: { value: { base: '#d2000571', _dark: '#ff5d61b0' } },
  a9: { value: { base: '#db0007b7', _dark: '#fe4e54e4' } },
  a10: { value: { base: '#d10005c1', _dark: '#ff6465eb' } },
  a11: { value: { base: '#c40006d3', _dark: '#ff9592' } },
  a12: { value: { base: '#55000de8', _dark: '#ffd1d9' } },
  solid: {
    bg: {
      DEFAULT: { value: { base: '{colors.red.9}', _dark: '{colors.red.9}' } },
      hover: { value: { base: '{colors.red.10}', _dark: '{colors.red.10}' } },
    },
    fg: { DEFAULT: { value: { base: 'white', _dark: 'white' } } },
  },
  subtle: {
    bg: {
      DEFAULT: { value: { base: '{colors.red.a3}', _dark: '{colors.red.a3}' } },
      hover: { value: { base: '{colors.red.a4}', _dark: '{colors.red.a4}' } },
      active: { value: { base: '{colors.red.a5}', _dark: '{colors.red.a5}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.red.a11}', _dark: '{colors.red.a11}' } } },
  },
  surface: {
    bg: {
      DEFAULT: { value: { base: '{colors.red.a2}', _dark: '{colors.red.a2}' } },
      active: { value: { base: '{colors.red.a3}', _dark: '{colors.red.a3}' } },
    },
    border: {
      DEFAULT: { value: { base: '{colors.red.a6}', _dark: '{colors.red.a6}' } },
      hover: { value: { base: '{colors.red.a7}', _dark: '{colors.red.a7}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.red.a11}', _dark: '{colors.red.a11}' } } },
  },
  outline: {
    bg: {
      hover: { value: { base: '{colors.red.a2}', _dark: '{colors.red.a2}' } },
      active: { value: { base: '{colors.red.a3}', _dark: '{colors.red.a3}' } },
    },
    border: { DEFAULT: { value: { base: '{colors.red.a7}', _dark: '{colors.red.a7}' } } },
    fg: { DEFAULT: { value: { base: '{colors.red.a11}', _dark: '{colors.red.a11}' } } },
  },
  plain: {
    bg: {
      hover: { value: { base: '{colors.red.a3}', _dark: '{colors.red.a3}' } },
      active: { value: { base: '{colors.red.a4}', _dark: '{colors.red.a4}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.red.a11}', _dark: '{colors.red.a11}' } } },
  },
})
