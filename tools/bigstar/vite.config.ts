import { fileURLToPath } from 'node:url';
import react from '@vitejs/plugin-react';
import { defineConfig } from 'vite';
import tsconfigPaths from 'vite-tsconfig-paths';
import packageJson from './package.json';
import {
  loadBuildProfileConfig,
  resolveRuntimeCapabilities,
} from './scripts/build-profile-config.mjs';
import { loadEditionConfig } from './scripts/edition-config.mjs';

const buildProfileName = process.env.BIGSTAR_BUILD_PROFILE || 'local';
const buildProfile = loadBuildProfileConfig(buildProfileName);
const edition = process.env.BIGSTAR_EDITION || 'insiders';
const editionConfig = loadEditionConfig(edition);
const runtimeCapabilities = resolveRuntimeCapabilities(
  editionConfig,
  buildProfile,
);
const devPort = Number(process.env.BIGSTAR_DEV_PORT || editionConfig.devPort);
const aiDevToolsEnabled = runtimeCapabilities.aiDevTools;
const appVersion = process.env.BIGSTAR_APP_VERSION || packageJson.version;

export default defineConfig({
  clearScreen: false,
  define: {
    'globalThis.__BIGSTAR_EDITION_CONFIG__': JSON.stringify({
      badge: editionConfig.badge,
      displayName: editionConfig.displayName,
      edition: editionConfig.edition,
    }),
    'globalThis.__BIGSTAR_RUNTIME_CAPABILITIES__':
      JSON.stringify(runtimeCapabilities),
    __BIGSTAR_GUI_VERSION__: JSON.stringify(appVersion),
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
    port: devPort,
    strictPort: true,
    watch: {
      ignored: ['**/src-tauri/**'],
    },
  },
});
