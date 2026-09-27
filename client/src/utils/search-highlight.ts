import { findTextMatch, type TextMatch } from './search-match.ts'
import type { SearchTarget } from './search-target.ts'


interface TextSegment {
  node: Text
  start: number
  end: number
}


export interface SearchHighlight {
  dispose: () => void
}


const excludedElements: string = 'script, style, noscript, button, [aria-hidden="true"], [hidden], .anchor-link'
const blockElements: string = 'div, p, pre, li, td, th, h1, h2, h3, h4, h5, h6, section, article, tr'


function collectText(root: HTMLElement): { text: string; segments: TextSegment[] } {
  // SHOW_ELEMENT | SHOW_TEXT; inspect explicit line breaks as well as text nodes.
  const walker: TreeWalker = root.ownerDocument.createTreeWalker(root, 5)
  const segments: TextSegment[] = []
  let text: string = ''
  let previousBlock: Element | null = null
  let current: Node | null = walker.nextNode()

  while (current) {
    if (current.nodeType === 1) {
      const element: Element = current as Element
      if (element.localName === 'br' && !element.closest(excludedElements)) {
        text += ' '
      }
      current = walker.nextNode()
      continue
    }
    const node: Text = current as Text
    const parent: HTMLElement | null = node.parentElement
    if (parent && !parent.closest(excludedElements)) {
      const block: Element | null = parent.closest(blockElements)
      if (text.length > 0 && block !== previousBlock) {
        text += ' '
      }
      const start: number = text.length
      text += node.data
      segments.push({ node, start, end: text.length })
      previousBlock = block
    }
    current = walker.nextNode()
  }

  return { text, segments }
}


function markRange(root: HTMLElement, match: TextMatch, segments: TextSegment[]): HTMLElement[] {
  const marks: HTMLElement[] = []
  for (const segment of segments) {
    const start: number = Math.max(0, match.start - segment.start)
    const end: number = Math.min(segment.node.length, match.end - segment.start)
    if (start >= end || segment.end <= match.start) {
      continue
    }

    const node: Text = segment.node
    if (end < node.length) {
      node.splitText(end)
    }
    const selected: Text = start > 0 ? node.splitText(start) : node
    const mark: HTMLElement = root.ownerDocument.createElement('mark')
    mark.dataset.searchHit = ''
    selected.replaceWith(mark)
    mark.append(selected)
    marks.push(mark)
  }
  return marks
}


export function activateSearchHighlight(
  root: HTMLElement,
  target: SearchTarget,
  onDismiss: () => void = () => {},
): SearchHighlight {
  const document: Document = root.ownerDocument
  const window: Window | null = document.defaultView
  let marks: HTMLElement[] = []
  let disposed: boolean = false
  let didScroll: boolean = false
  const observer: MutationObserver | null = window
    ? new (window as Window & typeof globalThis).MutationObserver(() => refresh())
    : null

  function removeMarks(): void {
    for (const mark of marks) {
      mark.replaceWith(...Array.from(mark.childNodes))
    }
    marks = []
  }


  function dispose(): void {
    if (disposed) {
      return
    }
    disposed = true
    observer?.disconnect()
    document.removeEventListener('click', dismiss, true)
    document.removeEventListener('keydown', onKeydown, true)
    removeMarks()
  }


  function dismiss(): void {
    dispose()
    onDismiss()
  }


  function onKeydown(event: KeyboardEvent): void {
    if (event.key === 'Escape') {
      dismiss()
    }
  }


  function refresh(): void {
    if (disposed) {
      return
    }
    observer?.disconnect()
    removeMarks()
    let container: HTMLElement = root
    if (target.sourceKey) {
      const source = Array.from(root.querySelectorAll<HTMLElement>('[data-search-scope]'))
        .find((element: HTMLElement) => element.dataset.searchScope === target.sourceKey)
      if (!source) {
        observer?.observe(root, { subtree: true, childList: true, characterData: true })
        return
      }
      container = source
    } else if (target.headingOnly) {
      const headings: HTMLElement[] = Array.from(root.querySelectorAll<HTMLElement>('h1, h2, h3, [data-search-heading]'))
      container = headings.find((heading: HTMLElement) => findTextMatch(heading.textContent ?? '', target.terms))
        ?? headings[0] ?? root
    }
    const { text, segments } = collectText(container)
    const match: TextMatch | null = findTextMatch(text, target.terms, target.snippet)
    if (match) {
      marks = markRange(container, match, segments)
    }
    const scrollTarget: HTMLElement | undefined = marks[0] ?? (target.headingOnly || target.sourceKey ? container : undefined)
    if (scrollTarget && !didScroll) {
      scrollTarget.scrollIntoView?.({ behavior: 'instant', block: 'center', inline: 'nearest' })
      didScroll = true
    }
    observer?.observe(root, { subtree: true, childList: true, characterData: true })
  }


  refresh()
  document.addEventListener('click', dismiss, true)
  document.addEventListener('keydown', onKeydown, true)
  return { dispose }
}
