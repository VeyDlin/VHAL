import assert from 'node:assert/strict'
import test from 'node:test'


async function getRanker(): Promise<(
  results: { path: string; title: string; score: number }[],
  query: string,
) => { path: string; title: string; score: number }[]> {
  const module = await import('../src/utils/search-ranking.ts').catch(() => null)
  assert.ok(module, 'search ranking helper should be available')
  return module.rankSearchResults
}


test('ranks an exact queried filename ahead of Utilities and Utilities ahead of score', async () => {
  const rankSearchResults = await getRanker()
  const results = [
    { path: 'Periphery/Adapter/Other.h', title: 'Other', score: 1000 },
    { path: 'Utilities/Memory/ReadArray.h', title: 'ReadArray utilities', score: 100 },
    { path: 'Periphery/Adapter/ReadArray.h', title: 'ReadArray.h', score: 1 },
  ]

  assert.deepEqual(
    rankSearchResults(results, 'readarray'),
    [results[2], results[1], results[0]],
  )
})


test('treats title equality case-insensitively and allows an omitted .h extension', async () => {
  const rankSearchResults = await getRanker()
  const results = [
    { path: 'Periphery/Adapter/ReadArray.h', title: 'ReadArray.h', score: 1 },
  ]

  assert.deepEqual(
    rankSearchResults(results, 'READARRAY'),
    [results[0]],
  )
})


test('preserves input order when exactness, utility path and score tie', async () => {
  const rankSearchResults = await getRanker()
  const first = { path: 'Utilities/A.h', title: 'A', score: 5 }
  const second = { path: 'Utilities/B.h', title: 'B', score: 5 }

  assert.deepEqual(rankSearchResults([first, second], 'utility'), [first, second])
})


test('protects an exact indexed symbol match before Utilities', async () => {
  const { rankSearchResults } = await import('../src/utils/search-ranking.ts')
  const utility = { path: 'Common/Utilities/A.h', title: 'A.h', score: 100, exactMatch: false }
  const symbol = { path: 'Periphery/B.h', title: 'B.h', score: 1, exactMatch: true }
  assert.deepEqual(rankSearchResults([utility, symbol], 'exactSymbol'), [symbol, utility])
})
