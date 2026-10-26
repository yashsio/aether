#include "image/codecs.hpp"

// stb is third-party code; keep -Wall -Wextra noise out of our build.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

namespace aether {

namespace {

Image decode_stb(const uint8_t* data, size_t size, std::string* err) {
    int w = 0, h = 0, comp = 0;
    if (!stbi_info_from_memory(data, static_cast<int>(size), &w, &h, &comp)) {
        if (err && err->empty())
            *err = "unrecognized image format";
        return {};
    }
    int nch = 0;
    unsigned char* rgba = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &nch, 4);
    if (!rgba) {
        if (err && err->empty())
            *err = stbi_failure_reason() ? stbi_failure_reason() : "decode failed";
        return {};
    }
    Image img = Image::create(w, h);
    std::memcpy(img.px.data(), rgba, img.px.size());
    stbi_image_free(rgba);
    return img;
}

std::vector<uint8_t> encode_png(const Image& img, std::string* err) {
    std::vector<uint8_t> out;
    const int ok = stbi_write_png_to_func(
        [](void* ctx, void* data, int size) {
            auto* v = static_cast<std::vector<uint8_t>*>(ctx);
            v->insert(v->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
        },
        &out, img.w, img.h, 4, img.px.data(), img.w * 4);
    if (!ok && err)
        *err = "png encode failed";
    return out;
}

std::vector<uint8_t> encode_bmp(const Image& img, std::string* err) {
    std::vector<uint8_t> out;
    const int ok = stbi_write_bmp_to_func(
        [](void* ctx, void* data, int size) {
            auto* v = static_cast<std::vector<uint8_t>*>(ctx);
            v->insert(v->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
        },
        &out, img.w, img.h, 4, img.px.data());
    if (!ok && err)
        *err = "bmp encode failed";
    return out;
}

std::vector<uint8_t> encode_tga(const Image& img, std::string* err) {
    std::vector<uint8_t> out;
    const int ok = stbi_write_tga_to_func(
        [](void* ctx, void* data, int size) {
            auto* v = static_cast<std::vector<uint8_t>*>(ctx);
            v->insert(v->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
        },
        &out, img.w, img.h, 4, img.px.data());
    if (!ok && err)
        *err = "tga encode failed";
    return out;
}

std::vector<uint8_t> encode_jpg(const Image& img, std::string* err) {
    std::vector<uint8_t> flat(static_cast<size_t>(img.w) * img.h * 3);
    for (size_t i = 0, p = 0; p < flat.size(); i += 4, p += 3) {
        const uint8_t a = img.px[i + 3];
        flat[p] = static_cast<uint8_t>(img.px[i] * a / 255);
        flat[p + 1] = static_cast<uint8_t>(img.px[i + 1] * a / 255);
        flat[p + 2] = static_cast<uint8_t>(img.px[i + 2] * a / 255);
    }
    std::vector<uint8_t> out;
    const int ok = stbi_write_jpg_to_func(
        [](void* ctx, void* data, int size) {
            auto* v = static_cast<std::vector<uint8_t>*>(ctx);
            v->insert(v->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
        },
        &out, img.w, img.h, 3, flat.data(), 92);
    if (!ok && err)
        *err = "jpg encode failed";
    return out;
}

void register_stb_codecs(std::vector<Codec>& v) {
    v.push_back({"png", {"png"}, decode_stb, encode_png});
    v.push_back({"jpeg", {"jpg", "jpeg", "jpe"}, decode_stb, encode_jpg});
    v.push_back({"bmp", {"bmp"}, decode_stb, encode_bmp});
    v.push_back({"tga", {"tga"}, decode_stb, encode_tga});
    v.push_back({"gif", {"gif"}, decode_stb, nullptr});
    v.push_back({"hdr", {"hdr"}, decode_stb, nullptr});
    v.push_back({"psd", {"psd"}, decode_stb, nullptr});
    v.push_back({"pnm", {"pnm", "ppm", "pgm", "pbm"}, decode_stb, nullptr});
    v.push_back({"pic", {"pic"}, decode_stb, nullptr});
    // Future formats go here, e.g.:
    // #ifdef AETHER_WEBP
    //     register_webp_codec(v);
    // #endif
}

} // namespace

#ifdef AETHER_WEBP
void register_webp_codec(std::vector<Codec>& v); // codec_webp.cpp
#endif

const std::vector<Codec>& codecs() {
    static const std::vector<Codec> all = [] {
        std::vector<Codec> v;
        register_stb_codecs(v);
#ifdef AETHER_WEBP
        register_webp_codec(v);
#endif
        return v;
    }();
    return all;
}

bool decode_image(const uint8_t* data, size_t size, Image* out, std::string* err) {
    for (const Codec& c : codecs()) {
        if (!c.decode)
            continue;
        std::string local;
        Image img = c.decode(data, size, &local);
        if (!img.empty()) {
            *out = std::move(img);
            return true;
        }
        if (err && err->empty())
            *err = local;
    }
    if (err && err->empty())
        *err = "no codec could decode the data";
    return false;
}

const Codec* codec_for_extension(std::string_view ext) {
    std::string lower(ext);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const Codec& c : codecs())
        for (const char* e : c.extensions)
            if (lower == e)
                return &c;
    return nullptr;
}

const std::vector<std::string>& browse_extensions() {
    static const std::vector<std::string> exts = [] {
        std::vector<std::string> v;
        for (const Codec& c : codecs())
            for (const char* e : c.extensions)
                v.emplace_back(e);
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
        return v;
    }();
    return exts;
}

bool read_file(const std::string& path, std::vector<uint8_t>* out, std::string* err) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        if (err)
            *err = "cannot open " + path;
        return false;
    }
    const std::streamsize size = f.tellg();
    f.seekg(0);
    out->resize(static_cast<size_t>(size));
    if (size > 0 && !f.read(reinterpret_cast<char*>(out->data()), size)) {
        if (err)
            *err = "read error: " + path;
        return false;
    }
    return true;
}

} // namespace aether
