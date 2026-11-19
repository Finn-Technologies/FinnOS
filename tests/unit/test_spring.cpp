#include "finn/spring.hpp"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

using namespace finn::ui;

void test_spring_convergence() {
    const SpringParams presets[] = {
        springs::kSnappy,
        springs::kSmooth,
        springs::kBouncy,
        springs::kGentle,
        springs::kOverscroll,
        SpringParams{140.0, 34.0, 1.0},  // overdamped
    };

    for (const auto& preset : presets) {
        SpringAnimator anim(preset, 0.0);
        anim.set_reduced_motion_override(false);
        anim.retarget(240.0);
        for (int i = 0; i < 480; ++i) {
            anim.step(1.0 / 120.0);
        }
        assert(anim.settled());
        assert(std::fabs(anim.value() - 240.0) < 1e-6);
        assert(std::fabs(anim.velocity()) < 1e-6);

        anim.set(100.0, -500.0);
        anim.retarget(-40.0);
        for (int i = 0; i < 360; ++i) {
            anim.step(1.0 / 60.0);
        }
        assert(anim.settled());
        assert(std::fabs(anim.value() - (-40.0)) < 1e-6);
    }

    SpringAnimator2D anim2d(springs::kSnappy);
    anim2d.set_reduced_motion_override(false);
    anim2d.set(10.0, 20.0);
    anim2d.retarget(310.0, -120.0);
    for (int i = 0; i < 180; ++i) {
        anim2d.step(1.0 / 120.0);
    }
    assert(anim2d.settled());
    assert(std::fabs(anim2d.x.value() - 310.0) < 1e-6);
    assert(std::fabs(anim2d.y.value() - (-120.0)) < 1e-6);
}

void test_critical_damping_no_overshoot() {
    const SpringParams critical_presets[] = {
        springs::kSnappy,
        springs::kSmooth,
        springs::kGentle,
        SpringParams::critical(400.0, 1.5),
    };

    for (const auto& preset : critical_presets) {
        assert(preset.is_critically_damped());
        SpringAnimator anim(preset, 0.0);
        anim.set_reduced_motion_override(false);
        anim.retarget(1.0);
        double prev = anim.value();
        for (int i = 0; i < 240; ++i) {
            const double cur = anim.step(1.0 / 120.0);
            assert(cur >= prev - 1e-12);
            assert(cur <= 1.0 + 1e-12);
            prev = cur;
        }
        assert(anim.settled());

        // Reverse direction: 1.0 -> 0.0 must also stay strictly monotonic
        anim.retarget(0.0);
        prev = anim.value();
        for (int i = 0; i < 240; ++i) {
            const double cur = anim.step(1.0 / 120.0);
            assert(cur <= prev + 1e-12);
            assert(cur >= -1e-12);
            prev = cur;
        }
        assert(anim.settled());
    }

    // Contrast: underdamped kBouncy is expected to overshoot > 1.0 slightly
    assert(springs::kBouncy.is_underdamped());
    SpringAnimator bouncy(springs::kBouncy, 0.0);
    bouncy.set_reduced_motion_override(false);
    bouncy.retarget(1.0);
    double peak = 0.0;
    for (int i = 0; i < 180; ++i) {
        peak = std::max(peak, bouncy.step(1.0 / 120.0));
    }
    assert(peak > 1.01 && peak < 1.15);
    assert(bouncy.settled());
}

void test_velocity_carried_across_retarget() {
    SpringAnimator anim(springs::kSmooth, 0.0);
    anim.set_reduced_motion_override(false);
    anim.retarget(200.0);

    // Advance until moving rapidly toward +200
    for (int i = 0; i < 6; ++i) {
        anim.step(1.0 / 120.0);
    }
    const double pos_before = anim.value();
    const double vel_before = anim.velocity();
    assert(pos_before > 0.0 && pos_before < 180.0);
    assert(vel_before > 200.0);

    // Mid-flight retarget back toward 10.0: position and velocity must persist
    anim.retarget(10.0);
    assert(anim.value() == pos_before);
    assert(anim.velocity() == vel_before);

    // Next tiny step continues briefly in +velocity direction (C1 continuity)
    // rather than snapping backward immediately, then reverses to 10.0.
    const double next_pos = anim.step(0.002);
    assert(next_pos > pos_before);

    for (int i = 0; i < 240; ++i) {
        anim.step(1.0 / 120.0);
    }
    assert(anim.settled());
    assert(std::fabs(anim.value() - 10.0) < 1e-6);
}

void test_rubber_band_monotonic_and_bounded() {
    constexpr double kDim = 600.0;
    assert(rubber_band(0.0, kDim) == 0.0);
    assert(rubber_band(100.0, 0.0) == 0.0);
    assert(rubber_band(100.0, kDim, 0.0) == 0.0);

    double prev = 0.0;
    const double offsets[] = {1.0, 5.0, 20.0, 80.0, 250.0, 1000.0, 1e4, 1e7};
    for (double x : offsets) {
        const double y = rubber_band(x, kDim);
        assert(y > prev);
        assert(y < kDim);
        assert(std::fabs(rubber_band(-x, kDim) + y) < 1e-12);
        if (x < 1e5) {
            const double inv = rubber_band_inverse(y, kDim);
            assert(std::fabs(inv - x) < 1e-7 * x + 1e-9);
        }
        prev = y;
    }

    assert(rubber_band_clamp(50.0, 0.0, 100.0, kDim) == 50.0);
    assert(rubber_band_clamp(-50.0, 0.0, 100.0, kDim) < 0.0);
    assert(rubber_band_clamp(-1e6, 0.0, 100.0, kDim) > -kDim);
    assert(rubber_band_clamp(250.0, 0.0, 100.0, kDim) > 100.0);
    assert(rubber_band_clamp(1e6, 0.0, 100.0, kDim) < 100.0 + kDim);
    assert(rubber_band_for_motion(120.0, kDim, true) == 0.0);
}

void test_elastic_press_feedback() {
    assert(press_down_scale(0.0, 0.96) == 1.0);
    assert(std::fabs(press_down_scale(1.0, 0.96) - 0.96) < 1e-9);
    assert(press_down_scale(0.5, 0.96) < press_down_scale(0.2, 0.96));

    assert(elastic_release_curve(0.0) == 0.0);
    assert(elastic_release_curve(1.0) == 1.0);
    assert(elastic_release_curve(0.4) > 1.0);
    assert(elastic_press_scale(0.0, 0.96) == 0.96);
    assert(elastic_press_scale(1.0, 0.96) == 1.0);

    PressFeedback press(0.96);
    press.set_reduced_motion_override(false);
    press.set_pressed(true);
    for (int i = 0; i < 60; ++i) {
        const double s = press.step(1.0 / 120.0);
        assert(s >= 0.96 - 1e-9 && s <= 1.0 + 1e-9);
    }
    assert(std::fabs(press.scale() - 0.96) < 1e-5);

    press.set_pressed(false);
    double max_scale = 0.0;
    for (int i = 0; i < 120; ++i) {
        max_scale = std::max(max_scale, press.step(1.0 / 120.0));
    }
    assert(max_scale > 1.0);
    assert(press.settled());
    assert(std::fabs(press.scale() - 1.0) < 1e-6);
}

void test_gesture_tracking_and_handoff() {
    GestureProgressTracker tracker(0.0, 400.0, 400.0);
    tracker.set_reduced_motion_override(false);
    tracker.begin(0.0);

    // Drag partway with strong upward velocity
    for (int i = 0; i < 10; ++i) {
        tracker.update_delta(14.0, 1.0 / 120.0);  // ~1680 units/s
    }
    assert(tracker.dragging());
    assert(std::fabs(tracker.position() - 140.0) < 1e-6);
    assert(tracker.velocity() > 1400.0);
    assert(tracker.progress() > 0.30 && tracker.progress() < 0.40);

    SpringAnimator sheet(springs::kSnappy);
    sheet.set_reduced_motion_override(false);
    const double expected_pos = tracker.position();
    const double expected_vel = tracker.velocity();
    const double chosen = tracker.handoff_projected(sheet, 0.5);
    assert(chosen == 400.0);
    assert(!tracker.dragging());
    assert(std::fabs(sheet.value() - expected_pos) < 1e-9);
    assert(std::fabs(sheet.velocity() - expected_vel) < 1e-9);
    assert(sheet.target() == 400.0);

    for (int i = 0; i < 180; ++i) {
        sheet.step(1.0 / 120.0);
    }
    assert(sheet.settled());
    assert(std::fabs(sheet.value() - 400.0) < 1e-6);

    // Overscroll drag beyond max_val=400 gets rubber-banded < 800
    tracker.begin(400.0);
    tracker.update_delta(1000.0, 0.016);
    assert(tracker.position() > 400.0 && tracker.position() < 800.0);
    tracker.handoff(sheet, 400.0);
    for (int i = 0; i < 180; ++i) {
        sheet.step(1.0 / 120.0);
    }
    assert(std::fabs(sheet.value() - 400.0) < 1e-6);
}

void test_reduced_motion_path() {
    unsetenv("FINN_REDUCED_MOTION");
    clear_reduced_motion_override();

    const std::string tmp_path =
        "/tmp/finn_test_spring_settings_" + std::to_string(::getpid());
    {
        std::ofstream out(tmp_path);
        out << "reduced_motion=1\n";
    }
    setenv("FINN_SETTINGS_PATH", tmp_path.c_str(), 1);
    refresh_reduced_motion();
    assert(reduced_motion() == true);

    SpringAnimator anim(springs::kBouncy, 0.0);
    anim.retarget(100.0);
    assert(anim.settled());
    assert(anim.value() == 100.0);
    assert(anim.velocity() == 0.0);

    PressFeedback press(0.96);
    press.set_pressed(true);
    assert(press.scale() == 0.96);
    press.set_pressed(false);
    assert(press.scale() == 1.0);
    assert(press.settled());

    GestureProgressTracker tracker(0.0, 100.0, 100.0);
    tracker.begin(0.0);
    tracker.update_delta(150.0, 0.016);
    assert(tracker.position() == 100.0);  // no rubber-band under reduced motion
    tracker.handoff(anim, 0.0);
    assert(anim.value() == 0.0 && anim.velocity() == 0.0);

    ReducedCrossfade fade;
    fade.start(0.0, 1.0);
    assert(fade.duration <= 0.10 + 1e-9);
    assert(fade.running());
    fade.step(0.05);
    assert(fade.value > 0.0 && fade.value < 1.0);
    fade.step(0.05);
    assert(!fade.running() && fade.value == 1.0);

    unsetenv("FINN_SETTINGS_PATH");
    std::remove(tmp_path.c_str());
    refresh_reduced_motion();
}

void test_frame_time_recorder_and_benchmark() {
    FrameTimeRecorder rec;
    rec.record_ms(4.0);
    rec.record_ms(8.0);
    rec.record_ms(10.0);
    rec.record_ms(20.0);
    assert(rec.count() == 4);
    assert(rec.min_ms() == 4.0);
    assert(rec.max_ms() == 20.0);
    assert(std::fabs(rec.mean_ms() - 10.5) < 1e-9);
    assert(rec.dropped_120hz() == 2);  // 10.0 and 20.0 > 8.333 ms
    assert(rec.dropped_60hz() == 1);   // 20.0 > 16.667 ms

    // Pure-C++ solver microbenchmark (host CPU cost per step, not device fps)
    constexpr int kSteps = 100000;
    SpringAnimator bench(springs::kBouncy, 0.0);
    bench.set_reduced_motion_override(false);
    double sink = 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kSteps; ++i) {
        if ((i & 255) == 0) bench.retarget((i & 256) ? 100.0 : 0.0);
        sink += bench.step(1.0 / 120.0);
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double total_us =
        std::chrono::duration<double, std::micro>(t1 - t0).count();
    const double ns_per_step = (total_us * 1000.0) / static_cast<double>(kSteps);
    assert(std::isfinite(sink));
    assert(ns_per_step < 50000.0);  // well under 50 us/step even under ASan/UBSan
    std::printf("test_spring: ok (solver benchmark: %.1f ns/step over %d steps)\n",
                ns_per_step, kSteps);
}

}  // namespace

int main() {
    test_spring_convergence();
    test_critical_damping_no_overshoot();
    test_velocity_carried_across_retarget();
    test_rubber_band_monotonic_and_bounded();
    test_elastic_press_feedback();
    test_gesture_tracking_and_handoff();
    test_reduced_motion_path();
    test_frame_time_recorder_and_benchmark();
    return 0;
}
