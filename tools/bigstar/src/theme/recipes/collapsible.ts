import { defineSlotRecipe } from '@pandacss/dev'

export const collapsible = defineSlotRecipe({
  className: 'kiso-collapsible',
  jsx: ['Collapsible', /^Collapsible\./],
  slots: ['root', 'trigger', 'indicator', 'panel'],
  base: {
    root: { width: 'full' },
    trigger: {
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'space-between',
      gap: '3',
      width: 'full',
      py: '2',
      textStyle: 'sm',
      fontWeight: 'medium',
      color: 'fg.default',
      borderRadius: 'l2',
      cursor: 'pointer',
      focusVisibleRing: 'outside',
      _disabled: { layerStyle: 'disabled' },
    },
    // The chevron at the end of Trigger; it turns while the panel is open.
    indicator: {
      display: 'inline-flex',
      flexShrink: '0',
      color: 'fg.subtle',
      transitionProperty: 'rotate',
      transitionDuration: 'normal',
      _icon: { boxSize: '4' },
      '[data-panel-open] &': { rotate: '180deg' },
    },
    panel: {
      overflow: 'hidden',
      height: 'var(--collapsible-panel-height)',
      textStyle: 'sm',
      color: 'fg.muted',
      transitionProperty: 'height',
      transitionDuration: 'normal',
      _startingStyle: { height: '0' },
      _endingStyle: { height: '0' },
      '& > *': { pb: '3' },
    },
  },
})
