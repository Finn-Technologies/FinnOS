#include <finn/ui.hpp>

#include <string>

namespace {

using finn::ui::App;
using finn::ui::Canvas;

class Hello final : public App {
public:
    Hello() : App("Hello, FinnOS", 760, 520) {}

    void draw(Canvas& canvas) override {
        const auto& theme = canvas.theme();
        const finn::ui::Rect content = content_rect();

        canvas.fill(content, theme.tile, 0);
        canvas.text(content.x + 32, content.y + 58, "A native app in one file", 26,
                    theme.ink);
        canvas.text(content.x + 32, content.y + 86,
                    "Wayland, input, text and widgets come from libfinnui.", 13,
                    theme.dim);

        if (canvas.button("click", {content.x + 32, content.y + 116, 220, 46},
                          "Clicks: " + std::to_string(clicks_))) {
            ++clicks_;
        }

        canvas.text(content.x + 32, content.y + 206, "Name", 13, theme.dim);
        canvas.text_field("name", {content.x + 32, content.y + 220, 380, 44}, name_,
                          "Type your name");
        if (!name_.empty()) {
            canvas.text(content.x + 32, content.y + 314, "Hello, " + name_, 22,
                        theme.ink);
        }
        canvas.toggle("toggle", {content.x + 32, content.y + 344, 280, 38},
                      "A toggle", toggle_);
    }

private:
    int clicks_ = 0;
    std::string name_;
    bool toggle_ = false;
};

}  // namespace

int main() {
    Hello app;
    return app.run();
}
