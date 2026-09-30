// Adapted from Park UI. See ../PARK-UI-LICENSE.
import { defineSemanticTokens } from '@pandacss/dev'

export const crimson = defineSemanticTokens.colors({
  '1': { value: { base: '#fffcfd', _dark: '#191114' } },
  '2': { value: { base: '#fef7f9', _dark: '#201318' } },
  '3': { value: { base: '#ffe9f0', _dark: '#381525' } },
  '4': { value: { base: '#fedce7', _dark: '#4d122f' } },
  '5': { value: { base: '#facedd', _dark: '#5c1839' } },
  '6': { value: { base: '#f3bed1', _dark: '#6d2545' } },
  '7': { value: { base: '#eaacc3', _dark: '#873356' } },
  '8': { value: { base: '#e093b2', _dark: '#b0436e' } },
  '9': { value: { base: '#e93d82', _dark: '#e93d82' } },
  '10': { value: { base: '#df3478', _dark: '#ee518a' } },
  '11': { value: { base: '#cb1d63', _dark: '#ff92ad' } },
  '12': { value: { base: '#621639', _dark: '#fdd3e8' } },
  a1: { value: { base: '#ff005503', _dark: '#f4126709' } },
  a2: { value: { base: '#e0004008', _dark: '#f22f7a11' } },
  a3: { value: { base: '#ff005216', _dark: '#fe2a8b2a' } },
  a4: { value: { base: '#f8005123', _dark: '#fd158741' } },
  a5: { value: { base: '#e5004f31', _dark: '#fd278f51' } },
  a6: { value: { base: '#d0004b41', _dark: '#fe459763' } },
  a7: { value: { base: '#bf004753', _dark: '#fd559b7f' } },
  a8: { value: { base: '#b6004a6c', _dark: '#fe5b9bab' } },
  a9: { value: { base: '#e2005bc2', _dark: '#fe418de8' } },
  a10: { value: { base: '#d70056cb', _dark: '#ff5693ed' } },
  a11: { value: { base: '#c4004fe2', _dark: '#ff92ad' } },
  a12: { value: { base: '#530026e9', _dark: '#ffd5eafd' } },
  solid: {
    bg: {
      DEFAULT: { value: { base: '{colors.crimson.9}', _dark: '{colors.crimson.9}' } },
      hover: { value: { base: '{colors.crimson.10}', _dark: '{colors.crimson.10}' } },
    },
    fg: { DEFAULT: { value: { base: 'white', _dark: 'white' } } },
  },
  subtle: {
    bg: {
      DEFAULT: { value: { base: '{colors.crimson.a3}', _dark: '{colors.crimson.a3}' } },
      hover: { value: { base: '{colors.crimson.a4}', _dark: '{colors.crimson.a4}' } },
      active: { value: { base: '{colors.crimson.a5}', _dark: '{colors.crimson.a5}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.crimson.a11}', _dark: '{colors.crimson.a11}' } } },
  },
  surface: {
    bg: {
      DEFAULT: { value: { base: '{colors.crimson.a2}', _dark: '{colors.crimson.a2}' } },
      active: { value: { base: '{colors.crimson.a3}', _dark: '{colors.crimson.a3}' } },
    },
    border: {
      DEFAULT: { value: { base: '{colors.crimson.a6}', _dark: '{colors.crimson.a6}' } },
      hover: { value: { base: '{colors.crimson.a7}', _dark: '{colors.crimson.a7}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.crimson.a11}', _dark: '{colors.crimson.a11}' } } },
  },
  outline: {
    bg: {
      hover: { value: { base: '{colors.crimson.a2}', _dark: '{colors.crimson.a2}' } },
      active: { value: { base: '{colors.crimson.a3}', _dark: '{colors.crimson.a3}' } },
    },
    border: { DEFAULT: { value: { base: '{colors.crimson.a7}', _dark: '{colors.crimson.a7}' } } },
    fg: { DEFAULT: { value: { base: '{colors.crimson.a11}', _dark: '{colors.crimson.a11}' } } },
  },
  plain: {
    bg: {
      hover: { value: { base: '{colors.crimson.a3}', _dark: '{colors.crimson.a3}' } },
      active: { value: { base: '{colors.crimson.a4}', _dark: '{colors.crimson.a4}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.crimson.a11}', _dark: '{colors.crimson.a11}' } } },
  },
})
