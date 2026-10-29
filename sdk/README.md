# FinnSDK

A FinnOS app is a normal Wayland client. `libfinnui` is the runtime that makes
one small: the display connection, shared-memory buffers, frame callbacks,
input translation, the software canvas, text, icons and widgets.

## Start

```sh
cp -r sdk/template my-app
cd my-app
make
```

`make` uses the installed `finnui` pkg-config file when it exists, and the
`/usr/local` paths otherwise. Install the runtime once:

```sh
sudo make -C src/ui install
```

## The app contract

- One program, one `xdg_toplevel`.
- Metadata in `assets/apps.registry`:
  `id | Name | icon | command | note | source`.
- Installed state in `/var/lib/finnpkg/installed` (one id per line).
- Launch through `finn-session`, never by starting the app from the
  compositor.

The compositor has no app ids and no special cases. A native app and a GTK
app are the same kind of client.

## API

The public header is `finn/ui.hpp`. The shape:

```cpp
class App {
public:
    App(std::string title, int width, int height);
    virtual void draw(Canvas& canvas) = 0;
    virtual void on_close() {}
    void request_frame();
    int run();
protected:
    Rect content_rect() const;
    void set_framed(bool);
    void set_title(std::string);
};
```

`Canvas` draws into a pixel buffer: `clear`, `fill`, `stroke`, `line`, `text`,
`icon`, `app_icon`, `push_clip`/`pop_clip`, and the widgets `button`,
`icon_button`, `app_icon_button`, `toggle`, `slider`, `text_field`,
`list_item`, `list_item_icon`, `app_list_item`. Every widget takes a stable id
and returns true when it was activated.

`input()` exposes the pointer, scroll, keys and committed text for the frame;
`theme()` exposes the tokens; `width()`/`height()` are the surface size.

`LayerApp` is the same runtime for a layer-shell surface. The shell uses it.

## Building without installing

The template's Makefile accepts overrides:

```sh
make FINNUI_CFLAGS=-I../../src/ui \
     FINNUI_LIBS="../../src/ui/libfinnui.a -lwayland-client -lxkbcommon -lfreetype -lm"
```
