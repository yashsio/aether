#include "platform/connection.hpp"

#include "pointer-gestures-unstable-v1-client-protocol.h"
#include "xdg-shell-protocol.h"

#include <algorithm>
#include <cstring>

namespace aether {

namespace {
constexpr uint32_t kMaxCompositor = 6;
constexpr uint32_t kMaxSeat = 9;
constexpr uint32_t kMaxDataManager = 3;
constexpr uint32_t kMaxWmBase = 6;
constexpr uint32_t kMaxOutput = 4;
constexpr uint32_t kMaxPointerGestures = 3; // hold gesture (v3); pinch is v1
} // namespace

Connection::~Connection() {
    if (mPointerGestures)
        zwp_pointer_gestures_v1_destroy(mPointerGestures);
    if (mSeat)
        wl_seat_destroy(mSeat);
    if (mDataManager)
        wl_data_device_manager_destroy(mDataManager);
    if (mWmBase)
        xdg_wm_base_destroy(mWmBase);
    if (mShm)
        wl_shm_destroy(mShm);
    if (mCompositor)
        wl_compositor_destroy(mCompositor);
    for (auto& [out, st] : mOutputs)
        wl_output_destroy(out);
    if (mRegistry)
        wl_registry_destroy(mRegistry);
    if (mDisplay)
        wl_display_disconnect(mDisplay);
}

bool Connection::connect(std::string* err) {
    mDisplay = wl_display_connect(nullptr);
    if (!mDisplay) {
        if (err)
            *err = "failed to connect to Wayland display (is WAYLAND_DISPLAY set?)";
        return false;
    }
    static const wl_registry_listener registry_listener = {handle_global, handle_global_remove};
    mRegistry = wl_display_get_registry(mDisplay);
    wl_registry_add_listener(mRegistry, &registry_listener, this);
    return roundtrip(err);
}

bool Connection::roundtrip(std::string* err) {
    if (wl_display_roundtrip(mDisplay) < 0) {
        if (err)
            *err = "Wayland roundtrip failed";
        return false;
    }
    return true;
}

int Connection::output_scale(wl_output* output) const {
    auto it = mOutputs.find(output);
    return it != mOutputs.end() ? it->second.scale : 1;
}

void Connection::handle_global(void* data, wl_registry* registry, uint32_t name,
                               const char* interface, uint32_t version) {
    auto* self = static_cast<Connection*>(data);
    if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        self->mCompositorVersion = std::min(version, kMaxCompositor);
        self->mCompositor = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, self->mCompositorVersion));
    } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
        self->mShm =
            static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
        self->mSeatVersion = std::min(version, kMaxSeat);
        self->mSeat =
            static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface,
                                                   self->mSeatVersion));
    } else if (std::strcmp(interface, wl_data_device_manager_interface.name) == 0) {
        self->mDataManagerVersion = std::min(version, kMaxDataManager);
        self->mDataManager = static_cast<wl_data_device_manager*>(wl_registry_bind(
            registry, name, &wl_data_device_manager_interface, self->mDataManagerVersion));
    } else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0) {
        const uint32_t ver = std::min(version, kMaxWmBase);
        self->mWmBase = static_cast<xdg_wm_base*>(
            wl_registry_bind(registry, name, &xdg_wm_base_interface, ver));
    } else if (std::strcmp(interface, zwp_pointer_gestures_v1_interface.name) == 0) {
        const uint32_t ver = std::min(version, kMaxPointerGestures);
        self->mPointerGestures = static_cast<zwp_pointer_gestures_v1*>(wl_registry_bind(
            registry, name, &zwp_pointer_gestures_v1_interface, ver));
    } else if (std::strcmp(interface, wl_output_interface.name) == 0) {
        const uint32_t ver = std::min(version, kMaxOutput);
        auto* output =
            static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface, ver));
        OutputState st;
        st.output = output;
        st.scale = 1;
        self->mOutputs.emplace(output, st);
        static const wl_output_listener listener = {
            handle_output_geometry, handle_output_mode, handle_output_done,
            handle_output_scale,   handle_output_name, handle_output_description,
        };
        wl_output_add_listener(output, &listener, self);
    }
}

void Connection::handle_global_remove(void*, wl_registry*, uint32_t) {}

void Connection::handle_output_geometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t,
                                        int32_t, const char*, const char*, int32_t) {}
void Connection::handle_output_mode(void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {}
void Connection::handle_output_done(void*, wl_output*) {}

void Connection::handle_output_scale(void* data, wl_output* output, int32_t factor) {
    auto* self = static_cast<Connection*>(data);
    auto it = self->mOutputs.find(output);
    if (it != self->mOutputs.end())
        it->second.scale = std::max(1, factor);
    if (self->on_output_event)
        self->on_output_event();
}

void Connection::handle_output_name(void*, wl_output*, const char*) {}
void Connection::handle_output_description(void*, wl_output*, const char*) {}

} // namespace aether
