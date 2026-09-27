import { ref, computed, type Ref } from 'vue'
import MiniSearch from 'minisearch'
import searchData from '../generated/search-index.json'
import { createSearchTarget, type SearchTarget } from '../utils/search-target'
import { rankSearchResults } from '../utils/search-ranking'
import { fuseSearchResults, lexicalQuery } from '../search/semantic-ranking.ts'
import { useSemanticSearch } from './useSemanticSearch'


export type MatchSource = 'docs' | 'api' | 'both'


export interface SearchResult {
  id: string
  title: string
  path: string
  breadcrumb: string
  terms: string[]
  score: number
  match: Record<string, string[]>
  snippet: string
  source: MatchSource
  target: SearchTarget
  exactMatch?: boolean
  meaningOnly?: boolean
}


interface SearchDoc {
  id: string
  title: string
  path: string
  breadcrumb: string
  readme: string
  symbols: string
  hasReadme: boolean
}


const miniSearch = new MiniSearch<SearchDoc>({
  fields: ['title', 'symbols', 'breadcrumb', 'readme'],
  storeFields: ['title', 'path', 'breadcrumb', 'readme', 'hasReadme'],
  searchOptions: {
    boost: { title: 5, symbols: 3, breadcrumb: 2, readme: 1 },
    fuzzy: 0.2,
    prefix: true,
    combineWith: 'OR',
    boostDocument: (...args): number => args[2]?.hasReadme ? 2 : 1,
  },
})

miniSearch.addAll(searchData as SearchDoc[])

const query: Ref<string> = ref('')
const isSearchActive = computed<boolean>(() => query.value.trim().length > 0)
const pendingSelection: Ref<SearchTarget | null> = ref(null)
const { state: semanticState, retry: retrySemantic } = useSemanticSearch(query)
const documentsByPath: Map<string, SearchDoc> = new Map((searchData as SearchDoc[]).map((document: SearchDoc) => [document.path, document]))

const results = computed<SearchResult[]>(() => {
  const text: string = query.value.trim()
  if (!text) {
    return []
  }

  const ranked: SearchResult[] = miniSearch.search(lexicalQuery(text)).map((result): SearchResult => {
    const target: SearchTarget = createSearchTarget({
      path: result.path as string,
      title: result.title as string,
      readme: result.readme as string,
      hasReadme: result.hasReadme as boolean,
    }, result.match)
    const fields: Set<string> = new Set(Object.values(result.match).flat())
    const source: MatchSource = fields.has('readme') && fields.has('symbols') ? 'both' : target.tab

    return {
      id: result.id as string,
      title: result.title as string,
      path: result.path as string,
      breadcrumb: result.breadcrumb as string,
      terms: result.terms,
      score: result.score,
      match: result.match,
      snippet: target.snippet,
      source,
      target,
      exactMatch: !/\s/u.test(text) && Object.entries(result.match).some(([term, fields]: [string, string[]]) => (
        term.toLowerCase() === text.toLowerCase() && fields.includes('symbols')
      )),
    }
  })

  const lexicalResults = rankSearchResults(ranked, text)
  if (semanticState.value.query !== text || semanticState.value.hits.length === 0) {
    return lexicalResults
  }
  const semanticResults: SearchResult[] = semanticState.value.hits.flatMap((hit): SearchResult[] => {
    const document = documentsByPath.get(hit.path)
    if (!document) {
      return []
    }
    const terms = hit.headingOnly ? [document.title] : [hit.anchor]
    const snippet = hit.text.slice(0, 180)
    return [{
      id: document.id,
      title: document.title,
      path: document.path,
      breadcrumb: document.breadcrumb,
      terms,
      score: hit.score,
      match: {},
      snippet,
      source: hit.tab,
      target: {
        path: hit.path, tab: hit.tab, terms, snippet, headingOnly: hit.headingOnly,
        ...(hit.sourceKey ? { sourceKey: hit.sourceKey } : {}),
      },
      meaningOnly: true,
    }]
  })
  return fuseSearchResults(lexicalResults, semanticResults, text)
})

const matchedPaths = computed<Set<string>>(() => new Set(results.value.map((result: SearchResult) => result.path)))


export function useSearch() {
  return { query, results, isSearchActive, matchedPaths, pendingSelection, semanticState, retrySemantic }
}
