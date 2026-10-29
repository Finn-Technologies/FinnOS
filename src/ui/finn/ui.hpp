#ifndef FINN_UI_UI_HPP
#define FINN_UI_UI_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

/*
 * libfinnui — the FinnOS app runtime.
 *
 * An app is a normal Wayland client. The runtime owns the display
 * connection, the shared-memory buffers, the xdg-shell or layer-shell
 * surface, the input translation and the software canvas. An app draws in
 * draw() and asks for another frame when it is animating.
 *
 * Coordinates are pixels. The default window frame is drawn by App and the
 * app draws into content_rect().
 */

namespace finn::ui {

struct Color {
    std::uint32_t argb = 0;

    static constexpr Color rgb(std::uint32_t value) {
        return Color{0xFF000000u | (value & 0x00FFFFFFu)};
    }
    static constexpr Color rgba(std::uint32_t value) { return Color{value}; }
    constexpr Color with_alpha(std::uint8_t alpha) const {
        return Color{(argb & 0x00FFFFFFu) | (static_cast<std::uint32_t>(alpha) << 24)};
    }
};

struct Rect {
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;

    bool contains(double px, double py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
    Rect inset(double amount) const {
        return Rect{x + amount, y + amount, w - 2 * amount, h - 2 * amount};
    }
};

struct Theme {
    bool dark = false;
    Color bone;
    Color tile;
    Color tile_on;
    Color hair;
    Color ink;
    Color mute;
    Color dim;
    Color accent;
    Color on;
    Color track;
    Color knob;
    Color glass;

    static Theme light();
    static Theme dark_theme();
};

enum class Key {
    none,
    escape,
    enter,
    tab,
    backspace,
    del,
    left,
    right,
    up,
    down,
    home,
    end,
};

struct KeyEvent {
    Key key = Key::none;
    std::uint32_t sym = 0;
    std::string text;
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
};

struct Input {
    double x = 0;
    double y = 0;
    bool pointer_down = false;
    bool pointer_pressed = false;
    bool pointer_released = false;
    std::uint32_t button = 0;
    double scroll_x = 0;
    double scroll_y = 0;
    bool focused = false;
    std::vector<KeyEvent> keys;
    std::string text;

    bool key(Key wanted) const {
        for (const KeyEvent& event : keys) {
            if (event.key == wanted) return true;
        }
        return false;
    }
};

class Canvas {
public:
    Canvas();
    ~Canvas();
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    void clear(Color color);
    void fill(Rect rect, Color color, double radius = 0);
    void stroke(Rect rect, Color color, double radius = 0, double width = 1);
    void line(double x1, double y1, double x2, double y2, Color color,
              double width = 1);

    void text(double x, double baseline, std::string_view value, double px,
              Color color);
    void text_centered(Rect rect, std::string_view value, double px,
                       Color color);
    double text_width(std::string_view value, double px) const;
    void icon(std::string_view path, Rect rect, Color color);
    void app_icon(std::string_view path, Rect rect);

    void push_clip(Rect rect);
    void pop_clip();

    bool button(std::string_view id, Rect rect, std::string_view label,
                bool enabled = true);
    bool icon_button(std::string_view id, Rect rect, std::string_view icon,
                     Color ink);
    bool app_icon_button(std::string_view id, Rect rect, std::string_view icon);
    bool toggle(std::string_view id, Rect rect, std::string_view label,
                bool& value);
    bool slider(std::string_view id, Rect rect, double& value, double min,
                double max);
    bool text_field(std::string_view id, Rect rect, std::string& value,
                    std::string_view placeholder = {});
    bool list_item(std::string_view id, Rect rect, std::string_view title,
                   std::string_view detail, bool selected = false);
    bool list_item_icon(std::string_view id, Rect rect, std::string_view icon,
                        std::string_view title, std::string_view detail,
                        bool selected = false, bool enabled = true);
    bool app_list_item(std::string_view id, Rect rect, std::string_view icon,
                       std::string_view title, std::string_view detail,
                       bool enabled = true);

    const Input& input() const;
    const Theme& theme() const;
    double width() const;
    double height() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    Canvas(std::uint32_t* pixels, int width, int height, double scale,
           Input& input, const Theme& theme);
    void begin(std::uint32_t* pixels, int width, int height, double scale,
               Input& input, const Theme& theme);
    void end();
    void clear_transient();

    friend class App;
    friend class LayerApp;
    friend class Client;
};

class App {
public:
    App(std::string title, int width, int height);
    virtual ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    virtual void draw(Canvas& canvas) = 0;
    virtual void on_close() {}

    void request_frame();
    int run();

protected:
    Rect content_rect() const;
    void set_framed(bool framed);
    void set_title(std::string title);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

enum Anchor : std::uint32_t {
    kAnchorTop = 1,
    kAnchorBottom = 2,
    kAnchorLeft = 4,
    kAnchorRight = 8,
    kAnchorAll = kAnchorTop | kAnchorBottom | kAnchorLeft | kAnchorRight,
};

enum Layer : std::uint32_t {
    kLayerBackground = 0,
    kLayerBottom = 1,
    kLayerTop = 2,
    kLayerOverlay = 3,
};

enum class KeyboardMode : std::uint32_t {
    none = 0,
    exclusive = 1,
    on_demand = 2,
};

/*
 * A layer-shell surface. The shell uses one full-output overlay and sets an
 * input region covering only the dock and the open menu, so the rest of the
 * screen still belongs to the app underneath.
 */
class LayerApp {
public:
    LayerApp(std::string name, int width, int height, std::uint32_t anchor,
             Layer layer = kLayerOverlay,
             KeyboardMode keyboard = KeyboardMode::on_demand);
    virtual ~LayerApp();
    LayerApp(const LayerApp&) = delete;
    LayerApp& operator=(const LayerApp&) = delete;

    virtual void draw(Canvas& canvas) = 0;
    virtual void on_tick() {}
    virtual void on_close() {}

    void set_tick_ms(int ms);
    void set_input_region(const std::vector<Rect>& rects);
    void set_keyboard_mode(KeyboardMode mode);
    void request_frame();
    int run();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace finn::ui

#endif
