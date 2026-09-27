const STOPWORDS: ReadonlySet<string> = new Set([
  'a', 'an', 'and', 'are', 'as', 'at', 'be', 'by', 'for', 'from', 'how', 'in', 'into',
  'is', 'it', 'of', 'on', 'or', 'the', 'to', 'with', 'what', 'where', 'when', 'why',
])

export interface FusionResult {
  path: string
  title: string
  score: number
  exactMatch?: boolean
}


export function lexicalQuery(query: string): string {
  const words = query.trim().split(/\s+/u)
  if (words.length < 2) {
    return query
  }
  const filtered = words.filter((word: string) => !STOPWORDS.has(word.toLowerCase()))
  return filtered.length > 0 ? filtered.join(' ') : query
}


export function fuseSearchResults<T extends FusionResult>(lexical: T[], semantic: T[], query: string): T[] {
  if (semantic.length === 0) {
    return lexical
  }
  const entries = new Map<string, { result: T; rank: number; order: number }>()
  lexical.forEach((result: T, index: number) => {
    entries.set(result.path, { result, rank: 1 / (61 + index), order: index })
  })
  semantic.forEach((result: T, index: number) => {
    const existing = entries.get(result.path)
    if (existing) {
      existing.rank += 1 / (61 + index)
    } else {
      entries.set(result.path, { result, rank: 1 / (61 + index), order: lexical.length + index })
    }
  })
  const exactQuery = /^\S+$/u.test(query.trim())
  return [...entries.values()].sort((left, right): number => {
    const leftExact = left.result.title.replace(/\.h$/iu, '').toLowerCase() === query.trim().replace(/\.h$/iu, '').toLowerCase()
      || (exactQuery && left.result.exactMatch === true)
    const rightExact = right.result.title.replace(/\.h$/iu, '').toLowerCase() === query.trim().replace(/\.h$/iu, '').toLowerCase()
      || (exactQuery && right.result.exactMatch === true)
    if (leftExact !== rightExact) {
      return leftExact ? -1 : 1
    }
    const leftUtility = /(?:^|\/)Utilities(?:\/|$)/iu.test(left.result.path)
    const rightUtility = /(?:^|\/)Utilities(?:\/|$)/iu.test(right.result.path)
    if (leftUtility !== rightUtility) {
      return leftUtility ? -1 : 1
    }
    return right.rank - left.rank || left.order - right.order
  }).map((entry) => entry.result)
}
