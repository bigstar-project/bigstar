// Adapted from Park UI. See ../PARK-UI-LICENSE.
import { defineSemanticTokens } from '@pandacss/dev'

export const mint = defineSemanticTokens.colors({
  '1': { value: { base: '#f9fefd', _dark: '#0e1515' } },
  '2': { value: { base: '#f2fbf9', _dark: '#0f1b1b' } },
  '3': { value: { base: '#ddf9f2', _dark: '#092c2b' } },
  '4': { value: { base: '#c8f4e9', _dark: '#003a38' } },
  '5': { value: { base: '#b3ecde', _dark: '#004744' } },
  '6': { value: { base: '#9ce0d0', _dark: '#105650' } },
  '7': { value: { base: '#7ecfbd', _dark: '#1e685f' } },
  '8': { value: { base: '#4cbba5', _dark: '#277f70' } },
  '9': { value: { base: '#86ead4', _dark: '#86ead4' } },
  '10': { value: { base: '#7de0cb', _dark: '#a8f5e5' } },
  '11': { value: { base: '#027864', _dark: '#58d5ba' } },
  '12': { value: { base: '#16433c', _dark: '#c4f5e1' } },
  a1: { value: { base: '#00d5aa06', _dark: '#00dede05' } },
  a2: { value: { base: '#00b18a0d', _dark: '#00f9f90b' } },
  a3: { value: { base: '#00d29e22', _dark: '#00fff61d' } },
  a4: { value: { base: '#00cc9937', _dark: '#00fff42c' } },
  a5: { value: { base: '#00c0914c', _dark: '#00fff23a' } },
  a6: { value: { base: '#00b08663', _dark: '#0effeb4a' } },
  a7: { value: { base: '#00a17d81', _dark: '#34fde55e' } },
  a8: { value: { base: '#009e7fb3', _dark: '#41ffdf76' } },
  a9: { value: { base: '#00d3a579', _dark: '#92ffe7e9' } },
  a10: { value: { base: '#00c39982', _dark: '#aefeedf5' } },
  a11: { value: { base: '#007763fd', _dark: '#67ffded2' } },
  a12: { value: { base: '#00312ae9', _dark: '#cbfee9f5' } },
  solid: {
    bg: {
      DEFAULT: { value: { base: '{colors.mint.9}', _dark: '{colors.mint.9}' } },
      hover: { value: { base: '{colors.mint.10}', _dark: '{colors.mint.10}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.gray.12}', _dark: '{colors.gray.1}' } } },
  },
  subtle: {
    bg: {
      DEFAULT: { value: { base: '{colors.mint.a3}', _dark: '{colors.mint.a3}' } },
      hover: { value: { base: '{colors.mint.a4}', _dark: '{colors.mint.a4}' } },
      active: { value: { base: '{colors.mint.a5}', _dark: '{colors.mint.a5}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.mint.a11}', _dark: '{colors.mint.a11}' } } },
  },
  surface: {
    bg: {
      DEFAULT: { value: { base: '{colors.mint.a2}', _dark: '{colors.mint.a2}' } },
      active: { value: { base: '{colors.mint.a3}', _dark: '{colors.mint.a3}' } },
    },
    border: {
      DEFAULT: { value: { base: '{colors.mint.a6}', _dark: '{colors.mint.a6}' } },
      hover: { value: { base: '{colors.mint.a7}', _dark: '{colors.mint.a7}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.mint.a11}', _dark: '{colors.mint.a11}' } } },
  },
  outline: {
    bg: {
      hover: { value: { base: '{colors.mint.a2}', _dark: '{colors.mint.a2}' } },
      active: { value: { base: '{colors.mint.a3}', _dark: '{colors.mint.a3}' } },
    },
    border: { DEFAULT: { value: { base: '{colors.mint.a7}', _dark: '{colors.mint.a7}' } } },
    fg: { DEFAULT: { value: { base: '{colors.mint.a11}', _dark: '{colors.mint.a11}' } } },
  },
  plain: {
    bg: {
      hover: { value: { base: '{colors.mint.a3}', _dark: '{colors.mint.a3}' } },
      active: { value: { base: '{colors.mint.a4}', _dark: '{colors.mint.a4}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.mint.a11}', _dark: '{colors.mint.a11}' } } },
  },
})
