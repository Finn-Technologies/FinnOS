#include <finn/ui.hpp>

#include <string>

class Counter final : public finn::ui::App {
public:
    Counter() : App("Counter", 640, 420) {}

    void draw(finn::ui::Canvas& canvas) override {
        const auto& theme = canvas.theme();
        const auto content = content_rect();
        canvas.fill(content, theme.tile);

        canvas.text(content.x + 28, content.y + 56, "A native FinnOS app", 24,
                    theme.ink);
        canvas.text(content.x + 28, content.y + 84,
                    "One class, one draw method, one registry line.", 12,
                    theme.dim);

        if (canvas.button("count", {content.x + 28, content.y + 120, 220, 46},
                          "Clicked " + std::to_string(count_) + " times")) {
            ++count_;
        }
    }

private:
    int count_ = 0;
};

int main() {
    Counter app;
    return app.run();
}
