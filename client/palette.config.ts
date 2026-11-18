import type { PaletteConfig } from 'vcopalette'


// Site colors. Keys run from 0 (white) to 1000 (black) and each pin sits on the key that matches its lightness, so
// the steps Nuxt UI reads for each theme stay readable (light: 50-200 surfaces, 400-700 text; dark: 800-900
// surfaces, 200-500 text). Pinned: the light surfaces, the dark page and panels, the base gray and the accent; every
// other step and every status color is derived by vcopalette. Status hues are OKLCH hues.
export const paletteConfig: PaletteConfig = {
  colors: {
    surface: {
      group: 'neutral',
      pins: [
        { key: 0, color: '#FFFFFF' },
        { key: 50, color: '#FAFAFA', name: 'light-muted' },
        { key: 100, color: '#F4F4F4', name: 'light-panel' },
        { key: 200, color: '#E4E4E4', name: 'light-border' },
        { key: 700, color: '#3B3A3A', name: 'base' },
        { key: 800, color: '#212020', name: 'panel' },
        { key: 900, color: '#181818', name: 'page-bg' },
        { key: 1000, color: '#0F0F0F' },
      ],
    },
    primary: {
      pins: [
        { key: 400, color: '#4A9EFF', name: 'accent' },
      ],
    },
    red: { hue: 25 },
    rose: { hue: 16 },
    pink: { hue: 354 },
    fuchsia: { hue: 328 },
    purple: { hue: 316 },
    violet: { hue: 302 },
    indigo: { hue: 273 },
    blue: { hue: 262 },
    sky: { hue: 234 },
    cyan: { hue: 214 },
    teal: { hue: 187 },
    emerald: { hue: 166 },
    green: { hue: 143 },
    lime: { hue: 130 },
    yellow: { hue: 104 },
    amber: { hue: 81 },
    orange: { hue: 53 },
  },
}


// Nuxt UI aliases: which palette color plays which role
export const paletteAliases: { primary: string; neutral: string } = {
  primary: 'primary',
  neutral: 'surface',
}
