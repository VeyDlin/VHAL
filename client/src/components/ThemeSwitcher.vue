<script setup lang="ts">
import { useId } from 'vue'
import { useTheme, type ThemeMode } from '../composables/useTheme'


interface Props {
  compact?: boolean
}

const props = withDefaults(defineProps<Props>(), { compact: false })
const selectId = useId()
const { mode, setMode } = useTheme()


function onChange(event: Event): void {
  const value: string = (event.target as HTMLSelectElement).value
  if (value === 'system' || value === 'light' || value === 'dark') {
    setMode(value as ThemeMode)
  }
}
</script>

<template>
  <div class="theme-switcher" :class="props.compact ? 'theme-switcher-compact' : ''">
    <label :for="selectId" :class="props.compact ? 'sr-only' : 'theme-switcher-label'">Theme</label>
    <select :id="selectId" class="theme-select" :value="mode" @change="onChange">
      <option value="system">System</option>
      <option value="light">Light</option>
      <option value="dark">Dark</option>
    </select>
  </div>
</template>
