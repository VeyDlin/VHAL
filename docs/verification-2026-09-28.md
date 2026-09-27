# Documentation site verification

Local verification date: 2026-09-28. Source baseline: main `805319d`; site baseline: pages `3626b32`. This report records the local release candidate before publication.

## Coverage

The update includes parser and API rendering corrections, refreshed source/wiki documentation, Light/Dark/System themes, a sticky independently scrolling navigation tree, Utilities-first search ordering, exact tab/anchor navigation with temporary highlights, and browser-local hybrid semantic search.

| Check | Result |
| --- | --- |
| Generator unit tests | 12 passed |
| Client unit/DOM tests | 45 passed |
| TypeScript check | Passed |
| Playwright browser suite, Edge | 17 passed |
| Production Vite build | Passed |
| npm audit, all dependencies | 0 reported vulnerabilities |
| Real Node embedding pipeline after dependency updates | Normalized 384-dimensional output |
| Real production browser worker at `/VHAL/` | Model loaded, semantic results and exact API scope verified |

Browser coverage includes typo/prefix navigation, repeat selection, documentation/API switching, protected members, API-only pages, repeated nested API tokens, delayed syntax assets, click/Escape dismissal, transient hashing failure and retry, lexical fallback, sticky navigation, theme persistence/system changes, denied storage and mobile table scrolling. A separate forced-cold dependency-optimizer run retained the first query and reached fallback successfully.

The real-model smoke used `smooth movement over time`, returned Animation plus related Animation.h/Easing.h pages, checked the selected API source key against its rendered scope, then reloaded in the same browser context and confirmed versioned index/model caches. No page errors were observed. Earlier production checks also exercised an aborted manifest followed by a successful Retry.

## Artifacts and performance observations

- 302 indexed documents; 1,375 chunks; 674 non-heading API chunks, all member-scoped.
- Artifact: 5,925,979 bytes, version `299737a1ddb76a995db5b254c6948c459fb62e04784dc89adbb838da3748ee86`.
- Model: 22,972,370 bytes. WASM: 21,596,019 bytes uncompressed; the observed CDN transfer body was approximately 4.11 MB. The initial index benchmark transferred approximately 1.96 MB compressed.
- Observed first model initialization ranged from 7 to 13 seconds and same-context reload from 3 to 7 seconds on this workstation, including runs during other tests. These are observations, not latency guarantees. Keyword results remain available while loading.
- Instrumented click-to-first-highlight paint improved from approximately 1,155/567/71 ms to 732/188/67 ms for a cold documentation visit, first API visit and repeated documentation visit respectively. Cold route loading still has a cost; syntax loading no longer gates the reveal, and scrolling uses instant behavior.

## Limits and maintenance

The production build still reports large-chunk warnings. Node 22 reports its experimental type-stripping warning in tests. Local browser runs also report inherited terminal-color environment warnings; these are not test failures.

Semantic relevance is not guaranteed: low-confidence queries fall back to lexical results. First use requires substantial static downloads; cache persistence depends on browser policy. There is no whole-site offline service worker or persistent shareable search-highlight URL. See [SEARCH.md](../client/SEARCH.md) and [TESTING.md](../client/TESTING.md).

The firmware itself was not changed or target-board tested. The source documentation audit records the existing Console label caveat and hardware-example limitations.

## Release target

Publish the reviewed site to `pages` before the matching source/wiki/workflow update to `main`. The active Deploy Docs workflow then runs generator tests, model generation, client tests, typechecking, build and Chromium browser tests before publishing [VHAL documentation](https://veydlin.github.io/VHAL/). Verify the workflow conclusion and the deployed manifest after publication; local verification alone is not deployment evidence.
