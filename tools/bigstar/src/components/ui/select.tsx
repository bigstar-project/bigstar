'use client'
import { Select as Base } from '@base-ui/react/select'
import { select, type SelectVariantProps } from '../../../styled-system/recipes'
import { createStyleContext, type ColorPaletteProp } from './style-context'
import { CheckIcon, ChevronDownIcon, withIcon } from './icons'
const { Provider, withContext } = createStyleContext(select)

// Keep value inference, including multiple selection; a generic HOC would erase it.
export function Root<Value, Multiple extends boolean | undefined = false>({
  size,
  variant,
  colorPalette,
  ...props
}: Base.Root.Props<Value, Multiple> & SelectVariantProps & ColorPaletteProp) {
  return (
    <Provider size={size} variant={variant} colorPalette={colorPalette}>
      <Base.Root {...props} />
    </Provider>
  )
}
export const Label = withContext(Base.Label, 'label')
export const Trigger = withContext(Base.Trigger, 'trigger')
export const Value = withContext(Base.Value, 'value')
export const Icon = withIcon(withContext(Base.Icon, 'icon'), <ChevronDownIcon />)
export const Backdrop = withContext(Base.Backdrop, 'backdrop')
export const Positioner = withContext(Base.Positioner, 'positioner')
export const Popup = withContext(Base.Popup, 'popup')
export const List = withContext(Base.List, 'list')
export const Item = withContext(Base.Item, 'item')
export const ItemText = withContext(Base.ItemText, 'itemText')
export const ItemIndicator = withIcon(
  withContext(Base.ItemIndicator, 'itemIndicator'),
  <CheckIcon />,
)
export const GroupLabel = withContext(Base.GroupLabel, 'groupLabel')
export const Separator = withContext(Base.Separator, 'separator')
export const ScrollUpArrow = withContext(Base.ScrollUpArrow, 'scrollUpArrow')
export const ScrollDownArrow = withContext(Base.ScrollDownArrow, 'scrollDownArrow')
/** Shown only with alignItemWithTrigger={false}; the list then scrolls instead of the popup. */
export const Arrow = withContext(Base.Arrow, 'arrow')
export const Group = Base.Group
export const Portal = Base.Portal
