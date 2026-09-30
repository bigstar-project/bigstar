import { defineRecipe } from '@pandacss/dev'
import { fieldControl, fieldVariants } from '../shared'

export const input = defineRecipe({
  className: 'kiso-input',
  jsx: ['Input'],
  base: {
    ...fieldControl,
    height: 'var(--input-height)',
    minHeight: 'var(--input-height)',
    textAlign: 'start',
  },
  defaultVariants: { size: 'md', variant: 'outline' },
  variants: {
    variant: {
      ...fieldVariants,
      flushed: {
        bg: 'transparent',
        borderBottomWidth: '1px',
        borderBottomColor: 'gray.outline.border',
        borderRadius: '0',
        px: '0!',
        _focusVisible: {
          borderColor: 'colorPalette.solid.bg',
          boxShadow: '0 1px 0 0 {colors.colorPalette.solid.bg}',
        },
        _invalid: {
          borderColor: 'error',
          _focusVisible: { borderColor: 'error', boxShadow: '0 1px 0 0 {colors.error}' },
        },
      },
    },
    size: {
      '2xs': { textStyle: 'xs', px: '1.5', '--input-height': 'sizes.7' },
      xs: { textStyle: 'sm', px: '2', '--input-height': 'sizes.8' },
      sm: { textStyle: 'sm', px: '2.5', '--input-height': 'sizes.9' },
      md: { textStyle: 'md', px: '3', '--input-height': 'sizes.10' },
      lg: { textStyle: 'md', px: '3.5', '--input-height': 'sizes.11' },
      xl: { textStyle: 'lg', px: '4', '--input-height': 'sizes.12' },
      '2xl': { textStyle: '3xl', px: '4.5', '--input-height': 'sizes.16' },
    },
  },
})
