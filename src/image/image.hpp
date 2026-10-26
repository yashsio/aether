#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace aether {

// Straight (non-premultiplied) 8-bit RGBA, tightly packed row-major.
struct Image {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> px;

    bool empty() const { return px.empty(); }

    static Image create(int width, int height) {
        Image img;
        img.w = width;
        img.h = height;
        img.px.resize(static_cast<size_t>(width) * height * 4);
        return img;
    }

    uint8_t* row(int y) { return px.data() + static_cast<size_t>(y) * w * 4; }
    const uint8_t* row(int y) const { return px.data() + static_cast<size_t>(y) * w * 4; }
};

} // namespace aether
