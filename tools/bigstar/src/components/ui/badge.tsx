import type { ComponentPropsWithRef } from 'react'
import { cx } from '../../../styled-system/css'
import { badge, type BadgeVariantProps } from '../../../styled-system/recipes'
import { paletteClass, type ColorPaletteProp } from './style-context'

export type BadgeProps = ComponentPropsWithRef<'span'> & BadgeVariantProps & ColorPaletteProp
export function Badge(props: BadgeProps) {
  const [variants, { colorPalette, ...rest }] = badge.splitVariantProps(props)
  return (
    <span {...rest} className={cx(badge(variants), paletteClass(colorPalette), rest.className)} />
  )
}
