import react from '@vitejs/plugin-react';
import { playwright } from '@vitest/browser-playwright';
import tsconfigPaths from 'vite-tsconfig-paths';
import { defineConfig } from 'vitest/config';

export default defineConfig({
  optimizeDeps: {
    include: ['@ark-ui/react/collapsible', '@tauri-apps/api/window'],
  },
  plugins: [react(), tsconfigPaths()],
  test: {
    include: ['src/**/*.browser.test.{ts,tsx}'],
    browser: {
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
