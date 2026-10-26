#include "image/ops.hpp"

#include "image/codecs.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace aether {

std::vector<Image> build_mips(const Image& src) {
    std::vector<Image> levels;
    if (src.empty())
        return levels;
    levels.push_back(src);
    while (true) {
        const Image& cur = levels.back();
        const int nw = std::max(1, cur.w / 2);
        const int nh = std::max(1, cur.h / 2);
        if (nw == cur.w && nh == cur.h)
            break;
        Image next = Image::create(nw, nh);
        for (int y = 0; y < nh; ++y) {
            const int sy0 = std::min(y * 2, cur.h - 1);
            const int sy1 = std::min(sy0 + 1, cur.h - 1);
            const uint8_t* r0 = cur.row(sy0);
            const uint8_t* r1 = cur.row(sy1);
            uint8_t* o = next.row(y);
            for (int x = 0; x < nw; ++x) {
                const int sx0 = std::min(x * 2, cur.w - 1);
                const int sx1 = std::min(sx0 + 1, cur.w - 1);
                for (int c = 0; c < 4; ++c)
                    o[x * 4 + c] = static_cast<uint8_t>(
                        (r0[sx0 * 4 + c] + r0[sx1 * 4 + c] + r1[sx0 * 4 + c] + r1[sx1 * 4 + c]) / 4);
            }
        }
        levels.push_back(std::move(next));
    }
    return levels;
}

Image rotate_quarters(const Image& src, int quarter_turns) {
    quarter_turns = ((quarter_turns % 4) + 4) % 4;
    if (quarter_turns == 0 || src.empty())
        return src;

    const int W = src.w, H = src.h;
    const bool swapped = quarter_turns == 1 || quarter_turns == 3;
    Image dst = Image::create(swapped ? H : W, swapped ? W : H);

    for (int y = 0; y < dst.h; ++y) {
        uint8_t* o = dst.row(y);
        for (int x = 0; x < dst.w; ++x) {
            int sx, sy;
            switch (quarter_turns) {
            case 1: sx = y; sy = H - 1 - x; break;      // 90 CW
            case 2: sx = W - 1 - x; sy = H - 1 - y; break; // 180
            default: sx = W - 1 - y; sy = x; break;     // 270 CW
            }
            const uint8_t* p = src.row(sy) + sx * 4;
            o[x * 4 + 0] = p[0];
            o[x * 4 + 1] = p[1];
            o[x * 4 + 2] = p[2];
            o[x * 4 + 3] = p[3];
        }
    }
    return dst;
}

bool save_image(const std::string& path, const Image& img, std::string* err) {
    const std::filesystem::path p(path);
    std::string ext = p.extension().string();
    if (!ext.empty())
        ext.erase(0, 1); // strip dot
    const Codec* codec = codec_for_extension(ext);
    if (!codec || !codec->encode) {
        if (err)
            *err = "cannot re-encode '." + ext + "'";
        return false;
    }
    std::vector<uint8_t> bytes = codec->encode(img, err);
    if (bytes.empty())
        return false;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()))) {
        if (err)
            *err = "write failed: " + path;
        return false;
    }
    return true;
}

} // namespace aether
