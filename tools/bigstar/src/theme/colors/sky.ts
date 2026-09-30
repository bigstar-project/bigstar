// Adapted from Park UI. See ../PARK-UI-LICENSE.
import { defineSemanticTokens } from '@pandacss/dev'

export const sky = defineSemanticTokens.colors({
  '1': { value: { base: '#f9feff', _dark: '#0d141f' } },
  '2': { value: { base: '#f1fafd', _dark: '#111a27' } },
  '3': { value: { base: '#e1f6fd', _dark: '#112840' } },
  '4': { value: { base: '#d1f0fa', _dark: '#113555' } },
  '5': { value: { base: '#bee7f5', _dark: '#154467' } },
  '6': { value: { base: '#a9daed', _dark: '#1b537b' } },
  '7': { value: { base: '#8dcae3', _dark: '#1f6692' } },
  '8': { value: { base: '#60b3d7', _dark: '#197cae' } },
  '9': { value: { base: '#7ce2fe', _dark: '#7ce2fe' } },
  '10': { value: { base: '#74daf8', _dark: '#a8eeff' } },
  '11': { value: { base: '#00749e', _dark: '#75c7f0' } },
  '12': { value: { base: '#1d3e56', _dark: '#c2f3ff' } },
  a1: { value: { base: '#00d5ff06', _dark: '#0044ff0f' } },
  a2: { value: { base: '#00a4db0e', _dark: '#1171fb18' } },
  a3: { value: { base: '#00b3ee1e', _dark: '#1184fc33' } },
  a4: { value: { base: '#00ace42e', _dark: '#128fff49' } },
  a5: { value: { base: '#00a1d841', _dark: '#1c9dfd5d' } },
  a6: { value: { base: '#0092ca56', _dark: '#28a5ff72' } },
  a7: { value: { base: '#0089c172', _dark: '#2badfe8b' } },
  a8: { value: { base: '#0085bf9f', _dark: '#1db2fea9' } },
  a9: { value: { base: '#00c7fe83', _dark: '#7ce3fffe' } },
  a10: { value: { base: '#00bcf38b', _dark: '#a8eeff' } },
  a11: { value: { base: '#00749e', _dark: '#7cd3ffef' } },
  a12: { value: { base: '#002540e2', _dark: '#c2f3ff' } },
  solid: {
    bg: {
      DEFAULT: { value: { base: '{colors.sky.9}', _dark: '{colors.sky.9}' } },
      hover: { value: { base: '{colors.sky.10}', _dark: '{colors.sky.10}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.gray.12}', _dark: '{colors.gray.1}' } } },
  },
  subtle: {
    bg: {
      DEFAULT: { value: { base: '{colors.sky.a3}', _dark: '{colors.sky.a3}' } },
      hover: { value: { base: '{colors.sky.a4}', _dark: '{colors.sky.a4}' } },
      active: { value: { base: '{colors.sky.a5}', _dark: '{colors.sky.a5}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.sky.a11}', _dark: '{colors.sky.a11}' } } },
  },
  surface: {
    bg: {
      DEFAULT: { value: { base: '{colors.sky.a2}', _dark: '{colors.sky.a2}' } },
      active: { value: { base: '{colors.sky.a3}', _dark: '{colors.sky.a3}' } },
    },
    border: {
      DEFAULT: { value: { base: '{colors.sky.a6}', _dark: '{colors.sky.a6}' } },
      hover: { value: { base: '{colors.sky.a7}', _dark: '{colors.sky.a7}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.sky.a11}', _dark: '{colors.sky.a11}' } } },
  },
  outline: {
    bg: {
      hover: { value: { base: '{colors.sky.a2}', _dark: '{colors.sky.a2}' } },
      active: { value: { base: '{colors.sky.a3}', _dark: '{colors.sky.a3}' } },
    },
    border: { DEFAULT: { value: { base: '{colors.sky.a7}', _dark: '{colors.sky.a7}' } } },
    fg: { DEFAULT: { value: { base: '{colors.sky.a11}', _dark: '{colors.sky.a11}' } } },
  },
  plain: {
    bg: {
      hover: { value: { base: '{colors.sky.a3}', _dark: '{colors.sky.a3}' } },
      active: { value: { base: '{colors.sky.a4}', _dark: '{colors.sky.a4}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.sky.a11}', _dark: '{colors.sky.a11}' } } },
  },
})
