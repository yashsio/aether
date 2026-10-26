#pragma once

#include "image/image.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace aether {

// A codec knows how to decode and/or encode one or more file formats.
//
// To add a new format (e.g. WebP):
//   1. write a register_<fmt>_codec(std::vector<Codec>&) in its own TU
//   2. call it from codecs() in codecs.cpp, ideally behind a CMake option
//      (see the AETHER_WEBP scaffold in CMakeLists.txt)
struct Codec {
    const char* name;                    // "png", "jpg", ...
    std::vector<const char*> extensions; // lower-case, no dot
    // Decode raw file bytes into an RGBA image. Empty image = failure.
    std::function<Image(const uint8_t* data, size_t size, std::string* err)> decode;
    // Encode an RGBA image into this format's file bytes.
    std::function<std::vector<uint8_t>(const Image& img, std::string* err)> encode;
};

// All codecs compiled into this build.
const std::vector<Codec>& codecs();

// Try every decode-capable codec in registration order until one succeeds.
bool decode_image(const uint8_t* data, size_t size, Image* out, std::string* err);

// Codec that can encode files with the given extension (no dot), or nullptr.
const Codec* codec_for_extension(std::string_view ext);

// Union of all codec extensions, lower-case (for directory browsing).
const std::vector<std::string>& browse_extensions();

// Convenience: read a whole file. false on failure.
bool read_file(const std::string& path, std::vector<uint8_t>* out, std::string* err);

} // namespace aether
