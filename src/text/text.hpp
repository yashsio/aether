#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace aether {

// Rasterizes the embedded subsetted UI font on demand, per pixel size.
// Glyphs are cached as 8-bit coverage bitmaps.
class Font {
public:
    Font() = default;
    ~Font();
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    // `data` must outlive the Font (static storage).
    bool load(const uint8_t* data, size_t size);

    int ascent(int px) const;
    int descent(int px) const; // positive magnitude, below baseline
    int line_height(int px) const;

    int advance(int px, uint32_t cp) const;
    int text_width(int px, const std::string& text) const;

    // Greedy word wrap on spaces; explicit '\n' always breaks.
    std::vector<std::string> wrap(int px, const std::string& text, int max_width) const;

    struct Glyph {
        int w = 0, h = 0;     // bitmap size
        int xoff = 0, yoff = 0; // offset from pen position to top-left
        int advance = 0;
        std::vector<uint8_t> alpha; // w*h coverage
    };
    // nullptr for undecodable/blank codepoints (callers should skip).
    const Glyph* glyph(int px, uint32_t cp) const;

private:
    struct Metrics {
        float scale = 0;
        int ascent = 0, descent = 0, line_height = 0;
        std::unordered_map<uint32_t, Glyph> cache;
    };
    Metrics& ensure(int px) const;          // create/fill entry, mutable
    const Metrics& metrics(int px) const;   // for const getters

    const uint8_t* mData = nullptr;
    size_t mSize = 0;
    mutable bool mInitialized = false;
    mutable void* mStbFont = nullptr; // stbtt_fontinfo, heap-allocated
    mutable std::unordered_map<int, Metrics> mSizes;
    mutable Metrics mDummyMetrics;
};

} // namespace aether
