#include "platform/cursors.hpp"

#include "platform/connection.hpp"

#include <wayland-cursor.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace aether {

Cursors::Cursors(Connection& conn, int buffer_scale) : mConn(conn) {
    if (conn.shm())
        ensure_theme(buffer_scale);
}

Cursors::~Cursors() {
    destroy_surfaces();
    if (mTheme)
        wl_cursor_theme_destroy(mTheme);
}

bool Cursors::ensure_theme(int scale) {
    scale = std::max(1, scale);
    if (mTheme && mThemeScale == scale)
        return true;
    const char* theme = std::getenv("XCURSOR_THEME");
    const char* size_env = std::getenv("XCURSOR_SIZE");
    int size = size_env && *size_env ? std::atoi(size_env) : 24;
    if (size <= 0)
        size = 24;
    wl_cursor_theme* next =
        wl_cursor_theme_load(theme && *theme ? theme : nullptr, size * scale, mConn.shm());
    if (!next)
        return false;
    // Rebuilding the theme invalidates the old theme buffers, so drop the
    // cached surfaces that reference them.
    destroy_surfaces();
    if (mTheme)
        wl_cursor_theme_destroy(mTheme);
    mTheme = next;
    mThemeScale = scale;
    return true;
}

void Cursors::destroy_surfaces() {
    if (mOpenSurface) {
        wl_surface_destroy(mOpenSurface);
        mOpenSurface = nullptr;
    }
    if (mGrabSurface) {
        wl_surface_destroy(mGrabSurface);
        mGrabSurface = nullptr;
    }
    mOpenBuffer = mGrabBuffer = nullptr;
}

bool Cursors::show_cursor(wl_pointer* pointer, uint32_t serial, const char* name) {
    if (!mTheme)
        return false;
    wl_cursor* cursor = wl_cursor_theme_get_cursor(mTheme, name);
    if (!cursor || cursor->image_count == 0)
        return false;
    wl_cursor_image* image = cursor->images[0];
    wl_buffer* buffer = wl_cursor_image_get_buffer(image);
    if (!buffer)
        return false;

    const bool grabbing = std::strcmp(name, "grabbing") == 0;
    wl_surface*& surface = grabbing ? mGrabSurface : mOpenSurface;
    wl_buffer*& old_buffer = grabbing ? mGrabBuffer : mOpenBuffer;
    uint32_t& hot_x = grabbing ? mGrabHotspotX : mOpenHotspotX;
    uint32_t& hot_y = grabbing ? mGrabHotspotY : mOpenHotspotY;

    // (Re)create the surface only when it doesn't exist yet or the buffer for
    // this role changed. Otherwise reuse the already-committed surface.
    if (!surface || old_buffer != buffer) {
        if (surface)
            wl_surface_destroy(surface);
        surface = wl_compositor_create_surface(mConn.compositor());
        if (!surface) {
            old_buffer = nullptr;
            return false;
        }
        wl_surface_attach(surface, buffer, 0, 0);
        wl_surface_damage(surface, 0, 0, static_cast<int>(image->width),
                          static_cast<int>(image->height));
        wl_surface_commit(surface);
        old_buffer = buffer;
        hot_x = image->hotspot_x;
        hot_y = image->hotspot_y;
    }
    wl_pointer_set_cursor(pointer, serial, surface, static_cast<int32_t>(hot_x),
                          static_cast<int32_t>(hot_y));
    return true;
}

void Cursors::show(wl_pointer* pointer, uint32_t serial, bool grabbing, int scale) {
    if (!pointer)
        return;
    if (!ensure_theme(scale))
        return;
    // Fall back to the arrow if the theme has no dedicated grabbing shape.
    if (grabbing && show_cursor(pointer, serial, "grabbing"))
        return;
    show_cursor(pointer, serial, "left_ptr");
}

} // namespace aether
