<script setup lang="ts">
import { ref, watch, nextTick, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { highlightHtmlString } from '../composables/useShiki'

const router = useRouter()
const docContentRef = ref<HTMLElement | null>(null)
const processedReadme = ref<string>('')

const homeModule = import.meta.glob('../generated/pages/__home.json', { eager: true }) as Record<string, { default?: any } & Record<string, any>>
const homeKey = '../generated/pages/__home.json'
const homeMod = homeModule[homeKey]
const homeData = (homeMod?.default ?? homeMod) as { readme?: string } | null

function externalLinksNewTab(container: HTMLElement) {
  container.querySelectorAll<HTMLAnchorElement>('a[href]').forEach(a => {
    const href = a.getAttribute('href') || ''
    if (href.startsWith('http://') || href.startsWith('https://')) {
      a.setAttribute('target', '_blank')
      a.setAttribute('rel', 'noopener noreferrer')
    }
  })
}

// Process readme with syntax highlighting
watch(
  () => homeData?.readme,
  async (raw) => {
    if (!raw) {
      processedReadme.value = ''
      return
    }
    processedReadme.value = raw
    const highlighted = await highlightHtmlString(raw)
    processedReadme.value = highlighted
  },
  { immediate: true }
)

watch(
  processedReadme,
  async () => {
    await nextTick()
    if (docContentRef.value) {
      externalLinksNewTab(docContentRef.value)
    }
  }
)
</script>

<template>
  <div class="w-full px-4 sm:px-8 py-6 sm:py-10">
    <div
      v-if="processedReadme"
      ref="docContentRef"
      class="doc-content prose prose-invert max-w-none"
      v-html="processedReadme"
    />

    <div v-else class="flex items-center justify-center min-h-[60vh] text-center">
      <div>
        <h1 class="text-5xl font-bold text-[var(--ui-text-highlighted)] mb-4">VHAL</h1>
        <p class="text-xl text-[var(--ui-text-muted)]">C++20 multiplatform HAL library for embedded MCUs</p>
      </div>
    </div>
  </div>
</template>

<style scoped>
.doc-content :deep(h1) {
  font-size: 2rem;
  margin-bottom: 1rem;
  padding-bottom: 0.5rem;
  border-bottom: 1px solid var(--ui-border);
}

.doc-content :deep(h2) {
  font-size: 1.5rem;
  margin-top: 2.5rem;
  margin-bottom: 0.75rem;
  padding-bottom: 0.3rem;
  border-bottom: 1px solid var(--ui-border);
}

.doc-content :deep(h3) {
  font-size: 1.25rem;
  margin-top: 2rem;
  margin-bottom: 0.5rem;
}

.doc-content :deep(.heading-anchor) {
  position: relative;
}

.doc-content :deep(.anchor-link) {
  margin-left: 0.25em;
  opacity: 0;
  color: var(--ui-text-dimmed);
  text-decoration: none;
  font-weight: 400;
  transition: opacity 0.15s;
}

.doc-content :deep(.heading-anchor:hover .anchor-link) {
  opacity: 1;
}

.doc-content :deep(.anchor-link:hover) {
  color: var(--ui-primary);
  text-decoration: none;
}

.doc-content :deep(pre) {
  background: var(--ui-bg-elevated);
  border: 1px solid var(--ui-border);
  border-radius: 0.5rem;
  padding: 1rem;
  overflow-x: auto;
  margin: 1rem 0;
}

.doc-content :deep(pre.shiki-highlighted) {
  border: 1px solid var(--ui-border);
  border-radius: 0.5rem;
  padding: 1rem;
  overflow-x: auto;
  margin: 1rem 0;
}

.doc-content :deep(code) {
  font-size: 0.875em;
}

.doc-content :deep(code)::before,
.doc-content :deep(code)::after {
  content: none;
}

.doc-content :deep(:not(pre) > code) {
  background: var(--ui-bg-elevated);
  padding: 0.15em 0.4em;
  border-radius: 0.25rem;
  color: var(--ui-primary);
}

.doc-content :deep(table) {
  width: 100%;
  border-collapse: collapse;
  margin: 1rem 0;
}

.doc-content :deep(th) {
  background: var(--ui-bg-elevated);
  text-align: left;
  padding: 0.5rem 0.75rem;
  border: 1px solid var(--ui-border);
  font-weight: 600;
}

.doc-content :deep(td) {
  padding: 0.5rem 0.75rem;
  border: 1px solid var(--ui-border);
}

.doc-content :deep(a) {
  color: var(--ui-primary);
  text-decoration: none;
}

.doc-content :deep(a:hover) {
  text-decoration: underline;
}

.doc-content :deep(blockquote) {
  border-left: 3px solid var(--ui-primary);
  padding-left: 1rem;
  margin: 1rem 0;
  color: var(--ui-text-muted);
}

.doc-content :deep(ul),
.doc-content :deep(ol) {
  padding-left: 1.5rem;
  margin: 0.75rem 0;
}

.doc-content :deep(li) {
  margin: 0.25rem 0;
}

.doc-content :deep(hr) {
  border: none;
  border-top: 1px solid var(--ui-border);
  margin: 2rem 0;
}

.doc-content :deep(strong) {
  color: var(--ui-text-highlighted);
}

.doc-content :deep(p) {
  margin: 0.75rem 0;
  line-height: 1.7;
}
</style>
