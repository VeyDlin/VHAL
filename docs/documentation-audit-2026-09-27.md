# Documentation freshness audit

Audit date: 2026-09-27. Source baseline: `main` at `805319d601f05f453de24b435befed5faf2a461c`.

## Repository history before this update

| Area | Latest committed update | Commit |
|------|-------------------------|--------|
| Library source (`main`) | 2026-08-24 | `805319d` |
| Documentation site (`origin/pages`) | 2026-03-15 | `3626b32` |
| Site generator (`origin/pages:generator`) | 2026-03-08 | `510b3e4` |
| Handwritten documentation (`.wiki`) | 2026-03-24 | `6287e3e` |

These are pre-update Git commit dates, not filesystem timestamps or deployment dates. A later deployment can regenerate API pages from newer headers without updating handwritten explanations or site code. The table records the baseline before the corrections below; release verification is recorded separately.

## Source changes checked against documentation

| Area | Source change | Previous documentation | Update |
|------|---------------|------------------------|--------|
| Version | 2026-03-24, `267f03d` | 2026-03-08; still showed 0.1.0 | Examples and version table now show 0.2.1 |
| Timer and DAC | 2026-08-24, `702149e` | Common adapter pages dated 2026-03-08 | DMA request API, port limitations, public DAC register address |
| STM32G4 DMA | 2026-04-16, `5d77a2f` | Common adapter page dated 2026-03-08 | Separate peripheral transfer width |
| STM32G0 ADC | 2026-03-25, `501a780` | Callback setup was not explained | DMA callbacks are installed by `SetDMA()` |
| SPI and NRF8001 | 2026-05-01, `af5ca5e`, `dd7556e` | Result migration and driver behavior were missing | Current Result-based SPI calls and driver guidance |
| ReliableProtocolCOBS | 2026-08-24, `3c22632` | No dedicated handwritten page | Opt-in device addressing, filtering, broadcast behavior and limitations |
| Software I2C | 2026-05-01, `af5ca5e` | No dedicated handwritten page | Current Result API and clock-stretch handling |
| Animation | 2026-04-11, `825eec9` | 2026-03-24 | Clarified time-based progress after division fix |
| RegisterMap | 2026-04-11, `b95fd71` | 2026-03-08 | Write callbacks execute outside the memory-copy critical section |
| MathUtilities | 2026-04-11, `a481012` | No dedicated handwritten page | Mixed-type SetMin/SetMax and interpolation preconditions |
| Console | 2026-03-13, `8770a25` | 2026-03-08 | All five levels, configurable labels, current default-label caveat |
| System::CriticalError | 2026-05-03, `0f88ae2` | 2026-03-08 | Console logging does not depend on an installed callback |

Additional corrections: FreeRTOS priority values, thread signal timeout, timer template arguments, helper-page links, and the ESP32-only DSI header location. I2CMutexAdapter's newer commit only changed trailing whitespace; no behavioral update was inferred from its date.

The main README now uses explicitly scoped application fragments instead of claiming a complete application. It removes a nonfunctional byte-at-a-time UART register-write example, uses public register registration, fixes include paths and timer channel names, and lists adapters actually selected by the current port headers. Board setup and protocol framing are explicitly application responsibilities.

## Validation

The companion documentation application generates 302 documentation pages plus the home page from this source tree. The generator regression suite contains 12 passing tests, including nested C++ types, standalone enums, link rewriting, visible search-text extraction and structural API token origins. All six rendered links targeting this site's documentation routes resolve to generated pages.

The accompanying site work adds browser regression coverage for search navigation, tab selection, temporary highlights, themes and the sticky sidebar. Client/build evidence and the release checklist are recorded separately in the documentation application's [verification report](https://github.com/VeyDlin/VHAL/blob/pages/docs/verification-2026-09-28.md). Target-board execution is outside this documentation audit.

## Known limitations

- In the library source, `Console::warningMark` defaults to `[ERROR]` and `errorMark` to `[WARNING]`. Documentation shows an explicit label override; this audit does not change runtime library code.
- Documentation examples are checked against current declarations and implementation. They have not been compiled or exercised on target boards.
- The old common DSI Markdown file is retained as a migration notice. The generator does not publish a file page when its matching source header no longer exists.
- This audit covers history-driven changes and inspected examples, not a guarantee that every pre-existing handwritten page or hardware implementation is correct.
- The companion site keeps MiniSearch as its immediate fallback and adds locally executed semantic search with versioned build-time embeddings. See the documentation application's `client/SEARCH.md` for its model, cache, download-size and privacy constraints.
