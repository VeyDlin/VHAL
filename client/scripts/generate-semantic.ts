import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'

import {
  createSemanticChunks,
  computeCorpusHash,
  computeSemanticVersion,
  validateSemanticArtifact,
  validateSemanticManifest,
} from '../src/search/semantic-artifacts.ts'
import {
  SEMANTIC_CONFIG,
  SEMANTIC_MODEL,
  type SemanticArtifact,
  type SemanticManifest,
  type SemanticSourceDocument,
} from '../src/search/semantic-contract.ts'


async function main(): Promise<void> {
  const started = performance.now()
  const sourcePath = resolve('src/generated/search-index.json')
  const outputDirectory = resolve('public/semantic')
  const manifestPath = resolve(outputDirectory, 'manifest.json')
  const source = JSON.parse(await readFile(sourcePath, 'utf8')) as unknown

  if (!isSourceDocuments(source)) {
    throw new Error(`Invalid search index: ${sourcePath}`)
  }

  const corpusHash = await computeCorpusHash(source)
  const version = await computeSemanticVersion(corpusHash)
  const chunks = createSemanticChunks(source)
  const artifactName = `embeddings-${version}.json`
  const artifactPath = resolve(outputDirectory, artifactName)

  if (await hasVerifiedArtifact(manifestPath, artifactPath, corpusHash, version, chunks.length)) {
    console.log(`Reused ${chunks.length} verified semantic chunks in ${Math.round(performance.now() - started)} ms`)
    return
  }

  console.log(`Embedding ${chunks.length} semantic chunks with ${SEMANTIC_MODEL.id}@${SEMANTIC_MODEL.revision}`)
  const { env, pipeline } = await import('@huggingface/transformers')
  env.cacheDir = resolve('.cache/transformers')
  const extractor = await pipeline('feature-extraction', SEMANTIC_MODEL.id, {
    revision: SEMANTIC_MODEL.revision,
    dtype: SEMANTIC_MODEL.dtype,
  })
  const documentsByPath = new Map<string, SemanticSourceDocument>(source.map((document) => [document.path, document]))
  const embeddedChunks: SemanticArtifact['chunks'] = []
  const batchSize = 16

  for (let start = 0; start < chunks.length; start += batchSize) {
    const batch = chunks.slice(start, start + batchSize)
    const inputs = batch.map((chunk) => {
      const document = documentsByPath.get(chunk.path)
      if (!document) {
        throw new Error(`Missing source document for ${chunk.path}`)
      }

      return `${document.title}\n${document.breadcrumb}\n${chunk.text}`
    })
    const output = await extractor(inputs, {
      pooling: SEMANTIC_CONFIG.pooling,
      normalize: SEMANTIC_CONFIG.normalize,
    })
    const vectors = output.tolist() as number[][]

    if (vectors.length !== batch.length) {
      throw new Error(`Embedding batch returned ${vectors.length} vectors for ${batch.length} chunks`)
    }

    for (const [index, chunk] of batch.entries()) {
      const vector = vectors[index]
      if (!vector || vector.length !== SEMANTIC_MODEL.dimensions) {
        throw new Error(`Invalid embedding dimensions for ${chunk.id}`)
      }

      embeddedChunks.push({ ...chunk, vector: vector.map((value) => Number(value.toFixed(6))) })
    }

    console.log(`Embedded ${Math.min(start + batch.length, chunks.length)}/${chunks.length}`)
  }

  const artifact: SemanticArtifact = { version, chunks: embeddedChunks }
  const artifactText = JSON.stringify(artifact)
  const manifest: SemanticManifest = {
    schemaVersion: 1,
    version,
    corpusHash,
    model: SEMANTIC_MODEL,
    artifact: artifactName,
    artifactHash: sha256(artifactText),
    chunkCount: chunks.length,
  }

  if (!validateSemanticArtifact(artifact, manifest)) {
    throw new Error('Generated semantic artifact failed validation')
  }

  await mkdir(outputDirectory, { recursive: true })
  await writeFile(artifactPath, artifactText)
  await writeFile(manifestPath, JSON.stringify(manifest))
  console.log(`Generated ${chunks.length} chunks, ${Buffer.byteLength(artifactText)} bytes in ${Math.round(performance.now() - started)} ms`)
}


async function hasVerifiedArtifact(
  manifestPath: string,
  artifactPath: string,
  corpusHash: string,
  version: string,
  expectedCount: number,
): Promise<boolean> {
  try {
    const manifestText = await readFile(manifestPath, 'utf8')
    const manifest = JSON.parse(manifestText) as unknown
    if (!validateSemanticManifest(manifest)
      || manifest.corpusHash !== corpusHash
      || manifest.version !== version
      || manifest.chunkCount !== expectedCount) {
      return false
    }

    const artifactText = await readFile(artifactPath, 'utf8')
    if (sha256(artifactText) !== manifest.artifactHash) {
      return false
    }

    return validateSemanticArtifact(JSON.parse(artifactText) as unknown, manifest)
  } catch (error: unknown) {
    if (error instanceof Error && 'code' in error && error.code === 'ENOENT') {
      return false
    }

    return false
  }
}


function sha256(text: string): string {
  return createHash('sha256').update(text).digest('hex')
}


function isSourceDocuments(value: unknown): value is SemanticSourceDocument[] {
  return Array.isArray(value) && value.every((item: unknown) => {
    if (typeof item !== 'object' || item === null) {
      return false
    }

    const document = item as Record<string, unknown>
    return typeof document.id === 'string'
      && typeof document.title === 'string'
      && typeof document.path === 'string'
      && typeof document.breadcrumb === 'string'
      && typeof document.readme === 'string'
      && typeof document.symbols === 'string'
      && typeof document.hasReadme === 'boolean'
  })
}


main().catch((error: unknown) => {
  console.error(error)
  process.exitCode = 1
})
