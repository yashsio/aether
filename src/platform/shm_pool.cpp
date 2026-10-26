#include "platform/shm_pool.hpp"

#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace aether {

namespace {
constexpr int kSlotCount = ShmPool::kSlots;
} // namespace

ShmPool::ShmPool(wl_shm* shm) : mShm(shm) {}

ShmPool::~ShmPool() {
    destroy();
}

void ShmPool::handle_release(void* data, wl_buffer*) {
    static_cast<Slot*>(data)->busy = false;
}

void ShmPool::destroy() {
    for (Slot& s : mSlots)
        if (s.buffer)
            wl_buffer_destroy(s.buffer);
    mSlots.clear();
    if (mPool)
        wl_shm_pool_destroy(mPool);
    mPool = nullptr;
    if (mMapped && mMappedSize)
        munmap(mMapped, mMappedSize);
    mMapped = nullptr;
    mMappedSize = 0;
    if (mFd >= 0)
        close(mFd);
    mFd = -1;
    mW = mH = 0;
}

bool ShmPool::create(int w, int h) {
    destroy();
    const size_t stride = static_cast<size_t>(w) * 4;
    const size_t slot_size = stride * h;
    const size_t total = slot_size * kSlotCount;

    mFd = memfd_create("aether-shm", MFD_CLOEXEC);
    if (mFd < 0)
        return false;
    if (ftruncate(mFd, static_cast<off_t>(total)) != 0) {
        close(mFd);
        mFd = -1;
        return false;
    }
    mMapped = mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_SHARED, mFd, 0);
    if (mMapped == MAP_FAILED) {
        mMapped = nullptr;
        close(mFd);
        mFd = -1;
        return false;
    }
    mMappedSize = total;
    mPool = wl_shm_create_pool(mShm, mFd, static_cast<int32_t>(total));
    close(mFd); // server holds its own reference from the create request
    mFd = -1;
    if (!mPool) {
        destroy();
        return false;
    }

    mSlots.resize(kSlotCount);
    for (int i = 0; i < kSlotCount; ++i) {
        Slot& s = mSlots[i];
        s.w = w;
        s.h = h;
        s.busy = false;
        s.data = static_cast<uint32_t*>(mMapped) + static_cast<size_t>(i) * w * h;
        s.buffer = wl_shm_pool_create_buffer(mPool, static_cast<int32_t>(i * slot_size), w, h,
                                             static_cast<int32_t>(stride),
                                             WL_SHM_FORMAT_ARGB8888);
        if (!s.buffer) {
            destroy();
            return false;
        }
        static const wl_buffer_listener listener = {handle_release};
        wl_buffer_add_listener(s.buffer, &listener, &s);
    }
    mW = w;
    mH = h;
    return true;
}

ShmPool::Slot* ShmPool::acquire(int w, int h, const std::function<bool()>& wait) {
    if (w <= 0 || h <= 0)
        return nullptr;
    if (mW != w || mH != h || !mPool) {
        if (!create(w, h))
            return nullptr;
    }
    for (;;) {
        for (Slot& s : mSlots)
            if (!s.busy) {
                s.busy = true;
                return &s;
        }
        if (!wait() || !mPool)
            return nullptr;
        // Pool may have been destroyed by a resize that happened inside wait().
        if (mW != w || mH != h || !mPool) {
            if (!create(w, h))
                return nullptr;
        }
    }
}

} // namespace aether
