<script setup lang="ts">
import { ref, onMounted, watch } from 'vue'
import { nextTick } from 'vue'
import type { Highlighter } from 'shiki'
import { getHighlighter, yieldAfterPaint } from '../composables/useShiki'
import { findTextMatch } from '../utils/search-match'
import {
  collectHighlightInputs,
  escapeHtml,
  formatSignature,
  getSymbolMembers,
  type ApiFile,
  type ApiMember,
  type ApiSymbol,
} from '../utils/api-display'

interface Props {
  api: ApiFile[]
  pagePath?: string
  compact?: boolean
  sharedHighlightCache?: Map<string, string>
  searchTerms?: string[]
  searchSourceKey?: string
  sourcePrefix?: string
}

const props = defineProps<Props>()
const emit = defineEmits<{ ready: [value: boolean] }>()

const highlightCache = ref<Map<string, string>>(new Map())
let hlInstance: Highlighter | null = null
let highlightRequest: number = 0

async function initHighlighter(): Promise<void> {
  const request: number = ++highlightRequest
  try {
    await yieldAfterPaint()
    if (request !== highlightRequest) {
      return
    }
    hlInstance = await getHighlighter()
    const entries: Map<string, string> = new Map()
    for (const input of collectHighlightInputs(props.api)) {
      if (!entries.has(input)) {
        entries.set(input, highlightInline(input))
      }
    }
    if (request === highlightRequest) {
      highlightCache.value = entries
    }
  } catch {
    // Escaped plain-text signatures remain available if syntax highlighting fails.
  }
}

function highlightInline(code: string): string {
  if (!hlInstance) return escapeHtml(code)
  const html = hlInstance.codeToHtml(code, {
    lang: 'cpp',
    themes: { light: 'github-light', dark: 'github-dark' },
  })
  // Extract inner content from <pre><code>...</code></pre>
  const match = html.match(/<code>([\s\S]*?)<\/code>/)
  return match?.[1] ?? escapeHtml(code)
}

onMounted(() => {
  if (!props.compact) {
    emit('ready', true)
    void initHighlighter()
  }
})

// Re-highlight when api prop changes (component reused across pages)
watch(() => props.api, () => {
  if (!props.compact) {
    highlightRequest += 1
    highlightCache.value = new Map()
    void nextTick().then(() => {
      emit('ready', true)
      void initHighlighter()
    })
  }
})

function getIncludePath(): string {
  if (!props.pagePath) return ''
  // Strip prefixes to get the actual include path:
  // "Common/Drivers/..." → "Drivers/..."
  // "Periphery/Adapter/..." → "Adapter/..."
  const path = props.pagePath.replace(/^Common\//, '').replace(/^Periphery\//, '')
  return `#include <${path}>`
}

function getPostfixQualifiers(member: ApiMember): string[] {
  if (!member.qualifiers) return []
  return member.qualifiers.filter(q => ['static', 'const', 'virtual', 'override', 'constexpr', 'noexcept'].includes(q))
}

function cleanDescription(desc?: string): string {
  if (!desc) return ''
  // Strip decorative comment markers like "=== Callbacks ===" or "--- Tween state ---"
  return desc.replace(/^[=\-\s]+.*?[=\-\s]+$/g, '').trim()
}

function hasMembers(members: ApiMember[]): boolean {
  return members.length > 0
}

function getMethods(members: ApiMember[]): ApiMember[] {
  return members.filter(m => m.kind === 'method')
}

function getFields(members: ApiMember[]): ApiMember[] {
  return members.filter(m => m.kind === 'field')
}

function getEnums(members: ApiMember[]): ApiMember[] {
  return members.filter(m => m.kind === 'enum')
}

function getNestedTypes(members: ApiMember[]): ApiMember[] {
  return members.filter(m => m.kind === 'class' || m.kind === 'struct')
}

function isMultilineType(type?: string): boolean {
  return !!type && type.includes('\n')
}

function hasAnyMembers(sym: ApiSymbol): boolean {
  const m = getSymbolMembers(sym)
  return hasMembers(m.public) || hasMembers(m.protected) || hasMembers(m.private)
}

function getHighlighted(code: string): string {
  return highlightCache.value.get(code) || props.sharedHighlightCache?.get(code) || escapeHtml(code)
}

function getSharedHighlightCache(): Map<string, string> {
  return props.sharedHighlightCache ?? highlightCache.value
}


function hasSearchMatch(members: ApiMember[]): boolean {
  const terms: string[] = props.searchTerms ?? []
  if (terms.length === 0) {
    return false
  }
  return members.some((member: ApiMember): boolean => {
    const text: string = [
      member.name, member.type, member.template, member.description,
      formatSignature(member), ...(member.values ?? []), ...(member.bases ?? []),
    ].filter(Boolean).join(' ')
    return findTextMatch(text, terms) !== null
      || hasSearchMatch(Object.values(getSymbolMembers(member)).flat())
  })
}

function symbolKey(fileIndex: number, symbolIndex: number): string {
  return props.sourcePrefix ?? `f${fileIndex}/s${symbolIndex}`
}

function memberKey(
  fileIndex: number,
  symbolIndex: number,
  symbol: ApiSymbol,
  access: 'public' | 'protected' | 'private',
  member: ApiMember,
): string {
  const index = getSymbolMembers(symbol)[access].indexOf(member)
  return `${symbolKey(fileIndex, symbolIndex)}/${access}/${index}`
}

function sourceWithin(fileIndex: number, symbolIndex: number, access: 'protected' | 'private'): boolean {
  return props.searchSourceKey?.startsWith(`${symbolKey(fileIndex, symbolIndex)}/${access}/`) ?? false
}
</script>

<template>
  <div>
    <h2 v-if="!compact" class="text-2xl font-bold text-[var(--ui-text-highlighted)] mb-6 border-b border-[var(--ui-border)] pb-3">
      API Reference
    </h2>

    <div v-for="(apiFile, fileIndex) in api" :key="apiFile.file" :class="compact ? '' : 'mb-10'">
      <h3 v-if="!compact" class="text-lg font-semibold text-[var(--ui-text-highlighted)] flex items-center gap-2">
        <UIcon name="i-lucide-file-code" class="size-5 text-[var(--ui-text-dimmed)]" />
        {{ apiFile.file }}
      </h3>
      <div v-if="!compact && getIncludePath()" class="mb-4">
        <code class="text-xs text-[var(--ui-text-dimmed)]">{{ getIncludePath() }}</code>
      </div>
      <div v-else-if="!compact" class="mb-4" />

      <div v-for="(sym, symbolIndex) in apiFile.symbols" :key="sym.name" class="mb-8 border border-[var(--ui-border)] rounded-lg overflow-hidden">
        <!-- Symbol header -->
        <div :data-search-scope="symbolKey(fileIndex, symbolIndex)" class="bg-[var(--ui-bg-elevated)] px-5 py-4" :class="hasAnyMembers(sym) ? 'border-b border-[var(--ui-border)]' : ''">
          <div class="flex items-center gap-3 flex-wrap">
            <UBadge :color="sym.kind === 'class' ? 'info' : sym.kind === 'method' ? 'warning' : 'success'" variant="subtle" size="sm">
              {{ sym.kind === 'method' ? 'function' : sym.kind }}
            </UBadge>
            <code class="text-base font-bold text-[var(--ui-text-highlighted)]">
              <span v-if="sym.namespace" class="text-[var(--ui-text-dimmed)] font-normal">{{ sym.namespace }}::</span>{{ sym.name }}
            </code>
          </div>
          <div v-if="sym.template" class="mt-1 flex items-center gap-2">
            <UIcon name="i-lucide-braces" class="size-3.5 text-[var(--ui-text-dimmed)] shrink-0" />
            <code class="text-xs shiki-inline" v-html="getHighlighted(sym.template)" />
          </div>
          <div v-if="sym.bases && sym.bases.length > 0" class="mt-1 text-xs text-[var(--ui-text-muted)]">
            extends
            <code v-for="(base, i) in sym.bases" :key="base" class="text-[var(--ui-primary)]">
              {{ base }}<span v-if="i < sym.bases.length - 1">, </span>
            </code>
          </div>
          <p v-if="cleanDescription(sym.description)" class="mt-2 text-sm text-[var(--ui-text-muted)] whitespace-pre-wrap font-mono break-words">
            {{ cleanDescription(sym.description) }}
          </p>
          <div v-if="sym.kind === 'enum' && sym.values?.length" class="mt-3 flex flex-wrap gap-1.5">
            <UBadge v-for="value in sym.values" :key="value" color="neutral" variant="outline" size="xs">
              {{ value }}
            </UBadge>
          </div>
          <!-- Standalone function signature -->
          <div v-if="sym.kind === 'method' && !hasAnyMembers(sym)" class="mt-2">
            <code class="text-xs font-mono shiki-inline" v-html="getHighlighted(formatSignature(sym))" />
          </div>
        </div>

        <div v-if="hasAnyMembers(sym)" class="px-5 py-4">
          <!-- Public members -->
          <div v-if="hasMembers(getSymbolMembers(sym).public)">
            <h4 class="text-sm font-semibold text-[var(--ui-text-highlighted)] uppercase tracking-wider mb-3">
              Public
            </h4>

            <!-- Enums -->
            <div v-for="en in getEnums(getSymbolMembers(sym).public)" :key="'enum-' + en.name" :data-search-scope="memberKey(fileIndex, symbolIndex, sym, 'public', en)" class="mb-4">
              <div class="flex items-center gap-2 mb-2">
                <UBadge color="warning" variant="subtle" size="xs">enum</UBadge>
                <code class="text-sm font-semibold text-[var(--ui-text-highlighted)]">{{ en.name }}</code>
              </div>
              <p v-if="cleanDescription(en.description)" class="text-sm text-[var(--ui-text-muted)] mb-2 whitespace-pre-wrap font-mono break-words">
                {{ cleanDescription(en.description) }}
              </p>
              <div class="ml-4 flex flex-wrap gap-1.5">
                <UBadge
                  v-for="val in en.values"
                  :key="val"
                  color="neutral"
                  variant="outline"
                  size="xs"
                >
                  {{ val }}
                </UBadge>
              </div>
            </div>

            <ApiReference
              v-for="nested in getNestedTypes(getSymbolMembers(sym).public)"
              :key="'public-nested-' + nested.name"
              :api="[{ file: apiFile.file, symbols: [nested] }]"
              :shared-highlight-cache="getSharedHighlightCache()"
              :search-terms="searchTerms"
              :search-source-key="searchSourceKey"
              :source-prefix="memberKey(fileIndex, symbolIndex, sym, 'public', nested)"
              compact
            />

            <!-- Fields -->
            <div v-if="getFields(getSymbolMembers(sym).public).length > 0" class="mb-4 overflow-x-auto">
              <table class="w-full text-sm min-w-[500px]">
                <thead>
                  <tr class="border-b border-[var(--ui-border)]">
                    <th class="text-left py-2 pr-4 text-[var(--ui-text-dimmed)] font-medium text-xs uppercase tracking-wider">Type</th>
                    <th class="text-left py-2 pr-4 text-[var(--ui-text-dimmed)] font-medium text-xs uppercase tracking-wider">Name</th>
                    <th class="text-left py-2 text-[var(--ui-text-dimmed)] font-medium text-xs uppercase tracking-wider">Description</th>
                  </tr>
                </thead>
                <tbody>
                  <tr
                    v-for="field in getFields(getSymbolMembers(sym).public)"
                    :key="'field-' + field.name"
                    :data-search-scope="memberKey(fileIndex, symbolIndex, sym, 'public', field)"
                    class="border-b border-[var(--ui-border)] last:border-b-0"
                  >
                    <td class="py-2 pr-4">
                      <pre v-if="isMultilineType(field.type)" class="text-xs bg-[var(--ui-bg-elevated)] rounded px-2 py-1 overflow-x-auto m-0 border-0"><code class="shiki-inline" v-html="getHighlighted(field.type || '')" /></pre>
                      <code v-else class="text-xs shiki-inline whitespace-nowrap" v-html="getHighlighted(field.type || '')" />
                    </td>
                    <td class="py-2 pr-4">
                      <code class="text-xs font-semibold text-[var(--ui-text-highlighted)]">{{ field.name }}</code>
                      <span v-if="field.default || field.defaultValue" class="text-xs text-[var(--ui-text-dimmed)]">
                        = {{ field.default || field.defaultValue }}
                      </span>
                    </td>
                    <td class="py-2 text-xs text-[var(--ui-text-muted)]">
                      {{ cleanDescription(field.description) }}
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>

            <!-- Methods -->
            <div v-if="getMethods(getSymbolMembers(sym).public).length > 0">
              <div
                v-for="(method, idx) in getMethods(getSymbolMembers(sym).public)"
                :key="'method-' + idx"
                :data-search-scope="memberKey(fileIndex, symbolIndex, sym, 'public', method)"
                class="mb-3 border border-[var(--ui-border)] rounded-md overflow-hidden"
              >
                <div class="bg-[var(--ui-bg)] px-4 py-2.5 flex items-start gap-2 flex-wrap">
                  <code v-if="method.template" class="w-full text-xs font-mono shiki-inline" v-html="getHighlighted(method.template)" />
                  <code class="text-xs font-mono leading-relaxed break-all shiki-inline" v-html="getHighlighted(formatSignature(method))" />
                  <div class="flex gap-1 ml-auto shrink-0">
                    <UBadge
                      v-for="q in getPostfixQualifiers(method)"
                      :key="q"
                      color="neutral"
                      variant="subtle"
                      size="xs"
                    >
                      {{ q }}
                    </UBadge>
                  </div>
                </div>
                <div v-if="cleanDescription(method.description)" class="px-4 py-2 bg-[var(--ui-bg-elevated)] border-t border-[var(--ui-border)]">
                  <p class="text-xs text-[var(--ui-text-muted)] whitespace-pre-wrap font-mono break-words">{{ cleanDescription(method.description) }}</p>
                </div>
              </div>
            </div>
          </div>

          <!-- Protected members -->
          <div v-if="hasMembers(getSymbolMembers(sym).protected)" class="mt-5">
            <UCollapsible :default-open="sourceWithin(fileIndex, symbolIndex, 'protected') || hasSearchMatch(getSymbolMembers(sym).protected)">
              <UButton
                color="neutral"
                variant="ghost"
                size="sm"
                class="w-full justify-start"
                trailing-icon="i-lucide-chevron-down"
              >
                <span class="text-sm font-semibold uppercase tracking-wider">Protected</span>
                <UBadge color="neutral" variant="subtle" size="xs" class="ml-2">
                  {{ getSymbolMembers(sym).protected.length }}
                </UBadge>
              </UButton>
              <template #content>
                <div class="pl-4 pt-3">
                  <ApiReference
                    v-for="nested in getNestedTypes(getSymbolMembers(sym).protected)"
                    :key="'protected-nested-' + nested.name"
                    :api="[{ file: apiFile.file, symbols: [nested] }]"
                    :shared-highlight-cache="getSharedHighlightCache()"
                    :search-terms="searchTerms"
                    :search-source-key="searchSourceKey"
                    :source-prefix="memberKey(fileIndex, symbolIndex, sym, 'protected', nested)"
                    compact
                  />
                  <ApiReference
                    v-for="en in getEnums(getSymbolMembers(sym).protected)"
                    :key="'protected-enum-' + en.name"
                    :api="[{ file: apiFile.file, symbols: [en] }]"
                    :shared-highlight-cache="getSharedHighlightCache()"
                    :search-source-key="searchSourceKey"
                    :source-prefix="memberKey(fileIndex, symbolIndex, sym, 'protected', en)"
                    compact
                  />
                  <!-- Protected fields -->
                  <div v-if="getFields(getSymbolMembers(sym).protected).length > 0" class="mb-4 overflow-x-auto">
                    <table class="w-full text-sm min-w-[500px]">
                      <tbody>
                        <tr
                          v-for="field in getFields(getSymbolMembers(sym).protected)"
                          :key="'prot-field-' + field.name"
                          :data-search-scope="memberKey(fileIndex, symbolIndex, sym, 'protected', field)"
                          class="border-b border-[var(--ui-border)] last:border-b-0"
                        >
                          <td class="py-2 pr-4">
                            <pre v-if="isMultilineType(field.type)" class="text-xs bg-[var(--ui-bg-elevated)] rounded px-2 py-1 overflow-x-auto m-0 border-0"><code class="shiki-inline" v-html="getHighlighted(field.type || '')" /></pre>
                            <code v-else class="text-xs shiki-inline whitespace-nowrap" v-html="getHighlighted(field.type || '')" />
                          </td>
                          <td class="py-2 pr-4">
                            <code class="text-xs font-semibold text-[var(--ui-text-highlighted)]">{{ field.name }}</code>
                          </td>
                          <td class="py-2 text-xs text-[var(--ui-text-muted)]">
                            {{ cleanDescription(field.description) }}
                          </td>
                        </tr>
                      </tbody>
                    </table>
                  </div>
                  <!-- Protected methods -->
                  <div
                    v-for="(method, idx) in getMethods(getSymbolMembers(sym).protected)"
                    :key="'prot-method-' + idx"
                    :data-search-scope="memberKey(fileIndex, symbolIndex, sym, 'protected', method)"
                    class="mb-3 border border-[var(--ui-border)] rounded-md overflow-hidden"
                  >
                    <div class="bg-[var(--ui-bg)] px-4 py-2.5">
                      <code v-if="method.template" class="block mb-1 text-xs font-mono shiki-inline" v-html="getHighlighted(method.template)" />
                      <code class="text-xs font-mono break-all shiki-inline" v-html="getHighlighted(formatSignature(method))" />
                    </div>
                    <div v-if="cleanDescription(method.description)" class="px-4 py-2 bg-[var(--ui-bg-elevated)] border-t border-[var(--ui-border)]">
                      <p class="text-xs text-[var(--ui-text-muted)] whitespace-pre-wrap font-mono break-words">{{ cleanDescription(method.description) }}</p>
                    </div>
                  </div>
                </div>
              </template>
            </UCollapsible>
          </div>

          <!-- Private members -->
          <div v-if="hasMembers(getSymbolMembers(sym).private)" class="mt-5">
            <UCollapsible :default-open="sourceWithin(fileIndex, symbolIndex, 'private') || hasSearchMatch(getSymbolMembers(sym).private)">
              <UButton
                color="neutral"
                variant="ghost"
                size="sm"
                class="w-full justify-start"
                trailing-icon="i-lucide-chevron-down"
              >
                <span class="text-sm font-semibold uppercase tracking-wider">Private</span>
                <UBadge color="neutral" variant="subtle" size="xs" class="ml-2">
                  {{ getSymbolMembers(sym).private.length }}
                </UBadge>
              </UButton>
              <template #content>
                <div class="pl-4 pt-3">
                  <ApiReference
                    v-for="nested in getNestedTypes(getSymbolMembers(sym).private)"
                    :key="'private-nested-' + nested.name"
                    :api="[{ file: apiFile.file, symbols: [nested] }]"
                    :shared-highlight-cache="getSharedHighlightCache()"
                    :search-terms="searchTerms"
                    :search-source-key="searchSourceKey"
                    :source-prefix="memberKey(fileIndex, symbolIndex, sym, 'private', nested)"
                    compact
                  />
                  <!-- Private fields -->
                  <div v-if="getFields(getSymbolMembers(sym).private).length > 0" class="mb-4 overflow-x-auto">
                    <table class="w-full text-sm min-w-[500px]">
                      <tbody>
                        <tr
                          v-for="field in getFields(getSymbolMembers(sym).private)"
                          :key="'priv-field-' + field.name"
                          :data-search-scope="memberKey(fileIndex, symbolIndex, sym, 'private', field)"
                          class="border-b border-[var(--ui-border)] last:border-b-0"
                        >
                          <td class="py-2 pr-4">
                            <pre v-if="isMultilineType(field.type)" class="text-xs bg-[var(--ui-bg-elevated)] rounded px-2 py-1 overflow-x-auto m-0 border-0"><code class="shiki-inline" v-html="getHighlighted(field.type || '')" /></pre>
                            <code v-else class="text-xs shiki-inline whitespace-nowrap" v-html="getHighlighted(field.type || '')" />
                          </td>
                          <td class="py-2 pr-4">
                            <code class="text-xs font-semibold text-[var(--ui-text-highlighted)]">{{ field.name || '(anonymous)' }}</code>
                            <span v-if="field.default || field.defaultValue" class="text-xs text-[var(--ui-text-dimmed)]">
                              = {{ field.default || field.defaultValue }}
                            </span>
                          </td>
                          <td class="py-2 text-xs text-[var(--ui-text-muted)]">
                            {{ cleanDescription(field.description) }}
                          </td>
                        </tr>
                      </tbody>
                    </table>
                  </div>
                  <!-- Private methods -->
                  <div
                    v-for="(method, idx) in getMethods(getSymbolMembers(sym).private)"
                    :key="'priv-method-' + idx"
                    :data-search-scope="memberKey(fileIndex, symbolIndex, sym, 'private', method)"
                    class="mb-3 border border-[var(--ui-border)] rounded-md overflow-hidden"
                  >
                    <div class="bg-[var(--ui-bg)] px-4 py-2.5">
                      <code v-if="method.template" class="block mb-1 text-xs font-mono shiki-inline" v-html="getHighlighted(method.template)" />
                      <code class="text-xs font-mono break-all shiki-inline" v-html="getHighlighted(formatSignature(method))" />
                    </div>
                    <div v-if="cleanDescription(method.description)" class="px-4 py-2 bg-[var(--ui-bg-elevated)] border-t border-[var(--ui-border)]">
                      <p class="text-xs text-[var(--ui-text-muted)] whitespace-pre-wrap font-mono break-words">{{ cleanDescription(method.description) }}</p>
                    </div>
                  </div>
                  <!-- Private enums -->
                  <div v-for="en in getEnums(getSymbolMembers(sym).private)" :key="'priv-enum-' + en.name" :data-search-scope="memberKey(fileIndex, symbolIndex, sym, 'private', en)" class="mb-4">
                    <div class="flex items-center gap-2 mb-2">
                      <UBadge color="warning" variant="subtle" size="xs">enum</UBadge>
                      <code class="text-sm font-semibold text-[var(--ui-text-highlighted)]">{{ en.name }}</code>
                    </div>
                    <div class="ml-4 flex flex-wrap gap-1.5">
                      <UBadge
                        v-for="val in en.values"
                        :key="val"
                        color="neutral"
                        variant="outline"
                        size="xs"
                      >
                        {{ val }}
                      </UBadge>
                    </div>
                  </div>
                </div>
              </template>
            </UCollapsible>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.shiki-inline :deep(span) {
  font-family: inherit;
}
.shiki-inline {
  background: transparent !important;
}
</style>
