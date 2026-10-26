#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include <wayland-client.h>

struct xdg_wm_base;
struct zwp_pointer_gestures_v1;

namespace aether {

// Owns the Wayland display connection and the global singletons
// (compositor, shm, seat, data device manager, xdg_wm_base, outputs).
class Connection {
public:
    Connection() = default;
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    bool connect(std::string* err);
    bool roundtrip(std::string* err);

    wl_display* display() const { return mDisplay; }

    wl_compositor* compositor() const { return mCompositor; }
    wl_shm* shm() const { return mShm; }
    wl_seat* seat() const { return mSeat; }
    wl_data_device_manager* data_manager() const { return mDataManager; }
    xdg_wm_base* wm_base() const { return mWmBase; }
    zwp_pointer_gestures_v1* pointer_gestures() const { return mPointerGestures; }

    uint32_t compositor_version() const { return mCompositorVersion; }
    uint32_t seat_version() const { return mSeatVersion; }
    uint32_t data_manager_version() const { return mDataManagerVersion; }

    // Scale of a bound output (default 1).
    int output_scale(wl_output* output) const;

    // Called whenever any output's properties change (scale etc.).
    std::function<void()> on_output_event;

private:
    static void handle_global(void* data, wl_registry* registry, uint32_t name,
                              const char* interface, uint32_t version);
    static void handle_global_remove(void* data, wl_registry* registry, uint32_t name);
    static void handle_output_geometry(void* data, wl_output* output, int32_t x, int32_t y,
                                       int32_t physical_w, int32_t physical_h, int32_t subpixel,
                                       const char* make, const char* model, int32_t transform);
    static void handle_output_mode(void* data, wl_output* output, uint32_t flags, int32_t w,
                                   int32_t h, int32_t refresh);
    static void handle_output_done(void* data, wl_output* output);
    static void handle_output_scale(void* data, wl_output* output, int32_t factor);
    static void handle_output_name(void* data, wl_output* output, const char* name);
    static void handle_output_description(void* data, wl_output* output, const char* desc);

    wl_display* mDisplay = nullptr;
    wl_registry* mRegistry = nullptr;
    wl_compositor* mCompositor = nullptr;
    wl_shm* mShm = nullptr;
    wl_seat* mSeat = nullptr;
    wl_data_device_manager* mDataManager = nullptr;
    xdg_wm_base* mWmBase = nullptr;
    zwp_pointer_gestures_v1* mPointerGestures = nullptr;
    uint32_t mCompositorVersion = 0;
    uint32_t mSeatVersion = 0;
    uint32_t mDataManagerVersion = 0;

    struct OutputState {
        wl_output* output = nullptr;
        int scale = 1;
    };
    std::map<wl_output*, OutputState> mOutputs;
};

} // namespace aether
