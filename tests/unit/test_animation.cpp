#include "finn/animation.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

int main() {
    using namespace finn::ui;

    unsetenv("FINN_REDUCED_MOTION");
    clear_reduced_motion_override();

    assert(ease_out_cubic(0.0) == 0.0);
    assert(ease_out_cubic(1.0) == 1.0);
    assert(ease_out_cubic(0.5) > 0.5);
    assert(ease_in_out_cubic(0.0) == 0.0);
    assert(ease_in_out_cubic(1.0) == 1.0);
    assert(std::fabs(ease_in_out_cubic(0.5) - 0.5) < 1e-9);

    // CubicBezier tokens
    assert(kCurveOut.evaluate(0.0) == 0.0);
    assert(kCurveOut.evaluate(1.0) == 1.0);
    assert(std::fabs(kCurveOut.evaluate(0.35) - ease_out_cubic(0.35)) < 1e-4);
    assert(std::fabs(kCurveInOut.evaluate(0.5) - 0.5) < 1e-4);
    assert(kCurveOutBack.evaluate(0.6) > 1.0);
    assert(std::fabs(kCurveOutBack.evaluate(1.0) - 1.0) < 1e-6);

    Animation animation;
    animation.set(0.0);
    animation.to(1.0, 0.2);
    assert(animation.running());
    animation.update(0.1);
    assert(animation.value > 0.5 && animation.value < 1.0);
    animation.update(0.2);
    assert(animation.value == 1.0);
    assert(!animation.running());

    assert(ease_out_back(0.0) == 0.0);
    assert(std::fabs(ease_out_back(1.0) - 1.0) < 1e-9);
    assert(ease_out_back(0.7) > ease_out_cubic(0.7));

    // Staggered grid: first item leads, last item trails, all finish together.
    assert(stagger(0.0, 0, 10) == 0.0);
    assert(stagger(1.0, 9, 10) == 1.0);
    assert(stagger(0.5, 0, 10) > stagger(0.5, 9, 10));
    assert(stagger(0.5, 9, 10) >= 0.0);
    assert(stagger(1.0, 0, 1) == 1.0);

    double value = 0.0;
    for (int i = 0; i < 30; ++i) {
        const double next = approach(value, 1.0, 0.016, kMotionFast);
        assert(next >= value);
        assert(next <= 1.0);
        value = next;
    }
    assert(value > 0.95);

    // Analytic SpringValue convergence
    SpringValue spring;
    spring.set(0.0);
    spring.to(100.0);
    for (int i = 0; i < 120; ++i) {
        spring.step(1.0 / 60.0);
    }
    assert(spring.settled());
    assert(std::fabs(spring.value - 100.0) < 1e-6);

    // Settings-file reduced_motion plumbing
    const std::string tmp_path =
        "/tmp/finn_test_anim_settings_" + std::to_string(::getpid());
    {
        std::ofstream out(tmp_path);
        out << "dark=0\nreduced_motion=1\n";
    }
    setenv("FINN_SETTINGS_PATH", tmp_path.c_str(), 1);
    refresh_reduced_motion();
    assert(read_reduced_motion_setting() == true);
    assert(reduced_motion() == true);

    Animation instant;
    instant.set(0.0);
    instant.to(1.0, kMotionStandard);
    assert(!instant.running());
    assert(instant.value == 1.0);
    assert(approach(0.0, 1.0, 0.016, kMotionFast) == 1.0);

    {
        std::ofstream out(tmp_path);
        out << "dark=0\nreduced_motion=0\n";
    }
    refresh_reduced_motion();
    assert(read_reduced_motion_setting() == false);
    assert(reduced_motion() == false);

    set_reduced_motion_override(true);
    assert(reduced_motion() == true);
    clear_reduced_motion_override();
    assert(reduced_motion() == false);

    setenv("FINN_REDUCED_MOTION", "1", 1);
    assert(reduced_motion() == true);
    unsetenv("FINN_REDUCED_MOTION");
    unsetenv("FINN_SETTINGS_PATH");
    std::remove(tmp_path.c_str());
    refresh_reduced_motion();

    std::puts("test_animation: ok");
    return 0;
}
