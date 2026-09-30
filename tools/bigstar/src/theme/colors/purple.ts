// Adapted from Park UI. See ../PARK-UI-LICENSE.
import { defineSemanticTokens } from '@pandacss/dev'

export const purple = defineSemanticTokens.colors({
  '1': { value: { base: '#fefcfe', _dark: '#18111b' } },
  '2': { value: { base: '#fbf7fe', _dark: '#1e1523' } },
  '3': { value: { base: '#f7edfe', _dark: '#301c3b' } },
  '4': { value: { base: '#f2e2fc', _dark: '#3d224e' } },
  '5': { value: { base: '#ead5f9', _dark: '#48295c' } },
  '6': { value: { base: '#e0c4f4', _dark: '#54346b' } },
  '7': { value: { base: '#d1afec', _dark: '#664282' } },
  '8': { value: { base: '#be93e4', _dark: '#8457aa' } },
  '9': { value: { base: '#8e4ec6', _dark: '#8e4ec6' } },
  '10': { value: { base: '#8347b9', _dark: '#9a5cd0' } },
  '11': { value: { base: '#8145b5', _dark: '#d19dff' } },
  '12': { value: { base: '#402060', _dark: '#ecd9fa' } },
  a1: { value: { base: '#aa00aa03', _dark: '#b412f90b' } },
  a2: { value: { base: '#8000e008', _dark: '#b744f714' } },
  a3: { value: { base: '#8e00f112', _dark: '#c150ff2d' } },
  a4: { value: { base: '#8d00e51d', _dark: '#bb53fd42' } },
  a5: { value: { base: '#8000db2a', _dark: '#be5cfd51' } },
  a6: { value: { base: '#7a01d03b', _dark: '#c16dfd61' } },
  a7: { value: { base: '#6d00c350', _dark: '#c378fd7a' } },
  a8: { value: { base: '#6600c06c', _dark: '#c47effa4' } },
  a9: { value: { base: '#5c00adb1', _dark: '#b661ffc2' } },
  a10: { value: { base: '#53009eb8', _dark: '#bc6fffcd' } },
  a11: { value: { base: '#52009aba', _dark: '#d19dff' } },
  a12: { value: { base: '#250049df', _dark: '#f1ddfffa' } },
  solid: {
    bg: {
      DEFAULT: { value: { base: '{colors.purple.9}', _dark: '{colors.purple.9}' } },
      hover: { value: { base: '{colors.purple.10}', _dark: '{colors.purple.10}' } },
    },
    fg: { DEFAULT: { value: { base: 'white', _dark: 'white' } } },
  },
  subtle: {
    bg: {
      DEFAULT: { value: { base: '{colors.purple.a3}', _dark: '{colors.purple.a3}' } },
      hover: { value: { base: '{colors.purple.a4}', _dark: '{colors.purple.a4}' } },
      active: { value: { base: '{colors.purple.a5}', _dark: '{colors.purple.a5}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.purple.a11}', _dark: '{colors.purple.a11}' } } },
  },
  surface: {
    bg: {
      DEFAULT: { value: { base: '{colors.purple.a2}', _dark: '{colors.purple.a2}' } },
      active: { value: { base: '{colors.purple.a3}', _dark: '{colors.purple.a3}' } },
    },
    border: {
      DEFAULT: { value: { base: '{colors.purple.a6}', _dark: '{colors.purple.a6}' } },
      hover: { value: { base: '{colors.purple.a7}', _dark: '{colors.purple.a7}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.purple.a11}', _dark: '{colors.purple.a11}' } } },
  },
  outline: {
    bg: {
      hover: { value: { base: '{colors.purple.a2}', _dark: '{colors.purple.a2}' } },
      active: { value: { base: '{colors.purple.a3}', _dark: '{colors.purple.a3}' } },
    },
    border: { DEFAULT: { value: { base: '{colors.purple.a7}', _dark: '{colors.purple.a7}' } } },
    fg: { DEFAULT: { value: { base: '{colors.purple.a11}', _dark: '{colors.purple.a11}' } } },
  },
  plain: {
    bg: {
      hover: { value: { base: '{colors.purple.a3}', _dark: '{colors.purple.a3}' } },
      active: { value: { base: '{colors.purple.a4}', _dark: '{colors.purple.a4}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.purple.a11}', _dark: '{colors.purple.a11}' } } },
  },
})
