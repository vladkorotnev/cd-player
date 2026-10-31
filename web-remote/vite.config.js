import { defineConfig, loadEnv } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { viteSingleFile } from 'vite-plugin-singlefile';

export default defineConfig(({ command, mode }) => {
  const { DEVICE_HOST = '' } = loadEnv(mode, process.cwd(), 'DEVICE_');
  const target = DEVICE_HOST
    ? new URL(/^https?:\/\//.test(DEVICE_HOST) ? DEVICE_HOST : `http://${DEVICE_HOST}`).origin
    : null;

  const proxy = target && {
    '/api': { target, changeOrigin: true },
    '/ws': {
      target,
      ws: true,
      changeOrigin: true,
      configure(server) {
        // The firmware accepts WebSocket handshakes only from its own Origin.
        server.on('proxyReqWs', (request) => request.setHeader('Origin', target));
      }
    }
  };

  return {
    plugins: [svelte(), ...(command === 'build' ? [viteSingleFile({ removeViteModuleLoader: true })] : [])],
    server: { proxy },
    build: {
      outDir: 'dist/device',
      emptyOutDir: true,
      minify: true,
      cssMinify: true,
      sourcemap: false
    }
  };
});
