'use client'
import { Input as BaseInput } from '@base-ui/react/input'
import { cx } from '../../../styled-system/css'
import { input, type InputVariantProps } from '../../../styled-system/recipes'
import { mergeClassName, paletteClass, type ColorPaletteProp } from './style-context'

export type InputProps = Omit<BaseInput.Props, 'size'> & InputVariantProps & ColorPaletteProp
export function Input(props: InputProps) {
  const [variants, { colorPalette, ...rest }] = input.splitVariantProps(props)
  return (
    <BaseInput
      {...rest}
      className={mergeClassName(cx(input(variants), paletteClass(colorPalette)), rest.className)}
    />
  )
}
