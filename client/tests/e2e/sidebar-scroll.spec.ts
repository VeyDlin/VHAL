import { expect, test, type Locator } from '@playwright/test'


test('desktop navigation stays in view and its tree scrolls independently', async ({ page }) => {
  await page.setViewportSize({ width: 1280, height: 600 })
  await page.goto('docs/Periphery/Adapter/TIMAdapter.h')
  await expect(page.locator('main h1')).toBeVisible()
  const sidebar: Locator = page.locator('aside')
  await page.evaluate(() => window.scrollTo(0, 900))
  await expect.poll(() => page.evaluate(() => window.scrollY)).toBe(900)

  await expect.poll(() => sidebar.evaluate((element: HTMLElement) => element.getBoundingClientRect().top)).toBe(0)
  await expect(sidebar.getByPlaceholder('Search...')).toBeInViewport()

  const tree: Locator = sidebar.locator('.sidebar-scroll')
  await tree.evaluate((element: HTMLElement) => {
    element.scrollTop = 0
  })
  await tree.hover()
  await page.mouse.wheel(0, 250)
  await expect.poll(() => tree.evaluate((element: HTMLElement) => element.scrollTop)).toBeGreaterThan(0)
  expect(await page.evaluate(() => window.scrollY)).toBe(900)
  await expect(sidebar.getByPlaceholder('Search...')).toBeInViewport()
})
