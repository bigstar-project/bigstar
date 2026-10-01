import { defineRecipe } from '@pandacss/dev'
import { fieldControl, fieldVariants } from '../shared'

export const textarea = defineRecipe({
  className: 'kiso-textarea',
  jsx: ['Textarea'],
  base: { ...fieldControl, minHeight: '20', resize: 'vertical' },
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
        _invalid: { borderColor: 'error' },
      },
    },
    size: {
      xs: { textStyle: 'sm', px: '2', py: '5px', scrollPaddingBottom: '5px' },
      sm: { textStyle: 'sm', px: '2.5', py: '7px', scrollPaddingBottom: '7px' },
      md: { textStyle: 'md', px: '3', py: '7px', scrollPaddingBottom: '7px' },
      lg: { textStyle: 'md', px: '3.5', py: '9px', scrollPaddingBottom: '9px' },
      xl: { textStyle: 'lg', px: '4', py: '9px', scrollPaddingBottom: '9px' },
    },
  },
})
