// Application catalogue, installed tracking, and unified Linux/Windows discovery.
// Preserves finn-session's line-oriented catalogue semantics while integrating
// Freedesktop .desktop / Flatpak entries, FinnSDK manifests, and Wine apps.

#ifndef FINN_SESSION_APP_REGISTRY_HPP
#define FINN_SESSION_APP_REGISTRY_HPP

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace finn {

enum class AppSource {
    kNative,
    kFlatpak,
    kWinget,
    kDesktop,
    kWindows,
    kMacos,
    kOhos,
    kUnknown,
};

struct App {
    std::string id;
    std::string name;
    std::string icon;
    std::string command;
    std::string note;
    std::string category = "System";
    std::string desktop_file;
    std::vector<std::string> mime_types;
    AppSource source = AppSource::kNative;
    bool installed = false;
    bool terminal = false;

    bool launchable() const;
};

const char* app_source_name(AppSource source);
AppSource parse_app_source(std::string_view text);

class AppRegistry {
public:
    // Loads the FinnOS catalogue and installed list (honoring empty/missing installed list).
    static std::optional<AppRegistry> load(const std::string& catalogue_path,
                                           const std::string& installed_path);

    // Loads catalogue + installed list + user FinnSDK manifests + Linux/Flatpak
    // .desktop applications + registered Windows executables.
    static AppRegistry load_all(const std::string& catalogue_path,
                                const std::string& installed_path);

    std::size_t size() const { return apps_.size(); }
    const std::vector<App>& apps() const { return apps_; }
    const App* find(std::string_view id) const;
    bool installed(std::string_view id) const;

    void merge_desktop_entries(bool check_try_exec = true);
    void merge_windows_entries(const std::string& windows_conf_path = {});
    void merge_macos_entries(const std::string& macos_apps_dir = {});
    void merge_ohos_entries(const std::string& ohos_registry_path = {});
    void merge_user_manifests(const std::string& manifests_dir = {});

private:
    std::vector<App> apps_;
};

}  // namespace finn

#endif
