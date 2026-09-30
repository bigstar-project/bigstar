// Adapted from Park UI. See ../PARK-UI-LICENSE.
import { defineSemanticTokens } from '@pandacss/dev'

export const lime = defineSemanticTokens.colors({
  '1': { value: { base: '#fcfdfa', _dark: '#11130c' } },
  '2': { value: { base: '#f8faf3', _dark: '#151a10' } },
  '3': { value: { base: '#eef6d6', _dark: '#1f2917' } },
  '4': { value: { base: '#e2f0bd', _dark: '#29371d' } },
  '5': { value: { base: '#d3e7a6', _dark: '#334423' } },
  '6': { value: { base: '#c2da91', _dark: '#3d522a' } },
  '7': { value: { base: '#abc978', _dark: '#496231' } },
  '8': { value: { base: '#8db654', _dark: '#577538' } },
  '9': { value: { base: '#bdee63', _dark: '#bdee63' } },
  '10': { value: { base: '#b0e64c', _dark: '#d4ff70' } },
  '11': { value: { base: '#5c7c2f', _dark: '#bde56c' } },
  '12': { value: { base: '#37401c', _dark: '#e3f7ba' } },
  a1: { value: { base: '#66990005', _dark: '#11bb0003' } },
  a2: { value: { base: '#6b95000c', _dark: '#78f7000a' } },
  a3: { value: { base: '#96c80029', _dark: '#9bfd4c1a' } },
  a4: { value: { base: '#8fc60042', _dark: '#a7fe5c29' } },
  a5: { value: { base: '#81bb0059', _dark: '#affe6537' } },
  a6: { value: { base: '#72aa006e', _dark: '#b2fe6d46' } },
  a7: { value: { base: '#61990087', _dark: '#b6ff6f57' } },
  a8: { value: { base: '#559200ab', _dark: '#b6fd6d6c' } },
  a9: { value: { base: '#93e4009c', _dark: '#caff69ed' } },
  a10: { value: { base: '#8fdc00b3', _dark: '#d4ff70' } },
  a11: { value: { base: '#375f00d0', _dark: '#d1fe77e4' } },
  a12: { value: { base: '#1e2900e3', _dark: '#e9febff7' } },
  solid: {
    bg: {
      DEFAULT: { value: { base: '{colors.lime.9}', _dark: '{colors.lime.9}' } },
      hover: { value: { base: '{colors.lime.10}', _dark: '{colors.lime.10}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.gray.12}', _dark: '{colors.gray.1}' } } },
  },
  subtle: {
    bg: {
      DEFAULT: { value: { base: '{colors.lime.a3}', _dark: '{colors.lime.a3}' } },
      hover: { value: { base: '{colors.lime.a4}', _dark: '{colors.lime.a4}' } },
      active: { value: { base: '{colors.lime.a5}', _dark: '{colors.lime.a5}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.lime.a11}', _dark: '{colors.lime.a11}' } } },
  },
  surface: {
    bg: {
      DEFAULT: { value: { base: '{colors.lime.a2}', _dark: '{colors.lime.a2}' } },
      active: { value: { base: '{colors.lime.a3}', _dark: '{colors.lime.a3}' } },
    },
    border: {
      DEFAULT: { value: { base: '{colors.lime.a6}', _dark: '{colors.lime.a6}' } },
      hover: { value: { base: '{colors.lime.a7}', _dark: '{colors.lime.a7}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.lime.a11}', _dark: '{colors.lime.a11}' } } },
  },
  outline: {
    bg: {
      hover: { value: { base: '{colors.lime.a2}', _dark: '{colors.lime.a2}' } },
      active: { value: { base: '{colors.lime.a3}', _dark: '{colors.lime.a3}' } },
    },
    border: { DEFAULT: { value: { base: '{colors.lime.a7}', _dark: '{colors.lime.a7}' } } },
    fg: { DEFAULT: { value: { base: '{colors.lime.a11}', _dark: '{colors.lime.a11}' } } },
  },
  plain: {
    bg: {
      hover: { value: { base: '{colors.lime.a3}', _dark: '{colors.lime.a3}' } },
      active: { value: { base: '{colors.lime.a4}', _dark: '{colors.lime.a4}' } },
    },
    fg: { DEFAULT: { value: { base: '{colors.lime.a11}', _dark: '{colors.lime.a11}' } } },
  },
})
