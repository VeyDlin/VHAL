import assert from 'node:assert/strict'
import test from 'node:test'


const hash = 'a'.repeat(64)
const otherHash = 'b'.repeat(64)


async function validFixture(): Promise<{ version: string; manifest: string; artifact: string }> {
  const { computeSemanticVersion, sha256 } = await import('../src/search/semantic-artifacts.ts')
  const version = await computeSemanticVersion(hash)
  const artifact = JSON.stringify({ version, chunks: [{
    id: 'doc:docs:0', path: 'A.h', tab: 'docs', text: 'hello world', anchor: 'hello world', headingOnly: false,
    vector: [1, ...Array<number>(383).fill(0)],
  }] })
  const manifest = JSON.stringify({ schemaVersion: 1, version, corpusHash: hash, model: {
    id: 'Xenova/all-MiniLM-L6-v2', revision: '751bff37182d3f1213fa05d7196b954e230abad9', dtype: 'q8', dimensions: 384,
  }, artifact: `embeddings-${version}.json`, artifactHash: await sha256(artifact), chunkCount: 1 })
  return { version, manifest, artifact }
}


test('rejects a manifest for a different corpus before downloading its artifact', async () => {
  const module = await import('../src/search/semantic-cache.ts')
  let requests = 0
  await assert.rejects(module.loadSemanticArtifact({ corpusHash: hash, version: otherHash }, {
    fetcher: async () => {
      requests += 1
      return new Response(JSON.stringify({ schemaVersion: 1, version: otherHash, corpusHash: otherHash, model: {
        id: 'Xenova/all-MiniLM-L6-v2', revision: '751bff37182d3f1213fa05d7196b954e230abad9', dtype: 'q8', dimensions: 384,
      }, artifact: `embeddings-${otherHash}.json`, artifactHash: hash, chunkCount: 1 }), { status: 200 })
    },
  }))
  assert.equal(requests, 1)
})


test('rejects a manifest version that does not match the pinned semantic configuration', async () => {
  const { loadSemanticArtifact } = await import('../src/search/semantic-cache.ts')
  let requests = 0
  await assert.rejects(loadSemanticArtifact({ corpusHash: hash, version: otherHash }, {
    baseUrl: 'http://localhost/VHAL/',
    fetcher: async () => {
      requests += 1
      return new Response(JSON.stringify({ schemaVersion: 1, version: otherHash, corpusHash: hash, model: {
        id: 'Xenova/all-MiniLM-L6-v2', revision: '751bff37182d3f1213fa05d7196b954e230abad9', dtype: 'q8', dimensions: 384,
      }, artifact: `embeddings-${otherHash}.json`, artifactHash: hash, chunkCount: 1 }))
    },
  }), /does not match/)
  assert.equal(requests, 1)
})


test('filters stopwords only in multiword lexical queries', async () => {
  const { lexicalQuery } = await import('../src/search/semantic-ranking.ts')
  assert.equal(lexicalQuery('move the pixel smoothly'), 'move pixel smoothly')
  assert.equal(lexicalQuery('the'), 'the')
  assert.equal(lexicalQuery('the and'), 'the and')
  assert.equal(lexicalQuery('getTheValue'), 'getTheValue')
})


test('fuses duplicate page ranks and protects exact symbol results', async () => {
  const { fuseSearchResults } = await import('../src/search/semantic-ranking.ts')
  const lexical = [
    { path: 'A.h', title: 'A.h', score: 10, exactMatch: false },
    { path: 'B.h', title: 'B.h', score: 9, exactMatch: false },
    { path: 'C.h', title: 'C.h', score: 8, exactMatch: false },
  ]
  const fused = fuseSearchResults(lexical, [lexical[2]!, lexical[1]!], 'movement')
  assert.deepEqual(fused.map((result) => result.path), ['C.h', 'B.h', 'A.h'])
  assert.equal(fused.filter((result) => result.path === 'B.h').length, 1)
  const exact = { path: 'API.h', title: 'API.h', score: 1, exactMatch: true }
  assert.equal(fuseSearchResults([lexical[0]!, exact], [lexical[0]!], 'exactSymbol')[0], exact)
})


test('ignores stale worker results and retries after an error with a fresh worker', async () => {
  const { createSemanticClient } = await import('../src/search/semantic-client.ts')
  class FakeWorker {
    onmessage: ((event: MessageEvent) => void) | null = null
    onerror: ((event: ErrorEvent) => void) | null = null
    messages: unknown[] = []
    terminated = false

    postMessage(message: unknown): void {
      this.messages.push(message)
    }

    terminate(): void {
      this.terminated = true
    }

    emit(message: unknown): void {
      this.onmessage?.({ data: message } as MessageEvent)
    }
  }
  const workers: FakeWorker[] = []
  const states: string[] = []
  const client = createSemanticClient(() => {
    const worker = new FakeWorker()
    workers.push(worker)
    return worker
  }, { debounceMs: 0, initTimeoutMs: 1000, queryTimeoutMs: 1000 })
  client.subscribe((state) => states.push(state.status))
  client.setQuery('first', { baseUrl: '/', corpusHash: hash, version: otherHash })
  await new Promise((resolve) => setTimeout(resolve, 0))
  workers[0]!.emit({ type: 'ready' })
  await new Promise((resolve) => setTimeout(resolve, 0))
  client.setQuery('second', { baseUrl: '/', corpusHash: hash, version: otherHash })
  workers[0]!.emit({ type: 'results', id: 1, hits: [{ path: 'old', score: 0.9 }] })
  assert.deepEqual(client.snapshot.hits, [])
  await new Promise((resolve) => setTimeout(resolve, 0))
  workers[0]!.emit({ type: 'results', id: 2, hits: [{ path: 'new', score: 0.9 }] })
  assert.equal(client.snapshot.hits[0]?.path, 'new')
  workers[0]!.onerror?.({ message: 'worker failed' } as ErrorEvent)
  assert.equal(client.snapshot.status, 'unavailable')
  client.retry()
  assert.equal(workers[0]!.terminated, true)
  assert.equal(workers.length, 2)
  workers[0]!.emit({ type: 'error', message: 'late error' })
  assert.equal(client.snapshot.status, 'loading')
  assert.equal(workers[1]!.terminated, false)
  assert.ok(states.includes('ready'))
  client.dispose()
})


test('uses a valid version cache and refetches corrupted cached bytes', async () => {
  const { loadSemanticArtifact } = await import('../src/search/semantic-cache.ts')
  const { version, artifact, manifest } = await validFixture()
  const cached = new Map<string, Response>()
  const deleted: string[] = []
  const storage = {
    open: async () => ({
      match: async (url: string) => cached.get(url)?.clone(),
      put: async (url: string, response: Response) => { cached.set(url, response.clone()) },
      delete: async (url: string) => cached.delete(url),
    }),
    keys: async () => [`vhal-semantic-${version}`, 'vhal-semantic-old', 'unrelated-cache'],
    delete: async (name: string) => { deleted.push(name); return true },
  } as unknown as CacheStorage
  let artifactFetches = 0
  const fetcher = async (url: string | URL | Request): Promise<Response> => {
    if (String(url).endsWith('manifest.json')) {
      return new Response(manifest)
    }
    artifactFetches += 1
    return new Response(artifact)
  }
  const options = { baseUrl: 'http://localhost/VHAL/', cacheStorage: storage, fetcher: fetcher as typeof fetch }
  await loadSemanticArtifact({ corpusHash: hash, version }, options)
  await loadSemanticArtifact({ corpusHash: hash, version }, options)
  assert.equal(artifactFetches, 1)
  const url = `http://localhost/VHAL/semantic/embeddings-${version}.json`
  cached.set(url, new Response('corrupted'))
  await loadSemanticArtifact({ corpusHash: hash, version }, options)
  assert.equal(artifactFetches, 2)
  assert.deepEqual(deleted, ['vhal-semantic-old', 'vhal-semantic-old'])
})


test('continues after Cache Storage denial and surfaces failed network fetches', async () => {
  const { loadSemanticArtifact } = await import('../src/search/semantic-cache.ts')
  const { version, manifest, artifact } = await validFixture()
  const expected = { corpusHash: hash, version }
  const previous = Object.getOwnPropertyDescriptor(globalThis, 'caches')
  Object.defineProperty(globalThis, 'caches', { configurable: true, get: () => { throw new Error('denied') } })
  try {
    const loaded = await loadSemanticArtifact(expected, {
      baseUrl: 'http://localhost/VHAL/',
      fetcher: async (url: string | URL | Request) => new Response(String(url).endsWith('manifest.json') ? manifest : artifact),
    })
    assert.equal(loaded.artifact.chunks[0]?.path, 'A.h')
    await assert.rejects(loadSemanticArtifact(expected, {
      baseUrl: 'http://localhost/VHAL/', fetcher: async () => new Response('', { status: 503 }),
    }), /503/)
  } finally {
    if (previous) {
      Object.defineProperty(globalThis, 'caches', previous)
    } else {
      Reflect.deleteProperty(globalThis, 'caches')
    }
  }
})


test('continues after Cache Storage open or write fails', async () => {
  const { loadSemanticArtifact } = await import('../src/search/semantic-cache.ts')
  const { version, manifest, artifact } = await validFixture()
  const fetcher = async (url: string | URL | Request): Promise<Response> => (
    new Response(String(url).endsWith('manifest.json') ? manifest : artifact)
  )
  const openDenied = { open: async () => { throw new Error('denied') } } as unknown as CacheStorage
  const cacheWriteDenied = {
    open: async () => ({ match: async () => undefined, put: async () => { throw new Error('quota') } }),
  } as unknown as CacheStorage
  for (const cacheStorage of [openDenied, cacheWriteDenied]) {
    const loaded = await loadSemanticArtifact({ corpusHash: hash, version }, {
      baseUrl: 'http://localhost/VHAL/', fetcher: fetcher as typeof fetch, cacheStorage,
    })
    assert.equal(loaded.artifact.chunks[0]?.path, 'A.h')
  }
})


test('bounds a stalled manifest response body', async () => {
  const { loadSemanticArtifact } = await import('../src/search/semantic-cache.ts')
  const stream = new ReadableStream({ start(): void { /* deliberately stalled */ } })
  await assert.rejects(loadSemanticArtifact({ corpusHash: hash, version: otherHash }, {
    baseUrl: 'http://localhost/VHAL/', timeoutMs: 10,
    fetcher: async () => new Response(stream),
  }), /timed out/)
})


test('worker initialization watchdog preserves an unavailable state', async () => {
  const { createSemanticClient } = await import('../src/search/semantic-client.ts')
  const worker = {
    onmessage: null, onerror: null, postMessage(): void { /* stalled worker */ }, terminate(): void { /* stopped */ },
  }
  const client = createSemanticClient(() => worker, { initTimeoutMs: 10, debounceMs: 0 })
  client.setQuery('motion', { baseUrl: '/', corpusHash: hash, version: otherHash })
  await new Promise((resolve) => setTimeout(resolve, 20))
  assert.equal(client.snapshot.status, 'unavailable')
  client.dispose()
})


test('coalesces queries while inference is running and ignores a cleared query', async () => {
  const { createSemanticClient } = await import('../src/search/semantic-client.ts')
  const messages: { type: string; id?: number; text?: string }[] = []
  let onmessage: ((event: MessageEvent) => void) | null = null
  const worker = {
    get onmessage(): ((event: MessageEvent) => void) | null { return onmessage },
    set onmessage(listener: ((event: MessageEvent) => void) | null) { onmessage = listener },
    onerror: null,
    postMessage(message: { type: string; id?: number; text?: string }): void { messages.push(message) },
    terminate(): void { /* stopped */ },
  }
  const session = { baseUrl: '/', corpusHash: hash, version: otherHash }
  const client = createSemanticClient(() => worker, { debounceMs: 0, queryTimeoutMs: 1000 })
  client.setQuery('first', session)
  onmessage?.({ data: { type: 'ready' } } as MessageEvent)
  await new Promise((resolve) => setTimeout(resolve, 0))
  client.setQuery('second', session)
  client.setQuery('third', session)
  await new Promise((resolve) => setTimeout(resolve, 0))
  onmessage?.({ data: { type: 'results', id: 1, hits: [{ path: 'stale' }] } } as MessageEvent)
  assert.deepEqual(messages.filter((message) => message.type === 'query').map((message) => message.text), ['first', 'third'])
  client.setQuery('', session)
  onmessage?.({ data: { type: 'results', id: 2, hits: [{ path: 'late' }] } } as MessageEvent)
  assert.deepEqual(client.snapshot.hits, [])
  client.dispose()
})


test('inference watchdog fails safely and retry replaces the stalled worker', async () => {
  const { createSemanticClient } = await import('../src/search/semantic-client.ts')
  const workers: { onmessage: ((event: MessageEvent) => void) | null; onerror: null; postMessage: () => void; terminate: () => void }[] = []
  const client = createSemanticClient(() => {
    const worker = { onmessage: null, onerror: null, postMessage(): void { /* stalled */ }, terminate(): void { /* stopped */ } }
    workers.push(worker)
    return worker
  }, { debounceMs: 0, initTimeoutMs: 1000, queryTimeoutMs: 10 })
  client.setQuery('first', { baseUrl: '/', corpusHash: hash, version: otherHash })
  workers[0]!.onmessage?.({ data: { type: 'ready' } } as MessageEvent)
  await new Promise((resolve) => setTimeout(resolve, 0))
  await new Promise((resolve) => setTimeout(resolve, 20))
  assert.equal(client.snapshot.status, 'unavailable')
  client.retry()
  assert.equal(workers.length, 2)
  assert.equal(client.snapshot.status, 'loading')
  client.dispose()
})
