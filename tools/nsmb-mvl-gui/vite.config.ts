import { fileURLToPath } from 'node:url';
import react from '@vitejs/plugin-react';
import { defineConfig } from 'vite';
import tsconfigPaths from 'vite-tsconfig-paths';
import packageJson from './package.json';
import { loadEditionConfig } from './scripts/edition-config.mjs';

const buildProfile =
  process.env.NSMB_MVL_BUILD_PROFILE === 'distribution'
    ? 'distribution'
    : 'local';
const edition = process.env.NSMB_MVL_EDITION || 'insiders';
const editionConfig = loadEditionConfig(edition);
const aiDevToolsEnabled =
  buildProfile === 'local' &&
  editionConfig.capabilities.aiDevToolsInLocalBuilds;
const appVersion = process.env.NSMB_MVL_APP_VERSION || packageJson.version;

export default defineConfig({
  clearScreen: false,
  define: {
    'globalThis.__NSMB_MVL_AI_DEVTOOLS_ENABLED__':
      JSON.stringify(aiDevToolsEnabled),
    'globalThis.__NSMB_MVL_BUILD_PROFILE__': JSON.stringify(buildProfile),
    'globalThis.__NSMB_MVL_EDITION_CONFIG__': JSON.stringify({
      badge: editionConfig.badge,
      capabilities: editionConfig.capabilities,
      displayName: editionConfig.displayName,
      edition: editionConfig.edition,
    }),
    __NSMB_MVL_GUI_VERSION__: JSON.stringify(appVersion),
  },
  plugins: [
    react({
      babel: {
        plugins: ['babel-plugin-react-compiler'],
      },
    }),
    tsconfigPaths(),
  ],
  resolve: {
    alias: aiDevToolsEnabled
      ? []
      : [
          {
            find: '@/launcher/AIReplayViewer',
            replacement: fileURLToPath(
              new URL(
                './src/launcher/AIReplayViewer.disabled.tsx',
                import.meta.url,
              ),
            ),
          },
        ],
  },
  server: {
    host: '127.0.0.1',
    port: 1420,
    strictPort: true,
    watch: {
      ignored: ['**/src-tauri/**'],
    },
  },
});
