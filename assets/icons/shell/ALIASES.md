# Figma-name icon aliases

These files let the exact layer names in `FinnOS - Abhi` resolve through
`finn::ui::resolve_icon_path()` without changing any caller. Each is a byte-for-byte
copy of the equivalent FinnOS-authored glyph already shipped here, so an alias
cannot drift visually from the original.

| Figma layer name | Alias | Original glyph |
|---|---|---|
| `minimise` (`21:113`) | `minimise.svg` | `minus-sign.svg` — the same 11.67 px line in a 20 px frame |
| `wifi` (`23:45`) | `wifi.svg` | `wifi-02.svg` |
| `globe` (`25:101`) | `globe.svg` | `internet.svg` — identical 21 px globe paths |
| `cog` (`71:211`) | `cog.svg` | `settings-02.svg` — identical 21 px gear paths |
| `swatch-book` (`71:218`) | `swatch-book.svg` | `paint-brush.svg` — identical three 0.6/0.7/1.0 swatches |
| `sun-dim` (`71:367`) | `sun-dim.svg` | `sun-01.svg` |

`window.cpp` now calls the Figma name `minimise`. App-side names (`wifi-02`,
`internet`, `paint-brush`, `sun-01`, …) keep working through their originals.
