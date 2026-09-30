import { defineSlotRecipe } from '@pandacss/dev'
import { disabledLabel } from '../shared'

export const field = defineSlotRecipe({
  className: 'kiso-field',
  jsx: ['Field', /^Field\./],
  slots: ['root', 'item', 'label', 'description', 'error'],
  base: {
    root: { display: 'flex', flexDirection: 'column', gap: '1.5', width: 'full' },
    // One checkbox or radio in a group, with its own label and description.
    item: { display: 'flex', flexDirection: 'column', gap: '1' },
    label: {
      display: 'flex',
      alignItems: 'center',
      gap: '0.5',
      textAlign: 'start',
      userSelect: 'none',
      color: 'fg.default',
      textStyle: 'label',
      _disabled: disabledLabel,
    },
    description: { color: 'fg.muted', textStyle: 'sm', _disabled: { layerStyle: 'disabled' } },
    error: { color: 'fg.error', textStyle: 'sm' },
  },
})
