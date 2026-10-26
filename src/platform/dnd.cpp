#include "platform/dnd.hpp"

#include "platform/connection.hpp"
#include "platform/window.hpp"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <chrono>
#include <cctype>

namespace aether {

namespace {

std::string percent_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            const auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return c - 'A' + 10;
            };
            out.push_back(static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2])));
            i += 2;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

// Read from the offer fd with a bounded budget (never hang the UI).
std::string read_offer_fd(int fd) {
    std::string data;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    for (;;) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
            break;
        const int ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
        pollfd p{fd, POLLIN, 0};
        const int r = poll(&p, 1, ms);
        if (r <= 0)
            break;
        char buf[4096];
        const ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0)
            break;
        data.append(buf, static_cast<size_t>(n));
        if (data.size() > (1u << 20)) // 1 MB of paths is plenty
            break;
    }
    return data;
}

std::vector<std::string> parse_uri_list(const std::string& raw) {
    std::vector<std::string> paths;
    size_t pos = 0;
    while (pos <= raw.size()) {
        size_t end = raw.find('\n', pos);
        if (end == std::string::npos)
            end = raw.size();
        std::string line = raw.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == '\0'))
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        line = percent_decode(line);
        const std::string scheme = "file://";
        if (line.rfind(scheme, 0) == 0) {
            line.erase(0, scheme.size());
            // file://host/path is rare; keep the simple file:///path case.
            if (!line.empty() && line[0] != '/')
                continue;
        }
        paths.push_back(line);
        if (pos > raw.size())
            break;
    }
    return paths;
}

} // namespace

Dnd::Dnd(Connection& conn, Events& events, Window& window)
    : mConn(conn), mEvents(events), mWindow(window) {
    if (!conn.data_manager() || !conn.seat())
        return;
    mManagerVersion = conn.data_manager_version();
    mDevice = wl_data_device_manager_get_data_device(conn.data_manager(), conn.seat());
    if (!mDevice)
        return;
    static const wl_data_device_listener listener = {
        handle_data_offer, handle_enter,   handle_leave,
        handle_motion,     handle_drop,     handle_selection,
    };
    wl_data_device_add_listener(mDevice, &listener, this);
}

void Dnd::destroy_offer(wl_data_offer*& offer) {
    if (offer) {
        wl_data_offer_destroy(offer);
        offer = nullptr;
    }
}

void Dnd::handle_data_offer(void* data, wl_data_device*, wl_data_offer* offer) {
    auto* self = static_cast<Dnd*>(data);
    // Only one offer is live at a time for our purposes.
    if (self->mPendingOffer && self->mPendingOffer != offer && self->mPendingOffer != self->mActiveOffer)
        self->destroy_offer(self->mPendingOffer);
    static const wl_data_offer_listener listener = {
        handle_mime_types, handle_source_actions, handle_action,
    };
    wl_data_offer_add_listener(offer, &listener, self);
    self->mPendingOffer = offer;
}

void Dnd::handle_enter(void* data, wl_data_device*, uint32_t, wl_surface* surface,
                       wl_fixed_t, wl_fixed_t, wl_data_offer* offer) {
    auto* self = static_cast<Dnd*>(data);
    if (self->mPendingOffer == offer)
        self->mPendingOffer = nullptr;
    if (self->mActiveOffer && self->mActiveOffer != offer)
        self->destroy_offer(self->mActiveOffer);
    self->mActiveOffer = offer;
    if (!offer || surface != self->mWindow.surface())
        return;
    wl_data_offer_set_actions(offer, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY,
                              WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
}

void Dnd::handle_leave(void* data, wl_data_device*) {
    auto* self = static_cast<Dnd*>(data);
    self->destroy_offer(self->mActiveOffer);
}

void Dnd::handle_motion(void*, wl_data_device*, uint32_t, wl_fixed_t, wl_fixed_t) {}

void Dnd::handle_drop(void* data, wl_data_device*) {
    auto* self = static_cast<Dnd*>(data);
    wl_data_offer* offer = self->mActiveOffer;
    self->mActiveOffer = nullptr;
    if (!offer)
        return;

    int fds[2] = {-1, -1};
    if (pipe2(fds, O_CLOEXEC) == 0) {
        wl_data_offer_receive(offer, "text/uri-list", fds[1]);
        close(fds[1]);
        const std::string raw = read_offer_fd(fds[0]);
        close(fds[0]);
        const std::vector<std::string> paths = parse_uri_list(raw);
        if (!paths.empty())
            self->mEvents.on_paths_dropped(paths);
    }
    if (self->mManagerVersion >= 3)
        wl_data_offer_finish(offer);
    wl_data_offer_destroy(offer);
}

void Dnd::handle_selection(void* data, wl_data_device*, wl_data_offer* offer) {
    auto* self = static_cast<Dnd*>(data);
    // Clipboard is not used; destroy the offer we are handed.
    if (offer && offer == self->mPendingOffer)
        self->mPendingOffer = nullptr;
    if (offer && offer != self->mActiveOffer)
        wl_data_offer_destroy(offer);
}

void Dnd::handle_mime_types(void*, wl_data_offer*, const char*) {}
void Dnd::handle_source_actions(void*, wl_data_offer*, uint32_t) {}
void Dnd::handle_action(void*, wl_data_offer*, uint32_t) {}

} // namespace aether
