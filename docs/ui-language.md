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
    Text("Settings", style:title)
    Row { Text("Theme"); Segmented(bind: system.theme, options:[Light,Dark]) }
    Row { Text("Local AI (opt-in)"); Toggle(bind: settings.ai_enabled, default:false) }
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
  color: surface_light #F7F9FC, surface_dark #0B0E14
  radius: sm 8, md 12, lg 24, pill 999
  font: family "Fixel Display", weights [Regular 400, Medium 500, SemiBold 600, Bold 700]
  type: body 14/20 "Fixel Display" 400, title 20/28 "Fixel Display" 600
  motion: theme_xfade 180ms linear, dock_pop 120ms ease-out
}
```

A token has one name and two values — the light one and the dark one —
because the appearance switch is a property of the whole theme and never
of a single surface. The compositor carries the same palette in
`finndraw.c`, and the two are kept in step by hand, which is a debt the
compiler will eventually pay.

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

`ui/StartMenu.fui` is the Start Menu: the app grid, the app's own state,
and the power row.

## The apps

`apps/<name>/<Name>.fui` is one first-party app. The names follow
`NAMING.md`: a brand and a plain English noun — **Finn Code**, **Finn
Files**, **Finn Store**.

An app is not yet a running program. There is no window manager and no
compositor client protocol, so a `.fui` file is a design that the compiler
turns into a `.fbin` and the system does not yet draw. The application
registry (`assets/apps.registry`) is what the Start Menu shows, and it
carries the honest state of each app: installed or not.

## Compiling

```sh
sdk/fuic/fuic ui/StartMenu.fui -o StartMenu.fbin
```

The compiler is part of [FinnSDK](../sdk/README.md). It is a Python
program with no dependencies, and it is the first piece of the SDK that
exists as more than a plan.
