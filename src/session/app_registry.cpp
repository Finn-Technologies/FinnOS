// Parser for apps.registry and the installed application list.
// Separates catalogue discovery metadata from machine installation states,
// ensuring only validated native applications are marked as launchable.

#include "session/app_registry.hpp"

#include <array>
#include <cctype>
#include <fstream>
#include <utility>

namespace finn {
namespace {

constexpr std::size_t kMaxFields = 6;
constexpr std::size_t kMaxLine = 4096;

struct LineFields {
    std::array<std::string_view, kMaxFields> values{};
    std::size_t count = 0;
};

std::string_view trim(std::string_view text) {
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

// Splits pipe-delimited fields while trimming whitespace to protect against
// formatting variance in manually edited or package-generated registry files.
LineFields split_fields(std::string_view line) {
    LineFields fields;
    while (fields.count < kMaxFields) {
        const std::size_t bar = line.find('|');
        fields.values[fields.count++] = trim(line.substr(0, bar));
        if (bar == std::string_view::npos) break;
        line.remove_prefix(bar + 1);
    }
    return fields;
}

AppSource parse_source(std::string_view text) {
    if (text.empty() || text == "native") return AppSource::kNative;
    if (text == "flatpak") return AppSource::kFlatpak;
    if (text == "winget") return AppSource::kWinget;
    return AppSource::kUnknown;
}

// Reads installed IDs line by line; non-existent files indicate no packages
// have been installed yet rather than an operational failure.
void mark_installed(std::vector<App>& apps, std::string_view path) {
    std::ifstream list{std::string(path)};
    if (!list) return;

    std::string line;
    while (std::getline(list, line)) {
        const std::string_view id = trim(line);
        if (id.empty() || id.front() == '#') continue;
        for (App& app : apps) {
            if (app.id == id) app.installed = true;
        }
    }
}

}  // namespace

const char* app_source_name(AppSource source) {
    switch (source) {
        case AppSource::kNative: return "native";
        case AppSource::kFlatpak: return "flatpak";
        case AppSource::kWinget: return "winget";
        case AppSource::kUnknown: return "unknown";
    }
    return "unknown";
}

bool App::launchable() const {
    return source == AppSource::kNative && !command.empty();
}

std::optional<AppRegistry> AppRegistry::load(std::string_view catalogue_path,
                                             std::string_view installed_path) {
    std::ifstream catalogue{std::string(catalogue_path)};
    if (!catalogue) return std::nullopt;

    AppRegistry registry;
    std::string line;
    while (std::getline(catalogue, line)) {
        const std::string_view text = trim(line);
        if (text.empty() || text.front() == '#') continue;
        if (text.size() > kMaxLine) continue;

        const LineFields fields = split_fields(text);
        if (fields.count < 3) continue;
        if (fields.values[0].empty()) continue;

        /* The first entry for an id wins; a duplicate is a catalogue error
         * that must not become two tiles. */
        if (registry.find(fields.values[0]) != nullptr) continue;

        App app;
        app.id = std::string(fields.values[0]);
        app.name = std::string(fields.values[1]);
        app.icon = std::string(fields.values[2]);
        if (fields.count > 3) app.command = std::string(fields.values[3]);
        if (fields.count > 4) app.note = std::string(fields.values[4]);
        if (fields.count > 5) app.source = parse_source(fields.values[5]);
        registry.apps_.push_back(std::move(app));
    }

    if (!installed_path.empty()) mark_installed(registry.apps_, installed_path);
    return registry;
}

const App* AppRegistry::find(std::string_view id) const {
    for (const App& app : apps_) {
        if (app.id == id) return &app;
    }
    return nullptr;
}

bool AppRegistry::installed(std::string_view id) const {
    const App* app = find(id);
    return app != nullptr && app->installed;
}

}  // namespace finn
