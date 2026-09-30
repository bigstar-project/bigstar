'use client'
import type { ReactNode } from 'react'
import { Toast as Base } from '@base-ui/react/toast'
import { toast } from '../../../styled-system/recipes'
import { Spinner } from './spinner'
import { createStyleContext } from './style-context'
import { Svg, XIcon, withIcon } from './icons'
const { withProvider, withContext } = createStyleContext(toast)
export const Provider = Base.Provider
export const Portal = Base.Portal
export const Root = withProvider(Base.Root, 'root')
export const Viewport = withContext(Base.Viewport, 'viewport')
/** Anchored toasts: place one toast next to an element (positionerProps.anchor). */
export const Positioner = withContext(Base.Positioner, 'positioner')
export const Arrow = withContext(Base.Arrow, 'arrow')
/** Optional icon before the content, tinted by the toast type. */
export const Indicator = withContext('span', 'indicator')
export const Content = withContext(Base.Content, 'content')
export const Title = withContext(Base.Title, 'title')
export const Description = withContext(Base.Description, 'description')
/** Draws × and is named "Dismiss notification" until children or aria-label replace them. */
export const Close = withIcon(withContext(Base.Close, 'close'), <XIcon />, 'Dismiss notification')
export const Action = withContext(Base.Action, 'action')
export const useToastManager = Base.useToastManager
export const createToastManager = Base.createToastManager

/** Icons the Toaster shows per type. Any other type (or none) shows no icon. */
export const typeIcons: Record<string, ReactNode> = {
  success: (
    <Svg>
      <circle cx="12" cy="12" r="9" />
      <path d="M8.5 12.5l2.5 2.5 4.5-5" />
    </Svg>
  ),
  error: (
    <Svg>
      <circle cx="12" cy="12" r="9" />
      <path d="M9.5 9.5l5 5M14.5 9.5l-5 5" />
    </Svg>
  ),
  warning: (
    <Svg>
      <path d="M12 4L21 19.5H3z" />
      <path d="M12 10v4M12 17h.01" />
    </Svg>
  ),
  info: (
    <Svg>
      <circle cx="12" cy="12" r="9" />
      <path d="M12 11v5M12 8h.01" />
    </Svg>
  ),
  loading: <Spinner size="inherit" aria-hidden />,
}

/**
 * Optional composition; mount once inside Provider. Use parts directly for custom layouts.
 * label names the region (Base UI: "Notifications") and closeLabel the dismiss button; pass
 * your own for another language.
 */
export function Toaster({
  label,
  closeLabel = 'Dismiss notification',
}: {
  label?: string
  closeLabel?: string
}) {
  const { toasts } = useToastManager()
  return (
    <Portal>
      {/* An undefined aria-label would erase Base UI's default name. */}
      <Viewport {...(label && { 'aria-label': label })}>
        {toasts.map((item) => (
          <Root key={item.id} toast={item}>
            {item.type && typeIcons[item.type] && (
              <Indicator aria-hidden="true">{typeIcons[item.type]}</Indicator>
            )}
            <Content>
              <Title>{item.title}</Title>
              {item.description && <Description>{item.description}</Description>}
            </Content>
            {item.actionProps && <Action {...item.actionProps} />}
            <Close aria-label={closeLabel} />
          </Root>
        ))}
      </Viewport>
    </Portal>
  )
}
