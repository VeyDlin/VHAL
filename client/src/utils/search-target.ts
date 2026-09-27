export interface SearchTarget {
  path: string
  tab: 'docs' | 'api'
  terms: string[]
  snippet: string
  headingOnly: boolean
  sourceKey?: string
}


interface SearchDocument {
  path: string
  title: string
  readme: string
  hasReadme: boolean
}


export function createSearchTarget(
  document: SearchDocument,
  matches: Record<string, string[]>,
): SearchTarget {
  const docsTerms: string[] = Object.keys(matches).filter((term: string) => matches[term]?.includes('readme'))
  const apiTerms: string[] = Object.keys(matches).filter((term: string) => matches[term]?.includes('symbols'))
  const tab: 'docs' | 'api' = docsTerms.length > 0
    ? 'docs'
    : apiTerms.length > 0 || !document.hasReadme ? 'api' : 'docs'
  const terms: string[] = docsTerms.length > 0 ? docsTerms : apiTerms
  const headingOnly: boolean = terms.length === 0
  let snippet: string = ''

  if (tab === 'docs' && !headingOnly) {
    const positions: number[] = terms.map((term: string) => document.readme.toLowerCase().indexOf(term.toLowerCase()))
      .filter((position: number) => position >= 0)
    const position: number = positions.length > 0 ? Math.min(...positions) : 0
    const start: number = Math.max(0, position - 60)
    const end: number = Math.min(document.readme.length, Math.max(start + 160, position + terms[0]!.length))
    snippet = `${start > 0 ? '...' : ''}${document.readme.slice(start, end)}${end < document.readme.length ? '...' : ''}`
  } else if (!headingOnly) {
    // The API index contains tokens, not rendered signatures. Show the exact token we will reveal.
    snippet = terms[0] ?? ''
  }

  return {
    path: document.path,
    tab,
    terms: headingOnly ? Object.keys(matches) : tab === 'api' ? terms.slice(0, 1) : terms,
    snippet,
    headingOnly,
  }
}
