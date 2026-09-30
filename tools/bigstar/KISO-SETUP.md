# Kiso UI setup

1. Install runtime packages: pnpm add @base-ui/react react react-dom
2. Install Panda: pnpm add -D @pandacss/dev; configure PostCSS using pnpm exec panda init --postcss (it keeps an existing panda.config.ts).
3. panda.config.ts: the CLI writes the configuration below when the file is missing and leaves an existing one alone. Rerun with --panda-config=merge to add Kiso to an existing file (your values win; if something cannot be added safely, nothing is written), or --panda-config=overwrite to replace it. --accent and --gray choose the palettes. gray and the roles (accent: definePalette('accent', iris), …) are copies of palettes, so their sources are not listed. To use a palette by its own name, import it from src/theme/colors and list it.

```ts
import { defineConfig } from '@pandacss/dev'
import { tokens, semanticColors, definePalette, radii, removePandaPresetColors } from './src/theme/tokens'
import { shadows } from './src/theme/shadows'
import { slate } from './src/theme/colors/slate'
import { blue } from './src/theme/colors/blue'
import { green } from './src/theme/colors/green'
import { amber } from './src/theme/colors/amber'
import { red } from './src/theme/colors/red'
import { conditions } from './src/theme/conditions'
import { globalCss } from './src/theme/global-css'
import { textStyles } from './src/theme/text-styles'
import { layerStyles } from './src/theme/layer-styles'
import { keyframes } from './src/theme/keyframes'
import { recipes, slotRecipes } from './src/theme/recipes'

export default defineConfig({
  preflight: true,
  jsxFramework: 'react',
  include: ['./src/**/*.{ts,tsx}'],
  outdir: 'styled-system',
  conditions: { extend: conditions },
  globalCss: { extend: globalCss },
  theme: {
    extend: {
      tokens,
      semanticTokens: {
        colors: {
          ...semanticColors,
          // Recipes read gray and these roles; each role is a copy of a palette. Only what is
          // listed here exists: to use another color by name, import it and list it too.
          gray: slate,
          accent: definePalette('accent', slate),
          info: definePalette('info', blue),
          success: definePalette('success', green),
          warning: definePalette('warning', amber),
          danger: definePalette('danger', red),
        },
        radii,
        shadows,
      },
      textStyles,
      layerStyles,
      keyframes,
      recipes,
      slotRecipes,
    },
  },
  plugins: [removePandaPresetColors],
})
```

4. Import src/theme/global.css in your application entry.
5. Put class="dark" on html for the dark theme (next-themes: attribute="class", its default); light is the default. Components inherit colorPalette="accent" (a copy of a palette, see panda.config.ts); pass colorPalette to any component, or set it on an ancestor to recolor a subtree. Use definePalette('brand', blue) for a renamed copy.
6. Run pnpm exec panda codegen. Add it to prepare and run it after adding components.
7. Import individual components from src/components/ui. Panda extracts variant, size and colorPalette values written in JSX (literals, ternaries, responsive objects) and emits only those; list values chosen from variables in staticCss.recipes, e.g. { button: [{ size: ['sm', 'lg'] }] }.

Fonts default to system fallbacks. Optionally install @fontsource-variable/geist and @fontsource-variable/geist-mono and import them in your app.

The CLI only supports the src/components/ui, src/theme and root styled-system layout.
No packages are installed and no network requests are made. Files the CLI wrote and you have not changed are updated when you run add or update; files you changed are kept (pnpm ui diff compares them with Kiso). Only --panda-config=overwrite replaces panda.config.ts.
Generated src/theme/recipes/index.ts is maintained by the CLI; edit recipe files instead.
