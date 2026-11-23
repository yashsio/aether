#include "image/codecs.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// nanosvg is third-party code; keep -Wall -Wextra noise out of our build.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif

#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"

#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace aether {

namespace {

// Rasterized SVGs are fixed-size bitmaps; cap the long edge so a hostile
// document (width="1000000") cannot force a huge allocation.
constexpr double kMaxSvgSide = 4096.0;

// Cheap sniff so we don't hand every non-SVG file to the XML parser.
bool looks_like_svg(const uint8_t* data, size_t size) {
    size_t i = 0;
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
        i = 3; // UTF-8 BOM
    while (i < size && (data[i] == ' ' || data[i] == '\t' || data[i] == '\r' ||
                        data[i] == '\n'))
        ++i;
    if (i >= size || data[i] != '<')
        return false;
    // The root can follow an XML declaration, comments or a doctype.
    const size_t end = std::min(size, i + 4096);
    for (size_t j = i; j + 4 <= end; ++j) {
        const unsigned char a = data[j + 1] | 0x20u;
        const unsigned char b = data[j + 2] | 0x20u;
        const unsigned char c = data[j + 3] | 0x20u;
        if (data[j] == '<' && a == 's' && b == 'v' && c == 'g')
            return true;
    }
    return false;
}

Image decode_svg(const uint8_t* data, size_t size, std::string* err) {
    if (!looks_like_svg(data, size))
        return {};

    // nsvgParse() mutates its input and requires a NUL terminator.
    std::vector<char> text(data, data + size);
    text.push_back('\0');

    NSVGimage* parsed = nsvgParse(text.data(), "px", 96.0f);
    if (!parsed) {
        if (err && err->empty())
            *err = "invalid svg";
        return {};
    }
    const std::unique_ptr<NSVGimage, void (*)(NSVGimage*)> svg(parsed, nsvgDelete);

    double fw = svg->width;
    double fh = svg->height;
    if (!(fw > 0.0) || !(fh > 0.0) || !std::isfinite(fw) || !std::isfinite(fh))
        fw = fh = 512.0; // no usable size or viewBox: pick a sane default

    const double scale = std::min(1.0, kMaxSvgSide / std::max(fw, fh));
    const auto dim = [](double v) {
        return std::max(1, static_cast<int>(std::min(kMaxSvgSide, std::ceil(v))));
    };
    const int w = dim(fw * scale);
    const int h = dim(fh * scale);

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        if (err && err->empty())
            *err = "svg rasterizer init failed";
        return {};
    }
    const std::unique_ptr<NSVGrasterizer, void (*)(NSVGrasterizer*)> guard(
        rast, nsvgDeleteRasterizer);

    Image img = Image::create(w, h);
    nsvgRasterize(rast, svg.get(), 0.0f, 0.0f, static_cast<float>(scale), img.px.data(),
                  w, h, w * 4);
    return img;
}

} // namespace

void register_svg_codec(std::vector<Codec>& v) {
    // SVG is vector: there is no lossless way to write it back in place, so
    // encode is intentionally null (the app saves rotations to a PNG sibling).
    v.push_back({"svg", {"svg"}, decode_svg, nullptr});
}

} // namespace aether
