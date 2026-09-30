import type { ComponentPropsWithRef } from 'react'
import { css, cx } from '../../../styled-system/css'
import { spinner, type SpinnerVariantProps } from '../../../styled-system/recipes'
export type SpinnerProps = ComponentPropsWithRef<'span'> & SpinnerVariantProps & { label?: string }
export function Spinner({ size, label = 'Loading', className, ...props }: SpinnerProps) {
  // Decorative inside an element that already announces itself (e.g. a busy Button).
  const hidden = props['aria-hidden'] === true || props['aria-hidden'] === 'true'
  return (
    <span
      role={hidden ? undefined : 'status'}
      {...props}
      className={cx(spinner({ size }), className)}
    >
      <span aria-hidden="true" data-spinner="" />
      {!hidden && <span className={css({ srOnly: true })}>{label}</span>}
    </span>
  )
}
