'use client'
import { RadioGroup as BaseGroup } from '@base-ui/react/radio-group'
import { Radio as Base } from '@base-ui/react/radio'
import { radioGroup, type RadioGroupVariantProps } from '../../../styled-system/recipes'
import { cx } from '../../../styled-system/css'
import {
  createStyleContext,
  mergeClassName,
  paletteClass,
  type ColorPaletteProp,
} from './style-context'
const { Provider, withContext } = createStyleContext(radioGroup)
export function Root<Value>(
  props: BaseGroup.Props<Value> & RadioGroupVariantProps & ColorPaletteProp,
) {
  const [variants, { colorPalette, ...rest }] = radioGroup.splitVariantProps(props)
  return (
    <Provider {...variants}>
      <BaseGroup
        {...rest}
        data-slot="root"
        className={mergeClassName(
          cx(radioGroup(variants).root, paletteClass(colorPalette)),
          rest.className,
        )}
      />
    </Provider>
  )
}
export const Item = withContext(Base.Root, 'item')
export const Indicator = withContext(Base.Indicator, 'indicator')
/** Wraps an Item and its text; dims with a disabled Item. */
export const Label = withContext('label', 'label')
