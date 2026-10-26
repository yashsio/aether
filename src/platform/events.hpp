#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace aether {

// Neutral key list (translated from xkbcommon in platform/input.cpp so
// the app layer never sees Wayland/XKB types).
enum class Key {
    Unknown,
    M,
    R,
    L,
    S,
    F,
    Zero,
    One,
    ZoomIn,   // + or =
    ZoomOut,  // -
    HelpToggle, // ? or /
    Left,
    Right,
    Up,
    Down,
    Delete,
    Backspace,
    Escape,
};

enum Mods : unsigned {
    MOD_CTRL = 1u << 0,
    MOD_SHIFT = 1u << 1,
    MOD_ALT = 1u << 2,
    MOD_LOGO = 1u << 3,
};

// Input/output interface between the Wayland platform layer and the app.
// All coordinates are logical surface pixels; times are milliseconds
// (compositor clock, wraps at 2^32 - fine for interval math).
struct Events {
    virtual void on_configure(int, int) {}
    virtual void on_close() {}
    virtual void on_pointer_motion(double, double, uint32_t) {}
    virtual void on_pointer_press(uint32_t, double, double) {}
    virtual void on_pointer_release(uint32_t) {}
    // Scroll, Qt convention: positive = wheel rotated away from the user.
    // pixel=true  -> dx/dy are touchpad finger pixels.
    // pixel=false -> dx/dy are wheel units, 120 per detent.
    virtual void on_scroll(bool, bool, double, double) {}
    virtual void on_key(Key, unsigned) {}
    virtual void on_paths_dropped(const std::vector<std::string>&) {}
    // Touchpad pinch (zwp_pointer_gesture_pinch_v1): focal is the surface-
    // local gesture center; scale is cumulative since begin (1.0 at begin).
    virtual void on_pinch_begin(double, double) {}
    virtual void on_pinch_update(double, double, double) {}
    virtual void on_pinch_end() {}
    // Housekeeping timer (e.g. status auto-hide): ms until next deadline,
    // -1 = none pending. on_timer() fires when it elapses.
    virtual int64_t timer_ms() { return -1; }
    virtual void on_timer() {}
    virtual ~Events() = default;
};

// Services the app can ask of the window/system.
struct Host {
    virtual void set_title(const std::string& title) = 0;
    virtual void invalidate() = 0; // schedule a redraw
    virtual void quit() = 0;
    virtual int width() const = 0;  // logical
    virtual int height() const = 0; // logical
    virtual int buffer_scale() const = 0;
    virtual ~Host() = default;
};

} // namespace aether
