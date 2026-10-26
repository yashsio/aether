#pragma once

#include "image/image.hpp"
#include "text/text.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace aether {

// Everything needed to compose one frame, in logical pixels except
// buf_w/buf_h which are physical buffer pixels (logical * buf_scale).
struct Frame {
    const std::vector<Image>* mips = nullptr; // full-res first, may be null
    const Font* font = nullptr;

    int log_w = 0, log_h = 0;
    int buf_scale = 1;
    int buf_w = 0, buf_h = 0;

    double scale = 1.0;
    double offx = 0.0, offy = 0.0;

    std::string meta;   // empty = hidden
    std::string help;   // empty = hidden
    std::string status; // empty = hidden
};

// Compose into `px` (XRGB8888, buf_w * buf_h, tightly packed, top-left origin).
void render_frame(const Frame& f, uint32_t* px);

} // namespace aether
