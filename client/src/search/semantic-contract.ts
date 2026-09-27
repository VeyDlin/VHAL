export const SEMANTIC_MODEL = {
  id: 'Xenova/all-MiniLM-L6-v2',
  revision: '751bff37182d3f1213fa05d7196b954e230abad9',
  dtype: 'q8',
  dimensions: 384,
} as const

export const SEMANTIC_CONFIG = {
  schemaVersion: 1,
  chunkingVersion: 2,
  docsMaxChars: 600,
  docsOverlapChars: 100,
  apiMaxWords: 60,
  apiOverlapWords: 10,
  anchorMaxChars: 80,
  pooling: 'mean',
  normalize: true,
  transformersVersion: '3.8.1',
  model: SEMANTIC_MODEL,
} as const

export interface SemanticChunk {
  id: string
  path: string
  tab: 'docs' | 'api'
  text: string
  anchor: string
  headingOnly: boolean
  sourceKey?: string
}

export interface SemanticWordOrigin {
  start: number
  key: string
}

export interface SemanticArtifact {
  version: string
  chunks: (SemanticChunk & { vector: number[] })[]
}

export interface SemanticManifest {
  schemaVersion: 1
  version: string
  corpusHash: string
  model: typeof SEMANTIC_MODEL
  artifact: string
  artifactHash: string
  chunkCount: number
}

export interface SemanticSourceDocument {
  id: string
  title: string
  path: string
  breadcrumb: string
  readme: string
  symbols: string
  symbolOrigins?: SemanticWordOrigin[]
  hasReadme: boolean
}
