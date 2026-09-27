import {
  SEMANTIC_CONFIG,
  SEMANTIC_MODEL,
  type SemanticArtifact,
  type SemanticChunk,
  type SemanticManifest,
  type SemanticSourceDocument,
  type SemanticWordOrigin,
} from './semantic-contract.ts'


export function createSemanticChunks(documents: SemanticSourceDocument[]): SemanticChunk[] {
  const chunks: SemanticChunk[] = []

  for (const document of documents) {
    const docsText = document.readme.trim()
    const docsParts = docsText.length > 0 ? splitDocumentation(docsText) : []

    for (const [index, text] of docsParts.entries()) {
      chunks.push({
        id: `${document.id}:docs:${index}`,
        path: document.path,
        tab: 'docs',
        text,
        anchor: documentationAnchor(text),
        headingOnly: false,
      })
    }

    const words = document.symbols.match(/\S+/g) ?? []

    for (let start = 0, index = 0; start < words.length; start += SEMANTIC_CONFIG.apiMaxWords - SEMANTIC_CONFIG.apiOverlapWords, index += 1) {
      const part = words.slice(start, start + SEMANTIC_CONFIG.apiMaxWords)
      const sourceKey = sourceKeyAt(document.symbolOrigins ?? [], start)
      chunks.push({
        id: `${document.id}:api:${index}`,
        path: document.path,
        tab: 'api',
        text: part.join(' '),
        anchor: part[0] ?? '',
        headingOnly: false,
        ...(sourceKey ? { sourceKey } : {}),
      })
    }

    if (docsParts.length === 0 && words.length === 0) {
      const tab = document.hasReadme ? 'docs' : 'api'
      chunks.push({
        id: `${document.id}:${tab}:0`,
        path: document.path,
        tab,
        text: document.title.trim() || document.path,
        anchor: '',
        headingOnly: true,
      })
    }
  }

  return chunks
}


export async function computeCorpusHash(corpus: unknown): Promise<string> {
  return sha256(JSON.stringify(corpus))
}


export async function computeSemanticVersion(corpusHash: string): Promise<string> {
  if (!isHash(corpusHash)) {
    throw new Error('Invalid corpus hash')
  }

  return sha256(corpusHash + JSON.stringify(SEMANTIC_CONFIG))
}


export function validateSemanticManifest(value: unknown): value is SemanticManifest {
  if (!isRecord(value) || !isRecord(value.model)) {
    return false
  }

  const model = value.model
  return value.schemaVersion === 1
    && isHash(value.version)
    && isHash(value.corpusHash)
    && isHash(value.artifactHash)
    && value.artifact === `embeddings-${value.version}.json`
    && Number.isSafeInteger(value.chunkCount)
    && (value.chunkCount as number) > 0
    && model.id === SEMANTIC_MODEL.id
    && model.revision === SEMANTIC_MODEL.revision
    && model.dtype === SEMANTIC_MODEL.dtype
    && model.dimensions === SEMANTIC_MODEL.dimensions
}


export function validateSemanticArtifact(value: unknown, manifest: SemanticManifest): value is SemanticArtifact {
  if (!isRecord(value) || value.version !== manifest.version || !Array.isArray(value.chunks)) {
    return false
  }

  if (value.chunks.length !== manifest.chunkCount) {
    return false
  }

  return value.chunks.every((chunk: unknown) => {
    if (!isRecord(chunk) || !Array.isArray(chunk.vector) || chunk.vector.length !== SEMANTIC_MODEL.dimensions) {
      return false
    }

    if (typeof chunk.id !== 'string' || chunk.id.length === 0
      || typeof chunk.path !== 'string' || chunk.path.length === 0
      || (chunk.tab !== 'docs' && chunk.tab !== 'api')
      || typeof chunk.text !== 'string' || chunk.text.length === 0
      || typeof chunk.anchor !== 'string' || typeof chunk.headingOnly !== 'boolean') {
      return false
    }

    if (!chunk.headingOnly && (chunk.anchor.length === 0 || !chunk.text.startsWith(chunk.anchor))) {
      return false
    }

    if (chunk.headingOnly && chunk.anchor !== '') {
      return false
    }

    if (chunk.tab === 'api' && !chunk.headingOnly
      && (typeof chunk.sourceKey !== 'string' || !/^f\d+\/s\d+(?:\/(?:public|protected|private)\/\d+)*$/.test(chunk.sourceKey))) {
      return false
    }

    const vector: number[] = chunk.vector
    if (!vector.every((component: unknown) => typeof component === 'number' && Number.isFinite(component))) {
      return false
    }

    const norm = Math.hypot(...vector)
    return norm > 0 && Math.abs(norm - 1) < 0.001
  })
}


function sourceKeyAt(origins: SemanticWordOrigin[], wordIndex: number): string | undefined {
  let key: string | undefined
  for (const origin of origins) {
    if (origin.start > wordIndex) {
      break
    }
    key = origin.key
  }
  return key
}


export async function sha256(text: string): Promise<string> {
  const bytes = new TextEncoder().encode(text)
  const digest = await crypto.subtle.digest('SHA-256', bytes)
  return Array.from(new Uint8Array(digest), (byte) => byte.toString(16).padStart(2, '0')).join('')
}


function splitDocumentation(text: string): string[] {
  const parts: string[] = []
  let start = 0

  while (start < text.length) {
    let end = Math.min(start + SEMANTIC_CONFIG.docsMaxChars, text.length)
    if (end < text.length) {
      const boundary = text.lastIndexOf(' ', end)
      if (boundary > start) {
        end = boundary
      }
    }

    const part = text.slice(start, end).trim()
    if (part.length > 0) {
      parts.push(part)
    }

    if (end === text.length) {
      break
    }

    let nextStart = Math.max(start + 1, end - SEMANTIC_CONFIG.docsOverlapChars)
    if (text[nextStart - 1] !== ' ' && text[nextStart] !== ' ') {
      const boundary = text.indexOf(' ', nextStart)
      if (boundary >= 0 && boundary < end) {
        nextStart = boundary + 1
      }
    }

    start = nextStart
  }

  return parts
}


function documentationAnchor(text: string): string {
  if (text.length <= SEMANTIC_CONFIG.anchorMaxChars) {
    return text
  }

  const boundary = text.lastIndexOf(' ', SEMANTIC_CONFIG.anchorMaxChars)
  return boundary > 0 ? text.slice(0, boundary) : text.slice(0, SEMANTIC_CONFIG.anchorMaxChars)
}


function isHash(value: unknown): value is string {
  return typeof value === 'string' && /^[a-f0-9]{64}$/.test(value)
}


function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value)
}
