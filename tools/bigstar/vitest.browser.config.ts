import react from '@vitejs/plugin-react';
import { playwright } from '@vitest/browser-playwright';
import tsconfigPaths from 'vite-tsconfig-paths';
import { defineConfig } from 'vitest/config';

export default defineConfig({
  define: {
    __BIGSTAR_GUI_VERSION__: JSON.stringify('test'),
  },
  optimizeDeps: {
    include: [
      '@base-ui/react/alert-dialog',
      '@base-ui/react/collapsible',
      '@base-ui/react/dialog',
      '@base-ui/react/field',
      '@base-ui/react/radio',
      '@base-ui/react/radio-group',
      '@base-ui/react/select',
      '@base-ui/react/switch',
      '@base-ui/react/toggle',
      '@base-ui/react/toggle-group',
      '@tauri-apps/api/webview',
      '@tauri-apps/api/window',
    ],
  },
  plugins: [
    react({
      babel: {
        plugins: ['babel-plugin-react-compiler'],
      },
    }),
    tsconfigPaths(),
  ],
  test: {
    include: ['src/**/*.browser.test.{ts,tsx}'],
    browser: {
      api: {
        host: '127.0.0.1',
        port: 1422,
        strictPort: true,
      },
      enabled: true,
      headless: true,
      provider: playwright(),
      instances: [{ browser: 'chromium' }],
      viewport: {
        height: 900,
        width: 1440,
      },
    },
  },
});
