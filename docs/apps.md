# Writing a native app

A FinnOS app is a program that connects to the compositor and creates an
`xdg_toplevel`. `libfinnui` hides the connection, the buffers, the frame
callbacks and the input; you write `draw`.

## The smallest app

```cpp
#include <finn/ui.hpp>

class Hello final : public finn::ui::App {
public:
    Hello() : App("Hello", 640, 420) {}

    void draw(finn::ui::Canvas& canvas) override {
        const auto& theme = canvas.theme();
        const auto content = content_rect();
        canvas.fill(content, theme.tile);
        canvas.text(content.x + 24, content.y + 56, "Hello, FinnOS", 24, theme.ink);
    }
};

int main() {
    Hello app;
    return app.run();
}
```

`App` draws a 44 pixel frame with the title and a close button. `content_rect()`
is the area under it. Apps that want the whole surface call
`set_framed(false)` and draw everything themselves.

## Build

Install `libfinnui` (`sudo make -C src/ui install`), then:

```make
CXX ?= c++
CXXFLAGS = -std=c++20 -O2 -Wall -Wextra -I/usr/local/include
LDLIBS = /usr/local/lib/libfinnui.a -lwayland-client -lxkbcommon -lfreetype -lm

finn-hello: main.cpp
	$(CXX) $(CXXFLAGS) -o $@ main.cpp $(LDLIBS)
```

The full template is [sdk/template](../sdk/template). It builds with the same
flags on the dev VM.

## Register it

Add one line to `/usr/share/finn/apps.registry`:

```
hello | Hello | app-grid.svg | /usr/local/bin/finn-hello | A native app
```

Mark it installed in `/var/lib/finn/installed` while there is no package
manager:

```
hello
```

Start it with `finn-session --launch hello` or from the shell's app menu.
`finn-session` checks that the command is executable and native before it
spawns it.

## The API

| Type | Use |
|---|---|
| `ui::App` | a window with an xdg-shell toplevel |
| `ui::LayerApp` | a layer-shell surface (the shell uses this) |
| `ui::Canvas` | drawing and widgets |
| `ui::Theme` | the tokens, light or dark |
| `ui::Rect`, `ui::Color` | geometry and colour |
| `ui::Input` | pointer position, buttons, scroll, keys and text |

Canvas:

| Call | What it does |
|---|---|
| `clear`, `fill`, `stroke`, `line` | shapes, with a radius for rounded corners |
| `glass` | backdrop blur with tint and corner radius |
| `text`, `text_centered`, `text_width` | Fixel Display text |
| `icon`, `app_icon` | monochrome or full-colour SVG |
| `button`, `icon_button`, `app_icon_button` | returns true on a click |
| `toggle`, `slider`, `text_field` | form controls |
| `list_item`, `list_item_icon`, `app_list_item` | rows |
| `push_clip`, `pop_clip` | clipping |
| `input()`, `theme()`, `width()`, `height()`, `delta_time()` | the frame's context |
| `animating()` | true while a widget still has motion to finish |

Every widget takes a stable string id. The canvas keeps hover and pressed
state per id, so a widget keeps its state across frames without a class for
each control.

## What an app may not do

- It may not talk to the compositor's internal sockets. It is a normal
  Wayland client and nothing else.
- It may not expect the compositor to know its name. The compositor does not
  contain app ids; the registry does.
- It may not draw outside its surface or assume a position. The window manager
  owns placement.
- It may not claim a capability it does not have. A disabled button is drawn
  disabled and refuses presses.

## Text and images

`FINN_FONT` overrides the font path; the default is
`/usr/share/fonts/OTF/FixelDisplay-Regular.otf`. `FINN_ICON_DIR` overrides the
icon directory; the default is `/usr/share/finn/icons`. A missing font or icon
is reported and left empty rather than replaced by a plausible-looking
substitute.

## Theme changes

`Canvas::theme()` is refreshed every few frames from `$HOME/.config/finn/settings`
(`dark=1`) and `/var/lib/finn/settings`. The Settings app writes the user file;
a new frame of every app picks it up.
