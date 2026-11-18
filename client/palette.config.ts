import type { PaletteConfig } from 'vcopalette'


// Site colors. The pins are the exact colors of the dark theme (page, panels, text, accent); every other step and
// every status color is derived from them by vcopalette. Keys run from 0 (white) to 1000 (black).
// Status hues are OKLCH hues.
export const paletteConfig: PaletteConfig = {
  colors: {
    surface: {
      group: 'neutral',
      pins: [
        { key: 0, color: '#FFFFFF' },
        { key: 200, color: '#B1B0B0', name: 'text' },
        { key: 500, color: '#3B3A3A', name: 'base' },
        { key: 800, color: '#212020', name: 'panel' },
        { key: 900, color: '#181818', name: 'page-bg' },
        { key: 1000, color: '#0F0F0F' },
      ],
    },
    primary: {
      pins: [
        { key: 500, color: '#4A9EFF', name: 'accent' },
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
