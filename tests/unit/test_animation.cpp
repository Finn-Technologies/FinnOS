#include "finn/animation.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>

int main() {
    using namespace finn::ui;

    assert(ease_out_cubic(0.0) == 0.0);
    assert(ease_out_cubic(1.0) == 1.0);
    assert(ease_out_cubic(0.5) > 0.5);
    assert(ease_in_out_cubic(0.0) == 0.0);
    assert(ease_in_out_cubic(1.0) == 1.0);
    assert(std::fabs(ease_in_out_cubic(0.5) - 0.5) < 1e-9);

    Animation animation;
    animation.set(0.0);
    animation.to(1.0, 0.2);
    assert(animation.running());
    animation.update(0.1);
    assert(animation.value > 0.5 && animation.value < 1.0);
    animation.update(0.2);
    assert(animation.value == 1.0);
    assert(!animation.running());

    double value = 0.0;
    for (int i = 0; i < 30; ++i) {
        const double next = approach(value, 1.0, 0.016, kMotionFast);
        assert(next >= value);
        assert(next <= 1.0);
        value = next;
    }
    assert(value > 0.95);

    std::puts("test_animation: ok");
    return 0;
}
