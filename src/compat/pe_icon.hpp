// Pure C++20 Windows PE (RT_GROUP_ICON / RT_ICON) and .ico icon extractor.
// Extracts the largest embedded PNG or DIB icon from a Windows .exe/.dll/.ico
// into a cache file (.png or .ico) that Qt6/FinnUI can render natively.

#ifndef FINN_COMPAT_PE_ICON_HPP
#define FINN_COMPAT_PE_ICON_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace finn::compat {

struct ExtractedIcon {
    bool ok = false;
    std::string ext;  // ".png" or ".ico"
    std::vector<std::uint8_t> bytes;
    std::string error;
};

// Extracts the best (largest resolution, highest bit depth) icon from raw PE
// bytes (.exe/.dll) or a standalone .ico buffer.
ExtractedIcon extract_pe_or_ico_icon_bytes(std::string_view bytes);

// Extracts the best icon from a local .exe, .dll, or .ico file and writes it
// into cache_dir/<slug>.{png,ico}. Returns the written file path on success, or
// an empty string if the file has no extractable icon.
std::string extract_and_cache_pe_icon(const std::string& exe_or_ico_path,
                                      const std::string& slug,
                                      const std::string& cache_dir = {});

}  // namespace finn::compat

#endif  // FINN_COMPAT_PE_ICON_HPP
