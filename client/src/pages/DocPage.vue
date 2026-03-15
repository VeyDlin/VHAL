<script setup lang="ts">
import { computed, ref, watch, nextTick, onMounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import type { TabsItem } from '@nuxt/ui'
import ApiReference from '../components/ApiReference.vue'
import { highlightHtmlString } from '../composables/useShiki'
import { useSearch } from '../composables/useSearch'

const route = useRoute()
const router = useRouter()
const { pendingTab } = useSearch()
const docContentRef = ref<HTMLElement | null>(null)
const activeTab = ref<string>('docs')
const processedReadme = ref<string>('')

function scrollToHash() {
  const hash = route.hash
  if (!hash) return
  const el = document.querySelector(hash)
  if (el) {
    el.scrollIntoView({ behavior: 'smooth' })
  }
}

function externalLinksNewTab(container: HTMLElement) {
  container.querySelectorAll<HTMLAnchorElement>('a[href]').forEach(a => {
    const href = a.getAttribute('href') || ''
    if (href.startsWith('http://') || href.startsWith('https://')) {
      a.setAttribute('target', '_blank')
      a.setAttribute('rel', 'noopener noreferrer')
    }
  })
}

const pageModules = import.meta.glob('../generated/pages/*.json', { eager: true }) as Record<string, { default?: any } & Record<string, any>>

interface PageData {
  path: string
  label: string
  readme?: string
  api?: any[]
}

const pageData = computed<PageData | null>(() => {
  const rawSlug = route.params.slug
  if (!rawSlug) return null

  const slugParts = Array.isArray(rawSlug) ? rawSlug : [rawSlug]
  const slug = slugParts.join('--')

  const key = `../generated/pages/${slug}.json`
  const mod = pageModules[key]
  if (!mod) return null

  return (mod.default ?? mod) as PageData
})

const hasReadme = computed(() => !!pageData.value?.readme)
const hasApi = computed(() => pageData.value?.api && pageData.value.api.length > 0)

const tabs = computed<TabsItem[]>(() => {
  const items: TabsItem[] = []
  if (hasReadme.value) {
    items.push({ label: 'Documentation', icon: 'i-lucide-book-open', value: 'docs' })
  }
  if (hasApi.value) {
    items.push({ label: 'API Reference', icon: 'i-lucide-code', value: 'api' })
  }
  return items
})

const showTabs = computed(() => hasReadme.value && hasApi.value)

// Pre-process readme HTML with syntax highlighting (reactive, not DOM-based)
watch(
  () => pageData.value?.readme,
  async (raw) => {
    if (!raw) {
      processedReadme.value = ''
      return
    }
    // Show raw content immediately, then replace with highlighted version
    processedReadme.value = raw
    const highlighted = await highlightHtmlString(raw)
    // Only update if the source hasn't changed while we were highlighting
    if (pageData.value?.readme === raw) {
      processedReadme.value = highlighted
    }
  },
  { immediate: true }
)

// Handle external links after DOM updates
watch(
  [processedReadme, activeTab],
  async () => {
    await nextTick()
    if (docContentRef.value) {
      externalLinksNewTab(docContentRef.value)
    }
  }
)

watch(
  () => route.params.slug,
  async () => {
    if (pendingTab.value) {
      activeTab.value = pendingTab.value
      pendingTab.value = null
    } else {
      activeTab.value = 'docs'
    }
    await nextTick()
    scrollToHash()
  },
  { immediate: true }
)

watch(() => route.hash, () => {
  nextTick(() => scrollToHash())
})

onMounted(() => scrollToHash())
</script>

<template>
  <div class="w-full px-4 sm:px-8 py-6 sm:py-10">
    <template v-if="pageData">
      <UTabs
        v-if="showTabs"
        v-model="activeTab"
        :items="tabs"
        :content="false"
        color="neutral"
        variant="link"
        class="mb-6"
      />

      <div
        v-if="hasReadme && (!showTabs || activeTab === 'docs')"
        ref="docContentRef"
        class="doc-content prose prose-invert max-w-none"
        v-html="processedReadme"
      />

      <ApiReference
        v-if="hasApi && (!showTabs || activeTab === 'api')"
        :api="pageData.api!"
        :page-path="pageData.path"
      />
    </template>

    <template v-else>
      <div class="flex flex-col items-center justify-center py-20 text-center">
        <UIcon name="i-lucide-file-question" class="size-12 text-[var(--ui-text-dimmed)] mb-4" />
        <h2 class="text-xl font-semibold text-[var(--ui-text-highlighted)] mb-2">Page Not Found</h2>
        <p class="text-[var(--ui-text-muted)]">
          The page <code class="text-sm bg-[var(--ui-bg-elevated)] px-1.5 py-0.5 rounded">{{ route.path }}</code> does not exist yet.
        </p>
      </div>
    </template>
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
