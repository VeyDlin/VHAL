import { expect, test, type Locator, type Page } from '@playwright/test'


const colorModeButtonName: RegExp = /switch to (dark|light) mode/i


// The color mode button of the visible header (desktop sidebar or mobile header)
function colorModeButton(page: Page): Locator {
  return page.getByRole('button', { name: colorModeButtonName }).filter({ visible: true }).first()
}


async function setColorMode(page: Page, mode: 'light' | 'dark'): Promise<void> {
  const html: Locator = page.locator('html')
  const isDark: boolean = await html.evaluate((element: HTMLElement) => element.classList.contains('dark'))
  if (isDark !== (mode === 'dark')) {
    await colorModeButton(page).click()
  }
  if (mode === 'dark') {
    await expect(html).toHaveClass(/\bdark\b/)
  } else {
    await expect(html).not.toHaveClass(/\bdark\b/)
  }
}


test('follows the system until the button sets a mode, and keeps that mode after a reload', async ({ page }) => {
  const html: Locator = page.locator('html')
  await page.emulateMedia({ colorScheme: 'light' })
  await page.goto('./')
  await expect(html).not.toHaveClass(/\bdark\b/)

  await page.emulateMedia({ colorScheme: 'dark' })
  await expect(html).toHaveClass(/\bdark\b/)

  await colorModeButton(page).click()
  await expect(html).not.toHaveClass(/\bdark\b/)
  await expect.poll(() => page.evaluate(() => localStorage.getItem('vueuse-color-scheme'))).toBe('light')

  await page.emulateMedia({ colorScheme: 'light' })
  await page.emulateMedia({ colorScheme: 'dark' })
  await expect(html).not.toHaveClass(/\bdark\b/)

  await page.reload()
  await expect(html).not.toHaveClass(/\bdark\b/)

  await colorModeButton(page).click()
  await expect(html).toHaveClass(/\bdark\b/)
  await page.emulateMedia({ colorScheme: 'light' })
  await expect(html).toHaveClass(/\bdark\b/)
})


test('keeps the mobile header controls visible at 390 pixels', async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 })
  await page.goto('./')

  const header: Locator = page.locator('.mobile-header')
  const theme: Locator = header.getByRole('button', { name: colorModeButtonName })
  const menu: Locator = header.getByRole('button', { name: 'Open navigation' })
  const github: Locator = header.getByRole('link', { name: 'VHAL on GitHub' })

  await expect(theme).toBeInViewport()
  await expect(menu).toBeInViewport()
  await expect(github).toBeInViewport()
})


test('allows wide documentation tables to scroll inside the mobile viewport', async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 })
  await page.goto('docs/Periphery/Adapter/TIMAdapter.h')
  await expect(page.locator('.doc-content pre.shiki-highlighted').first()).toBeVisible()

  const table = page.locator('.doc-content table').first()
  await expect(table).toBeVisible()
  const dimensions = await table.evaluate((element: HTMLElement) => ({
    clientWidth: element.clientWidth,
    scrollWidth: element.scrollWidth,
  }))
  expect(dimensions.scrollWidth).toBeGreaterThan(dimensions.clientWidth)
  await table.evaluate((element: HTMLElement) => {
    element.scrollLeft = 0
  })
  await table.hover()
  await page.mouse.wheel(240, 0)
  await expect.poll(() => table.evaluate((element: HTMLElement) => element.scrollLeft)).toBeGreaterThan(0)
})


test('remains usable when browser storage access is denied', async ({ page }) => {
  await page.addInitScript(() => {
    Object.defineProperty(window, 'localStorage', {
      configurable: true,
      get: () => {
        throw new DOMException('Storage access denied', 'SecurityError')
      },
    })
  })
  await page.emulateMedia({ colorScheme: 'light' })
  await page.goto('./')

  await setColorMode(page, 'dark')
  await expect(page.locator('main h1, main h2').first()).toBeVisible()
})


test('keeps documentation and API syntax readable while switching Shiki colors', async ({ page }) => {
  await page.emulateMedia({ colorScheme: 'light' })
  await page.goto('docs/Periphery/Adapter/TIMAdapter.h')
  const proseHeading = page.locator('.doc-content h1')
  const proseBody = page.locator('.doc-content p').first()
  await expect(proseHeading).toBeVisible()
  await expect(proseBody).toBeVisible()
  expect(await proseHeading.evaluate((element: HTMLElement) => getComputedStyle(element).color))
    .not.toBe('rgb(255, 255, 255)')
  expect(await proseBody.evaluate((element: HTMLElement) => getComputedStyle(element).color))
    .not.toBe('rgb(255, 255, 255)')

  const markdownToken = page.locator('.doc-content pre.shiki-highlighted span').first()
  await expect(markdownToken).toBeVisible()
  const markdownCode = page.locator('.doc-content pre.shiki-highlighted code').first()
  const markdownHtml: string = await markdownCode.innerHTML()
  const markdownLightColor: string = await markdownToken.evaluate((element: HTMLElement) => getComputedStyle(element).color)
  await setColorMode(page, 'dark')
  const markdownDarkColor: string = await markdownToken.evaluate((element: HTMLElement) => getComputedStyle(element).color)
  expect(markdownDarkColor).not.toBe(markdownLightColor)
  expect(await markdownCode.innerHTML()).toBe(markdownHtml)

  await page.getByRole('tab', { name: 'API Reference', exact: true }).click()
  const apiToken = page.locator('.shiki-inline span').first()
  await expect(apiToken).toBeVisible()
  const apiHtml: string | null = await apiToken.evaluate((element: HTMLElement) => element.parentElement?.innerHTML ?? null)
  const apiDarkColor: string = await apiToken.evaluate((element: HTMLElement) => getComputedStyle(element).color)
  await setColorMode(page, 'light')
  const apiLightColor: string = await apiToken.evaluate((element: HTMLElement) => getComputedStyle(element).color)
  expect(apiLightColor).not.toBe(apiDarkColor)
  expect(await apiToken.evaluate((element: HTMLElement) => element.parentElement?.innerHTML ?? null)).toBe(apiHtml)
})
