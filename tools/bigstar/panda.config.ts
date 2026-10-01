import { globalCss } from '@/theme/app/global-css';
import { appKeyframes } from '@/theme/app/keyframes';
import { amber } from '@/theme/colors/amber';
import { blue } from '@/theme/colors/blue';
import { green } from '@/theme/colors/green';
import { orange } from '@/theme/colors/orange';
import { red } from '@/theme/colors/red';
import { slate } from '@/theme/colors/slate';
import { conditions } from '@/theme/conditions';
import { keyframes } from '@/theme/keyframes';
import { layerStyles } from '@/theme/layer-styles';
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

  conditions: { extend: conditions },
  globalCss: { extend: globalCss },

  // Useful for theme customization
  theme: {
    extend: {
      recipes,
      slotRecipes,
      keyframes: { ...keyframes, ...appKeyframes },
      layerStyles,
      textStyles,

      tokens: {
        ...tokens,
        fonts: {
          sans: {
            value:
              "'IBM Plex Sans JP', 'Segoe UI', system-ui, -apple-system, BlinkMacSystemFont, sans-serif",
          },
          // 日本語の Windows で既定の等幅フォントに落ちると、\ が ¥ で表示される
          mono: { value: "'Cascadia Mono', Consolas, monospace" },
          // ロゴの BIGSTAR だけに使う。英字のサブセットだけを読み込む
          display: { value: "'Dela Gothic One', 'IBM Plex Sans JP', sans-serif" },
        },
        sizes: {
          appMin: { value: '920px' },
          contentMax: { value: '982px' },
          contentWide: { value: 'min(982px, calc(100vw - 256px))' },
          contentCompact: { value: 'calc(100vw - 92px)' },
          statusMax: { value: '42ch' },
          sidebar: { value: '216px' },
          sidebarCompact: { value: '92px' },
          mainPanel: { value: '616px' },
          cta: { value: '60px' },
          // Kiso で組んだ画面の本文の幅
          page: { value: '808px' },
        },
        // Panda 標準の角丸には none がなく、strictTokens では 0 を直接書けない
        radii: {
          none: { value: '0' },
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

          app: {
            bg: { value: '{colors.gray.1}' },
            panel: {
              value: { base: '#0e0e199a', _dark: '#40434800' },
            },
            panelStrong: { value: '{colors.gray.a4}' },
            card: {
              value: { base: '#0f0f1694', _dark: '#292a2e73' },
            },
            // 本文の gray.1 より一段暗くして、サイドバーを区切る
            sidebar: { value: '#0a0c0f' },
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
