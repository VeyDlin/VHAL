export function hexToRgb(hex: string): { r: number; g: number; b: number } {
	return {
		r: parseInt(hex.slice(1, 3), 16),
		g: parseInt(hex.slice(3, 5), 16),
		b: parseInt(hex.slice(5, 7), 16),
	};
}

export function rgbToHex(r: number, g: number, b: number): string {
	return (
		'#' +
		[r, g, b]
			.map((x) => Math.round(x).toString(16).padStart(2, '0'))
			.join('')
			.toUpperCase()
	);
}

export function rgbToHsl(r: number, g: number, b: number): { h: number; s: number; l: number } {
	r /= 255;
	g /= 255;
	b /= 255;
	const max = Math.max(r, g, b),
		min = Math.min(r, g, b);
	let h = 0,
		s = 0;
	const l = (max + min) / 2;

	if (max !== min) {
		const d = max - min;
		s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
		switch (max) {
			case r:
				h = ((g - b) / d + (g < b ? 6 : 0)) / 6;
				break;
			case g:
				h = ((b - r) / d + 2) / 6;
				break;
			case b:
				h = ((r - g) / d + 4) / 6;
				break;
		}
	}
	return { h: h * 360, s: s * 100, l: l * 100 };
}

export function hslToRgb(h: number, s: number, l: number): { r: number; g: number; b: number } {
	h /= 360;
	s /= 100;
	l /= 100;
	let r, g, b;

	if (s === 0) {
		r = g = b = l;
	} else {
		const hue2rgb = (p: number, q: number, t: number) => {
			if (t < 0) t += 1;
			if (t > 1) t -= 1;
			if (t < 1 / 6) return p + (q - p) * 6 * t;
			if (t < 1 / 2) return q;
			if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6;
			return p;
		};
		const q = l < 0.5 ? l * (1 + s) : l + s - l * s;
		const p = 2 * l - q;
		r = hue2rgb(p, q, h + 1 / 3);
		g = hue2rgb(p, q, h);
		b = hue2rgb(p, q, h - 1 / 3);
	}
	return { r: r * 255, g: g * 255, b: b * 255 };
}

export function hexToHsl(hex: string): { h: number; s: number; l: number } {
	const { r, g, b } = hexToRgb(hex);
	return rgbToHsl(r, g, b);
}

export function hslToHex(h: number, s: number, l: number): string {
	const { r, g, b } = hslToRgb(h, s, l);
	return rgbToHex(r, g, b);
}

export function shiftHue(hex: string, targetHue: number): string {
	const { s, l } = hexToHsl(hex);
	return hslToHex(targetHue, s, l);
}

export interface ThemeConfig {
	primaryColor: string;
	lightColor: string;
	darkColor: string;
	surfaceColor: string;
	paletteStep: number;
	colorHues: Record<string, number>;
}

export function generatePalette(
	primaryHex: string,
	lightHex: string,
	darkHex: string,
	step: number
): Record<number, string> {
	const primary = hexToRgb(primaryHex);
	const light = hexToRgb(lightHex);
	const dark = hexToRgb(darkHex);
	const palette: Record<number, string> = {};

	for (let i = 0; i <= 1000; i += step) {
		if (i === 0) {
			palette[i] = lightHex;
		} else if (i === 500) {
			palette[i] = primaryHex;
		} else if (i === 1000) {
			palette[i] = darkHex;
		} else {
			const [fg, bg, alpha] = i < 500 ? [primary, light, i / 500] : [primary, dark, (1000 - i) / 500];

			palette[i] = rgbToHex(
				fg.r * alpha + bg.r * (1 - alpha),
				fg.g * alpha + bg.g * (1 - alpha),
				fg.b * alpha + bg.b * (1 - alpha)
			);
		}
	}
	return palette;
}

export function generateAllColors(cfg: ThemeConfig): Record<string, Record<number, string>> {
	const colors: Record<string, Record<number, string>> = {};
	for (const [name, hue] of Object.entries(cfg.colorHues)) {
		const shiftedBase = shiftHue(cfg.primaryColor, hue);
		colors[name] = generatePalette(shiftedBase, cfg.lightColor, cfg.darkColor, cfg.paletteStep);
	}
	return colors;
}

/** Tailwind shade scale */
export const tailwindShades = [50, 100, 200, 300, 400, 500, 600, 700, 800, 900, 950] as const;

export function toTailwindShades(palette: Record<number, string>): Record<number, string> {
	const result: Record<number, string> = {};
	for (const shade of tailwindShades) {
		result[shade] = palette[shade] ?? palette[500];
	}
	return result;
}
