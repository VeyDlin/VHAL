import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import test from 'node:test'
import {
  collectHighlightInputs,
  escapeHtml,
  getSymbolMembers,
  type ApiFile,
  type ApiSymbol,
} from '../src/utils/api-display.ts'

interface GeneratedPage {
  label: string
  api: ApiFile[]
}

const iqPage = JSON.parse(readFileSync(
  new URL('../src/generated/pages/Common--Utilities--Math--IQMath--IQ.h.json', import.meta.url),
  'utf8',
)) as GeneratedPage

test('collects signatures from free functions with optional members', () => {
  const api: ApiFile[] = [{ file: iqPage.label, symbols: iqPage.api.flatMap((file) => file.symbols) }]
  const inputs = collectHighlightInputs(api)
  const absFunction = api[0].symbols.find((symbol) => symbol.name === 'abs')

  assert.ok(absFunction)
  assert.equal(absFunction.members, undefined)
  assert.ok(inputs.has('constexpr IQ<Q, L> abs(IQ<Q, L> x)'))
})

test('collects method templates and nested member types recursively', () => {
  const nested: ApiSymbol = {
    kind: 'struct',
    name: 'Node',
    template: 'template<typename DataType>',
    members: {
      public: [{
        kind: 'method',
        name: 'Write',
        template: 'template<typename DataType>',
        returnType: 'void',
        params: [{ type: 'DataType', name: 'value' }],
      }],
    },
  }
  const api: ApiFile[] = [{
    file: 'Nested.h',
    symbols: [{ kind: 'class', name: 'Container', members: { public: [nested] } }],
  }]
  const inputs = collectHighlightInputs(api)

  assert.ok(inputs.has('template<typename DataType>'))
  assert.ok(inputs.has('void Write(DataType value)'))
  assert.deepEqual(getSymbolMembers(nested).protected, [])
})

test('escapes unhighlighted code safely', () => {
  assert.equal(escapeHtml('<DataType & value>'), '&lt;DataType &amp; value&gt;')
})
