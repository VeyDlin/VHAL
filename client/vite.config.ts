import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import ui from '@nuxt/ui/vite'

export default defineConfig({
  base: '/VHAL/',
  cacheDir: process.env.VHAL_TEST_CACHE_DIR === '1' ? 'node_modules/.vite-e2e' : undefined,
  worker: { format: 'es' },
  optimizeDeps: { include: ['@huggingface/transformers'] },
  plugins: [
    vue(),
    ui({
      colorMode: false,
      ui: {
        colors: {
          primary: 'primary',
          neutral: 'surface',
        },
        button: {
          slots: {
            base: 'cursor-pointer active:scale-99 transition-transform',
          },
        },
        contextMenu: {
          slots: {
            item: 'cursor-pointer',
          },
        },
        dropdownMenu: {
          slots: {
            item: 'cursor-pointer',
          },
        },
        navigationMenu: {
          slots: {
            link: 'cursor-pointer',
            childLink: 'cursor-pointer',
          },
        },
        select: {
          slots: {
            base: 'cursor-pointer',
            item: 'cursor-pointer',
          },
        },
        selectMenu: {
          slots: {
            base: 'cursor-pointer',
            item: 'cursor-pointer',
          },
        },
        radioGroup: {
          slots: {
            item: 'cursor-pointer',
            base: 'cursor-pointer',
            label: 'cursor-pointer',
          },
        },
        switch: {
          slots: {
            base: 'cursor-pointer',
            label: 'cursor-pointer',
          },
        },
        tree: {
          slots: {
            link: 'cursor-pointer',
          },
        },
        tabs: {
          slots: {
            trigger: 'cursor-pointer',
          },
        },
      },
    }),
  ],
})
