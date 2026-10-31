// Background wallpaper daemon rendering on the background layer-shell surface.
// Draws the default WebP wallpaper or theme background color, dimming the canvas
// when dark appearance is active and refusing all input events.

#include <finn/ui.hpp>

#include <cstdio>

namespace {

class Wallpaper final : public finn::ui::LayerApp {
public:
    Wallpaper()
        : LayerApp("finn-wallpaper", 0, 0, finn::ui::kAnchorAll,
                   finn::ui::kLayerBackground,
                   finn::ui::KeyboardMode::none) {
        set_tick_ms(1000);
        set_input_region({});
    }

    // Renders the background image to cover the entire output, applying a semi-transparent
    // dark overlay when dark theme is enabled to preserve contrast for desktop elements.
    void draw(finn::ui::Canvas& canvas) override {
        const auto& theme = canvas.theme();
        const finn::ui::Rect screen{0, 0, canvas.width(), canvas.height()};
        canvas.clear(finn::ui::Color::rgba(0));

        if (!canvas.image("/usr/share/finn/wallpaper/default.webp", screen, true)) {
            canvas.fill(screen, theme.bone);
            if (!warned_) {
                std::fprintf(stderr,
                             "finn-wallpaper: /usr/share/finn/wallpaper/default.webp "
                             "is missing; using the theme colour\n");
                warned_ = true;
            }
        }
        if (theme.dark) {
            canvas.fill(screen, finn::ui::Color::rgba(0x66000000));
        }
    }

private:
    bool warned_ = false;
};

}  // namespace

int main() {
    Wallpaper wallpaper;
    return wallpaper.run();
}
