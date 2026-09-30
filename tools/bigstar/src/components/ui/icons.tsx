import {
  createElement,
  type ComponentProps,
  type ComponentPropsWithRef,
  type ElementType,
  type ReactNode,
} from 'react'

/**
 * Stroke icons drawn in the source, so components need no icon package. Recipes size them
 * through _icon; 1em is the fallback outside a recipe.
 */
export function Svg(props: ComponentProps<'svg'>) {
  return (
    <svg
      viewBox="0 0 24 24"
      width="1em"
      height="1em"
      fill="none"
      stroke="currentColor"
      strokeWidth="2"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
      {...props}
    />
  )
}

export const CheckIcon = () => (
  <Svg>
    <path d="M20 6L9 17l-5-5" />
  </Svg>
)
export const MinusIcon = () => (
  <Svg>
    <path d="M5 12h14" />
  </Svg>
)
export const PlusIcon = () => (
  <Svg>
    <path d="M5 12h14M12 5v14" />
  </Svg>
)
export const XIcon = () => (
  <Svg>
    <path d="M18 6L6 18M6 6l12 12" />
  </Svg>
)
export const ChevronDownIcon = () => (
  <Svg>
    <path d="M6 9l6 6 6-6" />
  </Svg>
)
export const ChevronLeftIcon = () => (
  <Svg>
    <path d="M15 18l-6-6 6-6" />
  </Svg>
)
export const ChevronRightIcon = () => (
  <Svg>
    <path d="M9 18l6-6-6-6" />
  </Svg>
)

/**
 * A part that holds one icon draws `icon` until children replace it. An icon-only button also
 * takes a default `label` (aria-label); aria-label or children replace it.
 */
export function withIcon<C extends ElementType>(Component: C, icon: ReactNode, label?: string) {
  function WithIcon({ children, ...props }: ComponentPropsWithRef<C>) {
    if (children !== undefined) return createElement(Component, props, children)
    return createElement(Component, label ? { 'aria-label': label, ...props } : props, icon)
  }
  return WithIcon
}
