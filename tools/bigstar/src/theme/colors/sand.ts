// Adapted from Park UI. See ../PARK-UI-LICENSE.
import { defineSemanticTokens } from '@pandacss/dev'

export const sand = defineSemanticTokens.colors({
  '1': { value: { base: '#fdfdfc', _dark: '#111110' } },
  '2': { value: { base: '#f9f9f8', _dark: '#191918' } },
  '3': { value: { base: '#f1f0ef', _dark: '#222221' } },
  '4': { value: { base: '#e9e8e6', _dark: '#2a2a28' } },
  '5': { value: { base: '#e2e1de', _dark: '#31312e' } },
  '6': { value: { base: '#dad9d6', _dark: '#3b3a37' } },
  '7': { value: { base: '#cfceca', _dark: '#494844' } },
  '8': { value: { base: '#bcbbb5', _dark: '#62605b' } },
  '9': { value: { base: '#8d8d86', _dark: '#6f6d66' } },
  '10': { value: { base: '#82827c', _dark: '#7c7b74' } },
  '11': { value: { base: '#63635e', _dark: '#b5b3ad' } },
  '12': { value: { base: '#21201c', _dark: '#eeeeec' } },
  a1: { value: { base: '#55550003', _dark: '#00000000' } },
  a2: { value: { base: '#25250007', _dark: '#f4f4f309' } },
  a3: { value: { base: '#20100010', _dark: '#f6f6f513' } },
  a4: { value: { base: '#1f150019', _dark: '#fefef31b' } },
  a5: { value: { base: '#1f180021', _dark: '#fbfbeb23' } },
  a6: { value: { base: '#19130029', _dark: '#fffaed2d' } },
  a7: { value: { base: '#19140035', _dark: '#fffbed3c' } },
  a8: { value: { base: '#1915014a', _dark: '#fff9eb57' } },
  a9: { value: { base: '#0f0f0079', _dark: '#fffae965' } },
  a10: { value: { base: '#0c0c0083', _dark: '#fffdee73' } },
  a11: { value: { base: '#080800a1', _dark: '#fffcf4b0' } },
  a12: { value: { base: '#060500e3', _dark: '#fffffded' } },
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
