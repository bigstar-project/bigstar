'use client'
import { Toggle as BaseToggle } from '@base-ui/react/toggle'
import { ToggleGroup as BaseToggleGroup } from '@base-ui/react/toggle-group'
import { cx } from '../../../styled-system/css'
import {
  toggle,
  toggleGroup,
  type ToggleVariantProps,
  type ToggleGroupVariantProps,
} from '../../../styled-system/recipes'
import { mergeClassName, paletteClass, type ColorPaletteProp } from './style-context'

export type ToggleProps<Value extends string = string> = BaseToggle.Props<Value> &
  ToggleVariantProps &
  ColorPaletteProp
export function Toggle<Value extends string = string>(props: ToggleProps<Value>) {
  const [variants, { colorPalette, ...rest }] = toggle.splitVariantProps(props)
  return (
    <BaseToggle
      {...rest}
      className={mergeClassName(cx(toggle(variants), paletteClass(colorPalette)), rest.className)}
    />
  )
}

export type ToggleGroupProps<Value extends string = string> = BaseToggleGroup.Props<Value> &
  ToggleGroupVariantProps &
  ColorPaletteProp
export function ToggleGroup<Value extends string = string>(props: ToggleGroupProps<Value>) {
  const [variants, { colorPalette, ...rest }] = toggleGroup.splitVariantProps(props)
  return (
    <BaseToggleGroup
      {...rest}
      className={mergeClassName(
        cx(toggleGroup(variants), paletteClass(colorPalette)),
        rest.className,
      )}
    />
  )
}
