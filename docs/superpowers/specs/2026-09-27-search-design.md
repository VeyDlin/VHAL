# Documentation search and appearance

## Intent and authorization

Finish the previously requested English documentation site improvements autonomously. The user explicitly requested no further design check-ins and inexpensive Luna workers. Preserve earlier correctness fixes and updated documentation. Work in the existing docs worktree. Publication requires verified remote configuration and an unambiguous safe target; never overwrite unrelated changes.

## Design

Keep MiniSearch as the immediate, offline-capable baseline. Add lazy semantic enhancement in a module worker using Transformers.js 3.8.1 and `Xenova/all-MiniLM-L6-v2`, revision `751bff37182d3f1213fa05d7196b954e230abad9`, q8, mean pooling, normalized 384-dimensional vectors. Use identical model options in the build and browser. The model is roughly 23 MB; the user relaxed the original 20 MB budget. Queries stay in the browser. Model downloads use Hugging Face; documentation embeddings are hosted with the site.

Generate bounded overlapping chunks of documentation text and API symbols, preserving path, tab, exact source text and a reveal anchor. Hash the corpus and configuration for a deterministic release version. Emit a small manifest and content-addressed embeddings JSON under `client/public/semantic`. GitHub Actions generates embeddings after documentation and before Vite. Cache the downloaded model in CI and reuse version-matching artifacts locally. Generated assets are not committed.

API anchors must also retain their structural member scope. Flattened token windows can begin with repeated qualifiers such as `constexpr`; token-origin spans identify the correct rendered member without generating an embedding for every member. A scoped match must never fall back to an unrelated occurrence elsewhere on the page. Changes to this mapping invalidate the artifact version.

On first nonempty search, load manifest, validate it against the bundled corpus, fetch and validate embeddings, then initialize the query model. Cache versioned embeddings with Cache Storage when available; validate cached data and delete only this feature's superseded entries after a valid replacement. Model caching uses Transformers.js. Disabled storage, fetch failure, unsupported WebAssembly, model failure, stale assets or a worker timeout keep lexical search working. Offer retry. Show honest phase/progress status above the sidebar search field, with a mobile equivalent.

Debounce semantic queries and ignore stale responses. Only the latest pending query should run after an in-flight inference. Merge deduplicated lexical and semantic results with exact identifier/title matches protected; otherwise rank Utilities ahead of Periphery. Semantic-only hits are labeled `Related meaning`, show actual source snippets, and navigate to their real source chunk, not fictitious query-word matches. Existing tab selection and temporary highlight remain valid.

Search navigation scrolls instantly to the match, independent of reduced-motion preferences. Themes are Light, Dark and System, default System, persisted defensively. Apply the theme before first paint. Use accessible neutral slate surfaces and blue accents; Shiki code must match both modes. Theme controls are available on desktop and mobile. Preserve sticky sidebar and independent tree scrolling.

Plain text must be sufficient for the initial reveal. Syntax-color loading and processing must not gate tab selection, scrolling or the temporary match highlight; subsequent syntax markup replacement preserves the highlight unless the user dismissed it.

## Acceptance

- Tests cover instant reveal, Utilities preference and exact-match protection.
- Theme tests cover persistence, system changes, unavailable storage, and desktop/mobile controls.
- Artifact tests cover deterministic versions, changed content/config, all chunks retaining revealable anchors, and bounded chunk sizes.
- Runtime tests cover cache hit/update/corruption/unavailable storage, fallback, stale queries, retry and semantic-only navigation.
- Real quantized inference is exercised locally, not only mocked. Browser smoke checks cover both themes and mobile; full tests, typecheck and build pass.
- Document commands, payload sizes, privacy, caching, deployment and limitations.

## Alternatives considered

Lexical-only search cannot match natural-language paraphrases. A hosted search API adds a service and sends queries externally. Browser-only document embedding wastes first-load compute. Build-time document embeddings plus local query inference satisfy the requested static hosting and privacy model.

## References

- https://huggingface.co/docs/transformers.js/v3.8.1/en/pipelines
- https://huggingface.co/Xenova/all-MiniLM-L6-v2
