import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'
import test from 'node:test'
import { createSSRApp, type Component } from 'vue'
import { renderToString } from '@vue/server-renderer'
import { createMemoryHistory, createRouter } from 'vue-router'
import { createServer } from 'vite'
import type { ApiFile } from '../src/utils/api-display.ts'

interface GeneratedPage {
  api: ApiFile[]
}

function readPage(name: string): GeneratedPage {
  return JSON.parse(readFileSync(new URL(`../src/generated/pages/${name}`, import.meta.url), 'utf8')) as GeneratedPage
}

test('renders nested API symbols, method templates, and standalone enum values', async () => {
  const vite = await createServer({
    configFile: resolve('vite.config.ts'),
    logLevel: 'silent',
    server: { middlewareMode: true },
  })

  try {
    const loadedModule = await vite.ssrLoadModule('/src/components/ApiReference.vue') as { default: Component }
    const nestedPage = readPage('Common--Drivers--Battery--BQ25798--BQ25798.h.json')
    const enumPage = readPage('Common--Drivers--Wireless--NRF8001--BLEAttribute.h.json')
    const templatePage = readPage('Periphery--Adapter--UARTAdapter.h.json')
    const app = createSSRApp(loadedModule.default, {
      api: [...nestedPage.api, ...enumPage.api, ...templatePage.api],
    })
    const router = createRouter({
      history: createMemoryHistory(),
      routes: [{ path: '/', component: loadedModule.default }],
    })

    app.use(router)
    await router.push('/')
    await router.isReady()

    const html = await renderToString(app)

    assert.match(html, /FaultStatus/)
    assert.match(html, /VoltageMeasurement/)
    assert.match(html, /BLEAttributeType/)
    assert.match(html, /BLETypeService/)
    assert.match(html, /BLETypeCharacteristic/)
    assert.match(html, /template&lt;typename DataType&gt;/)
  } finally {
    await vite.close()
  }
})
