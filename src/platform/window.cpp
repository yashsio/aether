#include "platform/window.hpp"

#include "platform/connection.hpp"
#include "platform/shm_pool.hpp"
#include "xdg-shell-protocol.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace aether {

Window::Window(Connection& conn, Events* events) : mConn(conn), mEvents(events) {
    if (!conn.compositor() || !conn.wm_base() || !conn.shm())
        return;

    mPool = new ShmPool(conn.shm());

    static const xdg_wm_base_listener wm_listener = {handle_wm_ping};
    xdg_wm_base_add_listener(conn.wm_base(), &wm_listener, this);

    mSurface = wl_compositor_create_surface(conn.compositor());
    if (!mSurface) {
        delete mPool;
        mPool = nullptr;
        return;
    }
    static const wl_surface_listener surface_listener = {
        handle_surface_enter,          handle_surface_leave,
        handle_surface_preferred_scale, handle_surface_preferred_transform,
    };
    wl_surface_add_listener(mSurface, &surface_listener, this);

    mXdgSurface = xdg_wm_base_get_xdg_surface(conn.wm_base(), mSurface);
    static const xdg_surface_listener xdg_listener = {handle_xdg_surface_configure};
    xdg_surface_add_listener(mXdgSurface, &xdg_listener, this);

    mToplevel = xdg_surface_get_toplevel(mXdgSurface);
    static const xdg_toplevel_listener toplevel_listener = {
        handle_toplevel_configure,    handle_toplevel_close,
        handle_toplevel_configure_bounds, handle_toplevel_wm_capabilities,
    };
    xdg_toplevel_add_listener(mToplevel, &toplevel_listener, this);
    xdg_toplevel_set_app_id(mToplevel, "aether");
    xdg_toplevel_set_title(mToplevel, "aether");

    // Initial commit without a buffer to solicit the first configure event.
    wl_surface_commit(mSurface);
    flush();
}

Window::~Window() {
    if (mPool)
        delete mPool;
    if (mToplevel)
        xdg_toplevel_destroy(mToplevel);
    if (mXdgSurface)
        xdg_surface_destroy(mXdgSurface);
    if (mSurface)
        wl_surface_destroy(mSurface);
}

void Window::set_title(const std::string& title) {
    if (mToplevel)
        xdg_toplevel_set_title(mToplevel, title.c_str());
}

void Window::invalidate() {
    mDirty = true;
}

bool Window::dispatch_pending() {
    return wl_display_dispatch_pending(mConn.display()) >= 0;
}

bool Window::flush() {
    return wl_display_flush(mConn.display()) >= 0 || errno == EAGAIN;
}

bool Window::render(const std::function<void(uint32_t*, int, int)>& compose) {
    if (!mConfigured || !mPool || mWidth <= 0 || mHeight <= 0)
        return false;
    const int bw = mWidth * mScale;
    const int bh = mHeight * mScale;

    ShmPool::Slot* slot = mPool->acquire(bw, bh, [this]() -> bool {
        // Block until the compositor releases a buffer (or we die).
        if (wl_display_dispatch(mConn.display()) < 0) {
            mRunning = false;
            return false;
        }
        return true;
    });
    if (!slot)
        return false;

    compose(slot->data, slot->w, slot->h);

    wl_surface_set_buffer_scale(mSurface, mScale);
    wl_surface_attach(mSurface, slot->buffer, 0, 0);
    if (mConn.compositor_version() >= 4)
        wl_surface_damage_buffer(mSurface, 0, 0, slot->w, slot->h);
    else
        wl_surface_damage(mSurface, 0, 0, mWidth, mHeight);
    wl_surface_commit(mSurface);
    flush();
    return true;
}

void Window::handle_wm_ping(void* data, xdg_wm_base* wm_base, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

void Window::handle_xdg_surface_configure(void* data, xdg_surface* surface, uint32_t serial) {
    auto* self = static_cast<Window*>(data);
    xdg_surface_ack_configure(surface, serial);
    self->mConfigured = true;
    self->mDirty = true;
    // Every buffer pixel is opaque; tell the compositor so it can skip
    // blending for this surface.
    if (self->mWidth > 0 && self->mHeight > 0) {
        if (wl_region* region =
                wl_compositor_create_region(self->mConn.compositor())) {
            wl_region_add(region, 0, 0, self->mWidth, self->mHeight);
            wl_surface_set_opaque_region(self->mSurface, region);
            wl_region_destroy(region);
        }
    }
    if (self->mResizePending) {
        self->mResizePending = false;
        if (self->mEvents)
            self->mEvents->on_configure(self->mWidth, self->mHeight);
    }
}

void Window::handle_toplevel_configure(void* data, xdg_toplevel*, int32_t w, int32_t h,
                                       wl_array*) {
    auto* self = static_cast<Window*>(data);
    if (w <= 0 || h <= 0)
        return;
    if (w == self->mWidth && h == self->mHeight)
        return;
    self->mWidth = w;
    self->mHeight = h;
    self->mResizePending = true;
    self->mDirty = true;
}

void Window::handle_toplevel_close(void* data, xdg_toplevel*) {
    auto* self = static_cast<Window*>(data);
    self->mRunning = false;
    if (self->mEvents)
        self->mEvents->on_close();
}

void Window::handle_toplevel_configure_bounds(void*, xdg_toplevel*, int32_t, int32_t) {}
void Window::handle_toplevel_wm_capabilities(void*, xdg_toplevel*, wl_array*) {}

void Window::handle_surface_enter(void* data, wl_surface*, wl_output* output) {
    auto* self = static_cast<Window*>(data);
    const bool empty = self->mEnteredOutputs.empty();
    self->mEnteredOutputs.insert(output);
    if (empty)
        self->recompute_scale();
}

void Window::handle_surface_leave(void* data, wl_surface*, wl_output* output) {
    auto* self = static_cast<Window*>(data);
    self->mEnteredOutputs.erase(output);
    self->recompute_scale();
}

void Window::handle_surface_preferred_scale(void* data, wl_surface*, int32_t scale) {
    auto* self = static_cast<Window*>(data);
    scale = std::max(1, scale);
    if (scale != self->mScale) {
        self->mScale = scale;
        self->mDirty = true;
    }
}

void Window::handle_surface_preferred_transform(void*, wl_surface*, uint32_t) {}

void Window::recompute_scale() {
    int scale = 1;
    for (wl_output* out : mEnteredOutputs)
        scale = std::max(scale, mConn.output_scale(out));
    if (scale != mScale) {
        mScale = scale;
        mDirty = true;
    }
}

} // namespace aether
