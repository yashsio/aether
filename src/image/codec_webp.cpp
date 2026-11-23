#include "image/codecs.hpp"

#include <webp/decode.h>
#include <webp/encode.h>

#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace aether {

namespace {

Image decode_webp(const uint8_t* data, size_t size, std::string* err) {
    WebPBitstreamFeatures features;
    if (WebPGetFeatures(data, size, &features) != VP8_STATUS_OK) {
        if (err && err->empty())
            *err = "invalid webp";
        return {};
    }
    if (features.width <= 0 || features.height <= 0)
        return {};

    // Guard the width * height * 4 arithmetic before allocating.
    const size_t pixels =
        static_cast<size_t>(features.width) * static_cast<size_t>(features.height);
    if (pixels > std::numeric_limits<size_t>::max() / 4)
        return {};

    int w = 0, h = 0;
    uint8_t* rgba = WebPDecodeRGBA(data, size, &w, &h);
    if (!rgba) {
        if (err && err->empty())
            *err = "webp decode failed";
        return {};
    }

    Image img = Image::create(w, h);
    std::memcpy(img.px.data(), rgba, img.px.size());
    WebPFree(rgba);
    return img;
}

std::vector<uint8_t> encode_webp(const Image& img, std::string* err) {
    if (img.empty())
        return {};

    WebPConfig config;
    if (!WebPConfigInit(&config)) {
        if (err)
            *err = "webp encoder init failed";
        return {};
    }
    config.lossless = 1;
    config.quality = 75; // effort/size knob; lossless is still bit-exact
    config.exact = 1;    // preserve RGB in fully transparent pixels

    WebPPicture picture;
    if (!WebPPictureInit(&picture)) {
        if (err)
            *err = "webp picture init failed";
        return {};
    }
    picture.width = img.w;
    picture.height = img.h;
    picture.use_argb = 1;
    if (!WebPPictureImportRGBA(&picture, img.px.data(), img.w * 4)) {
        WebPPictureFree(&picture);
        if (err)
            *err = "webp import failed";
        return {};
    }

    WebPMemoryWriter writer;
    WebPMemoryWriterInit(&writer);
    picture.writer = WebPMemoryWrite;
    picture.custom_ptr = &writer;

    const int ok = WebPEncode(&config, &picture);
    WebPPictureFree(&picture);

    std::vector<uint8_t> out;
    if (ok)
        out.assign(writer.mem, writer.mem + writer.size);
    WebPMemoryWriterClear(&writer);

    if (!ok && err)
        *err = "webp encode failed";
    return out;
}

} // namespace

void register_webp_codec(std::vector<Codec>& v) {
    // Lossless encode keeps rotation-save from degrading an existing WebP.
    v.push_back({"webp", {"webp"}, decode_webp, encode_webp});
}

} // namespace aether
