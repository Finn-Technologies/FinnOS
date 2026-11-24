// Flathub API v2 JSON parser & Flatpak CLI command planner for Finn Store.
// Pure C++20 so it compiles into both finn-store and headless ASan/UBSan tests.

#ifndef FINN_STORE_FLATHUB_PROVIDER_HPP
#define FINN_STORE_FLATHUB_PROVIDER_HPP

#include "store_types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace finn::store {

inline constexpr const char* kFlathubApiBase = "https://flathub.org/api/v2";

// Parses Flathub /api/v2/search or /api/v2/collection/* JSON ("hits":[...])
// into StoreAppItem entries with verified badges & summaries.
std::vector<StoreAppItem> parse_flathub_hits_json(std::string_view json);

// Parses Flathub /api/v2/appstream/<app_id> JSON to enrich an item with
// screenshots, full description, version, and permissions.
bool enrich_flathub_appstream_json(std::string_view json, StoreAppItem* item);

// Parses `flatpak remote-ls --updates --columns=application,version` stdout
// into a list of updatable Flatpak app IDs.
std::vector<std::string> parse_flatpak_updates_output(std::string_view stdout_text);

}  // namespace finn::store

#endif  // FINN_STORE_FLATHUB_PROVIDER_HPP
