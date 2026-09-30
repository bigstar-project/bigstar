// Adapted from Park UI. See ../PARK-UI-LICENSE.
import { defineSemanticTokens } from '@pandacss/dev'

export const ruby = defineSemanticTokens.colors({
  '1': { value: { base: '#fffcfd', _dark: '#191113' } },
  '2': { value: { base: '#fff7f8', _dark: '#1e1517' } },
  '3': { value: { base: '#feeaed', _dark: '#3a141e' } },
  '4': { value: { base: '#ffdce1', _dark: '#4e1325' } },
  '5': { value: { base: '#ffced6', _dark: '#5e1a2e' } },
  '6': { value: { base: '#f8bfc8', _dark: '#6f2539' } },
  '7': { value: { base: '#efacb8', _dark: '#883447' } },
  '8': { value: { base: '#e592a3', _dark: '#b3445a' } },
  '9': { value: { base: '#e54666', _dark: '#e54666' } },
  '10': { value: { base: '#dc3b5d', _dark: '#ec5a72' } },
  '11': { value: { base: '#ca244d', _dark: '#ff949d' } },
  '12': { value: { base: '#64172b', _dark: '#fed2e1' } },
  a1: { value: { base: '#ff005503', _dark: '#f4124a09' } },
  a2: { value: { base: '#ff002008', _dark: '#fe5a7f0e' } },
  a3: { value: { base: '#f3002515', _dark: '#ff235d2c' } },
  a4: { value: { base: '#ff002523', _dark: '#fd195e42' } },
  a5: { value: { base: '#ff002a31', _dark: '#fe2d6b53' } },
  a6: { value: { base: '#e4002440', _dark: '#ff447665' } },
  a7: { value: { base: '#ce002553', _dark: '#ff577d80' } },
  a8: { value: { base: '#c300286d', _dark: '#ff5c7cae' } },
  a9: { value: { base: '#db002cb9', _dark: '#fe4c70e4' } },
  a10: { value: { base: '#d2002cc4', _dark: '#ff617beb' } },
  a11: { value: { base: '#c10030db', _dark: '#ff949d' } },
  a12: { value: { base: '#550016e8', _dark: '#ffd3e2fe' } },
  solid: {
    bg: {
      DEFAULT: { value: { base: '{colors.ruby.9}', _dark: '{colors.ruby.9}' } },
      hover: { value: { base: '{colors.ruby.10}', _dark: '{colors.ruby.10}' } },
    },
    fg: { DEFAULT: { value: { base: 'white', _dark: 'white' } } },
  },
  subtle: {
    bg: {
      DEFAULT: { value: { base: '{colors.ruby.a3}', _dark: '{colors.ruby.a3}' } },
      hover: { value: { base: '{colors.ruby.a4}', _dark: '{colors.ruby.a4}' } },
      active: { value: { base: '{colors.ruby.a5}', _dark: '{colors.ruby.a5}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.ruby.a11}', _dark: '{colors.ruby.a11}' } } },
  },
  surface: {
    bg: {
      DEFAULT: { value: { base: '{colors.ruby.a2}', _dark: '{colors.ruby.a2}' } },
      active: { value: { base: '{colors.ruby.a3}', _dark: '{colors.ruby.a3}' } },
    },
    border: {
      DEFAULT: { value: { base: '{colors.ruby.a6}', _dark: '{colors.ruby.a6}' } },
      hover: { value: { base: '{colors.ruby.a7}', _dark: '{colors.ruby.a7}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.ruby.a11}', _dark: '{colors.ruby.a11}' } } },
  },
  outline: {
    bg: {
      hover: { value: { base: '{colors.ruby.a2}', _dark: '{colors.ruby.a2}' } },
      active: { value: { base: '{colors.ruby.a3}', _dark: '{colors.ruby.a3}' } },
    },
    border: { DEFAULT: { value: { base: '{colors.ruby.a7}', _dark: '{colors.ruby.a7}' } } },
    fg: { DEFAULT: { value: { base: '{colors.ruby.a11}', _dark: '{colors.ruby.a11}' } } },
  },
  plain: {
    bg: {
      hover: { value: { base: '{colors.ruby.a3}', _dark: '{colors.ruby.a3}' } },
      active: { value: { base: '{colors.ruby.a4}', _dark: '{colors.ruby.a4}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.ruby.a11}', _dark: '{colors.ruby.a11}' } } },
  },
})
