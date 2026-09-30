// Adapted from Park UI. See ../PARK-UI-LICENSE.
import { defineSemanticTokens } from '@pandacss/dev'

export const blue = defineSemanticTokens.colors({
  '1': { value: { base: '#fbfdff', _dark: '#0d1520' } },
  '2': { value: { base: '#f4faff', _dark: '#111927' } },
  '3': { value: { base: '#e6f4fe', _dark: '#0d2847' } },
  '4': { value: { base: '#d5efff', _dark: '#003362' } },
  '5': { value: { base: '#c2e5ff', _dark: '#004074' } },
  '6': { value: { base: '#acd8fc', _dark: '#104d87' } },
  '7': { value: { base: '#8ec8f6', _dark: '#205d9e' } },
  '8': { value: { base: '#5eb1ef', _dark: '#2870bd' } },
  '9': { value: { base: '#0090ff', _dark: '#0090ff' } },
  '10': { value: { base: '#0588f0', _dark: '#3b9eff' } },
  '11': { value: { base: '#0d74ce', _dark: '#70b8ff' } },
  '12': { value: { base: '#113264', _dark: '#c2e6ff' } },
  a1: { value: { base: '#0080ff04', _dark: '#004df211' } },
  a2: { value: { base: '#008cff0b', _dark: '#1166fb18' } },
  a3: { value: { base: '#008ff519', _dark: '#0077ff3a' } },
  a4: { value: { base: '#009eff2a', _dark: '#0075ff57' } },
  a5: { value: { base: '#0093ff3d', _dark: '#0081fd6b' } },
  a6: { value: { base: '#0088f653', _dark: '#0f89fd7f' } },
  a7: { value: { base: '#0083eb71', _dark: '#2a91fe98' } },
  a8: { value: { base: '#0084e6a1', _dark: '#3094feb9' } },
  a9: { value: { base: '#0090ff', _dark: '#0090ff' } },
  a10: { value: { base: '#0086f0fa', _dark: '#3b9eff' } },
  a11: { value: { base: '#006dcbf2', _dark: '#70b8ff' } },
  a12: { value: { base: '#002359ee', _dark: '#c2e6ff' } },
  solid: {
    bg: {
      DEFAULT: { value: { base: '{colors.blue.9}', _dark: '{colors.blue.9}' } },
      hover: { value: { base: '{colors.blue.10}', _dark: '{colors.blue.10}' } },
    },
    fg: { DEFAULT: { value: { base: 'white', _dark: 'white' } } },
  },
  subtle: {
    bg: {
      DEFAULT: { value: { base: '{colors.blue.a3}', _dark: '{colors.blue.a3}' } },
      hover: { value: { base: '{colors.blue.a4}', _dark: '{colors.blue.a4}' } },
      active: { value: { base: '{colors.blue.a5}', _dark: '{colors.blue.a5}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.blue.a11}', _dark: '{colors.blue.a11}' } } },
  },
  surface: {
    bg: {
      DEFAULT: { value: { base: '{colors.blue.a2}', _dark: '{colors.blue.a2}' } },
      active: { value: { base: '{colors.blue.a3}', _dark: '{colors.blue.a3}' } },
    },
    border: {
      DEFAULT: { value: { base: '{colors.blue.a6}', _dark: '{colors.blue.a6}' } },
      hover: { value: { base: '{colors.blue.a7}', _dark: '{colors.blue.a7}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.blue.a11}', _dark: '{colors.blue.a11}' } } },
  },
  outline: {
    bg: {
      hover: { value: { base: '{colors.blue.a2}', _dark: '{colors.blue.a2}' } },
      active: { value: { base: '{colors.blue.a3}', _dark: '{colors.blue.a3}' } },
    },
    border: { DEFAULT: { value: { base: '{colors.blue.a7}', _dark: '{colors.blue.a7}' } } },
    fg: { DEFAULT: { value: { base: '{colors.blue.a11}', _dark: '{colors.blue.a11}' } } },
  },
  plain: {
    bg: {
      hover: { value: { base: '{colors.blue.a3}', _dark: '{colors.blue.a3}' } },
      active: { value: { base: '{colors.blue.a4}', _dark: '{colors.blue.a4}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.blue.a11}', _dark: '{colors.blue.a11}' } } },
  },
})
