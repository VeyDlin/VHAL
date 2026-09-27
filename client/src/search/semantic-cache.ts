import { computeSemanticVersion, sha256, validateSemanticArtifact, validateSemanticManifest } from './semantic-artifacts.ts'
import type { SemanticArtifact, SemanticManifest } from './semantic-contract.ts'


export interface SemanticCacheOptions {
  baseUrl?: string
  fetcher?: typeof fetch
  cacheStorage?: CacheStorage
  timeoutMs?: number
}

export interface LoadedSemanticArtifact {
  manifest: SemanticManifest
  artifact: SemanticArtifact
}


const cachePrefix = 'vhal-semantic-'


export async function loadSemanticArtifact(
  expected: { corpusHash: string; version: string },
  options: SemanticCacheOptions = {},
): Promise<LoadedSemanticArtifact> {
  const fetcher = options.fetcher ?? fetch
  const baseUrl = options.baseUrl ?? '/VHAL/'
  const timeoutMs = options.timeoutMs ?? 15_000
  const semanticUrl = new URL('semantic/', new URL(baseUrl, globalThis.location?.href ?? 'http://localhost/'))
  const manifestUrl = new URL('manifest.json', semanticUrl)
  const manifestValue: unknown = JSON.parse(await timedFetchText(fetcher, manifestUrl.toString(), timeoutMs))
  if (!validateSemanticManifest(manifestValue)
    || manifestValue.corpusHash !== expected.corpusHash
    || manifestValue.version !== expected.version
    || manifestValue.version !== await computeSemanticVersion(expected.corpusHash)) {
    throw new Error('Semantic manifest does not match the bundled search index')
  }
  const manifest = manifestValue
  const artifactUrl = new URL(manifest.artifact, semanticUrl)
  if (artifactUrl.origin !== semanticUrl.origin || !artifactUrl.pathname.startsWith(semanticUrl.pathname)
    || artifactUrl.pathname !== `${semanticUrl.pathname}${manifest.artifact}`) {
    throw new Error('Semantic artifact URL escapes the semantic directory')
  }
  let storage: CacheStorage | undefined
  try {
    storage = options.cacheStorage ?? globalThis.caches
  } catch {
    storage = undefined
  }
  const cacheName = `${cachePrefix}${manifest.version}`
  let cache: Cache | null = null
  try {
    cache = storage ? await storage.open(cacheName) : null
  } catch {
    cache = null
  }
  if (cache) {
    try {
      const hit = await cache.match(artifactUrl.toString())
      if (hit) {
        const artifact = await parseArtifact(await hit.clone().text(), manifest)
        return { manifest, artifact }
      }
    } catch {
      try {
        await cache.delete(artifactUrl.toString())
      } catch {
        // Cache Storage can be unavailable after a successful open.
      }
    }
  }
  const artifactText = await timedFetchText(fetcher, artifactUrl.toString(), timeoutMs)
  const artifact = await parseArtifact(artifactText, manifest)
  if (cache) {
    try {
      await cache.put(artifactUrl.toString(), new Response(artifactText))
      const names = await storage!.keys()
      await Promise.all(names.filter((name: string) => name.startsWith(cachePrefix) && name !== cacheName)
        .map((name: string) => storage!.delete(name)))
    } catch {
      // A valid in-memory artifact remains usable when caching is denied.
    }
  }
  return { manifest, artifact }
}


async function parseArtifact(text: string, manifest: SemanticManifest): Promise<SemanticArtifact> {
  if (await sha256(text) !== manifest.artifactHash) {
    throw new Error('Semantic artifact integrity check failed')
  }
  const value: unknown = JSON.parse(text)
  if (!validateSemanticArtifact(value, manifest)) {
    throw new Error('Semantic artifact is invalid')
  }
  return value
}


async function timedFetchText(fetcher: typeof fetch, url: string, timeoutMs: number): Promise<string> {
  const controller = new AbortController()
  let timer: ReturnType<typeof setTimeout> | null = null
  const timeout = new Promise<never>((resolve, reject): void => {
    void resolve
    timer = setTimeout(() => {
      controller.abort()
      reject(new Error('Semantic request timed out'))
    }, timeoutMs)
  })
  try {
    return await Promise.race([
      (async (): Promise<string> => {
        const response = await fetcher(url, { cache: 'no-cache', signal: controller.signal })
        if (!response.ok) {
          throw new Error(`Semantic request failed: ${response.status}`)
        }
        return await response.text()
      })(),
      timeout,
    ])
  } finally {
    if (timer !== null) {
      clearTimeout(timer)
    }
  }
}
