# Documentation site checks

Run from `client/` after generating documentation:

```sh
npm run generate
npm test
npm run typecheck
npx playwright install chromium
npm run test:e2e
```

The browser suite starts a separate local Vite server on port 5174. It checks documentation/API tab selection, instant scrolling to highlighted matches, repeat navigation, click/Escape dismissal, collapsed API groups, API-only pages, sticky navigation, theme persistence/system changes, blocked storage and mobile table scrolling. It retains a trace on failure and a screenshot of the documentation match.

To use an installed Edge browser on Windows instead of downloading Chromium:

```powershell
$env:PLAYWRIGHT_CHANNEL = 'msedge'
npm run test:e2e
```

DOM tests cover inline syntax spans, snippet context, hidden controls, asynchronous content replacement, cleanup, and line breaks. The tests use development-only dependencies; no browser-test or DOM-emulation runtime is shipped in the website bundle.

Run documentation/semantic generation **before**, not concurrently with, browser tests: rewriting generated files can trigger a Vite reload in the middle of a test. The normal browser suite uses deterministic semantic loading fixtures/failures; a mocked worker is not evidence that real inference works.

To validate real semantic assets separately:

```sh
npm run generate:semantic
npm test
npm run typecheck
npx vite build
node node_modules/vite/bin/vite.js preview --host 127.0.0.1 --port 5175 --strictPort
```

Open `/VHAL/` on that preview server. Check first-load progress, a natural-language query such as `smooth movement over time`, exact code queries, correct result tab/anchor, and a repeated load in the same browser context. Block semantic asset/model requests to verify the keyword fallback and retry. Exercise both themes at desktop and mobile widths. Cache invalidation and stale-response cases also have focused unit tests.

The direct Vite command also avoids npm argument-forwarding differences in PowerShell. GitHub Actions runs generator tests, semantic generation, client tests, typechecking, the production build and Chromium browser tests before publishing.

See [SEARCH.md](SEARCH.md) for generation, model, cache, privacy, payload and deployment details. Persistent search-highlight links are not implemented.
