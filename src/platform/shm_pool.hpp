#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <wayland-client.h>

namespace aether {

// A double/triple-buffered wl_shm pool of XRGB frames (alpha byte forced
// to 0xFF so the buffer is valid as either XRGB or premultiplied ARGB).
class ShmPool {
public:
    explicit ShmPool(wl_shm* shm);
    ~ShmPool();
    ShmPool(const ShmPool&) = delete;
    ShmPool& operator=(const ShmPool&) = delete;

    struct Slot {
        wl_buffer* buffer = nullptr;
        uint32_t* data = nullptr;
        int w = 0, h = 0;
        bool busy = false;
    };

    // Returns a free slot of exactly (w, h), recreating the pool if the
    // size changed. If every slot is held by the compositor, invokes
    // `wait` (which must dispatch events until a release arrives) and
    // retries; returns nullptr if `wait` reports failure. Max slots = 3.
    Slot* acquire(int w, int h, const std::function<bool()>& wait);

    static constexpr int kSlots = 3;

private:
    void destroy();
    bool create(int w, int h);

    static void handle_release(void* data, wl_buffer* buffer);

    wl_shm* mShm = nullptr;
    wl_shm_pool* mPool = nullptr;
    void* mMapped = nullptr;
    size_t mMappedSize = 0;
    int mFd = -1;
    int mW = 0, mH = 0;
    std::vector<Slot> mSlots;
};

} // namespace aether
