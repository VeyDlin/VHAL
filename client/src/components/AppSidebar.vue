<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useEventListener } from '@vueuse/core'
import { navigation, flattenNavigation } from '../data/navigation'
import type { DocTreeItem } from '../data/navigation'
import { useSearch } from '../composables/useSearch'

const router = useRouter()
const route = useRoute()
const { query, isSearchActive, matchedPaths } = useSearch()

const selectedItem = ref<DocTreeItem | undefined>()
const expanded = ref<string[]>([])
const treeRef = ref<HTMLElement | null>(null)

/** Filter tree to only include items whose path is in matchedPaths */
function filterByPaths(items: DocTreeItem[], paths: Set<string>): DocTreeItem[] {
  const result: DocTreeItem[] = []
  for (const item of items) {
    if (item.children && item.children.length > 0) {
      const filtered = filterByPaths(item.children, paths)
      if (filtered.length > 0) {
        result.push({ ...item, children: filtered, defaultExpanded: true })
      } else if (item.path && paths.has(item.path)) {
        result.push({ ...item, defaultExpanded: true })
      }
    } else if (item.path && paths.has(item.path)) {
      result.push(item)
    }
  }
  return result
}

const filteredItems = computed(() => {
  if (!isSearchActive.value) return navigation
  return filterByPaths(navigation, matchedPaths.value)
})

const flatItems = flattenNavigation(navigation)

function hasPage(item: DocTreeItem): boolean {
  return !!(item.path && (item.hasReadme || item.hasHeaders))
}

function hasChildren(item: DocTreeItem): boolean {
  return !!(item.children && item.children.length > 0)
}

function getAncestorKeys(docPath: string): string[] {
  const parts = docPath.split('/')
  const keys: string[] = []
  for (let i = 1; i <= parts.length; i++) {
    keys.push(parts.slice(0, i).join('/'))
  }
  return keys
}

// Sync selection and auto-expand ancestors when route changes
watch(() => route.path, (path) => {
  if (path === '/') {
    selectedItem.value = flatItems.find(item => item.path === '__home')
    return
  }

  const prefix = '/docs/'
  if (!path.startsWith(prefix)) {
    selectedItem.value = undefined
    return
  }
  const docPath = path.slice(prefix.length)
  selectedItem.value = flatItems.find(item => item.path === docPath)

  const ancestors = getAncestorKeys(docPath)
  const current = new Set(expanded.value)
  let changed = false
  for (const key of ancestors) {
    if (!current.has(key)) {
      current.add(key)
      changed = true
    }
  }
  if (changed) {
    expanded.value = [...current]
  }
}, { immediate: true })

function onSelect(e: any, item: DocTreeItem) {
  if (!hasPage(item)) {
    e.preventDefault()
    return
  }
  const target = item.path === '__home' ? '/' : '/docs/' + item.path
  router.push(target).then(() => {
    query.value = ''
  })
}

function getTreeItemLabelPath(target: Element): string[] {
  const labels: string[] = []

  const ownLabel = target.querySelector('[data-slot="linkLabel"]')?.textContent?.trim()
  if (ownLabel) labels.unshift(ownLabel)

  let el: Element | null = target.parentElement
  while (el && treeRef.value?.contains(el)) {
    const group = el.parentElement
    if (!group || !treeRef.value?.contains(group)) break
    if (group.getAttribute('role') !== 'group') break

    const parentLi = group.parentElement
    if (!parentLi) break

    const parentButton = parentLi.querySelector(':scope > [role="treeitem"]')
    if (parentButton) {
      const parentLabel = parentButton.querySelector('[data-slot="linkLabel"]')?.textContent?.trim()
      if (parentLabel) labels.unshift(parentLabel)
    }

    el = parentLi
  }

  return labels
}

function findItemByLabels(labels: string[]): DocTreeItem | undefined {
  let items: DocTreeItem[] = filteredItems.value
  let item: DocTreeItem | undefined
  for (const lbl of labels) {
    item = items.find(i => i.label === lbl)
    if (!item) break
    items = item.children || []
  }
  return item
}

const isTouchDevice = 'ontouchstart' in window || navigator.maxTouchPoints > 0

function toggleFolder(item: DocTreeItem) {
  const key = item.path || item.label || ''
  const set = new Set(expanded.value)
  if (set.has(key)) {
    set.delete(key)
  } else {
    set.add(key)
  }
  expanded.value = [...set]
}

function navigateToItem(item: DocTreeItem) {
  if (!hasPage(item)) return
  selectedItem.value = item
  const target = item.path === '__home' ? '/' : '/docs/' + item.path
  router.push(target).then(() => {
    query.value = ''
  })
}

// Intercept clicks in capture phase to prevent UTree from expanding folders on single click.
// Files pass through to UTree normally; folders are handled manually.
useEventListener(treeRef, 'click', (e: MouseEvent) => {
  const target = (e.target as HTMLElement).closest('[role="treeitem"]')
  if (!target) return

  const labels = getTreeItemLabelPath(target)
  if (labels.length === 0) return

  const item = findItemByLabels(labels)
  if (!item || !hasChildren(item)) return // file — let UTree handle it

  // Folder — block UTree's internal toggle
  e.stopPropagation()

  if (isTouchDevice) {
    // Touch: single tap toggles folder and navigates
    toggleFolder(item)
    navigateToItem(item)
  } else {
    // Desktop: single click only navigates
    navigateToItem(item)
  }
}, { capture: true })

// Double-click toggles folder expansion (desktop only)
useEventListener(treeRef, 'dblclick', (e: MouseEvent) => {
  if (isTouchDevice) return

  const target = (e.target as HTMLElement).closest('[role="treeitem"]')
  if (!target) return

  const labels = getTreeItemLabelPath(target)
  if (labels.length === 0) return

  const item = findItemByLabels(labels)
  if (!item || !hasChildren(item)) return

  toggleFolder(item)
})
</script>

<template>
  <nav class="flex flex-col min-h-0 flex-1 overflow-hidden">
    <div class="px-2 pt-3 pb-2">
      <UInput
        v-model="query"
        icon="i-lucide-search"
        placeholder="Search..."
        size="sm"
        class="w-full"
      />
    </div>

    <div ref="treeRef" class="flex-1 min-h-0 overflow-y-auto overscroll-contain px-2 py-2 sidebar-scroll">
      <UTree
        v-model="selectedItem"
        v-model:expanded="expanded"
        :items="filteredItems"
        :get-key="(item: DocTreeItem) => item.path || item.label || ''"
        color="primary"
        size="sm"
        selection-behavior="replace"
        @select="onSelect"
      />

      <div v-if="filteredItems.length === 0" class="px-4 py-8 text-center text-sm text-[var(--ui-text-dimmed)]">
        No results found
      </div>
    </div>
  </nav>
</template>
