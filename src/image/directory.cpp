#include "image/directory.hpp"

#include "image/codecs.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

namespace aether {

bool natural_less(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const unsigned char ca = static_cast<unsigned char>(a[i]);
        const unsigned char cb = static_cast<unsigned char>(b[j]);
        if (std::isdigit(ca) && std::isdigit(cb)) {
            size_t ei = i, ej = j;
            while (ei < a.size() && std::isdigit(static_cast<unsigned char>(a[ei])))
                ++ei;
            while (ej < b.size() && std::isdigit(static_cast<unsigned char>(b[ej])))
                ++ej;
            // Skip leading zeros, compare length then lexicographically.
            size_t si = i, sj = j;
            while (si + 1 < ei && a[si] == '0')
                ++si;
            while (sj + 1 < ej && b[sj] == '0')
                ++sj;
            const size_t len_i = ei - si, len_j = ej - sj;
            if (len_i != len_j)
                return len_i < len_j;
            const int cmp = a.compare(si, len_i, b, sj, len_j);
            if (cmp != 0)
                return cmp < 0;
            if (ei - i != ej - j)
                return (ei - i) < (ej - j); // fewer leading zeros first
            i = ei;
            j = ej;
            continue;
        }
        if (ca != cb)
            return ca < cb;
        ++i;
        ++j;
    }
    if (a.size() - i != b.size() - j)
        return a.size() - i < b.size() - j;
    return false;
}

std::vector<std::string> image_files_in(const std::string& path) {
    const fs::path current = fs::absolute(fs::path(path)).lexically_normal();
    const std::vector<std::string>& exts = browse_extensions();

    std::vector<std::string> files;
    std::error_code ec;
    const fs::path dir = current.parent_path();
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec))
            continue;
        std::string ext = entry.path().extension().string();
        if (!ext.empty())
            ext.erase(0, 1);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (std::find(exts.begin(), exts.end(), ext) != exts.end())
            files.push_back(entry.path().lexically_normal().string());
    }

    const std::string cur = current.string();
    if (std::find(files.begin(), files.end(), cur) == files.end())
        files.push_back(cur);

    std::sort(files.begin(), files.end(), natural_less);
    return files;
}

} // namespace aether
