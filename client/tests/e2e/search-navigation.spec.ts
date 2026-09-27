import { expect, test, type Page } from '@playwright/test'


test.beforeEach(async ({ page }) => {
  await page.route('**/semantic/manifest.json', (route) => route.abort())
})


async function openResult(page: Page, query: string, title: string): Promise<void> {
  await page.getByPlaceholder('Search...').fill(query)
  await page.locator('main').getByRole('button').filter({ hasText: title }).first().click()
}


test('keeps keyword results usable when meaning search is unavailable', async ({ page }) => {
  test.setTimeout(60_000)
  await page.goto('./')
  await page.getByPlaceholder('Search...').fill('upperBound')
  await expect(page.getByRole('status').first()).toContainText('Keyword search still works', { timeout: 45_000 })
  await expect(page.getByPlaceholder('Search...')).toHaveValue('upperBound')
  await expect(page.locator('main').getByRole('button').filter({ hasText: 'MathUtilities.h' }).first()).toBeVisible()
})


test('opens a semantic-only documentation hit at its actual anchor', async ({ page }) => {
  await page.addInitScript(() => {
    class FixtureWorker {
      onmessage: ((event: MessageEvent) => void) | null = null
      onerror: ((event: ErrorEvent) => void) | null = null

      postMessage(message: { type: string; id?: number }): void {
        if (message.type === 'init') {
          setTimeout(() => this.onmessage?.(new MessageEvent('message', { data: { type: 'ready' } })), 0)
        } else if (message.type === 'query') {
          setTimeout(() => this.onmessage?.(new MessageEvent('message', { data: {
            type: 'results', id: message.id, hits: [{
              id: 'fixture', path: 'Common/Utilities/Math/MathUtilities.h', tab: 'docs',
              text: 'lowerBound becomes 3.0f after SetMin updates the value.',
              anchor: 'lowerBound', headingOnly: false, score: 0.8,
            }],
          } })), 0)
        }
      }

      terminate(): void { /* no external worker */ }
    }
    Object.defineProperty(window, 'Worker', { value: FixtureWorker })
  })
  await page.goto('./')
  await page.getByPlaceholder('Search...').fill('nonsenserandomzz')
  const result = page.locator('main').getByRole('button').filter({ hasText: 'Related meaning' }).first()
  await expect(result).toContainText('lowerBound becomes 3.0f')
  await result.click()
  await expect(page.getByRole('tab', { name: 'Documentation', exact: true })).toHaveAttribute('aria-selected', 'true')
  await expect(page.locator('main mark[data-search-hit]').first()).toHaveText('lowerBound')
})


test('reveals a repeated API token only inside its recorded nested member', async ({ page }) => {
  await page.addInitScript(() => {
    class FixtureWorker {
      onmessage: ((event: MessageEvent) => void) | null = null
      onerror: ((event: ErrorEvent) => void) | null = null

      postMessage(message: { type: string; id?: number }): void {
        if (message.type === 'init') {
          setTimeout(() => this.onmessage?.(new MessageEvent('message', { data: { type: 'ready' } })), 0)
        } else if (message.type === 'query') {
          setTimeout(() => this.onmessage?.(new MessageEvent('message', { data: {
            type: 'results', id: message.id, hits: [{
              id: 'fixture-api', path: 'Common/Drivers/Battery/BQ25798/BQ25798Registers.h', tab: 'api',
              text: 'constexpr uint8 ChargerStatus3 static constexpr uint8',
              anchor: 'constexpr', sourceKey: 'f0/s0/public/0/public/24', headingOnly: false, score: 0.8,
            }],
          } })), 0)
        }
      }

      terminate(): void { /* no external worker */ }
    }
    Object.defineProperty(window, 'Worker', { value: FixtureWorker })
  })
  await page.goto('./')
  await page.getByPlaceholder('Search...').fill('nonsenserandomzz')
  const result = page.locator('main').getByRole('button').filter({ hasText: 'Related meaning' }).first()
  await expect(result).toContainText('constexpr uint8 ChargerStatus3')
  await result.click()
  await expect(page.locator('main').getByRole('heading', { name: 'API Reference' })).toBeVisible()
  await expect(page.locator('main [data-search-scope="f0/s0/public/0/public/24"] mark[data-search-hit]')).toHaveText('constexpr')
  await expect(page.locator('main mark[data-search-hit]')).toHaveCount(1)
})


test('retries session creation after a transient local hash failure', async ({ page }) => {
  await page.addInitScript(() => {
    const digest = crypto.subtle.digest.bind(crypto.subtle)
    let attempts = 0
    Object.defineProperty(crypto.subtle, 'digest', {
      configurable: true,
      value: (...args: Parameters<typeof digest>): ReturnType<typeof digest> => {
        attempts += 1
        return attempts === 1 ? Promise.reject(new Error('temporary digest failure')) : digest(...args)
      },
    })
    class FixtureWorker {
      onmessage: ((event: MessageEvent) => void) | null = null
      onerror: ((event: ErrorEvent) => void) | null = null

      postMessage(message: { type: string }): void {
        if (message.type === 'init') {
          setTimeout(() => this.onmessage?.(new MessageEvent('message', { data: { type: 'ready' } })), 0)
        }
      }

      terminate(): void { /* no external worker */ }
    }
    Object.defineProperty(window, 'Worker', { value: FixtureWorker })
  })
  await page.goto('./')
  await page.getByPlaceholder('Search...').fill('upperBound')
  await expect(page.getByRole('status').first()).toContainText('Keyword search still works')
  await page.getByRole('button', { name: 'Retry' }).first().click()
  await expect(page.getByRole('status').first()).toContainText('Meaning search ready')
})


test('reveals a documentation match before C++ syntax assets finish loading', async ({ page }) => {
  test.setTimeout(60_000)
  let release: (() => void) | null = null
  const gate = new Promise<void>((resolve) => { release = resolve })
  await page.route('**/cpp-*.js*', async (route) => {
    await gate
    await route.continue()
  })
  try {
    await page.goto('./')
    await openResult(page, 'upperBound', 'MathUtilities.h')
    await expect(page.getByRole('tab', { name: 'Documentation', exact: true })).toHaveAttribute('aria-selected', 'true')
    await expect(page.locator('main mark[data-search-hit]').first()).toHaveText(/upperBound/i)
    release?.()
    await expect(page.locator('main mark[data-search-hit]').first()).toHaveText(/upperBound/i)
  } finally {
    release?.()
  }
})


test('reveals an API member before syntax loads and keeps Escape dismissal', async ({ page }) => {
  test.setTimeout(60_000)
  let release: (() => void) | null = null
  const gate = new Promise<void>((resolve) => { release = resolve })
  await page.route('**/cpp-*.js*', async (route) => {
    await gate
    await route.continue()
  })
  try {
    await page.goto('./')
    await openResult(page, 'inputBusClockHz', 'TIMAdapter.h')
    await expect(page.getByRole('tab', { name: 'API Reference', exact: true })).toHaveAttribute('aria-selected', 'true')
    await expect(page.locator('main mark[data-search-hit]').first()).toHaveText(/inputBusClockHz/i)
    await page.keyboard.press('Escape')
    release?.()
    await expect(page.locator('main mark[data-search-hit]')).toHaveCount(0)
  } finally {
    release?.()
  }
})


test('opens documentation at the match, supports repeat navigation, and clears on click or Escape', async ({ page }, testInfo) => {
  await page.goto('./')
  await openResult(page, 'upperBound', 'MathUtilities.h')
  await expect(page.getByRole('tab', { name: 'Documentation', exact: true })).toHaveAttribute('aria-selected', 'true')
  const highlight = page.locator('main mark[data-search-hit]').first()
  await expect(highlight).toHaveText(/upperBound/i)
  await expect(highlight).toBeInViewport()
  await page.screenshot({ path: testInfo.outputPath('search-match.png') })
  await page.keyboard.press('Escape')
  await expect(highlight).toHaveCount(0)

  await openResult(page, 'upperBound', 'MathUtilities.h')
  await expect(highlight).toHaveText(/upperBound/i)
  await expect(highlight).toBeInViewport()
  await page.locator('main h1').click()
  await expect(highlight).toHaveCount(0)
})


test('switches to API and reveals matches inside collapsed member groups', async ({ page }) => {
  await page.goto('./')
  await openResult(page, 'inputBusClockHz', 'TIMAdapter.h')
  await expect(page.getByRole('tab', { name: 'API Reference', exact: true })).toHaveAttribute('aria-selected', 'true')
  const highlight = page.locator('main mark[data-search-hit]').first()
  await expect(highlight).toHaveText(/inputBusClockHz/i)
  await expect(highlight).toBeInViewport()
  await expect(page.getByRole('button', { name: /Protected/ }).first()).toHaveAttribute('aria-expanded', 'true')
  await page.keyboard.press('Escape')
  await expect(highlight).toHaveCount(0)
  await expect(page.getByRole('button', { name: /Protected/ }).first()).toHaveAttribute('aria-expanded', 'true')
})


test('search can move from API to documentation on the same page', async ({ page }) => {
  await page.goto('./')
  await openResult(page, 'inputBusClockHz', 'TIMAdapter.h')
  await expect(page.locator('main mark[data-search-hit]').first()).toHaveText(/inputBusClockHz/i)
  await openResult(page, 'DMARequestOption', 'TIMAdapter.h')
  await expect(page.getByRole('tab', { name: 'Documentation', exact: true })).toHaveAttribute('aria-selected', 'true')
  await expect(page.locator('main mark[data-search-hit]').first()).toHaveText(/DMARequestOption/i)
  await expect(page.locator('main mark[data-search-hit]').first()).toBeInViewport()
})


test('reveals an API match when the page has no documentation tab', async ({ page }) => {
  await page.goto('./')
  await openResult(page, 'BLEUnsignedCharCharacteristic', 'BLETypedCharacteristics.h')
  await expect(page.getByRole('tab')).toHaveCount(0)
  await expect(page.locator('main mark[data-search-hit]').first()).toHaveText(/BLEUnsignedCharCharacteristic/i)
  await expect(page.locator('main mark[data-search-hit]').first()).toBeInViewport()
})


test('typos and prefixes reveal the indexed word rather than the raw search query', async ({ page }) => {
  await page.goto('./')
  await openResult(page, 'upperBounx', 'MathUtilities.h')
  await expect(page.locator('main mark[data-search-hit]').first()).toHaveText(/upperBound/i)
  await expect(page.locator('main mark[data-search-hit]').first()).toBeInViewport()
  await openResult(page, 'inputBusClo', 'TIMAdapter.h')
  await expect(page.locator('main mark[data-search-hit]').first()).toHaveText(/inputBusClockHz/i)
  await expect(page.locator('main mark[data-search-hit]').first()).toBeInViewport()
})
