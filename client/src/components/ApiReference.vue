<script setup lang="ts">
import { ref, onMounted, watch } from 'vue'
import { createHighlighter, type Highlighter } from 'shiki'

interface Member {
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
}

interface Symbol {
  kind: string
  name: string
  namespace?: string
  template?: string
  bases?: string[]
  description?: string
  members: {
    public: Member[]
    protected: Member[]
    private: Member[]
  }
}

interface ApiFile {
  file: string
  symbols: Symbol[]
}

interface Props {
  api: ApiFile[]
  pagePath?: string
}

const props = defineProps<Props>()

const highlightCache = ref<Map<string, string>>(new Map())
let hlInstance: Highlighter | null = null

async function initHighlighter() {
  hlInstance = await createHighlighter({
    themes: ['github-dark'],
    langs: ['cpp'],
  })
  // Pre-highlight all code snippets
  const entries = new Map<string, string>()
  for (const apiFile of props.api) {
    for (const sym of apiFile.symbols) {
      for (const access of ['public', 'protected', 'private'] as const) {
        for (const member of sym.members[access]) {
          if (member.kind === 'method') {
            const sig = formatSignature(member)
            if (sig && !entries.has(sig)) {
              entries.set(sig, highlightInline(sig))
            }
          }
          if (member.type && !entries.has(member.type)) {
            entries.set(member.type, highlightInline(member.type))
          }
        }
      }
      if (sym.template && !entries.has(sym.template)) {
        entries.set(sym.template, highlightInline(sym.template))
      }
    }
  }
  highlightCache.value = entries
}

function highlightInline(code: string): string {
  if (!hlInstance) return escapeHtml(code)
  const html = hlInstance.codeToHtml(code, { lang: 'cpp', theme: 'github-dark' })
  // Extract inner content from <pre><code>...</code></pre>
  const match = html.match(/<code>([\s\S]*?)<\/code>/)
  return match ? match[1] : escapeHtml(code)
}

function escapeHtml(s: string): string {
  return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
}

function getHighlighted(code: string): string {
  return highlightCache.value.get(code) || escapeHtml(code)
}

onMounted(() => {
  initHighlighter()
})

// Re-highlight when api prop changes (component reused across pages)
watch(() => props.api, () => {
  initHighlighter()
})

function getIncludePath(): string {
  if (!props.pagePath) return ''
  // Strip prefixes to get the actual include path:
  // "Common/Drivers/..." → "Drivers/..."
  // "Periphery/Adapter/..." → "Adapter/..."
  const path = props.pagePath.replace(/^Common\//, '').replace(/^Periphery\//, '')
  return `#include <${path}>`
}

function formatSignature(member: Member): string {
  if (member.kind !== 'method') return ''
  const quals = member.qualifiers || []
  const parts: string[] = []

  if (quals.includes('static')) parts.push('static ')
  if (quals.includes('constexpr')) parts.push('constexpr ')
  if (quals.includes('virtual')) parts.push('virtual ')

  if (member.returnType) {
    parts.push(member.returnType)
    parts.push(' ')
  }

  parts.push(member.name || '(unknown)')

  parts.push('(')
  if (member.params && member.params.length > 0) {
    parts.push(member.params.map(p => `${p.type} ${p.name}`).join(', '))
  }
  parts.push(')')

  if (quals.includes('const')) parts.push(' const')
  if (quals.includes('override')) parts.push(' override')
  if (quals.includes('noexcept')) parts.push(' noexcept')

  return parts.join('')
}

function getPostfixQualifiers(member: Member): string[] {
  if (!member.qualifiers) return []
  return member.qualifiers.filter(q => ['static', 'const', 'virtual', 'override', 'constexpr', 'noexcept'].includes(q))
}

function cleanDescription(desc?: string): string {
  if (!desc) return ''
  // Strip decorative comment markers like "=== Callbacks ===" or "--- Tween state ---"
  return desc.replace(/^[=\-\s]+.*?[=\-\s]+$/g, '').trim()
}

function hasMembers(members: Member[]): boolean {
  return members.length > 0
}

function getMethods(members: Member[]): Member[] {
  return members.filter(m => m.kind === 'method')
}

function getFields(members: Member[]): Member[] {
  return members.filter(m => m.kind === 'field')
}

function getEnums(members: Member[]): Member[] {
  return members.filter(m => m.kind === 'enum')
}

function isMultilineType(type?: string): boolean {
  return !!type && type.includes('\n')
}

function getMembers(sym: Symbol): Symbol['members'] {
  return sym.members || { public: [], protected: [], private: [] }
}

function hasAnyMembers(sym: Symbol): boolean {
  const m = getMembers(sym)
  return hasMembers(m.public) || hasMembers(m.protected) || hasMembers(m.private)
}
</script>

<template>
  <div>
    <h2 class="text-2xl font-bold text-[var(--ui-text-highlighted)] mb-6 border-b border-[var(--ui-border)] pb-3">
      API Reference
    </h2>

    <div v-for="apiFile in api" :key="apiFile.file" class="mb-10">
      <h3 class="text-lg font-semibold text-[var(--ui-text-highlighted)] flex items-center gap-2">
        <UIcon name="i-lucide-file-code" class="size-5 text-[var(--ui-text-dimmed)]" />
        {{ apiFile.file }}
      </h3>
      <div v-if="getIncludePath()" class="mb-4">
        <code class="text-xs text-[var(--ui-text-dimmed)]">{{ getIncludePath() }}</code>
      </div>
      <div v-else class="mb-4" />

      <div v-for="sym in apiFile.symbols" :key="sym.name" class="mb-8 border border-[var(--ui-border)] rounded-lg overflow-hidden">
        <!-- Symbol header -->
        <div class="bg-[var(--ui-bg-elevated)] px-5 py-4" :class="hasAnyMembers(sym) ? 'border-b border-[var(--ui-border)]' : ''">
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
          <!-- Standalone function signature -->
          <div v-if="sym.kind === 'method' && !hasAnyMembers(sym)" class="mt-2">
            <code class="text-xs font-mono shiki-inline" v-html="getHighlighted(formatSignature(sym as any))" />
          </div>
        </div>

        <div v-if="hasAnyMembers(sym)" class="px-5 py-4">
          <!-- Public members -->
          <div v-if="hasMembers(getMembers(sym).public)">
            <h4 class="text-sm font-semibold text-[var(--ui-text-highlighted)] uppercase tracking-wider mb-3">
              Public
            </h4>

            <!-- Enums -->
            <div v-for="en in getEnums(getMembers(sym).public)" :key="'enum-' + en.name" class="mb-4">
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

            <!-- Fields -->
            <div v-if="getFields(getMembers(sym).public).length > 0" class="mb-4 overflow-x-auto">
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
                    v-for="field in getFields(getMembers(sym).public)"
                    :key="'field-' + field.name"
                    class="border-b border-[var(--ui-border)] last:border-b-0"
                  >
                    <td class="py-2 pr-4">
                      <pre v-if="isMultilineType(field.type)" class="text-xs bg-[var(--ui-bg-elevated)] rounded px-2 py-1 overflow-x-auto m-0 border-0"><code class="shiki-inline" v-html="getHighlighted(field.type)" /></pre>
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
            <div v-if="getMethods(getMembers(sym).public).length > 0">
              <div
                v-for="(method, idx) in getMethods(getMembers(sym).public)"
                :key="'method-' + idx"
                class="mb-3 border border-[var(--ui-border)] rounded-md overflow-hidden"
              >
                <div class="bg-[var(--ui-bg)] px-4 py-2.5 flex items-start gap-2 flex-wrap">
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
          <div v-if="hasMembers(getMembers(sym).protected)" class="mt-5">
            <UCollapsible>
              <UButton
                color="neutral"
                variant="ghost"
                size="sm"
                class="w-full justify-start"
                trailing-icon="i-lucide-chevron-down"
              >
                <span class="text-sm font-semibold uppercase tracking-wider">Protected</span>
                <UBadge color="neutral" variant="subtle" size="xs" class="ml-2">
                  {{ getMembers(sym).protected.length }}
                </UBadge>
              </UButton>
              <template #content>
                <div class="pl-4 pt-3">
                  <!-- Protected fields -->
                  <div v-if="getFields(getMembers(sym).protected).length > 0" class="mb-4 overflow-x-auto">
                    <table class="w-full text-sm min-w-[500px]">
                      <tbody>
                        <tr
                          v-for="field in getFields(getMembers(sym).protected)"
                          :key="'prot-field-' + field.name"
                          class="border-b border-[var(--ui-border)] last:border-b-0"
                        >
                          <td class="py-2 pr-4">
                            <pre v-if="isMultilineType(field.type)" class="text-xs bg-[var(--ui-bg-elevated)] rounded px-2 py-1 overflow-x-auto m-0 border-0"><code class="shiki-inline" v-html="getHighlighted(field.type)" /></pre>
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
                    v-for="(method, idx) in getMethods(getMembers(sym).protected)"
                    :key="'prot-method-' + idx"
                    class="mb-3 border border-[var(--ui-border)] rounded-md overflow-hidden"
                  >
                    <div class="bg-[var(--ui-bg)] px-4 py-2.5">
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
          <div v-if="hasMembers(getMembers(sym).private)" class="mt-5">
            <UCollapsible>
              <UButton
                color="neutral"
                variant="ghost"
                size="sm"
                class="w-full justify-start"
                trailing-icon="i-lucide-chevron-down"
              >
                <span class="text-sm font-semibold uppercase tracking-wider">Private</span>
                <UBadge color="neutral" variant="subtle" size="xs" class="ml-2">
                  {{ getMembers(sym).private.length }}
                </UBadge>
              </UButton>
              <template #content>
                <div class="pl-4 pt-3">
                  <!-- Private fields -->
                  <div v-if="getFields(getMembers(sym).private).length > 0" class="mb-4 overflow-x-auto">
                    <table class="w-full text-sm min-w-[500px]">
                      <tbody>
                        <tr
                          v-for="field in getFields(getMembers(sym).private)"
                          :key="'priv-field-' + field.name"
                          class="border-b border-[var(--ui-border)] last:border-b-0"
                        >
                          <td class="py-2 pr-4">
                            <pre v-if="isMultilineType(field.type)" class="text-xs bg-[var(--ui-bg-elevated)] rounded px-2 py-1 overflow-x-auto m-0 border-0"><code class="shiki-inline" v-html="getHighlighted(field.type)" /></pre>
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
                    v-for="(method, idx) in getMethods(getMembers(sym).private)"
                    :key="'priv-method-' + idx"
                    class="mb-3 border border-[var(--ui-border)] rounded-md overflow-hidden"
                  >
                    <div class="bg-[var(--ui-bg)] px-4 py-2.5">
                      <code class="text-xs font-mono break-all shiki-inline" v-html="getHighlighted(formatSignature(method))" />
                    </div>
                    <div v-if="cleanDescription(method.description)" class="px-4 py-2 bg-[var(--ui-bg-elevated)] border-t border-[var(--ui-border)]">
                      <p class="text-xs text-[var(--ui-text-muted)] whitespace-pre-wrap font-mono break-words">{{ cleanDescription(method.description) }}</p>
                    </div>
                  </div>
                  <!-- Private enums -->
                  <div v-for="en in getEnums(getMembers(sym).private)" :key="'priv-enum-' + en.name" class="mb-4">
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
