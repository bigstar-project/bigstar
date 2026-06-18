import react from '@vitejs/plugin-react';
import { defineConfig } from 'vite';
import tsconfigPaths from 'vite-tsconfig-paths';
import packageJson from './package.json';

export default defineConfig({
  clearScreen: false,
  define: {
    __NSMB_MVL_GUI_VERSION__: JSON.stringify(packageJson.version),
  },
  plugins: [react(), tsconfigPaths()],
  server: {
    host: '127.0.0.1',
    port: 1420,
    strictPort: true,
    watch: {
      ignored: ['**/src-tauri/**'],
    },
  },
});
