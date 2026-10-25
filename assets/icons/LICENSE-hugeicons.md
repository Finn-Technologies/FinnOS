# Icons in this directory

The FinnOS shell icons are drawn on the HugeIcons Stroke Rounded grid: a
24×24 box, 1.5px strokes with round caps and joins. A few files are derived
directly from [HugeIcons free core icons](https://github.com/hugeicons/hugeicons)
(`@hugeicons/core-free-icons` 4.3.5), which is MIT licensed:

| File | HugeIcons source |
|---|---|
| `shell/display.svg` | `MonitorIcon` |
| `shell/moon.svg` | `Moon02Icon` |

The rest are the shell's own drawing, authored to the same grid and stroke so
the set reads as one family. `shell/internet.svg`, `shell/paint-brush.svg`
and `shell/info.svg` are exported from the FinnOS Figma file's Settings frame. Every stroke is black: `finndraw` recolours an
icon through its palette, so an icon never carries a colour of its own.

`apps/settings-app.svg` and `apps/files.svg` are app tiles rather than glyphs:
a rounded square with the app's own gradient and a white mark, because an app
icon is the one icon that is allowed to be a picture.

---

MIT License

Copyright (c) 2025 Hugeicons

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
