#include "render/render.hpp"

#include <algorithm>
#include <cmath>

namespace aether {

namespace {

// Port of the Qt version's mipLevel().
int mip_index(const std::vector<Image>& levels, double target_scale) {
    if (target_scale >= 1.0)
        return 0;
    const int max_level = static_cast<int>(levels.size()) - 1;
    const int index = static_cast<int>(std::floor(-std::log2(target_scale)));
    return std::clamp(index, 0, max_level);
}

void draw_image(const Frame& f, uint32_t* px) {
    if (!f.mips || f.mips->empty())
        return;
    const std::vector<Image>& levels = *f.mips;
    const Image& lvl = levels[mip_index(levels, f.scale)];

    const double s = static_cast<double>(f.buf_scale);
    const double tx = f.offx * s;
    const double ty = f.offy * s;
    const double tw = levels.front().w * f.scale * s;
    const double th = levels.front().h * f.scale * s;
    if (!(tw > 0.0) || !(th > 0.0))
        return;

    const int x0 = std::max(0, static_cast<int>(tx));
    const int x1 = std::min(f.buf_w, static_cast<int>(std::ceil(tx + tw)));
    const int y0 = std::max(0, static_cast<int>(ty));
    const int y1 = std::min(f.buf_h, static_cast<int>(std::ceil(ty + th)));
    if (x1 <= x0 || y1 <= y0)
        return;

    const double du = lvl.w / tw; // source pixels per destination pixel
    const double dv = lvl.h / th;
    const double max_u = lvl.w - 1;
    const double max_v = lvl.h - 1;

    for (int y = y0; y < y1; ++y) {
        double v = (y + 0.5 - ty) * dv - 0.5;
        v = std::clamp(v, 0.0, max_v);
        const int vy0 = static_cast<int>(v);
        const int vy1 = std::min(vy0 + 1, lvl.h - 1);
        const double fy = v - vy0;
        const uint8_t* r0 = lvl.row(vy0);
        const uint8_t* r1 = lvl.row(vy1);
        uint32_t* o = px + static_cast<size_t>(y) * f.buf_w;

        double u = (x0 + 0.5 - tx) * du - 0.5;
        for (int x = x0; x < x1; ++x, u += du) {
            const double uc = std::clamp(u, 0.0, max_u);
            const int ux0 = static_cast<int>(uc);
            const int ux1 = std::min(ux0 + 1, lvl.w - 1);
            const double fx = uc - ux0;

            const uint8_t* p00 = r0 + static_cast<size_t>(ux0) * 4;
            const uint8_t* p01 = r0 + static_cast<size_t>(ux1) * 4;
            const uint8_t* p10 = r1 + static_cast<size_t>(ux0) * 4;
            const uint8_t* p11 = r1 + static_cast<size_t>(ux1) * 4;

            int out[4];
            for (int c = 0; c < 4; ++c) {
                const double top = p00[c] + (p01[c] - p00[c]) * fx;
                const double bot = p10[c] + (p11[c] - p10[c]) * fx;
                out[c] = static_cast<int>(top + (bot - top) * fy + 0.5);
            }
            // Straight alpha over the black backdrop, XRGB output.
            const int a = out[3];
            const int r = out[0] * a / 255;
            const int g = out[1] * a / 255;
            const int b = out[2] * a / 255;
            o[x] = 0xFF000000u | (static_cast<uint32_t>(r) << 16) |
                   (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
        }
    }
}

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
};

// Black rounded rectangle, source-over onto the opaque backdrop.
void fill_rounded(uint32_t* px, int W, int H, Rect r, int radius, int alpha) {
    if (alpha <= 0 || r.w <= 0 || r.h <= 0)
        return;
    const int x0 = std::max(0, r.x), x1 = std::min(W, r.x + r.w);
    const int y0 = std::max(0, r.y), y1 = std::min(H, r.y + r.h);
    if (x1 <= x0 || y1 <= y0)
        return;
    radius = std::min({radius, r.w / 2, r.h / 2});
    const int keep = 255 - alpha;
    const long r2 = static_cast<long>(radius) * radius;
    for (int y = y0; y < y1; ++y) {
        uint32_t* o = px + static_cast<size_t>(y) * W;
        for (int x = x0; x < x1; ++x) {
            if (radius > 0) {
                const int cx = (x < r.x + radius)   ? r.x + radius - x
                               : (x >= r.x + r.w - radius) ? x - (r.x + r.w - radius - 1)
                                                           : 0;
                const int cy = (y < r.y + radius)   ? r.y + radius - y
                               : (y >= r.y + r.h - radius) ? y - (r.y + r.h - radius - 1)
                                                           : 0;
                if (cx > 0 && cy > 0 &&
                    static_cast<long>(cx) * cx + static_cast<long>(cy) * cy > r2)
                    continue;
            }
            const uint32_t p = o[x];
            const int r_ = static_cast<int>(p >> 16 & 0xFF) * keep / 255;
            const int g_ = static_cast<int>(p >> 8 & 0xFF) * keep / 255;
            const int b_ = static_cast<int>(p & 0xFF) * keep / 255;
            o[x] = 0xFF000000u | (static_cast<uint32_t>(r_) << 16) |
                   (static_cast<uint32_t>(g_) << 8) | static_cast<uint32_t>(b_);
        }
    }
}

void draw_line(uint32_t* px, int W, int H, const Font& font, int fpx, int pen_x, int y,
               const std::string& line) {
    const int base = y + font.ascent(fpx);
    size_t i = 0;
    int x = pen_x;
    while (i < line.size()) {
        const unsigned char c = static_cast<unsigned char>(line[i]);
        size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        if (i + len > line.size())
            len = 1;
        uint32_t cp = c;
        if (len > 1) {
            cp = c & (0x7F >> (len + 1));
            for (size_t k = 1; k < len; ++k)
                cp = (cp << 6) | (static_cast<unsigned char>(line[i + k]) & 0x3F);
        }
        i += len;

        const Font::Glyph* g = font.glyph(fpx, cp);
        if (!g)
            continue;
        if (g->w > 0 && g->h > 0) {
            const int gx0 = x + g->xoff;
            const int gy0 = base + g->yoff;
            for (int yy = 0; yy < g->h; ++yy) {
                const int dy = gy0 + yy;
                if (dy < 0 || dy >= H)
                    continue;
                uint32_t* o = px + static_cast<size_t>(dy) * W;
                const uint8_t* arow = g->alpha.data() + static_cast<size_t>(yy) * g->w;
                for (int xx = 0; xx < g->w; ++xx) {
                    const int dx = gx0 + xx;
                    if (dx < 0 || dx >= W)
                        continue;
                    const int a = arow[xx];
                    if (a == 0)
                        continue;
                    const uint32_t p = o[dx];
                    const int r = static_cast<int>(p >> 16 & 0xFF) + ((255 - static_cast<int>(p >> 16 & 0xFF)) * a + 127) / 255;
                    const int g_ = static_cast<int>(p >> 8 & 0xFF) + ((255 - static_cast<int>(p >> 8 & 0xFF)) * a + 127) / 255;
                    const int b = static_cast<int>(p & 0xFF) + ((255 - static_cast<int>(p & 0xFF)) * a + 127) / 255;
                    o[dx] = 0xFF000000u | (static_cast<uint32_t>(r) << 16) |
                            (static_cast<uint32_t>(g_) << 8) | static_cast<uint32_t>(b);
                }
            }
        }
        x += g->advance;
    }
}

// Draws wrapped text in a rounded black panel. anchor: 0 = top-left,
// 1 = centered, 2 = bottom-left.
void draw_overlay(const Frame& f, uint32_t* px, const std::string& text, int base_px,
                  int anchor, int margin, int pad_x, int pad_y, int radius, int alpha) {
    if (text.empty() || !f.font || f.buf_w <= 0 || f.buf_h <= 0)
        return;
    const int s = f.buf_scale;
    const int fpx = base_px * s;
    const int max_text_w = std::max(1, f.buf_w - 2 * (margin * s + pad_x * s));
    const std::vector<std::string> lines = f.font->wrap(fpx, text, max_text_w);
    if (lines.empty())
        return;

    int text_w = 0;
    for (const auto& l : lines)
        text_w = std::max(text_w, f.font->text_width(fpx, l));
    const int line_h = f.font->line_height(fpx);

    Rect bg;
    bg.w = std::min(text_w + 2 * pad_x * s, f.buf_w);
    bg.h = std::min(static_cast<int>(lines.size()) * line_h + 2 * pad_y * s, f.buf_h);
    switch (anchor) {
    case 1: // centered
        bg.x = (f.buf_w - bg.w) / 2;
        bg.y = (f.buf_h - bg.h) / 2;
        break;
    case 2: // bottom-left
        bg.x = margin * s;
        bg.y = f.buf_h - margin * s - bg.h;
        break;
    default: // top-left
        bg.x = margin * s;
        bg.y = margin * s;
        break;
    }

    fill_rounded(px, f.buf_w, f.buf_h, bg, radius * s, alpha);
    int y = bg.y + pad_y * s;
    for (const auto& l : lines) {
        draw_line(px, f.buf_w, f.buf_h, *f.font, fpx, bg.x + pad_x * s, y, l);
        y += line_h;
    }
}

} // namespace

void render_frame(const Frame& f, uint32_t* px) {
    const size_t n = static_cast<size_t>(f.buf_w) * f.buf_h;
    std::fill(px, px + n, 0xFF000000u);

    draw_image(f, px);

    if (!f.meta.empty())
        draw_overlay(f, px, f.meta, 13, 0, 8, 6, 4, 4, 180);
    if (!f.help.empty())
        draw_overlay(f, px, f.help, 15, 1, 20, 10, 8, 6, 200);
    if (!f.status.empty())
        draw_overlay(f, px, f.status, 13, 2, 8, 6, 4, 4, 200);
}

} // namespace aether
