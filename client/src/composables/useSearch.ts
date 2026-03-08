import { ref, computed } from 'vue'
import MiniSearch from 'minisearch'
import searchData from '../generated/search-index.json'

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
  /** where the match was found */
  source: MatchSource
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
    boostDocument: (_id, _term, storedFields) => {
      return (storedFields as any)?.hasReadme ? 2 : 1
    },
  },
})

miniSearch.addAll(searchData as SearchDoc[])

const query = ref('')
const isSearchActive = computed(() => query.value.trim().length > 0)

function extractSnippet(text: string, terms: string[], maxLen = 160): string {
  if (!text || terms.length === 0) return ''

  const lower = text.toLowerCase()
  let bestPos = -1

  for (const term of terms) {
    const pos = lower.indexOf(term.toLowerCase())
    if (pos !== -1) {
      bestPos = pos
      break
    }
  }

  if (bestPos === -1) return text.slice(0, maxLen)

  const start = Math.max(0, bestPos - 60)
  const end = Math.min(text.length, start + maxLen)
  let snippet = text.slice(start, end)

  if (start > 0) snippet = '...' + snippet
  if (end < text.length) snippet += '...'

  return snippet
}

function getMatchSource(match: Record<string, string[]>): MatchSource {
  const allFields = new Set(Object.values(match).flat())
  const hasApi = allFields.has('symbols')
  const hasDocs = allFields.has('readme') || allFields.has('title') || allFields.has('breadcrumb')
  if (hasApi && hasDocs) return 'both'
  if (hasApi) return 'api'
  return 'docs'
}

const results = computed<SearchResult[]>(() => {
  const q = query.value.trim()
  if (!q) return []

  const raw = miniSearch.search(q)

  return raw.map(r => ({
    id: r.id as string,
    title: r.title as string,
    path: r.path as string,
    breadcrumb: r.breadcrumb as string,
    terms: r.terms,
    score: r.score,
    match: r.match,
    snippet: extractSnippet(r.readme as string, r.terms),
    source: getMatchSource(r.match),
  }))
})

/** Set of page paths that have search matches — for sidebar filtering */
const matchedPaths = computed<Set<string>>(() => {
  return new Set(results.value.map(r => r.path))
})

/** Set by SearchResults before navigation, consumed by DocPage on mount */
const pendingTab = ref<string | null>(null)

export function useSearch() {
  return {
    query,
    results,
    isSearchActive,
    matchedPaths,
    pendingTab,
  }
}
