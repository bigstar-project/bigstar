// Base UI exposes presence attributes, not Ark/Radix data-state values.
export const conditions = {
  // Same as Park UI: dark is Panda's default (.dark &), so <html class="dark"> (next-themes'
  // default) switches it; light values apply everywhere else.
  light: ':root &, .light &',
  // Hover and press never paint a disabled control (Park UI keeps the same rule).
  hover: '&:is(:hover, [data-hover]):not(:disabled, [data-disabled], [aria-disabled=true])',
  active: '&:is(:active, [data-active]):not(:disabled, [data-disabled], [aria-disabled=true])',
  checked: '&:is([data-checked], [aria-checked=true])',
  unchecked: '&[data-unchecked]',
  highlighted: '&[data-highlighted]',
  selected: '&:is([data-selected], [aria-selected=true])',
  pressed: '&:is([data-pressed], [aria-pressed=true])',
  open: '&[data-open]',
  closed: '&[data-closed]',
  expanded: '&[data-panel-open]',
  startingStyle: '&[data-starting-style]',
  endingStyle: '&[data-ending-style]',
  invalid: '&:is([data-invalid], [aria-invalid=true])',
  indeterminate: '&[data-indeterminate]',
} as const
