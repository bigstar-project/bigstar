'use client'
import type { ReactNode } from 'react'
import { Button as BaseButton } from '@base-ui/react/button'
import { cx } from '../../../styled-system/css'
import { button, type ButtonVariantProps } from '../../../styled-system/recipes'
import { Spinner } from './spinner'
import { mergeClassName, paletteClass, type ColorPaletteProp } from './style-context'

export interface ButtonLoadingProps {
  /** Show a spinner and block presses. Without loadingText the button keeps its width. */
  loading?: boolean
  /** Replaces the label while loading, next to the spinner. */
  loadingText?: ReactNode
  /** Replaces the default spinner. */
  spinner?: ReactNode
  /** Where the spinner sits next to loadingText. @default "start" */
  spinnerPlacement?: 'start' | 'end'
}

export type ButtonProps = BaseButton.Props &
  ButtonVariantProps &
  ColorPaletteProp &
  ButtonLoadingProps
export function Button(props: ButtonProps) {
  const [variants, other] = button.splitVariantProps(props)
  const {
    colorPalette,
    loading,
    loadingText,
    // 1em like the label text, as in Park UI; inherits the label color.
    spinner = <Spinner size="inherit" aria-hidden />,
    spinnerPlacement = 'start',
    children,
    disabled,
    focusableWhenDisabled,
    ...rest
  } = other
  let content: ReactNode = children
  if (loading && loadingText !== undefined)
    content = (
      <>
        {spinnerPlacement === 'start' && spinner}
        {loadingText}
        {spinnerPlacement === 'end' && spinner}
      </>
    )
  else if (loading)
    // The label stays laid out (and named) but invisible, so the width does not change.
    content = (
      <>
        <span data-slot="loader">{spinner}</span>
        <span data-slot="label">{children}</span>
      </>
    )
  return (
    <BaseButton
      {...rest}
      disabled={disabled || loading}
      // Keep focus on the button while it loads instead of dropping it to the page.
      focusableWhenDisabled={focusableWhenDisabled ?? (loading && !disabled)}
      aria-busy={loading || undefined}
      data-loading={loading ? '' : undefined}
      className={mergeClassName(cx(button(variants), paletteClass(colorPalette)), rest.className)}
    >
      {content}
    </BaseButton>
  )
}
