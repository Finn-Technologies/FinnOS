// Aurora Store-style Google Play FDFE backend for Finn Store.
// Supports anonymous dispenser token acquisition, Protobuf-encoded FDFE
// search/details/delivery parsing (including base + split APK download URLs),
// and finn-android handoff. Pure C++20 parsers with zero Qt dependency.

#ifndef FINN_STORE_AURORA_PLAY_PROVIDER_HPP
#define FINN_STORE_AURORA_PLAY_PROVIDER_HPP

#include "store_types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace finn::store {

inline constexpr const char* kDefaultAuroraDispenserUrl = "https://auroraoss.com/api/auth";
inline constexpr const char* kGooglePlayFdfeBaseUrl = "https://android.clients.google.com/fdfe";

struct AuroraAuthSession {
    bool authenticated = false;
    std::string email;
    std::string auth_token;
    std::string gsf_id;
    std::string device_checkin_consistency_token;
    std::string locale = "en_US";
    std::string error;
};

struct PlaySplitFile {
    std::string name;          // e.g. "base.apk", "config.arm64_v8a.apk", "config.xxhdpi.apk"
    std::string download_url;
    std::string sha256_b64;
    std::uint64_t size_bytes = 0;
};

struct PlayDeliveryBundle {
    bool ok = false;
    std::string package_name;
    std::uint32_t version_code = 0;
    std::string cookie_name;
    std::string cookie_value;
    PlaySplitFile base_apk;
    std::vector<PlaySplitFile> splits;
    std::string error;
};

// Builds the JSON device-properties payload expected by an Aurora anonymous
// token dispenser (Pixel 9 / Android 15 x86_64 or arm64-v8a profile).
std::string build_aurora_device_properties_json(std::string_view abi = "arm64-v8a");

// Parses the JSON response from an Aurora anonymous token dispenser.
AuroraAuthSession parse_aurora_dispenser_json(std::string_view json);

// Builds standard Google Play FDFE HTTP headers for an authenticated Aurora session.
std::vector<std::pair<std::string, std::string>> build_play_fdfe_headers(
    const AuroraAuthSession& session);

// Parses a Google Play FDFE ResponseWrapper Protobuf containing Item/DocV2
// search results or app details into StoreAppItem records.
std::vector<StoreAppItem> parse_play_fdfe_items_protobuf(std::string_view raw_pb);

// Parses a Google Play FDFE DeliveryResponse (or BuyResponse) Protobuf
// containing base APK + split APK download URLs and download auth cookies.
PlayDeliveryBundle parse_play_delivery_protobuf(std::string_view raw_pb,
                                                const std::string& package_name);

}  // namespace finn::store

#endif  // FINN_STORE_AURORA_PLAY_PROVIDER_HPP
