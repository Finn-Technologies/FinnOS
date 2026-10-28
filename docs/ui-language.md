# The FinnUI language

`.fui` is the source language of the FinnOS interface. A `.fui` file describes a
screen: what is on it, how it adapts, and which tokens it uses. `fuic` compiles
it to a `.fbin`, the binary form the system reads.

The language is small on purpose. It describes *what the interface is*, and
never *how the machine draws it* — that is the compositor's business, and the
shell surfaces are drawn by hand in C for exactly that reason.

**Status.** `fuic` and the `.fbin` format are real and tested. The compositor
does not render a `.fbin` into a window yet: the first-party apps that work are
drawn in C inside `finn-comp`, and the `.fui` sources are the design language
and the target for the native renderer. External Linux apps can open a window
through the limited Wayland host; a `.fui` app cannot, yet.

## A screen

```fui
screen Settings {
  Column(padding:20, gap:12) {
    Text("Settings", style:token.title, ink:token.ink)
    Row { Text("Theme"); Segmented(bind: system.theme, options:[Light,Dark]) }
  }
}
```

A screen has a name, a target, and a body. The body is a tree of elements with
properties in parentheses and braces around a body.

## Tokens

`ui/_tokens.fui` holds the theme: the colours, the radii, the type scale and the
motion.

```fui
theme FinnTokens {
  color: bone_light #F6F1E7, bone_dark #23262E
  color: ink_light #1A1D24, ink_dark #F4F0E8
  radius: r_panel 20.8
  type: label 12/16 "Fixel Display" 400
  motion: surface_open 280ms out_back
}
```

A colour is one name with two halves, the light one and the dark one, because
the appearance switch is a property of the whole theme and never of a single
surface. `bone_light` and `bone_dark` are one token, `bone`, and a screen that
refers to `token.bone` gets both.

Everything else is declared once. The compositor carries the same palette in
`src/compositor/finndraw.c` and the same timings in
`src/compositor/finnmotion.h`; the C is the truth the shell draws with, and
this file is kept in step with it by hand. That duplication is a debt the
compiler will eventually pay; today, a token change is two changes.

## Targets

`ui/_targets.fui` holds the adaptive targets. A screen says which target it is
for, and what it becomes on the others.

```fui
target Desktop {
  viewport: min 1024x600, base 1920x1080
  density: 1.0x, type_scale: 1.0
  input: pointer + keyboard
  nav: windowed_workspace + floating_dock
  shell: {
    topbar: none,
    dock: bottom_center_floating,
    windows: freeform_tiled
  }
}
```

There are four today: **Desktop**, **Mobile**, **MobileXL** and **TV**. They
differ in viewport, density, type scale, input and navigation.

The type scale is the important one: on a TV the same body text is 32sp, because
a television is read from across a room and a phone is read from thirty
centimetres away. That is the same reasoning as a dock that is sized in points
rather than in pixels.

Only **Desktop** has code behind it. Mobile, MobileXL and TV are specifications:
the compositor's density model is in place, the input model is not.

## The shell

`ui/Desktop.fui` is the desktop itself: the wallpaper, the window area and the
dock. It is the reference the compositor's shell surfaces were drawn from — the
dock, the Control Center, the clock page and the Start Menu all follow it, and
all four live in `src/compositor` as cached layers.

`ui/StartMenu.fui` is the Start Menu: the centred card, the app grid, the app's
own state and the power row. `ui/ControlCenter.fui`, `ui/NotificationCenter.fui`,
`ui/Setup.fui`, `ui/Boot.fui` and `ui/Window.fui` are the same kind of
reference for the other surfaces.

## The apps

`apps/<id>/<Name>.fui` is one first-party app. The names follow `NAMING.md`: a
brand and a plain English noun — **Finn Code**, **Finn Files**, **Finn Store**.

An app is not yet a running `.fui` program. There is a window manager
(`finnwindow`) and a limited xdg-shell/`wl_shm` host for external Linux
clients, but there is no native `.fbin` host: a `.fbin` is a scene graph that
the compositor cannot yet draw into a window. The application registry
(`assets/apps.registry`) is what the Start Menu shows, and it carries the honest
state of each app: installed or not.

The first-party apps that do work — Settings, Finn Files, Finn Store — are drawn
in C in `src/compositor`, from the same design language.

## The grammar

The language is small. In full:

```
file     := item*
item     := 'screen' IDENT '{' body '}'
          | 'theme'  IDENT '{' decl* '}'
          | 'target' IDENT '{' decl* '}'
body     := stmt*
stmt     := property | element
property := IDENT ['(' args ')'] ':' value
element  := IDENT ['(' args ')'] ['{' body '}']
args     := arg (',' arg)*
arg      := IDENT ':' value | value
value    := atom+          -- a phrase, when there is more than one atom
atom     := STRING | NUMBER [unit] | COLOUR | IDENT ('.' IDENT)*
          | '[' value_list ']' | '(' value_list ')' | '{' map '}'
          | atom '->' atom
          | atom '@' atom
```

A statement ends at the end of the line, or at a `;`. A newline inside brackets
or parentheses is only whitespace, which is what lets a property be a phrase —
`focus_ring: 2px accent when keyboard_nav` — without the grammar having to know
about every kind of phrase there is. A property's value may also run onto the
next line when it ends with a comma.

The operators that matter:

| | |
|---|---|
| `+` | composition: `token.wallpaper_desktop + token.wallpaper_flute` |
| `->` | a mapping: `adapt(Desktop:5, Mobile:3)` sets a value per target |
| `@` | an attenuation: `token.ink @ 0.10` is the ink at ten percent |
| `:` | a named argument, or a property's value |
| `,` | a list |

A call is an element with arguments: `Row(gap: 24px) { ... }`. Its arguments
may be named or positional, and a positional one is the element's own content —
`Text("Settings")` is a text element whose text is "Settings".

## Compiling

```sh
sdk/bin/fuic compile ui/StartMenu.fui -o StartMenu.fbin
sdk/bin/fuic check   ui/StartMenu.fui     # parse it and report
sdk/bin/fuic dump    ui/StartMenu.fui     # print the tree
sdk/bin/fuic new     notes                # an app from the template
```

A compile is three steps: parse the source, resolve the theme's tokens into the
tree, and write the binary. The tokens are resolved at compile time on purpose —
a renderer should not have to carry the theme to draw one screen.

The tokens come from the theme the screen is compiled against: `_tokens.fui`
beside the screen if there is one, and otherwise the system theme,
`ui/_tokens.fui`. `--theme` names another file, and `--strict` turns warnings
into an error.

The compiler is part of [FinnSDK](../sdk/README.md). It is a Python program with
no dependencies, and the format it writes is specified byte for byte in
[FORMAT.md](../sdk/finnsdk/fuic/FORMAT.md).

## What the language does not do

- It does not describe behaviour. There are no event handlers, no scripts and no
  data model beyond a `bind:` name; behaviour is C, or it does not exist.
- It does not describe a running client. There is no `.fbin` host and no native
  renderer, as above.
- It does not carry the theme at runtime. Tokens are resolved at compile time.
- It does not implement the non-Desktop targets. They are specifications.
