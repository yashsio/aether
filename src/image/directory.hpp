#pragma once

#include <string>
#include <vector>

namespace aether {

// Natural (human) ordering: digit runs compare numerically, so
// img2 < img10. Case-sensitive tie-break, like a plain strcmp.
bool natural_less(const std::string& a, const std::string& b);

// All image files in the directory containing `path`, sorted naturally,
// with `path` guaranteed to be present (absolute, lexically normal).
std::vector<std::string> image_files_in(const std::string& path);

} // namespace aether
