#include "text/text.hpp"

// stb is third-party code; keep -Wall -Wextra noise out of our build.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cstring>

namespace aether {

namespace {

// Decode one UTF-8 codepoint at `i`, advance `i`. Returns 0xFFFD on error.
uint32_t next_cp(const std::string& s, size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    if (i + len > s.size())
        len = 1;
    uint32_t cp = c;
    if (len > 1) {
        cp = c & (0x7F >> (len + 1));
        for (size_t k = 1; k < len; ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    }
    i += len;
    return cp;
}

constexpr uint32_t kReplacement = '?';

} // namespace

Font::~Font() {
    delete static_cast<stbtt_fontinfo*>(mStbFont);
}

bool Font::load(const uint8_t* data, size_t size) {
    auto* info = new stbtt_fontinfo;
    if (!stbtt_InitFont(info, data, stbtt_GetFontOffsetForIndex(data, 0))) {
        delete info;
        return false;
    }
    delete static_cast<stbtt_fontinfo*>(mStbFont);
    mStbFont = info;
    mData = data;
    mSize = size;
    mSizes.clear();
    mInitialized = true;
    return true;
}

Font::Metrics& Font::ensure(int px) const {
    Metrics& m = mSizes[px];
    if (m.scale != 0)
        return m;
    auto* info = static_cast<stbtt_fontinfo*>(mStbFont);
    m.scale = stbtt_ScaleForPixelHeight(info, static_cast<float>(px));
    int a = 0, d = 0, gap = 0;
    stbtt_GetFontVMetrics(info, &a, &d, &gap);
    m.ascent = static_cast<int>(a * m.scale + 0.5f);
    m.descent = static_cast<int>(-d * m.scale + 0.5f);
    m.line_height = static_cast<int>((a - d + gap) * m.scale + 0.5f);
    if (m.line_height < px)
        m.line_height = px;
    return m;
}

const Font::Metrics& Font::metrics(int px) const {
    if (!mInitialized)
        mDummyMetrics = Metrics{};
    else
        ensure(px);
    return mInitialized ? mSizes.at(px) : mDummyMetrics;
}

int Font::ascent(int px) const { return metrics(px).ascent; }
int Font::descent(int px) const { return metrics(px).descent; }
int Font::line_height(int px) const { return metrics(px).line_height; }

const Font::Glyph* Font::glyph(int px, uint32_t cp) const {
    if (!mInitialized)
        return nullptr;
    Metrics& m = ensure(px);
    auto it = m.cache.find(cp);
    if (it != m.cache.end())
        return &it->second;

    auto* info = static_cast<stbtt_fontinfo*>(mStbFont);
    Glyph g;
    int gi = stbtt_FindGlyphIndex(info, static_cast<int>(cp));
    if (gi == 0 && cp != ' ')
        gi = stbtt_FindGlyphIndex(info, kReplacement);
    int adv = 0, lsb = 0;
    stbtt_GetGlyphHMetrics(info, gi, &adv, &lsb);
    g.advance = static_cast<int>(adv * m.scale + 0.5f);
    if (gi != 0 && cp != ' ') {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetGlyphBitmapBox(info, gi, m.scale, m.scale, &x0, &y0, &x1, &y1);
        g.w = x1 - x0;
        g.h = y1 - y0;
        g.xoff = x0;
        g.yoff = y0;
        if (g.w > 0 && g.h > 0) {
            g.alpha.resize(static_cast<size_t>(g.w) * g.h);
            stbtt_MakeGlyphBitmap(info, g.alpha.data(), g.w, g.h, g.w, m.scale, m.scale, gi);
        }
    }
    return &m.cache.emplace(cp, std::move(g)).first->second;
}

int Font::advance(int px, uint32_t cp) const {
    const Glyph* g = glyph(px, cp);
    return g ? g->advance : 0;
}

int Font::text_width(int px, const std::string& text) const {
    int w = 0;
    size_t i = 0;
    while (i < text.size()) {
        const uint32_t cp = next_cp(text, i);
        if (cp == '\n') {
            w = 0;
            continue;
        }
        w += advance(px, cp);
    }
    return w;
}

std::vector<std::string> Font::wrap(int px, const std::string& text, int max_width) const {
    std::vector<std::string> lines;
    if (max_width <= 0)
        return lines;

    std::string line;
    int line_w = 0;

    const auto flush = [&] {
        lines.push_back(std::move(line));
        line.clear();
        line_w = 0;
    };

    size_t i = 0;
    while (i < text.size()) {
        const size_t word_start = i;
        // Scan ahead without consuming the delimiter.
        while (i < text.size()) {
            size_t j = i;
            const uint32_t cp = next_cp(text, j);
            if (cp == '\n' || cp == ' ')
                break;
            i = j;
        }
        if (i > word_start) {
            const std::string word = text.substr(word_start, i - word_start);
            const int word_w = text_width(px, word);

            if (word_w > max_width) {
                // Word alone exceeds the box: hard-break on codepoints.
                if (!line.empty())
                    flush();
                std::string chunk;
                int chunk_w = 0;
                size_t k = 0;
                while (k < word.size()) {
                    const size_t start = k;
                    const uint32_t cp = next_cp(word, k);
                    const int a = advance(px, cp);
                    if (chunk_w + a > max_width && !chunk.empty()) {
                        flush();
                        chunk_w = 0;
                    }
                    chunk += word.substr(start, k - start);
                    chunk_w += a;
                }
                line = std::move(chunk);
                line_w = chunk_w;
                continue;
            }

            const int sep = line.empty() ? 0 : text_width(px, " ");
            if (!line.empty() && line_w + sep + word_w > max_width)
                flush();
            if (!line.empty()) {
                line += ' ';
                line_w += sep;
            }
            line += word;
            line_w += word_w;
            continue;
        }

        const uint32_t cp = next_cp(text, i); // consumed the delimiter
        if (cp == '\n') {
            flush();
        } else if (cp == ' ' && !line.empty()) {
            const int sep = text_width(px, " ");
            if (line_w + sep <= max_width) {
                line += ' ';
                line_w += sep;
            }
            // else: trailing space dropped, next word will trigger a flush
        }
    }
    if (!line.empty() || lines.empty())
        lines.push_back(std::move(line));
    return lines;
}

} // namespace aether
