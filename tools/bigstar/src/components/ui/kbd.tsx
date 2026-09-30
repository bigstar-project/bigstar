import type { ComponentPropsWithRef } from 'react'
import { cx } from '../../../styled-system/css'
import { kbd, type KbdVariantProps } from '../../../styled-system/recipes'
import { paletteClass, type ColorPaletteProp } from './style-context'

export type KbdProps = ComponentPropsWithRef<'kbd'> & KbdVariantProps & ColorPaletteProp
export function Kbd(props: KbdProps) {
  const [variants, { colorPalette, ...rest }] = kbd.splitVariantProps(props)
  return <kbd {...rest} className={cx(kbd(variants), paletteClass(colorPalette), rest.className)} />
}
