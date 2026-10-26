#pragma once

#include "platform/events.hpp"

#include <cstdint>
#include <memory>

#include <wayland-client.h>

struct xkb_context;
struct xkb_keymap;
struct xkb_state;
struct zwp_pointer_gesture_pinch_v1;

namespace aether {

class Connection;
class Window;
class Cursors;

// Binds wl_seat: pointer (pan/zoom/double-click, cursor shapes) and
// keyboard (xkbcommon keymap translation). Forwards neutral events.
class Input {
public:
    Input(Connection& conn, Events& events, Window& window);
    ~Input();
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;

    bool ok() const { return mHasPointer || mHasKeyboard; }

private:
    // wl_seat
    static void handle_capabilities(void* data, wl_seat* seat, uint32_t capabilities);
    static void handle_name(void* data, wl_seat* seat, const char* name);

    // wl_pointer
    static void handle_pointer_enter(void* data, wl_pointer* pointer, uint32_t serial,
                                     wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy);
    static void handle_pointer_leave(void* data, wl_pointer* pointer, uint32_t serial,
                                     wl_surface* surface);
    static void handle_pointer_motion(void* data, wl_pointer* pointer, uint32_t time,
                                      wl_fixed_t sx, wl_fixed_t sy);
    static void handle_pointer_button(void* data, wl_pointer* pointer, uint32_t serial,
                                      uint32_t time, uint32_t button, uint32_t state);
    static void handle_pointer_axis(void* data, wl_pointer* pointer, uint32_t time,
                                    uint32_t axis, wl_fixed_t value);
    static void handle_pointer_frame(void* data, wl_pointer* pointer);
    static void handle_pointer_axis_source(void* data, wl_pointer* pointer, uint32_t source);
    static void handle_pointer_axis_stop(void* data, wl_pointer* pointer, uint32_t time,
                                         uint32_t axis);
    static void handle_pointer_axis_discrete(void* data, wl_pointer* pointer, uint32_t axis,
                                             int32_t discrete);
    static void handle_pointer_axis_value120(void* data, wl_pointer* pointer, uint32_t axis,
                                             int32_t value120);
    static void handle_pointer_axis_relative_direction(void* data, wl_pointer* pointer,
                                                       uint32_t axis, uint32_t direction);
    static void handle_pointer_warp(void* data, wl_pointer* pointer, wl_fixed_t sx,
                                    wl_fixed_t sy);

    // zwp_pointer_gesture_pinch_v1
    static void handle_pinch_begin(void* data, zwp_pointer_gesture_pinch_v1* gesture,
                                   uint32_t serial, uint32_t time, wl_surface* surface,
                                   uint32_t fingers);
    static void handle_pinch_update(void* data, zwp_pointer_gesture_pinch_v1* gesture,
                                    uint32_t time, wl_fixed_t dx, wl_fixed_t dy,
                                    wl_fixed_t scale, wl_fixed_t rotation);
    static void handle_pinch_end(void* data, zwp_pointer_gesture_pinch_v1* gesture,
                                 uint32_t serial, uint32_t time, int32_t cancelled);

    // wl_keyboard
    static void handle_keymap(void* data, wl_keyboard* keyboard, uint32_t format, int32_t fd,
                              uint32_t size);
    static void handle_keyboard_enter(void* data, wl_keyboard* keyboard, uint32_t serial,
                                      wl_surface* surface, wl_array* keys);
    static void handle_keyboard_leave(void* data, wl_keyboard* keyboard, uint32_t serial,
                                      wl_surface* surface);
    static void handle_key(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t time,
                           uint32_t key, uint32_t state);
    static void handle_keyboard_modifiers(void* data, wl_keyboard* keyboard, uint32_t serial,
                                          uint32_t depressed, uint32_t latched, uint32_t locked,
                                          uint32_t group);
    static void handle_repeat_info(void* data, wl_keyboard* keyboard, int32_t rate,
                                   int32_t delay);

    void setup_pointer();
    void setup_keyboard();
    void flush_scroll();      // deliver accumulated scroll (frame path)
    void deliver_scroll_immediate(); // legacy path, wl_pointer < 5
    unsigned current_mods() const;
    Key translate_key(uint32_t keysym) const;

    Connection& mConn;
    Events& mEvents;
    Window& mWindow;

    wl_pointer* mPointer = nullptr;
    wl_keyboard* mKeyboard = nullptr;
    zwp_pointer_gesture_pinch_v1* mPinch = nullptr;
    uint32_t mPointerVersion = 0;
    bool mHasPointer = false;
    bool mHasKeyboard = false;

    double mLastX = 0, mLastY = 0;

    // Accumulated scroll across one pointer frame, Wayland signs
    // (positive = down/right); converted to Qt signs at delivery.
    double mAccX = 0, mAccY = 0;
    int32_t mWheelX = 0, mWheelY = 0; // 120-per-detent units (Qt sign)
    bool mHasWheel = false;
    bool mHasDiscrete = false;
    int32_t mDiscreteX = 0, mDiscreteY = 0;
    bool mScrollPending = false;

    uint32_t mEnterSerial = 0;
    bool mPointerOver = false;

    // Pinch: pointer position at gesture begin (dx/dy are relative to it).
    double mPinchStartX = 0, mPinchStartY = 0;

    xkb_context* mXkbContext = nullptr;
    xkb_keymap* mXkbKeymap = nullptr;
    xkb_state* mXkbState = nullptr;

    std::unique_ptr<Cursors> mCursors;
};

} // namespace aether
