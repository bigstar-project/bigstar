'use client'
import { Dialog as Base } from '@base-ui/react/dialog'
import { dialog, type DialogVariantProps } from '../../../styled-system/recipes'
import { createStyleContext, type ColorPaletteProp } from './style-context'
import { XIcon, withIcon } from './icons'

const { Provider: StyleProvider, withContext } = createStyleContext(dialog)

export function Root<Payload = unknown>(
  props: Base.Root.Props<Payload> & DialogVariantProps & ColorPaletteProp,
) {
  const [variants, { colorPalette, ...rest }] = dialog.splitVariantProps(props)
  return (
    <StyleProvider {...variants} colorPalette={colorPalette}>
      <Base.Root {...rest} />
    </StyleProvider>
  )
}
export const Backdrop = withContext(Base.Backdrop, 'backdrop')
export const Popup = withContext(Base.Popup, 'popup')
export const Title = withContext(Base.Title, 'title')
export const Description = withContext(Base.Description, 'description')
export const Close = withContext(Base.Close, 'close')
/** Corner close button: draws × and is named Close until children or aria-label replace them. */
export const CloseTrigger = withIcon(withContext(Base.Close, 'closeTrigger'), <XIcon />, 'Close')
export const Header = withContext('div', 'header')
export const Body = withContext('div', 'body')
export const Footer = withContext('div', 'footer')
export const Trigger = Base.Trigger
export const Portal = Base.Portal
export const Viewport = Base.Viewport

export const createHandle = Base.createHandle
export const Handle = Base.Handle
export type Handle<Payload> = Base.Handle<Payload>
