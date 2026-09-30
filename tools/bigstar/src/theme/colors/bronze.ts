// Adapted from Park UI. See ../PARK-UI-LICENSE.
import { defineSemanticTokens } from '@pandacss/dev'

export const bronze = defineSemanticTokens.colors({
  '1': { value: { base: '#fdfcfc', _dark: '#141110' } },
  '2': { value: { base: '#fdf7f5', _dark: '#1c1917' } },
  '3': { value: { base: '#f6edea', _dark: '#262220' } },
  '4': { value: { base: '#efe4df', _dark: '#302a27' } },
  '5': { value: { base: '#e7d9d3', _dark: '#3b3330' } },
  '6': { value: { base: '#dfcdc5', _dark: '#493e3a' } },
  '7': { value: { base: '#d3bcb3', _dark: '#5a4c47' } },
  '8': { value: { base: '#c2a499', _dark: '#6f5f58' } },
  '9': { value: { base: '#a18072', _dark: '#a18072' } },
  '10': { value: { base: '#957468', _dark: '#ae8c7e' } },
  '11': { value: { base: '#7d5e54', _dark: '#d4b3a5' } },
  '12': { value: { base: '#43302b', _dark: '#ede0d9' } },
  a1: { value: { base: '#55000003', _dark: '#d1110004' } },
  a2: { value: { base: '#cc33000a', _dark: '#fbbc910c' } },
  a3: { value: { base: '#92250015', _dark: '#faceb817' } },
  a4: { value: { base: '#80280020', _dark: '#facdb622' } },
  a5: { value: { base: '#7423002c', _dark: '#ffd2c12d' } },
  a6: { value: { base: '#7324003a', _dark: '#ffd1c03c' } },
  a7: { value: { base: '#6c1f004c', _dark: '#fdd0c04f' } },
  a8: { value: { base: '#671c0066', _dark: '#ffd6c565' } },
  a9: { value: { base: '#551a008d', _dark: '#fec7b09b' } },
  a10: { value: { base: '#4c150097', _dark: '#fecab5a9' } },
  a11: { value: { base: '#3d0f00ab', _dark: '#ffd7c6d1' } },
  a12: { value: { base: '#1d0600d4', _dark: '#fff1e9ec' } },
  solid: {
    bg: {
      DEFAULT: { value: { base: '{colors.bronze.9}', _dark: '{colors.bronze.9}' } },
      hover: { value: { base: '{colors.bronze.10}', _dark: '{colors.bronze.10}' } },
    },
    fg: { DEFAULT: { value: { base: 'white', _dark: 'white' } } },
  },
  subtle: {
    bg: {
      DEFAULT: { value: { base: '{colors.bronze.a3}', _dark: '{colors.bronze.a3}' } },
      hover: { value: { base: '{colors.bronze.a4}', _dark: '{colors.bronze.a4}' } },
      active: { value: { base: '{colors.bronze.a5}', _dark: '{colors.bronze.a5}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.bronze.a11}', _dark: '{colors.bronze.a11}' } } },
  },
  surface: {
    bg: {
      DEFAULT: { value: { base: '{colors.bronze.a2}', _dark: '{colors.bronze.a2}' } },
      active: { value: { base: '{colors.bronze.a3}', _dark: '{colors.bronze.a3}' } },
    },
    border: {
      DEFAULT: { value: { base: '{colors.bronze.a6}', _dark: '{colors.bronze.a6}' } },
      hover: { value: { base: '{colors.bronze.a7}', _dark: '{colors.bronze.a7}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.bronze.a11}', _dark: '{colors.bronze.a11}' } } },
  },
  outline: {
    bg: {
      hover: { value: { base: '{colors.bronze.a2}', _dark: '{colors.bronze.a2}' } },
      active: { value: { base: '{colors.bronze.a3}', _dark: '{colors.bronze.a3}' } },
    },
    border: { DEFAULT: { value: { base: '{colors.bronze.a7}', _dark: '{colors.bronze.a7}' } } },
    fg: { DEFAULT: { value: { base: '{colors.bronze.a11}', _dark: '{colors.bronze.a11}' } } },
  },
  plain: {
    bg: {
      hover: { value: { base: '{colors.bronze.a3}', _dark: '{colors.bronze.a3}' } },
      active: { value: { base: '{colors.bronze.a4}', _dark: '{colors.bronze.a4}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.bronze.a11}', _dark: '{colors.bronze.a11}' } } },
  },
})
