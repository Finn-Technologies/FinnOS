// Clean-room Apple .icns icon extractor.
// Extracts the highest-resolution embedded PNG payload from a macOS .icns
// container so installed .app bundles display real icons across FinnOS.

#ifndef FINN_DARWIN_ICNS_HPP
#define FINN_DARWIN_ICNS_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace finn::darwin {

// Extracts the best PNG image bytes from raw .icns data, or empty vector if none.
std::vector<std::uint8_t> extract_best_png_from_icns(std::string_view icns_bytes);

// Extracts the best PNG from icns_path and writes it to png_out_path.
bool extract_icns_to_png(const std::string& icns_path,
                         const std::string& png_out_path,
                         std::string* error = nullptr);

}  // namespace finn::darwin

#endif
