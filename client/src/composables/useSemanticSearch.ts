import { ref, watch, type Ref } from 'vue'
import searchData from '../generated/search-index.json'
import { computeCorpusHash, computeSemanticVersion } from '../search/semantic-artifacts.ts'
import { createSemanticClient, type SemanticSession, type SemanticState } from '../search/semantic-client.ts'


const semanticState: Ref<SemanticState> = ref({ status: 'idle', phase: '', percent: null, hits: [], query: '' })
const client = createSemanticClient(() => new Worker(new URL('../search/semantic-worker.ts', import.meta.url), { type: 'module' }))
client.subscribe((state: SemanticState): void => {
  semanticState.value = state
})
let sessionPromise: Promise<SemanticSession> | null = null


function getSession(): Promise<SemanticSession> {
  sessionPromise ??= (async (): Promise<SemanticSession> => {
    const corpusHash = await computeCorpusHash(searchData)
    return {
      baseUrl: import.meta.env.BASE_URL,
      corpusHash,
      version: await computeSemanticVersion(corpusHash),
    }
  })()
  return sessionPromise
}


function sessionUnavailable(text: string): void {
  sessionPromise = null
  semanticState.value = {
    status: 'unavailable', phase: 'Meaning search is unavailable. Keyword search still works.',
    percent: null, hits: [], query: text,
  }
}


export function useSemanticSearch(query: Ref<string>): { state: Ref<SemanticState>; retry: () => void } {
  watch(query, async (text: string): Promise<void> => {
    if (!text.trim()) {
      client.setQuery('', { baseUrl: import.meta.env.BASE_URL, corpusHash: '', version: '' })
      return
    }
    try {
      const session = await getSession()
      if (query.value === text) {
        client.setQuery(text, session)
      }
    } catch {
      sessionUnavailable(text)
    }
  }, { immediate: true })
  return {
    state: semanticState,
    retry(): void {
      if (!query.value.trim()) {
        return
      }
      if (client.snapshot.status === 'unavailable') {
        client.retry()
        return
      }
      void getSession().then((session: SemanticSession): void => {
        client.setQuery(query.value, session)
      }).catch((): void => sessionUnavailable(query.value))
    },
  }
}
