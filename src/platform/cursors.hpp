#pragma once

#include <cstdint>

#include <wayland-client.h>

struct wl_cursor_theme;
struct wl_cursor;

namespace aether {

class Connection;

// Cursor shapes for the viewer, loaded from the user's XCursor theme via
// libwayland-cursor: the "left_ptr" arrow normally and the "grabbing" hand
// while dragging. If no theme can be loaded the compositor's own default
// cursor is left in place.
class Cursors {
public:
    Cursors(Connection& conn, int buffer_scale);
    ~Cursors();
    Cursors(const Cursors&) = delete;
    Cursors& operator=(const Cursors&) = delete;

    // Shows the arrow (grabbing=false) or grabbing-hand cursor. Reloads the
    // theme if `scale` changed since the last call.
    void show(wl_pointer* pointer, uint32_t serial, bool grabbing, int scale);

private:
    bool ensure_theme(int scale);
    bool show_cursor(wl_pointer* pointer, uint32_t serial, const char* name);
    void destroy_surfaces();

    Connection& mConn;

    wl_cursor_theme* mTheme = nullptr;
    int mThemeScale = 0;

    // One persistent cursor surface per role. The surface must stay alive as
    // long as the compositor may render it, so we never destroy it right
    // after wl_pointer_set_cursor - we only re-attach when the shape changes.
    wl_surface* mOpenSurface = nullptr;
    wl_buffer* mOpenBuffer = nullptr;
    uint32_t mOpenHotspotX = 0, mOpenHotspotY = 0;
    wl_surface* mGrabSurface = nullptr;
    wl_buffer* mGrabBuffer = nullptr;
    uint32_t mGrabHotspotX = 0, mGrabHotspotY = 0;
};

} // namespace aether
