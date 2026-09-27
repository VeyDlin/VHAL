import assert from 'node:assert/strict'
import test from 'node:test'
import { createSearchTarget } from '../src/utils/search-target.ts'


test('API matches are not sent to docs merely because the title also matched', () => {
  const target = createSearchTarget({
    path: 'Driver.h', title: 'Driver', readme: 'General introduction', hasReadme: true,
  }, { driver: ['title'], send: ['symbols'] })

  assert.equal(target.tab, 'api')
  assert.deepEqual(target.terms, ['send'])
  assert.equal(target.snippet, 'send')
  assert.equal(target.headingOnly, false)
})


test('documentation matches keep the preview context and only its matching terms', () => {
  const target = createSearchTarget({
    path: 'Driver.h', title: 'Driver', readme: 'Call Start after setup.', hasReadme: true,
  }, { driver: ['title'], start: ['readme', 'symbols'], stop: ['symbols'] })

  assert.equal(target.tab, 'docs')
  assert.equal(target.snippet, 'Call Start after setup.')
  assert.deepEqual(target.terms, ['start'])
})


test('title-only matches fall back to a heading on an available tab', () => {
  const target = createSearchTarget({
    path: 'Driver.h', title: 'Driver', readme: '', hasReadme: false,
  }, { driver: ['title'] })

  assert.equal(target.tab, 'api')
  assert.equal(target.headingOnly, true)
  assert.equal(target.snippet, '')
  assert.deepEqual(target.terms, ['driver'])
})
