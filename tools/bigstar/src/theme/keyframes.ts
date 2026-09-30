import { defineKeyframes } from '@pandacss/dev'

export const keyframes = defineKeyframes({
  'kiso-indeterminate': {
    from: { insetInlineStart: '-40%' },
    to: { insetInlineStart: '100%' },
  },
  'kiso-shine': {
    from: { backgroundPosition: '200% 0' },
    to: { backgroundPosition: '-200% 0' },
  },
  'kiso-stripes': {
    from: { backgroundPosition: 'var(--stripe-size) 0' },
    to: { backgroundPosition: '0 0' },
  },
})
