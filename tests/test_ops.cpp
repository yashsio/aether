#include "font_data.hpp"
#include "image/codecs.hpp"
#include "image/directory.hpp"
#include "image/image.hpp"
#include "image/ops.hpp"
#include "render/render.hpp"
#include "text/text.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            ++g_failures;                                                               \
        }                                                                               \
    } while (0)

using aether::Image;

// A pixel with distinct, recognizable channel bytes.
void put(Image& img, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint8_t* p = img.row(y) + x * 4;
    p[0] = r;
    p[1] = g;
    p[2] = b;
    p[3] = a;
}

void get(const Image& img, int x, int y, uint8_t out[4]) {
    std::memcpy(out, img.row(y) + x * 4, 4);
}

bool pixels_equal(const Image& a, const Image& b) {
    return a.w == b.w && a.h == b.h && a.px == b.px;
}

void test_natural_less() {
    using aether::natural_less;
    CHECK(natural_less("img2.png", "img10.png"));
    CHECK(!natural_less("img10.png", "img2.png"));
    CHECK(!natural_less("img2.png", "img2.png"));
    CHECK(natural_less("img1.png", "img2.png"));
    // equal numeric runs: fewer leading zeros first
    CHECK(natural_less("img2", "img002"));
    CHECK(!natural_less("img002", "img2"));
    // numeric decision wins before suffixes are compared
    CHECK(natural_less("img2a", "img10b"));
    CHECK(natural_less("a", "ab"));
    CHECK(!natural_less("ab", "a"));
    // case-sensitive tie-break like strcmp
    CHECK(natural_less("A.png", "a.png"));
}

void test_rotate_quarters() {
    // 2x1 row: A B. 90 CW -> column A (top), B (bottom).
    Image row = Image::create(2, 1);
    put(row, 0, 0, 10, 11, 12, 13);
    put(row, 1, 0, 20, 21, 22, 23);

    Image cw = aether::rotate_quarters(row, 1);
    CHECK(cw.w == 1 && cw.h == 2);
    uint8_t p[4];
    get(cw, 0, 0, p);
    CHECK(p[0] == 10 && p[3] == 13);
    get(cw, 0, 1, p);
    CHECK(p[0] == 20 && p[3] == 23);

    // 180 keeps dims, flips both axes.
    Image h = aether::rotate_quarters(row, 2);
    CHECK(h.w == 2 && h.h == 1);
    get(h, 0, 0, p);
    CHECK(p[0] == 20);
    get(h, 1, 0, p);
    CHECK(p[0] == 10);

    // 4x2 rectangle through a full turn: every pixel returns home.
    Image rect = Image::create(4, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            put(rect, x, y, static_cast<uint8_t>(x), static_cast<uint8_t>(y + 10), 0, 255);
    CHECK(pixels_equal(aether::rotate_quarters(rect, 4), rect));
    // 270 CW then 90 CW == identity.
    Image back = aether::rotate_quarters(aether::rotate_quarters(rect, 3), 1);
    CHECK(pixels_equal(back, rect));
    // 90 four times == identity (and the intermediate dims swap properly).
    Image r = rect;
    for (int i = 0; i < 4; ++i)
        r = aether::rotate_quarters(r, 1);
    CHECK(pixels_equal(r, rect));

    // Non-square regression: rotation must stay in bounds (90 CW of a
    // 3x1 is a 1x3 column: src(0)->dst top).
    Image wide = Image::create(3, 1);
    put(wide, 0, 0, 1, 0, 0, 255);
    put(wide, 1, 0, 2, 0, 0, 255);
    put(wide, 2, 0, 3, 0, 0, 255);
    Image col = aether::rotate_quarters(wide, 1);
    CHECK(col.w == 1 && col.h == 3);
    get(col, 0, 0, p);
    CHECK(p[0] == 1);
    get(col, 0, 2, p);
    CHECK(p[0] == 3);
}

void test_build_mips() {
    Image src = Image::create(8, 4);
    // Checker-ish known values.
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 8; ++x) {
            const uint8_t v = static_cast<uint8_t>((y * 8 + x) * 4);
            put(src, x, y, v, v, v, 255);
        }

    const std::vector<Image> mips = aether::build_mips(src);
    CHECK(mips.size() == 4); // 8x4 -> 4x2 -> 2x1 -> 1x1
    CHECK(mips[0].w == 8 && mips[0].h == 4);
    CHECK(mips[1].w == 4 && mips[1].h == 2);
    CHECK(mips[2].w == 2 && mips[2].h == 1);
    CHECK(mips[3].w == 1 && mips[3].h == 1);
    CHECK(pixels_equal(mips[0], src));

    // Level 1 pixel (0,0) = average of src 2x2 block (0,0),(1,0),(0,1),(1,1):
    // values 0, 4, 32, 36 -> mean 18.
    uint8_t p[4];
    get(mips[1], 0, 0, p);
    CHECK(p[0] == 18);

    // Odd dimensions clamp instead of falling off the edge: 5 -> 2 -> 1.
    Image odd = Image::create(5, 3);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x)
            put(odd, x, y, 100, 100, 100, 255);
    const std::vector<Image> omips = aether::build_mips(odd);
    CHECK(omips.size() == 3);
    CHECK(omips[1].w == 2 && omips[1].h == 1);
    CHECK(omips[2].w == 1 && omips[2].h == 1);
    get(omips[1], 1, 0, p);
    CHECK(p[0] == 100); // clamped duplicate columns stay uniform

    CHECK(aether::build_mips(Image{}).empty());
}

void test_codec_roundtrip() {
    Image src = Image::create(16, 9);
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 16; ++x)
            put(src, x, y, static_cast<uint8_t>(x * 16), static_cast<uint8_t>(y * 28),
                static_cast<uint8_t>(x ^ y), 255);

    // Lossless formats: exact roundtrip through the codec registry.
    for (const char* ext : {"png", "bmp", "tga"}) {
        const aether::Codec* codec = aether::codec_for_extension(ext);
        CHECK(codec != nullptr);
        if (!codec || !codec->encode)
            continue;
        std::string err;
        const std::vector<uint8_t> bytes = codec->encode(src, &err);
        CHECK(!bytes.empty());
        Image out;
        CHECK(aether::decode_image(bytes.data(), bytes.size(), &out, &err));
        CHECK(pixels_equal(out, src));
    }

    // Extension lookup is case-insensitive.
    CHECK(aether::codec_for_extension("PNG") != nullptr);
    CHECK(aether::codec_for_extension("JpEg") != nullptr);
    CHECK(aether::codec_for_extension("webp") == nullptr); // intentionally absent
    CHECK(aether::codec_for_extension("xyz") == nullptr);

    // JPEG: decode must succeed with matching dims (lossy content).
    {
        const aether::Codec* jpg = aether::codec_for_extension("jpg");
        CHECK(jpg && jpg->encode);
        if (jpg && jpg->encode) {
            std::string err;
            const std::vector<uint8_t> bytes = jpg->encode(src, &err);
            CHECK(!bytes.empty());
            Image out;
            CHECK(aether::decode_image(bytes.data(), bytes.size(), &out, &err));
            CHECK(out.w == src.w && out.h == src.h);
        }
    }

    // Garbage input fails cleanly with a message.
    {
        const uint8_t junk[] = {0xde, 0xad, 0xbe, 0xef, 0x00, 0x11, 0x22};
        Image out;
        std::string err;
        CHECK(!aether::decode_image(junk, sizeof(junk), &out, &err));
        CHECK(!err.empty());
        CHECK(out.empty());
    }
}

void test_save_image() {
    Image src = Image::create(4, 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            put(src, x, y, static_cast<uint8_t>(x * 60), static_cast<uint8_t>(y * 60), 7, 255);

    const char* path = "aether_test_roundtrip.png";
    std::string err;
    CHECK(aether::save_image(path, src, &err));

    std::vector<uint8_t> bytes;
    CHECK(aether::read_file(path, &bytes, &err));
    Image back;
    CHECK(aether::decode_image(bytes.data(), bytes.size(), &back, &err));
    CHECK(pixels_equal(back, src));
    std::remove(path);

    // Unknown extension fails with a message, not a crash.
    CHECK(!aether::save_image("nope.xyz", src, &err));
    CHECK(!err.empty());
}

void test_read_file() {
    std::vector<uint8_t> bytes;
    std::string err;
    CHECK(!aether::read_file("aether_definitely_missing_file.png", &bytes, &err));
    CHECK(!err.empty());
}

// The window surface is ARGB8888 with no compositor blur; every pixel we
// emit must be fully opaque (alpha byte 0xFF) or the window shows through.
void test_render_is_opaque() {
    const int W = 320, H = 200;
    std::vector<uint32_t> px(static_cast<size_t>(W) * H, 0xDEADBEEFu); // poison

    Image img = Image::create(64, 48);
    for (int y = 0; y < 48; ++y)
        for (int x = 0; x < 64; ++x)
            put(img, x, y, 200, 100, 50, 255); // opaque source
    const std::vector<Image> mips = aether::build_mips(img);

    aether::Font font;
    font.load(kFontData, sizeof(kFontData));

    aether::Frame f;
    f.mips = &mips;
    f.font = &font;
    f.log_w = W;
    f.log_h = H;
    f.buf_scale = 1;
    f.buf_w = W;
    f.buf_h = H;
    f.scale = 2.0; // image larger than window: covers everything
    f.offx = -10;
    f.offy = -10;
    f.meta = "File: a.png\nResolution: 64 x 48";
    f.help = "Keys:\n  F: fit";
    f.status = "saved";

    aether::render_frame(f, px.data());

    size_t transparent = 0;
    for (uint32_t p : px)
        if ((p >> 24) != 0xFFu)
            ++transparent;
    CHECK(transparent == 0);

    // A smaller-than-window image must still produce an opaque backdrop
    // (the letterbox area is background black, not transparent).
    std::vector<uint32_t> px2(static_cast<size_t>(W) * H, 0);
    aether::Frame f2 = f;
    f2.scale = 0.5;
    f2.offx = W / 4.0;
    f2.offy = H / 4.0;
    f2.meta.clear();
    f2.help.clear();
    f2.status.clear();
    aether::render_frame(f2, px2.data());
    for (uint32_t p : px2)
        CHECK((p >> 24) == 0xFFu);
}

} // namespace

int main() {
    test_natural_less();
    test_rotate_quarters();
    test_build_mips();
    test_codec_roundtrip();
    test_save_image();
    test_read_file();
    test_render_is_opaque();
    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
