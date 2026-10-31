#include "session/app_registry.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>

namespace {

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,      \
                         __LINE__, #condition);                              \
            return false;                                                    \
        }                                                                    \
    } while (false)

class TempDir {
public:
    TempDir() {
        path_ = std::filesystem::temp_directory_path() /
                ("finn-registry-" + std::to_string(::getpid()));
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
        std::filesystem::create_directories(path_);
    }

    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    std::string write(std::string_view name, std::string_view content) const {
        const std::filesystem::path file = path_ / std::string(name);
        std::ofstream out{file};
        out << content;
        return file.string();
    }

    std::string path(std::string_view name) const {
        return (path_ / std::string(name)).string();
    }

private:
    std::filesystem::path path_;
};

bool test_catalogue_and_installed() {
    const TempDir dir;
    const std::string catalogue = dir.write(
        "apps.registry",
        "# the OS ships this\n"
        "settings | Settings | settings-app.svg | /usr/local/bin/finn-settings | Settings\n"
        "files | Finn Files | files.svg | /usr/local/bin/finn-files | File manager\n"
        "flatpak-gimp-x86_64 | GIMP | app-grid.svg |  | Image editor | flatpak\n"
        "winget-notepadpp | Notepad++ | app-grid.svg | | Text editor | winget\n"
        "short | line\n"
        "| No id | x.svg | /bin/x | n\n"
        "dup | First | a.svg | /bin/a | n\n"
        "dup | Second | b.svg | /bin/b | n\n");
    const std::string installed = dir.write(
        "installed", "settings\n  files  \n# not an id\nunknown\n");

    const auto registry = finn::AppRegistry::load(catalogue, installed);
    CHECK(registry.has_value());
    CHECK(registry->size() == 5);

    const finn::App* settings = registry->find("settings");
    CHECK(settings != nullptr);
    CHECK(settings->name == "Settings");
    CHECK(settings->icon == "settings-app.svg");
    CHECK(settings->command == "/usr/local/bin/finn-settings");
    CHECK(settings->note == "Settings");
    CHECK(settings->source == finn::AppSource::kNative);
    CHECK(settings->installed);
    CHECK(settings->launchable());

    const finn::App* files = registry->find("files");
    CHECK(files != nullptr);
    CHECK(files->installed);

    const finn::App* gimp = registry->find("flatpak-gimp-x86_64");
    CHECK(gimp != nullptr);
    CHECK(gimp->source == finn::AppSource::kFlatpak);
    CHECK(gimp->command.empty());
    CHECK(!gimp->launchable());
    CHECK(!gimp->installed);

    const finn::App* notepad = registry->find("winget-notepadpp");
    CHECK(notepad != nullptr);
    CHECK(notepad->source == finn::AppSource::kWinget);
    CHECK(!notepad->launchable());

    /* First duplicate wins; the second is not a second tile. */
    const finn::App* duplicate = registry->find("dup");
    CHECK(duplicate != nullptr);
    CHECK(duplicate->name == "First");

    CHECK(registry->installed("settings"));
    CHECK(!registry->installed("flatpak-gimp-x86_64"));
    CHECK(!registry->installed("missing"));
    CHECK(registry->find("missing") == nullptr);
    return true;
}

bool test_missing_installed_list() {
    const TempDir dir;
    const std::string catalogue = dir.write(
        "apps.registry", "settings | Settings | settings-app.svg | /bin/s | n\n");
    const auto registry = finn::AppRegistry::load(
        catalogue, dir.path("does-not-exist"));
    CHECK(registry.has_value());
    CHECK(registry->size() == 1);
    CHECK(!registry->installed("settings"));
    return true;
}

bool test_missing_catalogue() {
    const TempDir dir;
    const auto registry =
        finn::AppRegistry::load(dir.path("does-not-exist"), "");
    CHECK(!registry.has_value());
    return true;
}

bool test_fields_are_trimmed() {
    const TempDir dir;
    const std::string catalogue = dir.write(
        "apps.registry",
        "  files  |  Finn Files  |  files.svg  |  /usr/local/bin/finn-files  |  File manager  \n"
        "store | Finn Store | store.svg | /usr/local/bin/finn-store\n");
    const auto registry = finn::AppRegistry::load(catalogue, "");
    CHECK(registry.has_value());
    CHECK(registry->size() == 2);

    const finn::App* files = registry->find("files");
    CHECK(files != nullptr);
    CHECK(files->id == "files");
    CHECK(files->name == "Finn Files");
    CHECK(files->note == "File manager");

    const finn::App* store = registry->find("store");
    CHECK(store != nullptr);
    CHECK(store->note.empty());
    CHECK(store->source == finn::AppSource::kNative);
    return true;
}

bool test_overlong_line_is_skipped() {
    const TempDir dir;
    std::string catalogue = std::string(5000, 'a') + " | b | c | d | e\n";
    catalogue += "ok | Okay | ok.svg | /bin/ok | note\n";
    const auto registry = finn::AppRegistry::load(
        dir.write("apps.registry", catalogue), "");
    CHECK(registry.has_value());
    CHECK(registry->size() == 1);
    CHECK(registry->find("ok") != nullptr);
    return true;
}

bool test_unknown_source_is_not_native() {
    const TempDir dir;
    const auto registry = finn::AppRegistry::load(
        dir.write("apps.registry",
                  "app | App | a.svg | /bin/app | n | snap\n"),
        "");
    CHECK(registry.has_value());
    CHECK(registry->size() == 1);
    const finn::App* app = registry->find("app");
    CHECK(app != nullptr);
    CHECK(app->source == finn::AppSource::kUnknown);
    CHECK(!app->launchable());
    return true;
}

bool test_source_names() {
    CHECK(std::string_view(finn::app_source_name(finn::AppSource::kNative)) ==
          "native");
    CHECK(std::string_view(finn::app_source_name(finn::AppSource::kFlatpak)) ==
          "flatpak");
    CHECK(std::string_view(finn::app_source_name(finn::AppSource::kWinget)) ==
          "winget");
    CHECK(std::string_view(finn::app_source_name(finn::AppSource::kUnknown)) ==
          "unknown");
    return true;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_catalogue_and_installed();
    ok &= test_missing_installed_list();
    ok &= test_missing_catalogue();
    ok &= test_fields_are_trimmed();
    ok &= test_overlong_line_is_skipped();
    ok &= test_unknown_source_is_not_native();
    ok &= test_source_names();
    if (!ok) return 1;
    std::puts("test_app_registry: ok");
    return 0;
}
