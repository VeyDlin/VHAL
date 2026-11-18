import { ref, computed, watch } from 'vue';

// The colors come from the palette generated at build time (palette.config.ts); this composable only switches
// between the light and the dark theme.

export type ThemeMode = 'system' | 'light' | 'dark';

const THEME_STORAGE_KEY = 'vhal-theme';
const mode = ref<ThemeMode>('system');
const prefersDark = ref<boolean>(false);
const resolvedMode = computed<'light' | 'dark'>(() => {
	if (mode.value === 'system') {
		return prefersDark.value ? 'dark' : 'light';
	}
	return mode.value;
});
let modeInitialized = false;

function applyModeToDocument(): void {
	if (typeof document === 'undefined') {
		return;
	}
	document.documentElement.classList.toggle('dark', resolvedMode.value === 'dark');
	document.documentElement.style.colorScheme = resolvedMode.value;
}

function initializeMode(): void {
	if (modeInitialized || typeof window === 'undefined') {
		return;
	}
	modeInitialized = true;
	try {
		const storedMode: string | null = window.localStorage.getItem(THEME_STORAGE_KEY);
		if (storedMode === 'system' || storedMode === 'light' || storedMode === 'dark') {
			mode.value = storedMode;
		}
	} catch {
		// Theme controls remain available when storage access is blocked.
	}

	const mediaQuery: MediaQueryList = window.matchMedia('(prefers-color-scheme: dark)');
	prefersDark.value = mediaQuery.matches;
	mediaQuery.addEventListener('change', (event: MediaQueryListEvent) => {
		prefersDark.value = event.matches;
	});
	watch(resolvedMode, applyModeToDocument, { immediate: true });
}

export function useTheme() {
	initializeMode();

	function setMode(value: ThemeMode): void {
		mode.value = value;
		applyModeToDocument();
		try {
			window.localStorage.setItem(THEME_STORAGE_KEY, value);
		} catch {
			// Theme changes remain available when storage access is blocked.
		}
	}

	return {
		mode,
		setMode,
		resolvedMode,
	};
}
