import { defineConfig } from '@playwright/test'


export default defineConfig({
  testDir: './tests/e2e',
  fullyParallel: false,
  workers: 1,
  timeout: 30_000,
  use: {
    baseURL: 'http://127.0.0.1:5174/VHAL/',
    channel: process.env.PLAYWRIGHT_CHANNEL,
    viewport: { width: 1280, height: 800 },
    reducedMotion: 'reduce',
    trace: 'retain-on-failure',
  },
  webServer: {
    command: process.env.PLAYWRIGHT_COLD_DEPS === '1'
      ? 'npm run dev -- --host 127.0.0.1 --port 5174 --strictPort --force'
      : 'npm run dev -- --host 127.0.0.1 --port 5174 --strictPort',
    url: 'http://127.0.0.1:5174/VHAL/',
    env: { VHAL_TEST_CACHE_DIR: '1' },
    reuseExistingServer: false,
  },
})
