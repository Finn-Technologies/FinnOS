// Pure C++20 data types shared across Finn Store catalog providers,
// the Qt6 StoreController, and headless ASan/UBSan unit tests.

#ifndef FINN_STORE_TYPES_HPP
#define FINN_STORE_TYPES_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace finn::store {

enum class StoreEcosystem {
    Native,
    Flatpak,
    Android,
    Windows,
    Macos,
    Ohos,
};

inline const char* ecosystem_id(StoreEcosystem eco) {
    switch (eco) {
        case StoreEcosystem::Native:  return "native";
        case StoreEcosystem::Flatpak: return "flatpak";
        case StoreEcosystem::Android: return "android";
        case StoreEcosystem::Windows: return "windows";
        case StoreEcosystem::Macos:   return "macos";
        case StoreEcosystem::Ohos:    return "ohos";
    }
    return "native";
}

inline const char* ecosystem_label(StoreEcosystem eco) {
    switch (eco) {
        case StoreEcosystem::Native:  return "Native FinnOS";
        case StoreEcosystem::Flatpak: return "Linux (Flatpak)";
        case StoreEcosystem::Android: return "Android";
        case StoreEcosystem::Windows: return "Windows";
        case StoreEcosystem::Macos:   return "macOS";
        case StoreEcosystem::Ohos:    return "OpenHarmony";
    }
    return "Native FinnOS";
}

struct StoreAppItem {
    std::string id;            // Stable store ID (e.g. "files", "flatpak:org.mozilla.firefox", "android:org.thoughtcrime.securesms", "winget:7zip.7zip")
    std::string name;
    std::string subtitle;
    std::string description;
    std::string developer;
    std::string version;
    std::string category = "Utilities";
    StoreEcosystem ecosystem = StoreEcosystem::Native;
    std::string icon;          // Built-in icon name, abs file path, or https:// URL
    std::vector<std::string> screenshots;
    double rating = 0.0;       // 0.0 means unrated / not reported
    int review_count = 0;
    std::string size_label;
    bool verified = false;
    std::string verified_source;  // e.g. "FinnOS Core", "mozilla.org", "Play Store"
    bool installed = false;
    bool update_available = false;
    bool curated = false;
    bool featured_hero = false;
    std::string editorial_tag;    // e.g. "EDITOR'S CHOICE", "ESSENTIAL"
    std::string accent_hex = "#3b82f6";
    std::vector<std::string> permissions;
    std::string install_ref;      // Flathub ID, Android pkg, winget ID, or binary/bundle path
    std::string download_url;
    std::string sha256;
    std::string session_app_id;   // Matched AppRegistry::id when installed
};

struct LocalPackagePreview {
    bool valid = false;
    std::string path;
    std::string filename;
    std::string format_label;     // "FinnOS Native Bundle (.finnapp)", "Android APK", etc.
    StoreEcosystem ecosystem = StoreEcosystem::Native;
    std::string app_name;
    std::string app_id;
    std::string version;
    std::string summary;
    bool runtime_ready = false;
    std::string runtime_note;
    std::string error;
};

}  // namespace finn::store

#endif  // FINN_STORE_TYPES_HPP
