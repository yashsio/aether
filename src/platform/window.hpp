#pragma once

#include "platform/connection.hpp"
#include "platform/events.hpp"
#include "xdg-shell-protocol.h"

#include <cstdint>
#include <functional>
#include <set>
#include <string>

#include <wayland-client.h>

namespace aether {

class Connection;
class ShmPool;
// The xdg-shell toplevel window. Implements Host for the app.
class Window : public Host {
public:
    // `events` may be attached later via set_events() (App construction
    // needs the Window first) - no events dispatch before the main loop.
    Window(Connection& conn, Events* events = nullptr);
    ~Window() override;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    void set_events(Events* events) { mEvents = events; }

    bool ok() const { return mSurface != nullptr; }
    wl_surface* surface() const { return mSurface; }
    wl_display* display() const { return mConn.display(); }

    // Host interface
    void set_title(const std::string& title) override;
    void invalidate() override;
    void quit() override { mRunning = false; }
    int width() const override { return mWidth; }
    int height() const override { return mHeight; }
    int buffer_scale() const override { return mScale; }

    bool running() const { return mRunning; }
    bool dirty() const { return mDirty; }
    void clear_dirty() { mDirty = false; }
    bool configured() const { return mConfigured; }

    // Compose `pixels` (ARGB, buf_w*buf_h) and commit it to the screen.
    // `compose` receives (pixels, buf_w, buf_h).
    bool render(const std::function<void(uint32_t*, int, int)>& compose);

    // wl_display event pumping helpers for the main loop.
    bool dispatch_pending(); // false on connection error
    bool flush();

private:
    static void handle_wm_ping(void* data, xdg_wm_base* wm_base, uint32_t serial);
    static void handle_xdg_surface_configure(void* data, xdg_surface* surface, uint32_t serial);
    static void handle_toplevel_configure(void* data, xdg_toplevel* toplevel, int32_t w,
                                          int32_t h, wl_array* states);
    static void handle_toplevel_close(void* data, xdg_toplevel* toplevel);
    static void handle_toplevel_configure_bounds(void* data, xdg_toplevel* toplevel, int32_t w,
                                                 int32_t h);
    static void handle_toplevel_wm_capabilities(void* data, xdg_toplevel* toplevel,
                                                wl_array* capabilities);
    static void handle_surface_enter(void* data, wl_surface* surface, wl_output* output);
    static void handle_surface_leave(void* data, wl_surface* surface, wl_output* output);
    static void handle_surface_preferred_scale(void* data, wl_surface* surface, int32_t scale);
    static void handle_surface_preferred_transform(void* data, wl_surface* surface,
                                                   uint32_t transform);

    void recompute_scale();

    Connection& mConn;
    Events* mEvents = nullptr;

    wl_surface* mSurface = nullptr;
    xdg_surface* mXdgSurface = nullptr;
    xdg_toplevel* mToplevel = nullptr;

    int mWidth = 1280;
    int mHeight = 800;
    int mScale = 1;
    bool mConfigured = false;
    bool mDirty = true;
    bool mRunning = true;

    std::set<wl_output*> mEnteredOutputs;

    ShmPool* mPool = nullptr;
    bool mResizePending = false; // size changed since last commit
};

} // namespace aether
