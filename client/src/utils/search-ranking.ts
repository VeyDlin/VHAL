export interface RankableSearchResult {
  path: string
  title: string
  score: number
  exactMatch?: boolean
}


export function rankSearchResults<T extends RankableSearchResult>(results: T[], query: string): T[] {
  const normalizedQuery: string = normalizeFilename(query)

  return results
    .map((result: T, index: number) => ({
      result,
      index,
      exact: normalizeFilename(result.title) === normalizedQuery
        || (!/\s/u.test(query.trim()) && result.exactMatch === true),
      utilities: /(?:^|\/)Utilities(?:\/|$)/i.test(result.path),
    }))
    .sort((left, right) => {
      if (left.exact !== right.exact) {
        return left.exact ? -1 : 1
      }
      if (left.utilities !== right.utilities) {
        return left.utilities ? -1 : 1
      }
      if (left.result.score !== right.result.score) {
        return right.result.score - left.result.score
      }
      return left.index - right.index
    })
    .map(({ result }) => result)
}


function normalizeFilename(value: string): string {
  return value.trim().replace(/\.h$/i, '').toLowerCase()
}
