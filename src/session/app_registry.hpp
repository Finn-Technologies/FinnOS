// App registry definitions representing discoverable and installed applications.
// Parses static system catalogues and correlates them with installed package states
// to inform application menus and validate launch requests.

#ifndef FINN_SESSION_APP_REGISTRY_HPP
#define FINN_SESSION_APP_REGISTRY_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace finn {

enum class AppSource { kNative, kFlatpak, kWinget, kUnknown };

const char* app_source_name(AppSource source);

struct App {
    std::string id;
    std::string name;
    std::string icon;
    std::string command;
    std::string note;
    AppSource source = AppSource::kNative;
    bool installed = false;

    /* A remote catalogue entry is a discovery record, not a launch. */
    bool launchable() const;
};

/*
 * The catalogue the OS ships and the list the package manager writes. The
 * catalogue says what exists; the installed list says what is on this
 * machine. A missing catalogue is an error; a missing installed list means
 * nothing is installed and is not.
 */
class AppRegistry {
public:
    static std::optional<AppRegistry> load(std::string_view catalogue_path,
                                           std::string_view installed_path);

    const std::vector<App>& apps() const { return apps_; }
    const App* find(std::string_view id) const;
    bool installed(std::string_view id) const;
    std::size_t size() const { return apps_.size(); }

private:
    std::vector<App> apps_;
};

}  // namespace finn

#endif
