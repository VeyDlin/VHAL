import { createHighlighter, type Highlighter } from 'shiki'

let highlighterPromise: Promise<Highlighter> | null = null

function getHighlighter(): Promise<Highlighter> {
  if (!highlighterPromise) {
    highlighterPromise = createHighlighter({
      themes: ['github-dark'],
      langs: ['cpp', 'c', 'python', 'bash', 'json', 'yaml', 'cmake', 'makefile', 'markdown'],
    })
  }
  return highlighterPromise
}

/**
 * Process an HTML string: find all <pre><code class="language-xxx">...</code></pre>
 * blocks and replace them with Shiki-highlighted versions.
 * Returns the processed HTML string.
 */
export async function highlightHtmlString(html: string): Promise<string> {
  if (!html) return html

  const highlighter = await getHighlighter()

  // Match <pre><code class="language-xxx">...code...</code></pre>
  const pattern = /<pre><code class="language-(\w+)">([\s\S]*?)<\/code><\/pre>/g

  const matches: { full: string; lang: string; code: string }[] = []
  let match: RegExpExecArray | null
  while ((match = pattern.exec(html)) !== null) {
    matches.push({ full: match[0], lang: match[1], code: match[2] })
  }

  if (matches.length === 0) return html

  let result = html
  for (const m of matches) {
    // Decode HTML entities back to plain text for Shiki
    const decoded = decodeHtmlEntities(m.code)

    const loadedLangs = highlighter.getLoadedLanguages()
    if (!loadedLangs.includes(m.lang as any)) {
      try {
        await highlighter.loadLanguage(m.lang as any)
      } catch {
        continue
      }
    }

    const highlighted = highlighter.codeToHtml(decoded, { lang: m.lang, theme: 'github-dark' })

    // Strip inline styles from the shiki <pre> and add our class
    const processed = highlighted
      .replace(/^<pre[^>]*style="[^"]*"/, (tag) => tag.replace(/style="[^"]*"/, ''))
      .replace(/^<pre/, '<pre class="shiki-highlighted"')

    result = result.replace(m.full, processed)
  }

  return result
}

function decodeHtmlEntities(str: string): string {
  return str
    .replace(/&amp;/g, '&')
    .replace(/&lt;/g, '<')
    .replace(/&gt;/g, '>')
    .replace(/&quot;/g, '"')
    .replace(/&#39;/g, "'")
    .replace(/&#x27;/g, "'")
    .replace(/&#x2F;/g, '/')
}
