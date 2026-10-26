#pragma once

#include "platform/events.hpp"

#include <string>

#include <wayland-client.h>

namespace aether {

class Connection;
class Window;

// File drag & drop via wl_data_device ("text/uri-list").
class Dnd {
public:
    Dnd(Connection& conn, Events& events, Window& window);

private:
    // wl_data_device_manager / wl_data_device
    static void handle_data_offer(void* data, wl_data_device* device, wl_data_offer* offer);
    static void handle_enter(void* data, wl_data_device* device, uint32_t time,
                             wl_surface* surface, wl_fixed_t x, wl_fixed_t y,
                             wl_data_offer* offer);
    static void handle_leave(void* data, wl_data_device* device);
    static void handle_motion(void* data, wl_data_device* device, uint32_t time, wl_fixed_t x,
                              wl_fixed_t y);
    static void handle_drop(void* data, wl_data_device* device);
    static void handle_selection(void* data, wl_data_device* device, wl_data_offer* offer);

    // wl_data_offer
    static void handle_mime_types(void* data, wl_data_offer* offer, const char* mime_type);
    static void handle_source_actions(void* data, wl_data_offer* offer, uint32_t actions);
    static void handle_action(void* data, wl_data_offer* offer, uint32_t action);

    void destroy_offer(wl_data_offer*& offer);

    Connection& mConn;
    Events& mEvents;
    Window& mWindow;

    wl_data_device* mDevice = nullptr;
    wl_data_offer* mPendingOffer = nullptr; // announced, not yet used
    wl_data_offer* mActiveOffer = nullptr;   // entered/selected
    uint32_t mManagerVersion = 0;
};

} // namespace aether
