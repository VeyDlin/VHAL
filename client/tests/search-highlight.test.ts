import assert from 'node:assert/strict'
import test from 'node:test'
import { Window } from 'happy-dom'
import { activateSearchHighlight } from '../src/utils/search-highlight.ts'
import type { SearchTarget } from '../src/utils/search-target.ts'


const target: SearchTarget = {
  path: 'Example.h', tab: 'docs', terms: ['WriteArray'], snippet: '', headingOnly: false,
}


test('highlights a match split across syntax spans without changing source text', () => {
  const window = new Window()
  window.document.body.innerHTML = '<pre><code><span>Write</span><span>Array</span>(data)</code></pre>'
  const root = window.document.body as unknown as HTMLElement
  const original: string = root.textContent ?? ''
  const session = activateSearchHighlight(root, target)

  assert.equal(Array.from(root.querySelectorAll('mark')).map((mark) => mark.textContent).join(''), 'WriteArray')
  assert.equal(root.textContent, original)
  session.dispose()
  assert.equal(root.querySelector('mark'), null)
  assert.equal(root.textContent, original)
  assert.equal(root.querySelectorAll('code > span').length, 2)
})


test('scrolls to the preview occurrence rather than an earlier repeated term', () => {
  const window = new Window()
  window.document.body.innerHTML = '<p>WriteArray for the first example.</p><p>Use WriteArray for DMA.</p>'
  const root = window.document.body as unknown as HTMLElement
  let scrolled: string | null = null
  window.HTMLElement.prototype.scrollIntoView = function (): void {
    scrolled = this.parentElement?.textContent ?? null
  }
  const session = activateSearchHighlight(root, { ...target, snippet: '...Use WriteArray for DMA....' })

  assert.equal(root.querySelector('p:first-child mark'), null)
  assert.equal(root.querySelectorAll('p')[1]?.querySelector('mark')?.textContent, 'WriteArray')
  assert.equal(scrolled, 'Use WriteArray for DMA.')
  session.dispose()
})


test('reveals a search match instantly regardless of reduced motion preference', () => {
  const window = new Window()
  window.document.body.innerHTML = '<p>WriteArray</p>'
  const root = window.document.body as unknown as HTMLElement
  let scrollOptions: ScrollIntoViewOptions | undefined
  window.matchMedia = () => ({ matches: false }) as MediaQueryList
  window.HTMLElement.prototype.scrollIntoView = function (options?: ScrollIntoViewOptions): void {
    scrollOptions = options
  }

  const session = activateSearchHighlight(root, target)

  assert.equal(scrollOptions?.behavior, 'instant')
  session.dispose()
})


test('does not match text in hidden content or heading-link controls', () => {
  const window = new Window()
  window.document.body.innerHTML = '<div hidden>WriteArray</div><a class="anchor-link">WriteArray</a><p>WriteArray</p>'
  const root = window.document.body as unknown as HTMLElement
  const session = activateSearchHighlight(root, target)

  assert.equal(root.querySelectorAll('mark').length, 1)
  assert.equal(root.querySelector('p mark')?.textContent, 'WriteArray')
  session.dispose()
})


test('reapplies after async rendering and stops permanently after Escape', async () => {
  const window = new Window()
  window.document.body.innerHTML = '<p>WriteArray</p>'
  const root = window.document.body as unknown as HTMLElement
  let dismissed: number = 0
  const session = activateSearchHighlight(root, target, () => { dismissed += 1 })
  root.innerHTML = '<pre><span>Write</span><span>Array</span></pre>'
  await window.happyDOM.whenAsyncComplete()

  assert.equal(root.querySelectorAll('mark').length, 2)
  window.document.dispatchEvent(new window.KeyboardEvent('keydown', { key: 'Escape' }))
  assert.equal(root.querySelector('mark'), null)
  assert.equal(dismissed, 1)
  root.innerHTML = '<p>WriteArray</p>'
  await window.happyDOM.whenAsyncComplete()
  assert.equal(root.querySelector('mark'), null)
  session.dispose()
})


test('a subsequent click dismisses the highlight without cancelling the click', () => {
  const window = new Window()
  window.document.body.innerHTML = '<p>WriteArray</p><button>Continue</button>'
  const root = window.document.body as unknown as HTMLElement
  const session = activateSearchHighlight(root, target)
  const event = new window.MouseEvent('click', { bubbles: true, cancelable: true })
  window.document.querySelector('button')?.dispatchEvent(event)

  assert.equal(root.querySelector('mark'), null)
  assert.equal(event.defaultPrevented, false)
  session.dispose()
})


test('title-only results target the matching heading and do not mark body text', () => {
  const window = new Window()
  window.document.body.innerHTML = '<h2>API Reference</h2><h3>WriteArray.h</h3><p>WriteArray</p>'
  const root = window.document.body as unknown as HTMLElement
  const session = activateSearchHighlight(root, { ...target, headingOnly: true })

  assert.equal(root.querySelector('h3 mark')?.textContent, 'WriteArray')
  assert.equal(root.querySelector('p mark'), null)
  session.dispose()
})


test('line breaks separate words while inline markup preserves a contiguous token', () => {
  const window = new Window()
  window.document.body.innerHTML = '<p>Write<br>Array</p><p><strong>Write</strong><em>Array</em></p>'
  const root = window.document.body as unknown as HTMLElement
  const session = activateSearchHighlight(root, target)

  assert.equal(root.querySelectorAll('p:first-child mark').length, 0)
  assert.equal(root.querySelectorAll('p:nth-child(2) mark').length, 2)
  session.dispose()
})
