// Adapted from Park UI. See ../PARK-UI-LICENSE.
import { defineSemanticTokens } from '@pandacss/dev'

export const plum = defineSemanticTokens.colors({
  '1': { value: { base: '#fefcff', _dark: '#181118' } },
  '2': { value: { base: '#fdf7fd', _dark: '#201320' } },
  '3': { value: { base: '#fbebfb', _dark: '#351a35' } },
  '4': { value: { base: '#f7def8', _dark: '#451d47' } },
  '5': { value: { base: '#f2d1f3', _dark: '#512454' } },
  '6': { value: { base: '#e9c2ec', _dark: '#5e3061' } },
  '7': { value: { base: '#deade3', _dark: '#734079' } },
  '8': { value: { base: '#cf91d8', _dark: '#92549c' } },
  '9': { value: { base: '#ab4aba', _dark: '#ab4aba' } },
  '10': { value: { base: '#a144af', _dark: '#b658c4' } },
  '11': { value: { base: '#953ea3', _dark: '#e796f3' } },
  '12': { value: { base: '#53195d', _dark: '#f4d4f4' } },
  a1: { value: { base: '#aa00ff03', _dark: '#f112f108' } },
  a2: { value: { base: '#c000c008', _dark: '#f22ff211' } },
  a3: { value: { base: '#cc00cc14', _dark: '#fd4cfd27' } },
  a4: { value: { base: '#c200c921', _dark: '#f646ff3a' } },
  a5: { value: { base: '#b700bd2e', _dark: '#f455ff48' } },
  a6: { value: { base: '#a400b03d', _dark: '#f66dff56' } },
  a7: { value: { base: '#9900a852', _dark: '#f07cfd70' } },
  a8: { value: { base: '#9000a56e', _dark: '#ee84ff95' } },
  a9: { value: { base: '#89009eb5', _dark: '#e961feb6' } },
  a10: { value: { base: '#7f0092bb', _dark: '#ed70ffc0' } },
  a11: { value: { base: '#730086c1', _dark: '#f19cfef3' } },
  a12: { value: { base: '#40004be6', _dark: '#feddfef4' } },
  solid: {
    bg: {
      DEFAULT: { value: { base: '{colors.plum.9}', _dark: '{colors.plum.9}' } },
      hover: { value: { base: '{colors.plum.10}', _dark: '{colors.plum.10}' } },
    },
    fg: { DEFAULT: { value: { base: 'white', _dark: 'white' } } },
  },
  subtle: {
    bg: {
      DEFAULT: { value: { base: '{colors.plum.a3}', _dark: '{colors.plum.a3}' } },
      hover: { value: { base: '{colors.plum.a4}', _dark: '{colors.plum.a4}' } },
      active: { value: { base: '{colors.plum.a5}', _dark: '{colors.plum.a5}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.plum.a11}', _dark: '{colors.plum.a11}' } } },
  },
  surface: {
    bg: {
      DEFAULT: { value: { base: '{colors.plum.a2}', _dark: '{colors.plum.a2}' } },
      active: { value: { base: '{colors.plum.a3}', _dark: '{colors.plum.a3}' } },
    },
    border: {
      DEFAULT: { value: { base: '{colors.plum.a6}', _dark: '{colors.plum.a6}' } },
      hover: { value: { base: '{colors.plum.a7}', _dark: '{colors.plum.a7}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.plum.a11}', _dark: '{colors.plum.a11}' } } },
  },
  outline: {
    bg: {
      hover: { value: { base: '{colors.plum.a2}', _dark: '{colors.plum.a2}' } },
      active: { value: { base: '{colors.plum.a3}', _dark: '{colors.plum.a3}' } },
    },
    border: { DEFAULT: { value: { base: '{colors.plum.a7}', _dark: '{colors.plum.a7}' } } },
    fg: { DEFAULT: { value: { base: '{colors.plum.a11}', _dark: '{colors.plum.a11}' } } },
  },
  plain: {
    bg: {
      hover: { value: { base: '{colors.plum.a3}', _dark: '{colors.plum.a3}' } },
      active: { value: { base: '{colors.plum.a4}', _dark: '{colors.plum.a4}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.plum.a11}', _dark: '{colors.plum.a11}' } } },
  },
})
