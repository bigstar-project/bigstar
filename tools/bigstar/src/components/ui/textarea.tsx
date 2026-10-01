import type { ComponentPropsWithRef } from 'react'
import { cx } from '../../../styled-system/css'
import { textarea, type TextareaVariantProps } from '../../../styled-system/recipes'
import { paletteClass, type ColorPaletteProp } from './style-context'

export type TextareaProps = ComponentPropsWithRef<'textarea'> &
  TextareaVariantProps &
  ColorPaletteProp
export function Textarea(props: TextareaProps) {
  const [variants, { colorPalette, ...rest }] = textarea.splitVariantProps(props)
  return (
    <textarea
      {...rest}
      className={cx(textarea(variants), paletteClass(colorPalette), rest.className)}
    />
  )
}
