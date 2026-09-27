import { expect, test } from '@playwright/test'


test('persists an explicit theme and follows the system only in System mode', async ({ page }) => {
  await page.goto('./')
  const theme = page.getByRole('combobox', { name: 'Theme' }).first()

  await expect(theme).toHaveValue('system')
  await theme.selectOption('dark')
  await expect(page.locator('html')).toHaveClass(/\bdark\b/)
  await expect.poll(() => page.evaluate(() => localStorage.getItem('vhal-theme'))).toBe('dark')

  await page.emulateMedia({ colorScheme: 'dark' })
  await page.emulateMedia({ colorScheme: 'light' })
  await expect(page.locator('html')).toHaveClass(/\bdark\b/)

  await page.reload()
  await expect(page.locator('html')).toHaveClass(/\bdark\b/)
  await expect(page.getByRole('combobox', { name: 'Theme' }).first()).toHaveValue('dark')

  await page.getByRole('combobox', { name: 'Theme' }).first().selectOption('light')
  await page.emulateMedia({ colorScheme: 'dark' })
  await expect(page.locator('html')).not.toHaveClass(/\bdark\b/)

  await page.emulateMedia({ colorScheme: 'light' })
  await page.getByRole('combobox', { name: 'Theme' }).first().selectOption('system')
  await expect(page.locator('html')).not.toHaveClass(/\bdark\b/)
  await page.emulateMedia({ colorScheme: 'dark' })
  await expect(page.locator('html')).toHaveClass(/\bdark\b/)
})


test('keeps the mobile header controls visible at 390 pixels', async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 })
  await page.goto('./')

  const header = page.locator('.mobile-header')
  const theme = header.getByRole('combobox', { name: 'Theme' })
  const menu = header.getByRole('button', { name: 'Open navigation' })
  const github = header.getByRole('link', { name: 'VHAL on GitHub' })

  await expect(theme).toBeVisible()
  await expect(menu).toBeInViewport()
  await expect(github).toBeInViewport()
  const themeBox = await theme.boundingBox()
  expect(themeBox?.x).toBeGreaterThanOrEqual(0)
  expect((themeBox?.x ?? 0) + (themeBox?.width ?? 0)).toBeLessThanOrEqual(390)
})


test('allows wide documentation tables to scroll inside the mobile viewport', async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 })
  await page.goto('docs/Periphery/Adapter/TIMAdapter.h')

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
  await page.goto('./')

  const theme = page.getByRole('combobox', { name: 'Theme' }).first()
  await theme.selectOption('dark')
  await expect(page.locator('html')).toHaveClass(/\bdark\b/)
  await expect(page.locator('main h1, main h2').first()).toBeVisible()
})


test('keeps documentation and API syntax readable while switching Shiki colors', async ({ page }) => {
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
  await page.getByRole('combobox', { name: 'Theme' }).first().selectOption('dark')
  const markdownDarkColor: string = await markdownToken.evaluate((element: HTMLElement) => getComputedStyle(element).color)
  expect(markdownDarkColor).not.toBe(markdownLightColor)
  expect(await markdownCode.innerHTML()).toBe(markdownHtml)

  await page.getByRole('tab', { name: 'API Reference', exact: true }).click()
  const apiToken = page.locator('.shiki-inline span').first()
  await expect(apiToken).toBeVisible()
  const apiHtml: string | null = await apiToken.evaluate((element: HTMLElement) => element.parentElement?.innerHTML ?? null)
  const apiDarkColor: string = await apiToken.evaluate((element: HTMLElement) => getComputedStyle(element).color)
  await page.getByRole('combobox', { name: 'Theme' }).first().selectOption('light')
  const apiLightColor: string = await apiToken.evaluate((element: HTMLElement) => getComputedStyle(element).color)
  expect(apiLightColor).not.toBe(apiDarkColor)
  expect(await apiToken.evaluate((element: HTMLElement) => element.parentElement?.innerHTML ?? null)).toBe(apiHtml)
})
