export interface SemanticHit {
  id: string
  path: string
  tab: 'docs' | 'api'
  text: string
  anchor: string
  headingOnly: boolean
  sourceKey?: string
  score: number
}

export interface SemanticSession {
  baseUrl: string
  corpusHash: string
  version: string
}

export interface SemanticState {
  status: 'idle' | 'loading' | 'ready' | 'unavailable'
  phase: string
  percent: number | null
  hits: SemanticHit[]
  query: string
}

export interface SemanticWorkerLike {
  onmessage: ((event: MessageEvent) => void) | null
  onerror: ((event: ErrorEvent) => void) | null
  postMessage(message: unknown): void
  terminate(): void
}

export interface SemanticClientOptions {
  debounceMs?: number
  initTimeoutMs?: number
  queryTimeoutMs?: number
}

export interface SemanticClient {
  readonly snapshot: SemanticState
  subscribe(listener: (state: SemanticState) => void): () => void
  setQuery(query: string, session: SemanticSession): void
  retry(): void
  dispose(): void
}


export function createSemanticClient(
  workerFactory: () => SemanticWorkerLike,
  options: SemanticClientOptions = {},
): SemanticClient {
  const debounceMs = options.debounceMs ?? 200
  const initTimeoutMs = options.initTimeoutMs ?? 180_000
  const queryTimeoutMs = options.queryTimeoutMs ?? 15_000
  const listeners = new Set<(state: SemanticState) => void>()
  let state: SemanticState = { status: 'idle', phase: '', percent: null, hits: [], query: '' }
  let worker: SemanticWorkerLike | null = null
  let session: SemanticSession | null = null
  let sequence = 0
  let activeId: number | null = null
  let debounceTimer: ReturnType<typeof setTimeout> | null = null
  let watchdog: ReturnType<typeof setTimeout> | null = null
  let pending = false
  let debounceReady = false
  let disposed = false

  function emit(next: Partial<SemanticState>): void {
    state = { ...state, ...next }
    listeners.forEach((listener) => listener(state))
  }

  function clearWatchdog(): void {
    if (watchdog !== null) {
      clearTimeout(watchdog)
      watchdog = null
    }
  }

  function fail(): void {
    clearWatchdog()
    worker?.terminate()
    worker = null
    activeId = null
    pending = false
    emit({ status: 'unavailable', phase: 'Meaning search is unavailable. Keyword search still works.', percent: null, hits: [] })
  }

  function dispatch(): void {
    if (disposed || !worker || state.status !== 'ready' || !pending || !debounceReady || activeId !== null || !state.query) {
      return
    }
    pending = false
    debounceReady = false
    activeId = ++sequence
    const id = activeId
    watchdog = setTimeout(fail, queryTimeoutMs)
    try {
      worker.postMessage({ type: 'query', id, text: state.query })
    } catch {
      fail()
    }
  }

  function start(): void {
    if (disposed || !session || !state.query || worker) {
      return
    }
    try {
      const currentWorker = workerFactory()
      worker = currentWorker
      currentWorker.onmessage = (event: MessageEvent): void => {
        if (disposed || worker !== currentWorker) {
          return
        }
        const message: unknown = event.data
        if (typeof message !== 'object' || message === null || !('type' in message)) {
          return
        }
        if (message.type === 'progress' && 'phase' in message) {
          emit({ phase: String(message.phase), percent: 'percent' in message && typeof message.percent === 'number' ? message.percent : null })
        } else if (message.type === 'ready') {
          clearWatchdog()
          emit({ status: 'ready', phase: 'Meaning search ready', percent: 100 })
          dispatch()
        } else if (message.type === 'results' && 'id' in message && message.id === activeId && 'hits' in message) {
          clearWatchdog()
          activeId = null
          if (!pending && state.query) {
            emit({ hits: Array.isArray(message.hits) ? message.hits as SemanticHit[] : [] })
          }
          dispatch()
        } else if (message.type === 'error') {
          fail()
        }
      }
      currentWorker.onerror = (): void => {
        if (!disposed && worker === currentWorker) {
          fail()
        }
      }
      emit({ status: 'loading', phase: 'Loading meaning search…', percent: null, hits: [] })
      watchdog = setTimeout(fail, initTimeoutMs)
      currentWorker.postMessage({ type: 'init', ...session })
    } catch {
      fail()
    }
  }

  return {
    get snapshot(): SemanticState {
      return state
    },
    subscribe(listener: (value: SemanticState) => void): () => void {
      listeners.add(listener)
      listener(state)
      return (): void => { listeners.delete(listener) }
    },
    setQuery(query: string, nextSession: SemanticSession): void {
      session = nextSession
      if (debounceTimer !== null) {
        clearTimeout(debounceTimer)
      }
      emit({ query: query.trim(), hits: [] })
      pending = Boolean(state.query)
      debounceReady = false
      if (!state.query) {
        return
      }
      if (state.status === 'idle') {
        start()
      }
      debounceTimer = setTimeout((): void => {
        debounceReady = true
        dispatch()
      }, debounceMs)
    },
    retry(): void {
      if (debounceTimer !== null) {
        clearTimeout(debounceTimer)
      }
      clearWatchdog()
      worker?.terminate()
      worker = null
      activeId = null
      pending = Boolean(state.query)
      debounceReady = true
      emit({ status: 'idle', percent: null, hits: [] })
      start()
    },
    dispose(): void {
      disposed = true
      if (debounceTimer !== null) {
        clearTimeout(debounceTimer)
      }
      clearWatchdog()
      worker?.terminate()
      worker = null
      listeners.clear()
    },
  }
}
