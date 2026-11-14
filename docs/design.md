# Design source

The design lives in the Figma file **FinnOS - Abhi**, across **Components**,
**First Time Setup** (`36:2`), and the desktop/app pages.
The frames used by the implementation include:

| Frame | Node | What it fixes |
|---|---|---|
| Desktop | `186:4253` | wallpaper, dock, status, window |
| Empty Window | `186:4039` | window chrome and the empty state |
| Desktop - Control Center | `218:24` | Control Center panel |
| Desktop - Home | `186:4172` | Home / All Apps menu |
| Settings | `186:3863` / `20:64` | Settings navigation, tiles, and About |
| First Time Setup - `hi` | `36:3` | `#F5F5F5` OOBE canvas, top `Branding` (`36:45`), cursive `"hi."` (`36:37`), `56×56` Next circle button (`36:48`) |
| First Time Setup - `How do we talk?` | `36:53` | split prompt (`x=225, y=408`) + `350×648` `Languages` list (`37:96`) with progressive bottom blur (`y=0.607..1.0`) and `132×56` Back/Next controls (`36:70`) |
| Boot | `186:4294` | the bootstrap mark and `FinnRibbonAnimation` boot splash |

## Measurements taken from the file

- Dock and status pills: 42 px high, 8 px radius, translucent `glass` surface,
  task icons 26 px, balanced dock/status geometry.
- Window: 8 px radius, tokenized `bone`/`tile` canvas, crisp 1 px `hair`
  border; `FinnWindow` draws the `83x24` `#F9F9F9` / dark-mode equivalent
  window controls pill (Minimize, Maximize/Restore, Close).
- Typography: Fixel Display for UI and display sizes, bundled monospace for code
  and terminal views.
- Panels:
  - Home / App Menu (`186:4172` & `218:107`): glass panel with favorites,
    All Apps grid/list, account identity (`getpwuid`), and quick launch.
  - Control Center (`218:24`): appearance toggle (Light / Dark) plus honest
    NetworkManager, BlueZ, PipeWire audio, and UPower power/battery tiles.
  - Spotlight Search: instant filtering across installed apps, XDG `.desktop`
    entries, Wine prefixes, files, and arithmetic expression evaluation.

All measurements and color tokens derive from `ui/_tokens.fui` and
`assets/figma/SPEC.md`. See [docs/motion.md](motion.md) for animation timings.

![The apps menu over the wallpaper](images/app-menu.png)

## Figwright

When inspecting Figma locally with Figwright (`127.0.0.1:3055`),
`list_files` / `use_file` / `get_design_context` / `get_screenshot` can pull
updated Figma tokens and layouts into `ui/_tokens.fui`.
