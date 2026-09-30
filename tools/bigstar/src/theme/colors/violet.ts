// Adapted from Park UI. See ../PARK-UI-LICENSE.
import { defineSemanticTokens } from '@pandacss/dev'

export const violet = defineSemanticTokens.colors({
  '1': { value: { base: '#fdfcfe', _dark: '#14121f' } },
  '2': { value: { base: '#faf8ff', _dark: '#1b1525' } },
  '3': { value: { base: '#f4f0fe', _dark: '#291f43' } },
  '4': { value: { base: '#ebe4ff', _dark: '#33255b' } },
  '5': { value: { base: '#e1d9ff', _dark: '#3c2e69' } },
  '6': { value: { base: '#d4cafe', _dark: '#473876' } },
  '7': { value: { base: '#c2b5f5', _dark: '#56468b' } },
  '8': { value: { base: '#aa99ec', _dark: '#6958ad' } },
  '9': { value: { base: '#6e56cf', _dark: '#6e56cf' } },
  '10': { value: { base: '#654dc4', _dark: '#7d66d9' } },
  '11': { value: { base: '#6550b9', _dark: '#baa7ff' } },
  '12': { value: { base: '#2f265f', _dark: '#e2ddfe' } },
  a1: { value: { base: '#5500aa03', _dark: '#4422ff0f' } },
  a2: { value: { base: '#4900ff07', _dark: '#853ff916' } },
  a3: { value: { base: '#4400ee0f', _dark: '#8354fe36' } },
  a4: { value: { base: '#4300ff1b', _dark: '#7d51fd50' } },
  a5: { value: { base: '#3600ff26', _dark: '#845ffd5f' } },
  a6: { value: { base: '#3100fb35', _dark: '#8f6cfd6d' } },
  a7: { value: { base: '#2d01dd4a', _dark: '#9879ff83' } },
  a8: { value: { base: '#2b00d066', _dark: '#977dfea8' } },
  a9: { value: { base: '#2400b7a9', _dark: '#8668ffcc' } },
  a10: { value: { base: '#2300abb2', _dark: '#9176fed7' } },
  a11: { value: { base: '#1f0099af', _dark: '#baa7ff' } },
  a12: { value: { base: '#0b0043d9', _dark: '#e3defffe' } },
  solid: {
    bg: {
      DEFAULT: { value: { base: '{colors.violet.9}', _dark: '{colors.violet.9}' } },
      hover: { value: { base: '{colors.violet.10}', _dark: '{colors.violet.10}' } },
    },
    fg: { DEFAULT: { value: { base: 'white', _dark: 'white' } } },
  },
  subtle: {
    bg: {
      DEFAULT: { value: { base: '{colors.violet.a3}', _dark: '{colors.violet.a3}' } },
      hover: { value: { base: '{colors.violet.a4}', _dark: '{colors.violet.a4}' } },
      active: { value: { base: '{colors.violet.a5}', _dark: '{colors.violet.a5}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.violet.a11}', _dark: '{colors.violet.a11}' } } },
  },
  surface: {
    bg: {
      DEFAULT: { value: { base: '{colors.violet.a2}', _dark: '{colors.violet.a2}' } },
      active: { value: { base: '{colors.violet.a3}', _dark: '{colors.violet.a3}' } },
    },
    border: {
      DEFAULT: { value: { base: '{colors.violet.a6}', _dark: '{colors.violet.a6}' } },
      hover: { value: { base: '{colors.violet.a7}', _dark: '{colors.violet.a7}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.violet.a11}', _dark: '{colors.violet.a11}' } } },
  },
  outline: {
    bg: {
      hover: { value: { base: '{colors.violet.a2}', _dark: '{colors.violet.a2}' } },
      active: { value: { base: '{colors.violet.a3}', _dark: '{colors.violet.a3}' } },
    },
    border: { DEFAULT: { value: { base: '{colors.violet.a7}', _dark: '{colors.violet.a7}' } } },
    fg: { DEFAULT: { value: { base: '{colors.violet.a11}', _dark: '{colors.violet.a11}' } } },
  },
  plain: {
    bg: {
      hover: { value: { base: '{colors.violet.a3}', _dark: '{colors.violet.a3}' } },
      active: { value: { base: '{colors.violet.a4}', _dark: '{colors.violet.a4}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.violet.a11}', _dark: '{colors.violet.a11}' } } },
  },
})
