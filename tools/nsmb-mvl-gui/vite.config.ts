import { fileURLToPath } from 'node:url';
import react from '@vitejs/plugin-react';
import { defineConfig } from 'vite';
import tsconfigPaths from 'vite-tsconfig-paths';
import packageJson from './package.json';

const buildProfile =
  process.env.NSMB_MVL_BUILD_PROFILE === 'distribution'
    ? 'distribution'
    : 'local';
const aiDevToolsEnabled = buildProfile === 'local';

export default defineConfig({
  clearScreen: false,
  define: {
    'globalThis.__NSMB_MVL_AI_DEVTOOLS_ENABLED__':
      JSON.stringify(aiDevToolsEnabled),
    'globalThis.__NSMB_MVL_BUILD_PROFILE__': JSON.stringify(buildProfile),
    __NSMB_MVL_GUI_VERSION__: JSON.stringify(packageJson.version),
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
