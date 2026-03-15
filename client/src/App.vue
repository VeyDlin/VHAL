<script setup lang="ts">
import { ref } from 'vue'
import AppSidebar from './components/AppSidebar.vue'
import SearchResults from './components/SearchResults.vue'
import { useTheme } from './composables/useTheme'
import { useSearch } from './composables/useSearch'
import { useRoute } from 'vue-router'

useTheme()

const route = useRoute()
const { isSearchActive } = useSearch()
const mobileMenuOpen = ref(false)

// Close mobile menu on navigation
import { watch } from 'vue'
watch(() => route.path, () => {
  mobileMenuOpen.value = false
})
</script>

<template>
  <UApp>
    <div class="flex min-h-screen overflow-x-hidden">
      <!-- Desktop sidebar -->
      <aside class="hidden lg:flex w-72 shrink-0 border-r border-[var(--ui-border)] bg-[var(--ui-bg)] sticky top-0 h-screen overflow-hidden flex-col">
        <div class="flex items-center gap-2 px-4 py-3 border-b border-[var(--ui-border)]">
          <span class="text-lg font-bold text-[var(--ui-text-highlighted)]">VHAL</span>
          <span class="text-xs text-[var(--ui-text-dimmed)]">docs</span>
          <a href="https://github.com/VeyDlin/VHAL" target="_blank" class="ml-auto text-[var(--ui-text-dimmed)] hover:text-[var(--ui-text)] transition-colors">
            <UIcon name="i-lucide-github" class="size-5" />
          </a>
        </div>
        <AppSidebar />
      </aside>

      <!-- Mobile header + slideover -->
      <div class="lg:hidden fixed top-0 left-0 right-0 z-50 flex items-center gap-2 px-4 py-3 border-b border-[var(--ui-border)] bg-[var(--ui-bg)]">
        <UButton
          icon="i-lucide-menu"
          color="neutral"
          variant="ghost"
          size="sm"
          @click="mobileMenuOpen = true"
        />
        <span class="text-lg font-bold text-[var(--ui-text-highlighted)]">VHAL</span>
        <span class="text-xs text-[var(--ui-text-dimmed)]">docs</span>
        <a href="https://github.com/VeyDlin/VHAL" target="_blank" class="ml-auto text-[var(--ui-text-dimmed)] hover:text-[var(--ui-text)] transition-colors">
          <UIcon name="i-lucide-github" class="size-5" />
        </a>
      </div>

      <UDrawer v-model:open="mobileMenuOpen" direction="left" class="lg:hidden">
        <template #content>
          <div class="flex flex-col h-full w-72 max-w-[75vw]">
            <div class="flex items-center gap-2 px-4 py-3 border-b border-[var(--ui-border)]">
              <span class="text-lg font-bold text-[var(--ui-text-highlighted)]">VHAL</span>
              <span class="text-xs text-[var(--ui-text-dimmed)]">docs</span>
              <UButton
                icon="i-lucide-x"
                color="neutral"
                variant="ghost"
                size="sm"
                class="ml-auto"
                @click="mobileMenuOpen = false"
              />
            </div>
            <AppSidebar />
          </div>
        </template>
      </UDrawer>

      <!-- Main content -->
      <main class="flex-1 min-w-0 pt-[53px] lg:pt-0">
        <SearchResults v-if="isSearchActive" />
        <RouterView v-else />
      </main>
    </div>
  </UApp>
</template>
