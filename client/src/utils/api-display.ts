export interface ApiMember {
  kind: string
  name: string
  returnType?: string
  params?: Array<{ type: string; name: string }>
  qualifiers?: string[]
  type?: string
  default?: string
  defaultValue?: string
  values?: string[]
  description?: string
  template?: string
  members?: ApiMembers
  bases?: string[]
}

export interface ApiMembers {
  public?: ApiMember[]
  protected?: ApiMember[]
  private?: ApiMember[]
}

export interface ApiSymbol extends ApiMember {
  namespace?: string
  members?: ApiMembers
}

export interface ApiFile {
  file: string
  symbols: ApiSymbol[]
}

export const ACCESS_LEVELS = ['public', 'protected', 'private'] as const

export function getSymbolMembers(symbol: ApiSymbol): Required<ApiMembers> {
  return {
    public: symbol.members?.public ?? [],
    protected: symbol.members?.protected ?? [],
    private: symbol.members?.private ?? [],
  }
}

export function formatSignature(member: ApiMember): string {
  if (member.kind !== 'method') {
    return ''
  }

  const qualifiers = member.qualifiers ?? []
  const parts: string[] = []

  if (qualifiers.includes('static')) {
    parts.push('static ')
  }
  if (qualifiers.includes('constexpr')) {
    parts.push('constexpr ')
  }
  if (qualifiers.includes('virtual')) {
    parts.push('virtual ')
  }
  if (member.returnType) {
    parts.push(member.returnType, ' ')
  }

  parts.push(member.name || '(unknown)', '(')
  if (member.params && member.params.length > 0) {
    parts.push(member.params.map((parameter) => `${parameter.type} ${parameter.name}`).join(', '))
  }
  parts.push(')')

  if (qualifiers.includes('const')) {
    parts.push(' const')
  }
  if (qualifiers.includes('override')) {
    parts.push(' override')
  }
  if (qualifiers.includes('noexcept')) {
    parts.push(' noexcept')
  }

  return parts.join('')
}

export function collectHighlightInputs(api: ApiFile[]): Set<string> {
  const inputs = new Set<string>()

  function collectSymbol(symbol: ApiSymbol): void {
    if (symbol.template) {
      inputs.add(symbol.template)
    }
    if (symbol.kind === 'method') {
      const signature = formatSignature(symbol)
      if (signature) {
        inputs.add(signature)
      }
    }

    for (const access of ACCESS_LEVELS) {
      for (const member of getSymbolMembers(symbol)[access]) {
        if (member.kind === 'method') {
          const signature = formatSignature(member)
          if (signature) {
            inputs.add(signature)
          }
        }

        if (member.type) {
          inputs.add(member.type)
        }
        if (member.template) {
          inputs.add(member.template)
        }
        if (member.kind === 'class' || member.kind === 'struct') {
          collectSymbol(member)
        }
      }
    }
  }

  for (const apiFile of api) {
    for (const symbol of apiFile.symbols) {
      collectSymbol(symbol)
    }
  }

  return inputs
}

export function escapeHtml(value: string): string {
  return value.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
}
