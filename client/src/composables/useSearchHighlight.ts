import { computed, nextTick, onBeforeUnmount, watch, type Ref, type ComputedRef } from 'vue'
import { useSearch } from './useSearch'
import { activateSearchHighlight, type SearchHighlight } from '../utils/search-highlight'
import type { SearchTarget } from '../utils/search-target'


export function useSearchHighlight(
  container: Ref<HTMLElement | null>,
  pagePath: Ref<string>,
  ready: Ref<boolean>,
): ComputedRef<SearchTarget | null> {
  const { pendingSelection } = useSearch()
  const selection = computed<SearchTarget | null>(() => (
    pendingSelection.value?.path === pagePath.value ? pendingSelection.value : null
  ))

  watch(pagePath, (path: string) => {
    if (pendingSelection.value && pendingSelection.value.path !== path) {
      pendingSelection.value = null
    }
  })

  watch(selection, (target, previous, onCleanup) => {
    void previous
    if (!target) {
      return
    }
    // A click while syntax highlighting is loading also cancels the pending reveal.
    const dismiss = (): void => {
      if (pendingSelection.value === target) {
        pendingSelection.value = null
      }
    }
    const keydown = (event: KeyboardEvent): void => {
      if (event.key === 'Escape') {
        dismiss()
      }
    }
    document.addEventListener('click', dismiss, true)
    document.addEventListener('keydown', keydown, true)
    onCleanup(() => {
      document.removeEventListener('click', dismiss, true)
      document.removeEventListener('keydown', keydown, true)
    })
  }, { immediate: true })

  watch([container, selection, ready], async ([root, target, isReady], previous, onCleanup) => {
    void previous
    let cancelled: boolean = false
    let highlight: SearchHighlight | null = null
    onCleanup(() => {
      cancelled = true
      highlight?.dispose()
    })
    if (!root || !target || !isReady) {
      return
    }
    await nextTick()
    if (!cancelled && pendingSelection.value === target) {
      highlight = activateSearchHighlight(root, target, () => {
        if (pendingSelection.value === target) {
          pendingSelection.value = null
        }
      })
    }
  }, { flush: 'post', immediate: true })

  onBeforeUnmount(() => {
    if (pendingSelection.value === selection.value) {
      pendingSelection.value = null
    }
  })
  return selection
}
