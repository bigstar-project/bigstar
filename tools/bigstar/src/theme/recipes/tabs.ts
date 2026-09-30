import { defineSlotRecipe } from '@pandacss/dev'
import { insetRadius } from '../shared'

// Place <Tabs.Indicator /> inside List: Base UI positions it with --active-tab-* variables.
export const tabs = defineSlotRecipe({
  className: 'kiso-tabs',
  jsx: ['Tabs', /^Tabs\./],
  slots: ['root', 'list', 'tab', 'panel', 'indicator'],
  base: {
    root: {
      position: 'relative',
      display: 'flex',
      width: 'full',
      _horizontal: { flexDirection: 'column', gap: '2' },
      _vertical: { flexDirection: 'row', gap: '4' },
    },
    list: {
      display: 'flex',
      position: 'relative',
      isolation: 'isolate',
      maxWidth: 'full',
      // Scrolls on narrow screens; the indicator and line stay inside the padding box.
      overflowX: 'auto',
      scrollbarWidth: 'none',
      _horizontal: { flexDirection: 'row' },
      _vertical: { flexDirection: 'column', overflowX: 'visible' },
    },
    tab: {
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      flexShrink: '0',
      position: 'relative',
      whiteSpace: 'nowrap',
      fontWeight: 'semibold',
      color: 'fg.muted',
      cursor: 'pointer',
      outline: '0',
      transitionProperty: 'color',
      transitionDuration: 'fast',
      _hover: { color: 'fg.default' },
      _focusVisible: { zIndex: '1', focusVisibleRing: 'inside', borderRadius: 'l2' },
      _disabled: { layerStyle: 'disabled' },
    },
    panel: { focusVisibleRing: 'inside', minWidth: '0', flex: '1' },
    indicator: {
      position: 'absolute',
      zIndex: '-1',
      left: 'var(--active-tab-left)',
      top: 'var(--active-tab-top)',
      width: 'var(--active-tab-width)',
      height: 'var(--active-tab-height)',
      transitionProperty: 'left, top, width, height',
      transitionDuration: 'normal',
      transitionTimingFunction: 'ease-in-out',
    },
  },
  defaultVariants: { variant: 'line', size: 'md' },
  variants: {
    variant: {
      line: {
        list: {
          _horizontal: { boxShadow: 'inset 0 -1px 0 {colors.border}' },
          _vertical: { boxShadow: 'inset 1px 0 0 {colors.border}' },
        },
        tab: { _selected: { color: 'colorPalette.plain.fg' } },
        indicator: {
          bg: 'colorPalette.solid.bg',
          _horizontal: { top: 'auto', bottom: '0', height: '0.5' },
          _vertical: { left: '0', width: '0.5' },
        },
      },
      subtle: {
        list: { gap: '1' },
        tab: { borderRadius: 'l2', _selected: { color: 'colorPalette.subtle.fg' } },
        indicator: { bg: 'colorPalette.subtle.bg', borderRadius: 'l2' },
      },
      enclosed: {
        list: {
          bg: { base: 'gray.2', _dark: 'gray.1' },
          boxShadow: 'inset 0 0 0 1px {colors.border}',
          borderRadius: 'l3',
          p: '1',
          gap: '1',
          width: 'fit-content',
        },
        tab: {
          borderRadius: insetRadius,
          _focusVisible: { borderRadius: insetRadius },
          _selected: { color: 'colorPalette.surface.fg' },
        },
        indicator: {
          borderRadius: insetRadius,
          boxShadow: { base: 'xs', _dark: 'none' },
          bg: { base: 'white', _dark: 'gray.3' },
        },
      },
    },
    /** Tabs share the list width equally. */
    fitted: {
      true: {
        list: { width: 'full' },
        tab: { flex: '1' },
      },
    },
    size: {
      xs: { tab: { h: '8', minW: '8', textStyle: 'xs', px: '3', gap: '2' } },
      sm: { tab: { h: '9', minW: '9', textStyle: 'sm', px: '3.5', gap: '2' } },
      md: { tab: { h: '10', minW: '10', textStyle: 'sm', px: '4', gap: '2' } },
      lg: { tab: { h: '11', minW: '11', textStyle: 'md', px: '4.5', gap: '2' } },
    },
  },
})
