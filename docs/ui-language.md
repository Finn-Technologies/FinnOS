# The FinnUI language

`.fui` is the source language of the FinnOS interface. A `.fui` file
describes a screen: what is on it, how it adapts, and which tokens it uses.
`fuic` compiles it to a `.fbin`, the binary form the system loads.

The language is small on purpose. It describes *what the interface is*, and
never *how the machine draws it* — that is the compositor's business, and
the shell surfaces are drawn by hand in C for exactly that reason.

## A screen

```fui
screen Settings {
  Column(padding:20, gap:12) {
    Text("Settings", style:token.title, ink:token.ink)
    Row { Text("Theme"); Segmented(bind: system.theme, options:[Light,Dark]) }
  }
}
```

A screen has a name, a target, and a body. The body is a tree of elements
with properties in parentheses and braces around a body.

## Tokens

`ui/_tokens.fui` holds the theme: the colours, the radii, the type scale,
the motion.

```fui
theme FinnTokens {
  color: bone_light #F6F1E7, bone_dark #23262E
  color: ink_light #1A1D24, ink_dark #F4F0E8
  radius: r_panel 20.8
  type: label 12/16 "Fixel Display" 400
  motion: surface_open 280ms out_back
}
```

A colour is one name with two halves, the light one and the dark one,
because the appearance switch is a property of the whole theme and never
of a single surface. `bone_light` and `bone_dark` are one token, `bone`,
and a screen that refers to `token.bone` gets both.

Everything else is declared once. The compositor carries the same palette
in `src/compositor/finndraw.c` and the same timings in
`src/compositor/finnmotion.h`; the C is the truth and this file is kept in
step with it by hand, which is a debt the compiler will eventually pay.

## Targets

`ui/_targets.fui` holds the adaptive targets. A screen says which target it
is for, and what it becomes on the others.

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

There are four today: **Desktop**, **Mobile**, **MobileXL** and **TV**.
They differ in viewport, density, type scale, input and navigation.

The type scale is the important one: on a TV the same body text is 32sp,
because a television is read from across a room and a phone is read from
thirty centimetres away. That is the same reasoning as a dock that is
sized in points rather than in pixels.

## The shell

`ui/Desktop.fui` is the desktop itself: the wallpaper, the window area and
the dock. It is the reference the compositor's shell surfaces were drawn
from — the dock, the Control Center, the clock page and the Start Menu all
follow it, and all four live in `src/compositor` as cached layers.

`ui/StartMenu.fui` is the Start Menu: the centred card, the app grid, the
app's own state, and the power row.

## The apps

`apps/<id>/<Name>.fui` is one first-party app. The names follow
`NAMING.md`: a brand and a plain English noun — **Finn Code**, **Finn
Files**, **Finn Store**.

An app is not yet a running program. There is no window manager and no
compositor client protocol, so a `.fui` file is a design that the compiler
turns into a `.fbin` and the system does not yet draw. The application
registry (`assets/apps.registry`) is what the Start Menu shows, and it
carries the honest state of each app: installed or not.

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

A statement ends at the end of the line, or at a `;`. A newline inside
brackets or parentheses is only whitespace, which is what lets a property
be a phrase — `focus_ring: 2px accent when keyboard_nav` — without the
grammar having to know about every kind of phrase there is. A property's
value may also run onto the next line when it ends with a comma.

The operators that matter:

| | |
|---|---|
| `+` | composition: `token.wallpaper_desktop + token.wallpaper_flute` |
| `->` | a mapping: `adapt(Desktop:5, Mobile:3)` sets a value per target |
| `@` | an attenuation: `token.ink @ 0.10` is the ink at ten percent |
| `:` | a named argument, or a property's value |
| `,` | a list |

A call is an element with arguments: `Row(gap: 24px) { ... }`. Its
arguments may be named or positional, and a positional one is the
element's own content — `Text("Settings")` is a text element whose text
is "Settings".

## Compiling

```sh
sdk/bin/fuic compile ui/StartMenu.fui -o StartMenu.fbin
sdk/bin/fuic check   ui/StartMenu.fui     # parse it and report
sdk/bin/fuic dump    ui/StartMenu.fui     # print the tree
sdk/bin/fuic new     notes                # an app from the template
```

The compiler is part of [FinnSDK](../sdk/README.md). It is a Python
program with no dependencies, and the format it writes is specified byte
for byte in [FORMAT.md](../sdk/findsdk/fuic/FORMAT.md).
