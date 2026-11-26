import { defineConfig } from 'vite';
import { resolve } from 'node:path';

export default defineConfig({
  base: '/',
  resolve: {
    alias: [{ find: /^@i18n\//, replacement: resolve(__dirname, 'src/i18n/zh') + '/' }],
  },
  define: {
    __LANG__: JSON.stringify('zh'),
    __GEN_VER__: JSON.stringify({}),
    __IMG_VER__: JSON.stringify(''),
  },
  build: {
    target: 'es2022',
    emptyOutDir: true,
    modulePreload: { polyfill: false },
    rollupOptions: {
      input: {
        index: resolve(__dirname, 'index.html'),
        ep11_index: resolve(__dirname, 'ep11/index.html'),
        ep11_play: resolve(__dirname, 'ep11/play.html'),
        ep11_watch: resolve(__dirname, 'ep11/watch.html'),
        ep11_replay: resolve(__dirname, 'ep11/replay.html'),
        ep11_source: resolve(__dirname, 'ep11/source.html'),
      },
    },
  },
  worker: { format: 'es' },
  server: { port: 4374, open: '/ep11/' },
  preview: { port: 4374 },
});
