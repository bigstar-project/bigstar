// Adapted from Park UI. See ../PARK-UI-LICENSE.
import { defineSemanticTokens } from '@pandacss/dev'

export const amber = defineSemanticTokens.colors({
  '1': { value: { base: '#fefdfb', _dark: '#16120c' } },
  '2': { value: { base: '#fefbe9', _dark: '#1d180f' } },
  '3': { value: { base: '#fff7c2', _dark: '#302008' } },
  '4': { value: { base: '#ffee9c', _dark: '#3f2700' } },
  '5': { value: { base: '#fbe577', _dark: '#4d3000' } },
  '6': { value: { base: '#f3d673', _dark: '#5c3d05' } },
  '7': { value: { base: '#e9c162', _dark: '#714f19' } },
  '8': { value: { base: '#e2a336', _dark: '#8f6424' } },
  '9': { value: { base: '#ffc53d', _dark: '#ffc53d' } },
  '10': { value: { base: '#ffba18', _dark: '#ffd60a' } },
  '11': { value: { base: '#ab6400', _dark: '#ffca16' } },
  '12': { value: { base: '#4f3422', _dark: '#ffe7b3' } },
  a1: { value: { base: '#c0800004', _dark: '#e63c0006' } },
  a2: { value: { base: '#f4d10016', _dark: '#fd9b000d' } },
  a3: { value: { base: '#ffde003d', _dark: '#fa820022' } },
  a4: { value: { base: '#ffd40063', _dark: '#fc820032' } },
  a5: { value: { base: '#f8cf0088', _dark: '#fd8b0041' } },
  a6: { value: { base: '#eab5008c', _dark: '#fd9b0051' } },
  a7: { value: { base: '#dc9b009d', _dark: '#ffab2567' } },
  a8: { value: { base: '#da8a00c9', _dark: '#ffae3587' } },
  a9: { value: { base: '#ffb300c2', _dark: '#ffc53d' } },
  a10: { value: { base: '#ffb300e7', _dark: '#ffd60a' } },
  a11: { value: { base: '#ab6400', _dark: '#ffca16' } },
  a12: { value: { base: '#341500dd', _dark: '#ffe7b3' } },
  solid: {
    bg: {
      DEFAULT: { value: { base: '{colors.amber.9}', _dark: '{colors.amber.9}' } },
      hover: { value: { base: '{colors.amber.10}', _dark: '{colors.amber.10}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.gray.12}', _dark: '{colors.gray.1}' } } },
  },
  subtle: {
    bg: {
      DEFAULT: { value: { base: '{colors.amber.a3}', _dark: '{colors.amber.a3}' } },
      hover: { value: { base: '{colors.amber.a4}', _dark: '{colors.amber.a4}' } },
      active: { value: { base: '{colors.amber.a5}', _dark: '{colors.amber.a5}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.amber.a11}', _dark: '{colors.amber.a11}' } } },
  },
  surface: {
    bg: {
      DEFAULT: { value: { base: '{colors.amber.a2}', _dark: '{colors.amber.a2}' } },
      active: { value: { base: '{colors.amber.a3}', _dark: '{colors.amber.a3}' } },
    },
    border: {
      DEFAULT: { value: { base: '{colors.amber.a6}', _dark: '{colors.amber.a6}' } },
      hover: { value: { base: '{colors.amber.a7}', _dark: '{colors.amber.a7}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.amber.a11}', _dark: '{colors.amber.a11}' } } },
  },
  outline: {
    bg: {
      hover: { value: { base: '{colors.amber.a2}', _dark: '{colors.amber.a2}' } },
      active: { value: { base: '{colors.amber.a3}', _dark: '{colors.amber.a3}' } },
    },
    border: { DEFAULT: { value: { base: '{colors.amber.a7}', _dark: '{colors.amber.a7}' } } },
    fg: { DEFAULT: { value: { base: '{colors.amber.a11}', _dark: '{colors.amber.a11}' } } },
  },
  plain: {
    bg: {
      hover: { value: { base: '{colors.amber.a3}', _dark: '{colors.amber.a3}' } },
      active: { value: { base: '{colors.amber.a4}', _dark: '{colors.amber.a4}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.amber.a11}', _dark: '{colors.amber.a11}' } } },
  },
})
