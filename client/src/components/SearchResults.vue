<script setup lang="ts">
import { useRouter } from 'vue-router'
import { useSearch } from '../composables/useSearch'
import type { SearchResult } from '../composables/useSearch'

const router = useRouter()
const { query, results, pendingTab } = useSearch()

function navigate(result: SearchResult) {
  pendingTab.value = result.source === 'api' ? 'api' : null
  const target = result.path === '__home' ? '/' : '/docs/' + result.path
  query.value = ''
  router.push(target)
}

const sourceLabel: Record<string, string> = {
  docs: 'Documentation',
  api: 'API',
  both: 'Docs + API',
}

const sourceIcon: Record<string, string> = {
  docs: 'i-lucide-book-open',
  api: 'i-lucide-code',
  both: 'i-lucide-layers',
}

function highlightText(text: string, terms: string[]): string {
  if (!text || terms.length === 0) return escapeHtml(text)

  const escaped = terms.map(t => t.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'))
  const pattern = new RegExp(`(${escaped.join('|')})`, 'gi')

  return escapeHtml(text).replace(
    new RegExp(`(${escaped.map(e => escapeHtml(e)).join('|')})`, 'gi'),
    '<mark class="search-highlight">$1</mark>'
  )
}

function escapeHtml(str: string): string {
  return str
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
}

</script>

<template>
  <div class="w-full px-4 sm:px-8 py-6 sm:py-10">
    <div class="mb-6">
      <h1 class="text-2xl font-bold text-[var(--ui-text-highlighted)]">
        Search results
      </h1>
      <p class="text-sm text-[var(--ui-text-muted)] mt-1">
        {{ results.length }} result{{ results.length !== 1 ? 's' : '' }} for
        <span class="text-[var(--ui-primary)] font-medium">"{{ query }}"</span>
      </p>
    </div>

    <div class="space-y-3">
      <button
        v-for="result in results"
        :key="result.id"
        class="w-full text-left p-4 rounded-lg border border-[var(--ui-border)] bg-[var(--ui-bg)] hover:bg-[var(--ui-bg-elevated)] transition-colors cursor-pointer"
        @click="navigate(result)"
      >
        <div class="flex items-center gap-2 mb-1">
          <span
            class="text-base font-semibold text-[var(--ui-text-highlighted)]"
            v-html="highlightText(result.title, result.terms)"
          />
          <span class="ml-auto flex items-center gap-1 text-[10px] px-1.5 py-0.5 rounded bg-[var(--ui-bg-elevated)] text-[var(--ui-text-dimmed)]">
            <UIcon :name="sourceIcon[result.source]" class="size-3" />
            {{ sourceLabel[result.source] }}
          </span>
        </div>

        <p
          v-if="result.breadcrumb"
          class="text-xs text-[var(--ui-text-dimmed)] mb-1.5"
          v-html="highlightText(result.breadcrumb, result.terms)"
        />

        <p
          v-if="result.snippet"
          class="text-sm text-[var(--ui-text-muted)] line-clamp-2"
          v-html="highlightText(result.snippet, result.terms)"
        />
      </button>
    </div>

    <div v-if="results.length === 0" class="flex flex-col items-center justify-center py-20 text-center">
      <UIcon name="i-lucide-search-x" class="size-12 text-[var(--ui-text-dimmed)] mb-4" />
      <h2 class="text-xl font-semibold text-[var(--ui-text-highlighted)] mb-2">Nothing found</h2>
      <p class="text-[var(--ui-text-muted)]">
        Try a different query or check the spelling
      </p>
    </div>
  </div>
</template>

<style scoped>
:deep(.search-highlight) {
  background: color-mix(in srgb, var(--ui-primary) 25%, transparent);
  color: var(--ui-primary);
  border-radius: 2px;
  padding: 0 1px;
}
</style>
