<script setup lang="ts">
import AppSidebar from './components/AppSidebar.vue'
import SearchResults from './components/SearchResults.vue'
import { useTheme } from './composables/useTheme'
import { useSearch } from './composables/useSearch'

useTheme()

const { isSearchActive } = useSearch()
</script>

<template>
  <UApp>
    <div class="flex min-h-screen">
      <!-- Sidebar -->
      <aside class="w-72 shrink-0 border-r border-[var(--ui-border)] bg-[var(--ui-bg)] sticky top-0 h-screen overflow-hidden flex flex-col">
        <div class="flex items-center gap-2 px-4 py-3 border-b border-[var(--ui-border)]">
          <span class="text-lg font-bold text-[var(--ui-text-highlighted)]">VHAL</span>
          <span class="text-xs text-[var(--ui-text-dimmed)]">docs</span>
          <a href="https://github.com/VeyDlin/VHAL" target="_blank" class="ml-auto text-[var(--ui-text-dimmed)] hover:text-[var(--ui-text)] transition-colors">
            <UIcon name="i-lucide-github" class="size-5" />
          </a>
        </div>
        <AppSidebar />
      </aside>

      <!-- Main content -->
      <main class="flex-1 min-w-0">
        <SearchResults v-if="isSearchActive" />
        <RouterView v-else />
      </main>
    </div>
  </UApp>
</template>
