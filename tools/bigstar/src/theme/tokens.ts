import { defineTokens, defineSemanticTokens, definePlugin } from '@pandacss/dev'
import type { iris } from './colors/iris'
import { colors } from './base-colors'
import { zIndex } from './z-index'

/** Any palette from ./colors, or one made with definePalette. */
export type Palette = typeof iris
type TokenTree = { [key: string]: TokenTree | { value: string | Record<string, string> } }

export const tokens = defineTokens({
  colors,
  zIndex,
  fonts: {
    sans: { value: "'Geist Variable', -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif" },
    mono: { value: "'Geist Mono Variable', ui-monospace, monospace" },
  },
})

/** Park UI's semantic colors (plus Kiso's readable `fg.error`). They read `gray` and `danger`. */
export const semanticColors = defineSemanticTokens.colors({
  fg: {
    default: { value: '{colors.gray.12}' },
    muted: { value: '{colors.gray.11}' },
    subtle: { value: '{colors.gray.10}' },
    // Kiso: readable error text. `error` (step 9) stays for borders and rings, as in Park UI.
    error: { value: '{colors.danger.11}' },
  },
  canvas: { value: '{colors.gray.1}' },
  border: { value: '{colors.gray.4}' },
  error: { value: '{colors.danger.9}' },
})

/** Nested corner radii: l1 inside l2 inside l3. Point them at other radii to restyle. */
export const radii = defineSemanticTokens.radii({
  l1: { value: '{radii.xs}' },
  l2: { value: '{radii.sm}' },
  l3: { value: '{radii.md}' },
})

/** Park UI's Radix-based palettes replace Panda's 50–950 colors; drop those from the preset. */
export const removePandaPresetColors = definePlugin({
  name: 'Remove Panda Preset Colors',
  hooks: {
    'preset:resolved': ({ utils, preset, name }) =>
      name === '@pandacss/preset-panda'
        ? utils.omit(preset, ['theme.tokens.colors', 'theme.semanticTokens.colors'])
        : preset,
  },
})

/** The name a palette's roles refer to (`iris`, `brand`; gray palettes refer to `gray`). */
export function paletteName(palette: Palette) {
  const name = palette.subtle.bg.DEFAULT.value.base.match(/^\{colors\.([^.]+)\./)?.[1]
  if (!name) throw new Error('Not a Kiso palette: its subtle.bg does not reference a palette.')
  return name
}

/**
 * Copy a palette under a new name. Kiso's roles are made this way in panda.config.ts:
 * `accent: definePalette('accent', iris)`, `danger: definePalette('danger', red)`, and so is an
 * extra color such as `brand: definePalette('brand', blue)`. The copy holds the values itself, so
 * the source palette does not need to be listed. References are rebased, so overriding accent.9
 * also updates accent.solid.bg.
 */
export function definePalette(name: string, source: Palette): Palette {
  const from = paletteName(source)
  return JSON.parse(
    JSON.stringify(source).replaceAll(`{colors.${from}.`, `{colors.${name}.`),
  ) as Palette
}

/**
 * Point every role of a name at a registered palette (colorPalette="accent" then behaves like
 * "iris"). The preview uses it with conditions to switch palettes at runtime; apps use
 * definePalette instead.
 */
export function aliasPalette(
  name: string,
  shape: Palette,
  conditions: Record<string, string> = {},
): TokenTree {
  function visit(tree: TokenTree, path: string[] = []): TokenTree {
    return Object.fromEntries(
      Object.entries(tree).map(([key, node]) => {
        const next = [...path, key]
        if ('value' in node) {
          const suffix = next.filter((part) => part !== 'DEFAULT').join('.')
          const value: Record<string, string> = { base: `{colors.${name}.${suffix}}` }
          for (const [condition, option] of Object.entries(conditions))
            value[condition] = `{colors.${option}.${suffix}}`
          return [key, { value }]
        }
        return [key, visit(node, next)]
      }),
    )
  }
  return visit(shape as unknown as TokenTree)
}
