import assert from 'node:assert/strict'
import test from 'node:test'

import {
  createSemanticChunks,
  computeCorpusHash,
  computeSemanticVersion,
  validateSemanticArtifact,
  validateSemanticManifest,
} from '../src/search/semantic-artifacts.ts'
import { SEMANTIC_MODEL } from '../src/search/semantic-contract.ts'


test('creates overlapping documentation chunks with source anchors and stable IDs', () => {
  const words: string[] = Array.from({ length: 180 }, (_, index: number) => `word${index}`)
  const chunks = createSemanticChunks([{
    id: 'guide', title: 'Guide', path: 'Guide', breadcrumb: 'Docs',
    readme: words.join(' '), symbols: '', hasReadme: true,
  }])
  const docs = chunks.filter((chunk) => chunk.tab === 'docs')

  assert.ok(docs.length > 1)
  assert.equal(docs[0]?.id, 'guide:docs:0')
  assert.ok(docs.every((chunk) => chunk.text.length <= 600))
  assert.ok(docs.every((chunk) => chunk.anchor === chunk.text.slice(0, chunk.anchor.length)))
  assert.ok(docs.every((chunk) => chunk.anchor.length <= 80))
  assert.match(docs[0]?.text ?? '', /word\d+$/)
  assert.match(docs[1]?.text ?? '', /^word\d+/)
  assert.ok(docs[0]?.text.split(' ').some((word) => docs[1]?.text.split(' ').includes(word)))

  const source: string = words.join(' ')
  let coveredEnd: number = 0
  for (const [index, chunk] of docs.entries()) {
    const start: number = source.indexOf(chunk.text)
    assert.ok(start >= 0, 'Every chunk is an unchanged source substring')
    assert.ok(start <= coveredEnd, 'Chunks must not skip source text')
    if (index > 0) {
      const overlap: number = coveredEnd - start
      assert.ok(overlap >= 90 && overlap <= 100, `Unexpected overlap: ${overlap}`)
    }
    coveredEnd = start + chunk.text.length
  }
  assert.equal(coveredEnd, source.length)
})


test('creates API chunks at sixty words with ten words of overlap', () => {
  const words: string[] = Array.from({ length: 125 }, (_, index: number) => `symbol${index}`)
  const chunks = createSemanticChunks([{
    id: 'api', title: 'API', path: 'API', breadcrumb: 'Reference',
    readme: '', symbols: words.join(' '), hasReadme: false,
  }])
  const api = chunks.filter((chunk) => chunk.tab === 'api')

  assert.equal(api.length, 3)
  assert.equal(api[0]?.text.split(' ').length, 60)
  assert.equal(api[1]?.text.split(' ').length, 60)
  assert.equal(api[1]?.anchor, 'symbol50')
  assert.equal(api[2]?.anchor, 'symbol100')
})


test('carries the origin of a repeated API token into overlapping chunks', () => {
  const words: string[] = Array.from({ length: 125 }, (_, index: number) => index === 50 ? 'constexpr' : `symbol${index}`)
  const chunks = createSemanticChunks([{
    id: 'api', title: 'API', path: 'API', breadcrumb: 'Reference',
    readme: '', symbols: words.join(' '), hasReadme: false,
    symbolOrigins: [{ start: 0, key: 'f0/s0/public/0' }, { start: 50, key: 'f0/s0/protected/0' }],
  }])

  assert.equal(chunks[1]?.anchor, 'constexpr')
  assert.equal(chunks[1]?.sourceKey, 'f0/s0/protected/0')
})


test('retains every document and handles empty text and unbroken tokens', () => {
  const chunks = createSemanticChunks([
    { id: 'empty', title: 'Empty heading', path: 'Empty', breadcrumb: '', readme: '', symbols: '', hasReadme: false },
    { id: 'long', title: 'Long', path: 'Long', breadcrumb: '', readme: 'x'.repeat(1301), symbols: '', hasReadme: true },
  ])

  assert.equal(chunks[0]?.headingOnly, true)
  assert.equal(chunks[0]?.tab, 'api')
  assert.equal(chunks[0]?.text, 'Empty heading')
  assert.ok(chunks.filter((chunk) => chunk.path === 'Long').length > 1)
  assert.ok(chunks.every((chunk) => chunk.text.length <= 600))
})


test('keeps API-only documents on the API tab', () => {
  const chunks = createSemanticChunks([{
    id: 'api-only', title: 'API Only', path: 'API', breadcrumb: '',
    readme: '', symbols: 'Symbol Description', hasReadme: false,
  }])

  assert.equal(chunks.length, 1)
  assert.equal(chunks[0]?.tab, 'api')
  assert.equal(chunks[0]?.headingOnly, false)
})


test('hashes parsed corpus deterministically and versions with model configuration', async () => {
  const corpus = [{ id: 'one', title: 'Title', path: 'Path', breadcrumb: '', readme: 'Text', symbols: '', hasReadme: true }]
  const hash = await computeCorpusHash(corpus)
  const version = await computeSemanticVersion(hash)

  assert.match(hash, /^[a-f0-9]{64}$/)
  assert.match(version, /^[a-f0-9]{64}$/)
  assert.equal(await computeCorpusHash(corpus), hash)
  assert.equal(await computeSemanticVersion(hash), version)
  assert.notEqual(await computeSemanticVersion('0'.repeat(64)), version)
})


test('rejects mismatched manifests and malformed vectors', () => {
  const version = 'a'.repeat(64)
  const manifest = {
    schemaVersion: 1, version, corpusHash: 'b'.repeat(64), model: SEMANTIC_MODEL,
    artifact: `embeddings-${version}.json`, artifactHash: 'c'.repeat(64), chunkCount: 1,
  }
  const vector = Array.from({ length: 384 }, (_, index: number) => index === 0 ? 1 : 0)
  const artifact = {
    version,
    chunks: [{ id: 'one:docs:0', path: 'One', tab: 'docs', text: 'Text', anchor: 'Text', headingOnly: false, vector }],
  }

  assert.equal(validateSemanticManifest(manifest), true)
  assert.equal(validateSemanticManifest({ ...manifest, model: { ...SEMANTIC_MODEL, revision: 'other' } }), false)
  assert.equal(validateSemanticManifest({ ...manifest, artifact: 'other.json' }), false)
  assert.equal(validateSemanticArtifact(artifact, manifest), true)
  assert.equal(validateSemanticArtifact({ ...artifact, chunks: [{ ...artifact.chunks[0], vector: [...vector.slice(0, 383), Number.NaN] }] }, manifest), false)
  assert.equal(validateSemanticArtifact({ ...artifact, chunks: [{ ...artifact.chunks[0], vector: Array(384).fill(0) }] }, manifest), false)
  assert.equal(validateSemanticArtifact({ ...artifact, version: 'd'.repeat(64) }, manifest), false)
})
