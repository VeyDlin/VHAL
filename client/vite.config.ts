import { defineConfig, type Plugin } from 'vite'
import vue from '@vitejs/plugin-vue'
import ui from '@nuxt/ui/vite'
import { generate } from 'vcopalette'
import { toNuxtUi, type NuxtUiTheme } from 'vcopalette/nuxt-ui'
import { paletteAliases, paletteConfig } from './palette.config'


const theme: NuxtUiTheme = toNuxtUi(generate(paletteConfig), paletteAliases)


// The palette is generated at build time and put into the page head, so the colors are there before the app starts
function palettePlugin(): Plugin {
  return {
    name: 'vhal-palette',
    transformIndexHtml() {
      return [
        {
          tag: 'style',
          attrs: { id: 'vcopalette' },
          children: theme.css,
          injectTo: 'head',
        },
      ]
    },
  }
}


export default defineConfig({
  base: '/VHAL/',
  cacheDir: process.env.VHAL_TEST_CACHE_DIR === '1' ? 'node_modules/.vite-e2e' : undefined,
  worker: { format: 'es' },
  optimizeDeps: { include: ['@huggingface/transformers'] },
  plugins: [
    vue(),
    palettePlugin(),
    ui({
      ui: {
        colors: theme.appConfig.ui.colors,
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
