import { defineConfig, loadEnv, type ProxyOptions } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { viteSingleFile } from 'vite-plugin-singlefile';

export default defineConfig(({ command, mode }) => {
  const { DEVICE_HOST = '' } = loadEnv(mode, process.cwd(), 'DEVICE_');
  const target = DEVICE_HOST
    ? new URL(/^https?:\/\//.test(DEVICE_HOST) ? DEVICE_HOST : `http://${DEVICE_HOST}`).origin
    : null;

  const proxy: Record<string, ProxyOptions> | undefined = target ? {
    '/api': {
      target,
      changeOrigin: true,
      configure(server) {
        server.on('proxyReq', (request) => {
          // The installed firmware accepts only 512 bytes of request headers.
          // Browser-only headers are unnecessary for this API during development.
          const keep = new Set(['host', 'content-type', 'content-length', 'transfer-encoding', 'authorization']);
          for (const header of Object.keys(request.getHeaders())) {
            if (!keep.has(header)) request.removeHeader(header);
          }
          // Preference writes enforce the same Origin check as WebSocket handshakes.
          request.setHeader('Origin', target);
        });
      }
    },
    '/ws': {
      target,
      ws: true,
      changeOrigin: true,
      configure(server) {
        // The firmware accepts WebSocket handshakes only from its own Origin.
        server.on('proxyReqWs', (request) => request.setHeader('Origin', target));
      }
    }
  } : undefined;

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
