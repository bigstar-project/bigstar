// Adapted from Park UI. See ../PARK-UI-LICENSE.
import { defineSemanticTokens } from '@pandacss/dev'

export const green = defineSemanticTokens.colors({
  '1': { value: { base: '#fbfefc', _dark: '#0e1512' } },
  '2': { value: { base: '#f4fbf6', _dark: '#121b17' } },
  '3': { value: { base: '#e6f6eb', _dark: '#132d21' } },
  '4': { value: { base: '#d6f1df', _dark: '#113b29' } },
  '5': { value: { base: '#c4e8d1', _dark: '#174933' } },
  '6': { value: { base: '#adddc0', _dark: '#20573e' } },
  '7': { value: { base: '#8eceaa', _dark: '#28684a' } },
  '8': { value: { base: '#5bb98b', _dark: '#2f7c57' } },
  '9': { value: { base: '#30a46c', _dark: '#30a46c' } },
  '10': { value: { base: '#2b9a66', _dark: '#33b074' } },
  '11': { value: { base: '#218358', _dark: '#3dd68c' } },
  '12': { value: { base: '#193b2d', _dark: '#b1f1cb' } },
  a1: { value: { base: '#00c04004', _dark: '#00de4505' } },
  a2: { value: { base: '#00a32f0b', _dark: '#29f99d0b' } },
  a3: { value: { base: '#00a43319', _dark: '#22ff991e' } },
  a4: { value: { base: '#00a83829', _dark: '#11ff992d' } },
  a5: { value: { base: '#019c393b', _dark: '#2bffa23c' } },
  a6: { value: { base: '#00963c52', _dark: '#44ffaa4b' } },
  a7: { value: { base: '#00914071', _dark: '#50fdac5e' } },
  a8: { value: { base: '#00924ba4', _dark: '#54ffad73' } },
  a9: { value: { base: '#008f4acf', _dark: '#44ffa49e' } },
  a10: { value: { base: '#008647d4', _dark: '#43fea4ab' } },
  a11: { value: { base: '#00713fde', _dark: '#46fea5d4' } },
  a12: { value: { base: '#002616e6', _dark: '#bbffd7f0' } },
  solid: {
    bg: {
      DEFAULT: { value: { base: '{colors.green.9}', _dark: '{colors.green.9}' } },
      hover: { value: { base: '{colors.green.10}', _dark: '{colors.green.10}' } },
    },
    fg: { DEFAULT: { value: { base: 'white', _dark: 'white' } } },
  },
  subtle: {
    bg: {
      DEFAULT: { value: { base: '{colors.green.a3}', _dark: '{colors.green.a3}' } },
      hover: { value: { base: '{colors.green.a4}', _dark: '{colors.green.a4}' } },
      active: { value: { base: '{colors.green.a5}', _dark: '{colors.green.a5}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.green.a11}', _dark: '{colors.green.a11}' } } },
  },
  surface: {
    bg: {
      DEFAULT: { value: { base: '{colors.green.a2}', _dark: '{colors.green.a2}' } },
      active: { value: { base: '{colors.green.a3}', _dark: '{colors.green.a3}' } },
    },
    border: {
      DEFAULT: { value: { base: '{colors.green.a6}', _dark: '{colors.green.a6}' } },
      hover: { value: { base: '{colors.green.a7}', _dark: '{colors.green.a7}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.green.a11}', _dark: '{colors.green.a11}' } } },
  },
  outline: {
    bg: {
      hover: { value: { base: '{colors.green.a2}', _dark: '{colors.green.a2}' } },
      active: { value: { base: '{colors.green.a3}', _dark: '{colors.green.a3}' } },
    },
    border: { DEFAULT: { value: { base: '{colors.green.a7}', _dark: '{colors.green.a7}' } } },
    fg: { DEFAULT: { value: { base: '{colors.green.a11}', _dark: '{colors.green.a11}' } } },
  },
  plain: {
    bg: {
      hover: { value: { base: '{colors.green.a3}', _dark: '{colors.green.a3}' } },
      active: { value: { base: '{colors.green.a4}', _dark: '{colors.green.a4}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.green.a11}', _dark: '{colors.green.a11}' } } },
  },
})
