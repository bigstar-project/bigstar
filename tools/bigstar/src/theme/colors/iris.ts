// Adapted from Park UI. See ../PARK-UI-LICENSE.
import { defineSemanticTokens } from '@pandacss/dev'

export const iris = defineSemanticTokens.colors({
  '1': { value: { base: '#fdfdff', _dark: '#13131e' } },
  '2': { value: { base: '#f8f8ff', _dark: '#171625' } },
  '3': { value: { base: '#f0f1fe', _dark: '#202248' } },
  '4': { value: { base: '#e6e7ff', _dark: '#262a65' } },
  '5': { value: { base: '#dadcff', _dark: '#303374' } },
  '6': { value: { base: '#cbcdff', _dark: '#3d3e82' } },
  '7': { value: { base: '#b8baf8', _dark: '#4a4a95' } },
  '8': { value: { base: '#9b9ef0', _dark: '#5958b1' } },
  '9': { value: { base: '#5b5bd6', _dark: '#5b5bd6' } },
  '10': { value: { base: '#5151cd', _dark: '#6e6ade' } },
  '11': { value: { base: '#5753c6', _dark: '#b1a9ff' } },
  '12': { value: { base: '#272962', _dark: '#e0dffe' } },
  a1: { value: { base: '#0000ff02', _dark: '#3636fe0e' } },
  a2: { value: { base: '#0000ff07', _dark: '#564bf916' } },
  a3: { value: { base: '#0011ee0f', _dark: '#525bff3b' } },
  a4: { value: { base: '#000bff19', _dark: '#4d58ff5a' } },
  a5: { value: { base: '#000eff25', _dark: '#5b62fd6b' } },
  a6: { value: { base: '#000aff34', _dark: '#6d6ffd7a' } },
  a7: { value: { base: '#0008e647', _dark: '#7777fe8e' } },
  a8: { value: { base: '#0008d964', _dark: '#7b7afeac' } },
  a9: { value: { base: '#0000c0a4', _dark: '#6a6afed4' } },
  a10: { value: { base: '#0000b6ae', _dark: '#7d79ffdc' } },
  a11: { value: { base: '#0600abac', _dark: '#b1a9ff' } },
  a12: { value: { base: '#000246d8', _dark: '#e1e0fffe' } },
  solid: {
    bg: {
      DEFAULT: { value: { base: '{colors.iris.9}', _dark: '{colors.iris.9}' } },
      hover: { value: { base: '{colors.iris.10}', _dark: '{colors.iris.10}' } },
    },
    fg: { DEFAULT: { value: { base: 'white', _dark: 'white' } } },
  },
  subtle: {
    bg: {
      DEFAULT: { value: { base: '{colors.iris.a3}', _dark: '{colors.iris.a3}' } },
      hover: { value: { base: '{colors.iris.a4}', _dark: '{colors.iris.a4}' } },
      active: { value: { base: '{colors.iris.a5}', _dark: '{colors.iris.a5}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.iris.a11}', _dark: '{colors.iris.a11}' } } },
  },
  surface: {
    bg: {
      DEFAULT: { value: { base: '{colors.iris.a2}', _dark: '{colors.iris.a2}' } },
      active: { value: { base: '{colors.iris.a3}', _dark: '{colors.iris.a3}' } },
    },
    border: {
      DEFAULT: { value: { base: '{colors.iris.a6}', _dark: '{colors.iris.a6}' } },
      hover: { value: { base: '{colors.iris.a7}', _dark: '{colors.iris.a7}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.iris.a11}', _dark: '{colors.iris.a11}' } } },
  },
  outline: {
    bg: {
      hover: { value: { base: '{colors.iris.a2}', _dark: '{colors.iris.a2}' } },
      active: { value: { base: '{colors.iris.a3}', _dark: '{colors.iris.a3}' } },
    },
    border: { DEFAULT: { value: { base: '{colors.iris.a7}', _dark: '{colors.iris.a7}' } } },
    fg: { DEFAULT: { value: { base: '{colors.iris.a11}', _dark: '{colors.iris.a11}' } } },
  },
  plain: {
    bg: {
      hover: { value: { base: '{colors.iris.a3}', _dark: '{colors.iris.a3}' } },
      active: { value: { base: '{colors.iris.a4}', _dark: '{colors.iris.a4}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.iris.a11}', _dark: '{colors.iris.a11}' } } },
  },
})
