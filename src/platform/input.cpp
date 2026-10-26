#include "platform/input.hpp"

#include "platform/connection.hpp"
#include "platform/cursors.hpp"
#include "platform/window.hpp"
#include "pointer-gestures-unstable-v1-client-protocol.h"

#include <xkbcommon/xkbcommon.h>

#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <linux/input-event-codes.h>

namespace aether {

namespace {

// Wayland axis values are in motion coordinates (positive = down/right).
// Qt convention for wheel deltas: positive = away from the user.
// One line to flip if a touchpad pan direction ever feels wrong.
constexpr double kScrollSign = -1.0;

bool is_ctrl_down(xkb_state* state) {
    return state && xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CTRL,
                                                 XKB_STATE_MODS_EFFECTIVE) > 0;
}

} // namespace

Input::Input(Connection& conn, Events& events, Window& window)
    : mConn(conn), mEvents(events), mWindow(window) {
    if (!conn.seat())
        return;
    static const wl_seat_listener listener = {handle_capabilities, handle_name};
    wl_seat_add_listener(conn.seat(), &listener, this);

    mXkbContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    mCursors = std::make_unique<Cursors>(conn, window.buffer_scale());

    // Globals are bound; capabilities may already be pending - a roundtrip
    // makes sure the seat events have been dispatched.
    conn.roundtrip(nullptr);
}

Input::~Input() {
    if (mPinch)
        zwp_pointer_gesture_pinch_v1_destroy(mPinch);
    if (mPointer)
        wl_pointer_destroy(mPointer);
    if (mKeyboard)
        wl_keyboard_destroy(mKeyboard);
    if (mXkbState)
        xkb_state_unref(mXkbState);
    if (mXkbKeymap)
        xkb_keymap_unref(mXkbKeymap);
    if (mXkbContext)
        xkb_context_unref(mXkbContext);
}

void Input::handle_capabilities(void* data, wl_seat*, uint32_t capabilities) {
    auto* self = static_cast<Input*>(data);
    const bool has_pointer = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
    const bool has_keyboard = (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
    if (has_pointer && !self->mPointer)
        self->setup_pointer();
    else if (!has_pointer && self->mPointer) {
        if (self->mPinch) {
            zwp_pointer_gesture_pinch_v1_destroy(self->mPinch);
            self->mPinch = nullptr;
        }
        wl_pointer_destroy(self->mPointer);
        self->mPointer = nullptr;
    }
    if (has_keyboard && !self->mKeyboard)
        self->setup_keyboard();
    else if (!has_keyboard && self->mKeyboard) {
        wl_keyboard_destroy(self->mKeyboard);
        self->mKeyboard = nullptr;
    }
}

void Input::handle_name(void*, wl_seat*, const char*) {}

void Input::setup_pointer() {
    mPointer = wl_seat_get_pointer(mConn.seat());
    mPointerVersion = mConn.seat_version();
    static const wl_pointer_listener listener = {
        handle_pointer_enter,
        handle_pointer_leave,
        handle_pointer_motion,
        handle_pointer_button,
        handle_pointer_axis,
        handle_pointer_frame,
        handle_pointer_axis_source,
        handle_pointer_axis_stop,
        handle_pointer_axis_discrete,
        handle_pointer_axis_value120,
        handle_pointer_axis_relative_direction,
        handle_pointer_warp,
    };
    wl_pointer_add_listener(mPointer, &listener, this);
    mHasPointer = true;

    if (mConn.pointer_gestures()) {
        mPinch = zwp_pointer_gestures_v1_get_pinch_gesture(mConn.pointer_gestures(), mPointer);
        static const zwp_pointer_gesture_pinch_v1_listener pinch_listener = {
            handle_pinch_begin, handle_pinch_update, handle_pinch_end,
        };
        zwp_pointer_gesture_pinch_v1_add_listener(mPinch, &pinch_listener, this);
    }
}

void Input::setup_keyboard() {
    mKeyboard = wl_seat_get_keyboard(mConn.seat());
    static const wl_keyboard_listener listener = {
        handle_keymap,
        handle_keyboard_enter,
        handle_keyboard_leave,
        handle_key,
        handle_keyboard_modifiers,
        handle_repeat_info,
    };
    wl_keyboard_add_listener(mKeyboard, &listener, this);
    mHasKeyboard = true;
}

// --- pointer ---------------------------------------------------------------

void Input::handle_pointer_enter(void* data, wl_pointer* pointer, uint32_t serial,
                                 wl_surface* surface, wl_fixed_t, wl_fixed_t) {
    auto* self = static_cast<Input*>(data);
    if (surface != self->mWindow.surface())
        return;
    self->mPointerOver = true;
    self->mEnterSerial = serial;
    self->mCursors->show(pointer, serial, false, self->mWindow.buffer_scale());
}

void Input::handle_pointer_leave(void* data, wl_pointer*, uint32_t, wl_surface* surface) {
    auto* self = static_cast<Input*>(data);
    if (surface == self->mWindow.surface())
        self->mPointerOver = false;
}

void Input::handle_pointer_motion(void* data, wl_pointer*, uint32_t time, wl_fixed_t sx,
                                  wl_fixed_t sy) {
    auto* self = static_cast<Input*>(data);
    const double x = wl_fixed_to_double(sx);
    const double y = wl_fixed_to_double(sy);
    self->mLastX = x;
    self->mLastY = y;
    self->mEvents.on_pointer_motion(x, y, time);
}

void Input::handle_pointer_button(void* data, wl_pointer* pointer, uint32_t serial,
                                  uint32_t time, uint32_t button, uint32_t state) {
    auto* self = static_cast<Input*>(data);
    if (button != BTN_LEFT)
        return;
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        self->mEvents.on_pointer_press(time, self->mLastX, self->mLastY);
        if (self->mPointerOver)
            self->mCursors->show(pointer, serial, true, self->mWindow.buffer_scale());
    } else {
        self->mEvents.on_pointer_release(time);
        if (self->mPointerOver)
            self->mCursors->show(pointer, serial, false, self->mWindow.buffer_scale());
    }
}

void Input::handle_pointer_axis(void* data, wl_pointer*, uint32_t, uint32_t axis,
                                wl_fixed_t value) {
    auto* self = static_cast<Input*>(data);
    const double v = wl_fixed_to_double(value) * kScrollSign;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
        self->mAccY += v;
    else if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL)
        self->mAccX += v;
    self->mScrollPending = true;
    if (self->mPointerVersion < 5)
        self->deliver_scroll_immediate();
}

void Input::handle_pointer_frame(void* data, wl_pointer*) {
    static_cast<Input*>(data)->flush_scroll();
}

void Input::handle_pointer_axis_source(void*, wl_pointer*, uint32_t) {}
void Input::handle_pointer_axis_stop(void*, wl_pointer*, uint32_t, uint32_t) {}

void Input::handle_pointer_axis_discrete(void* data, wl_pointer*, uint32_t axis,
                                         int32_t discrete) {
    auto* self = static_cast<Input*>(data);
    const int32_t v = static_cast<int32_t>(kScrollSign * discrete);
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
        self->mDiscreteY += v;
    else
        self->mDiscreteX += v;
    self->mHasDiscrete = true;
    self->mScrollPending = true;
}

void Input::handle_pointer_axis_value120(void* data, wl_pointer*, uint32_t axis,
                                         int32_t value120) {
    auto* self = static_cast<Input*>(data);
    const int32_t v = static_cast<int32_t>(kScrollSign * value120);
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
        self->mWheelY += v;
    else
        self->mWheelX += v;
    self->mHasWheel = true;
    self->mScrollPending = true;
}

void Input::handle_pointer_axis_relative_direction(void*, wl_pointer*, uint32_t, uint32_t) {}

void Input::handle_pointer_warp(void*, wl_pointer*, wl_fixed_t, wl_fixed_t) {}

void Input::handle_pinch_begin(void* data, zwp_pointer_gesture_pinch_v1*, uint32_t, uint32_t,
                               wl_surface* surface, uint32_t) {
    auto* self = static_cast<Input*>(data);
    if (surface != self->mWindow.surface())
        return;
    // Remember the pointer position at gesture start. Hyprland (and some
    // other compositors) send pinch updates' dx/dy as offsets relative to
    // this starting center, not as absolute surface coordinates.
    self->mPinchStartX = self->mLastX;
    self->mPinchStartY = self->mLastY;
    self->mEvents.on_pinch_begin(self->mLastX, self->mLastY);
}

void Input::handle_pinch_update(void* data, zwp_pointer_gesture_pinch_v1*, uint32_t,
                                wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t scale,
                                wl_fixed_t) {
    auto* self = static_cast<Input*>(data);
    // dx/dy are the gesture center relative to the position at begin;
    // reconstruct the absolute surface-local focal point.
    const double fx = self->mPinchStartX + wl_fixed_to_double(dx);
    const double fy = self->mPinchStartY + wl_fixed_to_double(dy);
    const double fs = wl_fixed_to_double(scale);
    if (getenv("AETHER_PINCH_DEBUG"))
        fprintf(stderr, "[pinch] update rel=(%.1f, %.1f) focal=(%.1f, %.1f) scale=%.4f\n",
                wl_fixed_to_double(dx), wl_fixed_to_double(dy), fx, fy, fs);
    self->mEvents.on_pinch_update(fx, fy, fs);
}

void Input::handle_pinch_end(void* data, zwp_pointer_gesture_pinch_v1*, uint32_t, uint32_t,
                             int32_t) {
    static_cast<Input*>(data)->mEvents.on_pinch_end();
}

void Input::flush_scroll() {
    if (!mScrollPending)
        return;
    const bool wheel = mHasWheel || mHasDiscrete;
    double dx = mAccX, dy = mAccY;
    if (mHasWheel) {
        dx = mWheelX;
        dy = mWheelY;
    } else if (mHasDiscrete) {
        dx = mDiscreteX * 120.0;
        dy = mDiscreteY * 120.0;
    }
    mAccX = mAccY = 0;
    mWheelX = mWheelY = 0;
    mDiscreteX = mDiscreteY = 0;
    mHasWheel = mHasDiscrete = mScrollPending = false;
    if (dx != 0.0 || dy != 0.0)
        mEvents.on_scroll(is_ctrl_down(mXkbState), !wheel, dx, dy);
}

void Input::deliver_scroll_immediate() {
    // wl_pointer < 5: no frame events and no value120; raw pixel deltas only.
    if (mAccX == 0.0 && mAccY == 0.0)
        return;
    const double dx = mAccX, dy = mAccY;
    mAccX = mAccY = 0;
    mScrollPending = false;
    mEvents.on_scroll(is_ctrl_down(mXkbState), true, dx, dy);
}

// --- keyboard --------------------------------------------------------------

void Input::handle_keymap(void* data, wl_keyboard*, uint32_t format, int32_t fd,
                          uint32_t size) {
    auto* self = static_cast<Input*>(data);
    if (format != XKB_KEYMAP_FORMAT_TEXT_V1) {
        close(fd);
        return;
    }
    void* map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED)
        return;
    std::string text(static_cast<const char*>(map), size);
    munmap(map, size);
    while (!text.empty() && text.back() == '\0')
        text.pop_back();

    xkb_keymap* keymap = xkb_keymap_new_from_string(
        self->mXkbContext, text.c_str(), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap)
        return;
    xkb_state* state = xkb_state_new(keymap);
    if (self->mXkbState)
        xkb_state_unref(self->mXkbState);
    if (self->mXkbKeymap)
        xkb_keymap_unref(self->mXkbKeymap);
    self->mXkbKeymap = keymap;
    self->mXkbState = state;
}

void Input::handle_keyboard_enter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {}
void Input::handle_keyboard_leave(void*, wl_keyboard*, uint32_t, wl_surface*) {}

void Input::handle_key(void* data, wl_keyboard*, uint32_t, uint32_t, uint32_t key,
                       uint32_t state) {
    auto* self = static_cast<Input*>(data);
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED || !self->mXkbState)
        return;
    const xkb_keysym_t sym =
        xkb_state_key_get_one_sym(self->mXkbState, static_cast<xkb_keycode_t>(key + 8));
    const Key mapped = self->translate_key(sym);
    if (mapped != Key::Unknown)
        self->mEvents.on_key(mapped, self->current_mods());
}

void Input::handle_keyboard_modifiers(void* data, wl_keyboard*, uint32_t, uint32_t depressed,
                                      uint32_t latched, uint32_t locked, uint32_t group) {
    auto* self = static_cast<Input*>(data);
    if (self->mXkbState)
        xkb_state_update_mask(self->mXkbState, depressed, latched, locked, 0, 0, group);
}

void Input::handle_repeat_info(void*, wl_keyboard*, int32_t, int32_t) {}

unsigned Input::current_mods() const {
    unsigned mods = 0;
    if (!mXkbState)
        return mods;
    if (xkb_state_mod_name_is_active(mXkbState, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= MOD_CTRL;
    if (xkb_state_mod_name_is_active(mXkbState, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= MOD_SHIFT;
    if (xkb_state_mod_name_is_active(mXkbState, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= MOD_ALT;
    if (xkb_state_mod_name_is_active(mXkbState, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= MOD_LOGO;
    return mods;
}

Key Input::translate_key(uint32_t keysym) const {
    switch (keysym) {
    case XKB_KEY_m:
    case XKB_KEY_M:
        return Key::M;
    case XKB_KEY_r:
    case XKB_KEY_R:
        return Key::R;
    case XKB_KEY_l:
    case XKB_KEY_L:
        return Key::L;
    case XKB_KEY_s:
    case XKB_KEY_S:
        return Key::S;
    case XKB_KEY_f:
    case XKB_KEY_F:
        return Key::F;
    case XKB_KEY_0:
        return Key::Zero;
    case XKB_KEY_1:
        return Key::One;
    case XKB_KEY_plus:
    case XKB_KEY_equal:
        return Key::ZoomIn;
    case XKB_KEY_minus:
        return Key::ZoomOut;
    case XKB_KEY_question:
    case XKB_KEY_slash:
        return Key::HelpToggle;
    case XKB_KEY_Left:
        return Key::Left;
    case XKB_KEY_Right:
        return Key::Right;
    case XKB_KEY_Up:
        return Key::Up;
    case XKB_KEY_Down:
        return Key::Down;
    case XKB_KEY_Delete:
        return Key::Delete;
    case XKB_KEY_BackSpace:
        return Key::Backspace;
    case XKB_KEY_Escape:
        return Key::Escape;
    default:
        return Key::Unknown;
    }
}

} // namespace aether
