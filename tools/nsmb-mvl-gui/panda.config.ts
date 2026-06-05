import { animationStyles } from "@/theme/animation-styles";
import { blue } from "@/theme/colors/blue";
import { green } from "@/theme/colors/green";
import { red } from "@/theme/colors/red";
import { slate } from "@/theme/colors/slate";
import { yellow } from "@/theme/colors/yellow";
import { conditions } from "@/theme/conditions";
import { globalCss } from "@/theme/global-css";
import { keyframes } from "@/theme/keyframes";
import { layerStyles } from "@/theme/layer-styles";
import { recipes, slotRecipes } from "@/theme/recipes";
import { textStyles } from "@/theme/text-styles";
import { colors } from "@/theme/tokens/colors";
import { durations } from "@/theme/tokens/durations";
import { shadows } from "@/theme/tokens/shadows";
import { zIndex } from "@/theme/tokens/z-index";
import { defineConfig } from "@pandacss/dev";

export default defineConfig({
  // Whether to use css reset
  preflight: true,
  strictTokens: true,

  // Where to look for your css declarations
  include: ["./src/**/*.{js,jsx,ts,tsx}", "./pages/**/*.{js,jsx,ts,tsx}"],

  // Files to exclude
  exclude: [],

  // Useful for theme customization
  theme: {
    extend: {
      animationStyles: animationStyles,
      recipes: recipes,
      slotRecipes: slotRecipes,
      keyframes: keyframes,
      layerStyles: layerStyles,
      textStyles: textStyles,

      tokens: {
        colors: colors,
        durations: durations,
        zIndex: zIndex,
        fonts: {
          body: {
            value:
              'Inter, "Segoe UI", system-ui, -apple-system, BlinkMacSystemFont, sans-serif'
          }
        },
        sizes: {
          appMin: { value: "920px" },
          contentMax: { value: "1260px" },
          contentWide: { value: "min(1260px, calc(100vw - 284px))" },
          contentCompact: { value: "calc(100vw - 92px)" },
          statusMax: { value: "48ch" },
          sidebar: { value: "236px" },
          sidebarCompact: { value: "92px" },
          diagnostics: { value: "390px" },
          cta: { value: "72px" },
          settingsAside: { value: "360px" }
        }
      },

      semanticTokens: {
        colors: {
          fg: {
            default: {
              value: {
                _light: "{colors.gray.12}",
                _dark: "{colors.gray.12}"
              }
            },

            muted: {
              value: {
                _light: "{colors.gray.11}",
                _dark: "{colors.gray.11}"
              }
            },

            subtle: {
              value: {
                _light: "{colors.gray.10}",
                _dark: "{colors.gray.10}"
              }
            }
          },

          border: {
            value: {
              _light: "{colors.gray.4}",
              _dark: "{colors.gray.4}"
            }
          },

          error: {
            value: {
              _light: "{colors.red.9}",
              _dark: "{colors.red.9}"
            }
          },

          blue: blue,
          gray: slate,
          red: red,
          green: green,

          app: {
            bg: {
              value: {
                _light: "{colors.gray.1}",
                _dark: "{colors.gray.1}"
              }
            },
            panel: {
              value: {
                _light: "{colors.gray.a3}",
                _dark: "{colors.gray.a3}"
              }
            },
            panelStrong: {
              value: {
                _light: "{colors.gray.a4}",
                _dark: "{colors.gray.a4}"
              }
            },
            card: {
              value: {
                _light: "#111113a4",
                _dark: "#111113a4"
              }
            },
            sidebar: {
              value: {
                _light: "#06101de0",
                _dark: "#06101de0"
              }
            },
            pageText: {
              value: {
                _light: "{colors.gray.12}",
                _dark: "{colors.gray.12}"
              }
            }
          },

          yellow: yellow
        },

        shadows: shadows,

        radii: {
          l1: { value: "{radii.md}" },
          l2: { value: "{radii.lg}" },
          l3: { value: "{radii.xl}" }
        }
      }
    },
  },

  // The output directory for your css system
  outdir: "styled-system",

  // The JSX framework to use
  jsxFramework: "react",

  globalCss: globalCss,
  conditions: conditions
});
