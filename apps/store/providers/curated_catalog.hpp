// Curated out-of-the-box featured catalog for Finn Store across all four
// clearly separated ecosystems: Native FinnOS, Linux (Flatpak), Android, and Windows.

#ifndef FINN_STORE_CURATED_CATALOG_HPP
#define FINN_STORE_CURATED_CATALOG_HPP

#include "store_types.hpp"

#include <vector>

namespace finn::store {

std::vector<StoreAppItem> build_curated_catalog();

}  // namespace finn::store

#endif  // FINN_STORE_CURATED_CATALOG_HPP
