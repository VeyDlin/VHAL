export interface TextMatch {
  start: number
  end: number
}

interface NormalizedText {
  value: string
  starts: number[]
  ends: number[]
}

interface MatchCandidate {
  start: number
  end: number
  normalizedStart: number
  normalizedEnd: number
}


function normalizeText(value: string): NormalizedText {
  let normalized = ''
  let sourceOffset = 0
  const starts: number[] = []
  const ends: number[] = []

  for (const character of value) {
    const characterStart = sourceOffset
    sourceOffset += character.length

    if (/\s/u.test(character)) {
      if (normalized.endsWith(' ')) {
        ends[ends.length - 1] = sourceOffset
      } else {
        normalized += ' '
        starts.push(characterStart)
        ends.push(sourceOffset)
      }
      continue
    }

    const foldedCharacter = character.toLowerCase()
    normalized += foldedCharacter
    for (let index = 0; index < foldedCharacter.length; index += 1) {
      starts.push(characterStart)
      ends.push(sourceOffset)
    }
  }

  return { value: normalized, starts, ends }
}


function findCandidates(text: NormalizedText, terms: readonly string[]): MatchCandidate[] {
  const candidates: MatchCandidate[] = []

  for (const term of terms) {
    const normalizedTerm = normalizeText(term).value
    if (normalizedTerm.length === 0 || /^\s+$/u.test(normalizedTerm)) {
      continue
    }

    let normalizedStart = text.value.indexOf(normalizedTerm)
    while (normalizedStart >= 0) {
      const normalizedEnd = normalizedStart + normalizedTerm.length
      const originalStart = text.starts[normalizedStart]
      const originalEnd = text.ends[normalizedEnd - 1]
      if (originalStart !== undefined && originalEnd !== undefined) {
        candidates.push({ start: originalStart, end: originalEnd, normalizedStart, normalizedEnd })
      }
      normalizedStart = text.value.indexOf(normalizedTerm, normalizedStart + 1)
    }
  }

  return candidates
}


function chooseCandidate(candidates: readonly MatchCandidate[]): MatchCandidate | null {
  let chosen: MatchCandidate | null = null

  for (const candidate of candidates) {
    if (
      chosen === null ||
      candidate.start < chosen.start ||
      (candidate.start === chosen.start && candidate.end - candidate.start > chosen.end - chosen.start)
    ) {
      chosen = candidate
    }
  }

  return chosen
}


function stripSnippetEllipses(snippet: string): string {
  return snippet.replace(/^(?:(?:\.\.\.)|\u2026)+/u, '').replace(/(?:(?:\.\.\.)|\u2026)+$/u, '')
}


export function findTextMatch(
  text: string,
  terms: readonly string[],
  snippet?: string,
): TextMatch | null {
  const normalizedText = normalizeText(text)
  const candidates = findCandidates(normalizedText, terms)
  if (candidates.length === 0) {
    return null
  }

  if (snippet) {
    const normalizedSnippet = normalizeText(stripSnippetEllipses(snippet))
    if (normalizedSnippet.value.length > 0) {
      const snippetStarts: number[] = []
      let snippetStart = normalizedText.value.indexOf(normalizedSnippet.value)
      while (snippetStart >= 0) {
        snippetStarts.push(snippetStart)
        snippetStart = normalizedText.value.indexOf(normalizedSnippet.value, snippetStart + 1)
      }

      for (const contextStart of snippetStarts) {
        const contextEnd = contextStart + normalizedSnippet.value.length
        const contextualCandidates = candidates.filter((candidate) => (
          candidate.normalizedStart >= contextStart && candidate.normalizedEnd <= contextEnd
        ))
        const contextualCandidate = chooseCandidate(contextualCandidates)
        if (contextualCandidate) {
          return { start: contextualCandidate.start, end: contextualCandidate.end }
        }
      }
    }
  }

  const firstCandidate = chooseCandidate(candidates)
  if (firstCandidate === null) {
    return null
  }

  return { start: firstCandidate.start, end: firstCandidate.end }
}
