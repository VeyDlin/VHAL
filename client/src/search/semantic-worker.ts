import { env, pipeline, type FeatureExtractionPipelineType } from '@huggingface/transformers'
import { loadSemanticArtifact } from './semantic-cache.ts'
import { SEMANTIC_MODEL, type SemanticArtifact } from './semantic-contract.ts'
import type { SemanticHit, SemanticSession } from './semantic-client.ts'


interface InitMessage extends SemanticSession {
  type: 'init'
}

interface QueryMessage {
  type: 'query'
  id: number
  text: string
}

type WorkerRequest = InitMessage | QueryMessage

let artifact: SemanticArtifact | null = null
let extractor: FeatureExtractionPipelineType | null = null
const loadFeatureExtractor = pipeline as unknown as (
  task: 'feature-extraction',
  model: string,
  options: {
    revision: string
    dtype: 'q8'
    device: 'wasm'
    progress_callback: (progress: { status: string; progress?: number }) => void
  },
) => Promise<FeatureExtractionPipelineType>


self.onmessage = (event: MessageEvent<WorkerRequest>): void => {
  const message = event.data
  if (message.type === 'init') {
    void initialize(message)
  } else if (message.type === 'query') {
    void search(message)
  }
}


async function initialize(message: InitMessage): Promise<void> {
  try {
    self.postMessage({ type: 'progress', phase: 'Checking search data…', percent: null })
    const loaded = await loadSemanticArtifact(message, { baseUrl: message.baseUrl })
    artifact = loaded.artifact
    self.postMessage({ type: 'progress', phase: 'Loading language model…', percent: null })
    env.allowLocalModels = false
    const wasm = env.backends.onnx.wasm
    if (!wasm) {
      throw new Error('WASM backend is unavailable')
    }
    wasm.numThreads = 1
    wasm.proxy = false
    env.useBrowserCache = false
    try {
      if (typeof caches !== 'undefined') {
        await caches.open('transformers-cache')
        env.useBrowserCache = true
      }
    } catch {
      env.useBrowserCache = false
    }
    extractor = await loadFeatureExtractor('feature-extraction', SEMANTIC_MODEL.id, {
      revision: SEMANTIC_MODEL.revision,
      dtype: SEMANTIC_MODEL.dtype,
      device: 'wasm',
      progress_callback: (progress: { status: string; progress?: number }): void => {
        if (progress.status === 'progress') {
          self.postMessage({ type: 'progress', phase: 'Downloading language model…', percent: progress.progress ?? null })
        } else if (progress.status === 'initiate') {
          self.postMessage({ type: 'progress', phase: 'Loading language model…', percent: null })
        }
      },
    })
    self.postMessage({ type: 'ready' })
  } catch (error: unknown) {
    self.postMessage({ type: 'error', message: error instanceof Error ? error.message : 'Semantic search failed' })
  }
}


async function search(message: QueryMessage): Promise<void> {
  if (!artifact || !extractor) {
    self.postMessage({ type: 'error', message: 'Semantic search is not ready' })
    return
  }
  try {
    const output = await extractor(message.text, { pooling: 'mean', normalize: true })
    const vector = Array.from(output.data)
    if (vector.length !== SEMANTIC_MODEL.dimensions) {
      throw new Error('Unexpected model output dimensions')
    }
    const pages = new Map<string, SemanticHit>()
    for (const chunk of artifact.chunks) {
      let score = 0
      for (let index = 0; index < vector.length; index += 1) {
        score += vector[index]! * chunk.vector[index]!
      }
      if (score < 0.35) {
        continue
      }
      const previous = pages.get(chunk.path)
      if (!previous || score > previous.score) {
        pages.set(chunk.path, {
          id: chunk.id,
          path: chunk.path,
          tab: chunk.tab,
          text: chunk.text,
          anchor: chunk.anchor,
          headingOnly: chunk.headingOnly,
          ...(chunk.sourceKey ? { sourceKey: chunk.sourceKey } : {}),
          score,
        })
      }
    }
    const hits = [...pages.values()].sort((left, right) => right.score - left.score).slice(0, 20)
    self.postMessage({ type: 'results', id: message.id, hits })
  } catch (error: unknown) {
    self.postMessage({ type: 'error', message: error instanceof Error ? error.message : 'Semantic query failed' })
  }
}
