import { ref, computed, watch, type Ref } from 'vue';
import {
	generatePalette,
	generateAllColors,
	toTailwindShades,
	tailwindShades,
	type ThemeConfig,
} from '../utils/color-palettes';
import { config as defaultConfig } from '../utils/theme/app-theme';

const themeConfig = ref<ThemeConfig>({ ...defaultConfig });

const resolvedColors = computed(() => {
	const cfg = themeConfig.value;
	const allColors = generateAllColors(cfg);
	const result: Record<string, Record<number, string>> = {};
	for (const [name, palette] of Object.entries(allColors)) {
		result[name] = toTailwindShades(palette);
	}
	return result;
});

const resolvedPrimary = computed(() => {
	const cfg = themeConfig.value;
	const palette = generatePalette(cfg.primaryColor, cfg.lightColor, cfg.darkColor, cfg.paletteStep);
	return toTailwindShades(palette);
});

const resolvedNeutral = computed(() => {
	const cfg = themeConfig.value;
	const palette = generatePalette(cfg.surfaceColor, cfg.lightColor, cfg.darkColor, cfg.paletteStep);
	return toTailwindShades(palette);
});

const STYLE_ID = 'app-theme-colors';

function applyToCSS() {
	const lines: string[] = [];

	for (const shade of tailwindShades) {
		lines.push(`--color-primary-${shade}: ${resolvedPrimary.value[shade]};`);
	}

	for (const shade of tailwindShades) {
		lines.push(`--color-surface-${shade}: ${resolvedNeutral.value[shade]};`);
	}

	for (const [name, shades] of Object.entries(resolvedColors.value)) {
		for (const shade of tailwindShades) {
			lines.push(`--color-${name}-${shade}: ${shades[shade]};`);
		}
	}

	let el = document.getElementById(STYLE_ID) as HTMLStyleElement | null;
	if (!el) {
		el = document.createElement('style');
		el.id = STYLE_ID;
		document.head.appendChild(el);
	}
	el.textContent = `:root {\n${lines.join('\n')}\n}`;
}

let watchStarted = false;

export function useTheme() {
	if (!watchStarted) {
		watchStarted = true;
		watch([resolvedPrimary, resolvedNeutral, resolvedColors], applyToCSS, { immediate: true });
	}

	function getColor(name: string, shade: number): string {
		if (name === 'primary') return resolvedPrimary.value[shade] ?? '';
		if (name === 'neutral') return resolvedNeutral.value[shade] ?? '';
		return resolvedColors.value[name]?.[shade] ?? '';
	}

	function setPrimaryColor(hex: string) {
		themeConfig.value = { ...themeConfig.value, primaryColor: hex };
	}

	function setNeutralColor(hex: string) {
		themeConfig.value = { ...themeConfig.value, surfaceColor: hex };
	}

	function setThemeConfig(cfg: ThemeConfig) {
		themeConfig.value = { ...cfg };
	}

	return {
		config: themeConfig as Ref<ThemeConfig>,
		primary: resolvedPrimary,
		neutral: resolvedNeutral,
		colors: resolvedColors,
		getColor,
		setPrimaryColor,
		setNeutralColor,
		setThemeConfig,
	};
}
