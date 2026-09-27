import assert from 'node:assert/strict'
import test from 'node:test'
import { findTextMatch } from '../src/utils/search-match.ts'


test('finds case-insensitive literal matches with original UTF-16 offsets', () => {
  assert.deepEqual(findTextMatch('Before Widget after', ['widget']), { start: 7, end: 13 })
})


test('matches regex metacharacters literally', () => {
  assert.deepEqual(findTextMatch('a+b and aaab', ['a+b']), { start: 0, end: 3 })
})


test('prefers a term occurrence inside the matching snippet context', () => {
  const text = 'Alpha is here. Later, Beta appears. Alpha returns.'

  assert.deepEqual(findTextMatch(text, ['alpha', 'beta'], '...Later, Beta appears.\u2026'), { start: 22, end: 26 })
})


test('normalizes whitespace while preserving original offsets', () => {
  const text = 'Before Launch\n\u00a0Control after'

  assert.deepEqual(findTextMatch(text, ['launch control']), { start: 7, end: 22 })
})


test('chooses the longer term when terms match at the same offset', () => {
  assert.deepEqual(findTextMatch('Control Unit', ['control', 'control unit']), { start: 0, end: 12 })
})


test('ignores empty terms and returns null when nothing matches', () => {
  assert.equal(findTextMatch('Visible text', ['', '   ', '\n\u00a0']), null)
  assert.equal(findTextMatch('Visible text', ['missing']), null)
  assert.equal(findTextMatch('', ['visible']), null)
})


test('falls back to the first term when the snippet context is absent', () => {
  assert.deepEqual(findTextMatch('First Link, then Link', ['link'], 'missing excerpt'), { start: 6, end: 10 })
})
