# Documentation search

## Behavior

MiniSearch provides immediate keyword, prefix and typo-tolerant results. On the first nonempty query, a module worker loads the semantic index and an English sentence-embedding model. The input and keyword results remain usable while loading, after errors, and when semantic inference is unsupported.

Semantic results supplement keyword results using reciprocal rank fusion. Exact filenames and exact indexed symbol queries are protected; Utilities paths are prioritized over non-exact Periphery matches. Common English stopwords are removed only from multiword keyword queries. Single-token code queries are unchanged.

Semantic-only results are labeled **Related meaning** and display source text. Selecting a result opens its documentation/API tab, scrolls instantly to the source match and highlights it until the next click or Escape. A semantic match highlights a real source anchor, not query words that might be absent from the document. Persistent shareable search highlights are not implemented.

The sidebar reports initialization/download progress above the search field. Loading failures fall back to keyword search, with a retry action. Superseded asynchronous responses cannot overwrite results for a newer query.

## Generation

The model is [`Xenova/all-MiniLM-L6-v2`](https://huggingface.co/Xenova/all-MiniLM-L6-v2), pinned to revision `751bff37182d3f1213fa05d7196b954e230abad9`, with Transformers.js `3.8.1`, q8 weights, mean pooling and normalized 384-dimensional vectors. Build-time document inference and browser query inference use the same settings. See the [pipeline documentation](https://huggingface.co/docs/transformers.js/v3.8.1/en/pipelines).

After generating `src/generated/search-index.json`, run from `client/`:

```sh
npm ci
npm run generate:semantic
npm run typecheck
npx vite build
```

On this Windows workspace, `npm run build` also runs the Python documentation generator and semantic generator. Its documentation command expects the existing `generator/.venv/Scripts/python` and sibling `VHAL` source checkout. The GitHub workflow uses an explicit platform-independent Python command instead.

Output lives in `public/semantic/`:

- `manifest.json`: schema, version, corpus hash, pinned model, artifact filename/hash and chunk count.
- `embeddings-<version>.json`: source chunks and normalized vectors.

Documentation chunks are at most 600 characters with approximately 100 characters of overlap, adjusted at word boundaries. API chunks contain at most 60 whitespace-delimited tokens with 10 tokens of overlap. Each chunk retains its source path, tab and reveal anchor. API token-origin spans additionally preserve a structural member key, and the rendered API exposes matching scopes: repeated qualifiers are highlighted within the correct member, never at an unrelated first occurrence. Empty pages receive a heading fallback; API-only pages do not receive fictitious documentation chunks.

The version hashes the parsed corpus and explicit embedding/chunking configuration. Bump the chunking/configuration version when changing its algorithm. The generator validates version, byte integrity, count and vectors before reusing existing output. Generation errors fail the build; they do not silently publish an incomplete semantic index.

Generated artifacts and `.cache/transformers` are ignored by Git. GitHub Actions caches model files, generates document embeddings after data generation and dependency installation, and includes them in the Pages build artifact. No paid inference service or secret API key is required.

The package overrides Transformers.js's Node-only image dependency to `sharp` 0.35.5 to address its bundled image-codec advisories without changing the pinned text-model runtime. This site uses text feature extraction, not image pipelines. Keep the override covered by a real Node embedding smoke test when updating dependencies; a reused artifact alone does not exercise the pipeline.

The build also overrides `esbuild` to 0.28.2 because the transitive font tooling otherwise retains a version affected by the [Windows development-server advisory](https://github.com/advisories/GHSA-g7r4-m6w7-qqqr). Verify the production build and browser suite when updating this override.

## Browser cache and privacy

The browser revalidates the small manifest and checks that its corpus/configuration match the bundled site. Embeddings use a feature-owned Cache Storage namespace and content-addressed artifact URL. Cached data is validated before use; corrupt data is fetched again. A valid replacement permits cleanup of this feature's old index entries only. Unavailable or full storage does not prevent searching in memory.

Transformers.js separately caches the revision-pinned model when browser storage permits. Model files are fetched from Hugging Face; the pinned WASM runtime is fetched from jsDelivr. Search queries are embedded and compared locally and are not sent to an inference API or persisted as search history by this feature. Static file hosts still receive ordinary asset requests and connection metadata.

This is best-effort resource caching, not a service worker or a promise that the entire site works offline. If assets or inference cannot be loaded, keyword search remains available on the loaded site. During a deployment mismatch, semantic search falls back rather than mixing old vectors with new pages.

## Measured baseline

For the current 302-document corpus, real generation produced 1,375 chunks and a 5,925,979-byte JSON artifact, including member scopes for all 674 non-heading API chunks. The quantized ONNX file is 22,972,370 bytes. The initial native-generation benchmark took about 128 seconds on this workstation; verified reuse took about 450 milliseconds.

The cold browser load additionally needs the tokenizer, worker/library code and WASM runtime. The pinned runtime file is about 21.6 MB **uncompressed**; actual network transfer depends on compression and caches. The total cold-load resource budget therefore exceeds the original tentative 20 MB allowance, which was explicitly relaxed.

Semantic search is an enhancement, not a guarantee of relevance. The confidence cutoff is 0.35 and at most 20 distinct semantic pages are merged. For example, real inference ranks Animation first for `smooth movement over time`; broad or ambiguous paraphrases can remain below the cutoff and rely on keyword results.

## Release ordering

The repository stores the docs application on `pages` and source/wiki/workflow changes on `main`. Publish the reviewed application changes to `pages` first, then the corresponding source/wiki/workflow update to `main`, which triggers the Pages workflow. Never merge the unrelated branch layouts into each other or force-push them. Verify the resulting workflow and live `/VHAL/` site after deployment.
