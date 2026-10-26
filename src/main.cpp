#include "app.hpp"
#include "platform/connection.hpp"
#include "platform/dnd.hpp"
#include "platform/input.hpp"
#include "platform/window.hpp"

#include <poll.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace aether;

namespace {

// Optional frame-time instrumentation: AETHER_BENCH=1 prints the average
// render+commit time every 60 frames to stderr.
struct Bench {
    bool enabled = std::getenv("AETHER_BENCH") != nullptr;
    int frames = 0;
    double total_ms = 0;

    void add(double ms) {
        if (!enabled)
            return;
        total_ms += ms;
        if (++frames >= 60) {
            std::fprintf(stderr, "[bench] avg frame: %.2f ms (%d frames)\n", total_ms / frames,
                         frames);
            frames = 0;
            total_ms = 0;
        }
    }
};

double elapsed_ms(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
        .count();
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: aether <image>\n");
        std::fprintf(stderr, "(files can also be dropped onto the window after launch)\n");
        return 1;
    }

    std::string err;
    Connection conn;
    if (!conn.connect(&err)) {
        std::fprintf(stderr, "aether: %s\n", err.c_str());
        return 1;
    }

    Window window(conn);
    if (!window.ok()) {
        std::fprintf(stderr, "aether: failed to create a Wayland surface "
                             "(xdg-shell / wl_shm missing)\n");
        return 1;
    }

    App app(window);
    window.set_events(&app);

    Input input(conn, app, window);
    Dnd dnd(conn, app, window);

    app.load_file(argv[1]);

    Bench bench;
    wl_display* display = conn.display();
    const int fd = wl_display_get_fd(display);
    bool running = true;

    while (running) {
        if (window.dispatch_pending()) {
            // deliver anything already queued
        } else {
            break; // protocol error / display gone
        }
        if (!window.flush())
            break;

        if (window.dirty()) {
            const auto t0 = std::chrono::steady_clock::now();
            const bool ok = window.render(
                [&](uint32_t* px, int w, int h) { app.compose(px, w, h); });
            if (ok) {
                window.clear_dirty();
                bench.add(elapsed_ms(t0));
                continue; // process any events the commit produced
            }
            // render() failed: not configured yet (or zero-sized) - we must
            // read events before trying again, so fall through to the wait.
        }

        // Nothing to draw: sleep until the next event or timer deadline.
        const int timeout = static_cast<int>(app.timer_ms());

        while (wl_display_prepare_read(display) != 0) {
            if (wl_display_dispatch_pending(display) < 0) {
                running = false;
                break;
            }
        }
        if (!running)
            break;

        if (wl_display_flush(display) < 0 && errno != EAGAIN) {
            wl_display_cancel_read(display);
            break;
        }

        pollfd pfd{fd, POLLIN, 0};
        const int r = poll(&pfd, 1, timeout);
        if (r < 0 && errno != EINTR) {
            wl_display_cancel_read(display);
            break;
        }
        if (r == 0) {
            wl_display_cancel_read(display);
            app.on_timer();
            continue;
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            wl_display_cancel_read(display);
            break;
        }
        if (wl_display_read_events(display) < 0)
            break;
        if (wl_display_dispatch_pending(display) < 0)
            break;

        if (!window.running())
            running = false;
    }

    return 0;
}
