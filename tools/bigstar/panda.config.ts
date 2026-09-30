import { globalCss } from '@/theme/app/global-css';
import { appKeyframes } from '@/theme/app/keyframes';
import { amber } from '@/theme/colors/amber';
import { blue } from '@/theme/colors/blue';
import { green } from '@/theme/colors/green';
import { orange } from '@/theme/colors/orange';
import { red } from '@/theme/colors/red';
import { slate } from '@/theme/colors/slate';
import { yellow } from '@/theme/colors/yellow';
import { conditions } from '@/theme/conditions';
import { keyframes } from '@/theme/keyframes';
import { layerStyles } from '@/theme/layer-styles';
import { animationStyles as parkUiAnimationStyles } from '@/theme/park-ui/animation-styles';
import { parkUiConditions } from '@/theme/park-ui/conditions';
import { durations as parkUiDurations } from '@/theme/park-ui/durations';
import { keyframes as parkUiKeyframes } from '@/theme/park-ui/keyframes';
import { recipes as parkUiRecipes } from '@/theme/park-ui/recipes';
import { recipes, slotRecipes } from '@/theme/recipes';
import { shadows } from '@/theme/shadows';
import { textStyles } from '@/theme/text-styles';
import {
  definePalette,
  removePandaPresetColors,
  semanticColors,
  tokens,
} from '@/theme/tokens';
import { defineConfig } from '@pandacss/dev';

export default defineConfig({
  // Whether to use css reset
  preflight: true,
  strictTokens: true,

  // Where to look for your css declarations
  include: ['./src/**/*.{js,jsx,ts,tsx}', './pages/**/*.{js,jsx,ts,tsx}'],

  // Files to exclude
  exclude: [],

  // 旧 Park UI の部品が残っている間は、Ark UI の data-state にも一致させる
  conditions: { extend: { ...conditions, ...parkUiConditions } },
  globalCss: { extend: globalCss },

  // Useful for theme customization
  theme: {
    extend: {
      animationStyles: parkUiAnimationStyles,
      recipes: { ...parkUiRecipes, ...recipes },
      slotRecipes,
      keyframes: { ...parkUiKeyframes, ...keyframes, ...appKeyframes },
      layerStyles,
      textStyles,

      tokens: {
        ...tokens,
        durations: parkUiDurations,
        fonts: {
          sans: {
            value:
              "'IBM Plex Sans JP', 'Segoe UI', system-ui, -apple-system, BlinkMacSystemFont, sans-serif",
          },
          // 日本語の Windows で既定の等幅フォントに落ちると、\ が ¥ で表示される
          mono: { value: "'Cascadia Mono', Consolas, monospace" },
        },
        sizes: {
          appMin: { value: '920px' },
          contentMax: { value: '982px' },
          contentWide: { value: 'min(982px, calc(100vw - 244px))' },
          contentCompact: { value: 'calc(100vw - 92px)' },
          statusMax: { value: '42ch' },
          sidebar: { value: '204px' },
          sidebarCompact: { value: '92px' },
          mainPanel: { value: '616px' },
          cta: { value: '60px' },
          // Kiso で組んだ画面の本文の幅
          page: { value: '808px' },
        },
      },

      semanticTokens: {
        colors: {
          ...semanticColors,
          // 部品のレシピは gray と役割の名前だけを読む。役割は元のパレットの複製
          gray: slate,
          accent: definePalette('accent', slate),
          info: definePalette('info', blue),
          success: definePalette('success', green),
          // 主ボタンの amber と区別するため、警告は orange にする
          warning: definePalette('warning', orange),
          danger: definePalette('danger', red),
          // 画面が名前のまま使う色だけを登録する
          amber,
          blue,
          green,
          red,
          // 旧 Park UI の画面が使う
          yellow,

          app: {
            bg: { value: '{colors.gray.1}' },
            panel: {
              value: { base: '#0e0e199a', _dark: '#40434800' },
            },
            panelStrong: { value: '{colors.gray.a4}' },
            card: {
              value: { base: '#0f0f1694', _dark: '#292a2e73' },
            },
            sidebar: { value: '#06101de0' },
            pageText: { value: '{colors.gray.12}' },
          },
        },

        shadows,

        // Kiso の既定（2 / 4 / 6px）より丸くする。内側から 6 / 8 / 12px
        radii: {
          l1: { value: '{radii.md}' },
          l2: { value: '{radii.lg}' },
          l3: { value: '{radii.xl}' },
        },
      },
    },
  },

  plugins: [removePandaPresetColors],

  // The output directory for your css system
  outdir: 'styled-system',

  // The JSX framework to use
  jsxFramework: 'react',
});
