#pragma once

#include "image/image.hpp"

#include <string>
#include <vector>

namespace aether {

// Build the full mip pyramid: level 0 is a copy of src, each following
// level is an exact 2x2 box halving (edge-clamped for odd dimensions).
std::vector<Image> build_mips(const Image& src);

// Rotate by a multiple of 90 degrees clockwise: quarter_turns in 0..3.
// Pure pixel transpose, lossless.
Image rotate_quarters(const Image& src, int quarter_turns);

// Encode `img` to `path` using the codec for `path`'s extension.
// Returns false (with *err set) if the extension has no encoder.
bool save_image(const std::string& path, const Image& img, std::string* err);

} // namespace aether
